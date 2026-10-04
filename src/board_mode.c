// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "pscu/board_mode.h"

#define PSCU_BOARD_PULSES_MAX ((uint8_t)0xFFU)

pscu_board_detect_t pscu_board_detect_init(void) {
  pscu_board_detect_t state = {0U, 1U};
  return state;
}

pscu_board_detect_t pscu_board_detect_step(pscu_board_detect_t state,
                                           uint8_t wfck_sample) {
  uint8_t low = (uint8_t)((wfck_sample == 0U) ? 1U : 0U);

  if ((low == 1U) && (state.prev_high == 1U) &&
      (state.pulses < PSCU_BOARD_PULSES_MAX)) {
    state.pulses = (uint8_t)(state.pulses + 1U);
  }
  state.prev_high = (uint8_t)(1U - low);

  return state;
}

pscu_board_mode_t pscu_board_detect_mode(pscu_board_detect_t state,
                                         uint8_t low_pulses_needed) {
  pscu_board_mode_t mode = PSCU_BOARD_MODE_GATE;

  if (low_pulses_needed == 0U) {
    mode = PSCU_BOARD_MODE_WFCK;
  } else if (state.pulses >= low_pulses_needed) {
    mode = PSCU_BOARD_MODE_WFCK;
  } else {
    mode = PSCU_BOARD_MODE_GATE;
  }

  return mode;
}
