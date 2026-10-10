// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "calib_test.h"
#include "disc_test.h"
#include "led_test.h"
#include "loop_test.h"
#include "pscu/board_mode.h"
#include "pscu/inject.h"
#include "pscu/region.h"
#include "pscu/subq.h"
#include "trim_test.h"

// Host tests for the pure logic layer, run with assertions on. SUBQ frames
// below follow the Q-channel byte layout: [0] control (0x4x = data sector, 0x01
// = audio), [1] TNO (0x00 in the lead-in, a BCD track number in the program
// area, 0xAA in the lead-out), [2] POINT in the lead-in or INDEX in the program
// area, [3] the running minute in BCD, [6] the ZERO byte. Only the bytes that
// matter to a given case are set; the rest stay zero.

static int g_checks = 0;
static int g_failures = 0;

static void check(bool cond, const char *name) {
  g_checks++;
  if (!cond) {
    g_failures++;
    (void)printf("FAIL: %s\n", name);
  }
}

static void region_to_bits(pscu_region_t region, char *out) {
  for (uint8_t i = 0U; i < PSCU_SCEX_BIT_COUNT; i++) {
    out[i] = pscu_region_bit(region, i) ? '1' : '0';
  }
  out[PSCU_SCEX_BIT_COUNT] = '\0';
}

static void test_region(void) {
  char bits[PSCU_SCEX_BIT_COUNT + 1U];

  region_to_bits(PSCU_REGION_NTSC_J, bits);
  check(strcmp(bits, "10011010100100111101001010111010010110110100") == 0,
        "region SCEI 44-bit LSB-first");

  region_to_bits(PSCU_REGION_NTSC_UC, bits);
  check(strcmp(bits, "10011010100100111101001010111010010111110100") == 0,
        "region SCEA 44-bit LSB-first");

  region_to_bits(PSCU_REGION_PAL, bits);
  check(strcmp(bits, "10011010100100111101001010111010010101110100") == 0,
        "region SCEE 44-bit LSB-first");
}

