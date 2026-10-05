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
  pscu_stealth_t state = { 0U };

  PSCU_ASSERT(state.sent == 0U);

  return state;
}

pscu_stealth_step_t pscu_stealth_step(pscu_stealth_t state, bool in_window, uint8_t max_strings) {
  pscu_stealth_step_t out = { state, false };

  // Out of the window the chip stays silent and the counter resets, which is
  // both the stealth property during play and the re-arm for the next disc.
  // Inside the window it emits until the safety cap, then goes quiet even if
  // the window lingers, so it never drives the bus without bound.
  if (!in_window) {
    out.state.sent = 0U;
  } else if (state.sent < max_strings) {
    out.state.sent = (uint8_t)(state.sent + 1U);
    out.fire = true;
  } else {
  }

  PSCU_ASSERT(out.state.sent <= max_strings);

  return out;
}
