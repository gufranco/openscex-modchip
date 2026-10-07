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
  pscu_stealth_t state = { 0U, false, 0U };

  PSCU_ASSERT((state.sent == 0U) && !state.accepted && (state.wait == 0U));

  return state;
}

pscu_stealth_step_t pscu_stealth_step(pscu_stealth_t state,
                                      pscu_window_t window,
                                      bool program,
                                      bool disc_gone,
                                      uint8_t max_strings,
                                      uint8_t gap) {
  PSCU_ASSERT((window == PSCU_WINDOW_CLOSED) || (window == PSCU_WINDOW_OPEN) ||
              (window == PSCU_WINDOW_HELD));

  pscu_stealth_step_t out;
  out.state = state;
  out.fire = false;

  // A gone disc: emit nothing and drop both the count and the acceptance, so the
  // next disc finds the chip re-armed. With a disc present, a program-area frame
  // proves the console accepted the
  // string. Out of the window the count resets; inside it the chip emits until
  // the safety cap, and not at all once accepted, so it never drives the bus
  // without bound and never after the console has what it needs. Between two
  // strings it lets gap frames pass, as PsNee and Mayumi V4 do. A held window
  // freezes the burst where it stands, gap included.
  if (disc_gone) {
    out.state = pscu_stealth_init();
  } else {
    if (program) {
      out.state.accepted = true;
    }
    if (window == PSCU_WINDOW_CLOSED) {
      out.state.sent = 0U;
      out.state.wait = 0U;
    } else if (out.state.accepted || (window == PSCU_WINDOW_HELD)) {
    } else if (state.wait > 0U) {
      out.state.wait = (uint8_t)(state.wait - 1U);
    } else if (state.sent < max_strings) {
      out.state.sent = (uint8_t)(state.sent + 1U);
      out.state.wait = gap;
      out.fire = true;
    } else {
    }
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
  // A string restarts the wait, so the bounded wait runs only after the last
  // string of a burst: with strings spaced by the stealth gap, counting the gap
  // frames would resolve a session as refused while strings were still going
  // out, and a late acceptance would then be lost.
  if (!idle) {
    out.state.waited = 0U;
  } else if (out.state.waited < 0xFFU) {
    out.state.waited = (uint8_t)(out.state.waited + 1U);
  } else {
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

pscu_presence_t pscu_presence_init(void) {
  pscu_presence_t state = { 0U, false };
  return state;
}

pscu_presence_t pscu_presence_step(pscu_presence_t state, bool valid, uint32_t elapsed_ms) {
  pscu_presence_t next = state;
  uint32_t sum = state.quiet_ms + elapsed_ms;
  next.quiet_ms = valid ? 0U : ((sum < state.quiet_ms) ? 0xFFFFFFFFUL : sum);
  next.seen = state.seen || valid;
  return next;
}

bool pscu_presence_gone(pscu_presence_t state) {
  return state.quiet_ms >= PSCU_DISC_GONE_MS;
}
