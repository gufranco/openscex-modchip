// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "loop_test.h"

#include <stdbool.h>
#include <stdint.h>

#include "pscu/calib.h"
#include "pscu/config.h"
#include "pscu/indicator.h"
#include "pscu/loop.h"
#include "pscu/subq.h"

// Host tests for the run loop: how the modules are wired together. Each case
// drives whole passes with console frames; a pass is one 13 ms frame unless a
// case needs longer.

// The stealth gap and cap are requirements, not tuning: rule 5 and PsNee V9.0's
// REQUEST_INJECT_GAP 5. Pinning the values here makes a change to either one
// fail the build instead of passing every case that uses the symbol.
_Static_assert(PSCU_STEALTH_GAP_FRAMES == 5U, "the stealth gap is five frames");
_Static_assert(PSCU_STEALTH_STRINGS == 16U, "the stealth cap is sixteen strings");

static pscu_check_fn g_check;

#define FRAME_MS 13U
#define TRIGGER 10U

// A lead-in TOC marker; a lead-out frame, which decays the counter; a
// program-area frame, the sign of acceptance; a Video CD lead-in marker; and
// the pattern a failed capture leaves.
static const uint8_t LEAD_IN[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0, 0,     0,
                                                        0,     0,     0,     0, 0x64U, 0x49U };
static const uint8_t LEAD_OUT[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0xAAU, 0x01U, 0, 0,     0,
                                                         0,     0,     0,     0, 0x52U, 0xA5U };
static const uint8_t PROGRAM[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0x01U, 0x01U, 0x00U, 0x02U, 0,
                                                        0,     0,     0x02U, 0,     0xA3U, 0x72U };
static const uint8_t VCD[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0x02U, 0,     0,
                                                    0,     0,     0,     0,     0x04U, 0xAAU };
static const uint8_t FAILED[PSCU_SUBQ_FRAME_BYTES] = { 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU,
                                                       0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU };

static pscu_calib_t calib_with(uint8_t trigger, bool frozen) {
  pscu_calib_t calib = { 0U, trigger, frozen, 0 };
  return calib;
}

// The indicator past its boot blink, so a fault or a result can take it at once.
static pscu_indicator_t waiting_indicator(void) {
  return pscu_indicator_step(pscu_indicator_init(1U, 0U, PSCU_INDICATOR_PROFILE_DEBUG),
                             PSCU_INDICATOR_EVENT_NONE,
                             0U,
                             700U)
      .state;
}

static pscu_loop_t fresh(pscu_calib_t calib) {
  return pscu_loop_init(calib, waiting_indicator(), false);
}

// One pass's decisions together with the state it leaves.
typedef struct {
  pscu_loop_t state;
  bool fire;
  bool store;
  bool trim_sample;
  bool quiet;
  bool indicator_on;
} step_t;

static step_t pass_with(
    pscu_loop_t state, const uint8_t *frame, bool captured, bool supply_ok, uint32_t ms) {
  pscu_loop_in_t in = { frame, captured, supply_ok, ms };
  pscu_loop_t next = state;
  pscu_loop_out_t out = pscu_loop_step(&next, &in);
  step_t step = { next, out.fire, out.store, out.trim_sample, out.quiet, out.indicator_on };
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
  pscu_loop_t state = pscu_loop_init(calib_with(12U, true), waiting_indicator(), true);
  g_check((state.calib.trigger == 12U) && state.vcd_filter,
          "loop: init takes the stored start and filter");
  g_check((state.counter == 0U) && !state.was_gone && (state.since.ms == 0U) &&
              !state.since.framed && !state.since.armed,
          "loop: init starts with an empty counter and no disc history");
  g_check((state.session.injects == 0U) && !state.session.resolved && (state.stealth.sent == 0U),
          "loop: init starts with no session and no strings");
}

