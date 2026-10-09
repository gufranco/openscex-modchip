// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "loop_test.h"

#include <stdbool.h>
#include <stdint.h>

#include "pscu/calib.h"
#include "pscu/config.h"
#include "pscu/led.h"
#include "pscu/loop.h"
#include "pscu/subq.h"

// Host tests for the run loop's per-pass decisions: how the modules are wired
// together, which no module test can see. Each case drives whole passes with
// the frames a console produces, so a wrong wire shows up as a wrong string, a
// wrong store or a wrong LED code. A pass is one 13 ms frame unless a case needs
// a longer stretch, such as the silence of a disc swap.

static pscu_check_fn g_check;

#define FRAME_MS 13U
#define TRIGGER 10U
#define CAP 16U

// A lead-in TOC marker, the frame the region check reads; a lead-out frame,
// valid but never framed as lead-in, so it decays the counter; a program-area
// frame, the sign the console accepted; a Video CD lead-in marker, which only
// the SCPH-5903 filter rejects; and the pattern a failed capture leaves.
static const uint8_t LEAD_IN[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0, 0, 0,
                                                        0,     0,     0,     0, 0, 0 };
static const uint8_t LEAD_OUT[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0xAAU, 0x01U, 0, 0, 0,
                                                         0,     0,     0,     0, 0, 0 };
static const uint8_t PROGRAM[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0x01U, 0x01U, 0x00U, 0x02U, 0,
                                                        0,     0,     0x02U, 0,     0,     0 };
static const uint8_t VCD[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0x02U, 0, 0,
                                                    0,     0,     0,     0,     0, 0 };
static const uint8_t FAILED[PSCU_SUBQ_FRAME_BYTES] = { 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU,
                                                       0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU };

static pscu_calib_t calib_with(uint8_t cap, uint8_t trigger, bool frozen) {
  pscu_calib_t calib = { 0U, cap, trigger, frozen, 0 };
  return calib;
}

// The LED past its boot blink, so a fault or a result can take it at once.
static pscu_led_t waiting_led(void) {
  return pscu_led_step(pscu_led_init(1U, 0U), PSCU_LED_EVENT_NONE, 0U, 700U).state;
}

// The cap an accepted session leaves from CAP: halfway down to its need plus
// the margin (calib.c, pscu_calib_accepted).
static uint8_t halfway(uint8_t strings) {
  uint8_t fit = (uint8_t)(strings + PSCU_CALIB_CAP_MARGIN);
  return (uint8_t)(fit + ((uint8_t)(CAP - fit) / 2U));
}

static pscu_loop_t fresh(pscu_calib_t calib) {
  return pscu_loop_init(calib, waiting_led(), false);
}

// One pass's decisions together with the state it leaves, so each case can
// chain passes and read both without keeping a separate state variable.
typedef struct {
  pscu_loop_t state;
  bool fire;
  bool store;
  bool trim_sample;
  bool quiet;
  bool led_on;
} step_t;

static step_t pass_with(
    pscu_loop_t state, const uint8_t *frame, bool captured, bool supply_ok, uint32_t ms) {
  pscu_loop_in_t in = { frame, captured, supply_ok, ms };
  pscu_loop_t next = state;
  pscu_loop_out_t out = pscu_loop_step(&next, &in);
  step_t step = { next, out.fire, out.store, out.trim_sample, out.quiet, out.led_on };
  return step;
}

static step_t pass(pscu_loop_t state, const uint8_t *frame) {
  return pass_with(state, frame, true, true, FRAME_MS);
}

static step_t feed(pscu_loop_t state, const uint8_t *frame, uint16_t count) {
  step_t out = pass(state, frame);
  for (uint16_t i = 1U; i < count; i++) {
    out = pass(out.state, frame);
  }
  return out;
}

static void test_init(void) {
  pscu_loop_t state = pscu_loop_init(calib_with(7U, 12U, true), waiting_led(), true);
  g_check((state.cap == 7U) && (state.calib.trigger == 12U) && state.vcd_filter,
          "loop: init takes the stored cap, start and filter");
  g_check((state.counter == 0U) && !state.was_gone && (state.since.ms == 0U) &&
              !state.since.framed && !state.since.armed,
          "loop: init starts with an empty counter and no disc history");
  g_check((state.session.injects == 0U) && !state.session.resolved && (state.stealth.sent == 0U),
          "loop: init starts with no session and no strings");
}

