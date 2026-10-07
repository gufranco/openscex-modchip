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

// One disc's session: how many strings were emitted for it, whether its record
// has been written, and the confirmation state that decides what is written.
typedef struct {
  uint8_t injects;
  bool logged;
  pscu_confirm_t confirm;
} pscu_session_t;

static pscu_session_t pscu_session_init(void) {
  pscu_session_t session = { 0U, false, pscu_confirm_init() };
  return session;
}

// Count one emitted string. The first string of an arming starts a new session:
// a disc swap re-arms the stealth state machine, and that disc's check is
// recorded on its own rather than lost behind the first session of the power
// cycle.
static pscu_session_t pscu_session_fired(pscu_session_t session, uint8_t sent) {
  pscu_session_t next = (sent == 1U) ? pscu_session_init() : session;
  if (next.injects < 0xFFU) {
    next.injects = (uint8_t)(next.injects + 1U);
  }
  return next;
}

// Closed-loop confirmation. After injecting, watch for the program area: the
// mechacon re-enables reads only once it accepts the region string, so a
// program-area frame confirms the check passed. The pure FSM resolves on that
// or after a bounded idle wait. An open lid also resolves it: the disc is
// leaving, and waiting out the idle frames could let the next disc's first
// string start a new session before this one is written, losing exactly the
// failed checks the recorder exists for. Each session is recorded once, off the
// injection path, so the write never costs injection timing.
static pscu_session_t pscu_session_watch(
    pscu_session_t session, pscu_board_mode_t board, bool fired, bool program, bool lid_open) {
  pscu_session_t next = session;
  if ((session.injects > 0U) && !session.logged) {
    pscu_confirm_step_t outcome =
        pscu_confirm_step(session.confirm, !fired, program, PSCU_CONFIRM_FRAMES);
    next.confirm = outcome.state;
    if (outcome.resolved || lid_open) {
      pscu_engine_log_session(board, session.injects, outcome.confirmed ? 1U : 0U);
      next.logged = true;
    }
  }
  return next;
}

// The whole modchip, as one loop. Detect the board era once, then forever:
// read a SUBQ frame, let it move the leaky counter, decide from the counter
// whether the console is in its region-check window, and let the stealth state
// machine decide whether to emit one region string this pass. Inside the window
// it emits up to the cap then goes silent; outside it emits nothing and re-arms,
// so during play DATA is high-Z and the LED off. The first program-area frame
// shows the console accepted the string, and from then on the chip stays silent
// through any later lead-in read until the lid opens; the close re-arms it for
// the next disc, so every swap in a multi-disc game is seen, not inferred. This
// is the single non-terminating loop the firmware is built around; every call
// inside it is bounded, and the watchdog is kicked each pass so a stuck signal
// resets the chip rather than wedging it.
void pscu_run(void) {
  pscu_board_mode_t board = pscu_engine_detect_board();
  uint8_t counter = 0U;
  pscu_stealth_t stealth = pscu_stealth_init();
  pscu_session_t session = pscu_session_init();

  for (;;) {
    uint8_t frame[PSCU_SUBQ_FRAME_BYTES];
    pscu_engine_capture_frame(frame);
    counter = pscu_subq_update_counter(frame, counter, PSCU_VCD_FILTER_ENABLED);
    bool in_window = pscu_should_inject(counter, PSCU_INJECT_TRIGGER);
    bool program = pscu_subq_is_program_area(frame);
    bool lid_open = pscu_port_read_lid() != 0U;
    pscu_stealth_step_t step =
        pscu_stealth_step(stealth, in_window, program, lid_open, PSCU_STEALTH_STRINGS);
    stealth = step.state;
    if (step.fire) {
      session = pscu_session_fired(session, step.state.sent);
      pscu_engine_inject(board);
    }
    session = pscu_session_watch(session, board, step.fire, program, lid_open);
    pscu_port_watchdog_reset();
  }
}