// The window opens on the frame that brings the counter to the start point,
// not one frame before.
static void test_fire_at_trigger(void) {
  step_t idle = pass(fresh(calib_with(TRIGGER, false)), LEAD_OUT);
  g_check(idle.quiet, "loop: a counter far below the start leaves the window closed for stores");
  step_t before = feed(fresh(calib_with(TRIGGER, false)), LEAD_IN, TRIGGER - 1U);
  g_check(!before.fire && !before.quiet && (before.state.counter == TRIGGER - 1U),
          "loop: no string and no store one frame before the start point, the window held");
  step_t fired = pass(before.state, LEAD_IN);
  g_check(fired.fire && !fired.quiet && (fired.state.stealth.sent == 1U),
          "loop: the frame reaching the start point sends a string");
  g_check(
      (fired.state.session.injects == 1U) && (fired.state.indicator.stage == PSCU_INDICATOR_INJECT),
      "loop: a string opens the session and hands the indicator to injection");
  g_check(fired.state.since.armed && !fired.indicator_on, "loop: a string marks the disc armed");
  step_t dipped = pass(fired.state, LEAD_OUT);
  g_check(!dipped.fire && !dipped.quiet,
          "loop: a counter dipping just under the start keeps the window held, so no store");
}

// A low supply inside the window holds the burst and shows code 7; the next
// good frame sends the string the low one held back.
static void test_held_window(void) {
  step_t before = feed(fresh(calib_with(TRIGGER, false)), LEAD_IN, TRIGGER - 1U);
  step_t held = pass_with(before.state, LEAD_IN, true, false, FRAME_MS);
  g_check(!held.fire && !held.quiet && (held.state.stealth.sent == 0U),
          "loop: a low supply sends nothing inside the window");
  g_check((held.state.indicator.code == PSCU_INDICATOR_CODE_SUPPLY) && held.indicator_on,
          "loop: a low supply lights code 7");
  g_check(held.state.since.armed, "loop: a held window still counts as reached");
  step_t resumed = pass(held.state, LEAD_IN);
  g_check(resumed.fire, "loop: the string goes out once the supply is back");
}

// A supply dip longer than the confirmation span, after a string, pauses the
// wait: the session stays open, nothing is stored, and the program area that
// follows the supply's return still counts as acceptance.
static void test_held_session_waits(void) {
  step_t fired = feed(fresh(calib_with(TRIGGER, false)), LEAD_IN, TRIGGER);
  step_t held = pass_with(fired.state, LEAD_IN, true, false, FRAME_MS);
  bool stored = held.store;
  for (uint16_t i = 0U; i < (uint16_t)(PSCU_CONFIRM_FRAMES + 8U); i++) {
    held = pass_with(held.state, LEAD_IN, true, false, FRAME_MS);
    stored = stored || held.store;
  }
  g_check(!stored && !held.state.session.resolved && (held.state.calib.trigger == TRIGGER) &&
              !held.state.calib.frozen,
          "loop: a supply hold longer than the wait neither refuses nor learns");
  step_t back = pass(held.state, LEAD_IN);
  step_t accepted = pass(back.state, PROGRAM);
  g_check(accepted.store && (accepted.state.indicator.code == PSCU_INDICATOR_CODE_ACCEPTED),
          "loop: the program area after the supply returns is still an acceptance");
}

// A program-area frame after a string confirms the disc: code 1, a store, and
// the start probed one step later.
static void test_accepted(void) {
  step_t fired = feed(fresh(calib_with(TRIGGER, false)), LEAD_IN, TRIGGER);
  step_t accepted = pass(fired.state, PROGRAM);
  g_check(accepted.store && accepted.state.session.resolved,
          "loop: a program-area frame resolves the session and stores");
  g_check(accepted.state.calib.trigger == (uint8_t)(TRIGGER + PSCU_CALIB_TRIGGER_STEP),
          "loop: an accepted disc probes a later start");
  g_check(accepted.state.indicator.code == PSCU_INDICATOR_CODE_ACCEPTED,
          "loop: an accepted disc shows code 1");
  step_t gap = feed(fired.state, LEAD_IN, PSCU_STEALTH_GAP_FRAMES + 1U);
  g_check(gap.fire && (gap.state.session.injects == 2U),
          "loop: a second string joins the same session");
  step_t leaving = pass_with(fired.state, PROGRAM, false, true, PSCU_DISC_GONE_MS);
  g_check(leaving.state.session.resolved && !leaving.store,
          "loop: a program frame read as the disc leaves teaches nothing");
  step_t after = pass(accepted.state, PROGRAM);
  g_check(!after.store && (after.state.calib.trigger == accepted.state.calib.trigger),
          "loop: a resolved session learns once");

  // A program frame empties the counter and closes the window; a later lead-in
  // re-read, such as the anti-mod v2 ReadTOC, climbs from zero and is served.
  g_check(accepted.state.counter == 0U, "loop: a program-area frame empties the counter");
  step_t reread = feed(after.state, LEAD_IN, (uint16_t)(TRIGGER + PSCU_CALIB_TRIGGER_STEP));
  g_check(reread.fire, "loop: a lead-in re-read after play is served again");
}

