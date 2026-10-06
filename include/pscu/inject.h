// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_INJECT_H
#define PSCU_INJECT_H

#include <stdbool.h>
#include <stdint.h>

#include "pscu/subq.h"

// Injection policy, kept pure so it is fully host-tested.

// The chip is "in the region-check window" once the leaky SUBQ counter has
// built up to the trigger, meaning the console is reading the lead-in.
bool pscu_should_inject(uint8_t counter, uint8_t trigger);

// After the console accepts the region string it reads the program area, and
// from then on the drive stays unlocked until the disc stops (Concluded: the
// check runs at spin-up, which is why a disc swap needs a new string), so any
// further string can only expose the chip. The latch holds the chip silent through every
// later lead-in read, such as a game seeking back to the table of contents, and
// releases only after the subcode has been lost for PSCU_REARM_LOST_UNITS, the
// signature of a stopped drive and a disc swap. Units are SUBQ frame periods,
// 13.3 ms at single speed (75 frames per second, Read: Red Book): 150 units are
// 2 s at single speed and 1 s at double speed. The figure is Concluded: long
// enough to outlast a seek, which keeps the subcode clocking, and short enough
// that a person swapping a disc keeps the lid open longer. Unknown until a
// console confirms both sides.
#define PSCU_REARM_LOST_UNITS ((uint8_t)150U)

// A capture that fails because SQCK never clocked costs the run loop far more
// than one frame period, so it is weighted by its measured duration: the SQCK
// edge wait is 28 cycles per poll for 65535 polls, 229 ms at 8 MHz (counted from
// the avr-gcc 14.2 -Os listing of the ATtiny85 image), which is 17 frame
// periods. Nine silent captures, about 2 s of stopped clock, release the latch.
#define PSCU_SILENT_CAPTURE_UNITS ((uint8_t)17U)

// The second way out of the latch, so a disc swap never depends on the stop
// being visible. A console that keeps reading the lead-in without ever reaching
// the program area is stuck at a region check: disc 2 of a multi-disc game
// swapped in so fast, or on a drive whose subcode stayed clean, that the lost
// run never filled. After this many lead-in frames with no program-area frame in
// between, the latch releases and the chip injects again. 225 frames are 3 s at
// single speed, the speed of the region check (Concluded from PsNee's WFCK
// figures, Read: about 7.3 kHz during the check, doubling for reads). A game
// seeking back to read the table of contents returns to the program area long
// before that; Concluded, Unknown until a console confirms it.
#define PSCU_REARM_LEAD_IN_FRAMES ((uint8_t)225U)

// Stealth state: how many region strings have been emitted since the window
// was last entered, whether the console has accepted the string since the drive
// last stopped, how many frame periods of lost subcode have passed since, and
// how many lead-in frames have been read since the last program-area frame.
typedef struct {
  uint8_t sent;
  bool accepted;
  uint8_t lost;
  uint8_t lead_in;
} pscu_stealth_t;

// One step of the stealth state machine plus whether to inject right now.
typedef struct {
  pscu_stealth_t state;
  bool fire;
} pscu_stealth_step_t;

pscu_stealth_t pscu_stealth_init(void);

// Decide, for the current frame, whether to emit one region string. Inside the
// window the chip emits up to max_strings and then falls silent; outside the
// window it emits nothing and resets the count, so leaving the window re-arms
// the one-shot for the next lead-in. kind is what this capture showed: a
// program-area frame latches the chip silent at once, mid-burst included, and
// the latch holds until the subcode has been lost long enough to mean the drive
// stopped, or until the console has read the lead-in long enough without
// reaching the program area to mean it is stuck at a new check, so a real disc
// swap re-arms it and a short seek back to the lead-in does not.
pscu_stealth_step_t pscu_stealth_step(pscu_stealth_t state,
                                      bool in_window,
                                      pscu_frame_kind_t kind,
                                      uint8_t max_strings);

// Closed-loop confirmation state: how many idle frames have passed since
// injection while waiting for the program area, and whether it was seen.
typedef struct {
  uint8_t waited;
  bool program_seen;
} pscu_confirm_t;

// One confirmation step: the advanced state, whether the outcome is resolved
// (time to record the session), and whether it resolved as confirmed.
typedef struct {
  pscu_confirm_t state;
  bool resolved;
  bool confirmed;
} pscu_confirm_step_t;

pscu_confirm_t pscu_confirm_init(void);

// Fold one post-injection frame into the confirmation state. Seeing the program
// area resolves as confirmed; otherwise each idle frame advances a bounded wait
// that resolves as unconfirmed once it reaches timeout. Pure and host-tested, so
// the run loop records the session exactly once with the right verdict.
pscu_confirm_step_t pscu_confirm_step(pscu_confirm_t state,
                                      bool idle,
                                      bool program,
                                      uint8_t timeout);

#endif
