// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_INDICATOR_H
#define PSCU_INDICATOR_H

#include <stdbool.h>
#include <stdint.h>

// The status indicator: codes are counted flashes. The run loop passes elapsed time
// and events; this answers whether the indicator is lit. Nothing here waits, so the
// indicator never delays a feature.

// Result and fault codes, shown as that many long flashes. 5 and 6 are boot
// codes; 7 holds while the supply is low and no string is sent.
#define PSCU_INDICATOR_CODE_ACCEPTED ((uint8_t)1U)
#define PSCU_INDICATOR_CODE_REFUSED ((uint8_t)2U)
#define PSCU_INDICATOR_CODE_NO_SQCK ((uint8_t)3U)
#define PSCU_INDICATOR_CODE_NO_CHECK ((uint8_t)4U)
#define PSCU_INDICATOR_CODE_WATCHDOG ((uint8_t)5U)
#define PSCU_INDICATOR_CODE_BOARD_CHANGED ((uint8_t)6U)
#define PSCU_INDICATOR_CODE_SUPPLY ((uint8_t)7U)

// Flash timings in ms, chosen to differ from a string's 90 to 181 ms flash. A
// disc result repeats three times, then the indicator goes dark.
#define PSCU_INDICATOR_SHORT_MS ((uint32_t)300U)
#define PSCU_INDICATOR_LONG_MS ((uint32_t)700U)
#define PSCU_INDICATOR_GAP_MS ((uint32_t)300U)
#define PSCU_INDICATOR_PAUSE_MS ((uint32_t)2000U)
#define PSCU_INDICATOR_BEAT_ON_MS ((uint32_t)40U)
#define PSCU_INDICATOR_BEAT_PERIOD_MS ((uint32_t)2000U)
#define PSCU_INDICATOR_RESULT_REPEATS ((uint32_t)3U)

// Fault thresholds, chosen. Code 3: no valid frame 5 s after power-on (wiring
// or no disc). Code 4: frames but no region check 20 s after a disc arrives.
#define PSCU_INDICATOR_NO_SQCK_MS ((uint32_t)5000U)
// Code 3 stops after 15 s, so an idle console does not flash forever.
#define PSCU_INDICATOR_NO_SQCK_UNTIL_MS ((uint32_t)20000U)
#define PSCU_INDICATOR_NO_CHECK_MS ((uint32_t)20000U)

// Build-time profile for PB3 (LED or active buzzer). Debug shows everything;
// final chirps at power-on and on an accepted disc, and reports each fault once.
typedef enum {
  PSCU_INDICATOR_PROFILE_DEBUG = 0,
  PSCU_INDICATOR_PROFILE_FINAL = 1
} pscu_indicator_profile_t;

// Final-profile timings. 60 ms is many cycles of the buzzer's 3.5 kHz tone (PUI
// AI-3035-TWT-3V-R datasheet). A low supply repeats every 30 s.
#define PSCU_INDICATOR_CHIRP_MS ((uint32_t)60U)
#define PSCU_INDICATOR_FINAL_REFUSED_REPEATS ((uint32_t)2U)
#define PSCU_INDICATOR_SUPPLY_BACKOFF_MS ((uint32_t)30000U)

// Debug tick when OSCCAL moves or the record is written. Shown only in the dark
// stage, so it never lands inside a code or a string flash.
#define PSCU_INDICATOR_TICK_MS ((uint32_t)40U)

typedef enum {
  PSCU_INDICATOR_BOARD = 0,
  PSCU_INDICATOR_REPLAY = 1,
  PSCU_INDICATOR_WAIT = 2,
  PSCU_INDICATOR_INJECT = 3,
  PSCU_INDICATOR_CODE = 4,
  PSCU_INDICATOR_DARK = 5
} pscu_indicator_stage_t;

// What happened this pass.
typedef enum {
  PSCU_INDICATOR_EVENT_NONE = 0,
  PSCU_INDICATOR_EVENT_FIRED = 1,
  PSCU_INDICATOR_EVENT_ACCEPTED = 2,
  PSCU_INDICATOR_EVENT_REFUSED = 3
} pscu_indicator_event_t;

// live: a fault code that repeats while it holds. replay: a boot code, or 0.
// told: faults the final profile already showed for this disc, a bit per code.
// In the dark stage code is 1 while a debug tick shows.
typedef struct {
  pscu_indicator_stage_t stage;
  uint8_t code;
  bool live;
  uint8_t board;
  uint8_t replay;
  uint32_t phase_ms;
  pscu_indicator_profile_t profile;
  uint8_t told;
} pscu_indicator_t;

typedef struct {
  pscu_indicator_t state;
  bool on;
} pscu_indicator_step_t;

// Start after board detection: 1 blink for a gate or 2 for a carrier, the boot
// code if any, then the heartbeat.
pscu_indicator_t pscu_indicator_init(uint8_t board_blinks,
                                     uint8_t replay_code,
                                     pscu_indicator_profile_t profile);

// A new disc arrived: drop a shown result back to the heartbeat and forget
// which faults were told. Boot patterns and live faults are left alone.
pscu_indicator_t pscu_indicator_disc_arrived(pscu_indicator_t state);

// OSCCAL moved or the record was written: start a debug tick if dark.
pscu_indicator_t pscu_indicator_note(pscu_indicator_t state);

// Advance by elapsed_ms, apply this pass's event and the live fault (0 for
// none), and say whether the indicator is lit.
pscu_indicator_step_t pscu_indicator_step(pscu_indicator_t state,
                                          pscu_indicator_event_t event,
                                          uint8_t fault,
                                          uint32_t elapsed_ms);

// The live fault, most urgent first: low supply, no valid frame since power-on,
// or frames but no region check since this disc arrived. 0 for none.
uint8_t pscu_indicator_fault(
    bool supply_low, bool seen, uint32_t quiet_ms, uint32_t since_ms, bool framed, bool armed);

#endif
