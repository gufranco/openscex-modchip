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
// was last entered.
typedef struct {
  uint8_t sent;
} pscu_stealth_t;

// One step of the stealth state machine plus whether to inject right now.
typedef struct {
  pscu_stealth_t state;
  bool fire;
} pscu_stealth_step_t;

pscu_stealth_t pscu_stealth_init(void);

// Decide, for the current frame, whether to emit one region string. Inside the
// window the chip emits up to max_strings and then falls silent; outside the
// window it emits nothing and resets the count, so leaving the window (end of
// lead-in, or the drive stopping for a disc change) re-arms the one-shot and
// the next lead-in, on this disc or a swapped one, triggers again.
pscu_stealth_step_t pscu_stealth_step(pscu_stealth_t state, bool in_window, uint8_t max_strings);

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
