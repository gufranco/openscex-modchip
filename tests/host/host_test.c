// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pscu/board_mode.h"
#include "pscu/diag.h"
#include "pscu/inject.h"
#include "pscu/region.h"
#include "pscu/subq.h"

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
  check(pscu_subq_update_counter(lead_in, 0U, false) == 1U, "lead-in A0 increments");

  uint8_t unframed[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0x01U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  check(pscu_subq_update_counter(unframed, 5U, false) == 4U, "bad framing decays");

  uint8_t marker6[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0, 0xA0U, 0, 0, 0, 0x01U, 0, 0, 0, 0, 0 };
  check(pscu_subq_update_counter(marker6, 5U, false) == 4U, "second sync marker nonzero decays");

  uint8_t audio[PSCU_SUBQ_FRAME_BYTES] = { 0x01U, 0, 0x02U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  check(pscu_subq_update_counter(audio, 3U, false) == 4U, "track 01 keeps counter when synced");
  check(pscu_subq_update_counter(audio, 0U, false) == 0U, "track 01 does not start sync");

  uint8_t spiral[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0, 0x01U, 0x00U, 0, 0, 0, 0, 0, 0, 0, 0 };
  check(pscu_subq_update_counter(spiral, 0U, false) == 1U, "track 01 spiral start hits");

  uint8_t spiral_miss[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0, 0x01U, 0x05U, 0, 0, 0, 0, 0, 0, 0, 0 };
  check(pscu_subq_update_counter(spiral_miss, 0U, false) == 0U, "track 01 out of window misses");

  uint8_t minute_97[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0, 0x01U, 0x97U, 0, 0, 0, 0, 0, 0, 0, 0 };
  check(pscu_subq_update_counter(minute_97, 0U, false) == 0U,
        "point 01 at minute 97 is before the window");

  uint8_t minute_98[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0, 0x01U, 0x98U, 0, 0, 0, 0, 0, 0, 0, 0 };
  check(pscu_subq_update_counter(minute_98, 0U, false) == 1U,
        "point 01 at minute 98 opens the window");

  uint8_t minute_99[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0, 0x01U, 0x99U, 0, 0, 0, 0, 0, 0, 0, 0 };
  check(pscu_subq_update_counter(minute_99, 0U, false) == 1U, "point 01 at minute 99 hits");

  uint8_t minute_02[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0, 0x01U, 0x02U, 0, 0, 0, 0, 0, 0, 0, 0 };
  check(pscu_subq_update_counter(minute_02, 0U, false) == 1U,
        "point 01 at minute 02 closes the window");

  uint8_t minute_03[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0, 0x01U, 0x03U, 0, 0, 0, 0, 0, 0, 0, 0 };
  check(pscu_subq_update_counter(minute_03, 0U, false) == 0U,
        "point 01 at minute 03 is past the window");

  uint8_t mid_point[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0, 0x50U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  check(pscu_subq_update_counter(mid_point, 0U, false) == 0U,
        "data sector mid-point does not start sync");
  check(pscu_subq_update_counter(mid_point, 4U, false) == 5U,
        "data sector keeps counter when synced");

  uint8_t other[PSCU_SUBQ_FRAME_BYTES] = { 0x00U, 0, 0x50U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  check(pscu_subq_update_counter(other, 3U, false) == 2U,
        "non-data non-audio sample decays while synced");

  uint8_t empty[PSCU_SUBQ_FRAME_BYTES] = { 0 };
  check(pscu_subq_update_counter(empty, 0U, false) == 0U, "decay floors at zero");

  check(pscu_subq_update_counter(lead_in, 0xFFU, false) == 0xFFU, "counter clamps at max");
}

// The SCPH-5903 Video-CD filter, read from PsNee V9.0's SCPH_5903 variant: only
// the data-sector TOC markers A0..A2 arm, a marker whose frame[3] is 0x02 (the
// Video CD lead-in pattern) does not, and the point-01 spiral window is gone.
// Each frame below sits one step either side of a bound, so a mutant that widens
// or narrows the rule fails here.
static void test_subq_vcd_filter(void) {
  uint8_t game_toc[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0, 0xA0U, 0x00U, 0, 0, 0, 0, 0, 0, 0, 0 };
  check(pscu_subq_update_counter(game_toc, 0U, true) == 1U, "vcd filter: game TOC A0 arms");

  uint8_t vcd_toc[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0, 0xA0U, 0x02U, 0, 0, 0, 0, 0, 0, 0, 0 };
  check(pscu_subq_update_counter(vcd_toc, 0U, true) == 0U,
        "vcd filter: video CD lead-in does not arm");
  check(pscu_subq_update_counter(vcd_toc, 0U, false) == 1U,
        "ordinary filter arms on the same frame");

  uint8_t minute_01[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0, 0xA0U, 0x01U, 0, 0, 0, 0, 0, 0, 0, 0 };
  check(pscu_subq_update_counter(minute_01, 0U, true) == 1U, "vcd filter: only 0x02 is excluded");

  uint8_t last_marker[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0, 0xA2U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  check(pscu_subq_update_counter(last_marker, 0U, true) == 1U, "vcd filter: marker A2 arms");

  uint8_t past_markers[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0, 0xA3U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  check(pscu_subq_update_counter(past_markers, 0U, true) == 0U,
        "vcd filter: POINT A3 does not arm");

  uint8_t below_markers[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0, 0x9FU, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  check(pscu_subq_update_counter(below_markers, 0U, true) == 0U,
        "vcd filter: POINT 9F does not arm");

  uint8_t audio_marker[PSCU_SUBQ_FRAME_BYTES] = { 0x01U, 0, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  check(pscu_subq_update_counter(audio_marker, 0U, true) == 0U,
        "vcd filter: audio TOC frame does not arm");

  uint8_t spiral[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0, 0x01U, 0x98U, 0, 0, 0, 0, 0, 0, 0, 0 };
  check(pscu_subq_update_counter(spiral, 0U, true) == 0U, "vcd filter: no point-01 spiral window");

  uint8_t mid_point[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0, 0x50U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  check(pscu_subq_update_counter(mid_point, 4U, true) == 5U,
        "vcd filter: tracking keeps the counter");

  uint8_t unframed[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0x01U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  check(pscu_subq_update_counter(unframed, 5U, true) == 4U, "vcd filter: bad framing decays");
}

static pscu_board_detect_t feed(const uint8_t *samples, uint16_t count) {
  pscu_board_detect_t state = pscu_board_detect_init();
  for (uint16_t i = 0U; i < count; i++) {
    state = pscu_board_detect_step(state, samples[i]);
  }
  return state;
}

static void test_board_mode(void) {
  uint8_t high[8] = { 1U, 1U, 1U, 1U, 1U, 1U, 1U, 1U };
  check(pscu_board_detect_mode(feed(high, 8U), 3U) == PSCU_BOARD_MODE_GATE,
        "static high is legacy gate");

  uint8_t osc[6] = { 1U, 0U, 1U, 0U, 1U, 0U };
  check(pscu_board_detect_mode(feed(osc, 6U), 3U) == PSCU_BOARD_MODE_WFCK,
        "oscillating is wfck mode");

  uint8_t low_run[3] = { 0U, 0U, 0U };
  check(pscu_board_detect_mode(feed(low_run, 3U), 2U) == PSCU_BOARD_MODE_GATE,
        "one low pulse is not enough");

  uint8_t single_low[1] = { 0U };
  check(pscu_board_detect_mode(feed(single_low, 1U), 1U) == PSCU_BOARD_MODE_WFCK,
        "first low sample counts as one pulse");

  check(pscu_board_detect_mode(feed(high, 8U), 0U) == PSCU_BOARD_MODE_WFCK,
        "zero threshold is wfck");
}

static void test_board_saturates(void) {
  pscu_board_detect_t state = pscu_board_detect_init();
  for (uint16_t i = 0U; i < 600U; i++) {
    state = pscu_board_detect_step(state, (uint8_t)(i & 1U));
  }

  check(state.pulses == 0xFFU, "pulse count saturates at max");
  check(pscu_board_detect_mode(state, 25U) == PSCU_BOARD_MODE_WFCK, "saturated count is wfck mode");
}

static void test_inject(void) {
  check(pscu_should_inject(10U, 10U), "window open at trigger");
  check(!pscu_should_inject(9U, 10U), "window closed below trigger");
  check(pscu_should_inject(11U, 10U), "window open above trigger");
}

static void test_stealth(void) {
  pscu_stealth_t start = pscu_stealth_init();
  check((start.sent == 0U) && !start.accepted, "stealth starts disarmed and unlatched");

  pscu_stealth_step_t first = pscu_stealth_step(start, true, PSCU_FRAME_LEAD_IN, 2U);
  check(first.fire && (first.state.sent == 1U), "emits the first string in window");

  pscu_stealth_step_t second = pscu_stealth_step(first.state, true, PSCU_FRAME_LEAD_IN, 2U);
  check(second.fire && (second.state.sent == 2U), "emits the second string");

  pscu_stealth_step_t capped = pscu_stealth_step(second.state, true, PSCU_FRAME_LEAD_IN, 2U);
  check(!capped.fire && (capped.state.sent == 2U), "falls silent at the cap");

  pscu_stealth_step_t rearmed = pscu_stealth_step(capped.state, false, PSCU_FRAME_LOST, 2U);
  check(!rearmed.fire && (rearmed.state.sent == 0U), "silent and re-armed out of window");

  pscu_stealth_step_t again = pscu_stealth_step(rearmed.state, true, PSCU_FRAME_LEAD_IN, 2U);
  check(again.fire, "re-fires on the next window while the console has not accepted");
}

static void test_frame_kind(void) {
  uint8_t lead_in[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  check(pscu_subq_frame_kind(lead_in) == PSCU_FRAME_LEAD_IN, "data TOC frame is a lead-in read");

  uint8_t audio_lead_in[PSCU_SUBQ_FRAME_BYTES] = { 0x01U, 0, 0x05U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  check(pscu_subq_frame_kind(audio_lead_in) == PSCU_FRAME_LEAD_IN,
        "audio TOC frame is a lead-in read");

  uint8_t program[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0x01U, 0x01U, 0, 0x02U, 0, 0, 0, 0, 0, 0, 0 };
  check(pscu_subq_frame_kind(program) == PSCU_FRAME_PROGRAM, "track 01 frame is the program area");

  uint8_t zeros[PSCU_SUBQ_FRAME_BYTES] = { 0 };
  check(pscu_subq_frame_kind(zeros) == PSCU_FRAME_LOST, "an empty frame is lost subcode");

  uint8_t zero_byte_set[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0, 0xA0U, 0, 0, 0, 0x01U, 0, 0, 0, 0, 0 };
  check(pscu_subq_frame_kind(zero_byte_set) == PSCU_FRAME_LOST,
        "a lead-in frame with a nonzero ZERO byte is lost");

  uint8_t failed[PSCU_SUBQ_FRAME_BYTES] = { 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU,
                                            0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU };
  check(pscu_subq_frame_kind(failed) == PSCU_FRAME_LOST, "a failed capture frame is lost");
}

static pscu_stealth_t stealth_accepted(void) {
  pscu_stealth_step_t armed = pscu_stealth_step(pscu_stealth_init(), true, PSCU_FRAME_LEAD_IN, 16U);
  return pscu_stealth_step(armed.state, true, PSCU_FRAME_PROGRAM, 16U).state;
}

static pscu_stealth_t stealth_feed(pscu_stealth_t state, pscu_frame_kind_t kind, uint16_t count) {
  pscu_stealth_t next = state;
  for (uint16_t i = 0U; i < count; i++) {
    next = pscu_stealth_step(next, false, kind, 16U).state;
  }
  return next;
}

static void test_stealth_acceptance(void) {
  pscu_stealth_step_t armed = pscu_stealth_step(pscu_stealth_init(), true, PSCU_FRAME_LEAD_IN, 16U);
  pscu_stealth_step_t accept = pscu_stealth_step(armed.state, true, PSCU_FRAME_PROGRAM, 16U);
  check(!accept.fire && accept.state.accepted, "a program-area frame stops the burst at once");

  pscu_stealth_step_t reread = pscu_stealth_step(accept.state, true, PSCU_FRAME_LEAD_IN, 16U);
  check(!reread.fire && reread.state.accepted, "a later lead-in read stays silent");

  pscu_stealth_t seek = stealth_feed(stealth_accepted(), PSCU_FRAME_LOST, 149U);
  check(seek.accepted && (seek.lost == 149U), "a lost run below the threshold keeps the latch");

  pscu_stealth_t stopped = stealth_feed(stealth_accepted(), PSCU_FRAME_LOST, 150U);
  check(!stopped.accepted && (stopped.lost == 0U), "150 lost frames release the latch");

  pscu_stealth_t clock_8 = stealth_feed(stealth_accepted(), PSCU_FRAME_SILENT, 8U);
  check(clock_8.accepted && (clock_8.lost == 136U), "eight silent captures keep the latch");

  pscu_stealth_t clock_9 = stealth_feed(stealth_accepted(), PSCU_FRAME_SILENT, 9U);
  check(!clock_9.accepted, "nine silent captures release the latch");

  pscu_stealth_t interrupted = stealth_feed(stealth_accepted(), PSCU_FRAME_LOST, 149U);
  interrupted = stealth_feed(interrupted, PSCU_FRAME_LEAD_IN, 1U);
  interrupted = stealth_feed(interrupted, PSCU_FRAME_LOST, 1U);
  check(interrupted.accepted && (interrupted.lost == 1U), "a readable frame restarts the lost run");

  pscu_stealth_t toc = stealth_feed(stealth_accepted(), PSCU_FRAME_LEAD_IN, 224U);
  check(toc.accepted && (toc.lead_in == 224U), "a TOC re-read below the threshold stays silent");

  pscu_stealth_t stuck = stealth_feed(stealth_accepted(), PSCU_FRAME_LEAD_IN, 225U);
  check(!stuck.accepted && (stuck.lead_in == 0U), "225 lead-in frames without play release it");

  pscu_stealth_t back = stealth_feed(stealth_accepted(), PSCU_FRAME_LEAD_IN, 224U);
  back = stealth_feed(back, PSCU_FRAME_PROGRAM, 1U);
  back = stealth_feed(back, PSCU_FRAME_LEAD_IN, 1U);
  check(back.accepted && (back.lead_in == 1U), "returning to play restarts the lead-in run");

  pscu_stealth_step_t disc2 = pscu_stealth_step(stuck, true, PSCU_FRAME_LEAD_IN, 16U);
  check(disc2.fire, "after release the next disc's check is injected");
}

static void test_diag(void) {
  uint8_t erased[PSCU_DIAG_EEPROM_BYTES] = { 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU };
  pscu_diag_record_t fresh = pscu_diag_decode(erased);
  check((fresh.sessions == 0U) && (fresh.board == PSCU_BOARD_MODE_GATE) && (fresh.confirmed == 0U),
        "erased eeprom decodes as no prior record");

  uint8_t stored[PSCU_DIAG_EEPROM_BYTES] = { PSCU_DIAG_MAGIC, 1U, 7U, 3U, 1U };
  pscu_diag_record_t prior = pscu_diag_decode(stored);
  check((prior.board == PSCU_BOARD_MODE_WFCK) && (prior.sessions == 7U) && (prior.injects == 3U) &&
            (prior.confirmed == 1U),
        "valid record decodes all fields");

  uint8_t gate_stored[PSCU_DIAG_EEPROM_BYTES] = { PSCU_DIAG_MAGIC, 0U, 2U, 9U, 0U };
  check(pscu_diag_decode(gate_stored).board == PSCU_BOARD_MODE_GATE,
        "zero board byte decodes as gate");

  pscu_diag_record_t built = pscu_diag_build(PSCU_BOARD_MODE_WFCK, 7U, 4U, 1U);
  check((built.sessions == 8U) && (built.injects == 4U) && (built.confirmed == 1U),
        "build advances the session count");

  check(pscu_diag_build(PSCU_BOARD_MODE_GATE, 255U, 0U, 0U).sessions == 0U,
        "session count wraps at the byte boundary");

  uint8_t gate_raw[PSCU_DIAG_EEPROM_BYTES];
  pscu_diag_encode(pscu_diag_build(PSCU_BOARD_MODE_GATE, 0U, 1U, 0U), gate_raw);
  pscu_diag_record_t gate_back = pscu_diag_decode(gate_raw);
  check((gate_back.board == PSCU_BOARD_MODE_GATE) && (gate_back.sessions == 1U) &&
            (gate_back.confirmed == 0U),
        "encode then decode round-trips a gate record");

  uint8_t wfck_raw[PSCU_DIAG_EEPROM_BYTES];
  pscu_diag_encode(pscu_diag_build(PSCU_BOARD_MODE_WFCK, 10U, 16U, 1U), wfck_raw);
  pscu_diag_record_t wfck_back = pscu_diag_decode(wfck_raw);
  check((wfck_back.board == PSCU_BOARD_MODE_WFCK) && (wfck_back.injects == 16U) &&
            (wfck_back.confirmed == 1U),
        "encode then decode round-trips a wfck record");
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
  check(pscu_subq_update_counter(failed, 5U, false) == 4U, "a failed capture decays the counter");
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
  test_inject();
  test_stealth();
  test_frame_kind();
  test_stealth_acceptance();
  test_diag();
  test_program_area();
  test_failed_capture();
  test_confirm();

  (void)printf("%d checks, %d failures\n", g_checks, g_failures);
  return (g_failures == 0) ? 0 : 1;
}
