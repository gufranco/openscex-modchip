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

// Streaming detector state: `pulses` counts high-to-low WFCK edges seen so far,
// `prev_high` remembers the last level so only transitions are counted. Passed
// by value so the detector stays pure and needs no storage of its own.
typedef struct {
  uint8_t pulses;
  uint8_t prev_high;
} pscu_board_detect_t;

// Fold a stream of WFCK samples into the detector, then read off the verdict:
// init to a fresh state, step once per sample, and classify once the window is
// done against the pulse count that separates a live clock from a static gate.
pscu_board_detect_t pscu_board_detect_init(void);

pscu_board_detect_t pscu_board_detect_step(pscu_board_detect_t state, uint8_t wfck_sample);

pscu_board_mode_t pscu_board_detect_mode(pscu_board_detect_t state, uint8_t low_pulses_needed);

#endif
