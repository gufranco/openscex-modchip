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
#include "pscu/led.h"
#include "pscu/subq.h"

// Console clocks per millisecond, the divisor that turns Timer1 ticks (1024
// clocks each) into milliseconds for the LED. Integer division drops 0.6 of a
// clock per millisecond at 4.2336 MHz, a 0.014 percent error no eye can see.
#define PSCU_CLOCKS_PER_MS (F_CPU / 1000UL)
#define PSCU_CLOCKS_PER_TICK (1024UL)

// One disc's session: how many strings were emitted for it, whether its result
// has been shown, and the confirmation state that decides which result it is.
typedef struct {
  uint8_t injects;
  bool resolved;
  pscu_confirm_t confirm;
} pscu_session_t;

typedef struct {
  pscu_session_t session;
  pscu_led_event_t event;
} pscu_session_step_t;

// Time and evidence since the lid last closed (or since boot): the LED's fault
// codes are judged against them.
typedef struct {
  uint32_t ms;
  bool framed;
  bool armed;
} pscu_since_close_t;

// The last Timer1 reading and the clocks not yet counted as a whole millisecond,
// so rounding never accumulates into drift.
typedef struct {
  uint16_t last;
  uint32_t carry;
} pscu_clock_t;

typedef struct {
  pscu_clock_t clock;
  uint32_t elapsed_ms;
} pscu_clock_step_t;

static pscu_session_t pscu_session_init(void) {
  pscu_session_t session = { 0U, false, pscu_confirm_init() };
  return session;
}

// Count one emitted string. The first string of an arming starts a new session:
// a disc swap re-arms the stealth state machine, and that disc's check is judged
// on its own rather than lost behind the first session of the power cycle.
static pscu_session_t pscu_session_fired(pscu_session_t session, uint8_t sent) {
  pscu_session_t next = (sent == 1U) ? pscu_session_init() : session;
  if (next.injects < 0xFFU) {
    next.injects = (uint8_t)(next.injects + 1U);
  }
  return next;
}

// Closed-loop confirmation. After injecting, watch for the program area: the
// mechacon re-enables reads only once it accepts the region string, so a
// program-area frame confirms the check passed and the LED shows code 1. The FSM
// otherwise resolves after a bounded idle wait, and an open lid resolves it at
// once, both as refused, code 2, so a failed disc is reported before the next
// disc's first string starts a new session.
static pscu_session_step_t pscu_session_watch(pscu_session_t session,
                                              bool fired,
                                              bool program,
                                              bool lid_open) {
  pscu_session_step_t out;
  out.session = session;
  out.event = fired ? PSCU_LED_EVENT_FIRED : PSCU_LED_EVENT_NONE;
  if ((session.injects > 0U) && !session.resolved) {
    pscu_confirm_step_t outcome =
        pscu_confirm_step(session.confirm, !fired, program, PSCU_CONFIRM_FRAMES);
    out.session.confirm = outcome.state;
    if (outcome.resolved || lid_open) {
      out.session.resolved = true;
      out.event = outcome.confirmed ? PSCU_LED_EVENT_ACCEPTED : PSCU_LED_EVENT_REFUSED;
    }
  }
  return out;
}

// Milliseconds since the last pass, from the free-running Timer1. Unsigned
// subtraction handles the 16-bit wrap, since no pass comes near its 15.8 s.
static pscu_clock_step_t pscu_clock_step(pscu_clock_t clock) {
  uint16_t now = pscu_port_ticks();
  uint32_t ticks = (uint32_t)(uint16_t)(now - clock.last);
  uint32_t clocks = (ticks * PSCU_CLOCKS_PER_TICK) + clock.carry;
  pscu_clock_step_t out;
  out.clock.last = now;
  out.clock.carry = clocks % PSCU_CLOCKS_PER_MS;
  out.elapsed_ms = clocks / PSCU_CLOCKS_PER_MS;
  return out;
}

// An open lid starts the count over; a closed one accumulates time, whether any
// whole frame arrived, and whether the chip armed or the console accepted.
static pscu_since_close_t pscu_since_close_step(
    pscu_since_close_t since, bool lid_open, uint32_t elapsed_ms, bool captured, bool armed) {
  pscu_since_close_t next = { 0U, false, false };
  if (!lid_open) {
    uint32_t room = 0xFFFFFFFFUL - since.ms;
    next.ms = (elapsed_ms < room) ? (since.ms + elapsed_ms) : 0xFFFFFFFFUL;
    next.framed = since.framed || captured;
    next.armed = since.armed || armed;
  }
  return next;
}

// Draw this pass's LED state. Injection drives the LED itself for each string;
// between strings and the rest of the time the pattern player decides.
static pscu_led_t pscu_led_show(pscu_led_t led,
                                pscu_led_event_t event,
                                uint8_t fault,
                                uint32_t elapsed_ms) {
  pscu_led_step_t shown = pscu_led_step(led, event, fault, elapsed_ms);
  if (shown.on) {
    pscu_port_led_on();
  } else {
    pscu_port_led_off();
  }
  return shown.state;
}

// The whole modchip, as one loop. Detect the board era once, then forever:
// read a SUBQ frame, let it move the leaky counter, decide from the counter
// whether the console is in its region-check window, and let the stealth state
// machine decide whether to emit one region string this pass. Inside the window
// it emits up to the cap then goes silent; outside it emits nothing and re-arms,
// so during play DATA is high-Z. The first program-area frame shows the console
// accepted the string, and from then on the chip stays silent through any later
// lead-in read until the lid opens; the close re-arms it for the next disc. The
// LED reports each stage and result from the same facts without ever waiting.
// Every call inside the loop is bounded, and the watchdog is kicked each pass so
// a stuck signal resets the chip rather than wedging it.
void pscu_run(void) {
  pscu_board_mode_t board = pscu_engine_detect_board();
  uint8_t boot_code = (pscu_port_reset_was_watchdog() != 0U) ? PSCU_LED_CODE_WATCHDOG : 0U;
  pscu_led_t led = pscu_led_init((board == PSCU_BOARD_MODE_WFCK) ? 2U : 1U, boot_code);
  pscu_clock_t clock = { pscu_port_ticks(), 0U };
  pscu_since_close_t since = { 0U, false, false };
  uint8_t counter = 0U;
  pscu_stealth_t stealth = pscu_stealth_init();
  pscu_session_t session = pscu_session_init();

  for (;;) {
    uint8_t frame[PSCU_SUBQ_FRAME_BYTES];
    bool captured = pscu_engine_capture_frame(frame);
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
    pscu_session_step_t watched = pscu_session_watch(session, step.fire, program, lid_open);
    session = watched.session;
    pscu_clock_step_t tick = pscu_clock_step(clock);
    clock = tick.clock;
    since = pscu_since_close_step(since, lid_open, tick.elapsed_ms, captured, step.fire || program);
    uint8_t fault = pscu_led_fault(lid_open, since.ms, since.framed, since.armed);
    led = pscu_led_show(led, watched.event, fault, tick.elapsed_ms);
    pscu_port_watchdog_reset();
  }
}
