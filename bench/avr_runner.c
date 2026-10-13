// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

// Bench runner for AVR firmware, on simavr.
//
// Usage: avr_runner <image.elf> <mcu> <clock_hz> <timeline> <trace_out>
//                   <coverage_out> <duration_ns> <pin_map>
//
// Plays a console timeline (tools/bench/timeline.py) into one AVR image and
// records its output pins and every flash address executed. Pin map example:
// "sqck=B0,subq=B1,wfck=B4,data=B2,gate=B4"; a leading '!' inverts a line.
// Outputs are read from the port registers after every instruction, since
// simavr raises no notification when DDR clears with PORT already 0.
// Events: power_cycle resets the core and keeps EEPROM; eeprom_byte writes
// address * 256 + byte; osccal_factory sets the boot OSCCAL (default 0x50).

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "avr_eeprom.h"
#include "avr_ioport.h"
#include "sim_avr.h"
#include "sim_elf.h"
#include "sim_irq.h"
#include "sim_t85.h"

// The event list grows as the timeline is read. The largest scenario has about
// 4.9 million events; MAX_EVENTS is only a backstop against a runaway file, and
// reaching it is an error, never a silent cut of the scenario's tail.
#define FIRST_EVENTS 1048576U
#define MAX_EVENTS 33554432U
#define MAX_LINES 8
#define NAME_LEN 16

typedef struct {
  unsigned long long time_ns;
  char signal[NAME_LEN];
  long value;
} event_t;

// One pin-map line and the level the console last drove on it, restored after a
// power cycle.
typedef struct {
  char name[NAME_LEN];
  char port;
  uint8_t bit;
  int inverted;
  int value;
} line_t;

typedef struct {
  const line_t *line;
  avr_t *avr;
  FILE *trace;
  double ns_per_cycle;
  int driven;
  int level;
} output_t;

static line_t g_lines[MAX_LINES];
static int g_line_count = 0;
static output_t g_outputs[3];
static int g_output_count = 0;
static int g_osccal_factory = -1;

static void parse_pin_map(const char *text) {
  char copy[256];
  snprintf(copy, sizeof(copy), "%s", text);
  for (char *item = strtok(copy, ","); item != NULL && g_line_count < MAX_LINES;
       item = strtok(NULL, ",")) {
    char *eq = strchr(item, '=');
    if (eq == NULL) {
      continue;
    }
    *eq = '\0';
    const char *pin = eq + 1;
    line_t *line = &g_lines[g_line_count];
    line->inverted = pin[0] == '!';
    pin += line->inverted;
    snprintf(line->name, sizeof(line->name), "%s", item);
    line->port = pin[0];
    line->bit = (uint8_t)atoi(pin + 1);
    g_line_count++;
  }
}

static line_t *find_line(const char *name) {
  for (int i = 0; i < g_line_count; i++) {
    if (strcmp(g_lines[i].name, name) == 0) {
      return &g_lines[i];
    }
  }
  return NULL;
}

static void drive(avr_t *avr, const char *name, int value) {
  line_t *line = find_line(name);
  if (line == NULL) {
    return;
  }
  line->value = value;
  avr_irq_t *irq = avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ(line->port), line->bit);
  avr_raise_irq(irq, (uint32_t)((value != 0) != line->inverted));
}

// Write one EEPROM byte through simavr's EEPROM model, as a programmer would.
static void eeprom_byte(avr_t *avr, long packed) {
  uint8_t byte = (uint8_t)(packed & 0xFF);
  avr_eeprom_desc_t desc = { .ee = &byte, .offset = (uint16_t)(packed >> 8), .size = 1 };
  avr_ioctl(avr, AVR_IOCTL_EEPROM_SET, &desc);
}

// Set PORF (MCUSR bit 0, data address 0x54 on these parts) at power-on, as the
// silicon does; simavr leaves it clear.
#define MCUSR_DATA_ADDR 0x54U
#define MCUSR_PORF 0x01U

static void power_on_flag(avr_t *avr) {
  avr->data[MCUSR_DATA_ADDR] = (uint8_t)(avr->data[MCUSR_DATA_ADDR] | MCUSR_PORF);
}

// Power-cycle the chip: reset, reinstall the ATtiny85 fixes and restore the
// console inputs. EEPROM is kept.
static void power_cycle(avr_t *avr, const char *mcu) {
  avr_reset(avr);
  power_on_flag(avr);
  if (strcmp(mcu, "attiny85") == 0) {
    sim_t85_install(avr);
    if (g_osccal_factory >= 0) {
      avr->data[SIM_OSCCAL_ADDR] = (uint8_t)g_osccal_factory;
    }
  }
  static const char *const inputs[] = { "sqck", "subq", "wfck", "lid", "reset", "sense" };
  for (size_t i = 0; i < sizeof(inputs) / sizeof(inputs[0]); i++) {
    const line_t *line = find_line(inputs[i]);
    if (line != NULL) {
      drive(avr, inputs[i], line->value);
    }
  }
}

