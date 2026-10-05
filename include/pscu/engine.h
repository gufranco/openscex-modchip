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

// Write the diagnostics flight recorder to EEPROM once the chip is idle: read
// the previous record, advance the session count, store the detected board and
// this session's injection count. The run loop calls this exactly once per power
// cycle, after injection has finished, so it never competes with injection
// timing and the EEPROM endurance is one write per session.
void pscu_engine_log_session(pscu_board_mode_t board, uint8_t injects, uint8_t confirmed);

#endif
