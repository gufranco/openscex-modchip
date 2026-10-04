// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_BOARD_MODE_H
#define PSCU_BOARD_MODE_H

#include <stdint.h>

typedef enum { PSCU_BOARD_MODE_GATE = 0, PSCU_BOARD_MODE_WFCK = 1 } pscu_board_mode_t;

pscu_board_mode_t pscu_board_mode_from_samples(const uint8_t *wfck_samples,
                                               uint16_t count,
                                               uint8_t low_pulses_needed);

#endif
