// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "pscu/diag.h"

#include <stddef.h>
#include <stdint.h>

#include "pscu/assert.h"
#include "pscu/board_mode.h"

// The flight-recorder codec. These are pure so the host tests pin the byte
// layout that a human reads back with avrdude; the platform layer only moves the
// bytes to and from EEPROM. Keeping the format here, tested, is what stops the
// recorder from silently drifting from what the docs promise an installer.

pscu_diag_record_t pscu_diag_decode(const uint8_t *raw) {
  PSCU_ASSERT(raw != NULL);

  pscu_diag_record_t record = {PSCU_BOARD_MODE_GATE, 0U, 0U};
  if (raw[0] == PSCU_DIAG_MAGIC) {
    record.board = (raw[1] != 0U) ? PSCU_BOARD_MODE_WFCK : PSCU_BOARD_MODE_GATE;
    record.sessions = raw[2];
    record.injects = raw[3];
  }
  return record;
}

// sessions advances by one and wraps at 255 on purpose: the count is a liveness
// hint for the installer, not an exact odometer, and a wrap is harmless.
pscu_diag_record_t pscu_diag_build(pscu_board_mode_t board, uint8_t prev_sessions,
                                   uint8_t injects) {
  PSCU_ASSERT((board == PSCU_BOARD_MODE_GATE) || (board == PSCU_BOARD_MODE_WFCK));

  pscu_diag_record_t record = {board, (uint8_t)(prev_sessions + 1U), injects};
  return record;
}

void pscu_diag_encode(pscu_diag_record_t record, uint8_t *raw) {
  PSCU_ASSERT(raw != NULL);

  raw[0] = PSCU_DIAG_MAGIC;
  raw[1] = (record.board == PSCU_BOARD_MODE_WFCK) ? 1U : 0U;
  raw[2] = record.sessions;
  raw[3] = record.injects;
}
