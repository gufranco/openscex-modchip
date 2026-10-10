// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "pscu/board_mode.h"

#include <stdbool.h>

#include "pscu/assert.h"

// Pulse count saturates here rather than wrapping, so a long WFCK burst cannot
// roll the counter back down past the detection threshold.
#define PSCU_BOARD_PULSES_MAX ((uint8_t)0xFFU)

// Board detection distinguishes two eras by how the former gate pin behaves:
// on PU-7..PU-20 it is a static high gate, on PU-22+ it is a live ~7.3 kHz
// WFCK clock. The detector counts high-to-low edges on that pin, in runs of
// edges that follow each other closely: a static gate produces almost none, a
// live clock a long unbroken run.
pscu_board_detect_t pscu_board_detect_init(uint8_t needed) {
  // prev_high starts at 1 so the very first low sample is counted as an edge,
  // and since starts full, as after a long silence, so that edge opens a run
  // of one like any edge with no close predecessor. A zero run is already met,
  // which forces the carrier mode.
  pscu_board_detect_t state = { 0U, needed, (needed == 0U) ? 1U : 0U, PSCU_BOARD_PULSES_MAX, 1U };

  PSCU_ASSERT(state.pulses == 0U);
  PSCU_ASSERT(state.prev_high == 1U);

  return state;
}

pscu_board_detect_t pscu_board_detect_step(pscu_board_detect_t state, uint8_t wfck_sample) {
  PSCU_ASSERT(state.prev_high <= 1U);

  // Any nonzero reading is "high": the port read returns the masked pin bit,
  // not a clean 0/1, so only equality with zero is meaningful.
  uint8_t low = (uint8_t)((wfck_sample == 0U) ? 1U : 0U);

  // Count only the high-to-low transition, not every low sample, so one pulse
  // of the clock is counted once regardless of how long it stays low. An edge
  // that comes too long after the last one starts a new run of one: a clock
  // keeps its edges close, a noisy or floating line does not.
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

  // A run that reached the needed length means a live clock (modern board);
  // none means a static gate (legacy board).
  if (state.carrier != 0U) {
    mode = PSCU_BOARD_MODE_WFCK;
  } else {
    mode = PSCU_BOARD_MODE_GATE;
  }

  PSCU_ASSERT((mode == PSCU_BOARD_MODE_GATE) || (mode == PSCU_BOARD_MODE_WFCK));

  return mode;
}
