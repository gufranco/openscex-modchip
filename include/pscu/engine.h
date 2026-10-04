// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_ENGINE_H
#define PSCU_ENGINE_H

#include <stdint.h>

#include "pscu/board_mode.h"

// The platform layer: the three operations the run loop drives on real pins.
// Detect the board era once, capture one SUBQ frame into a 12-byte buffer, and
// inject all three region words using the method the detected board needs.
pscu_board_mode_t pscu_engine_detect_board(void);

void pscu_engine_capture_frame(uint8_t *frame);

void pscu_engine_inject(pscu_board_mode_t board);

#endif
