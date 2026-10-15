// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "pscu/calib.h"

// Host tests built only for Japan, where the start probe is off: a trigger
// above 11 breaks anti-mod discs there (PsNee V9.0 PSNee.ino:44-51), so the
// learned start is pinned at 10 frames, and every learning outcome must keep
// it there. The shared host suite cannot run in this build, since its inputs
// carry the probing starts of the other regions.

#if !defined(PSCU_REGION_JP)
#error "jp_test.c is built only with PSCU_REGION_JP"
#endif

#define JP_START 10U

static int g_checks = 0;
static int g_failures = 0;

static void check(bool cond, const char *name) {
  g_checks++;
  if (!cond) {
    g_failures++;
    (void)printf("FAIL: %s\n", name);
  }
}

static pscu_calib_t at_start(bool frozen) {
  pscu_calib_t calib = { 1U, JP_START, frozen, 0 };
  return calib;
}

int main(void) {
  check((PSCU_CALIB_TRIGGER_MIN == JP_START) && (PSCU_CALIB_TRIGGER_MAX == JP_START),
        "jp: the start is fixed at 10 frames, earliest and latest alike");

  pscu_calib_t accepted = pscu_calib_learn(at_start(false), PSCU_CALIB_ACCEPTED);
  check((accepted.trigger == JP_START) && accepted.frozen,
        "jp: an accepted disc keeps the start and freezes it");

  pscu_calib_t refused = pscu_calib_learn(at_start(false), PSCU_CALIB_REFUSED);
  check((refused.trigger == JP_START) && refused.frozen,
        "jp: a refused disc keeps the start and freezes it");

  pscu_calib_t missed = pscu_calib_learn(at_start(true), PSCU_CALIB_MISSED);
  check((missed.trigger == JP_START) && missed.frozen, "jp: a missed window keeps the start");

  check(!pscu_calib_missed(at_start(true), 12U, 2U, false),
        "jp: a lead-in that ends never counts as a miss, the start being the earliest");

  (void)printf("%d checks, %d failures\n", g_checks, g_failures);
  return (g_failures == 0) ? 0 : 1;
}
