// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "harness.h"

#include <stdio.h>
#include <string.h>

#include "avr_ioport.h"
#include "sim_cycle_timers.h"
#include "sim_elf.h"

// The console model every scenario drives: the frame clock, the WFCK
// carrier, the lid line and the LED recorder, plus the DATA decoder.

const char SCEA_BITS[SCEX_BITS + 1] = "10011010100100111101001010111010010111110100";
const char SCEI_BITS[SCEX_BITS + 1] = "10011010100100111101001010111010010110110100";

int g_checks = 0;
int g_failures = 0;
int g_led_seen = 0;
uint64_t g_led_cycle = 0;
uint64_t g_pulse_rise[MAX_PULSES];
uint64_t g_pulse_len[MAX_PULSES];
int g_pulses = 0;
uint64_t g_rise = 0;
int g_strings = 0;
uint32_t g_freq = 4233600U;
// The SQCK half-period the harness clocks frames with, normally EDGE_CYCLES.
uint64_t g_edge_cycles = EDGE_CYCLES;

void check(int cond, const char *name) {
  g_checks++;
  if (!cond) {
    g_failures++;
    (void)printf("FAIL: %s\n", name);
  }
}

uint64_t ms_cycles(uint32_t ms) {
  return ((uint64_t)g_freq * ms) / 1000U;
}

// The first LED rise after boot marks the first region string, which the
// scenarios that decode or interrupt a string start from; nothing else lights
// the LED that early after boot. Every pulse is also recorded, and one whose
// length matches a region string is counted as one.
void on_led(struct avr_irq_t *irq, uint32_t value, void *param) {
  avr_t *avr = param;
  (void)irq;
  if (value != 0U) {
    g_rise = avr->cycle;
    if ((avr->cycle >= DETECT_CYCLES) && (g_led_seen == 0)) {
      g_led_seen = 1;
      g_led_cycle = avr->cycle;
    }
    return;
  }
  if (g_rise == 0U) {
    return;
  }
  uint64_t len = avr->cycle - g_rise;
  if (g_pulses < MAX_PULSES) {
    g_pulse_rise[g_pulses] = g_rise;
    g_pulse_len[g_pulses] = len;
    g_pulses++;
  }
  if ((g_rise >= DETECT_CYCLES) && (len >= ms_cycles(60U)) && (len <= ms_cycles(200U))) {
    g_strings++;
  }
  g_rise = 0U;
}

// Count the first group of pulses at or after `from` whose length lies in
// [min_ms, max_ms], a group ending where the next such pulse starts more than
// 1.5 s after the previous one. A code is that many long flashes one second
// apart, then a 2 s pause, so this reads one repetition of a code.
int pulse_group(uint64_t from, uint32_t min_ms, uint32_t max_ms) {
  int count = 0;
  uint64_t last = 0U;
  for (int i = 0; i < g_pulses; i++) {
    if ((g_pulse_rise[i] < from) || (g_pulse_len[i] < ms_cycles(min_ms)) ||
        (g_pulse_len[i] > ms_cycles(max_ms))) {
      continue;
    }
    if ((count > 0) && ((g_pulse_rise[i] - last) > ms_cycles(1500U))) {
      break;
    }
    count++;
    last = g_pulse_rise[i];
  }
  return count;
}

int code_after(uint64_t from) {
  return pulse_group(from, 600U, 800U);
}

avr_cycle_count_t wfck_tick(avr_t *avr, avr_cycle_count_t when, void *param) {
  wfck_ctx_t *ctx = param;
  (void)avr;
  ctx->level = (uint8_t)(ctx->level ^ 1U);
  avr_raise_irq(ctx->irq, ctx->level);
  return when + ctx->half;
}

void run_to(avr_t *avr, uint64_t target) {
  while (avr->cycle < target) {
    int state = avr_run(avr);
    if ((state == cpu_Crashed) || (state == cpu_Done)) {
      break;
    }
  }
}

void run_cycles(avr_t *avr, uint64_t n) {
  run_to(avr, avr->cycle + n);
}

avr_irq_t *pin_irq(avr_t *avr, const target_t *t, uint8_t pin) {
  return avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ((uint32_t)t->port), pin);
}

avr_irq_t *lid_irq(avr_t *avr) {
  return avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ((uint32_t)LID_PORT), LID_PIN);
}

// The firmware's port init turns on the lid pull-up, which simavr reflects on the
// pin until an external level is driven again. So the harness lets init run,
// then pulses the lid line high and back low: the console's closed-lid level
// then holds over the weak pull-up, as on the real board, and every scenario
// starts with a disc in.
#define PORT_INIT_CYCLES 2000U

