// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_MODE_H
#define PSCU_MODE_H

#include <stdint.h>

#define PSCU_MODE_COUNT ((uint8_t)4U)

typedef enum {
  PSCU_MODE_DEFAULT = 0,
  PSCU_MODE_ALT_TIMING = 1,
  PSCU_MODE_OLD_MODCHIP = 2,
  PSCU_MODE_DISABLED = 3
} pscu_mode_t;

typedef struct {
  uint16_t held;
  uint8_t armed;
  uint8_t fired;
} pscu_gesture_t;

pscu_mode_t pscu_mode_next(pscu_mode_t mode);

pscu_gesture_t pscu_gesture_init(void);

pscu_gesture_t pscu_gesture_step(pscu_gesture_t state, uint8_t active,
                                 uint16_t threshold);

#endif
