// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_TRIM_H
#define PSCU_TRIM_H

#include <stdbool.h>
#include <stdint.h>

// Oscillator trim against the SUBQ frame rate, 75 Hz at single speed (Red
// Book). The RC is +-10 percent from the factory, +-1 percent once trimmed
// (datasheet 2586Q, Table 21-2). Double-speed periods fall outside the window.

// Frame periods per batch: about 0.85 s, 417 Timer1 ticks, so one tick is
// 0.24 percent.
#define PSCU_TRIM_FRAMES ((uint16_t)64U)

// Dead band: within 1 percent changes nothing (datasheet user-calibration
// accuracy).
#define PSCU_TRIM_DEADBAND_DIVISOR ((uint32_t)100U)

// Most OSCCAL may move from factory: half the datasheet's 0x20, keeping under
// the 8.8 MHz EEPROM write limit.
#define PSCU_TRIM_MAX_OFFSET ((int8_t)16)

// Largest step per batch, in notches: one per percent of error past the dead
// band. A notch is 0.5 to 1 percent (datasheet curve); writes go one notch at a
// time, under the datasheet's 2 percent per-cycle limit.
#define PSCU_TRIM_MAX_STEP ((int8_t)4)

// CAL7 selects one of two overlapping ranges; a trim never changes it.
#define PSCU_TRIM_RANGE_BIT ((uint8_t)0x80U)

// From F_CPU: the accepted tick range for one frame period and the expected
// tick sum for a batch.
typedef struct {
  uint32_t low;
  uint32_t high;
  uint32_t expected;
} pscu_trim_ref_t;

// The batch so far: how many periods were summed and their total ticks.
typedef struct {
  uint16_t frames;
  uint32_t ticks;
} pscu_trim_t;

// The new batch and the OSCCAL change it asks for; negative when fast.
typedef struct {
  pscu_trim_t state;
  int8_t adjust;
} pscu_trim_step_t;

pscu_trim_t pscu_trim_init(void);

// Fold one frame period in; ignored unless sample is set and delta is in range.
// A full batch is judged and starts over.
pscu_trim_step_t pscu_trim_step(pscu_trim_t state,
                                bool sample,
                                uint16_t delta,
                                pscu_trim_ref_t ref);

// current moved by adjust, clamped to PSCU_TRIM_MAX_OFFSET of factory and to
// the factory's CAL7 range.
uint8_t pscu_trim_apply(uint8_t factory, uint8_t current, int8_t adjust);

#endif
