// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "calib_test.h"

#include <stdbool.h>
#include <stdint.h>

#include "pscu/calib.h"

// Host tests for the calibration record and the learning rules, each boundary
// on both sides.

#define MIN_T PSCU_CALIB_TRIGGER_MIN
#define MAX_T PSCU_CALIB_TRIGGER_MAX
#define STEP PSCU_CALIB_TRIGGER_STEP

static pscu_check_fn g_check;

static pscu_calib_t make(uint8_t board, uint8_t trigger, bool frozen) {
  pscu_calib_t calib = { board, trigger, frozen, 0 };
  return calib;
}

// The defaults, written out: no board seen, the earliest trigger, not frozen
// and no trim, which is what a chip with no usable record must run with.
static pscu_calib_t defaults(void) {
  return make(PSCU_CALIB_BOARD_NONE, MIN_T, false);
}

static bool same(pscu_calib_t a, pscu_calib_t b) {
  return (a.board == b.board) && (a.trigger == b.trigger) && (a.frozen == b.frozen) &&
         (a.trim == b.trim);
}

// Encode never validates, so it can build any record; decoding shows what the
// reader accepts.
static bool accepted_record(pscu_calib_t calib) {
  return same(pscu_calib_decode(pscu_calib_encode(calib)), calib);
}

static bool is_default(pscu_calib_t calib) {
  return same(calib, defaults());
}

static void test_record_round_trip(void) {
  pscu_calib_record_t blank = { { 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU } };
  g_check(is_default(pscu_calib_decode(blank)), "calib: an erased chip reads as defaults");
  g_check(accepted_record(make(1U, (uint8_t)(MIN_T + 4U), true)),
          "calib: a learned record reads back unchanged");
  g_check(accepted_record(make(PSCU_CALIB_BOARD_NONE, MIN_T, false)),
          "calib: a fresh record reads back unchanged");

  pscu_calib_record_t torn = pscu_calib_encode(make(1U, MIN_T, false));
  torn.bytes[PSCU_CALIB_AT_TRIGGER] = (uint8_t)(MIN_T + STEP);
  g_check(is_default(pscu_calib_decode(torn)), "calib: a byte changed after the check is caught");

  pscu_calib_record_t stale = pscu_calib_encode(make(1U, MIN_T, false));
  stale.bytes[PSCU_CALIB_AT_CHECK] = (uint8_t)(stale.bytes[PSCU_CALIB_AT_CHECK] ^ 0x01U);
  g_check(is_default(pscu_calib_decode(stale)), "calib: a wrong check byte reads as defaults");

  // A record as any build writes it, from the documented layout rather than the
  // encoder: magic 0xC8, board 1, start 10, not frozen, trim 0, then the check,
  // 0x5A ^ 0xC8 ^ 0x01 ^ 0x0A ^ 0x00 ^ 0x00 = 0x99.
  pscu_calib_record_t written = { { 0xC8U, 0x01U, 0x0AU, 0x00U, 0x00U, 0x99U } };
  pscu_calib_t read = pscu_calib_decode(written);
  g_check((read.board == 1U) && (read.trigger == 0x0AU) && !read.frozen && (read.trim == 0),
          "calib: a record checked over all five bytes from the seed reads back");

  pscu_calib_record_t foreign = pscu_calib_encode(make(1U, MIN_T, false));
  foreign.bytes[PSCU_CALIB_AT_MAGIC] = 0x00U;
  g_check(is_default(pscu_calib_decode(foreign)), "calib: a record without the magic is ignored");
}

static void test_record_ranges(void) {
  g_check(accepted_record(make(0U, MIN_T, false)), "calib: board 0 is valid");
  g_check(accepted_record(make(1U, MIN_T, false)), "calib: board 1 is valid");
  g_check(!accepted_record(make(2U, MIN_T, false)), "calib: board 2 is rejected");
  g_check(!accepted_record(make(0U, (uint8_t)(MIN_T - STEP), false)),
          "calib: a trigger before the default is rejected");
  g_check(accepted_record(make(0U, MAX_T, true)), "calib: the latest start is valid");
  g_check(!accepted_record(make(0U, (uint8_t)(MAX_T + STEP), true)),
          "calib: a trigger past the bound is rejected");
  g_check(!accepted_record(make(0U, (uint8_t)(MIN_T + 1U), false)),
          "calib: a trigger off the step grid is rejected");

  pscu_calib_record_t frozen_two = pscu_calib_encode(make(0U, MIN_T, true));
  frozen_two.bytes[PSCU_CALIB_AT_FROZEN] = 2U;
  frozen_two.bytes[PSCU_CALIB_AT_CHECK] = (uint8_t)(frozen_two.bytes[PSCU_CALIB_AT_CHECK] ^ 3U);
  g_check(is_default(pscu_calib_decode(frozen_two)), "calib: a frozen byte of 2 is rejected");
}

static pscu_calib_t trimmed(pscu_calib_t calib, int8_t trim) {
  pscu_calib_t next = calib;
  next.trim = trim;
  return next;
}

// The trim is a two's-complement byte within the trim bound; it describes the
// chip, so a board change keeps it.
static void test_record_trim(void) {
  pscu_calib_t base = make(0U, MIN_T, false);
  g_check(accepted_record(trimmed(base, PSCU_TRIM_MAX_OFFSET)), "calib: the largest trim is valid");
  g_check(accepted_record(trimmed(base, (int8_t)-PSCU_TRIM_MAX_OFFSET)),
          "calib: the smallest trim is valid");
  g_check(!accepted_record(trimmed(base, (int8_t)(PSCU_TRIM_MAX_OFFSET + 1))),
          "calib: a trim past the bound is rejected");
  g_check(!accepted_record(trimmed(base, (int8_t)(-PSCU_TRIM_MAX_OFFSET - 1))),
          "calib: a trim below the bound is rejected");

  pscu_calib_boot_t moved = pscu_calib_boot(trimmed(make(1U, MIN_T, true), -5), 0U);
  g_check(moved.board_changed && (moved.calib.trim == -5),
          "calib: a board change keeps the oscillator trim");
  g_check(pscu_calib_learn(trimmed(base, 3), PSCU_CALIB_REFUSED).trim == 3,
          "calib: learning leaves the trim alone");
}

