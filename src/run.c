// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "pscu/run.h"

#include <stdbool.h>
#include <stdint.h>

#include "port/port.h"
#include "pscu/board_mode.h"
#include "pscu/calib.h"
#include "pscu/config.h"
#include "pscu/engine.h"
#include "pscu/inject.h"
#include "pscu/led.h"
#include "pscu/subq.h"
#include "pscu/supply.h"
#include "pscu/trim.h"

// Clocks per millisecond, the divisor that turns Timer1 ticks (16384 clocks
// each) into milliseconds for the LED; 8000 at 8 MHz, an exact division.
#define PSCU_CLOCKS_PER_MS (F_CPU / 1000UL)
#define PSCU_CLOCKS_PER_TICK (16384UL)

// The trim's reference at the nominal clock. A 75 Hz SUBQ frame spans 6.51
// Timer1 ticks at 8 MHz, so one frame period reads as 6 or 7 ticks, and as 5 to
// 8 with the RC 10 percent slow or fast. The window, 80 to 140 percent of a
// period floored (5 to 9 ticks), keeps all of those, so quantization never
// biases a batch, while rejecting a double-speed read (3 or 4 ticks) and a
// skipped frame (13 or more). A batch of PSCU_TRIM_FRAMES periods counts 417
// ticks on a nominal clock, rounded, so one tick is 0.24 percent.
#define PSCU_TRIM_SUBQ_HZ (75UL)
#define PSCU_TRIM_TICK_HZ (PSCU_CLOCKS_PER_TICK * PSCU_TRIM_SUBQ_HZ)
#define PSCU_TRIM_LOW ((F_CPU * 4UL) / (PSCU_TRIM_TICK_HZ * 5UL))
#define PSCU_TRIM_HIGH ((F_CPU * 7UL) / (PSCU_TRIM_TICK_HZ * 5UL))
// The trim samples only a disc that has been spinning for a second, so the
// frame rate of a drive still spinning up after a swap never skews a batch. A
// design choice: the drive locks its speed before it reads the lead-in.
#define PSCU_TRIM_SETTLE_MS (1000UL)
#define PSCU_TRIM_EXPECTED \
  ((((uint32_t)PSCU_TRIM_FRAMES * F_CPU) + (PSCU_TRIM_TICK_HZ / 2UL)) / PSCU_TRIM_TICK_HZ)

// One disc's session: how many strings were emitted for it, whether its result
// has been shown, and the confirmation state that decides which result it is.
typedef struct {
  uint8_t injects;
  bool resolved;
  pscu_confirm_t confirm;
} pscu_session_t;

// learn marks a session that resolved on its own, by the program area or by the
// bounded wait, and outcome says which; a session ended by the disc leaving
// teaches the calibration nothing.
typedef struct {
  pscu_session_t session;
  pscu_led_event_t event;
  bool learn;
  pscu_calib_outcome_t outcome;
} pscu_session_step_t;

// Time and evidence since this disc arrived (or since boot): how long, whether
// any valid frame arrived, and whether the region-check window was reached or
// the console accepted. A window the supply guard kept shut still counts as
// reached: the console did run its check, and the chip held back for its own
// supply, so the disc is neither a missed window that should move the trigger
// nor a disc with no check. The LED's region-check fault and the missed-window
// test are judged on it.
typedef struct {
  uint32_t ms;
  bool framed;
  bool armed;
} pscu_since_disc_t;

// The last Timer1 reading and the clocks not yet counted as a whole millisecond,
// so rounding never accumulates into drift.
typedef struct {
  uint8_t last;
  uint32_t carry;
} pscu_clock_t;

typedef struct {
  pscu_clock_t clock;
  uint32_t elapsed_ms;
} pscu_clock_step_t;

// The oscillator trim as the loop runs it: the factory OSCCAL value that bounds
// every trim, the value in OSCCAL now, the batch being summed, the Timer1 stamp
// of the last pass and whether that pass read a lead-in frame, and whether a
// trim is waiting to be stored.
typedef struct {
  uint8_t factory;
  uint8_t osccal;
  pscu_trim_t batch;
  uint8_t stamp;
  bool hit;
  bool dirty;
} pscu_osc_t;

typedef struct {
  pscu_osc_t osc;
  pscu_calib_t calib;
} pscu_osc_store_t;

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
// otherwise resolves after a bounded idle wait, and a disc that leaves resolves
// it at once, both as refused, code 2, so a failed disc is reported before the
// next disc's first string starts a new session.
static pscu_session_step_t pscu_session_watch(pscu_session_t session,
                                              bool fired,
                                              bool program,
                                              bool disc_gone) {
  pscu_session_step_t out;
  out.session = session;
  out.event = fired ? PSCU_LED_EVENT_FIRED : PSCU_LED_EVENT_NONE;
  out.learn = false;
  out.outcome = PSCU_CALIB_REFUSED;
  if ((session.injects > 0U) && !session.resolved) {
    pscu_confirm_step_t outcome =
        pscu_confirm_step(session.confirm, !fired, program, PSCU_CONFIRM_FRAMES);
    out.session.confirm = outcome.state;
    if (outcome.resolved || disc_gone) {
      out.session.resolved = true;
      out.event = outcome.confirmed ? PSCU_LED_EVENT_ACCEPTED : PSCU_LED_EVENT_REFUSED;
      out.learn = outcome.resolved && !disc_gone;
      out.outcome = outcome.confirmed ? PSCU_CALIB_ACCEPTED : PSCU_CALIB_REFUSED;
    }
  }
  return out;
}

