// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_ENGINE_H
#define PSCU_ENGINE_H

#include <stdbool.h>
#include <stdint.h>

#include "pscu/board_mode.h"

// The platform layer: the operations the run loop drives on real pins. Detect
// the board era once; capture one SUBQ frame into a 12-byte buffer, starting on
// a frame boundary, filling the buffer with 0xFF and returning false when no
// frame could be captured; and inject the one configured region word using the method the
// detected board needs, stopping the moment the lid opens.
pscu_board_mode_t pscu_engine_detect_board(void);

bool pscu_engine_capture_frame(uint8_t *frame);

void pscu_engine_inject(pscu_board_mode_t board);

#endif