static void test_boot(void) {
  pscu_calib_boot_t fresh = pscu_calib_boot(defaults(), 1U);
  g_check(!fresh.board_changed && (fresh.calib.board == 1U),
          "calib: the first boot records the board without a change");

  pscu_calib_t learned = make(0U, (uint8_t)(MIN_T + 4U), true);
  pscu_calib_boot_t again = pscu_calib_boot(learned, 0U);
  g_check(!again.board_changed && same(again.calib, learned),
          "calib: the same board keeps what it learned");

  pscu_calib_boot_t moved = pscu_calib_boot(learned, 1U);
  pscu_calib_t expected = make(1U, MIN_T, false);
  g_check(moved.board_changed && same(moved.calib, expected),
          "calib: a different board reports a change and starts over");
}

static void test_learn_start(void) {
  pscu_calib_t probing = make(0U, MIN_T, false);
  pscu_calib_t later = pscu_calib_learn(probing, PSCU_CALIB_ACCEPTED);
  g_check((later.trigger == (uint8_t)(MIN_T + STEP)) && !later.frozen,
          "calib: an accepted disc probes one step later");

  pscu_calib_t settled = make(0U, (uint8_t)(MIN_T + 4U), true);
  g_check(pscu_calib_learn(settled, PSCU_CALIB_ACCEPTED).trigger == (uint8_t)(MIN_T + 4U),
          "calib: a frozen probe stays put");

  pscu_calib_t near = make(0U, (uint8_t)(MAX_T - STEP), false);
  pscu_calib_t last = pscu_calib_learn(near, PSCU_CALIB_ACCEPTED);
  g_check((last.trigger == MAX_T) && last.frozen, "calib: the probe stops at the bound");

  pscu_calib_t two_short = make(0U, (uint8_t)(MAX_T - (2U * STEP)), false);
  g_check(!pscu_calib_learn(two_short, PSCU_CALIB_ACCEPTED).frozen,
          "calib: the probe keeps going short of the bound");

  pscu_calib_t at_bound = make(0U, MAX_T, false);
  pscu_calib_t held = pscu_calib_learn(at_bound, PSCU_CALIB_ACCEPTED);
  g_check((held.trigger == MAX_T) && held.frozen, "calib: a probe found at the bound stops there");
}

static void test_back_off(void) {
  pscu_calib_t late = make(0U, (uint8_t)(MIN_T + 4U), false);
  pscu_calib_t refused = pscu_calib_learn(late, PSCU_CALIB_REFUSED);
  g_check((refused.trigger == (uint8_t)(MIN_T + STEP)) && refused.frozen,
          "calib: a refusal steps the start back and freezes it");

  pscu_calib_t missed = pscu_calib_learn(late, PSCU_CALIB_MISSED);
  g_check((missed.trigger == (uint8_t)(MIN_T + STEP)) && missed.frozen,
          "calib: a missed window steps the start back and freezes it");

  pscu_calib_t at_default = make(0U, MIN_T, false);
  g_check(pscu_calib_learn(at_default, PSCU_CALIB_REFUSED).trigger == MIN_T,
          "calib: the start never steps back past the default");
}

static void test_missed(void) {
  pscu_calib_t late = make(0U, (uint8_t)(MIN_T + STEP), false);
  uint8_t below = (uint8_t)(MIN_T - 1U);
  g_check(pscu_calib_missed(late, MIN_T, below, false),
          "calib: falling from the default unsent is a miss");
  g_check(!pscu_calib_missed(late, below, below, false),
          "calib: a counter that never got there is no miss");
  g_check(!pscu_calib_missed(late, MIN_T, MIN_T, false),
          "calib: a counter still at the default is no miss");
  g_check(!pscu_calib_missed(late, MIN_T, below, true),
          "calib: a disc that got a string is no miss");

  pscu_calib_t prompt = make(0U, MIN_T, false);
  g_check(!pscu_calib_missed(prompt, MIN_T, below, false), "calib: the default start cannot miss");
}

static void test_watchdog_board(void) {
  pscu_calib_t carrier = make(1U, MIN_T, false);
  g_check(pscu_calib_keeps_board(carrier, true),
          "calib: a watchdog reset keeps the board recorded at boot");
  g_check(!pscu_calib_keeps_board(carrier, false),
          "calib: a power-on boot detects the board again");
  g_check(!pscu_calib_keeps_board(defaults(), true),
          "calib: with no board recorded a watchdog reset still detects one");
  g_check(pscu_calib_stored_board(carrier) == PSCU_BOARD_MODE_WFCK,
          "calib: a recorded carrier board reads as the carrier mode");
  g_check(pscu_calib_stored_board(make(0U, MIN_T, false)) == PSCU_BOARD_MODE_GATE,
          "calib: a recorded gate board reads as the gate mode");
}

void calib_tests(pscu_check_fn check) {
  g_check = check;
  test_record_round_trip();
  test_record_ranges();
  test_record_trim();
  test_boot();
  test_learn_start();
  test_back_off();
  test_missed();
  test_watchdog_board();
}
