// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "trim_test.h"

#include <stdbool.h>
#include <stdint.h>

#include "pscu/trim.h"

// Host tests for the oscillator trim. The logic is independent of the tick
// rate, so these tests use a fine reference, 104 ticks per frame, a window of 83
// to 124 and 6666 ticks over a batch, which gives a 66-tick dead band and room to
// test each boundary on both sides; the firmware passes its own coarser one.
// Every boundary is tested on both sides, since a trim that steps the wrong way
// or one step too far skews every legacy-board bit after it.

#define BATCH PSCU_TRIM_FRAMES
#define EXPECTED 6666U
#define BAND 66U

static pscu_check_fn g_check;

static pscu_trim_ref_t ref(void) {
  pscu_trim_ref_t r = { 83U, 124U, EXPECTED };
  return r;
}

// Feed a batch whose ticks sum to total, spread evenly so every sample stays
// inside the window: each carries total / BATCH, and the first total % BATCH
// carry one more. The last step's result is returned.
static pscu_trim_step_t batch_of(uint32_t total) {
  pscu_trim_t state = pscu_trim_init();
  pscu_trim_step_t step = { state, 0 };
  for (uint16_t i = 0U; i < BATCH; i++) {
    uint32_t extra = (i < (total % BATCH)) ? 1U : 0U;
    step = pscu_trim_step(step.state, true, (uint16_t)((total / BATCH) + extra), ref());
  }
  return step;
}

static void test_verdicts(void) {
  g_check(batch_of(EXPECTED).adjust == 0, "trim: a nominal batch holds still");
  g_check(batch_of(EXPECTED + BAND).adjust == 0, "trim: the top of the dead band holds still");
  g_check(batch_of(EXPECTED + BAND + 1U).adjust == -1, "trim: a fast oscillator steps down");
  g_check(batch_of(EXPECTED - BAND).adjust == 0, "trim: the bottom of the dead band holds still");
  g_check(batch_of(EXPECTED - BAND - 1U).adjust == 1, "trim: a slow oscillator steps up");

  // Past the dead band the step grows with the error, one notch per dead band
  // of error, up to PSCU_TRIM_MAX_STEP, so a chip far off converges in a few
  // batches rather than one notch per batch.
  g_check(batch_of(EXPECTED + (2U * BAND)).adjust == -1,
          "trim: two bands of error step one notch, leaving one band");
  g_check(batch_of(EXPECTED + (2U * BAND) + 1U).adjust == -2,
          "trim: just past two bands steps two notches");
  g_check(batch_of(EXPECTED - ((3U * BAND) + 1U)).adjust == 3,
          "trim: just past three bands steps three notches");
  g_check(batch_of(EXPECTED + (6U * BAND) + 1U).adjust == -PSCU_TRIM_MAX_STEP,
          "trim: a large error steps at most the largest step");

  pscu_trim_step_t done = batch_of(EXPECTED + 200U);
  g_check((done.state.frames == 0U) && (done.state.ticks == 0U),
          "trim: a judged batch starts over");
}

static void test_samples(void) {
  pscu_trim_t start = pscu_trim_init();
  pscu_trim_step_t low = pscu_trim_step(start, true, 83U, ref());
  g_check((low.state.frames == 1U) && (low.state.ticks == 83U),
          "trim: the window's low edge counts");
  pscu_trim_step_t high = pscu_trim_step(start, true, 124U, ref());
  g_check(high.state.frames == 1U, "trim: the window's high edge counts");
  g_check(pscu_trim_step(start, true, 82U, ref()).state.frames == 0U,
          "trim: a short period is ignored");
  g_check(pscu_trim_step(start, true, 125U, ref()).state.frames == 0U,
          "trim: a skipped frame is ignored");
  pscu_trim_step_t off = pscu_trim_step(start, false, 104U, ref());
  g_check((off.state.frames == 0U) && (off.adjust == 0), "trim: a non-sample is ignored");
}

