// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "calib_test.h"
#include "pscu/board_mode.h"
#include "pscu/inject.h"
#include "pscu/led.h"
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

  pscu_stealth_step_t first = pscu_stealth_step(start, true, false, false, 2U);
  check(first.fire && (first.state.sent == 1U), "emits the first string in window");

  pscu_stealth_step_t second = pscu_stealth_step(first.state, true, false, false, 2U);
  check(second.fire && (second.state.sent == 2U), "emits the second string");

  pscu_stealth_step_t capped = pscu_stealth_step(second.state, true, false, false, 2U);
  check(!capped.fire && (capped.state.sent == 2U), "falls silent at the cap");

  pscu_stealth_step_t rearmed = pscu_stealth_step(capped.state, false, false, false, 2U);
  check(!rearmed.fire && (rearmed.state.sent == 0U), "silent and re-armed out of window");

  pscu_stealth_step_t again = pscu_stealth_step(rearmed.state, true, false, false, 2U);
  check(again.fire, "re-fires on the next window while the console has not accepted");
}

static void test_stealth_lid(void) {
  pscu_stealth_step_t armed = pscu_stealth_step(pscu_stealth_init(), true, false, false, 16U);
  pscu_stealth_step_t accept = pscu_stealth_step(armed.state, true, true, false, 16U);
  check(!accept.fire && accept.state.accepted, "a program-area frame stops the burst at once");

  pscu_stealth_step_t reread = pscu_stealth_step(accept.state, true, false, false, 16U);
  check(!reread.fire && reread.state.accepted,
        "a later lead-in read with the lid shut stays silent");

  pscu_stealth_step_t closed_out = pscu_stealth_step(reread.state, false, false, false, 16U);
  check(closed_out.state.accepted, "leaving the window does not end the acceptance");

  pscu_stealth_step_t opened = pscu_stealth_step(closed_out.state, true, false, true, 16U);
  check(!opened.fire && !opened.state.accepted && (opened.state.sent == 0U),
        "an open lid emits nothing and forgets the disc");

  pscu_stealth_step_t open_program = pscu_stealth_step(opened.state, true, true, true, 16U);
  check(!open_program.state.accepted, "a frame read while the lid is open cannot latch");

  pscu_stealth_step_t disc2 = pscu_stealth_step(open_program.state, true, false, false, 16U);
  check(disc2.fire && (disc2.state.sent == 1U), "after the lid closes the next disc is injected");

  pscu_stealth_step_t mid = pscu_stealth_step(disc2.state, true, false, true, 16U);
  check(!mid.fire && (mid.state.sent == 0U), "an open lid mid-burst ends the burst");
}

static pscu_led_step_t led_after(pscu_led_t state, uint32_t ms) {
  return pscu_led_step(state, PSCU_LED_EVENT_NONE, 0U, ms);
}

static void test_led_boot(void) {
  pscu_led_t gate = pscu_led_init(1U, 0U);
  check(led_after(gate, 0U).on, "led: board blink lit at start");
  check(!led_after(gate, 310U).on, "led: board blink gap is dark");
  // Boundaries: a flash lasts exactly its on time, and a pass that took no
  // measurable time must not move the pattern, let alone pin it at the end.
  check(!led_after(gate, 300U).on, "led: a blink is dark from its 300th ms");
  pscu_led_step_t still = led_after(gate, 0U);
  check((still.state.phase_ms == 0U) && (still.state.stage == PSCU_LED_BOARD),
        "led: a zero step leaves the phase where it was");
  pscu_led_step_t waiting = led_after(gate, 600U);
  check((waiting.state.stage == PSCU_LED_WAIT) && !waiting.on,
        "led: one gate blink then the heartbeat wait");

  pscu_led_t carrier = pscu_led_init(2U, PSCU_LED_CODE_WATCHDOG);
  check(led_after(carrier, 610U).on, "led: carrier second blink lit");
  pscu_led_step_t replay = led_after(carrier, 1200U);
  check((replay.state.stage == PSCU_LED_REPLAY) && replay.on,
        "led: watchdog code replayed after the board blinks");
  pscu_led_step_t replay_dark = led_after(replay.state, 750U);
  check(!replay_dark.on, "led: replay code flash gap is dark");
  pscu_led_step_t after_replay = led_after(replay.state, 8000U);
  check(after_replay.state.stage == PSCU_LED_WAIT, "led: replay plays once then waits");

  pscu_led_t moved = pscu_led_init(1U, PSCU_LED_CODE_BOARD_CHANGED);
  pscu_led_step_t moved_replay = led_after(moved, 600U);
  check((moved_replay.state.stage == PSCU_LED_REPLAY) && (moved_replay.state.replay == 7U),
        "led: a board change is replayed as code 7");

  pscu_led_step_t early = pscu_led_step(gate, PSCU_LED_EVENT_NONE, PSCU_LED_CODE_LID, 10U);
  check(early.state.stage == PSCU_LED_BOARD, "led: a fault does not cut the board blinks");
}

static void test_led_heartbeat(void) {
  pscu_led_t wait = led_after(pscu_led_init(1U, 0U), 600U).state;
  check(!led_after(wait, 1959U).on, "led: heartbeat dark before the blip");
  check(led_after(wait, 1960U).on, "led: heartbeat blip at the end of the period");
  check(led_after(wait, 1999U).on, "led: heartbeat blip lasts 40 ms");
  check(!led_after(wait, 2000U).on, "led: heartbeat blip ends with the period");
}

