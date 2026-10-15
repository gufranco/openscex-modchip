// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_LOOP_H
#define PSCU_LOOP_H

#include <stdbool.h>
#include <stdint.h>

#include "pscu/calib.h"
#include "pscu/indicator.h"
#include "pscu/inject.h"

// Every decision of one run-loop pass, as a pure function so the host suite
// covers the wiring between modules. run.c keeps only the hardware calls.

// The trim ignores the first second of a disc, while the drive spins up. A
// design bound, not yet measured on a console.
#define PSCU_LOOP_TRIM_SETTLE_MS ((uint32_t)1000U)

// How far past the learned start the SUBQ counter may climb. Bounds how long
// the window stays open once frames stop: 8 misses, about 0.5 s. A design
// bound, not yet tuned on a console.
#define PSCU_LOOP_COUNTER_HEADROOM ((uint8_t)8U)

// How far below the learned start the counter must fall before the next
// opening gets a fresh cap. A shallower dip holds the burst, so alternating
// hits and misses cannot refire it. A design bound, kept under the lowest
// start by the assertion below, not yet tuned on a console.
#define PSCU_LOOP_REARM_DEPTH ((uint8_t)8U)

// Keeps a drained counter at least the depth below any start, so
// pscu_loop_burst needs no separate empty-counter test.
_Static_assert(PSCU_LOOP_REARM_DEPTH < PSCU_CALIB_TRIGGER_MIN,
               "the re-arm depth must stay below the lowest learned start");

// One disc's session: strings sent, whether its result was shown, and the
// confirmation state.
typedef struct {
  uint8_t injects;
  bool resolved;
  pscu_confirm_t confirm;
} pscu_session_t;

// Since this disc arrived: elapsed ms, whether a valid frame came, and whether
// the window was reached. A window held by the supply guard counts as reached.
typedef struct {
  uint32_t ms;
  bool framed;
  bool armed;
} pscu_since_disc_t;

// The loop's state between passes.
typedef struct {
  pscu_calib_t calib;
  uint8_t counter;
  bool was_gone;
  bool vcd_filter;
  pscu_presence_t presence;
  pscu_since_disc_t since;
  pscu_stealth_t stealth;
  pscu_session_t session;
  pscu_indicator_t indicator;
} pscu_loop_t;

// One pass's inputs.
typedef struct {
  const uint8_t *frame;
  bool captured;
  bool supply_ok;
  uint32_t elapsed_ms;
} pscu_loop_in_t;

// One pass's decisions. store: write the calibration. trim_sample: this frame
// may time the oscillator. quiet: the stealth window is closed, neither open
// nor held, so a record may be stored. The rest is what the pass saw, for the
// trace profile: the window, a program-area frame, the disc gone, the outcome
// shown, and a lead-in that missed the start.
typedef struct {
  bool fire;
  bool store;
  bool trim_sample;
  bool quiet;
  bool indicator_on;
  pscu_window_t window;
  bool program;
  bool disc_gone;
  pscu_indicator_event_t event;
  bool missed;
} pscu_loop_out_t;

pscu_loop_t pscu_loop_init(pscu_calib_t calib, pscu_indicator_t indicator, bool vcd_filter);

// Decide one pass and advance the state in place (see src/loop.c).
pscu_loop_out_t pscu_loop_step(pscu_loop_t *state, const pscu_loop_in_t *in);

#endif
