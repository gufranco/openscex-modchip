// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_ENGINE_H
#define PSCU_ENGINE_H

#include <stdbool.h>
#include <stdint.h>

#include "pscu/board_mode.h"
#include "pscu/calib.h"

// The platform layer the run loop drives on real pins. A failed capture fills
// the 12-byte buffer with 0xFF and returns false.
pscu_board_mode_t pscu_engine_detect_board(bool lamp);

// Re-check a board taken for a static gate just before a string: if WFCK is a
// live carrier after all, return the carrier mode so WFCK is never driven.
pscu_board_mode_t pscu_engine_confirm_board(pscu_board_mode_t board);

bool pscu_engine_capture_frame(uint8_t *frame);

void pscu_engine_inject(pscu_board_mode_t board, bool lamp);

// Read the calibration record from EEPROM; a missing or damaged one reads as the
// defaults.
pscu_calib_t pscu_engine_load_calib(void);

// Store a calibration, writing only the bytes that differ from EEPROM. Returns
// whether the record is now in EEPROM: false on a low supply or a write that
// could not start, so the caller keeps it to store again.
bool pscu_engine_store_calib(pscu_calib_t calib);

// Step OSCCAL from the factory value to the stored trim; returns the factory
// value that bounds later trims.
uint8_t pscu_engine_apply_trim(int8_t trim);

#endif
