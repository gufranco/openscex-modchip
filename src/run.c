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
#include "pscu/indicator.h"
#include "pscu/loop.h"
#include "pscu/subq.h"
#include "pscu/supply.h"
#include "pscu/trim.h"

// Converts Timer1 ticks (16384 clocks each) to milliseconds.
#define PSCU_CLOCKS_PER_MS (F_CPU / 1000UL)
#define PSCU_CLOCKS_PER_TICK (16384UL)

// Trim reference from F_CPU and the 75 Hz frame rate (Red Book): a period is
// 6.51 ticks. The window, 80 to 140 percent (5 to 9 ticks), covers a 10 percent
// RC error and rejects double speed (3 or 4) and skipped frames (13 or more).
#define PSCU_TRIM_SUBQ_HZ (75UL)
#define PSCU_TRIM_TICK_HZ (PSCU_CLOCKS_PER_TICK * PSCU_TRIM_SUBQ_HZ)
#define PSCU_TRIM_LOW ((F_CPU * 4UL) / (PSCU_TRIM_TICK_HZ * 5UL))
#define PSCU_TRIM_HIGH ((F_CPU * 7UL) / (PSCU_TRIM_TICK_HZ * 5UL))
#define PSCU_TRIM_EXPECTED \
  ((((uint32_t)PSCU_TRIM_FRAMES * F_CPU) + (PSCU_TRIM_TICK_HZ / 2UL)) / PSCU_TRIM_TICK_HZ)

// The last Timer1 reading and the clocks left over below one millisecond.
typedef struct {
  uint8_t last;
  uint32_t carry;
} pscu_clock_t;

typedef struct {
  pscu_clock_t clock;
  uint32_t elapsed_ms;
} pscu_clock_step_t;

// The running trim. hit: the last pass was a sample. dirty: a trim is waiting
// to be stored.
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

// The indicator profile, fixed at build time (pscu/config.h).
#if PSCU_PROFILE_FINAL
#define PSCU_RUN_PROFILE PSCU_INDICATOR_PROFILE_FINAL
#else
#define PSCU_RUN_PROFILE PSCU_INDICATOR_PROFILE_DEBUG
#endif

static void pscu_run_show(bool on) {
  if (on) {
    pscu_port_indicator_on();
  } else {
    pscu_port_indicator_off();
  }
}

// Fold the detected board into the record and store it.
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

// Time this frame against the last; two sampled frames in a row make a sample.
// A decided step moves OSCCAL one notch per write.
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

// A new trim is stored only outside the injection window.
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

// Milliseconds since the last pass. The 8-bit wrap is 524 ms, longer than any
// pass.
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

// Detect the board, then forever: capture a frame, let pscu_loop_step decide,
// and carry out its decisions on the hardware. Every call is bounded and the
// watchdog is kicked each pass.
void pscu_run(void) {
  pscu_calib_t stored = pscu_engine_load_calib();
  pscu_osc_t osc = pscu_osc_init(pscu_engine_apply_trim(stored.trim));
  bool watchdog = pscu_port_reset_was_watchdog() != 0U;
  pscu_board_mode_t board = PSCU_BOARD_MODE_GATE;
  if (pscu_calib_keeps_board(stored, watchdog)) {
    board = pscu_calib_stored_board(stored);
  } else {
    board = pscu_engine_detect_board(PSCU_LAMP_STRINGS);
  }
  pscu_calib_boot_t boot = pscu_calib_start(stored, board);
  uint8_t changed_code = boot.board_changed ? PSCU_INDICATOR_CODE_BOARD_CHANGED : 0U;
  uint8_t boot_code = watchdog ? PSCU_INDICATOR_CODE_WATCHDOG : changed_code;
  pscu_indicator_t indicator =
      pscu_indicator_init((board == PSCU_BOARD_MODE_WFCK) ? 2U : 1U, boot_code, PSCU_RUN_PROFILE);
  pscu_loop_t loop = pscu_loop_init(boot.calib, indicator, PSCU_VCD_FILTER_ENABLED);
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
    uint8_t osccal_before = osc.osccal;
    osc = pscu_osc_step(osc, stamp, out.trim_sample);
    if (out.fire) {
      board = pscu_engine_confirm_board(board);
      pscu_engine_inject(board, PSCU_LAMP_STRINGS);
    }
    if (out.store) {
      pscu_engine_store_calib(loop.calib);
    }
    pscu_osc_store_t kept = pscu_osc_store(osc, loop.calib, out.quiet);
    // Anything learned this pass gets a debug tick.
    bool learned = out.store || (osc.osccal != osccal_before) || (kept.osc.dirty != osc.dirty);
    osc = kept.osc;
    loop.calib = kept.calib;
    loop.indicator = learned ? pscu_indicator_note(loop.indicator) : loop.indicator;
    pscu_run_show(out.indicator_on);
    pscu_port_watchdog_reset();
  }
}
