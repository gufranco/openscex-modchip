// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "pscu/calib.h"

#include <stdbool.h>
#include <stdint.h>

#include "pscu/assert.h"

// The check byte folds every data byte into the seed. A single changed byte
// always changes it, and a torn write that leaves old and new bytes mixed
// changes it unless the differences cancel, so a damaged record is caught
// rather than trusted.
static uint8_t pscu_calib_check(pscu_calib_record_t record) {
  uint8_t check = PSCU_CALIB_SEED;
  for (uint8_t at = 0U; at < PSCU_CALIB_AT_CHECK; at++) {
    check = (uint8_t)(check ^ record.bytes[at]);
  }
  return check;
}

static bool pscu_calib_in_range(uint8_t value, uint8_t low, uint8_t high) {
  return (value >= low) && (value <= high);
}

// Every field must hold a value this firmware could have written; anything else
// is a different firmware's data or a corrupted cell, and is not trusted.
static bool pscu_calib_valid(pscu_calib_record_t record) {
  uint8_t board = record.bytes[PSCU_CALIB_AT_BOARD];
  bool board_ok = (board <= 1U) || (board == PSCU_CALIB_BOARD_NONE);
  bool cap_ok =
      pscu_calib_in_range(record.bytes[PSCU_CALIB_AT_CAP], PSCU_CALIB_CAP_MIN, PSCU_CALIB_CAP_MAX);
  uint8_t trigger = record.bytes[PSCU_CALIB_AT_TRIGGER];
  bool trigger_ok = pscu_calib_in_range(trigger, PSCU_CALIB_TRIGGER_MIN, PSCU_CALIB_TRIGGER_MAX) &&
                    (((uint8_t)(trigger - PSCU_CALIB_TRIGGER_MIN) % PSCU_CALIB_TRIGGER_STEP) == 0U);
  bool frozen_ok = record.bytes[PSCU_CALIB_AT_FROZEN] <= 1U;
  int8_t trim = (int8_t)record.bytes[PSCU_CALIB_AT_TRIM];
  bool trim_ok = (trim >= -PSCU_TRIM_MAX_OFFSET) && (trim <= PSCU_TRIM_MAX_OFFSET);
  bool magic_ok = record.bytes[PSCU_CALIB_AT_MAGIC] == PSCU_CALIB_MAGIC;
  bool check_ok = record.bytes[PSCU_CALIB_AT_CHECK] == pscu_calib_check(record);
  return magic_ok && board_ok && cap_ok && trigger_ok && frozen_ok && trim_ok && check_ok;
}

static pscu_calib_t pscu_calib_defaults(void) {
  pscu_calib_t calib = {
    PSCU_CALIB_BOARD_NONE, PSCU_CALIB_CAP_MAX, PSCU_CALIB_TRIGGER_MIN, false, 0
  };
  return calib;
}

pscu_calib_t pscu_calib_decode(pscu_calib_record_t record) {
  pscu_calib_t calib = pscu_calib_defaults();
  if (pscu_calib_valid(record)) {
    calib.board = record.bytes[PSCU_CALIB_AT_BOARD];
    calib.cap = record.bytes[PSCU_CALIB_AT_CAP];
    calib.trigger = record.bytes[PSCU_CALIB_AT_TRIGGER];
    calib.frozen = record.bytes[PSCU_CALIB_AT_FROZEN] != 0U;
    calib.trim = (int8_t)record.bytes[PSCU_CALIB_AT_TRIM];
  }
  return calib;
}

pscu_calib_record_t pscu_calib_encode(pscu_calib_t calib) {
  pscu_calib_record_t record = { { 0U, 0U, 0U, 0U, 0U, 0U, 0U } };
  record.bytes[PSCU_CALIB_AT_MAGIC] = PSCU_CALIB_MAGIC;
  record.bytes[PSCU_CALIB_AT_BOARD] = calib.board;
  record.bytes[PSCU_CALIB_AT_CAP] = calib.cap;
  record.bytes[PSCU_CALIB_AT_TRIGGER] = calib.trigger;
  record.bytes[PSCU_CALIB_AT_FROZEN] = calib.frozen ? 1U : 0U;
  record.bytes[PSCU_CALIB_AT_TRIM] = (uint8_t)calib.trim;
  record.bytes[PSCU_CALIB_AT_CHECK] = pscu_calib_check(record);
  return record;
}

