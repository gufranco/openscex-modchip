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
// so during play DATA is high-Z and the LED off. The first program-area frame
// shows the console accepted the string, and from then on the chip stays silent
// through any later lead-in read until a disc swap, seen as the drive stopping or
// the console stuck at a new check, re-arms it for the next disc. This is the single
// non-terminating loop the firmware is built around; every call inside it is bounded, and the
// watchdog is kicked each pass so a stuck signal resets the chip rather than wedging it.
void pscu_run(void) {
  pscu_board_mode_t board = pscu_engine_detect_board();
  uint8_t counter = 0U;
  uint8_t injects = 0U;
  bool logged = false;
  pscu_stealth_t stealth = pscu_stealth_init();
  pscu_confirm_t confirm = pscu_confirm_init();

  for (;;) {
    uint8_t frame[PSCU_SUBQ_FRAME_BYTES];
    bool captured = pscu_engine_capture_frame(frame);
    pscu_frame_kind_t kind = captured ? pscu_subq_frame_kind(frame) : PSCU_FRAME_SILENT;
    counter = pscu_subq_update_counter(frame, counter, PSCU_VCD_FILTER_ENABLED);
    bool in_window = pscu_should_inject(counter, PSCU_INJECT_TRIGGER);
    pscu_stealth_step_t step = pscu_stealth_step(stealth, in_window, kind, PSCU_STEALTH_STRINGS);
    stealth = step.state;
    if (step.fire) {
      // The first string of an arming starts a new session: a disc swap re-arms
      // the stealth state machine, and that disc's check is recorded on its own
      // rather than lost behind the first session of the power cycle.
      if (step.state.sent == 1U) {
        injects = 0U;
        logged = false;
        confirm = pscu_confirm_init();
      }
      pscu_engine_inject(board);
      if (injects < 0xFFU) {
        injects = (uint8_t)(injects + 1U);
      }
    }
    // Closed-loop confirmation. After injecting, watch for the program area: the
    // mechacon re-enables reads only once it accepts the region string, so a
    // program-area frame confirms the check passed. The pure FSM resolves on that
    // or after a bounded idle wait, and each session is recorded once, off the
    // injection path, so the write never costs injection timing.
    if ((injects > 0U) && !logged) {
      pscu_confirm_step_t outcome = pscu_confirm_step(
          confirm, !step.fire, pscu_subq_is_program_area(frame), PSCU_CONFIRM_FRAMES);
      confirm = outcome.state;
      if (outcome.resolved) {
        pscu_engine_log_session(board, injects, outcome.confirmed ? 1U : 0U);
        logged = true;
      }
    }
    pscu_port_watchdog_reset();
  }
}
