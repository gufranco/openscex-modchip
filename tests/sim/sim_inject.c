// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <string.h>

#include "avr_ioport.h"
#include "harness.h"
#include "scenarios.h"
#include "sim_cycle_timers.h"

// Injection scenarios: decoding per board family, resync, carrier timing, the
// VCD build, the gate line and the gap between strings.

static void scenario_inject(const target_t *t,
                            const char *elf,
                            uint32_t freq,
                            int modern,
                            int trigger,
                            uint32_t wfck_hz,
                            const char *family) {
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, (uint32_t)(freq / (2UL * wfck_hz)) };
  g_indicator_seen = 0;
  g_indicator_cycle = 0;
  avr_irq_register_notify(pin_irq(avr, t, t->indicator), on_indicator, avr);
  boot_quiet(avr, t, modern, &ctx);

  // A TOC lead-in frame that arms injection (control 0x41, track 0xA0) and an
  // audio frame that must not (control 0x01, track 0x02).
  uint8_t toc[SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  uint8_t audio[SUBQ_FRAME_BYTES] = { 0x01U, 0x00U, 0x02U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  clock_until_inject(avr, t, (trigger != 0) ? toc : audio);

  uint64_t deadline = avr->cycle + INDICATOR_DEADLINE;
  while ((g_indicator_seen == 0) && (avr->cycle < deadline)) {
    run_cycles(avr, 2000U);
  }

  // A legacy bit is a fixed delay; a carrier bit is WFCK_PERIODS_PER_BIT
  // periods, so its length follows wfck_hz.
  uint64_t bit_cycles =
      (modern != 0) ? ((uint64_t)WFCK_PERIODS_PER_BIT * freq / wfck_hz) : BIT_CYCLES(freq);
  const char *tag = family;
  char label[96];
  char decoded[SCEX_BITS + 1];
  if (trigger != 0) {
    (void)snprintf(label, sizeof(label), "%s: inject triggered at %u Hz", tag, freq);
    check(g_indicator_seen != 0, label);
    if (g_indicator_seen != 0) {
      decode_region(avr, t, modern, bit_cycles, decoded);
      (void)snprintf(label, sizeof(label), "%s: decodes SCEA at %u Hz", tag, freq);
      check(strcmp(decoded, SCEA_BITS) == 0, label);
      if (strcmp(decoded, SCEA_BITS) != 0) {
        (void)printf("  expected %s\n  decoded  %s\n", SCEA_BITS, decoded);
      }
    }
  } else {
    (void)snprintf(label, sizeof(label), "%s: non-TOC does not inject at %u Hz", tag, freq);
    check((g_indicator_seen == 0) && (g_data_drives == 0), label);
  }
}

// The indicator result code after a disc's check: one long flash for an accepted
// disc, two for a refused one, each shown three times.
#define RESULT_TOC_FRAMES 12
#define RESULT_AFTER_FRAMES 120

void result_case(const target_t *t, const char *elf, uint32_t freq, int program) {
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, 0U };
  avr_irq_register_notify(pin_irq(avr, t, t->indicator), on_indicator, avr);
  boot_quiet(avr, t, 0, &ctx);

  const uint8_t toc[SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  const uint8_t silence[SUBQ_FRAME_BYTES] = { 0 };
  const uint8_t play[SUBQ_FRAME_BYTES] = { 0x41U, 0x01U, 0x01U, 0x00U, 0x02U, 0,
                                           0,     0,     0x02U, 0,     0,     0 };
  clock_frames(avr, t, toc, RESULT_TOC_FRAMES);
  uint64_t mark = avr->cycle;
  clock_frames(avr, t, (program != 0) ? play : silence, RESULT_AFTER_FRAMES);
  run_cycles(avr, ms_cycles(4000U));

  int expected = (program != 0) ? 1 : 2;
  char label[96];
  (void)snprintf(label,
                 sizeof(label),
                 "indicator %s: %s disc shows code %d",
                 t->mcu,
                 (program != 0) ? "accepted" : "refused",
                 expected);
  check((g_indicator_seen != 0) && (code_after(mark) == expected), label);
}

// A capture that starts mid-burst must realign on the next inter-frame gap and
// still inject.
#define PARTIAL_BURST_BITS 37
#define RESYNC_FRAMES (TRIGGER_FRAMES + 3)

static void clock_partial(avr_t *avr, const target_t *t, int bits) {
  avr_irq_t *sqck = pin_irq(avr, t, t->sqck);
  avr_irq_t *subq = pin_irq(avr, t, t->subq);
  for (int bit = 0; bit < bits; bit++) {
    avr_raise_irq(subq, (uint8_t)(bit & 1));
    avr_raise_irq(sqck, 0U);
    run_cycles(avr, ns_cycles(g_edge_ns));
    avr_raise_irq(sqck, 1U);
    run_cycles(avr, ns_cycles(g_edge_ns));
  }
}

void scenario_resync(const target_t *t, const char *elf, uint32_t freq) {
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, (uint32_t)(freq / (2UL * WFCK_HZ)) };
  g_indicator_seen = 0;
  g_indicator_cycle = 0;
  avr_irq_register_notify(pin_irq(avr, t, t->indicator), on_indicator, avr);
  boot_quiet(avr, t, 0, &ctx);

  uint8_t toc[SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  clock_partial(avr, t, PARTIAL_BURST_BITS);
  for (int i = 0; i < RESYNC_FRAMES; i++) {
    clock_frame(avr, t, toc);
  }
  uint64_t deadline = avr->cycle + INDICATOR_DEADLINE;
  while ((g_indicator_seen == 0) && (avr->cycle < deadline)) {
    run_cycles(avr, 2000U);
  }

  const char *tag = t->mcu;
  char label[96];
  (void)snprintf(label, sizeof(label), "resync %s: injects after a capture starts mid-burst", tag);
  check(g_indicator_seen != 0, label);
}

// WFCK starting 283 ms after power-on must still be detected as a carrier; the
// settle delay before detection covers it.
void scenario_late_carrier(const target_t *t, const char *elf, uint32_t freq) {
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, (uint32_t)(freq / (2UL * WFCK_HZ)) };
  g_indicator_seen = 0;
  g_indicator_cycle = 0;
  avr_irq_register_notify(pin_irq(avr, t, t->indicator), on_indicator, avr);
  avr_raise_irq(pin_irq(avr, t, t->sqck), 1U);
  avr_raise_irq(pin_irq(avr, t, t->subq), 0U);
  avr_raise_irq(pin_irq(avr, t, t->wfck), 1U);
  run_cycles(avr, LATE_CARRIER_CYCLES);
  ctx.irq = pin_irq(avr, t, t->wfck);
  avr_cycle_timer_register(avr, ctx.half, wfck_tick, &ctx);
  run_to(avr, DETECT_CYCLES);

  uint8_t toc[SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  clock_until_inject(avr, t, toc);
  uint64_t deadline = avr->cycle + INDICATOR_DEADLINE;
  while ((g_indicator_seen == 0) && (avr->cycle < deadline)) {
    run_cycles(avr, 2000U);
  }

  const char *tag = t->mcu;
  char label[96];
  char decoded[SCEX_BITS + 1] = { 0 };
  (void)snprintf(label, sizeof(label), "late carrier %s: injection triggered", tag);
  check(g_indicator_seen != 0, label);
  if (g_indicator_seen != 0) {
    decode_region(avr, t, 1, (uint64_t)WFCK_PERIODS_PER_BIT * freq / WFCK_HZ, decoded);
    (void)snprintf(label, sizeof(label), "late carrier %s: detected as a carrier board", tag);
    check(strcmp(decoded, SCEA_BITS) == 0, label);
  }
}

// A carrier that starts after boot detection is first seen as a gate; the
// firmware re-checks WFCK before the string and mirrors instead. WFCK must
// never become an output.
static int g_wfck_driven = 0;
static uint8_t g_wfck_bit = 0U;

static void on_direction(struct avr_irq_t *irq, uint32_t value, void *param) {
  (void)irq;
  (void)param;
  if (((value >> g_wfck_bit) & 1U) != 0U) {
    g_wfck_driven = 1;
  }
}

void scenario_carrier_after_boot(const target_t *t, const char *elf, uint32_t freq) {
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, (uint32_t)(freq / (2UL * WFCK_HZ)) };
  g_wfck_driven = 0;
  g_wfck_bit = t->wfck;
  avr_irq_register_notify(pin_irq(avr, t, t->indicator), on_indicator, avr);
  avr_irq_register_notify(pin_irq(avr, t, IOPORT_IRQ_DIRECTION_ALL), on_direction, NULL);
  boot_quiet(avr, t, 0, &ctx);
  int blinks = pulse_group(0U, 250U, 350U);
  ctx.irq = pin_irq(avr, t, t->wfck);
  avr_cycle_timer_register(avr, ctx.half, wfck_tick, &ctx);

  uint8_t toc[SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  clock_until_inject(avr, t, toc);
  uint64_t deadline = avr->cycle + INDICATOR_DEADLINE;
  while ((g_indicator_seen == 0) && (avr->cycle < deadline)) {
    run_cycles(avr, 2000U);
  }
  char decoded[SCEX_BITS + 1] = { 0 };
  if (g_indicator_seen != 0) {
    decode_region(avr, t, 1, (uint64_t)WFCK_PERIODS_PER_BIT * freq / WFCK_HZ, decoded);
  }

  char label[96];
  (void)snprintf(label, sizeof(label), "carrier after boot %s: boot shows a gate board", t->mcu);
  check(blinks == 1, label);
  (void)snprintf(label, sizeof(label), "carrier after boot %s: injects by mirroring", t->mcu);
  check(strcmp(decoded, SCEA_BITS) == 0, label);
  (void)snprintf(label, sizeof(label), "carrier after boot %s: WFCK is never driven", t->mcu);
  check(g_wfck_driven == 0, label);
}

// A carrier that stalls mid-string must not leave DATA driven: the bit cell
// kicks the watchdog only on WFCK edges, so the reset releases DATA within
// about 0.5 s.
#define STALL_AFTER_CYCLES ns_cycles(47000000ULL)
#define STALL_WAIT_CYCLES ns_cycles(1417000000ULL)
#define STALL_TOC_FRAMES 150

// The WFCK pin's direction bit: set while the chip drives the line.
static uint8_t wfck_ddr(avr_t *avr, const target_t *t) {
  avr_ioport_state_t state;
  (void)avr_ioctl(avr, AVR_IOCTL_IOPORT_GETSTATE((uint32_t)t->port), &state);
  return (uint8_t)((state.ddr >> t->wfck) & 1U);
}

void scenario_stall(const target_t *t, const char *elf, uint32_t freq) {
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, (uint32_t)(freq / (2UL * WFCK_HZ)) };
  g_indicator_seen = 0;
  g_indicator_cycle = 0;
  avr_irq_register_notify(pin_irq(avr, t, t->indicator), on_indicator, avr);
  boot_quiet(avr, t, 1, &ctx);

  uint8_t toc[SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  clock_until_inject(avr, t, toc);
  uint64_t deadline = avr->cycle + INDICATOR_DEADLINE;
  while ((g_indicator_seen == 0) && (avr->cycle < deadline)) {
    run_cycles(avr, 2000U);
  }

  const char *tag = t->mcu;
  char label[96];
  (void)snprintf(label, sizeof(label), "stall %s: injection started", tag);
  check(g_indicator_seen != 0, label);
  run_to(avr, g_indicator_cycle + STALL_AFTER_CYCLES);
  (void)snprintf(label, sizeof(label), "stall %s: DATA driven while the carrier runs", tag);
  check(data_ddr(avr, t) != 0U, label);

  avr_cycle_timer_cancel(avr, wfck_tick, &ctx);
  run_cycles(avr, STALL_WAIT_CYCLES);
  (void)snprintf(label, sizeof(label), "stall %s: DATA released after the carrier stops", tag);
  check(data_ddr(avr, t) == 0U, label);

  uint64_t rebooted = avr->cycle;
  run_cycles(avr, ms_cycles(9000U));
  (void)snprintf(label, sizeof(label), "stall %s: the next boot shows watchdog code 5", tag);
  check(code_after(rebooted) == 5, label);

  // After the reset the carrier is still stalled; the chip must keep the board
  // it booted on and never drive WFCK, a CD DSP output here.
  int wfck_driven = 0;
  for (int i = 0; i < STALL_TOC_FRAMES; i++) {
    clock_frame(avr, t, toc);
    wfck_driven |= (int)wfck_ddr(avr, t);
  }
  (void)snprintf(label, sizeof(label), "stall %s: WFCK is never driven after the reset", tag);
  check(wfck_driven == 0, label);
}

// The board-family matrix. The firmware only knows the static gate and the
// live carrier, so each family reduces to one of them and a carrier frequency.
typedef struct {
  const char *name;
  int modern;
  uint32_t wfck_hz;
} board_family_t;

static const board_family_t FAMILIES[] = {
  { "PU-18 and PU-20 legacy gate", 0, WFCK_HZ },
  { "PU-22 to PM-41 carrier 7.3kHz", 1, WFCK_HZ },
  { "PU-22 to PM-41 carrier 14.6kHz", 1, WFCK_READ_HZ },
};
#define FAMILY_COUNT ((int)(sizeof(FAMILIES) / sizeof(FAMILIES[0])))

// Boot a legacy-board image, clock one lead-in frame until a string starts or
// the frames run out, and decode DATA when an expected word is given.
static void vcd_case(
    const char *elf, uint32_t freq, const uint8_t *frame, const char *expect, const char *what) {
  target_t t85 = { "attiny85", 'B', 0U, 1U, 2U, 3U, 4U };
  avr_t *avr = build_avr(&t85, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, 0U };
  g_indicator_seen = 0;
  g_indicator_cycle = 0;
  avr_irq_register_notify(pin_irq(avr, &t85, t85.indicator), on_indicator, avr);
  boot_quiet(avr, &t85, 0, &ctx);
  clock_until_inject(avr, &t85, frame);
  uint64_t deadline = avr->cycle + INDICATOR_DEADLINE;
  while ((g_indicator_seen == 0) && (avr->cycle < deadline)) {
    run_cycles(avr, 2000U);
  }

  char label[96];
  (void)snprintf(label, sizeof(label), "vcd build: %s at %u Hz", what, freq);
  if (expect == NULL) {
    check((g_indicator_seen == 0) && (g_data_drives == 0), label);
    return;
  }
  check(g_indicator_seen != 0, label);
  if (g_indicator_seen != 0) {
    char decoded[SCEX_BITS + 1];
    decode_region(avr, &t85, 0, BIT_CYCLES(freq), decoded);
    (void)snprintf(label, sizeof(label), "vcd build: decodes SCEI at %u Hz", freq);
    check(strcmp(decoded, expect) == 0, label);
  }
}

// The SCPH-5903 build must unlock a game and stay silent on a Video CD, whose
// lead-in has 0x02 in frame[3] (PsNee V9.0, PSNee.ino:471-502).
void scenario_vcd(const char *elf, uint32_t freq) {
  const uint8_t game[SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0x00U, 0, 0, 0, 0, 0, 0, 0, 0 };
  const uint8_t vcd[SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0x02U, 0, 0, 0, 0, 0, 0, 0, 0 };
  const uint8_t spiral[SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0x01U, 0x98U, 0, 0, 0, 0, 0, 0, 0, 0 };
  vcd_case(elf, freq, game, SCEI_BITS, "game TOC injects");
  vcd_case(elf, freq, vcd, NULL, "video CD lead-in does not inject");
  vcd_case(elf, freq, spiral, NULL, "point-01 spiral does not inject");
}

// The real SQCK rate is unknown: a whole injection must still decode with
// frames clocked at FAST_EDGE_NS.
void scenario_fast_sqck(const target_t *t, const char *elf, uint32_t freq) {
  g_edge_ns = FAST_EDGE_NS;
  scenario_inject(t, elf, freq, 0, 1, WFCK_HZ, "fast SQCK, 4.7 us half period");
  g_edge_ns = EDGE_NS;
}

// On a gate board the chip holds WFCK low for the whole string, as PsNee and
// Mayumi V4 do; on a carrier board it never drives WFCK.
static uint8_t wfck_driven_low(avr_t *avr, const target_t *t) {
  avr_ioport_state_t state;
  (void)avr_ioctl(avr, AVR_IOCTL_IOPORT_GETSTATE((uint32_t)t->port), &state);
  uint8_t output = (uint8_t)((state.ddr >> t->wfck) & 1U);
  uint8_t high = (uint8_t)((state.port >> t->wfck) & 1U);
  return (uint8_t)((output != 0U) && (high == 0U));
}

void scenario_gate(const target_t *t, const char *elf, uint32_t freq, int modern) {
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, (uint32_t)(freq / (2UL * WFCK_HZ)) };
  avr_irq_register_notify(pin_irq(avr, t, t->indicator), on_indicator, avr);
  boot_quiet(avr, t, modern, &ctx);
  const uint8_t toc[SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  clock_until_inject(avr, t, toc);
  uint64_t deadline = avr->cycle + INDICATOR_DEADLINE;
  while ((g_indicator_seen == 0) && (avr->cycle < deadline)) {
    run_cycles(avr, 2000U);
  }
  run_to(avr, g_indicator_cycle + ms_cycles(80U));
  uint8_t during = wfck_driven_low(avr, t);
  run_to(avr, g_indicator_cycle + ms_cycles(210U));
  uint8_t after = wfck_driven_low(avr, t);

  char label[96];
  const char *board = (modern != 0) ? "carrier" : "gate";
  (void)snprintf(label,
                 sizeof(label),
                 "%s board: WFCK %s during a string",
                 board,
                 (modern != 0) ? "left alone" : "held low");
  check((g_indicator_seen != 0) && (during == ((modern != 0) ? 0U : 1U)), label);
  (void)snprintf(label, sizeof(label), "%s board: WFCK released after the string", board);
  check(after == 0U, label);
}

// Strings are spaced by the stealth gap of five frames: the chip lets five
// frames pass after a string and fires on the sixth, so the quiet time between
// two strings is at least five of this harness's 12.2 ms frames (96 SQCK bits of
// 28.3 us plus the 9.45 ms idle gap). The exact count is pinned in the host test.
#define GAP_TOC_FRAMES 80
#define GAP_MIN_MS 60U

void scenario_string_gap(const target_t *t, const char *elf, uint32_t freq) {
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, 0U };
  avr_irq_register_notify(pin_irq(avr, t, t->indicator), on_indicator, avr);
  boot_quiet(avr, t, 0, &ctx);
  const uint8_t toc[SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  clock_frames(avr, t, toc, GAP_TOC_FRAMES);

  uint64_t ends[2] = { 0U, 0U };
  uint64_t starts[2] = { 0U, 0U };
  int found = 0;
  for (int q = 0; (q < g_pulses) && (found < 2); q++) {
    uint64_t len = g_pulse_len[q];
    if ((g_pulse_rise[q] >= DETECT_CYCLES) && (len >= ms_cycles(80U)) && (len <= ms_cycles(200U))) {
      starts[found] = g_pulse_rise[q];
      ends[found] = g_pulse_rise[q] + len;
      found++;
    }
  }
  char label[96];
  (void)snprintf(label, sizeof(label), "%s: strings are spaced by the stealth gap", t->mcu);
  check((found == 2) && ((starts[1] - ends[0]) >= ms_cycles(GAP_MIN_MS)), label);
}

// A slow, noisy SQCK can keep every edge wait within its 30 ms bound while a
// frame takes seconds. The capture kicks the watchdog once per frame, so the
// chip resets with DATA released. 40 edges, 0.8 s, exceed one watchdog period.
#define SLOW_SQCK_EDGES 40
#define SLOW_SQCK_EDGE_MS 20U
#define SLOW_SQCK_CODE_MS 9000U

void scenario_slow_sqck(const target_t *t, const char *elf, uint32_t freq) {
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, 0U };
  avr_irq_register_notify(pin_irq(avr, t, t->indicator), on_indicator, avr);
  boot_quiet(avr, t, 0, &ctx);
  avr_irq_t *sqck = pin_irq(avr, t, t->sqck);
  uint64_t started = avr->cycle;
  for (int edge = 0; edge < SLOW_SQCK_EDGES; edge++) {
    avr_raise_irq(sqck, (uint8_t)(edge & 1));
    run_cycles(avr, ms_cycles(SLOW_SQCK_EDGE_MS));
  }
  avr_raise_irq(sqck, 1U);
  char label[96];
  (void)snprintf(label, sizeof(label), "%s: a slow SQCK leaves DATA released", t->mcu);
  check(data_ddr(avr, t) == 0U, label);
  run_cycles(avr, ms_cycles(SLOW_SQCK_CODE_MS));
  (void)snprintf(label, sizeof(label), "%s: a slow SQCK trips the watchdog, code 5", t->mcu);
  check(code_after(started) == 5, label);
}

