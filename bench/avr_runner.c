// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

// Bench runner for AVR firmware, on simavr.
//
// Usage: avr_runner <image.elf> <mcu> <clock_hz> <timeline> <trace_out>
//                   <coverage_out> <duration_ns> <pin_map>
//
// It plays a console timeline (tools/bench/timeline.py) into one AVR image and
// writes what the chip did to its output pins, plus every flash address it
// executed. The pin map names port bits, for example
// "sqck=B0,subq=B1,wfck=B4,data=B2,gate=B4"; a leading '!' inverts a line.
// Outputs are sampled from the port registers after every instruction. simavr's
// pin and direction notifications are not enough: a release that clears DDR
// while the PORT bit is already 0 raises neither (seen 2026-10-07: our DATA
// read as driven low through every gap between strings).
// An ATtiny85 image gets the Timer1 and OSCCAL fixes the console harness uses
// (tests/sim/sim_t85.h), shared so the bench and the harness run ours alike.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "avr_ioport.h"
#include "sim_avr.h"
#include "sim_elf.h"
#include "sim_irq.h"
#include "sim_t85.h"

#define MAX_EVENTS 4000000
#define MAX_LINES 8
#define NAME_LEN 16

typedef struct {
  unsigned long long time_ns;
  char signal[NAME_LEN];
  long value;
} event_t;

typedef struct {
  char name[NAME_LEN];
  char port;
  uint8_t bit;
  int inverted;
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

static const line_t *find_line(const char *name) {
  for (int i = 0; i < g_line_count; i++) {
    if (strcmp(g_lines[i].name, name) == 0) {
      return &g_lines[i];
    }
  }
  return NULL;
}

static void drive(avr_t *avr, const char *name, int value) {
  const line_t *line = find_line(name);
  if (line == NULL) {
    return;
  }
  avr_irq_t *irq = avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ(line->port), line->bit);
  avr_raise_irq(irq, (uint32_t)((value != 0) != line->inverted));
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

static event_t *read_timeline(const char *path, size_t *count) {
  event_t *events = calloc(MAX_EVENTS, sizeof(event_t));
  FILE *in = fopen(path, "r");
  if (events == NULL || in == NULL) {
    fprintf(stderr, "cannot read %s\n", path);
    exit(1);
  }
  size_t n = 0;
  while (n < MAX_EVENTS &&
         fscanf(in, "%llu %15s %ld", &events[n].time_ns, events[n].signal, &events[n].value) == 3) {
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
    const line_t *line = find_line(outputs[i]);
    if (line == NULL) {
      continue;
    }
    output_t *out = &g_outputs[g_output_count++];
    *out = (output_t){ line, avr, trace, ns_per_cycle, -1, -1 };
    sample(out);
  }

  // Console idle levels, as in the PIC runner.
  drive(avr, "wfck", 1);
  drive(avr, "lid", 0);
  drive(avr, "reset", 1);
  drive(avr, "xlat", 1);
  drive(avr, "sqck", 1);

  const size_t flash_words = (size_t)(avr->flashend + 1U) / 2U;
  uint8_t *executed = calloc(flash_words, 1);
  const uint64_t end_cycle = (uint64_t)((double)duration_ns / ns_per_cycle);
  const uint64_t max_steps = end_cycle + 1024U;
  size_t next_event = 0;
  unsigned long long wfck_half_ns = 0;
  unsigned long long next_wfck_ns = 0;
  int wfck = 1;
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
    if (state == cpu_Crashed || state == cpu_Done) {
      fprintf(stderr, "cpu stopped at cycle %llu\n", (unsigned long long)avr->cycle);
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
  return 0;
}
