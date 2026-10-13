// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_INJECT_H
#define PSCU_INJECT_H

#include <stdbool.h>
#include <stdint.h>

// Injection policy, kept pure so it is fully host-tested.

// True once the SUBQ counter reaches the trigger: the console is reading the
// lead-in.
bool pscu_should_inject(uint8_t counter, uint8_t trigger);

// Strings sent in this window, whether the console accepted one, and frames to
// wait before the next. After acceptance the chip stays silent: anti-mod code
// counts strings in the program area (psx-spx, 19h,04h and 19h,05h).
typedef struct {
  uint8_t sent;
  bool accepted;
  uint8_t wait;
} pscu_stealth_t;

// The window on this frame. HELD is inside the window but not allowed to send,
// e.g. under the supply guard; the burst keeps its count and gap.
typedef enum { PSCU_WINDOW_CLOSED = 0, PSCU_WINDOW_OPEN = 1, PSCU_WINDOW_HELD = 2 } pscu_window_t;

// One step of the stealth state machine plus whether to inject right now.
typedef struct {
  pscu_stealth_t state;
  bool fire;
} pscu_stealth_step_t;

pscu_stealth_t pscu_stealth_init(void);

// Decide, per frame, whether to emit one string: up to max_strings per window,
// gap frames apart, none after a program-area frame. Leaving the window or
// losing the disc resets the state; a held window changes nothing.
pscu_stealth_step_t pscu_stealth_step(pscu_stealth_t state,
                                      pscu_window_t window,
                                      bool program,
                                      bool disc_gone,
                                      uint8_t max_strings,
                                      uint8_t gap);

// Idle frames since injection, and whether the program area was seen.
typedef struct {
  uint8_t waited;
  bool program_seen;
} pscu_confirm_t;

// The next state, whether the session is resolved, and whether it was
// confirmed.
typedef struct {
  pscu_confirm_t state;
  bool resolved;
  bool confirmed;
} pscu_confirm_step_t;

// No valid SUBQ frame for this long means the disc is gone: opening the lid
// stops the spindle. A seek keeps SUBQ frames coming, since the drive decodes
// subchannel position data while seeking (psx-spx cdromdrive.md L899), so only a
// stopped spindle goes this long without one; 1.5 s also clears the more than
// one second a seek or spin-up can take to answer (L1846) and stays under a
// disc swap.
#define PSCU_DISC_GONE_MS ((uint32_t)1500U)

typedef struct {
  uint32_t quiet_ms;
  bool seen;
} pscu_presence_t;

pscu_presence_t pscu_presence_init(void);

// Fold one pass in. The quiet time saturates.
pscu_presence_t pscu_presence_step(pscu_presence_t state, bool valid, uint32_t elapsed_ms);

// Whether the disc counts as gone: no valid frame for PSCU_DISC_GONE_MS.
bool pscu_presence_gone(pscu_presence_t state);

pscu_confirm_t pscu_confirm_init(void);

// Fold one post-injection frame in. The program area resolves as confirmed;
// timeout idle frames resolve as unconfirmed.
pscu_confirm_step_t pscu_confirm_step(pscu_confirm_t state,
                                      bool idle,
                                      bool program,
                                      uint8_t timeout);

#endif
