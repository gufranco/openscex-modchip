// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_BOARD_MODE_H
#define PSCU_BOARD_MODE_H

#include <stdint.h>

typedef enum { PSCU_BOARD_MODE_GATE = 0, PSCU_BOARD_MODE_WFCK = 1 } pscu_board_mode_t;

typedef struct {
  uint8_t pulses;
  uint8_t prev_high;
} pscu_board_detect_t;

pscu_board_detect_t pscu_board_detect_init(void);

pscu_board_detect_t pscu_board_detect_step(pscu_board_detect_t state,
                                           uint8_t wfck_sample);

pscu_board_mode_t pscu_board_detect_mode(pscu_board_detect_t state,
                                         uint8_t low_pulses_needed);

#endif