static void test_apply(void) {
  g_check(pscu_trim_apply(0x50U, 0x50U, -1) == 0x4FU, "trim: a step down moves one LSB");
  g_check(pscu_trim_apply(0x50U, 0x50U, 1) == 0x51U, "trim: a step up moves one LSB");
  g_check(pscu_trim_apply(0x50U, 0x50U, 0) == 0x50U, "trim: no step keeps the value");
  g_check(pscu_trim_apply(0x50U, 0x60U, 0) == 0x60U, "trim: the bound itself is a valid value");
  g_check(pscu_trim_apply(0x50U, 0x60U, 1) == 0x60U, "trim: no step past the upper bound");
  g_check(pscu_trim_apply(0x50U, 0x40U, -1) == 0x40U, "trim: no step past the lower bound");
  g_check(pscu_trim_apply(0x50U, 0x41U, -1) == 0x40U,
          "trim: a step onto the lower bound is allowed");
  g_check(pscu_trim_apply(0x50U, 0x5FU, 1) == 0x60U,
          "trim: a step onto the upper bound is allowed");

  g_check(pscu_trim_apply(0x82U, 0x80U, -1) == 0x80U,
          "trim: no step across the range bit downward");
  g_check(pscu_trim_apply(0x7DU, 0x7FU, 1) == 0x7FU, "trim: no step across the range bit upward");
  g_check(pscu_trim_apply(0x02U, 0x00U, -1) == 0x00U, "trim: no step below zero");
  g_check(pscu_trim_apply(0x02U, 0x01U, -1) == 0x00U, "trim: a step down to zero is allowed");
  g_check(pscu_trim_apply(0xFDU, 0xFFU, 1) == 0xFFU, "trim: no step above 255");
  g_check(pscu_trim_apply(0xFDU, 0xFEU, 1) == 0xFFU, "trim: a step up to 255 is allowed");

  // A multi-notch step moves as far as it is allowed and stops at the first
  // bound it meets, the offset limit or the CAL7 edge, rather than being
  // refused whole and leaving the chip where it was.
  g_check(pscu_trim_apply(0x50U, 0x50U, -3) == 0x4DU, "trim: a three-notch step moves three");
  g_check(pscu_trim_apply(0x50U, 0x5EU, 4) == 0x60U, "trim: a step stops at the offset bound");
  g_check(pscu_trim_apply(0x7DU, 0x7CU, 4) == 0x7FU, "trim: a step stops below the range bit");
  g_check(pscu_trim_apply(0x82U, 0x81U, -4) == 0x80U, "trim: a step stops above the range bit");
}

// A chip whose RC is off by error_ppm, where each OSCCAL notch moves the
// frequency by notch_ppm: each batch measures ticks in proportion to the
// running frequency, and the trim steps until it holds still. Returns the
// batches taken, or 0 when it never settles within the limit. The datasheet
// gives the notch only as a curve (ATtiny25/45/85, calibrated RC oscillator
// frequency against OSCCAL), so the tests span half to twice the nominal.
#define MODEL_BATCHES 12U
#define PPM 1000000L

static uint8_t settle(long error_ppm, long notch_ppm, uint8_t *final_osccal) {
  const uint8_t factory = 0x50U;
  uint8_t osccal = factory;
  for (uint8_t batch = 1U; batch <= MODEL_BATCHES; batch++) {
    long offset = (long)osccal - (long)factory;
    long rate = PPM + error_ppm + (notch_ppm * offset);
    uint32_t ticks = (uint32_t)(((long)EXPECTED * rate) / PPM);
    int8_t adjust = batch_of(ticks).adjust;
    if (adjust == 0) {
      *final_osccal = osccal;
      return batch;
    }
    osccal = pscu_trim_apply(factory, osccal, adjust);
  }
  return 0U;
}

// Whether a chip settled at all, and within the given number of batches.
static bool settles_within(long error_ppm, long notch_ppm, uint8_t batches) {
  uint8_t osccal = 0U;
  uint8_t taken = settle(error_ppm, notch_ppm, &osccal);
  return (taken > 0U) && (taken <= batches);
}

static void test_convergence(void) {
  g_check(settles_within(50000L, 8000L, 4U),
          "trim: a chip 5 percent fast settles within four batches");
  g_check(settles_within(-50000L, 8000L, 4U),
          "trim: a chip 5 percent slow settles within four batches");
  g_check(settles_within(50000L, 16000L, 5U),
          "trim: twice the nominal notch settles without oscillating");
  g_check(settles_within(50000L, 4000L, 5U), "trim: half the nominal notch still settles");
  uint8_t osccal = 0U;
  uint8_t taken = settle(9000L, 8000L, &osccal);
  g_check((taken == 1U) && (osccal == 0x50U), "trim: a chip inside the band never moves");
}

void trim_tests(pscu_check_fn check) {
  g_check = check;
  test_verdicts();
  test_samples();
  test_apply();
  test_convergence();
}
