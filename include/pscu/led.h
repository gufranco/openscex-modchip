// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_LED_H
#define PSCU_LED_H

#include <stdbool.h>
#include <stdint.h>

// The status LED as a diagnostic channel. One LED, one colour, so meaning is
// carried by how many flashes and how long each lasts, the way laptop power LEDs
// report POST errors as counted blinks. The run loop feeds this pure state
// machine the time elapsed and what happened; it answers whether the LED is lit.
// Nothing here waits: patterns advance with the hardware timer while the loop
// keeps reading SUBQ, so the LED can never delay or gate a feature (AGENTS.md
// rule 7), and with no LED fitted nothing else changes.

// Result and fault codes, each shown as that many long flashes; 7 is the
// highest. The LED keeps no history: a code is about now, except the two boot
// codes. Code 5 comes from the reset-cause flag; code 6 from comparing the
// detected board with the one the calibration record stored, which changes when
// the chip moves to another console or its WFCK wire is intermittent. Code 7 is
// a live fault: the supply measures under 2.9 V, or the measurement failed, and
// no string is sent while it holds.
#define PSCU_LED_CODE_ACCEPTED ((uint8_t)1U)
#define PSCU_LED_CODE_REFUSED ((uint8_t)2U)
#define PSCU_LED_CODE_NO_SQCK ((uint8_t)3U)
#define PSCU_LED_CODE_NO_CHECK ((uint8_t)4U)
#define PSCU_LED_CODE_WATCHDOG ((uint8_t)5U)
#define PSCU_LED_CODE_BOARD_CHANGED ((uint8_t)6U)
#define PSCU_LED_CODE_SUPPLY ((uint8_t)7U)

// Flash timings in milliseconds. Every pattern length is kept apart from the
// length of a region string, 90 to 181 ms depending on the board and WFCK rate,
// so a string flash is never mistaken for a code: board blinks are 300 ms, code
// flashes 700 ms, and the waiting heartbeat a 40 ms blip every 2 s. A code
// repeats after a 2 s pause, three times for a disc result, so it can be counted
// without a stopwatch, and then the LED goes dark for play.
#define PSCU_LED_SHORT_MS ((uint32_t)300U)
#define PSCU_LED_LONG_MS ((uint32_t)700U)
#define PSCU_LED_GAP_MS ((uint32_t)300U)
#define PSCU_LED_PAUSE_MS ((uint32_t)2000U)
#define PSCU_LED_BEAT_ON_MS ((uint32_t)40U)
#define PSCU_LED_BEAT_PERIOD_MS ((uint32_t)2000U)
#define PSCU_LED_RESULT_REPEATS ((uint32_t)3U)

// Fault thresholds. No valid SUBQ frame at all for 5 s after power-on points at
// the SQCK, SUBQ or power wiring, or at a console with no disc in; once any
// frame has arrived, silence just means no disc and is not a fault. Valid
// frames that never show a region check for 20 s since this disc arrived point
// at the SUBQ wiring, or at a disc with no data lead-in such as an audio CD.
// Both are Concluded: a console reads the lead-in within a few seconds of
// spin-up.
#define PSCU_LED_NO_SQCK_MS ((uint32_t)5000U)
// Code 3 is an install check: it shows for three of its cycles, 15 s, and then
// gives way to the heartbeat, so a console left on with no disc does not flash
// a fault for ever.
#define PSCU_LED_NO_SQCK_UNTIL_MS ((uint32_t)20000U)
#define PSCU_LED_NO_CHECK_MS ((uint32_t)20000U)

typedef enum {
  PSCU_LED_BOARD = 0,
  PSCU_LED_REPLAY = 1,
  PSCU_LED_WAIT = 2,
  PSCU_LED_INJECT = 3,
  PSCU_LED_CODE = 4,
  PSCU_LED_DARK = 5
} pscu_led_stage_t;

// What happened this pass, from the run loop: a region string was sent, or the
// disc's session resolved as accepted or refused.
typedef enum {
  PSCU_LED_EVENT_NONE = 0,
  PSCU_LED_EVENT_FIRED = 1,
  PSCU_LED_EVENT_ACCEPTED = 2,
  PSCU_LED_EVENT_REFUSED = 3
} pscu_led_event_t;

// stage is where the display is; code the number of long flashes it shows in a
// code stage; live marks a fault code that repeats for as long as the fault
// holds; board the 1 or 2 short boot blinks; replay a code shown once at boot,
// 6 after a watchdog reset, 7 after a board change, 0 otherwise; phase_ms the
// time spent in the current stage.
typedef struct {
  pscu_led_stage_t stage;
  uint8_t code;
  bool live;
  uint8_t board;
  uint8_t replay;
  uint32_t phase_ms;
} pscu_led_t;

typedef struct {
  pscu_led_t state;
  bool on;
} pscu_led_step_t;

// Start the display after board detection: board short blinks (1 for a static
// gate, 2 for a WFCK carrier), then the boot code once if any, then the waiting
// heartbeat.
pscu_led_t pscu_led_init(uint8_t board_blinks, uint8_t replay_code);

// A new disc has arrived: a result still showing belonged to the last one, so
// the display drops back to the heartbeat, as it does from a string or the dark
// stage. The result stays up until then, so a refused disc's code 2 remains
// readable while the console idles after it. Boot blinks, the boot replay, a
// live fault and the heartbeat itself are left alone.
pscu_led_t pscu_led_disc_arrived(pscu_led_t state);

// Advance by elapsed_ms, apply this pass's event and the live fault (0 for
// none), and say whether the LED is lit.
pscu_led_step_t pscu_led_step(pscu_led_t state,
                              pscu_led_event_t event,
                              uint8_t fault,
                              uint32_t elapsed_ms);

// The live fault, if any, most urgent first: the supply is low or unmeasurable
// (supply_low true); no valid frame since power-on, from PSCU_LED_NO_SQCK_MS to
// PSCU_LED_NO_SQCK_UNTIL_MS (seen false, quiet_ms the time since power-on); or
// valid frames but no region check since this disc arrived (framed true, armed
// false, since_ms the time since the disc arrived, or since power-on).
uint8_t pscu_led_fault(
    bool supply_low, bool seen, uint32_t quiet_ms, uint32_t since_ms, bool framed, bool armed);

#endif