// Fold this pass's evidence into the calibration. A session that resolved on
// its own teaches the cap and the start; a missed window teaches the start. The
// store writes only bytes that changed, and both cases come after the strings
// have stopped, so the EEPROM write never lands inside the injection window.
static pscu_calib_t pscu_calib_update(pscu_calib_t calib,
                                      pscu_session_step_t watched,
                                      bool missed) {
  pscu_calib_t next = calib;
  if (watched.learn) {
    next = pscu_calib_learn(calib, watched.outcome, watched.session.injects);
    pscu_engine_store_calib(next);
  } else if (missed) {
    next = pscu_calib_learn(calib, PSCU_CALIB_MISSED, 0U);
    pscu_engine_store_calib(next);
  } else {
  }
  return next;
}

// Boot: read the record, fold in the board just detected, and store the result
// (nothing is written when it did not change). The boot code is the watchdog's
// 6 if the last reset came from it, else 7 if the board changed, else none; a
// boot that has both shows 6, since a hang is the more urgent report.
static pscu_calib_boot_t pscu_calib_start(pscu_calib_t stored, pscu_board_mode_t board) {
  uint8_t detected = (board == PSCU_BOARD_MODE_WFCK) ? 1U : 0U;
  pscu_calib_boot_t boot = pscu_calib_boot(stored, detected);
  pscu_engine_store_calib(boot.calib);
  return boot;
}

static pscu_osc_t pscu_osc_init(uint8_t factory) {
  pscu_osc_t osc;
  osc.factory = factory;
  osc.osccal = pscu_port_osccal_read();
  osc.batch = pscu_trim_init();
  osc.stamp = pscu_port_ticks();
  osc.hit = false;
  osc.dirty = false;
  return osc;
}

// Time this pass's frame against the last one. Only two lead-in frames in a row
// make a sample; the batch decides a step, which moves OSCCAL at once, between
// strings, and marks the trim for storing.
static pscu_osc_t pscu_osc_step(pscu_osc_t osc, uint8_t stamp, bool hit) {
  pscu_trim_ref_t ref = { PSCU_TRIM_LOW, PSCU_TRIM_HIGH, PSCU_TRIM_EXPECTED };
  bool sample = osc.hit && hit;
  uint16_t delta = (uint8_t)(stamp - osc.stamp);
  pscu_trim_step_t step = pscu_trim_step(osc.batch, sample, delta, ref);
  pscu_osc_t next = osc;
  next.batch = step.state;
  next.stamp = stamp;
  next.hit = hit;
  next.osccal = pscu_trim_apply(osc.factory, osc.osccal, step.adjust);
  if (next.osccal != osc.osccal) {
    pscu_port_osccal_write(next.osccal);
    next.dirty = true;
  }
  return next;
}

// A new trim reaches EEPROM only while no arming is under way, so the write
// never lands inside the injection window.
static pscu_osc_store_t pscu_osc_store(pscu_osc_t osc, pscu_calib_t calib, bool quiet) {
  pscu_osc_store_t out;
  out.osc = osc;
  out.calib = calib;
  if (osc.dirty && quiet) {
    out.calib.trim = (int8_t)((int16_t)osc.osccal - (int16_t)osc.factory);
    out.osc.dirty = false;
    pscu_engine_store_calib(out.calib);
  }
  return out;
}

// Milliseconds since the last pass, from the free-running Timer1. Unsigned
// subtraction handles the 8-bit wrap, since no pass comes near its 524 ms.
static pscu_clock_step_t pscu_clock_step(pscu_clock_t clock) {
  uint8_t now = pscu_port_ticks();
  uint32_t ticks = (uint8_t)(now - clock.last);
  uint32_t clocks = (ticks * PSCU_CLOCKS_PER_TICK) + clock.carry;
  pscu_clock_step_t out;
  out.clock.last = now;
  out.clock.carry = clocks % PSCU_CLOCKS_PER_MS;
  out.elapsed_ms = clocks / PSCU_CLOCKS_PER_MS;
  return out;
}

// A gone disc starts the count over; a present one accumulates time, whether any
// valid frame arrived, and whether the window was reached or the console accepted.
static pscu_since_disc_t pscu_since_disc_step(
    pscu_since_disc_t since, bool disc_gone, uint32_t elapsed_ms, bool valid, bool armed) {
  pscu_since_disc_t next = { 0U, false, false };
  if (!disc_gone) {
    uint32_t room = 0xFFFFFFFFUL - since.ms;
    next.ms = (elapsed_ms < room) ? (since.ms + elapsed_ms) : 0xFFFFFFFFUL;
    next.framed = since.framed || valid;
    next.armed = since.armed || armed;
  }
  return next;
}