// A lead-in of frames with a bad CRC must never arm a string; the same frames
// with a good CRC do.
void scenario_bad_crc(const target_t *t, const char *elf, uint32_t freq) {
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, 0U };
  g_indicator_seen = 0;
  avr_irq_register_notify(pin_irq(avr, t, t->indicator), on_indicator, avr);
  boot_quiet(avr, t, 0, &ctx);
  const uint8_t toc[SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  g_seal_frames = 0;
  clock_frames(avr, t, toc, GAP_TOC_FRAMES);
  g_seal_frames = 1;
  char label[96];
  (void)snprintf(label, sizeof(label), "%s: a lead-in failing its CRC never arms", t->mcu);
  check((g_indicator_seen == 0) && (g_data_drives == 0), label);
  // Judged on DATA, not the lamp: the debug image's waiting heartbeat lights
  // the lamp on its own, which would pass a chip that never armed.
  for (int i = 0; (i < (TRIGGER_FRAMES + TRIGGER_SPARE_FRAMES)) && (g_data_drives == 0); i++) {
    clock_frame(avr, t, toc);
  }
  uint64_t deadline = avr->cycle + INDICATOR_DEADLINE;
  while ((g_data_drives == 0) && (avr->cycle < deadline)) {
    run_cycles(avr, 2000U);
  }
  (void)snprintf(label, sizeof(label), "%s: the same lead-in with its CRC arms", t->mcu);
  check(g_data_drives != 0, label);
}