// The window opens on the frame that brings the counter to the start point, and
// not one frame before; until then the chip is quiet and may store a trim.
static void test_fire_at_trigger(void) {
  step_t before = feed(fresh(calib_with(CAP, TRIGGER, false)), LEAD_IN, TRIGGER - 1U);
  g_check(!before.fire && before.quiet && (before.state.counter == TRIGGER - 1U),
          "loop: no string one frame before the start point");
  step_t fired = pass(before.state, LEAD_IN);
  g_check(fired.fire && !fired.quiet && (fired.state.stealth.sent == 1U),
          "loop: the frame reaching the start point sends a string");
  g_check((fired.state.session.injects == 1U) && (fired.state.led.stage == PSCU_LED_INJECT),
          "loop: a string opens the session and hands the LED to injection");
  g_check(fired.state.since.armed && !fired.led_on, "loop: a string marks the disc armed");
}

// A low supply inside the window holds the burst and shows code 7; the next
// good frame sends the string the low one held back.
static void test_held_window(void) {
  step_t before = feed(fresh(calib_with(CAP, TRIGGER, false)), LEAD_IN, TRIGGER - 1U);
  step_t held = pass_with(before.state, LEAD_IN, true, false, FRAME_MS);
  g_check(!held.fire && !held.quiet && (held.state.stealth.sent == 0U),
          "loop: a low supply sends nothing inside the window");
  g_check((held.state.led.code == PSCU_LED_CODE_SUPPLY) && held.led_on,
          "loop: a low supply lights code 7");
  g_check(held.state.since.armed, "loop: a held window still counts as reached");
  step_t resumed = pass(held.state, LEAD_IN);
  g_check(resumed.fire, "loop: the string goes out once the supply is back");
}

// A program-area frame after a string confirms the disc: code 1, a store, and
// the cap learned as the strings sent plus the margin, with the start probed
// one step later.
static void test_accepted(void) {
  step_t fired = feed(fresh(calib_with(CAP, TRIGGER, false)), LEAD_IN, TRIGGER);
  step_t accepted = pass(fired.state, PROGRAM);
  g_check(accepted.store && accepted.state.session.resolved,
          "loop: a program-area frame resolves the session and stores");
  g_check((accepted.state.calib.cap == halfway(1U)) &&
              (accepted.state.calib.trigger == (uint8_t)(TRIGGER + PSCU_CALIB_TRIGGER_STEP)),
          "loop: an accepted disc learns the cap and probes a later start");
  g_check(accepted.state.led.code == PSCU_LED_CODE_ACCEPTED, "loop: an accepted disc shows code 1");
  step_t gap = feed(fired.state, LEAD_IN, PSCU_STEALTH_GAP_FRAMES + 1U);
  g_check(gap.fire && (gap.state.session.injects == 2U),
          "loop: a second string joins the same session");
  step_t two = pass(gap.state, PROGRAM);
  g_check(two.state.calib.cap == halfway(2U),
          "loop: the cap is learned from every string of the session");
  step_t leaving = pass_with(fired.state, PROGRAM, false, true, PSCU_DISC_GONE_MS);
  g_check(leaving.state.session.resolved && !leaving.store,
          "loop: a program frame read as the disc leaves teaches nothing");
  step_t after = pass(accepted.state, PROGRAM);
  g_check(!after.store && (after.state.calib.cap == accepted.state.calib.cap),
          "loop: a resolved session learns once");

  // The drive reads the program area only once it has accepted, so a program
  // frame empties the counter and closes the window at once; a later lead-in
  // re-read, as the anti-mod v2 ReadTOC is, climbs from zero and is served.
  g_check(accepted.state.counter == 0U, "loop: a program-area frame empties the counter");
  step_t reread = feed(after.state, LEAD_IN, (uint16_t)(TRIGGER + PSCU_CALIB_TRIGGER_STEP));
  g_check(reread.fire, "loop: a lead-in re-read after play is served again");
}

