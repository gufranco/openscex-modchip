// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <string.h>

#include "avr_ioport.h"
#include "harness.h"
#include "scenarios.h"
#include "sim_cycle_timers.h"

// Injection scenarios: every board family decodes the right word, results,
// resync after a partial burst, a late or stalled carrier, a fast SQCK, the VCD
// build, the gate line on legacy boards and the gap between strings.

static void scenario_inject(const target_t *t,
                            const char *elf,
                            uint32_t freq,
                            int modern,
                            int trigger,
                            uint32_t wfck_hz,
                            const char *family) {
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, (uint32_t)(freq / (2UL * wfck_hz)) };
  g_led_seen = 0;
  g_led_cycle = 0;
  avr_irq_register_notify(pin_irq(avr, t, t->led), on_led, avr);
  boot_quiet(avr, t, modern, &ctx);

  // A lead-in frame that must arm injection (control 0x41 = data sector,
  // track 0xA0 = TOC) versus an ordinary audio frame that must not (control
  // 0x01, track 0x02). frame[1] and frame[6] are zero so both parse as framed.
  uint8_t toc[SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  uint8_t audio[SUBQ_FRAME_BYTES] = { 0x01U, 0x00U, 0x02U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  clock_until_inject(avr, t, (trigger != 0) ? toc : audio);

  uint64_t deadline = avr->cycle + LED_DEADLINE;
  while ((g_led_seen == 0) && (avr->cycle < deadline)) {
    run_cycles(avr, 2000U);
  }

  // A legacy bit is a fixed MCU-delay cell; an adaptive modern bit is
  // WFCK_PERIODS_PER_BIT carrier periods, so its length scales with the carrier
  // frequency and the decoder must measure it from wfck_hz, not a constant.
  uint64_t bit_cycles =
      (modern != 0) ? ((uint64_t)WFCK_PERIODS_PER_BIT * freq / wfck_hz) : BIT_CYCLES(freq);
  const char *tag = family;
  char label[96];
  char decoded[SCEX_BITS + 1];
  if (trigger != 0) {
    (void)snprintf(label, sizeof(label), "%s: inject triggered at %u Hz", tag, freq);
    check(g_led_seen != 0, label);
    if (g_led_seen != 0) {
      decode_region(avr, t, modern, bit_cycles, decoded);
      (void)snprintf(label, sizeof(label), "%s: decodes SCEA at %u Hz", tag, freq);
      check(strcmp(decoded, SCEA_BITS) == 0, label);
      if (strcmp(decoded, SCEA_BITS) != 0) {
        (void)printf("  expected %s\n  decoded  %s\n", SCEA_BITS, decoded);
      }
    }
  } else {
    (void)snprintf(label, sizeof(label), "%s: non-TOC does not inject at %u Hz", tag, freq);
    check(g_led_seen == 0, label);
  }
}

// The LED reports each disc's result after its check: one long flash repeated
// for a disc the console accepted, two for one it never accepted, each code
// three times and then dark. A legacy board is driven with TOC frames to arm
// injection, then with program-area frames (accepted) or framed silence until
// the confirmation wait expires (refused).
#define RESULT_TOC_FRAMES 12
#define RESULT_AFTER_FRAMES 120

void result_case(const target_t *t, const char *elf, uint32_t freq, int program) {
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, 0U };
  avr_irq_register_notify(pin_irq(avr, t, t->led), on_led, avr);
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
                 "led %s: %s disc shows code %d",
                 t->mcu,
                 (program != 0) ? "accepted" : "refused",
                 expected);
  check((g_led_seen != 0) && (code_after(mark) == expected), label);
}

// A capture that starts inside a burst must not stay misaligned. Present a
// partial burst first, as a console already mid-sector would at the moment the
// firmware starts listening, then ordinary TOC frames. Without resync every
// later capture straddles two frames by the partial's length, nothing ever
// parses as framed, and injection never fires; with it the firmware loses at
// most one frame, realigns on the next inter-frame gap, and injects.
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
  g_led_seen = 0;
  g_led_cycle = 0;
  avr_irq_register_notify(pin_irq(avr, t, t->led), on_led, avr);
  boot_quiet(avr, t, 0, &ctx);

  uint8_t toc[SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  clock_partial(avr, t, PARTIAL_BURST_BITS);
  for (int i = 0; i < RESYNC_FRAMES; i++) {
    clock_frame(avr, t, toc);
  }
  uint64_t deadline = avr->cycle + LED_DEADLINE;
  while ((g_led_seen == 0) && (avr->cycle < deadline)) {
    run_cycles(avr, 2000U);
  }

  const char *tag = t->mcu;
  char label[96];
  (void)snprintf(label, sizeof(label), "resync %s: injects after a capture starts mid-burst", tag);
  check(g_led_seen != 0, label);
}

