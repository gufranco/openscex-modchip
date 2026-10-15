// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

// Bench runner for PIC firmware, on gpsim's simulation library.
//
// Usage: pic_runner <image.hex> <processor> <clock_hz> <timeline> <trace_out>
//                   <coverage_out> <duration_ns> <pin_map>
//
// Plays a console timeline (tools/bench/timeline.py) into one PIC image and
// records its output pins and every program address executed. Pin map example:
// "wfck=5,lid=7,reset=4,sense=3,data=6,gate=5"; a leading '!' inverts a line.
// One instruction cycle is four clocks.
//
// gpsim quirks: only step(1) advances the cycle counter; inputs must be driven
// through the pin's node; 12-bit TRIS is not loggable, so direction is read
// from the pin.

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

// A console line driving one input pin through its node. The 50 ohm source
// overrides any internal pull-up.
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

// A pin map the runner cannot read in full would leave a console wire out of
// the run, so anything it cannot place is refused, as in the AVR runner: an
// item without '=', an empty name, or a pin that is not a package pin number
// from 1 to 40.
[[noreturn]] void bad_pin_map(const char *text, const char *why) {
  std::fprintf(stderr, "pin map %s: %s\n", text, why);
  std::exit(2);
}

constexpr long MAX_PACKAGE_PIN = 40L;

std::map<std::string, Line> parse_pin_map(const char *text) {
  std::map<std::string, Line> lines;
  std::stringstream all(text);
  std::string item;
  while (std::getline(all, item, ',')) {
    size_t eq = item.find('=');
    if (eq == std::string::npos || eq == 0U) {
      bad_pin_map(text, "an item without a name and '='");
    }
    Line line;
    std::string pin = item.substr(eq + 1);
    if (!pin.empty() && pin[0] == '!') {
      line.inverted = true;
      pin = pin.substr(1);
    }
    char *end = nullptr;
    long number = std::strtol(pin.c_str(), &end, 10);
    if (pin.empty() || *end != '\0' || number < 1L || number > MAX_PACKAGE_PIN) {
      bad_pin_map(text, "a pin that is not a package pin from 1 to 40");
    }
    line.pin = static_cast<int>(number);
    lines[item.substr(0, eq)] = line;
  }
  return lines;
}

// A whole decimal number, or exit: a clock or a duration that does not parse
// would run nothing and still exit 0.
unsigned long long number(const char *text, const char *what) {
  char *end = nullptr;
  unsigned long long value = std::strtoull(text, &end, 10);
  if (end == text || *end != '\0' || value == 0ULL) {
    std::fprintf(stderr, "%s must be a positive whole number, not %s\n", what, text);
    std::exit(2);
  }
  return value;
}

// Close a file the run wrote, or exit: a short write must not pass for a
// complete trace.
void finish(std::FILE *file, const char *path) {
  if (std::ferror(file) != 0 || std::fclose(file) != 0) {
    std::fprintf(stderr, "cannot finish writing %s\n", path);
    std::exit(1);
  }
}

// Console wires a chip may not use; the timeline drives them for every chip,
// and one this chip lacks is simply not wired. Any other name is an error.
bool is_console_wire(const std::string &name) {
  for (const char *wire : { "sqck", "subq", "wfck", "lid", "reset", "sense" }) {
    if (name == wire) {
      return true;
    }
  }
  return false;
}

// Every line must parse; a malformed one stops the run instead of silently
// ending the timeline there.
std::vector<ConsoleEvent> read_timeline(const char *path) {
  std::vector<ConsoleEvent> events;
  std::ifstream in(path);
  if (!in) {
    std::fprintf(stderr, "cannot read %s\n", path);
    std::exit(1);
  }
  ConsoleEvent event;
  while (in >> event.time_ns >> event.signal >> event.value) {
    events.push_back(event);
  }
  if (!in.eof()) {
    std::fprintf(stderr, "%s: malformed event %zu\n", path, events.size() + 1U);
    std::exit(1);
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
  const double clock_hz = static_cast<double>(number(argv[3], "clock_hz"));
  const double ns_per_cycle = 4.0e9 / clock_hz;
  const unsigned long long duration_ns = number(argv[7], "duration_ns");
  const std::map<std::string, Line> pins = parse_pin_map(argv[8]);
  const std::vector<ConsoleEvent> events = read_timeline(argv[4]);
  // The bench compares this count with the timeline it wrote.
  std::printf("events %zu\n", events.size());

  initialize_gpsim_core();
  CSimulationContext *context = CSimulationContext::GetContext();
  context->Initialize();
  Processor *cpu = nullptr;
  if (!context->LoadProgram(argv[1], argv[2], &cpu) || cpu == nullptr) {
    std::fprintf(stderr, "cannot load %s as %s\n", argv[1], argv[2]);
    return 1;
  }
  cpu->set_frequency(clock_hz);

  // Fill an erased oscillator calibration word with 0x80, as a real part ships
  // one: MOVLW on the 12C508 (DS40139, 7.2.5), RETLW on the 12F629 (DS41190,
  // 9.2.5.1).
  const unsigned int last = cpu->program_memory_size() - 1U;
  const std::string processor = argv[2];
  const unsigned int erased = processor == "p12f629" ? 0x3FFFU : 0xFFFU;
  const unsigned int factory = processor == "p12f629" ? 0x3480U : 0xC80U;
  if (cpu->get_program_memory_at_address(last) == erased) {
    cpu->init_program_memory(last, factory);
  }
  // The 16C5x resets to its last program word (DS30453); gpsim's 16C54 starts
  // at 0x000, so set the reset address and reset once.
  if (processor == "p16c54") {
    cpu->pc->set_reset_address(last);
    cpu->reset(POR_RESET);
  }

  // One node and source per input, starting at the console's idle levels: WFCK
  // high, lid closed, reset released, sense high.
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
  // Trace each console input whose weak pull-up is on as pull-<name>, sampled
  // after every instruction: gpsim offers no hook on the option register's
  // writes, and a sparser sample could miss a pull-up that is on only briefly.
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

  // Step bound: the duration in instruction cycles plus a margin.
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
        // Power-on reset; the calibration word stays in program memory.
        cpu->reset(POR_RESET);
      } else if (event.signal == "watchdog_reset") {
        cpu->reset(WDT_RESET);
      } else if (event.signal == "vcc_mv" || event.signal == "eeprom_byte" ||
                 event.signal == "osccal_factory") {
        // AVR-only events; ignored here.
      } else if (is_console_wire(event.signal)) {
        drive_line(event.signal, event.value != 0);
      } else {
        std::fprintf(stderr, "unknown timeline signal %s\n", event.signal.c_str());
        return 1;
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
  finish(trace, argv[5]);

  std::FILE *coverage = std::fopen(argv[6], "w");
  if (coverage == nullptr) {
    std::fprintf(stderr, "cannot write %s\n", argv[6]);
    return 1;
  }
  for (unsigned int address : executed) {
    std::fprintf(coverage, "%x\n", address);
  }
  finish(coverage, argv[6]);
  return 0;
}