// With no program area, the session resolves as refused after the bounded
// wait, counted from the last string, and not one frame sooner.
static void test_refused(void) {
  step_t fired = feed(fresh(calib_with(8U, 12U, false)), LEAD_IN, 12U);
  step_t waiting = feed(fired.state, LEAD_OUT, PSCU_CONFIRM_FRAMES - 1U);
  g_check(!waiting.store && !waiting.state.session.resolved,
          "loop: the session waits the whole confirmation span");
  step_t refused = pass(waiting.state, LEAD_OUT);
  g_check(refused.store && (refused.state.led.code == PSCU_LED_CODE_REFUSED),
          "loop: the wait running out stores a refusal and shows code 2");
  g_check((refused.state.calib.cap == CAP) && (refused.state.calib.trigger == TRIGGER) &&
              refused.state.calib.frozen,
          "loop: a refusal restores the full cap and steps the start back");
}

// A disc leaving after a string resolves its session at once without teaching
// the calibration, and the next disc starts from nothing.
static void test_disc_gone(void) {
  step_t fired = feed(fresh(calib_with(CAP, TRIGGER, false)), LEAD_IN, TRIGGER);
  step_t gone = pass_with(fired.state, FAILED, false, true, PSCU_DISC_GONE_MS);
  g_check(
      !gone.store && gone.state.session.resolved && (gone.state.led.code == PSCU_LED_CODE_REFUSED),
      "loop: a disc leaving shows its refusal and learns nothing");
  g_check((gone.state.counter == 0U) && (gone.state.stealth.sent == 0U) && gone.state.was_gone,
          "loop: a disc leaving empties the counter and the burst");
  g_check((gone.state.since.ms == 0U) && !gone.state.since.armed,
          "loop: a disc leaving starts the disc history over");
  step_t still = pass_with(gone.state, FAILED, false, true, FRAME_MS);
  g_check(still.state.led.code == PSCU_LED_CODE_REFUSED,
          "loop: the result stays while no disc is in");
  step_t next = pass(gone.state, LEAD_IN);
  g_check((next.state.led.stage == PSCU_LED_WAIT) && !next.state.was_gone,
          "loop: the next disc clears the last disc's result");
  step_t short_pause = pass_with(fired.state, FAILED, false, true, PSCU_DISC_GONE_MS - 1U);
  g_check(!short_pause.state.was_gone && !short_pause.state.session.resolved,
          "loop: a pause short of the bound keeps the disc and its session");
}

// The counter falling back below the default start while nothing reached the
// window is a missed window: the start steps back and code 2 shows. A window
// the supply held shut is not a miss.
static void test_missed(void) {
  step_t climbed = feed(fresh(calib_with(CAP, 12U, false)), LEAD_IN, 11U);
  step_t at_default = pass(climbed.state, LEAD_OUT);
  g_check(!at_default.store, "loop: falling to the default start is not yet a miss");
  step_t missed = pass(at_default.state, LEAD_OUT);
  g_check(missed.store && (missed.state.calib.trigger == TRIGGER) && missed.state.calib.frozen,
          "loop: falling below the default start steps the start back");
  g_check(missed.state.led.code == PSCU_LED_CODE_REFUSED, "loop: a missed window shows code 2");

  step_t reached = feed(fresh(calib_with(CAP, 12U, false)), LEAD_IN, 11U);
  step_t held = pass_with(reached.state, LEAD_IN, true, false, FRAME_MS);
  step_t decayed = feed(held.state, LEAD_OUT, 4U);
  g_check(!decayed.store && (decayed.state.calib.trigger == 12U),
          "loop: a window the supply held shut is not a miss");
}

// The trim times only consecutive lead-in frames from a disc spinning for a
// second, and only frames that were really captured.
static void test_trim_sample(void) {
  pscu_loop_t start = fresh(calib_with(CAP, TRIGGER, false));
  g_check(!pass(start, LEAD_IN).trim_sample, "loop: no trim sample while the disc settles");
  step_t early = pass_with(start, LEAD_OUT, true, true, PSCU_LOOP_TRIM_SETTLE_MS - 1U);
  g_check(!pass(early.state, LEAD_IN).trim_sample, "loop: no trim sample just short of a second");
  step_t settled = pass_with(start, LEAD_OUT, true, true, PSCU_LOOP_TRIM_SETTLE_MS);
  g_check(pass(settled.state, LEAD_IN).trim_sample, "loop: a lead-in frame samples the trim");
  g_check(pass(settled.state, PROGRAM).trim_sample,
          "loop: a program-area frame in play samples the trim too");
  g_check(!pass(settled.state, FAILED).trim_sample, "loop: a frame that is not valid is no sample");
  g_check(!pass_with(settled.state, LEAD_IN, false, true, FRAME_MS).trim_sample,
          "loop: a failed capture is no sample");

  // The string blocks the loop for 176 ms, so the next frame's stamp would time
  // the tail of the string, not a frame period; the pass that fires is no
  // sample, which leaves the next one without a partner.
  step_t climbing = settled;
  for (uint8_t i = 0U; (i < (uint8_t)(TRIGGER + 2U)) && !climbing.fire; i++) {
    climbing = pass(climbing.state, LEAD_IN);
  }
  g_check(climbing.fire && !climbing.trim_sample,
          "loop: the pass that fires a string is no sample");
}

