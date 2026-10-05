// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_DIAG_H
#define PSCU_DIAG_H

#include <stdint.h>

#include "pscu/board_mode.h"

// In-field diagnostics. No existing PS1 modchip reports what it saw, so a failed
// install is a silent guess. This chip records a tiny flight recorder to EEPROM
// after it has finished injecting and gone idle, so the installer can read it
// back with the programmer and learn which board era was detected and whether
// injection ran. The write happens only once the chip is idle, never at boot and
// never inside the region-check window, so it cannot perturb injection timing.

// Five bytes, laid out so an erased EEPROM (all 0xFF) is told apart from a real
// record by the magic byte. The layout is a public contract read by a human with
// avrdude, so it is encoded and decoded by pure functions the host tests pin.
#define PSCU_DIAG_EEPROM_BYTES ((uint8_t)5U)
#define PSCU_DIAG_EEPROM_ADDR ((uint8_t)0U)
#define PSCU_DIAG_MAGIC ((uint8_t)0x50U)

// board is the detected era, sessions counts power cycles that reached idle
// (wraps at 255), injects counts region strings emitted this session (clamped at
// 255), confirmed is 1 when the console was seen reaching the program area after
// injection (the region check passed). A decode of an erased or foreign EEPROM
// yields sessions 0 and confirmed 0.
typedef struct {
  pscu_board_mode_t board;
  uint8_t sessions;
  uint8_t injects;
  uint8_t confirmed;
} pscu_diag_record_t;

// Read back the previous record from raw EEPROM bytes; a wrong magic means no
// prior record, so sessions is 0. Pure, so the host tests own its correctness.
pscu_diag_record_t pscu_diag_decode(const uint8_t *raw);

// Build this session's record from the detected board, the previous session
// count, and the injection count, advancing sessions by one with wraparound.
pscu_diag_record_t pscu_diag_build(pscu_board_mode_t board,
                                   uint8_t prev_sessions,
                                   uint8_t injects,
                                   uint8_t confirmed);

// Serialise a record into the five EEPROM bytes, magic first.
void pscu_diag_encode(pscu_diag_record_t record, uint8_t *raw);

#endif
