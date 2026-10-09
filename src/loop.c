// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "pscu/loop.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pscu/assert.h"
#include "pscu/calib.h"
#include "pscu/config.h"
#include "pscu/inject.h"
#include "pscu/led.h"
#include "pscu/run.h"
#include "pscu/subq.h"

// What one frame says, worked out once and read by every later stage of the
// pass: whether the disc is there, where the counter was, whether the window is
// reached, whether the frame is from the program area, and what the stealth
// state machine decided.
typedef struct {
  bool disc_gone;
  bool valid;
  uint8_t previous;
  bool reached;
  bool program;
  bool fire;
} pscu_loop_pass_t;

// What the session and the calibration made of the pass: the LED event, whether
// the window was missed, and whether the calibration must be stored.
typedef struct {
  pscu_led_event_t event;
  bool missed;
  bool store;
} pscu_loop_learned_t;

// The stages below all update the one loop state the run loop owns, reached
// through a pointer, rather than taking and returning it by value. On the
// ATtiny85 every by-value pass of this structure is a block copy: taking and
// returning it at each stage put the image 722 bytes over the 8192-byte flash,
// and returning a new state only from the step still cost 210 bytes more than
// updating it in place (Verified: avr-size of the linked image, 7866 against
// 7656 bytes). The step is still a function of its inputs: the same state and
// pass always give the same new state and decisions.

static pscu_session_t pscu_loop_session_init(void) {
  pscu_session_t session = { 0U, false, pscu_confirm_init() };
  return session;
}

pscu_loop_t pscu_loop_init(pscu_calib_t calib, pscu_led_t led, bool vcd_filter) {
  pscu_loop_t state;
  state.calib = calib;
  state.cap = calib.cap;
  state.counter = 0U;
  state.was_gone = false;
  state.vcd_filter = vcd_filter;
  state.presence = pscu_presence_init();
  state.since.ms = 0U;
  state.since.framed = false;
  state.since.armed = false;
  state.stealth = pscu_stealth_init();
  state.session = pscu_loop_session_init();
  state.led = led;
  return state;
}

// Count one emitted string. The first string of an arming starts a new session:
// a disc swap re-arms the stealth state machine, and that disc's check is judged
// on its own rather than lost behind the first session of the power cycle. A
// session never outlives its arming and an arming sends at most the stealth
// cap, so the count stays within it.
static pscu_session_t pscu_loop_session_fired(pscu_session_t session, uint8_t sent) {
  pscu_session_t next = (sent == 1U) ? pscu_loop_session_init() : session;
  PSCU_ASSERT(next.injects < PSCU_STEALTH_STRINGS);
  next.injects = (uint8_t)(next.injects + 1U);
  return next;
}

// Read the frame: whether the disc is there, and the leaky counter. A gone disc
// also empties the counter, so the next disc's spin-up cannot find the window
// already open and spend strings before its lead-in. So does a program-area
// frame: the drive reads there only once it has accepted, and a counter left
// saturated by a long lead-in would otherwise take some 245 frames, 3.3 s, to
// close the window. Emptied, the window closes at once, and a later lead-in
// read, such as the anti-mod v2 ReadTOC re-read that needs a fresh string on a
// copy (tonyhax docs/ap_v2.c), must climb from zero to the start again, as
// PsNee's counter reset after each string makes it do (PSNee.ino:736).
static void pscu_loop_read(pscu_loop_t *next, const pscu_loop_in_t *in, pscu_loop_pass_t *pass) {
  pass->valid = in->captured && pscu_subq_is_valid(in->frame);
  next->presence = pscu_presence_step(next->presence, pass->valid, in->elapsed_ms);
  pass->disc_gone = pscu_presence_gone(next->presence);
  pass->program = pscu_subq_is_program_area(in->frame);
  pass->previous = next->counter;
  bool empties = pass->disc_gone || pass->program;
  next->counter =
      empties ? 0U : pscu_subq_update_counter(in->frame, next->counter, next->vcd_filter);
}

// Decide the string. The window is closed below the learned start, open above
// it, and held while the supply guard keeps strings back; a held burst keeps its
// count and gap. The cap is taken when an arming starts, so a value learned
// mid-window, such as the full cap after a refusal, applies from the next disc.
static void pscu_loop_burst(pscu_loop_t *next, bool supply_ok, pscu_loop_pass_t *pass) {
  pass->reached = pscu_should_inject(next->counter, next->calib.trigger);
  pscu_window_t open = supply_ok ? PSCU_WINDOW_OPEN : PSCU_WINDOW_HELD;
  pscu_window_t window = pass->reached ? open : PSCU_WINDOW_CLOSED;
  next->cap = (next->stealth.sent == 0U) ? next->calib.cap : next->cap;
  pscu_stealth_step_t step = pscu_stealth_step(
      next->stealth, window, pass->program, pass->disc_gone, next->cap, PSCU_STEALTH_GAP_FRAMES);
  next->stealth = step.state;
  pass->fire = step.fire;
  if (step.fire) {
    next->session = pscu_loop_session_fired(next->session, step.state.sent);
  }
}

