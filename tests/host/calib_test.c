// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "calib_test.h"

#include <stdbool.h>
#include <stdint.h>

#include "pscu/calib.h"

// Host tests for the calibration record and the learning rules. Each boundary
// is tested on both sides, because a learned value that lands one step off is
// exactly the kind of fault no console would ever report: the chip would just
// be a little less stealthy, or a disc would fail once.

#define MIN_T PSCU_CALIB_TRIGGER_MIN
#define MAX_T PSCU_CALIB_TRIGGER_MAX
#define STEP PSCU_CALIB_TRIGGER_STEP

static pscu_check_fn g_check;

// The defaults are what an erased chip reads as, so the tests take them from
// there rather than from a function the firmware has no other use for.
static pscu_calib_t defaults(void) {
  pscu_calib_record_t erased = { { 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU } };
  return pscu_calib_decode(erased);
}

static pscu_calib_t make(uint8_t board, uint8_t cap, uint8_t trigger, bool frozen) {
  pscu_calib_t calib = { board, cap, trigger, frozen };
  return calib;
}

static bool same(pscu_calib_t a, pscu_calib_t b) {
  return (a.board == b.board) && (a.cap == b.cap) && (a.trigger == b.trigger) &&
         (a.frozen == b.frozen);
}

// Encode never validates, so it builds records holding any field value; decoding
// them shows which values the reader accepts.
static bool accepted_record(pscu_calib_t calib) {
  return same(pscu_calib_decode(pscu_calib_encode(calib)), calib);
}

static bool is_default(pscu_calib_t calib) {
  return same(calib, defaults());
}

static void test_record_round_trip(void) {
  pscu_calib_t erased = defaults();
  g_check((erased.board == PSCU_CALIB_BOARD_NONE) && (erased.cap == PSCU_CALIB_CAP_MAX) &&
              (erased.trigger == MIN_T) && !erased.frozen,
          "calib: an erased chip reads as defaults");
  g_check(accepted_record(make(1U, 7U, (uint8_t)(MIN_T + 4U), true)),
          "calib: a learned record reads back unchanged");
  g_check(accepted_record(make(PSCU_CALIB_BOARD_NONE, PSCU_CALIB_CAP_MAX, MIN_T, false)),
          "calib: a fresh record reads back unchanged");

  pscu_calib_record_t torn = pscu_calib_encode(make(1U, 7U, MIN_T, false));
  torn.bytes[PSCU_CALIB_AT_CAP] = 9U;
  g_check(is_default(pscu_calib_decode(torn)), "calib: a byte changed after the check is caught");

  pscu_calib_record_t stale = pscu_calib_encode(make(1U, 7U, MIN_T, false));
  stale.bytes[PSCU_CALIB_AT_CHECK] = (uint8_t)(stale.bytes[PSCU_CALIB_AT_CHECK] ^ 0x01U);
  g_check(is_default(pscu_calib_decode(stale)), "calib: a wrong check byte reads as defaults");

  pscu_calib_record_t foreign = pscu_calib_encode(make(1U, 7U, MIN_T, false));
  foreign.bytes[PSCU_CALIB_AT_MAGIC] = 0x00U;
  g_check(is_default(pscu_calib_decode(foreign)), "calib: a record without the magic is ignored");
}

static void test_record_ranges(void) {
  g_check(accepted_record(make(0U, 7U, MIN_T, false)), "calib: board 0 is valid");
  g_check(accepted_record(make(1U, 7U, MIN_T, false)), "calib: board 1 is valid");
  g_check(!accepted_record(make(2U, 7U, MIN_T, false)), "calib: board 2 is rejected");
  g_check(accepted_record(make(0U, PSCU_CALIB_CAP_MIN, MIN_T, false)),
          "calib: the cap floor is valid");
  g_check(!accepted_record(make(0U, (uint8_t)(PSCU_CALIB_CAP_MIN - 1U), MIN_T, false)),
          "calib: a cap below the floor is rejected");
  g_check(accepted_record(make(0U, PSCU_CALIB_CAP_MAX, MIN_T, false)),
          "calib: the full cap is valid");
  g_check(!accepted_record(make(0U, (uint8_t)(PSCU_CALIB_CAP_MAX + 1U), MIN_T, false)),
          "calib: a cap above the safety cap is rejected");
  g_check(!accepted_record(make(0U, 7U, (uint8_t)(MIN_T - STEP), false)),
          "calib: a trigger before the default is rejected");
  g_check(accepted_record(make(0U, 7U, MAX_T, true)), "calib: the latest start is valid");
  g_check(!accepted_record(make(0U, 7U, (uint8_t)(MAX_T + STEP), true)),
          "calib: a trigger past the bound is rejected");
  g_check(!accepted_record(make(0U, 7U, (uint8_t)(MIN_T + 1U), false)),
          "calib: a trigger off the step grid is rejected");

  pscu_calib_record_t frozen_two = pscu_calib_encode(make(0U, 7U, MIN_T, true));
  frozen_two.bytes[PSCU_CALIB_AT_FROZEN] = 2U;
  frozen_two.bytes[PSCU_CALIB_AT_CHECK] = (uint8_t)(frozen_two.bytes[PSCU_CALIB_AT_CHECK] ^ 3U);
  g_check(is_default(pscu_calib_decode(frozen_two)), "calib: a frozen byte of 2 is rejected");
}

