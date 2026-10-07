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
                                      bool in_window,
                                      bool program,
                                      bool lid_open,
                                      uint8_t max_strings,
                                      uint8_t gap) {
  pscu_stealth_step_t out;
  out.state = state;
  out.fire = false;

  // An open lid means the disc is leaving: emit nothing and drop both the count
  // and the acceptance, so the close re-arms the chip for the next disc. With
  // the lid closed, a program-area frame proves the console accepted the
  // string. Out of the window the count resets; inside it the chip emits until
  // the safety cap, and not at all once accepted, so it never drives the bus
  // without bound and never after the console has what it needs. Between two
  // strings it lets gap frames pass, as PsNee and Mayumi V4 do.
  if (lid_open) {
    out.state = pscu_stealth_init();
  } else {
    if (program) {
      out.state.accepted = true;
    }
    if (!in_window) {
      out.state.sent = 0U;
      out.state.wait = 0U;
    } else if (out.state.accepted) {
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
