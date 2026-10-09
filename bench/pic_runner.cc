// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

// Bench runner for PIC firmware, on gpsim's simulation library.
//
// Usage: pic_runner <image.hex> <processor> <clock_hz> <timeline> <trace_out>
//                   <coverage_out> <duration_ns> <pin_map>
//
// It plays a console timeline (tools/bench/timeline.py) into one PIC image and
// writes what the chip did to its output pins, plus every program address it
// executed. The pin map names package pins, for example
// "wfck=5,lid=7,reset=4,sense=3,data=6,gate=5"; a leading '!' inverts a line
// whose polarity is the console's opposite. gpsim counts instruction cycles,
// four clocks each on these cores, so times convert with 4e9 / clock_hz.
//
// Three gpsim behaviours shaped this file (measured 2026-10-07): step_one() never advances the
// cycle counter, step(1) does; a pin's putState(), setDrivenState() and forceDrivenState() do not
// reach the GPIO register the firmware reads, a stimulus on the pin's node
// does; and the 12-bit cores' TRIS is not a loggable register, so the output
// direction is read from the pin itself after every instruction.

#include <gpsim/gpsim_time.h>
#include <gpsim/interface.h>
#include <gpsim/ioports.h>
#include <gpsim/pic-processor.h>
#include <gpsim/processor.h>
#include <gpsim/registers.h>
#include <gpsim/sim_context.h>
#include <gpsim/stimuli.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

// A console line driving one input pin through the pin's node, the path gpsim
// propagates to the port register. The 50 ohm source overrides any internal
// pull-up, like a real console output would.
class Drive : public stimulus {
 public:
  explicit Drive(const char *name) : stimulus(name) {
  }
  void set(bool value) {
    if (value != level_) {
      level_ = value;
      updateNode();
    }
  }
  void getThevenin(double &v, double &z, double &c) override {
    v = level_ ? 5.0 : 0.0;
    z = 50.0;
    c = 0.0;
  }
  double get_Vth() override {
    return level_ ? 5.0 : 0.0;
  }

 private:
  bool level_ = true;
};

struct ConsoleEvent {
  unsigned long long time_ns;
  std::string signal;
  long value;
};

struct Line {
  int pin = 0;
  bool inverted = false;
};

std::map<std::string, Line> parse_pin_map(const char *text) {
  std::map<std::string, Line> lines;
  std::stringstream all(text);
  std::string item;
  while (std::getline(all, item, ',')) {
    size_t eq = item.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    Line line;
    std::string pin = item.substr(eq + 1);
    if (!pin.empty() && pin[0] == '!') {
      line.inverted = true;
      pin = pin.substr(1);
    }
    line.pin = std::atoi(pin.c_str());
    lines[item.substr(0, eq)] = line;
  }
  return lines;
}

std::vector<ConsoleEvent> read_timeline(const char *path) {
  std::vector<ConsoleEvent> events;
  std::ifstream in(path);
  ConsoleEvent event;
  while (in >> event.time_ns >> event.signal >> event.value) {
    events.push_back(event);
  }
  return events;
}

}  // namespace