// A carrier board whose WFCK starts oscillating after the chip has powered up
// must still be detected as a carrier board. The oscillation starts 150 ms after
// power-on: a detect window taken immediately would see a static line, choose the
// legacy gate, and send every logic one as high-Z instead of the carrier mirror,
// so the modern decode would fail. The settle delay lets the carrier start first.
void scenario_late_carrier(const target_t *t, const char *elf, uint32_t freq) {
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, (uint32_t)(freq / (2UL * WFCK_HZ)) };
  g_led_seen = 0;
  g_led_cycle = 0;
  avr_irq_register_notify(pin_irq(avr, t, t->led), on_led, avr);
  avr_raise_irq(pin_irq(avr, t, t->sqck), 1U);
  avr_raise_irq(pin_irq(avr, t, t->subq), 0U);
  avr_raise_irq(pin_irq(avr, t, t->wfck), 1U);
  run_cycles(avr, LATE_CARRIER_CYCLES);
  ctx.irq = pin_irq(avr, t, t->wfck);
  avr_cycle_timer_register(avr, ctx.half, wfck_tick, &ctx);
  run_to(avr, DETECT_CYCLES);

  uint8_t toc[SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  clock_until_inject(avr, t, toc);
  uint64_t deadline = avr->cycle + LED_DEADLINE;
  while ((g_led_seen == 0) && (avr->cycle < deadline)) {
    run_cycles(avr, 2000U);
  }

  const char *tag = t->mcu;
  char label[96];
  char decoded[SCEX_BITS + 1] = { 0 };
  (void)snprintf(label, sizeof(label), "late carrier %s: injection triggered", tag);
  check(g_led_seen != 0, label);
  if (g_led_seen != 0) {
    decode_region(avr, t, 1, (uint64_t)WFCK_PERIODS_PER_BIT * freq / WFCK_HZ, decoded);
    (void)snprintf(label, sizeof(label), "late carrier %s: detected as a carrier board", tag);
    check(strcmp(decoded, SCEA_BITS) == 0, label);
  }
}

// A carrier that stops mid-injection must not leave DATA driven. The adaptive
// bit cell counts WFCK rising edges and kicks the watchdog only on a counted
// edge, so when the carrier stalls the watchdog expires and resets the chip,
// which returns DATA to high-Z. Stop the carrier about 25 ms into an injection
// (a few bit cells in, while DATA is driven) and check it is released within
// the next 0.75 s, comfortably past the ~0.5 s watchdog timeout.
#define STALL_AFTER_CYCLES ns_cycles(47000000ULL)
#define STALL_WAIT_CYCLES ns_cycles(1417000000ULL)

void scenario_stall(const target_t *t, const char *elf, uint32_t freq) {
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, (uint32_t)(freq / (2UL * WFCK_HZ)) };
  g_led_seen = 0;
  g_led_cycle = 0;
  avr_irq_register_notify(pin_irq(avr, t, t->led), on_led, avr);
  boot_quiet(avr, t, 1, &ctx);

  uint8_t toc[SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  clock_until_inject(avr, t, toc);
  uint64_t deadline = avr->cycle + LED_DEADLINE;
  while ((g_led_seen == 0) && (avr->cycle < deadline)) {
    run_cycles(avr, 2000U);
  }

  const char *tag = t->mcu;
  char label[96];
  (void)snprintf(label, sizeof(label), "stall %s: injection started", tag);
  check(g_led_seen != 0, label);
  run_to(avr, g_led_cycle + STALL_AFTER_CYCLES);
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
}

// The board-family matrix. The firmware has no per-family code path, only the
// legacy static gate and the live WFCK carrier, so each family collapses to one
// of those with its carrier frequency. Distinct behaviours only, no duplicate
// runs: the two carrier rows differ by the init and read frequencies that bound
// the WFCK band.
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

// Boot one legacy-board image, clock copies of one lead-in frame until a string
// starts or the spare frames run out, and report whether the LED marked an
// injection; when it did and an expected word is given, decode DATA and compare
// it. Each call is a fresh power-on, so one frame kind is judged on its own.
static void vcd_case(
    const char *elf, uint32_t freq, const uint8_t *frame, const char *expect, const char *what) {
  target_t t85 = { "attiny85", 'B', 0U, 1U, 2U, 3U, 4U };
  avr_t *avr = build_avr(&t85, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, 0U };
  g_led_seen = 0;
  g_led_cycle = 0;
  avr_irq_register_notify(pin_irq(avr, &t85, t85.led), on_led, avr);
  boot_quiet(avr, &t85, 0, &ctx);
  clock_until_inject(avr, &t85, frame);
  uint64_t deadline = avr->cycle + LED_DEADLINE;
  while ((g_led_seen == 0) && (avr->cycle < deadline)) {
    run_cycles(avr, 2000U);
  }

  char label[96];
  (void)snprintf(label, sizeof(label), "vcd build: %s at %u Hz", what, freq);
  if (expect == NULL) {
    check(g_led_seen == 0, label);
    return;
  }
  check(g_led_seen != 0, label);
  if (g_led_seen != 0) {
    char decoded[SCEX_BITS + 1];
    decode_region(avr, &t85, 0, BIT_CYCLES(freq), decoded);
    (void)snprintf(label, sizeof(label), "vcd build: decodes SCEI at %u Hz", freq);
    check(strcmp(decoded, expect) == 0, label);
  }
}

