// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "pscu/board_mode.h"

#include <stdbool.h>

#include "pscu/assert.h"

// Pulse count saturates here, so a long burst cannot wrap below the threshold.
#define PSCU_BOARD_PULSES_MAX ((uint8_t)0xFFU)

// The edge gap is a header constant the mutation tool never edits, and the
// host test derives both sides of its boundary from the name, so its value is
// pinned here: 360 us at 5.25 us a sample is 68.6 samples, rounded up.
_Static_assert(PSCU_BOARD_EDGE_GAP_MAX == 69U, "360 us at 5.25 us a sample is 69 samples");

// Counts runs of closely spaced falling edges on WFCK: a static gate (PU-7 to
// PU-20) gives almost none, a 7.3 kHz carrier (PU-22 on) a long run.
pscu_board_detect_t pscu_board_detect_init(uint8_t needed) {
  // prev_high = 1 counts a first low sample as an edge; since starts full so
  // that edge opens a new run.
  pscu_board_detect_t state = { 0U, needed, (needed == 0U) ? 1U : 0U, PSCU_BOARD_PULSES_MAX, 1U };

  PSCU_ASSERT(state.pulses == 0U);
  PSCU_ASSERT(state.prev_high == 1U);

  return state;
}

pscu_board_detect_t pscu_board_detect_step(pscu_board_detect_t state, uint8_t wfck_sample) {
  PSCU_ASSERT(state.prev_high <= 1U);

  // The port read returns the masked pin bit, so any nonzero value is high.
  uint8_t low = (uint8_t)((wfck_sample == 0U) ? 1U : 0U);

  // Count falling edges only. An edge too long after the last starts a new run.
  if ((low == 1U) && (state.prev_high == 1U)) {
    bool joins = state.since <= PSCU_BOARD_EDGE_GAP_MAX;
    uint8_t run = (state.pulses < PSCU_BOARD_PULSES_MAX) ? (uint8_t)(state.pulses + 1U)
                                                         : PSCU_BOARD_PULSES_MAX;
    state.pulses = joins ? run : 1U;
    state.since = 0U;
  } else if (state.since < PSCU_BOARD_PULSES_MAX) {
    state.since = (uint8_t)(state.since + 1U);
  } else {
  }
  state.carrier = (state.pulses >= state.needed) ? 1U : state.carrier;
  state.prev_high = (uint8_t)(1U - low);

  PSCU_ASSERT(state.prev_high <= 1U);

  return state;
}

pscu_board_mode_t pscu_board_detect_mode(pscu_board_detect_t state) {
  pscu_board_mode_t mode = PSCU_BOARD_MODE_GATE;

  if (state.carrier != 0U) {
    mode = PSCU_BOARD_MODE_WFCK;
  } else {
    mode = PSCU_BOARD_MODE_GATE;
  }

  PSCU_ASSERT((mode == PSCU_BOARD_MODE_GATE) || (mode == PSCU_BOARD_MODE_WFCK));

  return mode;
}
