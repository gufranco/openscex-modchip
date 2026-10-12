// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_CALIB_H
#define PSCU_CALIB_H

#include <stdbool.h>
#include <stdint.h>

#include "pscu/board_mode.h"
#include "pscu/config.h"
#include "pscu/run.h"
#include "pscu/trim.h"

// Per-console calibration kept in EEPROM: how late in the lead-in this console
// still accepts a string, so later discs start later and drive DATA for less
// time. Every value is a measurement; a lost record falls back to the defaults.

// Record layout at EEPROM address 0. The magic changes with the layout. The
// check byte is the XOR of the bytes before it with a seed, so a torn write
// almost always fails it.
#define PSCU_CALIB_BYTES ((uint8_t)6U)
#define PSCU_CALIB_MAGIC ((uint8_t)0xC8U)
#define PSCU_CALIB_SEED ((uint8_t)0x5AU)
#define PSCU_CALIB_AT_MAGIC ((uint8_t)0U)
#define PSCU_CALIB_AT_BOARD ((uint8_t)1U)
#define PSCU_CALIB_AT_TRIGGER ((uint8_t)2U)
#define PSCU_CALIB_AT_FROZEN ((uint8_t)3U)
#define PSCU_CALIB_AT_TRIM ((uint8_t)4U)
#define PSCU_CALIB_AT_CHECK ((uint8_t)5U)

// Board byte of a fresh chip; the first boot never reports a board change.
#define PSCU_CALIB_BOARD_NONE ((uint8_t)0xFFU)

// Start-point probe: each accepted disc moves the trigger two frames later, up
// to 20 frames (0.27 s at 75 Hz, Red Book); a refusal or miss steps back and
// freezes it. Japan does not probe: a trigger above 11 breaks anti-mod discs
// there (PsNee V9.0 PSNee.ino:44-51).
#define PSCU_CALIB_TRIGGER_MIN PSCU_INJECT_TRIGGER
#define PSCU_CALIB_TRIGGER_STEP ((uint8_t)2U)
#if defined(PSCU_REGION_JP)
#define PSCU_CALIB_PROBE_FRAMES 0U
#else
#define PSCU_CALIB_PROBE_FRAMES 20U
#endif
#define PSCU_CALIB_TRIGGER_MAX ((uint8_t)(PSCU_INJECT_TRIGGER + PSCU_CALIB_PROBE_FRAMES))

// board: 0 gate, 1 carrier, or NONE. trigger: counter value that opens the
// window. frozen: the probe has stopped. trim: OSCCAL steps from factory.
typedef struct {
  uint8_t board;
  uint8_t trigger;
  bool frozen;
  int8_t trim;
} pscu_calib_t;

// The record as stored, byte for byte.
typedef struct {
  uint8_t bytes[PSCU_CALIB_BYTES];
} pscu_calib_record_t;

// How a disc's session ended: accepted, refused, or the window was missed
// because the learned start came too late.
typedef enum {
  PSCU_CALIB_ACCEPTED = 0,
  PSCU_CALIB_REFUSED = 1,
  PSCU_CALIB_MISSED = 2
} pscu_calib_outcome_t;

// The calibration to run with, and whether the board differs from the stored
// one.
typedef struct {
  pscu_calib_t calib;
  bool board_changed;
} pscu_calib_boot_t;

// Decode a record; a bad magic, range or check byte reads as the defaults.
pscu_calib_t pscu_calib_decode(pscu_calib_record_t record);

// Build the record for a calibration, check byte included.
pscu_calib_record_t pscu_calib_encode(pscu_calib_t calib);

// Fold this boot's board into the stored calibration. A different board resets
// the learned start; the trim belongs to the chip, so it is kept.
pscu_calib_boot_t pscu_calib_boot(pscu_calib_t stored, uint8_t board);

// Whether to reuse the recorded board instead of detecting it. True after a
// watchdog reset: a stalled carrier would read as a gate, and the chip would
// then drive WFCK against the CD DSP. Power-on always detects.
bool pscu_calib_keeps_board(pscu_calib_t stored, bool watchdog);

// The board mode a valid record names.
pscu_board_mode_t pscu_calib_stored_board(pscu_calib_t stored);

// Learn from one resolved session.
pscu_calib_t pscu_calib_learn(pscu_calib_t calib, pscu_calib_outcome_t outcome);

// Whether the window was missed: the counter rose past the default trigger and
// fell back, the learned start is later than the default, and no string or
// program area came since this disc arrived.
bool pscu_calib_missed(pscu_calib_t calib, uint8_t previous, uint8_t counter, bool armed);

#endif