// With no program area, the session resolves as refused after the bounded
// wait from the last string, not one frame sooner.
static void test_refused(void) {
  step_t fired = feed(fresh(calib_with(12U, false)), LEAD_IN, 12U);
  step_t waiting = feed(fired.state, LEAD_OUT, PSCU_CONFIRM_FRAMES - 1U);
  g_check(!waiting.store && !waiting.state.session.resolved,
          "loop: the session waits the whole confirmation span");
  step_t refused = pass(waiting.state, LEAD_OUT);
  g_check(refused.store && (refused.state.indicator.code == PSCU_INDICATOR_CODE_REFUSED),
          "loop: the wait running out stores a refusal and shows code 2");
  g_check((refused.state.calib.trigger == TRIGGER) && refused.state.calib.frozen,
          "loop: a refusal steps the start back");
}

// A disc leaving after a string resolves its session at once without teaching
// the calibration.
static void test_disc_gone(void) {
  step_t fired = feed(fresh(calib_with(TRIGGER, false)), LEAD_IN, TRIGGER);
  step_t gone = pass_with(fired.state, FAILED, false, true, PSCU_DISC_GONE_MS);
  g_check(!gone.store && gone.state.session.resolved &&
              (gone.state.indicator.code == PSCU_INDICATOR_CODE_REFUSED),
          "loop: a disc leaving shows its refusal and learns nothing");
  g_check((gone.state.counter == 0U) && (gone.state.stealth.sent == 0U) && gone.state.was_gone,
          "loop: a disc leaving empties the counter and the burst");
  g_check((gone.state.since.ms == 0U) && !gone.state.since.armed,
          "loop: a disc leaving starts the disc history over");
  step_t still = pass_with(gone.state, FAILED, false, true, FRAME_MS);
  g_check(still.state.indicator.code == PSCU_INDICATOR_CODE_REFUSED,
          "loop: the result stays while no disc is in");
  step_t next = pass(gone.state, LEAD_IN);
  g_check((next.state.indicator.stage == PSCU_INDICATOR_WAIT) && !next.state.was_gone,
          "loop: the next disc clears the last disc's result");
  step_t short_pause = pass_with(fired.state, FAILED, false, true, PSCU_DISC_GONE_MS - 1U);
  g_check(!short_pause.state.was_gone && !short_pause.state.session.resolved,
          "loop: a pause short of the bound keeps the disc and its session");
}

// The counter falling below the default start before the window opened is a
// miss: the start steps back and code 2 shows. A supply-held window is not.
static void test_missed(void) {
  step_t climbed = feed(fresh(calib_with(12U, false)), LEAD_IN, 11U);
  step_t at_default = pass(climbed.state, LEAD_OUT);
  g_check(!at_default.store, "loop: falling to the default start is not yet a miss");
  step_t missed = pass(at_default.state, LEAD_OUT);
  g_check(missed.store && (missed.state.calib.trigger == TRIGGER) && missed.state.calib.frozen,
          "loop: falling below the default start steps the start back");
  g_check(missed.state.indicator.code == PSCU_INDICATOR_CODE_REFUSED,
          "loop: a missed window shows code 2");

  step_t reached = feed(fresh(calib_with(12U, false)), LEAD_IN, 11U);
  step_t held = pass_with(reached.state, LEAD_IN, true, false, FRAME_MS);
  step_t decayed = feed(held.state, LEAD_OUT, 4U);
  g_check(!decayed.store && (decayed.state.calib.trigger == 12U),
          "loop: a window the supply held shut is not a miss");
}

// The trim times only consecutive, captured lead-in frames from a disc that has
// spun for a second.
static void test_trim_sample(void) {
  pscu_loop_t start = fresh(calib_with(TRIGGER, false));
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
  g_check(!pass_with(settled.state, LEAD_IN, true, false, FRAME_MS).trim_sample,
          "loop: a frame read while the supply check fails is no sample");

  // The string blocks the loop for 176 ms, so the pass that fires is no sample.
  step_t climbing = settled;
  for (uint8_t i = 0U; (i < (uint8_t)(TRIGGER + 2U)) && !climbing.fire; i++) {
    climbing = pass(climbing.state, LEAD_IN);
  }
  g_check(climbing.fire && !climbing.trim_sample,
          "loop: the pass that fires a string is no sample");
}

