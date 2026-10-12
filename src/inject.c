// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "pscu/inject.h"

#include "pscu/assert.h"

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

  // Leaving the window resets the state, so a later lead-in read, such as the
  // anti-mod v2 ReadTOC re-read, gets a fresh burst (tonyhax docs/ap_v2.c).
  if (disc_gone) {
    out.state = pscu_stealth_init();
  } else {
    if (program) {
      // coverage: unreachable: pscu_loop_read empties the counter on a
      // program-area frame, so the window is always closed here.
      out.state.accepted = true;
    }
    if (window == PSCU_WINDOW_CLOSED) {
      out.state.sent = 0U;
      out.state.wait = 0U;
      out.state.accepted = false;
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
  // A string restarts the wait, so it times only from the last string.
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
  // coverage: unreachable: saturating needs 49.7 days of quiet.
  next.quiet_ms = valid ? 0U : ((sum < state.quiet_ms) ? 0xFFFFFFFFUL : sum);
  next.seen = state.seen || valid;
  return next;
}

bool pscu_presence_gone(pscu_presence_t state) {
  return state.quiet_ms >= PSCU_DISC_GONE_MS;
}