// One output's state from the port's own registers: driven when its DDR bit
// is set, at the PORT bit's level. Written only when it changes.
static void sample(output_t *out) {
  avr_ioport_state_t state;
  avr_ioctl(out->avr, AVR_IOCTL_IOPORT_GETSTATE(out->line->port), &state);
  int driven = (int)((state.ddr >> out->line->bit) & 1U);
  int level = (int)((state.port >> out->line->bit) & 1U);
  if (driven != out->driven || (driven == 1 && level != out->level)) {
    fprintf(out->trace,
            "%llu %s %d %d\n",
            (unsigned long long)((double)out->avr->cycle * out->ns_per_cycle),
            out->line->name,
            driven,
            level);
    out->driven = driven;
    out->level = level;
  }
}

// Trace each console input whose pull-up is on (DDR clear, PORT set) as
// pull-<name>. Sampled every PULL_SAMPLE_STEPS instructions, since a pull-up
// holds once written.
#define PULL_SAMPLE_STEPS 256U

static int g_pulled[MAX_LINES];

static void sample_pulls(avr_t *avr, FILE *trace, double ns_per_cycle) {
  for (int i = 0; i < g_line_count; i++) {
    const line_t *line = &g_lines[i];
    if (strcmp(line->name, "led") == 0) {
      continue;
    }
    avr_ioport_state_t state;
    avr_ioctl(avr, AVR_IOCTL_IOPORT_GETSTATE(line->port), &state);
    int input = ((state.ddr >> line->bit) & 1U) == 0U;
    int pulled = input && (((state.port >> line->bit) & 1U) != 0U);
    if (pulled != g_pulled[i]) {
      fprintf(trace,
              "%llu pull-%s %d %d\n",
              (unsigned long long)((double)avr->cycle * ns_per_cycle),
              line->name,
              pulled,
              pulled);
      g_pulled[i] = pulled;
    }
  }
}

static event_t *read_timeline(const char *path, size_t *count) {
  size_t capacity = FIRST_EVENTS;
  event_t *events = malloc(capacity * sizeof(event_t));
  FILE *in = fopen(path, "r");
  if (events == NULL || in == NULL) {
    fprintf(stderr, "cannot read %s\n", path);
    exit(1);
  }
  size_t n = 0;
  for (;;) {
    if (n == capacity) {
      if (capacity >= MAX_EVENTS) {
        fprintf(stderr, "%s: more than %u events\n", path, MAX_EVENTS);
        exit(1);
      }
      capacity *= 2U;
      event_t *grown = realloc(events, capacity * sizeof(event_t));
      if (grown == NULL) {
        fprintf(stderr, "out of memory reading %s\n", path);
        exit(1);
      }
      events = grown;
    }
    int fields =
        fscanf(in, "%llu %15s %ld", &events[n].time_ns, events[n].signal, &events[n].value);
    if (fields == EOF) {
      break;
    }
    if (fields != 3) {
      fprintf(stderr, "%s: malformed event %zu\n", path, n + 1U);
      exit(1);
    }
    n++;
  }
  fclose(in);
  *count = n;
  return events;
}

