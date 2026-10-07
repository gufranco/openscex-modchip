// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_TRIM_H
#define PSCU_TRIM_H

#include <stdbool.h>
#include <stdint.h>

// Oscillator trim. The chip runs from its internal RC oscillator, factory
// calibrated to +-10 percent and user-calibratable to +-1 percent (Read:
// ATtiny24A/44A/84A datasheet DS40002269A, Table 20-2). A legacy-board bit cell
// is an RC-timed 4 ms delay, so the chip calibrates itself against the one
// crystal-locked rate it already sees: SUBQ frames, 75 per second at single
// speed (Read: Red Book, 75 sectors per second). The lead-in is read at single
// speed (Concluded: WFCK runs at about 7.3 kHz during the protection phase and
// doubles for data reads, PsNee V9.0 PSNee.ino:366-368), so only the time
// between two consecutive lead-in frames is sampled. The sum over a batch of
// samples, against what a nominal 8 MHz clock would count, says whether the RC
// runs fast or slow.

// Samples per decision: 64 frame periods, about 0.85 s of lead-in. Timer1 at
// clk/1024 counts about 6667 ticks over them, so one tick is 0.015 percent.
#define PSCU_TRIM_FRAMES ((uint16_t)64U)

// Dead band: a batch within 1 percent of nominal changes nothing, the accuracy
// the datasheet gives for user calibration, so a trimmed chip stops moving.
#define PSCU_TRIM_DEADBAND_DIVISOR ((uint32_t)100U)

// OSCCAL may move at most this far from the factory value. The datasheet allows
// changes of up to 0x20 per calibration, made in small steps; half of that keeps
// every trim well inside it, and with the 8.8 MHz ceiling for EEPROM writes.
#define PSCU_TRIM_MAX_OFFSET ((int8_t)16)

// The CAL7 bit picks one of two overlapping frequency ranges; crossing it jumps
// the frequency, so a trim never changes it.
#define PSCU_TRIM_RANGE_BIT ((uint8_t)0x80U)

// The reference, computed by the caller from F_CPU: the shortest and longest
// tick count accepted as one frame period, which rejects a missed frame or a
// double-speed read, and the tick sum a nominal clock counts over a batch.
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

// One step: the new batch, and the OSCCAL change it asks for, -1 when the RC
// runs fast, +1 when slow, 0 otherwise.
typedef struct {
  pscu_trim_t state;
  int8_t adjust;
} pscu_trim_step_t;

pscu_trim_t pscu_trim_init(void);

// Fold one frame period in. sample says delta is the time between two
// consecutive lead-in frames; anything else, or a delta outside the reference
// window, is ignored. A full batch is judged and starts over.
pscu_trim_step_t pscu_trim_step(pscu_trim_t state,
                                bool sample,
                                uint16_t delta,
                                pscu_trim_ref_t ref);

// The OSCCAL value after moving current by adjust, held to within
// PSCU_TRIM_MAX_OFFSET of factory and to the factory's CAL7 range; a move that
// would leave either bound is refused and current is returned.
uint8_t pscu_trim_apply(uint8_t factory, uint8_t current, int8_t adjust);

#endif