avr_t *build_avr(const target_t *t, const char *elf, uint32_t freq) {
  elf_firmware_t firmware;
  memset(&firmware, 0, sizeof(firmware));
  (void)elf_read_firmware(elf, &firmware);

  avr_t *avr = avr_make_mcu_by_name(t->mcu);
  avr_init(avr);
  avr_load_firmware(avr, &firmware);
  avr->frequency = freq;
  g_freq = freq;
  g_led_seen = 0;
  g_led_cycle = 0U;
  g_pulses = 0;
  g_rise = 0U;
  g_strings = 0;
  avr_irq_t *lid = lid_irq(avr);
  avr_raise_irq(lid, 0U);
  run_cycles(avr, PORT_INIT_CYCLES);
  avr_raise_irq(lid, 1U);
  avr_raise_irq(lid, 0U);
  return avr;
}

uint8_t data_ddr(avr_t *avr, const target_t *t) {
  avr_ioport_state_t state;
  (void)avr_ioctl(avr, AVR_IOCTL_IOPORT_GETSTATE((uint32_t)t->port), &state);
  return (uint8_t)((state.ddr >> t->data) & 1U);
}

uint8_t data_pin(avr_t *avr, const target_t *t) {
  avr_ioport_state_t state;
  (void)avr_ioctl(avr, AVR_IOCTL_IOPORT_GETSTATE((uint32_t)t->port), &state);
  return (uint8_t)((state.port >> t->data) & 1U) & (uint8_t)((state.ddr >> t->data) & 1U);
}

void clock_frame(avr_t *avr, const target_t *t, const uint8_t *frame) {
  avr_irq_t *sqck = pin_irq(avr, t, t->sqck);
  avr_irq_t *subq = pin_irq(avr, t, t->subq);
  for (int byte = 0; byte < SUBQ_FRAME_BYTES; byte++) {
    for (int bit = 0; bit < SUBQ_BITS; bit++) {
      avr_raise_irq(subq, (uint8_t)((frame[byte] >> bit) & 1U));
      avr_raise_irq(sqck, 0U);
      run_cycles(avr, g_edge_cycles);
      avr_raise_irq(sqck, 1U);
      run_cycles(avr, g_edge_cycles);
    }
  }
  // Idle the clock for the inter-frame gap, but stop early the moment the LED
  // marks the start of an injection inside it: the decoder samples DATA forward
  // from that edge and cannot sample cycles the gap has already run past.
  int led_before = g_led_seen;
  uint64_t gap_end = avr->cycle + FRAME_GAP_CYCLES;
  while ((avr->cycle < gap_end) && !((g_led_seen != 0) && (led_before == 0))) {
    run_cycles(avr, 500U);
  }
}

// Clock lead-in frames until the first string starts, with the spare frames
// TRIGGER_SPARE_FRAMES allows, and stop there so no frame overlaps the string.
// A frame that would not arm injection is clocked the full count, which only
// makes a negative case stricter.
void clock_until_inject(avr_t *avr, const target_t *t, const uint8_t *frame) {
  for (int i = 0; (i < (TRIGGER_FRAMES + TRIGGER_SPARE_FRAMES)) && (g_led_seen == 0); i++) {
    clock_frame(avr, t, frame);
  }
}

void boot_quiet(avr_t *avr, const target_t *t, int modern, wfck_ctx_t *ctx) {
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
void decode_region(avr_t *avr, const target_t *t, int modern, uint64_t bit_cycles, char *out) {
  for (int k = 0; k < SCEX_BITS; k++) {
    uint64_t base = g_led_cycle + ((uint64_t)k * bit_cycles);
    uint8_t bit;
    if (modern != 0) {
      // A modern one is DATA mirroring the WFCK carrier, so it oscillates
      // within the cell. Sampling only a few points can alias onto the low
      // phase and misread it; step finely across the central 40 percent of the
      // cell and treat any high as a one, which always catches the carrier.
      uint8_t any_high = 0U;
      uint64_t lo = base + ((3U * bit_cycles) / 10U);
      uint64_t hi = base + ((7U * bit_cycles) / 10U);
      for (uint64_t c = lo; c <= hi; c += 64U) {
        run_to(avr, c);
        if (data_pin(avr, t) != 0U) {
          any_high = 1U;
        }
      }
      bit = any_high;
    } else {
      run_to(avr, base + (bit_cycles / 2U));
      bit = (uint8_t)((data_ddr(avr, t) != 0U) ? 0U : 1U);
    }
    out[k] = (bit != 0U) ? '1' : '0';
  }
  out[SCEX_BITS] = '\0';
}

void clock_frames(avr_t *avr, const target_t *t, const uint8_t *frame, int count) {
  for (int i = 0; i < count; i++) {
    clock_frame(avr, t, frame);
  }
}

int strings_while(avr_t *avr, const target_t *t, const uint8_t *frame, int count) {
  int before = g_strings;
  clock_frames(avr, t, frame, count);
  return g_strings - before;
}

void swap_disc(avr_t *avr) {
  avr_raise_irq(lid_irq(avr), 1U);
  run_cycles(avr, MD_LID_OPEN_CYCLES);
  avr_raise_irq(lid_irq(avr), 0U);
}

int sim_report(void) {
  (void)printf("%d checks, %d failures\n", g_checks, g_failures);
  return (g_failures == 0) ? 0 : 1;
}
