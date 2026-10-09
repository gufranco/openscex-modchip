// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_TRIM_H
#define PSCU_TRIM_H

#include <stdbool.h>
#include <stdint.h>

// Oscillator trim. The chip runs from its internal RC oscillator, factory
// calibrated to +-10 percent and user-calibratable to +-1 percent (Read:
// ATtiny25/45/85 datasheet 2586Q, Table 21-2). A legacy-board bit cell
// is an RC-timed 4 ms delay, so the chip calibrates itself against the one
// crystal-locked rate it already sees: SUBQ frames, 75 per second at single
// speed (Read: Red Book, 75 sectors per second). The lead-in is read at single
// speed (Concluded: WFCK runs at about 7.3 kHz during the protection phase and
// doubles for data reads, PsNee V9.0 PSNee.ino:366-368), so the time between
// two consecutive valid frames is sampled in the lead-in and in play alike, and
// a double-speed period, half as long, falls outside the accept window. The sum
// over a batch of samples, against what a nominal 8 MHz clock would count, says
// whether the RC runs fast or slow.

// Samples per decision: 64 frame periods, about 0.85 s at single speed. Timer1 at
// clk/16384 counts about 417 ticks over them, so one tick is 0.24 percent, well
// inside the dead band.
#define PSCU_TRIM_FRAMES ((uint16_t)64U)

// Dead band: a batch within 1 percent of nominal changes nothing, the accuracy
// the datasheet gives for user calibration, so a trimmed chip stops moving.
#define PSCU_TRIM_DEADBAND_DIVISOR ((uint32_t)100U)

// OSCCAL may move at most this far from the factory value. The datasheet allows
// changes of up to 0x20 per calibration, made in small steps; half of that keeps
// every trim well inside it, and with the 8.8 MHz ceiling for EEPROM writes.
#define PSCU_TRIM_MAX_OFFSET ((int8_t)16)

// Largest step one batch may ask for, in OSCCAL notches. Past the dead band the
// verdict asks for one notch per band of error left outside the dead band, so a chip 5 percent off
// moves most of the way in its first batch instead of one notch per batch. The
// datasheet gives a notch only as a curve (ATtiny25/45/85 datasheet 2586Q, the
// calibrated 8 MHz oscillator against OSCCAL), roughly 0.5 to 1 percent, so a
// step of one notch per percent undershoots on a fine part and overshoots by
// under half on a coarse one, and either way settles. The firmware writes the
// step one notch at a time: the same datasheet warns that a change of more
// than 2 percent from one clock cycle to the next can upset the core (OSCCAL
// register description), and one notch stays well under it.
#define PSCU_TRIM_MAX_STEP ((int8_t)4)

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
// would leave either bound stops at that bound.
uint8_t pscu_trim_apply(uint8_t factory, uint8_t current, int8_t adjust);

#endif
