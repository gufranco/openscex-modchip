// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "pscu/loop.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pscu/assert.h"
#include "pscu/calib.h"
#include "pscu/config.h"
#include "pscu/indicator.h"
#include "pscu/inject.h"
#include "pscu/run.h"
#include "pscu/subq.h"

// What one frame says, worked out once for every later stage of the pass.
typedef struct {
  bool disc_gone;
  bool valid;
  uint8_t previous;
  bool reached;
  bool program;
  bool fire;
  pscu_window_t window;
} pscu_loop_pass_t;

// What the session and the calibration made of the pass.
typedef struct {
  pscu_indicator_event_t event;
  bool missed;
  bool store;
} pscu_loop_learned_t;

// The stages update the loop state through a pointer: by-value copies of it do
// not fit the 8 KB flash. The step is still deterministic.

static pscu_session_t pscu_loop_session_init(void) {
  pscu_session_t session = { 0U, false, pscu_confirm_init() };
  return session;
}

pscu_loop_t pscu_loop_init(pscu_calib_t calib, pscu_indicator_t indicator, bool vcd_filter) {
  pscu_loop_t state;
  state.calib = calib;
  state.counter = 0U;
  state.was_gone = false;
  state.vcd_filter = vcd_filter;
  state.presence = pscu_presence_init();
  state.since.ms = 0U;
  state.since.framed = false;
  state.since.armed = false;
  state.stealth = pscu_stealth_init();
  state.session = pscu_loop_session_init();
  state.indicator = indicator;
  return state;
}

// Count one string. The first string of an arming starts a new session, so
// each disc is judged on its own.
static pscu_session_t pscu_loop_session_fired(pscu_session_t session, uint8_t sent) {
  pscu_session_t next = (sent == 1U) ? pscu_loop_session_init() : session;
  PSCU_ASSERT(next.injects < PSCU_STEALTH_STRINGS);
  next.injects = (uint8_t)(next.injects + 1U);
  return next;
}

// Update presence and the counter. A gone disc or a program-area frame empties
// the counter, closing the window at once; a later lead-in read climbs from
// zero again.
static void pscu_loop_read(pscu_loop_t *next, const pscu_loop_in_t *in, pscu_loop_pass_t *pass) {
  pass->valid = in->captured && pscu_subq_is_valid(in->frame);
  next->presence = pscu_presence_step(next->presence, pass->valid, in->elapsed_ms);
  pass->disc_gone = pscu_presence_gone(next->presence);
  pass->program = pscu_subq_is_program_area(in->frame);
  pass->previous = next->counter;
  bool empties = pass->disc_gone || pass->program;
  uint8_t ceiling = (uint8_t)(next->calib.trigger + PSCU_LOOP_COUNTER_HEADROOM);
  uint8_t counted = pscu_subq_update_counter(in->frame, next->counter, next->vcd_filter, ceiling);
  next->counter = empties ? 0U : counted;
}

// Decide the string. Open at or above the learned start, held under the supply
// guard or just below the start, closed PSCU_LOOP_REARM_DEPTH below it.
static void pscu_loop_burst(pscu_loop_t *next, bool supply_ok, pscu_loop_pass_t *pass) {
  pass->reached = pscu_should_inject(next->counter, next->calib.trigger);
  // Widened before the sum so a counter near 255 cannot wrap past the start.
  uint16_t depth_above = (uint16_t)((uint16_t)next->counter + PSCU_LOOP_REARM_DEPTH);
  bool rearm = depth_above <= next->calib.trigger;
  pscu_window_t below = rearm ? PSCU_WINDOW_CLOSED : PSCU_WINDOW_HELD;
  pscu_window_t open = supply_ok ? PSCU_WINDOW_OPEN : PSCU_WINDOW_HELD;
  pscu_window_t window = pass->reached ? open : below;
  pass->window = window;
  pscu_stealth_step_t step = pscu_stealth_step(next->stealth,
                                               window,
                                               pass->program,
                                               pass->disc_gone,
                                               PSCU_STEALTH_STRINGS,
                                               PSCU_STEALTH_GAP_FRAMES);
  next->stealth = step.state;
  pass->fire = step.fire;
  if (step.fire) {
    next->session = pscu_loop_session_fired(next->session, step.state.sent);
  }
}

