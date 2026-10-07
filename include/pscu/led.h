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

// Result and fault codes, each shown as that many long flashes. Nothing is
// stored: the LED is the whole report, so a code is only ever about now, except
// code 6, which the chip learns at boot from the reset-cause flag.
#define PSCU_LED_CODE_ACCEPTED ((uint8_t)1U)
#define PSCU_LED_CODE_REFUSED ((uint8_t)2U)
#define PSCU_LED_CODE_LID ((uint8_t)3U)
#define PSCU_LED_CODE_NO_SQCK ((uint8_t)4U)
#define PSCU_LED_CODE_NO_CHECK ((uint8_t)5U)
#define PSCU_LED_CODE_WATCHDOG ((uint8_t)6U)

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

// Fault thresholds, measured from boot or from the last lid close. A drive that
// clocks no SUBQ frame at all for 5 s with the lid shut points at the clock or
// SQCK wiring; frames that never show a region check for 20 s point at the SUBQ
// wiring, or at a disc with no data lead-in such as an audio CD. Both are
// Concluded: a console reads the lead-in within a few seconds of spin-up.
#define PSCU_LED_NO_SQCK_MS ((uint32_t)5000U)
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
// 6 after a watchdog reset and 0 otherwise; phase_ms the time spent in the
// current stage.
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

// Advance by elapsed_ms, apply this pass's event and the live fault (0 for
// none), and say whether the LED is lit.
pscu_led_step_t pscu_led_step(pscu_led_t state,
                              pscu_led_event_t event,
                              uint8_t fault,
                              uint32_t elapsed_ms);

// The live fault, if any: the lid is open or its wire is missing; no SUBQ frame
// at all since the lid closed; or frames but no region check since the lid
// closed. ms_since_close counts from boot or the last lid close, framed says a
// whole frame has been captured since then, and armed that a string was sent or
// the console accepted since then.
uint8_t pscu_led_fault(bool lid_open, uint32_t ms_since_close, bool framed, bool armed);

#endif
