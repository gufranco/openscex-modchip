// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_BOARD_MODE_H
#define PSCU_BOARD_MODE_H

#include <stdint.h>

// GATE: PU-7..PU-20, WFCK is a static gate and a one is sent as high-Z.
// WFCK: PU-22 and later, WFCK is a carrier and a one mirrors it.
typedef enum { PSCU_BOARD_MODE_GATE = 0, PSCU_BOARD_MODE_WFCK = 1 } pscu_board_mode_t;

// Most samples between two falling edges of one carrier run: 360 us, the gap
// MultiMode 3 and Mayumi V4 allow, at 42 cycles a sample, 5.25 us at 8 MHz
// (measured in simavr; scenario_detect_timing fails if it moves). A 7.35 kHz
// carrier falls every 136 us, about 26 samples.
#define PSCU_BOARD_EDGE_GAP_MAX ((uint8_t)69U)

// Detector state. `pulses` counts falling edges in the current run; a gap over
// PSCU_BOARD_EDGE_GAP_MAX starts a new run, so stray edges never add up.
// `carrier` latches once a run reaches `needed`. `since` counts samples since
// the last edge.
typedef struct {
  uint8_t pulses;
  uint8_t needed;
  uint8_t carrier;
  uint8_t since;
  uint8_t prev_high;
} pscu_board_detect_t;

// Init with the edge run that proves a carrier, step once per WFCK sample,
// then classify. needed = 0 forces carrier mode, used by the simulation.
pscu_board_detect_t pscu_board_detect_init(uint8_t needed);

pscu_board_detect_t pscu_board_detect_step(pscu_board_detect_t state, uint8_t wfck_sample);

pscu_board_mode_t pscu_board_detect_mode(pscu_board_detect_t state);

#endif
