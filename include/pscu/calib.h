// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_CALIB_H
#define PSCU_CALIB_H

#include <stdbool.h>
#include <stdint.h>

#include "pscu/config.h"
#include "pscu/run.h"

// Per-console calibration. Every console reads the region string a little
// differently: how many strings it takes before the mechacon accepts, and how
// far into the lead-in it looks. The chip measures both on each disc and keeps
// the result in EEPROM, so on later discs it sends fewer strings and starts
// later, which is less time with DATA driven. Nothing here is a mode or a user
// setting (AGENTS.md rule 7): every value is a measurement of this console, and
// every value only ever falls back toward the fixed defaults, so a lost or torn
// record costs stealth, never a disc.

// The record lives at EEPROM address 0, six bytes in this order. The magic marks
// a record this firmware wrote; an erased chip reads 0xFF everywhere and so
// starts from the defaults. The check byte is the XOR of the five bytes before
// it with a fixed seed, so a write cut short by power-off, which leaves some
// bytes new and some old, almost always fails the check and reads as defaults.
#define PSCU_CALIB_BYTES ((uint8_t)6U)
#define PSCU_CALIB_MAGIC ((uint8_t)0xC5U)
#define PSCU_CALIB_SEED ((uint8_t)0x5AU)
#define PSCU_CALIB_AT_MAGIC ((uint8_t)0U)
#define PSCU_CALIB_AT_BOARD ((uint8_t)1U)
#define PSCU_CALIB_AT_CAP ((uint8_t)2U)
#define PSCU_CALIB_AT_TRIGGER ((uint8_t)3U)
#define PSCU_CALIB_AT_FROZEN ((uint8_t)4U)
#define PSCU_CALIB_AT_CHECK ((uint8_t)5U)

// A board byte that names no board: the state of a fresh chip, which has never
// detected one, so the first boot can never report a board change.
#define PSCU_CALIB_BOARD_NONE ((uint8_t)0xFFU)

// String cap. A disc that was accepted after n strings sets the cap to n plus a
// margin, never above the fixed safety cap. n is counted until the program area
// appears, so it already includes the time the console takes to resume reading
// after it accepts; the margin covers a disc that reads a little worse than the
// last one. The margin is a design choice with no console measurement behind it
// yet (Unknown until hardware), and any refusal puts the cap straight back to
// PSCU_STEALTH_STRINGS. A session has at least one string, so the smallest cap
// is one string plus the margin. FITS is the first n whose sum would pass the
// safety cap; below it the sum is used as is.
#define PSCU_CALIB_CAP_MARGIN ((uint8_t)4U)
#define PSCU_CALIB_CAP_MIN ((uint8_t)(1U + PSCU_CALIB_CAP_MARGIN))
#define PSCU_CALIB_CAP_MAX PSCU_STEALTH_STRINGS
#define PSCU_CALIB_CAP_FITS ((uint8_t)((PSCU_CALIB_CAP_MAX - PSCU_CALIB_CAP_MARGIN) + 1U))

// Start point. The window opens when the leaky SUBQ counter reaches the trigger,
// one step per lead-in frame, so each step is one frame later, 13.3 ms at the
// 75 Hz subcode rate (Read: Red Book, 75 sectors per second). Each accepted disc
// probes two frames later, up to twenty frames, 0.27 s, past the default; the
// bound keeps the start well inside a lead-in read that lasts seconds
// (Concluded: the TOC repeats through the whole lead-in). A refusal or a missed
// window steps back and freezes the probe, so the chip settles on the latest
// start this console has accepted. Step and bound are design choices, Unknown
// until hardware. The trigger only ever moves in whole steps from the default
// and the bound sits on that grid, so a stored trigger off the grid is corrupt.
// A Japanese build does not probe at all: PsNee warns that a trigger above 11 on
// Japanese models causes problems with anti-mod discs (Read: PsNee V9.0
// PSNee.ino:44-51), and the first step from the default of 10 already lands on
// 12. The bound then equals the default, so the first accepted disc freezes the
// probe where it started.
#define PSCU_CALIB_TRIGGER_MIN PSCU_INJECT_TRIGGER
#define PSCU_CALIB_TRIGGER_STEP ((uint8_t)2U)
#if defined(PSCU_REGION_JP)
#define PSCU_CALIB_PROBE_FRAMES 0U
#else
#define PSCU_CALIB_PROBE_FRAMES 20U
#endif
#define PSCU_CALIB_TRIGGER_MAX ((uint8_t)(PSCU_INJECT_TRIGGER + PSCU_CALIB_PROBE_FRAMES))

// board is the detected board (0 static gate, 1 WFCK carrier, or NONE), cap the
// string cap, trigger the counter value that opens the window, and frozen whether
// the start-point probe has stopped.
typedef struct {
  uint8_t board;
  uint8_t cap;
  uint8_t trigger;
  bool frozen;
} pscu_calib_t;

// The record as stored, byte for byte.
typedef struct {
  uint8_t bytes[PSCU_CALIB_BYTES];
} pscu_calib_record_t;

// How a disc's session ended, as far as calibration cares: accepted after a
// number of strings, refused after the strings ran out or the window closed, or
// the window missed entirely because the learned start came too late. A session
// ended by the lid opening teaches nothing and is never passed in.
typedef enum {
  PSCU_CALIB_ACCEPTED = 0,
  PSCU_CALIB_REFUSED = 1,
  PSCU_CALIB_MISSED = 2
} pscu_calib_outcome_t;

// The boot step's result: the calibration to run with, and whether the detected
// board differs from the stored one, which is LED code 7.
typedef struct {
  pscu_calib_t calib;
  bool board_changed;
} pscu_calib_boot_t;

// Read a stored record; anything without the magic, with an out-of-range value,
// or failing the check byte reads as the fixed defaults: no board, the full cap,
// the default trigger, probing.
pscu_calib_t pscu_calib_decode(pscu_calib_record_t record);

// Build the record for a calibration, check byte included.
pscu_calib_record_t pscu_calib_encode(pscu_calib_t calib);

// Fold the board detected at this boot into the stored calibration. A different
// board means the chip moved to another console or its WFCK wire is
// intermittent; either way the learned values describe something else, so they
// restart from the defaults under the new board.
pscu_calib_boot_t pscu_calib_boot(pscu_calib_t stored, uint8_t board);

// Learn from one resolved session. strings is the number sent for it.
pscu_calib_t pscu_calib_learn(pscu_calib_t calib, pscu_calib_outcome_t outcome, uint8_t strings);

// Whether the window was missed on this pass: the counter had reached the
// default trigger and has now fallen back below it, the learned start is later
// than the default, and since the lid closed no string was sent and no program
// area was seen. That disc needed a string the late start never sent.
bool pscu_calib_missed(pscu_calib_t calib, uint8_t previous, uint8_t counter, bool armed);

#endif
