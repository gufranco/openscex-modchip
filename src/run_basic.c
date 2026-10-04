// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include <stdint.h>

#include "port/port.h"
#include "pscu/board_mode.h"
#include "pscu/engine.h"
#include "pscu/inject.h"
#include "pscu/run.h"
#include "pscu/subq.h"

void pscu_run(void) {
  pscu_board_mode_t board = pscu_engine_detect_board();
  uint8_t counter = 0U;

  for (;;) {
    uint8_t frame[PSCU_SUBQ_FRAME_BYTES];
    pscu_engine_capture_frame(frame);
    counter = pscu_subq_update_counter(frame, counter);
    if (pscu_should_inject(counter, PSCU_INJECT_TRIGGER)) {
      pscu_engine_inject(board);
      counter = pscu_counter_after_inject(PSCU_INJECT_TRIGGER, PSCU_INJECT_GAP);
    }
    pscu_port_watchdog_reset();
  }
}