void scenario_families(const target_t *t, const char *elf, uint32_t freq) {
  for (int f = 0; f < FAMILY_COUNT; f++) {
    scenario_inject(t, elf, freq, FAMILIES[f].modern, 1, FAMILIES[f].wfck_hz, FAMILIES[f].name);
  }
  scenario_inject(t, elf, freq, 0, 0, WFCK_HZ, "legacy non-TOC negative");
}

// The gap check before each capture must last 1 ms of idle SQCK (PsNee's resync
// gap) and no more, or a disc's first frame can go unseen. Timed in the built
// image by the program counter.
#define GAP_CHECK_MIN_NS 1000000ULL
#define GAP_CHECK_MAX_NS 1050000ULL
#define GAP_CHECK_LIMIT_MS 200U

void scenario_gap_check(const target_t *t, const char *elf, uint32_t freq) {
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, 0U };
  boot_quiet(avr, t, 0, &ctx);
  uint64_t limit = avr->cycle + ms_cycles(GAP_CHECK_LIMIT_MS);
  uint64_t entry = 0U;
  uint64_t first_read = 0U;
  uint64_t next = 0U;
  while ((avr->cycle < limit) && (next == 0U)) {
    uint32_t pc = avr->pc;
    if ((pc == g_addr_capture) && (entry == 0U)) {
      entry = avr->cycle;
    } else if ((pc == g_addr_read_sqck) && (entry != 0U) && (first_read == 0U)) {
      first_read = avr->cycle;
    } else if ((pc == g_addr_capture) && (first_read != 0U)) {
      next = avr->cycle;
    }
    avr_run(avr);
  }
  uint64_t gap = next - first_read;
  char label[112];
  (void)snprintf(label,
                 sizeof(label),
                 "%s: the gap check lasts 1 ms of idle SQCK (%llu us)",
                 t->mcu,
                 (unsigned long long)((gap * 1000000ULL) / freq));
  check((g_addr_capture != 0U) && (g_addr_read_sqck != 0U) && (next != 0U) &&
            (gap >= ns_cycles(GAP_CHECK_MIN_NS)) && (gap <= ns_cycles(GAP_CHECK_MAX_NS)),
        label);
}

