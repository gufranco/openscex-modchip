// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include <stdio.h>

#include "harness.h"
#include "scenarios.h"
#include "sim_cycle_timers.h"

// The PB3 profiles (include/pscu/config.h). The final image must stay quiet, and
// the debug, final and trace images must drive DATA identically.

// DATA is sampled every 0.1 ms, and each change of direction or level is kept.
#define PROFILE_SAMPLE_NS 100000ULL
#define PROFILE_TRACE_MAX 4096

// One session: lead-in frames, program-area frames, then two quiet seconds.
// The lead-in is long enough for the whole stealth cap, so every string's
// spacing and the gaps between them are compared, not only the first string.
#define PROFILE_TOC_FRAMES 360
#define PROFILE_STRINGS 16
#define PROFILE_PLAY_FRAMES 600
#define PROFILE_TAIL_MS 2000U

// The debug image does a little more around each string, so edges may shift.
// Within a string, spacing must hold to 0.5 ms; each string may start up to one
// frame, 13.3 ms, apart, since strings start on frames. Edges further apart
// than a bit cell's worst gap begin a new string.
#define PROFILE_EDGE_TOLERANCE_NS 500000ULL
#define PROFILE_START_TOLERANCE_NS 14000000ULL
#define PROFILE_STRING_GAP_NS 40000000ULL

// A chirp is 60 ms; a pulse up to 80 ms is a chirp stretched by a loop pass.
// A code flash is 700 ms, so 600 ms separates the two.
#define PROFILE_CHIRP_MAX_MS 80U
#define PROFILE_LONG_MIN_MS 600U

// An audio CD for 32 s. Code 4 is due at 20 s; the debug profile repeats it at
// 26 s and 32 s, the final profile must not.
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
static data_trace_t g_trace_trace;

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

// A legacy-board session, so DATA has no carrier to alias against. The indicator is
// not watched, so both images get the same script.
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

// The strings in a trace: edges more than PROFILE_STRING_GAP_NS apart start
// a new one.
static int strings_in(const data_trace_t *a) {
  int strings = (a->count > 0) ? 1 : 0;
  for (int i = 1; i < a->count; i++) {
    strings += ((a->at[i] - a->at[i - 1]) > ns_cycles(PROFILE_STRING_GAP_NS)) ? 1 : 0;
  }
  return strings;
}

// The same edges, in the same order: each string starting within a frame of
// its counterpart, and each edge inside it at the same offset from that start.
static int same_data(const data_trace_t *a, const data_trace_t *b) {
  int same = (a->count == b->count) && (a->count > 0) && (a->count < PROFILE_TRACE_MAX);
  uint64_t start_a = 0U;
  uint64_t start_b = 0U;
  for (int i = 0; same && (i < a->count); i++) {
    int begins = (i == 0) || ((a->at[i] - a->at[i - 1]) > ns_cycles(PROFILE_STRING_GAP_NS));
    if (begins) {
      start_a = a->at[i];
      start_b = b->at[i];
      same = distance(start_a, start_b) <= ns_cycles(PROFILE_START_TOLERANCE_NS);
    }
    same =
        same && (a->level[i] == b->level[i]) &&
        (distance(a->at[i] - start_a, b->at[i] - start_b) <= ns_cycles(PROFILE_EDGE_TOLERANCE_NS));
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
  avr_irq_register_notify(pin_irq(avr, t, t->indicator), on_indicator, avr);
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
  avr_irq_register_notify(pin_irq(avr, t, t->indicator), on_indicator, avr);
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
                       const char *trace_elf,
                       uint32_t freq) {
  accepted_session(t, debug_elf, freq, &g_debug_trace);
  accepted_session(t, final_elf, freq, &g_final_trace);
  char label[112];
  (void)snprintf(label,
                 sizeof(label),
                 "profiles: the session sends the whole cap (%d strings)",
                 strings_in(&g_debug_trace));
  check(strings_in(&g_debug_trace) == PROFILE_STRINGS, label);
  (void)snprintf(label,
                 sizeof(label),
                 "profiles: debug and final put the same edges on DATA (%d and %d)",
                 g_debug_trace.count,
                 g_final_trace.count);
  check(same_data(&g_debug_trace, &g_final_trace), label);

  // The trace line goes out in the gap after each frame, so it must not move a
  // single DATA edge either.
  if (trace_elf != NULL) {
    accepted_session(t, trace_elf, freq, &g_trace_trace);
    (void)snprintf(label,
                   sizeof(label),
                   "profiles: debug and trace put the same edges on DATA (%d and %d)",
                   g_debug_trace.count,
                   g_trace_trace.count);
    check(same_data(&g_debug_trace, &g_trace_trace), label);
  }

  final_accepted_case(t, final_elf, freq);
  final_audio_case(t, final_elf, freq);
}
