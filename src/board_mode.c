// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "pscu/board_mode.h"

#include "pscu/assert.h"

// Pulse count saturates here rather than wrapping, so a long WFCK burst cannot
// roll the counter back down past the detection threshold.
#define PSCU_BOARD_PULSES_MAX ((uint8_t)0xFFU)

// Board detection distinguishes two eras by how the former gate pin behaves:
// on PU-7..PU-20 it is a static high gate, on PU-22+ it is a live ~7.3 kHz
// WFCK clock. The detector counts high-to-low edges on that pin: a static gate
// produces almost none, a live clock produces many.
pscu_board_detect_t pscu_board_detect_init(void) {
  // prev_high starts at 1 so the very first low sample is counted as an edge.
  pscu_board_detect_t state = {0U, 1U};

  PSCU_ASSERT(state.pulses == 0U);
  PSCU_ASSERT(state.prev_high == 1U);

  return state;
}

pscu_board_detect_t pscu_board_detect_step(pscu_board_detect_t state,
                                           uint8_t wfck_sample) {
  PSCU_ASSERT(state.prev_high <= 1U);

  // Any nonzero reading is "high": the port read returns the masked pin bit,
  // not a clean 0/1, so only equality with zero is meaningful.
  uint8_t low = (uint8_t)((wfck_sample == 0U) ? 1U : 0U);

  // Count only the high-to-low transition, not every low sample, so one pulse
  // of the clock is counted once regardless of how long it stays low.
  if ((low == 1U) && (state.prev_high == 1U) &&
      (state.pulses < PSCU_BOARD_PULSES_MAX)) {
    state.pulses = (uint8_t)(state.pulses + 1U);
  }
  state.prev_high = (uint8_t)(1U - low);

  PSCU_ASSERT(state.prev_high <= 1U);

  return state;
}

pscu_board_mode_t pscu_board_detect_mode(pscu_board_detect_t state,
                                         uint8_t low_pulses_needed) {
  pscu_board_mode_t mode = PSCU_BOARD_MODE_GATE;

  // A zero threshold forces WFCK mode, which the simulation uses to exercise the
  // modern path deterministically. Otherwise enough counted pulses mean a live
  // clock (modern board); too few mean a static gate (legacy board).
  if (low_pulses_needed == 0U) {
    mode = PSCU_BOARD_MODE_WFCK;
  } else if (state.pulses >= low_pulses_needed) {
    mode = PSCU_BOARD_MODE_WFCK;
  } else {
    mode = PSCU_BOARD_MODE_GATE;
  }

  PSCU_ASSERT((mode == PSCU_BOARD_MODE_GATE) || (mode == PSCU_BOARD_MODE_WFCK));

  return mode;
}
