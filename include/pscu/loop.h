// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_LOOP_H
#define PSCU_LOOP_H

#include <stdbool.h>
#include <stdint.h>

#include "pscu/calib.h"
#include "pscu/inject.h"
#include "pscu/led.h"

// Every decision one pass of the run loop makes, as a pure function of the
// pass's inputs and the loop state, so the host suite and mutation testing
// cover the wiring between the modules and not only the modules. The run loop
// keeps only what touches the chip: capturing the frame, reading the timer and
// the supply, injecting, writing EEPROM and OSCCAL, and lighting the LED.

// The trim samples only a disc that has been spinning for a second, so the
// frame rate of a drive still spinning up after a swap never skews a batch. A
// design choice: the drive locks its speed before it reads the lead-in.
#define PSCU_LOOP_TRIM_SETTLE_MS ((uint32_t)1000U)

// How far past the learned start the SUBQ counter may climb. Every missed pass
// takes one step off it, so after a disc swap the window closes within this
// many passes plus one: a lid opened on a disc the console refused, still
// sitting in the lead-in with its cap spent, stops the frames, and the counter
// falls below the start in about half a second of failed captures, well before
// the 1.5 s disc-gone bound, so even a quick swap gets a fresh burst. The
// headroom still rides out a run of bad frames inside the lead-in, where at
// least 9 of every 10 frames are mode 1 (pscu/subq.h). Against it, a counter
// free to reach 255 kept a refused lead-in's window open for 3.3 s of misses.
// A design choice, Unknown until hardware: no source gives a console figure
// for how long a swap keeps the drive stopped.
#define PSCU_LOOP_COUNTER_HEADROOM ((uint8_t)8U)

// One disc's session: how many strings were emitted for it, whether its result
// has been shown, and the confirmation state that decides which result it is.
typedef struct {
  uint8_t injects;
  bool resolved;
  pscu_confirm_t confirm;
} pscu_session_t;

// Time and evidence since this disc arrived (or since boot): how long, whether
// any valid frame arrived, and whether the region-check window was reached or
// the console accepted. A window the supply guard kept shut still counts as
// reached: the console did run its check, and the chip held back for its own
// supply, so the disc is neither a missed window that should move the trigger
// nor a disc with no check. The LED's region-check fault and the missed-window
// test are judged on it.
typedef struct {
  uint32_t ms;
  bool framed;
  bool armed;
} pscu_since_disc_t;

// The loop's state between passes.
// vcd_filter selects the SCPH-5903 lead-in rule; the firmware fixes it at build
// time and the host suite drives both values.
typedef struct {
  pscu_calib_t calib;
  uint8_t counter;
  bool was_gone;
  bool vcd_filter;
  pscu_presence_t presence;
  pscu_since_disc_t since;
  pscu_stealth_t stealth;
  pscu_session_t session;
  pscu_led_t led;
} pscu_loop_t;

// One pass's inputs: the frame as captured (filled with a failed pattern when
// the capture failed), whether the capture succeeded, whether the supply reads
// good, and the milliseconds since the last pass.
typedef struct {
  const uint8_t *frame;
  bool captured;
  bool supply_ok;
  uint32_t elapsed_ms;
} pscu_loop_in_t;

// One pass's decisions; the step updates the loop state itself. fire asks for one region string
// now; store asks for the calibration in state to be written; trim_sample says this frame may time
// the oscillator; quiet says the window is not reached, so no arming is under way, since a closed
// window always empties the burst, and a trim may be stored; led_on is the LED level for the rest
// of the pass.
typedef struct {
  bool fire;
  bool store;
  bool trim_sample;
  bool quiet;
  bool led_on;
} pscu_loop_out_t;

pscu_loop_t pscu_loop_init(pscu_calib_t calib, pscu_led_t led, bool vcd_filter);

// Decide one pass and move the state to the next one, in place (see src/loop.c
// for why). The order inside matches the bus: the frame first moves the
// presence and the counter, the counter opens or holds the window, the stealth
// state machine decides a string, the session and the calibration learn from
// the result, and the LED shows it.
pscu_loop_out_t pscu_loop_step(pscu_loop_t *state, const pscu_loop_in_t *in);

#endif