static void test_led_results(void) {
  pscu_led_t wait = led_after(pscu_led_init(1U, 0U), 600U).state;
  pscu_led_step_t fired = pscu_led_step(wait, PSCU_LED_EVENT_FIRED, 0U, 0U);
  check((fired.state.stage == PSCU_LED_INJECT) && !fired.on,
        "led: injection leaves the LED to the string flashes");

  pscu_led_step_t accepted = pscu_led_step(fired.state, PSCU_LED_EVENT_ACCEPTED, 0U, 0U);
  check((accepted.state.code == PSCU_LED_CODE_ACCEPTED) && accepted.on,
        "led: accepted shows code 1");
  check(!led_after(accepted.state, 1000U).on, "led: code 1 has one flash then a pause");
  check(led_after(accepted.state, 3000U).on, "led: code 1 repeats after the pause");
  pscu_led_step_t done = led_after(accepted.state, 9000U);
  check((done.state.stage == PSCU_LED_DARK) && !done.on, "led: result shown three times then dark");
  check(led_after(accepted.state, 8999U).state.stage == PSCU_LED_CODE,
        "led: result still showing just before its third repeat ends");

  pscu_led_step_t refused = pscu_led_step(fired.state, PSCU_LED_EVENT_REFUSED, 0U, 0U);
  check(refused.state.code == PSCU_LED_CODE_REFUSED, "led: refused shows code 2");
  check(led_after(refused.state, 1000U).on, "led: code 2 second flash lit");
  check(!led_after(refused.state, 2000U).on, "led: code 2 has only two flashes");
}

static void test_led_faults(void) {
  pscu_led_t wait = led_after(pscu_led_init(1U, 0U), 600U).state;
  pscu_led_step_t lid = pscu_led_step(wait, PSCU_LED_EVENT_NONE, PSCU_LED_CODE_LID, 0U);
  check((lid.state.code == PSCU_LED_CODE_LID) && lid.state.live,
        "led: a fault takes the waiting LED");
  pscu_led_step_t held = pscu_led_step(lid.state, PSCU_LED_EVENT_NONE, PSCU_LED_CODE_LID, 20000U);
  check((held.state.stage == PSCU_LED_CODE) && (held.state.phase_ms == 20000U),
        "led: a live code repeats for as long as the fault holds");
  pscu_led_step_t cleared = pscu_led_step(held.state, PSCU_LED_EVENT_NONE, 0U, 0U);
  check(cleared.state.stage == PSCU_LED_WAIT, "led: a cleared fault returns to the heartbeat");

  pscu_led_t result = pscu_led_step(wait, PSCU_LED_EVENT_ACCEPTED, 0U, 0U).state;
  pscu_led_step_t swap = pscu_led_step(result, PSCU_LED_EVENT_NONE, PSCU_LED_CODE_LID, 0U);
  check(swap.state.code == PSCU_LED_CODE_LID, "led: opening the lid replaces a result code");
  check(pscu_led_step(result, PSCU_LED_EVENT_NONE, 0U, 0U).state.code == PSCU_LED_CODE_ACCEPTED,
        "led: a result code is not cut short without a fault");

  pscu_led_t dark = led_after(result, 9000U).state;
  check(
      pscu_led_step(dark, PSCU_LED_EVENT_NONE, PSCU_LED_CODE_LID, 0U).state.stage == PSCU_LED_CODE,
      "led: a fault wakes the dark LED");
  pscu_led_t inject = pscu_led_step(wait, PSCU_LED_EVENT_FIRED, 0U, 0U).state;
  check(pscu_led_step(inject, PSCU_LED_EVENT_NONE, PSCU_LED_CODE_LID, 0U).state.stage ==
            PSCU_LED_CODE,
        "led: a fault interrupts waiting for a result");

  pscu_led_t full = wait;
  full.phase_ms = 0xFFFFFFF0UL;
  check(led_after(full, 0x100U).state.phase_ms == 0xFFFFFFFFUL, "led: phase saturates");
}

static void test_led_fault_rules(void) {
  check(pscu_led_fault(true, 0U, true, true) == PSCU_LED_CODE_LID, "fault: open lid wins");
  check(pscu_led_fault(false, 4999U, false, false) == 0U, "fault: no frames yet, still waiting");
  check(pscu_led_fault(false, 5000U, false, false) == PSCU_LED_CODE_NO_SQCK,
        "fault: no frame for 5 s means no SQCK");
  check(pscu_led_fault(false, 19999U, true, false) == 0U, "fault: frames, check not due yet");
  check(pscu_led_fault(false, 20000U, true, false) == PSCU_LED_CODE_NO_CHECK,
        "fault: frames but no region check for 20 s");
  check(pscu_led_fault(false, 20000U, true, true) == 0U, "fault: an armed disc is not a fault");
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
  test_stealth_lid();
  test_led_boot();
  test_led_heartbeat();
  test_led_results();
  test_led_faults();
  test_led_fault_rules();
  test_program_area();
  test_failed_capture();
  test_confirm();
  calib_tests(check);

  (void)printf("%d checks, %d failures\n", g_checks, g_failures);
  return (g_failures == 0) ? 0 : 1;
}
