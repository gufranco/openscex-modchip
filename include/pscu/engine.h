// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_ENGINE_H
#define PSCU_ENGINE_H

#include <stdbool.h>
#include <stdint.h>

#include "pscu/board_mode.h"
#include "pscu/calib.h"

// The platform layer: the operations the run loop drives on real pins. Detect
// the board era once; capture one SUBQ frame into a 12-byte buffer, starting on
// a frame boundary, filling the buffer with 0xFF and returning false when no
// frame could be captured; and inject the one configured region word using the method the
// detected board needs.
pscu_board_mode_t pscu_engine_detect_board(bool lamp);

// Re-check a board taken for a static gate just before a string: if WFCK is a
// live carrier after all, return the carrier mode so WFCK is never driven.
pscu_board_mode_t pscu_engine_confirm_board(pscu_board_mode_t board);

bool pscu_engine_capture_frame(uint8_t *frame);

void pscu_engine_inject(pscu_board_mode_t board, bool lamp);

// Read the calibration record from EEPROM; a missing or damaged one reads as the
// defaults.
pscu_calib_t pscu_engine_load_calib(void);

// Store a calibration, writing only the bytes that differ from what is already
// in EEPROM, so an unchanged value costs no write and no wear.
void pscu_engine_store_calib(pscu_calib_t calib);

// Move OSCCAL from the factory value to the stored trim, one step at a time,
// and return the factory value the run loop's later trims are bounded by.
uint8_t pscu_engine_apply_trim(int8_t trim);

#endif
