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

// A software PlayStation, just enough to exercise the firmware end to end in
// simavr: it drives the console-side pins (SQCK, SUBQ, WFCK) and watches the
// firmware-side pins (DATA, LED), then decodes the injected bitstream and
// checks it equals the expected region word. Timing is in CPU cycles, which is
// independent of the simulated clock frequency, so the same model runs across
// the oscillator tolerance band.

#define SUBQ_FRAME_BYTES 12
#define SUBQ_BITS 8
// Half-period of the SQCK clock we fake while shifting a SUBQ frame.
#define EDGE_CYCLES 60
// Cycles to let the firmware's boot-time board detection complete.
#define DETECT_CYCLES 600000UL
#define INJECT_CYCLES 7000000UL
#define TRIGGER_FRAMES 10
#define SCEX_BITS 44
// One SCEx bit cell is 4 ms; at 8 MHz that is 32000 cycles. The decoder samples
// at this spacing from the LED edge that marks injection start.
#define BIT_CYCLES 32000UL
// WFCK carrier the modern-board model oscillates at (~7.3 kHz during init).
#define WFCK_HZ 7300UL
#define LED_DEADLINE 1000000UL

typedef struct {
  const char *mcu;
  char port;
  uint8_t sqck;
  uint8_t subq;
  uint8_t data;
  uint8_t led;
  uint8_t wfck;
} target_t;

// The Japanese (SCEI) word as the 44 DATA levels the firmware should emit,
// LSB-first. The decoder reconstructs this and compares; a correct decode on
// both board models proves the encoder and the per-model bit timing.
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

static avr_irq_t *pin_irq(avr_t *avr, const target_t *t, uint8_t pin) {
  return avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ((uint32_t)t->port), pin);
}

static avr_t *build_avr(const target_t *t, const char *elf, uint32_t freq) {
  elf_firmware_t firmware;
  memset(&firmware, 0, sizeof(firmware));
  (void)elf_read_firmware(elf, &firmware);

  avr_t *avr = avr_make_mcu_by_name(t->mcu);
  avr_init(avr);
  avr_load_firmware(avr, &firmware);
  avr->frequency = freq;
  return avr;
}

static uint8_t data_ddr(avr_t *avr, const target_t *t) {
  avr_ioport_state_t state;
  (void)avr_ioctl(avr, AVR_IOCTL_IOPORT_GETSTATE((uint32_t)t->port), &state);
  return (uint8_t)((state.ddr >> t->data) & 1U);
}

static uint8_t data_pin(avr_t *avr, const target_t *t) {
  avr_ioport_state_t state;
  (void)avr_ioctl(avr, AVR_IOCTL_IOPORT_GETSTATE((uint32_t)t->port), &state);
  return (uint8_t)((state.port >> t->data) & 1U) &
         (uint8_t)((state.ddr >> t->data) & 1U);
}

