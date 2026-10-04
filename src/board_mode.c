// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "pscu/board_mode.h"

#include <stdbool.h>
#include <stddef.h>

#include "pscu/assert.h"

pscu_board_mode_t pscu_board_mode_from_samples(const uint8_t *wfck_samples,
                                               uint16_t count,
                                               uint8_t low_pulses_needed) {
  PSCU_ASSERT(wfck_samples != NULL);

  if (low_pulses_needed == 0U) {
    return PSCU_BOARD_MODE_WFCK;
  }

  uint8_t pulses = 0U;
  bool prev_high = true;

  for (uint16_t i = 0U; i < count; i++) {
    bool low = (wfck_samples[i] == 0U);
    if (low && prev_high) {
      pulses++;
      if (pulses >= low_pulses_needed) {
        return PSCU_BOARD_MODE_WFCK;
      }
    }
    prev_high = !low;
  }

  return PSCU_BOARD_MODE_GATE;
}
