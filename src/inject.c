// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "pscu/inject.h"

#include "pscu/assert.h"

// The window is open once the leaky SUBQ counter has reached the trigger,
// meaning the console has settled into reading the lead-in and is about to
// perform the region check.
bool pscu_should_inject(uint8_t counter, uint8_t trigger) {
  PSCU_ASSERT(trigger > 0U);

  return counter >= trigger;
}

pscu_stealth_t pscu_stealth_init(void) {
  pscu_stealth_t state = { 0U, false, 0U, 0U };

  PSCU_ASSERT((state.sent == 0U) && !state.accepted);

  return state;
}

// Fold one capture into the acceptance latch. A program-area frame proves the
// console accepted the string, so it sets the latch and clears both runs. A
// lead-in frame shows the disc is spinning and readable, so it clears the lost
// run and adds to the lead-in run. Anything else adds to the lost run, a silent
// capture by its measured weight. Either run reaching its threshold means a new
// disc needs a check: the drive stopped, or the console is stuck reading the
// lead-in. The latch and both runs then clear, so the chip injects for that disc
// as it did for the first. Because each run clears at its threshold, it enters
// a step below it, and below + one step still fits a byte (149 + 17 and 224 + 1),
// so neither sum can wrap and no saturation is needed. The compiler re-checks
// that bound whenever a threshold or the silent weight changes.
_Static_assert(((uint16_t)PSCU_REARM_LOST_UNITS + PSCU_SILENT_CAPTURE_UNITS) <= 0x100U,
               "the lost run must not wrap a byte before it releases");
_Static_assert((uint16_t)PSCU_REARM_LEAD_IN_FRAMES <= 0xFFU,
               "the lead-in run must release before it wraps a byte");

static pscu_stealth_t pscu_stealth_observe(pscu_stealth_t state, pscu_frame_kind_t kind) {
  PSCU_ASSERT(state.lost < PSCU_REARM_LOST_UNITS);
  PSCU_ASSERT(state.lead_in < PSCU_REARM_LEAD_IN_FRAMES);

  pscu_stealth_t next = state;

  if (kind == PSCU_FRAME_PROGRAM) {
    next.accepted = true;
    next.lost = 0U;
    next.lead_in = 0U;
  } else if (kind == PSCU_FRAME_LEAD_IN) {
    next.lost = 0U;
    next.lead_in = (uint8_t)(state.lead_in + 1U);
  } else {
    uint8_t weight = (kind == PSCU_FRAME_SILENT) ? PSCU_SILENT_CAPTURE_UNITS : 1U;
    next.lost = (uint8_t)(state.lost + weight);
  }
  if ((next.lost >= PSCU_REARM_LOST_UNITS) || (next.lead_in >= PSCU_REARM_LEAD_IN_FRAMES)) {
    next.accepted = false;
    next.lost = 0U;
    next.lead_in = 0U;
  }

  return next;
}

pscu_stealth_step_t pscu_stealth_step(pscu_stealth_t state,
                                      bool in_window,
                                      pscu_frame_kind_t kind,
                                      uint8_t max_strings) {
  pscu_stealth_step_t out;
  out.state = pscu_stealth_observe(state, kind);
  out.fire = false;

  // Out of the window the chip stays silent and the counter resets, which is
  // both the stealth property during play and the re-arm for the next disc.
  // Inside the window it emits until the safety cap, then goes quiet even if
  // the window lingers, so it never drives the bus without bound. Once the
  // console has accepted the string it emits nothing at all, window or not.
  if (!in_window) {
    out.state.sent = 0U;
  } else if (out.state.accepted) {
  } else if (state.sent < max_strings) {
    out.state.sent = (uint8_t)(state.sent + 1U);
    out.fire = true;
  } else {
  }

  PSCU_ASSERT(out.state.sent <= max_strings);

  return out;
}

pscu_confirm_t pscu_confirm_init(void) {
  pscu_confirm_t state = { 0U, false };

  PSCU_ASSERT(!state.program_seen);

  return state;
}

// Resolve as confirmed the moment the program area appears; otherwise let a
// bounded idle wait expire and resolve as unconfirmed. Keeping this pure means
// the run loop just records whatever verdict the frames produced, with no timing
// logic of its own.
pscu_confirm_step_t pscu_confirm_step(pscu_confirm_t state,
                                      bool idle,
                                      bool program,
                                      uint8_t timeout) {
  pscu_confirm_step_t out;
  out.state = state;
  out.resolved = false;
  out.confirmed = false;

  if (program) {
    out.state.program_seen = true;
  }
  if (idle && (out.state.waited < 0xFFU)) {
    out.state.waited = (uint8_t)(out.state.waited + 1U);
  }
  if (out.state.program_seen) {
    out.resolved = true;
    out.confirmed = true;
  } else if (out.state.waited >= timeout) {
    out.resolved = true;
  } else {
  }

  return out;
}