// Feed lead-in frames until count more strings have gone out.
static step_t fire_strings(step_t from, uint8_t count) {
  step_t out = from;
  uint8_t fired = 0U;
  for (uint16_t i = 0U; (i < 400U) && (fired < count); i++) {
    out = pass(out.state, LEAD_IN);
    fired = out.fire ? (uint8_t)(fired + 1U) : fired;
  }
  return out;
}

// Every arming may send the whole stealth cap, and not one string more.
static void test_whole_cap(void) {
  step_t start = { fresh(calib_with(TRIGGER, true)), false, false, false, false, false };
  step_t all = fire_strings(start, PSCU_STEALTH_STRINGS);
  g_check(all.fire && (all.state.stealth.sent == PSCU_STEALTH_STRINGS),
          "loop: a refused disc gets the whole stealth cap");
  step_t more = fire_strings(all, 1U);
  g_check(!more.fire && (more.state.stealth.sent == PSCU_STEALTH_STRINGS),
          "loop: and not one string more in that arming");
}

// The counter never climbs past the start plus the headroom, so after a swap
// the window closes within a few failed captures.
static void test_quick_swap(void) {
  step_t start = { fresh(calib_with(TRIGGER, true)), false, false, false, false, false };
  step_t spent = fire_strings(start, PSCU_STEALTH_STRINGS);
  step_t parked = feed(spent.state, LEAD_IN, 50U);
  g_check(parked.state.counter == (uint8_t)(TRIGGER + PSCU_LOOP_COUNTER_HEADROOM),
          "loop: the counter stops at the start plus the headroom");
  step_t opened = parked;
  uint8_t passes = 0U;
  while ((passes < 40U) && (opened.state.stealth.sent > 0U)) {
    opened = pass_with(opened.state, FAILED, false, true, 60U);
    passes = (uint8_t)(passes + 1U);
  }
  g_check(passes == (uint8_t)(PSCU_LOOP_COUNTER_HEADROOM + PSCU_LOOP_REARM_DEPTH),
          "loop: a refused disc re-arms within the headroom and the depth of failed captures");
  g_check(!opened.state.was_gone, "loop: that is well before the disc-gone bound");
  step_t next_disc = fire_strings(opened, 1U);
  g_check(next_disc.fire && (next_disc.state.stealth.sent == 1U),
          "loop: the next disc's lead-in gets a fresh burst");
}

// A counter dithering around the start on alternating misses and hits must
// still keep the stealth gap and stop at the cap. UberNee V1.42 and PsNee V9.0
// clear their counter after a string for the same reason.
static void test_dither_keeps_gap_and_cap(void) {
  step_t at = pass(feed(fresh(calib_with(TRIGGER, true)), LEAD_IN, TRIGGER - 1U).state, LEAD_IN);
  uint16_t fires = at.fire ? 1U : 0U;
  uint16_t last = 0U;
  bool spaced = true;
  for (uint16_t i = 1U; i <= 400U; i++) {
    bool miss = (i % 2U) == 1U;
    at = miss ? pass_with(at.state, FAILED, false, true, FRAME_MS) : pass(at.state, LEAD_IN);
    if (at.fire) {
      spaced = spaced && ((uint16_t)(i - last) > PSCU_STEALTH_GAP_FRAMES);
      last = i;
      fires = (uint16_t)(fires + 1U);
    }
  }
  g_check(spaced, "loop: a counter dithering at the start keeps the gap between strings");
  g_check(fires == PSCU_STEALTH_STRINGS, "loop: and stops at the cap, not one string more");
}