static void clock_frame(avr_t *avr, const target_t *t, const uint8_t *frame) {
  avr_irq_t *sqck = pin_irq(avr, t, t->sqck);
  avr_irq_t *subq = pin_irq(avr, t, t->subq);
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

static void boot_quiet(avr_t *avr, const target_t *t, int modern, wfck_ctx_t *ctx) {
  avr_raise_irq(pin_irq(avr, t, t->sqck), 1U);
  avr_raise_irq(pin_irq(avr, t, t->subq), 0U);
  avr_raise_irq(pin_irq(avr, t, t->wfck), 1U);
  if (modern != 0) {
    ctx->irq = pin_irq(avr, t, t->wfck);
    ctx->level = 1U;
    avr_cycle_timer_register(avr, ctx->half, wfck_tick, ctx);
  }
  run_cycles(avr, DETECT_CYCLES);
}

// Reconstruct the 44-bit word the firmware drove, reading DATA once per bit
// cell starting from the LED injection marker. The two board models encode a
// logic one differently, so each is decoded on its own terms: on a modern
// board a one is DATA mirroring the WFCK carrier, seen as the pin going high
// somewhere mid-cell (sampled across s = 3..7 tenths); on a legacy board a one
// is high-Z, seen as DATA left as an input (DDR clear), a zero as driven low.
static void decode_region(avr_t *avr, const target_t *t, int modern, char *out) {
  for (int k = 0; k < SCEX_BITS; k++) {
    uint64_t base = g_led_cycle + ((uint64_t)k * BIT_CYCLES);
    uint8_t bit;
    if (modern != 0) {
      uint8_t any_high = 0U;
      for (int s = 3; s <= 7; s++) {
        run_to(avr, base + ((uint64_t)s * BIT_CYCLES) / 10U);
        if (data_pin(avr, t) != 0U) {
          any_high = 1U;
        }
      }
      bit = any_high;
    } else {
      run_to(avr, base + (BIT_CYCLES / 2U));
      bit = (uint8_t)((data_ddr(avr, t) != 0U) ? 0U : 1U);
    }
    out[k] = (bit != 0U) ? '1' : '0';
  }
  out[SCEX_BITS] = '\0';
}

static void scenario_inject(const target_t *t, const char *elf, uint32_t freq,
                            int modern, int trigger) {
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = {NULL, 1U, (uint32_t)(freq / (2UL * WFCK_HZ))};
  g_led_seen = 0;
  g_led_cycle = 0;
  avr_irq_register_notify(pin_irq(avr, t, t->led), on_led, avr);
  boot_quiet(avr, t, modern, &ctx);

  // A lead-in frame that must arm injection (control 0x41 = data sector,
  // track 0xA0 = TOC) versus an ordinary audio frame that must not (control
  // 0x01, track 0x02). frame[1] and frame[6] are zero so both parse as framed.
  uint8_t toc[SUBQ_FRAME_BYTES] = {0x41U, 0x00U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  uint8_t audio[SUBQ_FRAME_BYTES] = {0x01U, 0x00U, 0x02U, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  for (int i = 0; i < TRIGGER_FRAMES; i++) {
    clock_frame(avr, t, (trigger != 0) ? toc : audio);
  }

  uint64_t deadline = avr->cycle + LED_DEADLINE;
  while ((g_led_seen == 0) && (avr->cycle < deadline)) {
    run_cycles(avr, 2000U);
  }

  const char *tag = (modern != 0) ? "modern" : "legacy";
  char label[96];
  char decoded[SCEX_BITS + 1];
  if (trigger != 0) {
    (void)snprintf(label, sizeof(label), "%s: inject triggered at %u Hz", tag, freq);
    check(g_led_seen != 0, label);
    if (g_led_seen != 0) {
      decode_region(avr, t, modern, decoded);
      (void)snprintf(label, sizeof(label), "%s: decodes SCEI at %u Hz", tag, freq);
      check(strcmp(decoded, SCEI_BITS) == 0, label);
    }
  } else {
    (void)snprintf(label, sizeof(label), "%s: non-TOC does not inject at %u Hz", tag,
                   freq);
    check(g_led_seen == 0, label);
  }
}

int main(int argc, char *argv[]) {
  if (argc < 3) {
    (void)fprintf(stderr, "usage: %s elf freq_hz\n", argv[0]);
    return 2;
  }
  const char *elf = argv[1];
  uint32_t freq = (uint32_t)strtoul(argv[2], NULL, 10);

  target_t t85 = {"attiny85", 'B', 0U, 1U, 2U, 3U, 4U};

  scenario_inject(&t85, elf, freq, 0, 1);
  scenario_inject(&t85, elf, freq, 1, 1);
  scenario_inject(&t85, elf, freq, 0, 0);

  (void)printf("%d checks, %d failures\n", g_checks, g_failures);
  return (g_failures == 0) ? 0 : 1;
}