// The SCPH-5903 build (REGION=jp VCD_FILTER=on) must still unlock a game, whose
// lead-in carries the TOC markers, and must stay silent on a Video CD, whose
// lead-in marker carries 0x02 in frame[3]. The point-01 spiral frame that arms
// the ordinary build must not arm this one either. Read: PsNee V9.0 SCPH_5903
// FilterSUBQSamples, PSNee.ino:471-502.
void scenario_vcd(const char *elf, uint32_t freq) {
  const uint8_t game[SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0x00U, 0, 0, 0, 0, 0, 0, 0, 0 };
  const uint8_t vcd[SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0x02U, 0, 0, 0, 0, 0, 0, 0, 0 };
  const uint8_t spiral[SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0x01U, 0x98U, 0, 0, 0, 0, 0, 0, 0, 0 };
  vcd_case(elf, freq, game, SCEI_BITS, "game TOC injects");
  vcd_case(elf, freq, vcd, NULL, "video CD lead-in does not inject");
  vcd_case(elf, freq, spiral, NULL, "point-01 spiral does not inject");
}

// The console's real SQCK rate is not documented anywhere this project cites,
// so the capture has to keep a wide margin. Drive a whole injection on a legacy
// board with frames clocked at FAST_EDGE_NS and require the region word to
// decode, then restore the normal rate for the scenarios that follow.
void scenario_fast_sqck(const target_t *t, const char *elf, uint32_t freq) {
  g_edge_ns = FAST_EDGE_NS;
  scenario_inject(t, elf, freq, 0, 1, WFCK_HZ, "fast SQCK, 4.7 us half period");
  g_edge_ns = EDGE_NS;
}

// The gate line. On a gate board the chip holds WFCK low for the whole string
// and releases it with DATA, as PsNee and Mayumi V4 do; on a carrier board WFCK
// is the console's clock and the chip never drives it. Sampled 80 ms into the
// string, then 30 ms after its 177 ms end.
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
  avr_irq_register_notify(pin_irq(avr, t, t->led), on_led, avr);
  boot_quiet(avr, t, modern, &ctx);
  const uint8_t toc[SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  clock_until_inject(avr, t, toc);
  uint64_t deadline = avr->cycle + LED_DEADLINE;
  while ((g_led_seen == 0) && (avr->cycle < deadline)) {
    run_cycles(avr, 2000U);
  }
  run_to(avr, g_led_cycle + ms_cycles(80U));
  uint8_t during = wfck_driven_low(avr, t);
  run_to(avr, g_led_cycle + ms_cycles(210U));
  uint8_t after = wfck_driven_low(avr, t);

  char label[96];
  const char *board = (modern != 0) ? "carrier" : "gate";
  (void)snprintf(label,
                 sizeof(label),
                 "%s board: WFCK %s during a string",
                 board,
                 (modern != 0) ? "left alone" : "held low");
  check((g_led_seen != 0) && (during == ((modern != 0) ? 0U : 1U)), label);
  (void)snprintf(label, sizeof(label), "%s board: WFCK released after the string", board);
  check(after == 0U, label);
}

// Strings are spaced by the stealth gap, five frames, not sent back to back:
// the quiet time between the first two strings must exceed four frame periods,
// against about 5 ms before the gap existed.
// Long enough for the first string, the gap and the whole second string, which
// is counted only when it ends.
#define GAP_TOC_FRAMES 80
#define GAP_MIN_MS 45U

void scenario_string_gap(const target_t *t, const char *elf, uint32_t freq) {
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, 0U };
  avr_irq_register_notify(pin_irq(avr, t, t->led), on_led, avr);
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

void scenario_families(const target_t *t, const char *elf, uint32_t freq) {
  for (int f = 0; f < FAMILY_COUNT; f++) {
    scenario_inject(t, elf, freq, FAMILIES[f].modern, 1, FAMILIES[f].wfck_hz, FAMILIES[f].name);
  }
  scenario_inject(t, elf, freq, 0, 0, WFCK_HZ, "legacy non-TOC negative");
}