// A dip shallower than the re-arm depth holds the burst; the pass that reaches
// the depth ends it, so the next opening is a new arming with a fresh cap.
static void test_rearm_depth(void) {
  step_t start = { fresh(calib_with(TRIGGER, true)), false, false, false, false, false };
  step_t dipped = fire_strings(start, 3U);
  for (uint8_t i = 0U; (i < 40U) && (dipped.state.counter >= TRIGGER); i++) {
    dipped = pass_with(dipped.state, FAILED, false, true, FRAME_MS);
  }
  uint8_t held = dipped.state.stealth.sent;
  for (uint8_t i = 0U;
       (i < 40U) && (dipped.state.counter > (uint8_t)(TRIGGER - PSCU_LOOP_REARM_DEPTH + 1U));
       i++) {
    dipped = pass_with(dipped.state, FAILED, false, true, FRAME_MS);
  }
  g_check((held >= 3U) &&
              (dipped.state.counter == (uint8_t)(TRIGGER - PSCU_LOOP_REARM_DEPTH + 1U)) &&
              (dipped.state.stealth.sent == held),
          "loop: a dip short of the re-arm depth keeps the burst's count");
  step_t deep = pass_with(dipped.state, FAILED, false, true, FRAME_MS);
  g_check(deep.state.stealth.sent == 0U, "loop: the pass reaching the depth ends the burst");
}

// A bad frame decays the counter by one, so a lead-in with two good frames per
// bad one still arms and one with equal counts does not. UberNee V1.42 makes
// this threshold user-tunable (TWEAK_DRIVE).
static uint16_t fires_with_noise(uint8_t good_per_bad) {
  step_t at = { fresh(calib_with(TRIGGER, true)), false, false, false, false, false };
  uint16_t fires = 0U;
  for (uint16_t i = 0U; i < 300U; i++) {
    bool bad = (i % (uint16_t)(good_per_bad + 1U)) == good_per_bad;
    at = bad ? pass_with(at.state, FAILED, false, true, FRAME_MS) : pass(at.state, LEAD_IN);
    fires = at.fire ? (uint16_t)(fires + 1U) : fires;
  }
  return fires;
}

static void test_noisy_lead_in(void) {
  g_check(fires_with_noise(2U) > 0U, "loop: a lead-in with one bad frame in three still arms");
  g_check(fires_with_noise(1U) == 0U, "loop: one with a bad frame for every good one does not");
}

// The Video-CD filter is the state's, not the build's, so both rules run here.
static void test_vcd_filter(void) {
  pscu_loop_t filtered = pscu_loop_init(calib_with(TRIGGER, false), waiting_indicator(), true);
  g_check(pass(filtered, VCD).state.counter == 0U, "loop: the filter ignores a Video CD lead-in");
  g_check(pass(fresh(calib_with(TRIGGER, false)), VCD).state.counter == 1U,
          "loop: without the filter a Video CD lead-in counts");
}

// The disc history behind codes 3 and 4: time, frames and the window.
static void test_since(void) {
  pscu_loop_t start = fresh(calib_with(TRIGGER, false));
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
  pscu_loop_t seen = start;
  seen.since.ms = 0x200U;
  seen.since.framed = true;
  seen.since.armed = true;
  step_t gone = pass_with(seen, LEAD_OUT, false, true, PSCU_DISC_GONE_MS);
  g_check((gone.state.since.ms == 0U) && !gone.state.since.framed && !gone.state.since.armed,
          "loop: a disc leaving restarts its time and evidence");
  pscu_loop_t timed = start;
  timed.since.ms = 0x200U;
  g_check(pass_with(timed, LEAD_OUT, true, true, 0U).state.since.ms == 0x200U,
          "loop: a pass that took no time adds none");
}

static void test_faults(void) {
  pscu_loop_t start = fresh(calib_with(TRIGGER, false));
  step_t silent = pass_with(start, FAILED, false, true, PSCU_INDICATOR_NO_SQCK_MS);
  g_check(silent.state.indicator.code == PSCU_INDICATOR_CODE_NO_SQCK,
          "loop: no frame since power-on is code 3");
  step_t audio = pass_with(start, LEAD_OUT, true, true, PSCU_INDICATOR_NO_CHECK_MS);
  g_check(audio.state.indicator.code == PSCU_INDICATOR_CODE_NO_CHECK,
          "loop: frames but no region check is code 4");
}

void loop_tests(pscu_check_fn check) {
  g_check = check;
  test_init();
  test_fire_at_trigger();
  test_held_window();
  test_held_session_waits();
  test_accepted();
  test_refused();
  test_disc_gone();
  test_missed();
  test_trim_sample();
  test_whole_cap();
  test_quick_swap();
  test_dither_keeps_gap_and_cap();
  test_rearm_depth();
  test_noisy_lead_in();
  test_vcd_filter();
  test_since();
  test_faults();
}