// Rule 6: a build emits only its own region's word. A lead-in long enough for the
// whole stealth cap, on a gate board, with every string of the burst decoded.
#define REGION_TOC_FRAMES 420
#define REGION_CAP 16

void scenario_region(
    const target_t *t, const char *elf, const char *expect, const char *name, uint32_t freq) {
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, 0U };
  avr_irq_register_notify(pin_irq(avr, t, t->indicator), on_indicator, avr);
  boot_quiet(avr, t, 0, &ctx);
  const uint8_t toc[SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  clock_frames(avr, t, toc, REGION_TOC_FRAMES);
  int matched = decode_bursts(BIT_CYCLES(freq), expect);
  char label[112];
  (void)snprintf(label,
                 sizeof(label),
                 "%s build: all %d strings of the burst carry its word (%d bursts, %d match)",
                 name,
                 REGION_CAP,
                 g_data_bursts,
                 matched);
  check((g_data_bursts == REGION_CAP) && (matched == REGION_CAP), label);
}

// The boot detection window is 10000 WFCK samples; engine.c states its length
// in time from the cycles one sample takes. Timed in the built image by the
// program counter, from the first read of WFCK to the 10000th.
#define DETECT_SAMPLES 10000U
#define DETECT_LIMIT_MS 1500U
// The 42 cycles engine.c and board_mode.h state, give or take two.
#define DETECT_CYCLES_MIN 40U
#define DETECT_CYCLES_MAX 44U

void scenario_detect_timing(const target_t *t, const char *elf, uint32_t freq) {
  avr_t *avr = build_avr(t, elf, freq);
  avr_raise_irq(pin_irq(avr, t, t->wfck), 1U);
  uint64_t limit = avr->cycle + ms_cycles(DETECT_LIMIT_MS);
  uint64_t first = 0U;
  uint64_t last = 0U;
  unsigned reads = 0U;
  while ((avr->cycle < limit) && (reads < DETECT_SAMPLES)) {
    if (avr->pc == g_addr_read_wfck) {
      first = (reads == 0U) ? avr->cycle : first;
      last = avr->cycle;
      reads++;
    }
    avr_run(avr);
  }
  uint64_t per_sample = (reads > 1U) ? ((last - first) / (reads - 1U)) : 0U;
  char label[112];
  (void)snprintf(label,
                 sizeof(label),
                 "%s: one boot WFCK sample takes %llu cycles",
                 t->mcu,
                 (unsigned long long)per_sample);
  check((reads == DETECT_SAMPLES) && (per_sample >= DETECT_CYCLES_MIN) &&
            (per_sample <= DETECT_CYCLES_MAX),
        label);
}