// The cap is taken when an arming starts: a value learned mid-burst waits for
// the next arming.
static void test_cap_per_arming(void) {
  step_t fired = feed(fresh(calib_with(6U, TRIGGER, false)), LEAD_IN, TRIGGER);
  pscu_loop_t learned = fired.state;
  learned.calib.cap = CAP;
  step_t mid = pass(learned, LEAD_IN);
  g_check(mid.state.cap == 6U, "loop: a cap learned mid-burst waits for the next arming");
  step_t closed = feed(mid.state, LEAD_OUT, TRIGGER);
  step_t next = pass(closed.state, LEAD_OUT);
  g_check(next.state.cap == CAP, "loop: the next arming takes the learned cap");
}

// The Video-CD filter is the state's, not the build's, so both rules run here.
static void test_vcd_filter(void) {
  pscu_loop_t filtered = pscu_loop_init(calib_with(CAP, TRIGGER, false), waiting_led(), true);
  g_check(pass(filtered, VCD).state.counter == 0U, "loop: the filter ignores a Video CD lead-in");
  g_check(pass(fresh(calib_with(CAP, TRIGGER, false)), VCD).state.counter == 1U,
          "loop: without the filter a Video CD lead-in counts");
}

// The disc history behind codes 3 and 4: time, frames and the window.
static void test_since(void) {
  pscu_loop_t start = fresh(calib_with(CAP, TRIGGER, false));
  g_check(pass(start, LEAD_OUT).state.since.framed, "loop: a valid frame marks the disc framed");
  g_check(!pass_with(start, LEAD_OUT, false, true, FRAME_MS).state.since.framed,
          "loop: a failed capture does not");
  g_check(!pass(start, FAILED).state.since.framed,
          "loop: nor does a captured frame that is not valid");
  g_check(pass(start, PROGRAM).state.since.armed,
          "loop: a program-area frame marks the disc armed");
  g_check(!pass(start, LEAD_OUT).state.since.armed, "loop: a lead-out frame does not");
  pscu_loop_t full = start;
  full.since.ms = 0xFFFFFFF0UL;
  g_check(pass_with(full, LEAD_OUT, true, true, 0x100U).state.since.ms == 0xFFFFFFFFUL,
          "loop: the disc time saturates");
  g_check(pass_with(start, LEAD_OUT, true, true, 0x100U).state.since.ms == 0x100U,
          "loop: the disc time adds the pass");
  pscu_loop_t timed = start;
  timed.since.ms = 0x200U;
  g_check(pass_with(timed, LEAD_OUT, true, true, 0U).state.since.ms == 0x200U,
          "loop: a pass that took no time adds none");
}

static void test_faults(void) {
  pscu_loop_t start = fresh(calib_with(CAP, TRIGGER, false));
  step_t silent = pass_with(start, FAILED, false, true, PSCU_LED_NO_SQCK_MS);
  g_check(silent.state.led.code == PSCU_LED_CODE_NO_SQCK,
          "loop: no frame since power-on is code 3");
  step_t audio = pass_with(start, LEAD_OUT, true, true, PSCU_LED_NO_CHECK_MS);
  g_check(audio.state.led.code == PSCU_LED_CODE_NO_CHECK,
          "loop: frames but no region check is code 4");
}

void loop_tests(pscu_check_fn check) {
  g_check = check;
  test_init();
  test_fire_at_trigger();
  test_held_window();
  test_accepted();
  test_refused();
  test_disc_gone();
  test_missed();
  test_trim_sample();
  test_cap_per_arming();
  test_vcd_filter();
  test_since();
  test_faults();
}
