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
// was last entered, and whether the console has accepted the string since the
// lid last closed. After acceptance the console reads the program area, and the
// drive stays unlocked until the lid opens (Concluded: the check runs at spin-up,
// which is why a disc swap needs a new string), so any further string could only
// expose the chip. The lid line, a mandatory wire, is what ends the latch: a
// swap is seen, never inferred. wait counts the frames still to pass before the
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
// lid is open the chip emits nothing and forgets the disc, so the close re-arms
// it for whatever disc goes in. With the lid closed, a program-area frame shows
// the console accepted the string and latches the chip silent at once, mid-burst
// included, through every later lead-in read until the lid opens. Before that,
// inside the window the chip emits up to max_strings and then falls silent, and
// outside the window it emits nothing and resets the count. After each string
// the next waits gap window frames; each call is one frame.
pscu_stealth_step_t pscu_stealth_step(pscu_stealth_t state,
                                      bool in_window,
                                      bool program,
                                      bool lid_open,
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
