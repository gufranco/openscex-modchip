// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include <stdio.h>

#include "harness.h"
#include "scenarios.h"

// Exact counts and lengths in the platform C (src/engine.c, src/run.c): how
// many SQCK polls qualify the frame gap, how many WFCK samples decide the
// board, how many cells a string has, which frame pairs time the trim, and
// that the dark stage stays dark while nothing is learned. Each is set by a
// constant a one-off error would move without breaking the coarser checks.

static const uint8_t TIMING_TOC[SUBQ_FRAME_BYTES] = {
  0x41U, 0x00U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0
};
static const uint8_t TIMING_PLAY[SUBQ_FRAME_BYTES] = { 0x41U, 0x01U, 0x01U, 0x00U, 0x02U, 0,
                                                       0,     0,     0x02U, 0,     0,     0 };

// The idle gap is PSCU_SQCK_IDLE_POLLS reads of SQCK high in a row: 1 ms at
// 41 cycles a poll, rounded up, plus one (src/engine.c).
#define GAP_POLL_CYCLES 41U
#define GAP_LIMIT_MS 200U
#define GAP_LOW_US 100U

static uint32_t gap_polls(uint32_t freq) {
  uint32_t per_ms = 1000U * GAP_POLL_CYCLES;
  return ((freq + per_ms - 1U) / per_ms) + 1U;
}

// From inside one capture, run until the next one starts, counting the SQCK
// reads between them. Returns the count, or 0 when no capture came in time.
static unsigned reads_to_capture(avr_t *avr) {
  uint64_t limit = avr->cycle + ms_cycles(GAP_LIMIT_MS);
  unsigned reads = 0U;
  int left_capture = 0;
  while (avr->cycle < limit) {
    uint32_t pc = avr->pc;
    if ((pc == g_addr_capture) && (left_capture != 0)) {
      return reads;
    }
    left_capture = left_capture || (pc != g_addr_capture);
    reads += (pc == g_addr_read_sqck) ? 1U : 0U;
    sim_step(avr);
  }
  return 0U;
}

static void run_to_pc(avr_t *avr, uint32_t target) {
  uint64_t limit = avr->cycle + ms_cycles(GAP_LIMIT_MS);
  while ((avr->cycle < limit) && (avr->pc != target)) {
    sim_step(avr);
  }
}

// With SQCK idle high, the gap check reads it exactly the stated number of
// times; after a low blip inside the wait, it starts over and again needs
// every one of them.
void scenario_gap_polls(const target_t *t, const char *elf, uint32_t freq) {
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, 0U };
  boot_quiet(avr, t, 0, &ctx);
  run_to_pc(avr, g_addr_capture);
  unsigned steady = reads_to_capture(avr);

  // The capture just entered waits for an edge and gives up; the next gap
  // check then reads SQCK high, and the blip goes in halfway through, so a
  // check that kept its count instead of starting over would finish early.
  unsigned half = gap_polls(freq) / 2U;
  unsigned seen = 0U;
  uint64_t limit = avr->cycle + ms_cycles(GAP_LIMIT_MS);
  run_to_pc(avr, g_addr_read_sqck);
  while ((seen < half) && (avr->cycle < limit)) {
    seen += (avr->pc == g_addr_read_sqck) ? 1U : 0U;
    sim_step(avr);
  }
  avr_irq_t *sqck = pin_irq(avr, t, t->sqck);
  avr_raise_irq(sqck, 0U);
  run_cycles(avr, ns_cycles((uint64_t)GAP_LOW_US * 1000ULL));
  avr_raise_irq(sqck, 1U);
  unsigned after_low = reads_to_capture(avr);

  unsigned want = gap_polls(freq);
  char label[112];
  (void)snprintf(label,
                 sizeof(label),
                 "%s: the gap check reads SQCK high %u times (%u, %u after a low)",
                 t->mcu,
                 want,
                 steady,
                 after_low);
  check((g_addr_capture != 0U) && (steady == want) && (after_low == want), label);
}

// Boot detection samples WFCK exactly PSCU_DETECT_WINDOW times; nothing else
// reads it before a string is due.
#define DETECT_WINDOW_SAMPLES 10000U
#define DETECT_WATCH_MS 1500U

void scenario_detect_samples(const target_t *t, const char *elf, uint32_t freq) {
  avr_t *avr = build_avr(t, elf, freq);
  avr_raise_irq(pin_irq(avr, t, t->sqck), 1U);
  avr_raise_irq(pin_irq(avr, t, t->wfck), 1U);
  uint64_t limit = avr->cycle + ms_cycles(DETECT_WATCH_MS);
  unsigned reads = 0U;
  while (avr->cycle < limit) {
    reads += (avr->pc == g_addr_read_wfck) ? 1U : 0U;
    sim_step(avr);
  }
  char label[96];
  (void)snprintf(label, sizeof(label), "%s: boot detection takes %u WFCK samples", t->mcu, reads);
  check((g_addr_read_wfck != 0U) && (reads == DETECT_WINDOW_SAMPLES), label);
}

