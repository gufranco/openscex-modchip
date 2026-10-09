// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include <stdio.h>

#include "harness.h"
#include "scenarios.h"
#include "sim_cycle_timers.h"

// The two indicator profiles (include/pscu/led.h), run as whole console
// sessions. The final image must be quiet where a player would hear it, and
// both images must put the same thing on DATA at the same time, since the
// profile is only meant to change PB3.

// DATA is sampled every 0.1 ms, far finer than its 4 ms bit cells, and each
// change of its state is kept: driven or released (DDR), and the level driven.
#define PROFILE_SAMPLE_NS 100000ULL
#define PROFILE_TRACE_MAX 4096

// One session: lead-in frames to arm the window, then program-area frames, as
// result_case drives an accepted disc, and two quiet seconds after.
#define PROFILE_TOC_FRAMES 12
#define PROFILE_PLAY_FRAMES 600
#define PROFILE_TAIL_MS 2000U

// The debug image does a little more work around each string, lighting and
// clearing the pin, so its edges may move by a few microseconds. Within a
// string the edges must keep their spacing to half a millisecond, an eighth of
// a cell, and a string may start up to one frame, 13.3 ms, apart.
#define PROFILE_EDGE_TOLERANCE_NS 500000ULL
#define PROFILE_START_TOLERANCE_NS 14000000ULL

// A chirp is 60 ms; a pulse up to 80 ms is a chirp stretched by a loop pass.
// A code flash is 700 ms, so 600 ms separates the two.
#define PROFILE_CHIRP_MAX_MS 80U
#define PROFILE_LONG_MIN_MS 600U

// An audio CD: framed audio frames with no lead-in, for 32 s. Code 4 is due 20
// s after the disc arrives and takes 6 s to show; the debug profile would show
// it again at 26 s and 32 s, the final profile must not.
#define PROFILE_AUDIO_FRAMES 2400
#define PROFILE_NO_CHECK_FLASHES 4

typedef struct {
  const target_t *t;
  uint64_t at[PROFILE_TRACE_MAX];
  uint8_t level[PROFILE_TRACE_MAX];
  int count;
  uint8_t last;
} data_trace_t;

static data_trace_t g_debug_trace;
static data_trace_t g_final_trace;

static avr_cycle_count_t data_sample(avr_t *avr, avr_cycle_count_t when, void *param) {
  data_trace_t *trace = param;
  uint8_t level = (uint8_t)((uint8_t)(data_ddr(avr, trace->t) << 1U) | data_pin(avr, trace->t));
  if ((level != trace->last) && (trace->count < PROFILE_TRACE_MAX)) {
    trace->at[trace->count] = avr->cycle;
    trace->level[trace->count] = level;
    trace->count++;
  }
  trace->last = level;
  return when + ns_cycles(PROFILE_SAMPLE_NS);
}

static const uint8_t PROFILE_TOC[SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0, 0, 0,
                                                       0,     0,     0,     0, 0, 0 };
static const uint8_t PROFILE_PLAY[SUBQ_FRAME_BYTES] = { 0x41U, 0x01U, 0x01U, 0x00U, 0x02U, 0,
                                                        0,     0,     0x02U, 0,     0,     0 };
static const uint8_t PROFILE_AUDIO[SUBQ_FRAME_BYTES] = { 0x01U, 0x00U, 0x02U, 0, 0, 0,
                                                         0,     0,     0,     0, 0, 0 };

// A legacy-board session, so a one is DATA released and a zero DATA driven low
// with no carrier to alias against the sampling. The LED is not watched here:
// the harness stops clocking a frame when the LED marks a string, which would
// give the two images different scripts.
static void accepted_session(const target_t *t,
                             const char *elf,
                             uint32_t freq,
                             data_trace_t *trace) {
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, 0U };
  trace->t = t;
  trace->count = 0;
  trace->last = 0U;
  avr_cycle_timer_register(avr, ns_cycles(PROFILE_SAMPLE_NS), data_sample, trace);
  boot_quiet(avr, t, 0, &ctx);
  clock_frames(avr, t, PROFILE_TOC, PROFILE_TOC_FRAMES);
  clock_frames(avr, t, PROFILE_PLAY, PROFILE_PLAY_FRAMES);
  run_cycles(avr, ms_cycles(PROFILE_TAIL_MS));
}

static uint64_t distance(uint64_t a, uint64_t b) {
  return (a > b) ? (a - b) : (b - a);
}

// The same edges, in the same order, with the same spacing.
static int same_data(const data_trace_t *a, const data_trace_t *b) {
  int same = (a->count == b->count) && (a->count > 0) && (a->count < PROFILE_TRACE_MAX);
  if (same) {
    same = distance(a->at[0], b->at[0]) <= ns_cycles(PROFILE_START_TOLERANCE_NS);
  }
  for (int i = 0; same && (i < a->count); i++) {
    uint64_t span_a = a->at[i] - a->at[0];
    uint64_t span_b = b->at[i] - b->at[0];
    same = (a->level[i] == b->level[i]) &&
           (distance(span_a, span_b) <= ns_cycles(PROFILE_EDGE_TOLERANCE_NS));
  }
  return same;
}

static int pulses_longer_than(uint32_t ms) {
  int count = 0;
  for (int i = 0; i < g_pulses; i++) {
    count += (g_pulse_len[i] >= ms_cycles(ms)) ? 1 : 0;
  }
  return count;
}

static void final_accepted_case(const target_t *t, const char *elf, uint32_t freq) {
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, 0U };
  avr_irq_register_notify(pin_irq(avr, t, t->led), on_led, avr);
  boot_quiet(avr, t, 0, &ctx);
  clock_frames(avr, t, PROFILE_TOC, PROFILE_TOC_FRAMES);
  clock_frames(avr, t, PROFILE_PLAY, PROFILE_PLAY_FRAMES);
  run_cycles(avr, ms_cycles(PROFILE_TAIL_MS));

  char label[112];
  (void)snprintf(label,
                 sizeof(label),
                 "final: a played disc gives only the board and acceptance chirps (%d pulses)",
                 g_pulses);
  check((g_pulses == 2) && (pulses_longer_than(PROFILE_CHIRP_MAX_MS) == 0), label);
}

static void final_audio_case(const target_t *t, const char *elf, uint32_t freq) {
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, 0U };
  avr_irq_register_notify(pin_irq(avr, t, t->led), on_led, avr);
  boot_quiet(avr, t, 0, &ctx);
  clock_frames(avr, t, PROFILE_AUDIO, PROFILE_AUDIO_FRAMES);

  int long_flashes = pulses_longer_than(PROFILE_LONG_MIN_MS);
  char label[112];
  (void)snprintf(label,
                 sizeof(label),
                 "final: an audio CD shows code 4 once, then stays quiet (%d long flashes)",
                 long_flashes);
  check((long_flashes == PROFILE_NO_CHECK_FLASHES) && (g_pulses == PROFILE_NO_CHECK_FLASHES + 1),
        label);
}

void scenario_profiles(const target_t *t,
                       const char *debug_elf,
                       const char *final_elf,
                       uint32_t freq) {
  accepted_session(t, debug_elf, freq, &g_debug_trace);
  accepted_session(t, final_elf, freq, &g_final_trace);
  char label[112];
  (void)snprintf(label,
                 sizeof(label),
                 "profiles: debug and final put the same edges on DATA (%d and %d)",
                 g_debug_trace.count,
                 g_final_trace.count);
  check(same_data(&g_debug_trace, &g_final_trace), label);

  final_accepted_case(t, final_elf, freq);
  final_audio_case(t, final_elf, freq);
}
