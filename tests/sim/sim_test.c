// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "avr_ioport.h"
#include "sim_avr.h"
#include "sim_cycle_timers.h"
#include "sim_elf.h"
#include "sim_irq.h"

#define PIN_SQCK IOPORT_IRQ_PIN0
#define PIN_SUBQ IOPORT_IRQ_PIN1
#define PIN_DATA IOPORT_IRQ_PIN2
#define PIN_LED IOPORT_IRQ_PIN3
#define PIN_WFCK IOPORT_IRQ_PIN4

#define SUBQ_FRAME_BYTES 12
#define SUBQ_BITS 8
#define EDGE_CYCLES 60
#define DETECT_CYCLES 600000UL
#define TRIGGER_FRAMES 10
#define SCEX_BITS 44
#define FW_MS_CYCLES 8000UL
#define BIT_CYCLES (4UL * FW_MS_CYCLES)
#define WFCK_HZ 7300UL
#define LED_DEADLINE 1000000UL

static const char SCEI_BITS[SCEX_BITS + 1] =
    "10011010100100111101001010111010010110110100";

static int g_checks = 0;
static int g_failures = 0;
static int g_led_seen = 0;
static uint64_t g_led_cycle = 0;

typedef struct {
  avr_irq_t *irq;
  uint8_t level;
  uint32_t half;
} wfck_ctx_t;

static void check(int cond, const char *name) {
  g_checks++;
  if (!cond) {
    g_failures++;
    (void)printf("FAIL: %s\n", name);
  }
}

static void on_led(struct avr_irq_t *irq, uint32_t value, void *param) {
  avr_t *avr = param;
  (void)irq;
  if ((value != 0U) && (g_led_seen == 0)) {
    g_led_seen = 1;
    g_led_cycle = avr->cycle;
  }
}

static avr_cycle_count_t wfck_tick(avr_t *avr, avr_cycle_count_t when, void *param) {
  wfck_ctx_t *ctx = param;
  (void)avr;
  ctx->level = (uint8_t)(ctx->level ^ 1U);
  avr_raise_irq(ctx->irq, ctx->level);
  return when + ctx->half;
}

static void run_to(avr_t *avr, uint64_t target) {
  while (avr->cycle < target) {
    int state = avr_run(avr);
    if ((state == cpu_Crashed) || (state == cpu_Done)) {
      break;
    }
  }
}

static void run_cycles(avr_t *avr, uint64_t n) {
  run_to(avr, avr->cycle + n);
}

static void clock_frame(avr_t *avr, avr_irq_t *sqck, avr_irq_t *subq,
                        const uint8_t *frame) {
  for (int byte = 0; byte < SUBQ_FRAME_BYTES; byte++) {
    for (int bit = 0; bit < SUBQ_BITS; bit++) {
      avr_raise_irq(subq, (uint8_t)((frame[byte] >> bit) & 1U));
      avr_raise_irq(sqck, 0U);
      run_cycles(avr, EDGE_CYCLES);
      avr_raise_irq(sqck, 1U);
      run_cycles(avr, EDGE_CYCLES);
    }
  }
}

static uint8_t data_ddr(avr_t *avr) {
  avr_ioport_state_t state;
  (void)avr_ioctl(avr, AVR_IOCTL_IOPORT_GETSTATE('B'), &state);
  return (uint8_t)((state.ddr >> PIN_DATA) & 1U);
}

static uint8_t data_pin(avr_t *avr) {
  avr_ioport_state_t state;
  (void)avr_ioctl(avr, AVR_IOCTL_IOPORT_GETSTATE('B'), &state);
  return (uint8_t)((state.port >> PIN_DATA) & 1U) &
         (uint8_t)((state.ddr >> PIN_DATA) & 1U);
}

static void decode_region(avr_t *avr, int modern, char *out) {
  for (int k = 0; k < SCEX_BITS; k++) {
    uint64_t base = g_led_cycle + ((uint64_t)k * BIT_CYCLES);
    uint8_t bit;
    if (modern != 0) {
      uint8_t any_high = 0U;
      for (int s = 3; s <= 7; s++) {
        run_to(avr, base + ((uint64_t)s * BIT_CYCLES) / 10U);
        if (data_pin(avr) != 0U) {
          any_high = 1U;
        }
      }
      bit = any_high;
    } else {
      run_to(avr, base + (BIT_CYCLES / 2U));
      bit = (uint8_t)((data_ddr(avr) != 0U) ? 0U : 1U);
    }
    out[k] = (bit != 0U) ? '1' : '0';
  }
  out[SCEX_BITS] = '\0';
}

static void scenario(const char *elf_path, uint32_t freq, int modern) {
  elf_firmware_t firmware;
  memset(&firmware, 0, sizeof(firmware));
  (void)elf_read_firmware(elf_path, &firmware);

  avr_t *avr = avr_make_mcu_by_name("attiny85");
  avr_init(avr);
  avr_load_firmware(avr, &firmware);
  avr->frequency = freq;

  avr_irq_t *sqck = avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('B'), PIN_SQCK);
  avr_irq_t *subq = avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('B'), PIN_SUBQ);
  avr_irq_t *led = avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('B'), PIN_LED);
  avr_irq_t *wfck = avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('B'), PIN_WFCK);

  g_led_seen = 0;
  g_led_cycle = 0;
  avr_irq_register_notify(led, on_led, avr);

  wfck_ctx_t ctx = {wfck, 1U, (uint32_t)(freq / (2UL * WFCK_HZ))};
  avr_raise_irq(sqck, 1U);
  avr_raise_irq(subq, 0U);
  if (modern != 0) {
    avr_raise_irq(wfck, 1U);
    avr_cycle_timer_register(avr, ctx.half, wfck_tick, &ctx);
  } else {
    avr_raise_irq(wfck, 1U);
  }

  run_cycles(avr, DETECT_CYCLES);

  uint8_t toc[SUBQ_FRAME_BYTES] = {0x41U, 0x00U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  for (int i = 0; i < TRIGGER_FRAMES; i++) {
    clock_frame(avr, sqck, subq, toc);
  }

  uint64_t deadline = avr->cycle + LED_DEADLINE;
  while ((g_led_seen == 0) && (avr->cycle < deadline)) {
    run_cycles(avr, 2000U);
  }

  const char *tag = (modern != 0) ? "modern" : "legacy";
  char label[64];
  char decoded[SCEX_BITS + 1];
  (void)snprintf(label, sizeof(label), "%s: injection triggered at %u Hz", tag, freq);
  check(g_led_seen != 0, label);

  if (g_led_seen != 0) {
    decode_region(avr, modern, decoded);
    (void)snprintf(label, sizeof(label), "%s: first region decodes to SCEI at %u Hz",
                   tag, freq);
    check(strcmp(decoded, SCEI_BITS) == 0, label);
    if (strcmp(decoded, SCEI_BITS) != 0) {
      (void)printf("  decoded %s\n  expect %s\n", decoded, SCEI_BITS);
    }
  }

  check(avr->state != cpu_Crashed, "firmware did not crash");
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    (void)fprintf(stderr, "usage: %s firmware.elf [freq_hz]\n", argv[0]);
    return 2;
  }
  uint32_t freq = (argc >= 3) ? (uint32_t)strtoul(argv[2], NULL, 10) : 8000000U;

  scenario(argv[1], freq, 0);
  scenario(argv[1], freq, 1);

  (void)printf("%d checks, %d failures\n", g_checks, g_failures);
  return (g_failures == 0) ? 0 : 1;
}
