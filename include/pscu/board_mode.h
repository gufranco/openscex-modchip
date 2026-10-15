// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_BOARD_MODE_H
#define PSCU_BOARD_MODE_H

#include <stdint.h>

// GATE: PU-7..PU-20, WFCK is a static gate and a one is sent as high-Z.
// WFCK: PU-22 and later, WFCK is a carrier and a one mirrors it.
typedef enum { PSCU_BOARD_MODE_GATE = 0, PSCU_BOARD_MODE_WFCK = 1 } pscu_board_mode_t;

// Detector state. `pulses` counts falling edges in the current run; a gap of
// more than `gap_max` samples starts a new run, so stray edges never add up.
// The caller sets gap_max from its clock: the samples in 360 us, the gap
// MultiMode 3 and Mayumi V4 allow between two edges of one carrier run.
// `carrier` latches once a run reaches `needed`. `since` counts samples since
// the last edge.
typedef struct {
  uint8_t pulses;
  uint8_t needed;
  uint8_t carrier;
  uint8_t since;
  uint8_t prev_high;
  uint8_t gap_max;
} pscu_board_detect_t;

// Init with the edge run that proves a carrier and the longest gap inside a
// run, step once per WFCK sample, then classify. needed = 0 forces carrier
// mode, used by the simulation.
pscu_board_detect_t pscu_board_detect_init(uint8_t needed, uint8_t gap_max);

pscu_board_detect_t pscu_board_detect_step(pscu_board_detect_t state, uint8_t wfck_sample);

pscu_board_mode_t pscu_board_detect_mode(pscu_board_detect_t state);

#endif