// Draw this pass's LED state. Injection drives the LED itself for each string;
// between strings and the rest of the time the pattern player decides. A missed
// window reads as a refusal: the disc got no string, and the next one will.
static pscu_led_t pscu_led_show(
    pscu_led_t led, pscu_led_event_t event, bool missed, uint8_t fault, uint32_t elapsed_ms) {
  bool none = event == PSCU_LED_EVENT_NONE;
  pscu_led_event_t shown_event = (none && missed) ? PSCU_LED_EVENT_REFUSED : event;
  pscu_led_step_t shown = pscu_led_step(led, shown_event, fault, elapsed_ms);
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
// lead-in read until the disc leaves, which re-arms it for the next disc. With
// no lid wire, the disc leaving is a stretch with no valid SUBQ frame. The
// LED reports each stage and result from the same facts without ever waiting.
// The window opens at the learned trigger and each arming sends at most the
// learned cap; the cap is taken when an arming starts, so a value learned
// mid-window, such as the full cap after a refusal, applies from the next disc
// rather than restarting strings on this one. A gone disc also empties the
// counter, so the next disc's spin-up cannot find the window already open and
// spend strings before its lead-in. The stored oscillator trim is
// applied before anything is timed, and each pass's frame timing refines it.
// Every call inside the loop is bounded, and the watchdog is kicked each pass so
// a stuck signal resets the chip rather than wedging it.
void pscu_run(void) {
  pscu_calib_t stored = pscu_engine_load_calib();
  pscu_osc_t osc = pscu_osc_init(pscu_engine_apply_trim(stored.trim));
  pscu_board_mode_t board = pscu_engine_detect_board();
  bool watchdog = pscu_port_reset_was_watchdog() != 0U;
  pscu_calib_boot_t boot = pscu_calib_start(stored, board);
  pscu_calib_t calib = boot.calib;
  uint8_t changed_code = boot.board_changed ? PSCU_LED_CODE_BOARD_CHANGED : 0U;
  uint8_t boot_code = watchdog ? PSCU_LED_CODE_WATCHDOG : changed_code;
  pscu_led_t led = pscu_led_init((board == PSCU_BOARD_MODE_WFCK) ? 2U : 1U, boot_code);
  uint8_t cap = calib.cap;
  pscu_clock_t clock = { pscu_port_ticks(), 0U };
  pscu_since_disc_t since = { 0U, false, false };
  pscu_presence_t presence = pscu_presence_init();
  bool was_gone = false;
  uint8_t counter = 0U;
  pscu_stealth_t stealth = pscu_stealth_init();
  pscu_session_t session = pscu_session_init();

  for (;;) {
    uint8_t frame[PSCU_SUBQ_FRAME_BYTES];
    bool captured = pscu_engine_capture_frame(frame);
    uint8_t stamp = pscu_port_ticks();
    pscu_clock_step_t tick = pscu_clock_step(clock);
    clock = tick.clock;
    bool valid = captured && pscu_subq_is_valid(frame);
    presence = pscu_presence_step(presence, valid, tick.elapsed_ms);
    bool disc_gone = pscu_presence_gone(presence);
    uint8_t previous = counter;
    counter = disc_gone ? 0U : pscu_subq_update_counter(frame, counter, PSCU_VCD_FILTER_ENABLED);
    bool settled = since.ms >= PSCU_TRIM_SETTLE_MS;
    osc = pscu_osc_step(osc, stamp, captured && settled && (counter > previous));
    bool supply_ok = pscu_supply_ok(pscu_port_supply_raw());
    bool reached = pscu_should_inject(counter, calib.trigger);
    bool in_window = supply_ok && reached;
    bool program = pscu_subq_is_program_area(frame);
    cap = (stealth.sent == 0U) ? calib.cap : cap;
    pscu_stealth_step_t step =
        pscu_stealth_step(stealth, in_window, program, disc_gone, cap, PSCU_STEALTH_GAP_FRAMES);
    stealth = step.state;
    if (step.fire) {
      session = pscu_session_fired(session, step.state.sent);
      board = pscu_engine_confirm_board(board);
      pscu_engine_inject(board);
    }
    pscu_session_step_t watched = pscu_session_watch(session, step.fire, program, disc_gone);
    session = watched.session;
    bool missed = !disc_gone && pscu_calib_missed(calib, previous, counter, since.armed);
    calib = pscu_calib_update(calib, watched, missed);
    pscu_osc_store_t kept = pscu_osc_store(osc, calib, !in_window && (stealth.sent == 0U));
    osc = kept.osc;
    calib = kept.calib;
    since = pscu_since_disc_step(since, disc_gone, tick.elapsed_ms, valid, reached || program);
    uint8_t fault = pscu_led_fault(
        !supply_ok, presence.seen, presence.quiet_ms, since.ms, since.framed, since.armed);
    led = (was_gone && !disc_gone) ? pscu_led_disc_arrived(led) : led;
    was_gone = disc_gone;
    led = pscu_led_show(led, watched.event, missed, fault, tick.elapsed_ms);
    pscu_port_watchdog_reset();
  }
}
