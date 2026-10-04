// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "avr_ioport.h"
#include "sim_avr.h"
#include "sim_elf.h"
#include "sim_irq.h"

#define PIN_SQCK IOPORT_IRQ_PIN0
#define PIN_SUBQ IOPORT_IRQ_PIN1
#define PIN_DATA IOPORT_IRQ_PIN2
#define PIN_WFCK IOPORT_IRQ_PIN4

#define SUBQ_FRAME_BYTES 12
#define SUBQ_BITS 8
#define EDGE_CYCLES 60
#define DETECT_CYCLES 600000UL
#define INJECT_CYCLES 7000000UL
#define TRIGGER_FRAMES 10

static int g_checks = 0;
static int g_failures = 0;
static uint32_t g_data_events = 0;

static void check(int cond, const char *name) {
  g_checks++;
  if (!cond) {
    g_failures++;
    (void)printf("FAIL: %s\n", name);
  }
}

static void on_data(struct avr_irq_t *irq, uint32_t value, void *param) {
  (void)irq;
  (void)value;
  (void)param;
  g_data_events++;
}

static void run_cycles(avr_t *avr, uint64_t n) {
  uint64_t target = avr->cycle + n;
  while (avr->cycle < target) {
    int state = avr_run(avr);
    if ((state == cpu_Crashed) || (state == cpu_Done)) {
      break;
    }
  }
}

static void clock_bit(avr_t *avr, avr_irq_t *sqck, avr_irq_t *subq, uint8_t bit) {
  avr_raise_irq(subq, bit ? 1U : 0U);
  avr_raise_irq(sqck, 0U);
  run_cycles(avr, EDGE_CYCLES);
  avr_raise_irq(sqck, 1U);
  run_cycles(avr, EDGE_CYCLES);
}

static void clock_frame(avr_t *avr, avr_irq_t *sqck, avr_irq_t *subq,
                        const uint8_t *frame) {
  for (int byte = 0; byte < SUBQ_FRAME_BYTES; byte++) {
    for (int bit = 0; bit < SUBQ_BITS; bit++) {
      clock_bit(avr, sqck, subq, (uint8_t)((frame[byte] >> bit) & 1U));
    }
  }
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    (void)fprintf(stderr, "usage: %s firmware.elf [freq_hz]\n", argv[0]);
    return 2;
  }

  elf_firmware_t firmware;
  memset(&firmware, 0, sizeof(firmware));
  if (elf_read_firmware(argv[1], &firmware) != 0) {
    (void)fprintf(stderr, "cannot read %s\n", argv[1]);
    return 2;
  }

  avr_t *avr = avr_make_mcu_by_name("attiny85");
  if (avr == NULL) {
    (void)fprintf(stderr, "cannot make attiny85\n");
    return 2;
  }
  avr_init(avr);
  avr_load_firmware(avr, &firmware);
  avr->frequency = (argc >= 3) ? (uint32_t)strtoul(argv[2], NULL, 10) : 8000000U;

  avr_irq_t *sqck = avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('B'), PIN_SQCK);
  avr_irq_t *subq = avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('B'), PIN_SUBQ);
  avr_irq_t *data = avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('B'), PIN_DATA);
  avr_irq_t *wfck = avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('B'), PIN_WFCK);
  avr_irq_register_notify(data, on_data, NULL);

  avr_raise_irq(wfck, 1U);
  avr_raise_irq(sqck, 1U);
  avr_raise_irq(subq, 0U);
  run_cycles(avr, DETECT_CYCLES);

  uint8_t toc[SUBQ_FRAME_BYTES] = {0x41U, 0x00U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  for (int i = 0; i < TRIGGER_FRAMES; i++) {
    clock_frame(avr, sqck, subq, toc);
  }

  g_data_events = 0;
  avr_ioport_state_t state;
  int prev_out = -1;
  uint32_t dir_changes = 0;
  uint64_t end = avr->cycle + INJECT_CYCLES;
  while (avr->cycle < end) {
    run_cycles(avr, 16000U);
    if (avr_ioctl(avr, AVR_IOCTL_IOPORT_GETSTATE('B'), &state) == 0) {
      int out = (int)((state.ddr >> PIN_DATA) & 1U);
      if ((prev_out != -1) && (out != prev_out)) {
        dir_changes++;
      }
      prev_out = out;
    }
  }

  check(avr->state != cpu_Crashed, "firmware did not crash");
  check(g_data_events > 0U, "legacy injection drives the DATA line");
  check(dir_changes > 20U, "legacy injection bit-bangs DATA across many bits");

  (void)printf("%d checks, %d failures, %u data events, %u dir changes\n", g_checks,
               g_failures, g_data_events, dir_changes);
  return (g_failures == 0) ? 0 : 1;
}
