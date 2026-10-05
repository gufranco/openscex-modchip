// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "pscu/run.h"

#include <stdbool.h>
#include <stdint.h>

#include "port/port.h"
#include "pscu/board_mode.h"
#include "pscu/config.h"
#include "pscu/engine.h"
#include "pscu/inject.h"
#include "pscu/subq.h"

// The whole modchip, as one loop. Detect the board era once, then forever:
// read a SUBQ frame, let it move the leaky counter, decide from the counter
// whether the console is in its region-check window, and let the stealth state
// machine decide whether to emit one region string this pass. Inside the window
// it emits up to the cap then goes silent; outside it emits nothing and re-arms,
// so during play DATA is high-Z and the LED off, and a disc change re-reads the
// lead-in and triggers again. This is the single non-terminating loop the
// firmware is built around; every call inside it is bounded, and the watchdog
// is kicked each pass so a stuck signal resets the chip rather than wedging it.
void pscu_run(void) {
  pscu_board_mode_t board = pscu_engine_detect_board();
  uint8_t counter = 0U;
  uint8_t injects = 0U;
  bool logged = false;
  pscu_stealth_t stealth = pscu_stealth_init();

  for (;;) {
    uint8_t frame[PSCU_SUBQ_FRAME_BYTES];
    pscu_engine_capture_frame(frame);
    counter = pscu_subq_update_counter(frame, counter);
    bool in_window = pscu_should_inject(counter, PSCU_INJECT_TRIGGER);
    pscu_stealth_step_t step =
        pscu_stealth_step(stealth, in_window, PSCU_STEALTH_STRINGS);
    stealth = step.state;
    if (step.fire) {
      pscu_engine_inject(board);
      if (injects < 0xFFU) {
        injects = (uint8_t)(injects + 1U);
      }
    }
    // Once injection has happened and the chip falls silent, write the flight
    // recorder exactly once. Deferring to this idle point keeps the EEPROM
    // latency out of the injection window and spends one write per power cycle,
    // so the recorder never costs injection timing or EEPROM endurance.
    if (!step.fire && !logged && (injects > 0U)) {
      pscu_engine_log_session(board, injects);
      logged = true;
    }
    pscu_port_watchdog_reset();
  }
}
