// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "pscu/mode.h"

pscu_mode_t pscu_mode_next(pscu_mode_t mode) {
  uint8_t next = (uint8_t)((uint8_t)mode + 1U);

  if (next >= PSCU_MODE_COUNT) {
    next = 0U;
  }

  return (pscu_mode_t)next;
}

pscu_gesture_t pscu_gesture_init(void) {
  pscu_gesture_t state = {0U, 0U, 0U};
  return state;
}

pscu_gesture_t pscu_gesture_step(pscu_gesture_t state, uint8_t active,
                                 uint16_t threshold) {
  state.fired = 0U;

  if (active != 0U) {
    if (state.held < 0xFFFFU) {
      state.held = (uint16_t)(state.held + 1U);
    }
    if (state.held >= threshold) {
      state.armed = 1U;
    }
  } else {
    if (state.armed != 0U) {
      state.fired = 1U;
    }
    state.held = 0U;
    state.armed = 0U;
  }

  return state;
}