static void test_boot(void) {
  pscu_calib_boot_t fresh = pscu_calib_boot(defaults(), 1U);
  g_check(
      !fresh.board_changed && (fresh.calib.board == 1U) && (fresh.calib.cap == PSCU_CALIB_CAP_MAX),
      "calib: the first boot records the board without a change");

  pscu_calib_t learned = make(0U, 7U, (uint8_t)(MIN_T + 4U), true);
  pscu_calib_boot_t again = pscu_calib_boot(learned, 0U);
  g_check(!again.board_changed && same(again.calib, learned),
          "calib: the same board keeps what it learned");

  pscu_calib_boot_t moved = pscu_calib_boot(learned, 1U);
  pscu_calib_t expected = make(1U, PSCU_CALIB_CAP_MAX, MIN_T, false);
  g_check(moved.board_changed && same(moved.calib, expected),
          "calib: a different board reports a change and starts over");
}

static void test_learn_cap(void) {
  pscu_calib_t start = make(0U, PSCU_CALIB_CAP_MAX, MIN_T, true);
  g_check(pscu_calib_learn(start, PSCU_CALIB_ACCEPTED, 1U).cap == PSCU_CALIB_CAP_MIN,
          "calib: one string needed gives the smallest cap");
  g_check(pscu_calib_learn(start, PSCU_CALIB_ACCEPTED, 3U).cap == 7U,
          "calib: the cap is need plus margin");
  uint8_t last_fit = (uint8_t)(PSCU_CALIB_CAP_FITS - 1U);
  g_check(pscu_calib_learn(start, PSCU_CALIB_ACCEPTED, last_fit).cap == PSCU_CALIB_CAP_MAX,
          "calib: the largest need that fits reaches the safety cap");
  g_check(
      pscu_calib_learn(start, PSCU_CALIB_ACCEPTED, PSCU_CALIB_CAP_FITS).cap == PSCU_CALIB_CAP_MAX,
      "calib: a need past the fit is held at the safety cap");

  pscu_calib_t low = make(0U, 7U, MIN_T, true);
  g_check(pscu_calib_learn(low, PSCU_CALIB_REFUSED, 7U).cap == PSCU_CALIB_CAP_MAX,
          "calib: a refusal restores the full cap");
  g_check(pscu_calib_learn(low, PSCU_CALIB_MISSED, 0U).cap == 7U,
          "calib: a missed window keeps the cap");
}

static void test_learn_start(void) {
  pscu_calib_t probing = make(0U, PSCU_CALIB_CAP_MAX, MIN_T, false);
  pscu_calib_t later = pscu_calib_learn(probing, PSCU_CALIB_ACCEPTED, 2U);
  g_check((later.trigger == (uint8_t)(MIN_T + STEP)) && !later.frozen,
          "calib: an accepted disc probes one step later");

  pscu_calib_t settled = make(0U, PSCU_CALIB_CAP_MAX, (uint8_t)(MIN_T + 4U), true);
  g_check(pscu_calib_learn(settled, PSCU_CALIB_ACCEPTED, 2U).trigger == (uint8_t)(MIN_T + 4U),
          "calib: a frozen probe stays put");

  pscu_calib_t near = make(0U, PSCU_CALIB_CAP_MAX, (uint8_t)(MAX_T - STEP), false);
  pscu_calib_t last = pscu_calib_learn(near, PSCU_CALIB_ACCEPTED, 2U);
  g_check((last.trigger == MAX_T) && last.frozen, "calib: the probe stops at the bound");

  pscu_calib_t two_short = make(0U, PSCU_CALIB_CAP_MAX, (uint8_t)(MAX_T - (2U * STEP)), false);
  g_check(!pscu_calib_learn(two_short, PSCU_CALIB_ACCEPTED, 2U).frozen,
          "calib: the probe keeps going short of the bound");

  pscu_calib_t at_bound = make(0U, PSCU_CALIB_CAP_MAX, MAX_T, false);
  pscu_calib_t held = pscu_calib_learn(at_bound, PSCU_CALIB_ACCEPTED, 2U);
  g_check((held.trigger == MAX_T) && held.frozen, "calib: a probe found at the bound stops there");
}

static void test_back_off(void) {
  pscu_calib_t late = make(0U, 7U, (uint8_t)(MIN_T + 4U), false);
  pscu_calib_t refused = pscu_calib_learn(late, PSCU_CALIB_REFUSED, 7U);
  g_check((refused.trigger == (uint8_t)(MIN_T + STEP)) && refused.frozen,
          "calib: a refusal steps the start back and freezes it");

  pscu_calib_t missed = pscu_calib_learn(late, PSCU_CALIB_MISSED, 0U);
  g_check((missed.trigger == (uint8_t)(MIN_T + STEP)) && missed.frozen,
          "calib: a missed window steps the start back and freezes it");

  pscu_calib_t at_default = make(0U, 7U, MIN_T, false);
  g_check(pscu_calib_learn(at_default, PSCU_CALIB_REFUSED, 7U).trigger == MIN_T,
          "calib: the start never steps back past the default");
}

static void test_missed(void) {
  pscu_calib_t late = make(0U, 7U, (uint8_t)(MIN_T + STEP), false);
  uint8_t below = (uint8_t)(MIN_T - 1U);
  g_check(pscu_calib_missed(late, MIN_T, below, false),
          "calib: falling from the default unsent is a miss");
  g_check(!pscu_calib_missed(late, below, below, false),
          "calib: a counter that never got there is no miss");
  g_check(!pscu_calib_missed(late, MIN_T, MIN_T, false),
          "calib: a counter still at the default is no miss");
  g_check(!pscu_calib_missed(late, MIN_T, below, true),
          "calib: a disc that got a string is no miss");

  pscu_calib_t prompt = make(0U, 7U, MIN_T, false);
  g_check(!pscu_calib_missed(prompt, MIN_T, below, false), "calib: the default start cannot miss");
}

void calib_tests(pscu_check_fn check) {
  g_check = check;
  test_record_round_trip();
  test_record_ranges();
  test_boot();
  test_learn_cap();
  test_learn_start();
  test_back_off();
  test_missed();
}