static void test_subq_counter(void) {
  uint8_t lead_in[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  check(pscu_subq_update_counter(lead_in, 0U, false, 0xFFU) == 1U, "lead-in A0 increments");
  check(pscu_subq_update_counter(lead_in, 8U, false, 9U) == 9U, "a hit climbs up to the ceiling");
  check(pscu_subq_update_counter(lead_in, 9U, false, 9U) == 9U, "a hit at the ceiling stays there");

  uint8_t unframed[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0x01U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  check(pscu_subq_update_counter(unframed, 5U, false, 0xFFU) == 4U, "bad framing decays");

  uint8_t marker6[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0, 0xA0U, 0, 0, 0, 0x01U, 0, 0, 0, 0, 0 };
  check(pscu_subq_update_counter(marker6, 5U, false, 0xFFU) == 4U,
        "second sync marker nonzero decays");

  uint8_t audio[PSCU_SUBQ_FRAME_BYTES] = { 0x01U, 0, 0x02U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  check(pscu_subq_update_counter(audio, 3U, false, 0xFFU) == 4U,
        "track 01 keeps counter when synced");
  check(pscu_subq_update_counter(audio, 0U, false, 0xFFU) == 0U, "track 01 does not start sync");

  uint8_t spiral[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0, 0x01U, 0x00U, 0, 0, 0, 0, 0, 0, 0, 0 };
  check(pscu_subq_update_counter(spiral, 0U, false, 0xFFU) == 1U, "track 01 spiral start hits");

  uint8_t spiral_miss[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0, 0x01U, 0x05U, 0, 0, 0, 0, 0, 0, 0, 0 };
  check(pscu_subq_update_counter(spiral_miss, 0U, false, 0xFFU) == 0U,
        "track 01 out of window misses");

  uint8_t minute_97[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0, 0x01U, 0x97U, 0, 0, 0, 0, 0, 0, 0, 0 };
  check(pscu_subq_update_counter(minute_97, 0U, false, 0xFFU) == 0U,
        "point 01 at minute 97 is before the window");

  uint8_t minute_98[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0, 0x01U, 0x98U, 0, 0, 0, 0, 0, 0, 0, 0 };
  check(pscu_subq_update_counter(minute_98, 0U, false, 0xFFU) == 1U,
        "point 01 at minute 98 opens the window");

  uint8_t minute_99[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0, 0x01U, 0x99U, 0, 0, 0, 0, 0, 0, 0, 0 };
  check(pscu_subq_update_counter(minute_99, 0U, false, 0xFFU) == 1U, "point 01 at minute 99 hits");

  uint8_t minute_02[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0, 0x01U, 0x02U, 0, 0, 0, 0, 0, 0, 0, 0 };
  check(pscu_subq_update_counter(minute_02, 0U, false, 0xFFU) == 1U,
        "point 01 at minute 02 closes the window");

  uint8_t minute_03[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0, 0x01U, 0x03U, 0, 0, 0, 0, 0, 0, 0, 0 };
  check(pscu_subq_update_counter(minute_03, 0U, false, 0xFFU) == 0U,
        "point 01 at minute 03 is past the window");

  uint8_t mid_point[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0, 0x50U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  check(pscu_subq_update_counter(mid_point, 0U, false, 0xFFU) == 0U,
        "data sector mid-point does not start sync");
  check(pscu_subq_update_counter(mid_point, 4U, false, 0xFFU) == 5U,
        "data sector keeps counter when synced");

  uint8_t other[PSCU_SUBQ_FRAME_BYTES] = { 0x00U, 0, 0x50U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  check(pscu_subq_update_counter(other, 3U, false, 0xFFU) == 2U,
        "non-data non-audio sample decays while synced");

  uint8_t empty[PSCU_SUBQ_FRAME_BYTES] = { 0 };
  check(pscu_subq_update_counter(empty, 0U, false, 0xFFU) == 0U, "decay floors at zero");

  check(pscu_subq_update_counter(lead_in, 0xFFU, false, 0xFFU) == 0xFFU, "counter clamps at max");
}

// The SCPH-5903 Video-CD filter, read from PsNee V9.0's SCPH_5903 variant: only
// the data-sector TOC markers A0..A2 arm, a marker whose frame[3] is 0x02 (the
// Video CD lead-in pattern) does not, and the point-01 spiral window is gone.
// Each frame below sits one step either side of a bound, so a mutant that widens
// or narrows the rule fails here.
static void test_subq_vcd_filter(void) {
  uint8_t game_toc[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0, 0xA0U, 0x00U, 0, 0, 0, 0, 0, 0, 0, 0 };
  check(pscu_subq_update_counter(game_toc, 0U, true, 0xFFU) == 1U, "vcd filter: game TOC A0 arms");

  uint8_t vcd_toc[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0, 0xA0U, 0x02U, 0, 0, 0, 0, 0, 0, 0, 0 };
  check(pscu_subq_update_counter(vcd_toc, 0U, true, 0xFFU) == 0U,
        "vcd filter: video CD lead-in does not arm");
  check(pscu_subq_update_counter(vcd_toc, 0U, false, 0xFFU) == 1U,
        "ordinary filter arms on the same frame");

  uint8_t minute_01[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0, 0xA0U, 0x01U, 0, 0, 0, 0, 0, 0, 0, 0 };
  check(pscu_subq_update_counter(minute_01, 0U, true, 0xFFU) == 1U,
        "vcd filter: only 0x02 is excluded");

  uint8_t last_marker[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0, 0xA2U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  check(pscu_subq_update_counter(last_marker, 0U, true, 0xFFU) == 1U, "vcd filter: marker A2 arms");

  uint8_t past_markers[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0, 0xA3U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  check(pscu_subq_update_counter(past_markers, 0U, true, 0xFFU) == 0U,
        "vcd filter: POINT A3 does not arm");

  uint8_t below_markers[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0, 0x9FU, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  check(pscu_subq_update_counter(below_markers, 0U, true, 0xFFU) == 0U,
        "vcd filter: POINT 9F does not arm");

  uint8_t audio_marker[PSCU_SUBQ_FRAME_BYTES] = { 0x01U, 0, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  check(pscu_subq_update_counter(audio_marker, 0U, true, 0xFFU) == 0U,
        "vcd filter: audio TOC frame does not arm");

  uint8_t spiral[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0, 0x01U, 0x98U, 0, 0, 0, 0, 0, 0, 0, 0 };
  check(pscu_subq_update_counter(spiral, 0U, true, 0xFFU) == 0U,
        "vcd filter: no point-01 spiral window");

  uint8_t mid_point[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0, 0x50U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  check(pscu_subq_update_counter(mid_point, 4U, true, 0xFFU) == 5U,
        "vcd filter: tracking keeps the counter");

  uint8_t unframed[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0x01U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  check(pscu_subq_update_counter(unframed, 5U, true, 0xFFU) == 4U,
        "vcd filter: bad framing decays");
}

static pscu_board_detect_t feed(const uint8_t *samples, uint16_t count, uint8_t needed) {
  pscu_board_detect_t state = pscu_board_detect_init(needed);
  for (uint16_t i = 0U; i < count; i++) {
    state = pscu_board_detect_step(state, samples[i]);
  }
  return state;
}

static void test_board_mode(void) {
  uint8_t high[8] = { 1U, 1U, 1U, 1U, 1U, 1U, 1U, 1U };
  check(pscu_board_detect_mode(feed(high, 8U, 3U)) == PSCU_BOARD_MODE_GATE,
        "static high is legacy gate");

  uint8_t osc[6] = { 1U, 0U, 1U, 0U, 1U, 0U };
  check(pscu_board_detect_mode(feed(osc, 6U, 3U)) == PSCU_BOARD_MODE_WFCK,
        "oscillating is wfck mode");

  uint8_t low_run[3] = { 0U, 0U, 0U };
  check(pscu_board_detect_mode(feed(low_run, 3U, 2U)) == PSCU_BOARD_MODE_GATE,
        "one low pulse is not enough");

  uint8_t single_low[1] = { 0U };
  check(pscu_board_detect_mode(feed(single_low, 1U, 1U)) == PSCU_BOARD_MODE_WFCK,
        "first low sample counts as one pulse");

  check(pscu_board_detect_mode(feed(high, 8U, 0U)) == PSCU_BOARD_MODE_WFCK,
        "zero threshold is wfck");
}

static void test_board_saturates(void) {
  pscu_board_detect_t state = pscu_board_detect_init(25U);
  for (uint16_t i = 0U; i < 600U; i++) {
    state = pscu_board_detect_step(state, (uint8_t)(i & 1U));
  }

  check(state.pulses == 0xFFU, "pulse count saturates at max");
  check(pscu_board_detect_mode(state) == PSCU_BOARD_MODE_WFCK, "saturated count is wfck mode");
}

// Feed count falling edges, each after gap high samples, onto state.
static pscu_board_detect_t edges(pscu_board_detect_t state, uint8_t count, uint16_t gap) {
  pscu_board_detect_t next = state;
  for (uint8_t e = 0U; e < count; e++) {
    for (uint16_t h = 0U; h < gap; h++) {
      next = pscu_board_detect_step(next, 1U);
    }
    next = pscu_board_detect_step(next, 0U);
  }
  return next;
}

// Only edges that follow each other within the gap bound form a run: a clock
// passes on exactly the needed run and not one edge sooner, the same number of
// edges spread out like noise does not, and a run that stops still counts once
// it was long enough.
static void test_board_runs(void) {
  pscu_board_detect_t clock = edges(pscu_board_detect_init(25U), 25U, 30U);
  check(pscu_board_detect_mode(clock) == PSCU_BOARD_MODE_WFCK, "25 close edges are a carrier");
  pscu_board_detect_t short_run = edges(pscu_board_detect_init(25U), 24U, 30U);
  check(pscu_board_detect_mode(short_run) == PSCU_BOARD_MODE_GATE, "24 close edges are not");
  pscu_board_detect_t at_bound = edges(pscu_board_detect_init(25U), 25U, PSCU_BOARD_EDGE_GAP_MAX);
  check(at_bound.pulses == 25U, "an edge exactly at the gap bound still joins the run");
  pscu_board_detect_t spread =
      edges(pscu_board_detect_init(25U), 25U, (uint16_t)(PSCU_BOARD_EDGE_GAP_MAX + 1U));
  check(spread.pulses == 1U, "an edge one sample past the bound starts a new run");
  check(pscu_board_detect_mode(spread) == PSCU_BOARD_MODE_GATE, "spread-out edges are a gate");
  pscu_board_detect_t paused = edges(clock, 3U, 300U);
  check((paused.pulses == 1U) && (pscu_board_detect_mode(paused) == PSCU_BOARD_MODE_WFCK),
        "a carrier that ran long enough and then paused still counts");
  pscu_board_detect_t idle = edges(pscu_board_detect_init(25U), 0U, 0U);
  for (uint16_t i = 0U; i < 400U; i++) {
    idle = pscu_board_detect_step(idle, 1U);
  }
  check(idle.since == 0xFFU, "the gap count saturates on a static line");
}

static void test_inject(void) {
  check(pscu_should_inject(10U, 10U), "window open at trigger");
  check(!pscu_should_inject(9U, 10U), "window closed below trigger");
  check(pscu_should_inject(11U, 10U), "window open above trigger");
}

static void test_stealth(void) {
  pscu_stealth_t start = pscu_stealth_init();
  check((start.sent == 0U) && !start.accepted, "stealth starts disarmed and unlatched");

  pscu_stealth_step_t first = pscu_stealth_step(start, PSCU_WINDOW_OPEN, false, false, 2U, 0U);
  check(first.fire && (first.state.sent == 1U), "emits the first string in window");

  pscu_stealth_step_t second =
      pscu_stealth_step(first.state, PSCU_WINDOW_OPEN, false, false, 2U, 0U);
  check(second.fire && (second.state.sent == 2U), "emits the second string");

  pscu_stealth_step_t capped =
      pscu_stealth_step(second.state, PSCU_WINDOW_OPEN, false, false, 2U, 0U);
  check(!capped.fire && (capped.state.sent == 2U), "falls silent at the cap");

  pscu_stealth_step_t rearmed =
      pscu_stealth_step(capped.state, PSCU_WINDOW_CLOSED, false, false, 2U, 0U);
  check(!rearmed.fire && (rearmed.state.sent == 0U), "silent and re-armed out of window");

  pscu_stealth_step_t again =
      pscu_stealth_step(rearmed.state, PSCU_WINDOW_OPEN, false, false, 2U, 0U);
  check(again.fire, "re-fires on the next window while the console has not accepted");
}

// Strings are spaced: after one, gap frames in the window pass with no string,
// and the next frame sends again. Leaving the window clears the wait, so the next
// disc's first string is never held back by the last disc's gap.
static void test_stealth_gap(void) {
  pscu_stealth_step_t first =
      pscu_stealth_step(pscu_stealth_init(), PSCU_WINDOW_OPEN, false, false, 16U, 2U);
  check(first.fire && (first.state.wait == 2U), "a string starts the gap");
  pscu_stealth_step_t held1 =
      pscu_stealth_step(first.state, PSCU_WINDOW_OPEN, false, false, 16U, 2U);
  check(!held1.fire && (held1.state.wait == 1U), "the first gap frame sends nothing");
  pscu_stealth_step_t held2 =
      pscu_stealth_step(held1.state, PSCU_WINDOW_OPEN, false, false, 16U, 2U);
  check(!held2.fire && (held2.state.wait == 0U), "the last gap frame sends nothing");
  pscu_stealth_step_t next =
      pscu_stealth_step(held2.state, PSCU_WINDOW_OPEN, false, false, 16U, 2U);
  check(next.fire && (next.state.sent == 2U), "the frame after the gap sends the next string");

  pscu_stealth_step_t left =
      pscu_stealth_step(first.state, PSCU_WINDOW_CLOSED, false, false, 16U, 2U);
  check((left.state.wait == 0U) && (left.state.sent == 0U), "leaving the window clears the gap");
  pscu_stealth_step_t back = pscu_stealth_step(left.state, PSCU_WINDOW_OPEN, false, false, 16U, 2U);
  check(back.fire, "a new window sends at once");
}

// A held window is the supply guard keeping strings back inside the window: the
// burst must keep its count and its gap, so the cap is never restarted on the
// same disc, and must carry on from where it stood once the window opens again.
static void test_stealth_held(void) {
  pscu_stealth_step_t first =
      pscu_stealth_step(pscu_stealth_init(), PSCU_WINDOW_OPEN, false, false, 2U, 1U);
  pscu_stealth_step_t held = pscu_stealth_step(first.state, PSCU_WINDOW_HELD, false, false, 2U, 1U);
  check(!held.fire && (held.state.sent == 1U) && (held.state.wait == 1U),
        "a held window keeps the count and the gap");
  pscu_stealth_step_t gap = pscu_stealth_step(held.state, PSCU_WINDOW_OPEN, false, false, 2U, 1U);
  check(!gap.fire && (gap.state.wait == 0U), "the gap resumes once the window opens");
  pscu_stealth_step_t second = pscu_stealth_step(gap.state, PSCU_WINDOW_OPEN, false, false, 2U, 1U);
  check(second.fire && (second.state.sent == 2U), "the burst continues where it stood");
  pscu_stealth_step_t at_cap =
      pscu_stealth_step(second.state, PSCU_WINDOW_HELD, false, false, 2U, 0U);
  pscu_stealth_step_t after =
      pscu_stealth_step(at_cap.state, PSCU_WINDOW_OPEN, false, false, 2U, 0U);
  check(!after.fire && (after.state.sent == 2U), "a hold never restarts the cap");
  pscu_stealth_step_t idle =
      pscu_stealth_step(pscu_stealth_init(), PSCU_WINDOW_HELD, false, false, 2U, 0U);
  check(!idle.fire && (idle.state.sent == 0U), "a held window sends no first string");
  pscu_stealth_step_t accepted =
      pscu_stealth_step(first.state, PSCU_WINDOW_HELD, true, false, 2U, 0U);
  check(accepted.state.accepted, "a program-area frame latches during a hold");
  pscu_stealth_step_t gone = pscu_stealth_step(first.state, PSCU_WINDOW_HELD, false, true, 2U, 0U);
  check(gone.state.sent == 0U, "a disc leaving during a hold ends the burst");
}

static void test_stealth_disc_gone(void) {
  pscu_stealth_step_t armed =
      pscu_stealth_step(pscu_stealth_init(), PSCU_WINDOW_OPEN, false, false, 16U, 0U);
  pscu_stealth_step_t accept =
      pscu_stealth_step(armed.state, PSCU_WINDOW_OPEN, true, false, 16U, 0U);
  check(!accept.fire && accept.state.accepted, "a program-area frame stops the burst at once");

  pscu_stealth_step_t reread =
      pscu_stealth_step(accept.state, PSCU_WINDOW_OPEN, false, false, 16U, 0U);
  check(!reread.fire && reread.state.accepted,
        "a later lead-in read of the same disc stays silent");

  // The program area drains the SUBQ counter and closes the window, which ends
  // the acceptance: the anti-mod v2 check re-reads the lead-in with ReadTOC,
  // clearing the drive's licensed status, and a copy then needs a fresh string
  // (tonyhax docs/ap_v2.c). The next opening of the window is armed again.
  pscu_stealth_step_t closed_out =
      pscu_stealth_step(reread.state, PSCU_WINDOW_CLOSED, false, false, 16U, 0U);
  check(!closed_out.state.accepted, "leaving the window ends the acceptance");

  pscu_stealth_step_t toc =
      pscu_stealth_step(closed_out.state, PSCU_WINDOW_OPEN, false, false, 16U, 0U);
  check(toc.fire && (toc.state.sent == 1U), "a lead-in re-read after play is injected again");

  pscu_stealth_step_t opened =
      pscu_stealth_step(closed_out.state, PSCU_WINDOW_OPEN, false, true, 16U, 0U);
  check(!opened.fire && !opened.state.accepted && (opened.state.sent == 0U),
        "a gone disc emits nothing and is forgotten");

  pscu_stealth_step_t open_program =
      pscu_stealth_step(opened.state, PSCU_WINDOW_OPEN, true, true, 16U, 0U);
  check(!open_program.state.accepted, "a frame read while the disc counts as gone cannot latch");

  pscu_stealth_step_t disc2 =
      pscu_stealth_step(open_program.state, PSCU_WINDOW_OPEN, false, false, 16U, 0U);
  check(disc2.fire && (disc2.state.sent == 1U), "the next disc is injected once it arrives");

  pscu_stealth_step_t mid = pscu_stealth_step(disc2.state, PSCU_WINDOW_OPEN, false, true, 16U, 0U);
  check(!mid.fire && (mid.state.sent == 0U), "a disc leaving mid-burst ends the burst");
}

static void test_program_area(void) {
  uint8_t data_track2[PSCU_SUBQ_FRAME_BYTES] = {
    0x41U, 0x02U, 0x01U, 0x10U, 0, 0, 0, 0, 0, 0, 0, 0
  };
  check(pscu_subq_is_program_area(data_track2), "data track 2 is program area");

  uint8_t audio_track2[PSCU_SUBQ_FRAME_BYTES] = {
    0x01U, 0x02U, 0x01U, 0x10U, 0, 0, 0, 0, 0, 0, 0, 0
  };
  check(pscu_subq_is_program_area(audio_track2), "audio track 2 is program area");

  uint8_t track1[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0x01U, 0x01U, 0x00U, 0, 0, 0, 0, 0, 0, 0, 0 };
  check(pscu_subq_is_program_area(track1), "track 1, the lowest track number, is program area");

  uint8_t track99[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0x99U, 0x01U, 0x10U, 0, 0, 0, 0, 0, 0, 0, 0 };
  check(pscu_subq_is_program_area(track99), "track 99, the highest track number, is program area");

  uint8_t track19[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0x19U, 0x01U, 0x10U, 0, 0, 0, 0, 0, 0, 0, 0 };
  check(pscu_subq_is_program_area(track19), "a BCD low digit of 9 is a valid track");

  uint8_t toc[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  check(!pscu_subq_is_program_area(toc), "lead-in TOC marker is not program area");

  uint8_t toc_entry[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0x05U, 0x10U, 0, 0, 0, 0, 0, 0, 0, 0 };
  check(!pscu_subq_is_program_area(toc_entry),
        "lead-in TOC entry naming a track is not program area");

  uint8_t lead_out[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0xAAU, 0x01U, 0x10U, 0, 0, 0, 0, 0, 0, 0, 0 };
  check(!pscu_subq_is_program_area(lead_out), "lead-out is not program area");

  uint8_t bad_bcd[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0x1AU, 0x01U, 0x10U, 0, 0, 0, 0, 0, 0, 0, 0 };
  check(!pscu_subq_is_program_area(bad_bcd), "a non-BCD track number is not program area");

  uint8_t failed[PSCU_SUBQ_FRAME_BYTES] = { 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU,
                                            0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU };
  check(!pscu_subq_is_program_area(failed), "a failed capture is not program area");

  uint8_t noncontent[PSCU_SUBQ_FRAME_BYTES] = {
    0x00U, 0x02U, 0x01U, 0x10U, 0, 0, 0, 0, 0, 0, 0, 0
  };
  check(!pscu_subq_is_program_area(noncontent), "non-content control is not program area");

  uint8_t misframed[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0x02U, 0x01U, 0x10U, 0, 0,
                                               0x01U, 0,     0,     0,     0, 0 };
  check(!pscu_subq_is_program_area(misframed), "a nonzero ZERO byte is not program area");
}

static void test_failed_capture(void) {
  uint8_t failed[PSCU_SUBQ_FRAME_BYTES] = { 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU,
                                            0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU };
  check(pscu_subq_update_counter(failed, 5U, false, 0xFFU) == 4U,
        "a failed capture decays the counter");
}

static void test_confirm(void) {
  pscu_confirm_t start = pscu_confirm_init();
  check((start.waited == 0U) && !start.program_seen, "confirm starts fresh");

  pscu_confirm_step_t seen = pscu_confirm_step(start, false, true, 3U);
  check(seen.resolved && seen.confirmed, "program area resolves as confirmed");

  pscu_confirm_step_t wait1 = pscu_confirm_step(start, true, false, 3U);
  check(!wait1.resolved && (wait1.state.waited == 1U), "idle without program advances the wait");

  pscu_confirm_step_t busy = pscu_confirm_step(start, false, false, 3U);
  check(!busy.resolved && (busy.state.waited == 0U), "a non-idle frame does not advance the wait");
  pscu_confirm_t midway = { 2U, false };
  check(pscu_confirm_step(midway, false, false, 3U).state.waited == 0U,
        "a string restarts the wait, so it runs only after the last string");

  pscu_confirm_t near = { 2U, false };
  pscu_confirm_step_t timed = pscu_confirm_step(near, true, false, 3U);
  check(timed.resolved && !timed.confirmed, "idle wait times out as unconfirmed");

  pscu_confirm_t maxed = { 0xFFU, false };
  pscu_confirm_step_t clamped = pscu_confirm_step(maxed, true, false, 3U);
  check((clamped.state.waited == 0xFFU) && clamped.resolved, "wait clamps at the byte boundary");
}

int main(void) {
  test_region();
  test_subq_counter();
  test_subq_vcd_filter();
  test_board_mode();
  test_board_saturates();
  test_board_runs();
  test_inject();
  test_stealth();
  test_stealth_gap();
  test_stealth_disc_gone();
  test_stealth_held();
  test_program_area();
  test_failed_capture();
  test_confirm();
  calib_tests(check);
  trim_tests(check);
  disc_tests(check);
  led_tests(check);
  loop_tests(check);

  (void)printf("%d checks, %d failures\n", g_checks, g_failures);
  return (g_failures == 0) ? 0 : 1;
}