// After injecting, a program-area frame confirms acceptance. A timeout or the
// disc leaving resolves as refused. Only a session that resolved on its own
// teaches the calibration.
static pscu_loop_learned_t pscu_loop_watch(pscu_loop_t *next, const pscu_loop_pass_t *pass) {
  pscu_loop_learned_t out;
  out.event = pass->fire ? PSCU_INDICATOR_EVENT_FIRED : PSCU_INDICATOR_EVENT_NONE;
  out.store = false;
  out.missed = false;
  if ((next->session.injects > 0U) && !next->session.resolved) {
    pscu_confirm_step_t outcome =
        pscu_confirm_step(next->session.confirm, !pass->fire, pass->program, PSCU_CONFIRM_FRAMES);
    next->session.confirm = outcome.state;
    if (outcome.resolved || pass->disc_gone) {
      next->session.resolved = true;
      out.event = outcome.confirmed ? PSCU_INDICATOR_EVENT_ACCEPTED : PSCU_INDICATOR_EVENT_REFUSED;
      out.store = !pass->disc_gone;
    }
  }
  return out;
}

// A resolved session or a missed window teaches the start. Both come after the
// strings stop, so the store never lands inside the window.
static pscu_loop_learned_t pscu_loop_learn(pscu_loop_t *next, const pscu_loop_pass_t *pass) {
  pscu_loop_learned_t out = pscu_loop_watch(next, pass);
  bool confirmed = out.event == PSCU_INDICATOR_EVENT_ACCEPTED;
  pscu_calib_outcome_t outcome = confirmed ? PSCU_CALIB_ACCEPTED : PSCU_CALIB_REFUSED;
  out.missed = !pass->disc_gone &&
               pscu_calib_missed(next->calib, pass->previous, next->counter, next->since.armed);
  // One call site, so the image holds one copy of the learning.
  if (out.store || out.missed) {
    next->calib = pscu_calib_learn(next->calib, out.store ? outcome : PSCU_CALIB_MISSED);
    out.store = true;
  }
  return out;
}

// A gone disc starts over; otherwise accumulate time (saturating) and evidence.
static pscu_since_disc_t pscu_loop_since(pscu_since_disc_t since,
                                         const pscu_loop_pass_t *pass,
                                         uint32_t elapsed_ms) {
  pscu_since_disc_t next = { 0U, false, false };
  if (!pass->disc_gone) {
    uint32_t sum = since.ms + elapsed_ms;
    // coverage: unreachable: saturating needs one disc to stay 49.7 days.
    next.ms = (sum < since.ms) ? 0xFFFFFFFFUL : sum;
    next.framed = since.framed || pass->valid;
    next.armed = since.armed || pass->reached || pass->program;
  }
  return next;
}

// Draw this pass's indicator state. A missed window shows as a refusal.
static bool pscu_loop_show(pscu_loop_t *next,
                           const pscu_loop_in_t *in,
                           pscu_loop_learned_t learned,
                           const pscu_loop_pass_t *pass) {
  next->since = pscu_loop_since(next->since, pass, in->elapsed_ms);
  uint8_t fault = pscu_indicator_fault(!in->supply_ok,
                                       next->presence.seen,
                                       next->presence.quiet_ms,
                                       next->since.ms,
                                       next->since.framed,
                                       next->since.armed);
  bool arrived = next->was_gone && !pass->disc_gone;
  pscu_indicator_t indicator =
      arrived ? pscu_indicator_disc_arrived(next->indicator) : next->indicator;
  bool none = learned.event == PSCU_INDICATOR_EVENT_NONE;
  pscu_indicator_event_t shown =
      (none && learned.missed) ? PSCU_INDICATOR_EVENT_REFUSED : learned.event;
  pscu_indicator_step_t step = pscu_indicator_step(indicator, shown, fault, in->elapsed_ms);
  next->indicator = step.state;
  next->was_gone = pass->disc_gone;
  return step.on;
}

pscu_loop_out_t pscu_loop_step(pscu_loop_t *state, const pscu_loop_in_t *in) {
  PSCU_ASSERT((state != NULL) && (in != NULL) && (in->frame != NULL));

  pscu_loop_pass_t pass = { false, false, 0U, false, false, false, PSCU_WINDOW_CLOSED };
  pscu_loop_out_t out;
  bool settled = state->since.ms >= PSCU_LOOP_TRIM_SETTLE_MS;
  pscu_loop_read(state, in, &pass);
  // Any valid frame times the trim, but not under a failing supply: the RC
  // frequency follows VCC (datasheet 2586Q).
  bool timed = settled && pass.valid && in->supply_ok;
  pscu_loop_burst(state, in->supply_ok, &pass);
  pscu_loop_learned_t learned = pscu_loop_learn(state, &pass);
  out.indicator_on = pscu_loop_show(state, in, learned, &pass);
  // A pass that fires blocks for the whole string, so it times nothing.
  out.trim_sample = timed && !pass.fire;
  out.fire = pass.fire;
  out.store = learned.store;
  out.quiet = pass.window == PSCU_WINDOW_CLOSED;
  return out;
}