pscu_calib_boot_t pscu_calib_boot(pscu_calib_t stored, uint8_t board) {
  PSCU_ASSERT(board <= 1U);

  pscu_calib_boot_t out;
  out.board_changed = (stored.board != PSCU_CALIB_BOARD_NONE) && (stored.board != board);
  out.calib = out.board_changed ? pscu_calib_defaults() : stored;
  out.calib.board = board;
  out.calib.trim = stored.trim;
  return out;
}

bool pscu_calib_keeps_board(pscu_calib_t stored, bool watchdog) {
  return watchdog && (stored.board != PSCU_CALIB_BOARD_NONE);
}

pscu_board_mode_t pscu_calib_stored_board(pscu_calib_t stored) {
  PSCU_ASSERT(stored.board <= 1U);

  return (stored.board == 1U) ? PSCU_BOARD_MODE_WFCK : PSCU_BOARD_MODE_GATE;
}

// Step the start back to the last value that worked and stop probing. Repeated
// failures keep stepping back, down to the default, which is where the chip
// started before it learned anything. The trigger sits on the step grid, so one
// step back from anything above the default never passes below it.
static pscu_calib_t pscu_calib_back_off(pscu_calib_t calib) {
  pscu_calib_t next = calib;
  next.trigger = (calib.trigger > PSCU_CALIB_TRIGGER_MIN)
                     ? (uint8_t)(calib.trigger - PSCU_CALIB_TRIGGER_STEP)
                     : PSCU_CALIB_TRIGGER_MIN;
  next.frozen = true;
  return next;
}

// An accepted disc sets the cap from what it needed, and, while probing, tries
// the next later start; reaching the bound ends the probe there.
static pscu_calib_t pscu_calib_accepted(pscu_calib_t calib, uint8_t strings) {
  PSCU_ASSERT(strings > 0U);

  pscu_calib_t next = calib;
  next.cap = (strings < PSCU_CALIB_CAP_FITS) ? (uint8_t)(strings + PSCU_CALIB_CAP_MARGIN)
                                             : PSCU_CALIB_CAP_MAX;
  if (!calib.frozen) {
    next.trigger = (calib.trigger < PSCU_CALIB_TRIGGER_MAX)
                       ? (uint8_t)(calib.trigger + PSCU_CALIB_TRIGGER_STEP)
                       : PSCU_CALIB_TRIGGER_MAX;
    next.frozen = next.trigger == PSCU_CALIB_TRIGGER_MAX;
  }
  return next;
}

// A refusal and a missed window both step the start back; only a refusal also
// restores the full cap, since a missed window sent no strings to judge it by.
pscu_calib_t pscu_calib_learn(pscu_calib_t calib, pscu_calib_outcome_t outcome, uint8_t strings) {
  pscu_calib_t backed = pscu_calib_back_off(calib);
  backed.cap = (outcome == PSCU_CALIB_REFUSED) ? PSCU_CALIB_CAP_MAX : calib.cap;
  pscu_calib_t next =
      (outcome == PSCU_CALIB_ACCEPTED) ? pscu_calib_accepted(calib, strings) : backed;

  PSCU_ASSERT(pscu_calib_in_range(next.cap, PSCU_CALIB_CAP_MIN, PSCU_CALIB_CAP_MAX));
  PSCU_ASSERT(pscu_calib_in_range(next.trigger, PSCU_CALIB_TRIGGER_MIN, PSCU_CALIB_TRIGGER_MAX));
  return next;
}

bool pscu_calib_missed(pscu_calib_t calib, uint8_t previous, uint8_t counter, bool armed) {
  bool dropped = (previous >= PSCU_CALIB_TRIGGER_MIN) && (counter < PSCU_CALIB_TRIGGER_MIN);
  return dropped && (calib.trigger > PSCU_CALIB_TRIGGER_MIN) && !armed;
}
