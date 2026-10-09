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
#include "pscu/led.h"
#include "pscu/loop.h"
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
#define PSCU_TRIM_EXPECTED \
  ((((uint32_t)PSCU_TRIM_FRAMES * F_CPU) + (PSCU_TRIM_TICK_HZ / 2UL)) / PSCU_TRIM_TICK_HZ)

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

// The LED level the loop decided for the rest of the pass. Injection drives the
// LED itself for each string; between strings the pattern player decides.
static void pscu_run_show(bool on) {
  if (on) {
    pscu_port_led_on();
  } else {
    pscu_port_led_off();
  }
}

// Boot: read the record, fold in the board just detected, and store the result
// (nothing is written when it did not change). The boot code is the watchdog's
// 5 if the last reset came from it, else 6 if the board changed, else none; a
// boot that has both shows 5, since a hang is the more urgent report.
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

// Time this pass's frame against the last one. Only two sampled frames in a row
// make a sample; the batch decides a step, which moves OSCCAL between strings,
// one notch per write so no write changes the clock by more than a notch
// (PSCU_TRIM_MAX_STEP), and marks the trim for storing.
static pscu_osc_t pscu_osc_step(pscu_osc_t osc, uint8_t stamp, bool hit) {
  pscu_trim_ref_t ref = { PSCU_TRIM_LOW, PSCU_TRIM_HIGH, PSCU_TRIM_EXPECTED };
  bool sample = osc.hit && hit;
  uint16_t delta = (uint8_t)(stamp - osc.stamp);
  pscu_trim_step_t step = pscu_trim_step(osc.batch, sample, delta, ref);
  pscu_osc_t next = osc;
  next.batch = step.state;
  next.stamp = stamp;
  next.hit = hit;
  uint8_t target = pscu_trim_apply(osc.factory, osc.osccal, step.adjust);
  for (uint8_t n = 0U; (n < (uint8_t)PSCU_TRIM_MAX_STEP) && (next.osccal != target); n++) {
    next.osccal =
        (target > next.osccal) ? (uint8_t)(next.osccal + 1U) : (uint8_t)(next.osccal - 1U);
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

// The whole modchip, as one loop. Detect the board era once, or after a
// watchdog reset keep the one recorded at boot (see pscu_calib_keeps_board),
// then forever:
// capture a SUBQ frame, read the timer and the supply, and let the pure loop
// step decide everything else: the counter and the window, whether to emit one
// region string, what the session and the calibration learn, and the LED level
// (see pscu/loop.h). This shell then does what the decision asks of the chip:
// it confirms a gate board and injects, stores the calibration, steps and stores
// the oscillator trim, and lights the LED. The stored trim is applied before
// anything is timed. Every call inside the loop is bounded, and the watchdog is
// kicked each pass so a stuck signal resets the chip rather than wedging it.
void pscu_run(void) {
  pscu_calib_t stored = pscu_engine_load_calib();
  pscu_osc_t osc = pscu_osc_init(pscu_engine_apply_trim(stored.trim));
  bool watchdog = pscu_port_reset_was_watchdog() != 0U;
  pscu_board_mode_t board = PSCU_BOARD_MODE_GATE;
  if (pscu_calib_keeps_board(stored, watchdog)) {
    board = pscu_calib_stored_board(stored);
  } else {
    board = pscu_engine_detect_board();
  }
  pscu_calib_boot_t boot = pscu_calib_start(stored, board);
  uint8_t changed_code = boot.board_changed ? PSCU_LED_CODE_BOARD_CHANGED : 0U;
  uint8_t boot_code = watchdog ? PSCU_LED_CODE_WATCHDOG : changed_code;
  pscu_led_t led = pscu_led_init((board == PSCU_BOARD_MODE_WFCK) ? 2U : 1U, boot_code);
  pscu_loop_t loop = pscu_loop_init(boot.calib, led, PSCU_VCD_FILTER_ENABLED);
  pscu_clock_t clock = { pscu_port_ticks(), 0U };

  for (;;) {
    uint8_t frame[PSCU_SUBQ_FRAME_BYTES];
    bool captured = pscu_engine_capture_frame(frame);
    uint8_t stamp = pscu_port_ticks();
    pscu_clock_step_t tick = pscu_clock_step(clock);
    clock = tick.clock;
    bool supply_ok = pscu_supply_ok(pscu_port_supply_raw());
    pscu_loop_in_t in = { frame, captured, supply_ok, tick.elapsed_ms };
    pscu_loop_out_t out = pscu_loop_step(&loop, &in);
    osc = pscu_osc_step(osc, stamp, out.trim_sample);
    if (out.fire) {
      board = pscu_engine_confirm_board(board);
      pscu_engine_inject(board);
    }
    if (out.store) {
      pscu_engine_store_calib(loop.calib);
    }
    pscu_osc_store_t kept = pscu_osc_store(osc, loop.calib, out.quiet);
    osc = kept.osc;
    loop.calib = kept.calib;
    pscu_run_show(out.led_on);
    pscu_port_watchdog_reset();
  }
}