// Closed-loop confirmation. After injecting, watch for the program area: the
// mechacon re-enables reads only once it accepts the region string, so a
// program-area frame confirms the check passed and the LED shows code 1. The FSM
// otherwise resolves after a bounded idle wait, and a disc that leaves resolves
// it at once, both as refused, code 2, so a failed disc is reported before the
// next disc's first string starts a new session. Only a session that resolved
// on its own teaches the calibration; one ended by the disc leaving does not.
static pscu_loop_learned_t pscu_loop_watch(pscu_loop_t *next, const pscu_loop_pass_t *pass) {
  pscu_loop_learned_t out;
  out.event = pass->fire ? PSCU_LED_EVENT_FIRED : PSCU_LED_EVENT_NONE;
  out.store = false;
  out.missed = false;
  if ((next->session.injects > 0U) && !next->session.resolved) {
    pscu_confirm_step_t outcome =
        pscu_confirm_step(next->session.confirm, !pass->fire, pass->program, PSCU_CONFIRM_FRAMES);
    next->session.confirm = outcome.state;
    if (outcome.resolved || pass->disc_gone) {
      next->session.resolved = true;
      out.event = outcome.confirmed ? PSCU_LED_EVENT_ACCEPTED : PSCU_LED_EVENT_REFUSED;
      out.store = outcome.resolved && !pass->disc_gone;
    }
  }
  return out;
}

// Fold this pass's evidence into the calibration. A session that resolved on
// its own teaches the cap and the start; a missed window, the counter falling
// back below the default start while nothing reached the window, teaches the
// start. Both come after the strings have stopped, so the store the run loop
// makes never lands inside the injection window.
static pscu_loop_learned_t pscu_loop_learn(pscu_loop_t *next, const pscu_loop_pass_t *pass) {
  pscu_loop_learned_t out = pscu_loop_watch(next, pass);
  bool confirmed = out.event == PSCU_LED_EVENT_ACCEPTED;
  pscu_calib_outcome_t outcome = confirmed ? PSCU_CALIB_ACCEPTED : PSCU_CALIB_REFUSED;
  out.missed = !pass->disc_gone &&
               pscu_calib_missed(next->calib, pass->previous, next->counter, next->since.armed);
  if (out.store) {
    next->calib = pscu_calib_learn(next->calib, outcome, next->session.injects);
  } else if (out.missed) {
    next->calib = pscu_calib_learn(next->calib, PSCU_CALIB_MISSED, 0U);
    out.store = true;
  } else {
  }
  return out;
}

// A gone disc starts the count over; a present one accumulates time, whether any
// valid frame arrived, and whether the window was reached or the console accepted.
// The time saturates: unsigned addition wraps, so a sum below the old time means
// it overflowed, and a pass that took no time leaves the sum equal, never pinned.
static pscu_since_disc_t pscu_loop_since(pscu_since_disc_t since,
                                         const pscu_loop_pass_t *pass,
                                         uint32_t elapsed_ms) {
  pscu_since_disc_t next = { 0U, false, false };
  if (!pass->disc_gone) {
    uint32_t sum = since.ms + elapsed_ms;
    // coverage: unreachable: the saturated branch needs one disc to stay 2^32
    // ms, 49.7 days; the console bench runs minutes.
    next.ms = (sum < since.ms) ? 0xFFFFFFFFUL : sum;
    next.framed = since.framed || pass->valid;
    next.armed = since.armed || pass->reached || pass->program;
  }
  return next;
}

// Draw this pass's LED state from the same facts, without ever waiting. A
// missed window reads as a refusal: the disc got no string, and the next one
// will. A disc arriving ends the last disc's result, so its own shows cleanly.
static bool pscu_loop_show(pscu_loop_t *next,
                           const pscu_loop_in_t *in,
                           pscu_loop_learned_t learned,
                           const pscu_loop_pass_t *pass) {
  next->since = pscu_loop_since(next->since, pass, in->elapsed_ms);
  uint8_t fault = pscu_led_fault(!in->supply_ok,
                                 next->presence.seen,
                                 next->presence.quiet_ms,
                                 next->since.ms,
                                 next->since.framed,
                                 next->since.armed);
  bool arrived = next->was_gone && !pass->disc_gone;
  pscu_led_t led = arrived ? pscu_led_disc_arrived(next->led) : next->led;
  bool none = learned.event == PSCU_LED_EVENT_NONE;
  pscu_led_event_t shown = (none && learned.missed) ? PSCU_LED_EVENT_REFUSED : learned.event;
  pscu_led_step_t step = pscu_led_step(led, shown, fault, in->elapsed_ms);
  next->led = step.state;
  next->was_gone = pass->disc_gone;
  return step.on;
}

pscu_loop_out_t pscu_loop_step(pscu_loop_t *state, const pscu_loop_in_t *in) {
  PSCU_ASSERT((state != NULL) && (in != NULL) && (in->frame != NULL));

  pscu_loop_pass_t pass = { false, false, 0U, false, false, false };
  pscu_loop_out_t out;
  bool settled = state->since.ms >= PSCU_LOOP_TRIM_SETTLE_MS;
  pscu_loop_read(state, in, &pass);
  // Any valid frame times the trim, the lead-in and the program area alike: a
  // frame at single speed is 13.3 ms apart wherever the head is, so play keeps
  // the trim following the RC as the chip warms. Double-speed frames, 6.7 ms
  // apart, fall outside the trim's accept window and count for nothing.
  bool timed = settled && pass.valid;
  pscu_loop_burst(state, in->supply_ok, &pass);
  pscu_loop_learned_t learned = pscu_loop_learn(state, &pass);
  out.led_on = pscu_loop_show(state, in, learned, &pass);
  // A pass that fires blocks the loop for the whole string, so the next frame's
  // Timer1 stamp would time the string's tail, not a frame period, and bias
  // the trim; it is no sample, which also leaves the next pass unpaired.
  out.trim_sample = timed && !pass.fire;
  out.fire = pass.fire;
  out.store = learned.store;
  out.quiet = !pass.reached;
  return out;
}
