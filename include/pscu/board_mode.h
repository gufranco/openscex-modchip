// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_BOARD_MODE_H
#define PSCU_BOARD_MODE_H

#include <stdint.h>

// GATE is a legacy board (PU-7..PU-20): WFCK is a static high gate and a logic
// one is injected as high-Z. WFCK is a modern board (PU-22+): WFCK is a live
// carrier and a logic one is injected by mirroring it. This choice is made once
// at boot and drives every injected bit afterwards.
typedef enum { PSCU_BOARD_MODE_GATE = 0, PSCU_BOARD_MODE_WFCK = 1 } pscu_board_mode_t;

// The longest stretch, in samples, between two falling edges that still counts
// as one run of a live clock. A carrier at the 7.35 kHz single-speed rate
// falls every 136 us, about 36 samples of the 30-cycle sampling loop at 8 MHz
// (Concluded: the loop in the ATtiny85 listing, the same figure engine.c uses
// for the guard window), and twice as often at double speed. MultiMode 3 and
// Mayumi V4 accept a carrier only when each half period stays under 100 us,
// a 200 us period (Read: multimode_v3_12c508a_any.asm and mayumi_v4 source,
// the WFCK detect loops). 96 samples, 360 us at that loop cost, keeps the
// same intent with room for a sampling loop that compiles faster: even at 20
// cycles a sample, 240 us, a real carrier still falls well inside it.
#define PSCU_BOARD_EDGE_GAP_MAX ((uint8_t)96U)

// Streaming detector state. `pulses` counts the falling WFCK edges of the
// current run, each within PSCU_BOARD_EDGE_GAP_MAX samples of the last; a
// longer gap starts a new run, so stray edges on a noisy or unconnected line
// never add up the way a clock's do. `needed` is the run that proves a
// carrier and `carrier` latches once a run reaches it, so a carrier that ran
// for long enough and then paused still counts. `since` counts samples since
// the last falling edge, and `prev_high` remembers the last level so only
// transitions are counted. Passed by value so the detector stays pure and
// needs no storage of its own.
typedef struct {
  uint8_t pulses;
  uint8_t needed;
  uint8_t carrier;
  uint8_t since;
  uint8_t prev_high;
} pscu_board_detect_t;

// Fold a stream of WFCK samples into the detector, then read off the verdict:
// init to a fresh state with the run of consecutive edges that separates a
// live clock from a static gate, step once per sample, and classify once the
// window is done. A run of zero forces the carrier mode, which the simulation
// uses to exercise the modern path deterministically.
pscu_board_detect_t pscu_board_detect_init(uint8_t needed);

pscu_board_detect_t pscu_board_detect_step(pscu_board_detect_t state, uint8_t wfck_sample);

pscu_board_mode_t pscu_board_detect_mode(pscu_board_detect_t state);

#endif
