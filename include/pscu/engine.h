// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_ENGINE_H
#define PSCU_ENGINE_H

#include <stdint.h>

#include "pscu/board_mode.h"

pscu_board_mode_t pscu_engine_detect_board(void);

void pscu_engine_capture_frame(uint8_t *frame);

void pscu_engine_inject(pscu_board_mode_t board);

#endif
