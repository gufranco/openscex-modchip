// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_INJECT_H
#define PSCU_INJECT_H

#include <stdbool.h>
#include <stdint.h>

// Injection policy, kept pure so it is fully host-tested.

// The chip is "in the region-check window" once the leaky SUBQ counter has
// built up to the trigger, meaning the console is reading the lead-in.
bool pscu_should_inject(uint8_t counter, uint8_t trigger);

// Stealth state: how many region strings have been emitted since the window
// was last entered, and whether the console has accepted the string since this
// disc arrived. After acceptance the console reads the program area, and the
// drive stays unlocked until the disc leaves (Concluded: the check runs at
// spin-up, which is why a disc swap needs a new string), so any further string
// could only expose the chip, including during a later lead-in reread by an
// anti-mod check. The disc leaving, seen as a stretch with no valid SUBQ frame,
// is what ends the latch. wait counts the frames still to pass before the
// next string may start, so strings are spaced the way a disc and the
// long-deployed chips space them rather than sent back to back.
typedef struct {
  uint8_t sent;
  bool accepted;
  uint8_t wait;
} pscu_stealth_t;

// One step of the stealth state machine plus whether to inject right now.
typedef struct {
  pscu_stealth_t state;
  bool fire;
} pscu_stealth_step_t;

pscu_stealth_t pscu_stealth_init(void);

// Decide, for the current frame, whether to emit one region string. While the
// disc is gone the chip emits nothing and forgets it, so the next disc finds the
// chip re-armed. With a disc present, a program-area frame shows the console
// accepted the string and latches the chip silent at once, mid-burst included,
// through every later lead-in read until the disc leaves. Before that,
// inside the window the chip emits up to max_strings and then falls silent, and
// outside the window it emits nothing and resets the count. After each string
// the next waits gap window frames; each call is one frame.
pscu_stealth_step_t pscu_stealth_step(pscu_stealth_t state,
                                      bool in_window,
                                      bool program,
                                      bool disc_gone,
                                      uint8_t max_strings,
                                      uint8_t gap);

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

// Disc presence, inferred from SUBQ alone. Opening the lid stops the drive, so
// valid frames stop (Concluded: the lid switch halts the spindle, and a stopped
// disc yields no mode 1 frame). quiet_ms counts the time since the last valid
// frame and seen says one has arrived since power-on. A stretch of
// PSCU_DISC_GONE_MS without one means the disc is gone. A lead-in reread or a
// seek on a spinning disc keeps producing valid frames, so it never reads as a
// swap; a game that stops the drive for longer than this does, and the chip
// then re-arms as PsNee always does. 1.5 s is a design choice, Unknown until
// hardware: longer than any seek on a spinning disc, shorter than a person
// takes to open the lid, swap the disc and close it.
#define PSCU_DISC_GONE_MS ((uint32_t)1500U)

typedef struct {
  uint32_t quiet_ms;
  bool seen;
} pscu_presence_t;

pscu_presence_t pscu_presence_init(void);

// Fold one pass in: valid says this pass captured a valid frame, elapsed_ms the
// time since the last pass. The quiet time saturates rather than wrapping.
pscu_presence_t pscu_presence_step(pscu_presence_t state, bool valid, uint32_t elapsed_ms);

// Whether the disc counts as gone: no valid frame for PSCU_DISC_GONE_MS.
bool pscu_presence_gone(pscu_presence_t state);

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