int main(int argc, char **argv) {
  if (argc != 9) {
    std::fprintf(stderr,
                 "usage: %s image processor clock_hz timeline trace coverage duration_ns pins\n",
                 argv[0]);
    return 2;
  }
  const double clock_hz = std::atof(argv[3]);
  const double ns_per_cycle = 4.0e9 / clock_hz;
  const unsigned long long duration_ns = std::strtoull(argv[7], nullptr, 10);
  const std::map<std::string, Line> pins = parse_pin_map(argv[8]);
  const std::vector<ConsoleEvent> events = read_timeline(argv[4]);

  initialize_gpsim_core();
  CSimulationContext *context = CSimulationContext::GetContext();
  context->Initialize();
  Processor *cpu = nullptr;
  if (!context->LoadProgram(argv[1], argv[2], &cpu) || cpu == nullptr) {
    std::fprintf(stderr, "cannot load %s as %s\n", argv[1], argv[2]);
    return 1;
  }
  cpu->set_frequency(clock_hz);

  // A real part leaves the factory with its oscillator calibration in the last
  // program word: a MOVLW on the 12C508 (DS40139, section 7.2.5, executed as
  // the reset vector) and a RETLW on the 12F629 (DS41190, section 9.2.5.1,
  // reached by CALL 0x3FF). An image dumped from source leaves that word
  // erased, all ones, which on the 12F629 is no return at all, so the program
  // calls it and never comes back. The word is filled with the centre value,
  // 0x80, only when the image left it erased, so an image that carries its own
  // word runs unchanged.
  const unsigned int last = cpu->program_memory_size() - 1U;
  const std::string processor = argv[2];
  const unsigned int erased = processor == "p12f629" ? 0x3FFFU : 0xFFFU;
  const unsigned int factory = processor == "p12f629" ? 0x3480U : 0xC80U;
  if (cpu->get_program_memory_at_address(last) == erased) {
    cpu->init_program_memory(last, factory);
  }
  // The 16C5x starts from the last program word, its reset vector (PIC16C5X
  // data sheet DS30453, reset and program memory map); gpsim's 16C54 model
  // starts from 0x000, which runs a program's first routine before its start
  // code has set OPTION. The reset address is set to the part's own and the
  // core reset once, so the run begins where the silicon does.
  if (processor == "p16c54") {
    cpu->pc->set_reset_address(last);
    cpu->reset(POR_RESET);
  }

  // Every input line gets its own node and source. Lines start at the
  // console's idle levels: WFCK high (the gate's resting state), lid closed,
  // reset released and the sense line high, which is XLAT's idle level; a
  // scenario that wires it to the SPEED level sets it at time zero.
  std::map<std::string, Drive *> drives;
  const char *inputs[] = { "sqck", "subq", "wfck", "lid", "reset", "sense" };
  for (const char *name : inputs) {
    auto found = pins.find(name);
    if (found == pins.end() || found->second.pin == 0) {
      continue;
    }
    IOPIN *pin = cpu->get_pin(found->second.pin);
    Stimulus_Node *node = new Stimulus_Node((std::string(name) + "_node").c_str());
    Drive *drive = new Drive((std::string(name) + "_drive").c_str());
    node->attach_stimulus(pin);
    node->attach_stimulus(drive);
    drives[name] = drive;
  }
  auto drive_line = [&](const std::string &name, bool value) {
    auto drive = drives.find(name);
    if (drive != drives.end()) {
      drive->second->set(value != pins.at(name).inverted);
    }
  };
  drive_line("wfck", true);
  drive_line("lid", false);
  drive_line("reset", true);
  drive_line("sense", true);
  drive_line("sqck", true);

  std::FILE *trace = std::fopen(argv[5], "w");
  if (trace == nullptr) {
    std::fprintf(stderr, "cannot write %s\n", argv[5]);
    return 1;
  }
  struct Output {
    const char *name;
    IOPIN *pin;
    int driven;
    int level;
  };
  std::vector<Output> outputs;
  for (const char *name : { "data", "gate" }) {
    auto found = pins.find(name);
    if (found != pins.end() && found->second.pin != 0) {
      outputs.push_back({ name, cpu->get_pin(found->second.pin), -1, -1 });
    }
  }
  // Whether each console line is pulled up: an input whose weak pull-up gpsim
  // reports enabled, which these PICs switch with OPTION's /GPPU bit and, on
  // the 12F629, the WPU register (Read: the 12C508A and 12F629 datasheets,
  // GPIO and OPTION); a pin with no pull-up circuit reports none. Written as
  // pull-<name>, sampled every PULL_SAMPLE_STEPS
  // instructions as in the AVR runner, since a pull-up holds once written.
  struct Pull {
    std::string name;
    IOPIN *pin;
    int pulled;
  };
  std::vector<Pull> pulls;
  for (const auto &entry : pins) {
    if (entry.second.pin != 0) {
      pulls.push_back({ entry.first, cpu->get_pin(entry.second.pin), -1 });
    }
  }
  constexpr unsigned PULL_SAMPLE_STEPS = 256U;
  unsigned long long pic_steps = 0U;

  // The step bound is the duration in instruction cycles plus a margin; a
  // two-cycle instruction only makes the loop end sooner, never later.
  const unsigned long long max_steps =
      static_cast<unsigned long long>(duration_ns / ns_per_cycle) + 1024ULL;
  std::set<unsigned int> executed;
  size_t next_event = 0;
  unsigned long long wfck_half_ns = 0;
  unsigned long long next_wfck_ns = 0;
  bool wfck = true;
  unsigned long long now_ns = 0;
  for (unsigned long long step = 0; step < max_steps && now_ns < duration_ns; step++) {
    now_ns = static_cast<unsigned long long>(get_cycles().get() * ns_per_cycle);
    while (next_event < events.size() && events[next_event].time_ns <= now_ns) {
      const ConsoleEvent &event = events[next_event];
      if (event.signal == "wfck_half_ns") {
        wfck_half_ns = static_cast<unsigned long long>(event.value);
        next_wfck_ns = event.time_ns + wfck_half_ns;
      } else if (event.signal == "wfck") {
        wfck = event.value != 0;
        drive_line("wfck", wfck);
      } else if (event.signal == "power_cycle") {
        // Switching the console off and on: a power-on reset. The calibration
        // word placed at load stays in program memory, as a real part keeps it.
        cpu->reset(POR_RESET);
      } else if (event.signal == "vcc_mv" || event.signal == "eeprom_byte" ||
                 event.signal == "osccal_factory") {
        // The supply level, the AVR record seed and the AVR factory OSCCAL have
        // no counterpart here: gpsim models no supply on these parts, and
        // Mayumi V4 and MM3 keep no EEPROM record.
      } else {
        drive_line(event.signal, event.value != 0);
      }
      next_event++;
    }
    if (wfck_half_ns != 0 && now_ns >= next_wfck_ns) {
      wfck = !wfck;
      drive_line("wfck", wfck);
      next_wfck_ns += wfck_half_ns;
    }
    executed.insert(cpu->pc->get_value());
    cpu->step(1, false);
    if ((pic_steps++ % PULL_SAMPLE_STEPS) == 0U) {
      for (Pull &pull : pulls) {
        bool input = pull.pin->get_direction() != IOPIN::DIR_OUTPUT;
        auto *bidirectional = dynamic_cast<IO_bi_directional *>(pull.pin);
        bool enabled = bidirectional != nullptr && bidirectional->getPullupStatus();
        int pulled = (input && enabled) ? 1 : 0;
        if (pulled != pull.pulled) {
          std::fprintf(trace,
                       "%llu pull-%s %d %d\n",
                       static_cast<unsigned long long>(get_cycles().get() * ns_per_cycle),
                       pull.name.c_str(),
                       pulled,
                       pulled);
          pull.pulled = pulled;
        }
      }
    }
    for (Output &out : outputs) {
      int driven = out.pin->get_direction() == IOPIN::DIR_OUTPUT ? 1 : 0;
      int level = out.pin->getDrivingState() ? 1 : 0;
      if (driven != out.driven || (driven == 1 && level != out.level)) {
        std::fprintf(trace,
                     "%llu %s %d %d\n",
                     static_cast<unsigned long long>(get_cycles().get() * ns_per_cycle),
                     out.name,
                     driven,
                     level);
        out.driven = driven;
        out.level = level;
      }
    }
  }
  std::fprintf(trace, "%llu end 0 0\n", now_ns);
  std::fclose(trace);

  std::FILE *coverage = std::fopen(argv[6], "w");
  if (coverage == nullptr) {
    std::fprintf(stderr, "cannot write %s\n", argv[6]);
    return 1;
  }
  for (unsigned int address : executed) {
    std::fprintf(coverage, "%x\n", address);
  }
  std::fclose(coverage);
  return 0;
}