// A string is 44 cells of 4 ms; the debug lamp is lit from before the first
// cell to after the last, so its pulse is 176 ms and under one more cell.
#define STRING_CELLS 44U
#define STRING_CELL_MS 4U
#define STRING_LEAD_FRAMES 30

void scenario_string_cells(const target_t *t, const char *elf, uint32_t freq) {
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, 0U };
  avr_irq_register_notify(pin_irq(avr, t, t->indicator), on_indicator, avr);
  boot_quiet(avr, t, 0, &ctx);
  clock_frames(avr, t, TIMING_TOC, STRING_LEAD_FRAMES);
  uint64_t len = 0U;
  for (int i = 0; (i < g_pulses) && (len == 0U); i++) {
    int string = (g_pulse_rise[i] >= DETECT_CYCLES) && (g_pulse_len[i] >= ms_cycles(80U)) &&
                 (g_pulse_len[i] <= ms_cycles(200U));
    len = string ? g_pulse_len[i] : 0U;
  }
  uint64_t cells = ms_cycles(STRING_CELLS * STRING_CELL_MS);
  char label[96];
  (void)snprintf(label,
                 sizeof(label),
                 "%s: a string lights the lamp for 44 cells (%llu us)",
                 t->mcu,
                 (unsigned long long)((len * 1000000ULL) / freq));
  check((len >= cells) && (len < (cells + ms_cycles(STRING_CELL_MS))), label);
}

// After an accepted disc's result has played (three cycles of code 1, 9 s in
// the debug profile), the display goes dark for play. A pass that learns
// nothing must not tick it, so the lamp stays off however long the game runs.
#define DARK_LEAD_FRAMES 12
#define DARK_PLAY_FRAMES 1000
#define DARK_FROM_MS 10000U
#define DARK_LIT_MAX_MS 100U

void scenario_dark_play(const target_t *t, const char *elf, uint32_t freq) {
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, 0U };
  avr_irq_register_notify(pin_irq(avr, t, t->indicator), on_indicator, avr);
  boot_quiet(avr, t, 0, &ctx);
  clock_frames(avr, t, TIMING_TOC, DARK_LEAD_FRAMES);
  uint64_t dark_from = avr->cycle + ms_cycles(DARK_FROM_MS);
  clock_frames(avr, t, TIMING_PLAY, DARK_PLAY_FRAMES);
  uint64_t played = avr->cycle;
  uint64_t lit = 0U;
  for (int i = 0; i < g_pulses; i++) {
    lit += (g_pulse_rise[i] >= dark_from) ? g_pulse_len[i] : 0U;
  }
  lit += (g_rise != 0U) ? (avr->cycle - g_rise) : 0U;
  char label[112];
  (void)snprintf(label,
                 sizeof(label),
                 "%s: the lamp stays dark through play (%llu ms lit)",
                 t->mcu,
                 (unsigned long long)((lit * 1000ULL) / freq));
  check((played > dark_from) && (lit <= ms_cycles(DARK_LIT_MAX_MS)), label);
}

// Only two captured frames in a row time the trim. A failed capture has a
// stamp too, taken when its edge wait gives up, and the next frame can land
// a frame period after it; that interval measures the outage, not the disc.
// SQCK held low makes the capture fail about 30 ms in; the next frame then
// ends 16 ms after that, inside the accepted 10 to 18 ms. Counted, those
// intervals would read as an RC 6 percent fast and walk OSCCAL down four
// notches a batch. Real frame pairs at nominal speed leave only the Timer1
// rounding of short runs, a notch or two either way. The frames are valid
// program-area ones: only valid frames are timed.
#define OUTAGE_RUN_FRAMES 3
#define OUTAGE_LOW_MS 40U
#define OUTAGE_RESUME_MS 43U
#define OUTAGE_CYCLES 100
#define OUTAGE_DRIFT_MAX 2

void scenario_trim_outage(const target_t *t, const char *elf, uint32_t freq) {
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, 0U };
  boot_quiet(avr, t, 0, &ctx);
  avr_irq_t *sqck = pin_irq(avr, t, t->sqck);
  g_frame_period_ns = SIM_SECTOR_NS;
  for (int cycle = 0; cycle < OUTAGE_CYCLES; cycle++) {
    clock_frames(avr, t, TIMING_PLAY, OUTAGE_RUN_FRAMES);
    uint64_t low_at = avr->cycle;
    avr_raise_irq(sqck, 0U);
    run_cycles(avr, ms_cycles(OUTAGE_LOW_MS));
    avr_raise_irq(sqck, 1U);
    run_to(avr, low_at + ms_cycles(OUTAGE_RESUME_MS));
  }
  g_frame_period_ns = 0U;
  int drift = (int)avr->data[SIM_OSCCAL_ADDR] - (int)SIM_OSCCAL_FACTORY;
  char label[112];
  (void)snprintf(label,
                 sizeof(label),
                 "%s: a frame after a failed capture never times the trim (drift %d)",
                 t->mcu,
                 drift);
  check((drift >= -OUTAGE_DRIFT_MAX) && (drift <= OUTAGE_DRIFT_MAX), label);
}