int main(int argc, char **argv) {
  if (argc != 9) {
    fprintf(
        stderr, "usage: %s image mcu clock_hz timeline trace coverage duration_ns pins\n", argv[0]);
    return 2;
  }
  const uint32_t clock_hz = (uint32_t)strtoul(argv[3], NULL, 10);
  const double ns_per_cycle = 1.0e9 / (double)clock_hz;
  const unsigned long long duration_ns = strtoull(argv[7], NULL, 10);
  parse_pin_map(argv[8]);
  size_t event_count = 0;
  event_t *events = read_timeline(argv[4], &event_count);
  // The bench compares this count with the timeline it wrote, so a reader that
  // stops early can never pass for a complete run.
  printf("events %zu\n", event_count);

  elf_firmware_t firmware;
  memset(&firmware, 0, sizeof(firmware));
  if (elf_read_firmware(argv[1], &firmware) != 0) {
    fprintf(stderr, "cannot load %s\n", argv[1]);
    return 1;
  }
  avr_t *avr = avr_make_mcu_by_name(argv[2]);
  if (avr == NULL) {
    fprintf(stderr, "unknown mcu %s\n", argv[2]);
    return 1;
  }
  avr_init(avr);
  avr_load_firmware(avr, &firmware);
  avr->frequency = clock_hz;
  avr->vcc = 5000;
  avr->avcc = 5000;
  power_on_flag(avr);
  if (strcmp(argv[2], "attiny85") == 0) {
    sim_t85_install(avr);
  }

  FILE *trace = fopen(argv[5], "w");
  if (trace == NULL) {
    fprintf(stderr, "cannot write %s\n", argv[5]);
    return 1;
  }
  const char *outputs[] = { "data", "gate", "led" };
  for (int i = 0; i < 3; i++) {
    line_t *line = find_line(outputs[i]);
    if (line == NULL) {
      continue;
    }
    output_t *out = &g_outputs[g_output_count++];
    *out = (output_t){ line, avr, trace, ns_per_cycle, -1, -1 };
    sample(out);
  }
  for (int i = 0; i < g_line_count; i++) {
    g_pulled[i] = -1;
  }
  sample_pulls(avr, trace, ns_per_cycle);

  // Console idle levels, as in the PIC runner.
  drive(avr, "wfck", 1);
  drive(avr, "lid", 0);
  drive(avr, "reset", 1);
  drive(avr, "sense", 1);
  drive(avr, "sqck", 1);

  const size_t flash_words = (size_t)(avr->flashend + 1U) / 2U;
  uint8_t *executed = calloc(flash_words, 1);
  const uint64_t end_cycle = (uint64_t)((double)duration_ns / ns_per_cycle);
  const uint64_t max_steps = end_cycle + 1024U;
  size_t next_event = 0;
  unsigned long long wfck_half_ns = 0;
  unsigned long long next_wfck_ns = 0;
  int wfck = 1;
  int crashed = 0;
  for (uint64_t step = 0; step < max_steps && avr->cycle < end_cycle; step++) {
    unsigned long long now_ns = (unsigned long long)((double)avr->cycle * ns_per_cycle);
    while (next_event < event_count && events[next_event].time_ns <= now_ns) {
      const event_t *event = &events[next_event];
      if (strcmp(event->signal, "wfck_half_ns") == 0) {
        wfck_half_ns = (unsigned long long)event->value;
        next_wfck_ns = event->time_ns + wfck_half_ns;
      } else if (strcmp(event->signal, "wfck") == 0) {
        wfck = event->value != 0;
        drive(avr, "wfck", wfck);
      } else if (strcmp(event->signal, "vcc_mv") == 0) {
        avr->vcc = (uint32_t)event->value;
        avr->avcc = (uint32_t)event->value;
      } else if (strcmp(event->signal, "power_cycle") == 0) {
        power_cycle(avr, argv[2]);
      } else if (strcmp(event->signal, "eeprom_byte") == 0) {
        eeprom_byte(avr, event->value);
      } else if (strcmp(event->signal, "osccal_factory") == 0) {
        g_osccal_factory = (int)event->value;
        avr->data[SIM_OSCCAL_ADDR] = (uint8_t)event->value;
      } else {
        drive(avr, event->signal, (int)event->value);
      }
      next_event++;
    }
    if (wfck_half_ns != 0 && now_ns >= next_wfck_ns) {
      wfck = !wfck;
      drive(avr, "wfck", wfck);
      next_wfck_ns += wfck_half_ns;
    }
    if (avr->pc / 2U < flash_words) {
      executed[avr->pc / 2U] = 1;
    }
    int state = avr_run(avr);
    for (int i = 0; i < g_output_count; i++) {
      sample(&g_outputs[i]);
    }
    if ((step % PULL_SAMPLE_STEPS) == 0U) {
      sample_pulls(avr, trace, ns_per_cycle);
    }
    // A crash, such as a jump past the end of flash, makes the rest of the run
    // meaningless, so the runner fails. A core that stopped cleanly, sleeping
    // with interrupts off, is the image's own behaviour and is measured.
    if (state == cpu_Crashed || state == cpu_Done) {
      fprintf(stderr, "cpu stopped at cycle %llu\n", (unsigned long long)avr->cycle);
      crashed = state == cpu_Crashed;
      break;
    }
  }
  fprintf(trace, "%llu end 0 0\n", (unsigned long long)((double)avr->cycle * ns_per_cycle));
  fclose(trace);

  FILE *coverage = fopen(argv[6], "w");
  if (coverage == NULL) {
    fprintf(stderr, "cannot write %s\n", argv[6]);
    return 1;
  }
  for (size_t word = 0; word < flash_words; word++) {
    if (executed[word] != 0U) {
      fprintf(coverage, "%zx\n", word * 2U);
    }
  }
  fclose(coverage);
  free(executed);
  free(events);
  return crashed ? 3 : 0;
}
