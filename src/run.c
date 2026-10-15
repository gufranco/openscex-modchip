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
#if PSCU_PROFILE_TRACE
#include "pscu/trace.h"
#endif

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
#if F_CPU == 8000000UL
// 64 frames at 8 MHz are 416.7 ticks; the dead band is measured from the
// nearest whole tick, never from a truncated 416.
_Static_assert(PSCU_TRIM_EXPECTED == 417UL, "the batch reference rounds to 417 ticks");
_Static_assert((PSCU_CLOCKS_PER_TICK * 1000UL) == (2048UL * PSCU_CLOCKS_PER_MS),
               "Timer1 at clk/16384 ticks every 2.048 ms (datasheet 2586Q Table 12-5)");
_Static_assert(PSCU_TRIM_LOW == 5UL, "80 percent of a 6.51-tick frame period is 5 ticks");
_Static_assert(PSCU_TRIM_HIGH == 9UL, "140 percent of a 6.51-tick frame period is 9 ticks");
#endif
_Static_assert((PSCU_CLOCKS_PER_MS * 1000UL) == F_CPU, "a millisecond holds F_CPU / 1000 clocks");

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

#if PSCU_PROFILE_TRACE
static void pscu_run_trace_send(const uint8_t *line, uint8_t len) {
  for (uint8_t at = 0U; at < len; at++) {
    pscu_port_trace_byte(line[at]);
  }
}

// After every pass, in the gap before the next frame: the frame as 24 hex
// digits, 2.2 ms at 115200 baud, then at most one queued event, under 2.8 ms.
// A failed capture shows as all FF. Both go out after the pass's decisions and
// any string, so captures and DATA keep the debug image's timing.
static void pscu_run_trace_after_pass(pscu_trace_t *trace, const uint8_t *frame, uint8_t *line) {
  uint8_t len = pscu_trace_format_frame(frame, line);
  pscu_run_trace_send(line, len);
  pscu_trace_event_t event;
  if (pscu_trace_pop(trace, &event)) {
    len = pscu_trace_format(&event, line);
    pscu_run_trace_send(line, len);
  }
}

// Fold one pass into the trace: the loop's decisions, the counter and string
// count behind them, the record and oscillator after the pass, and the store.
static void pscu_run_trace_note(pscu_trace_t *trace,
                                const pscu_loop_t *loop,
                                const pscu_loop_out_t *out,
                                uint32_t elapsed_ms,
                                pscu_board_mode_t board,
                                uint8_t osccal,
                                pscu_trace_store_t store,
                                bool supply_ok) {
  pscu_trace_pass_t pass;
  pass.elapsed_ms = elapsed_ms;
  pass.window = out->window;
  pass.fire = out->fire;
  pass.program = out->program;
  pass.disc_gone = out->disc_gone;
  pass.event = out->event;
  pass.missed = out->missed;
  pass.supply_ok = supply_ok;
  pass.counter = loop->counter;
  pass.sent = loop->stealth.sent;
  pass.board = (uint8_t)board;
  pass.calib = loop->calib;
  pass.osccal = osccal;
  pass.store = store;
  pass.supply_raw = pscu_port_supply_raw();
  pscu_trace_pass(trace, &pass);
}

// A store this pass and whether it held: a refusal outweighs a success.
static pscu_trace_store_t pscu_run_trace_merge(pscu_trace_store_t so_far,
                                               bool tried,
                                               bool written) {
  pscu_trace_store_t now = written ? PSCU_TRACE_STORE_DONE : PSCU_TRACE_STORE_REFUSED;
  pscu_trace_store_t merged = so_far;
  if (tried && (so_far != PSCU_TRACE_STORE_REFUSED)) {
    merged = now;
  }
  return merged;
}
#else
static void pscu_run_show(bool on) {
  if (on) {
    pscu_port_indicator_on();
  } else {
    pscu_port_indicator_off();
  }
}
#endif

// Fold the detected board into the record and store it.
static pscu_calib_boot_t pscu_calib_start(pscu_calib_t stored, pscu_board_mode_t board) {
  uint8_t detected = (board == PSCU_BOARD_MODE_WFCK) ? 1U : 0U;
  pscu_calib_boot_t boot = pscu_calib_boot(stored, detected);
  // A boot record that cannot be written now is written with the next store.
  (void)pscu_engine_store_calib(boot.calib);
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

// A new trim is stored only outside the injection window, and stays dirty
// until a store succeeds.
static pscu_osc_store_t pscu_osc_store(pscu_osc_t osc, pscu_calib_t calib, bool quiet) {
  pscu_osc_store_t out;
  out.osc = osc;
  out.calib = calib;
  if (osc.dirty && quiet) {
    out.calib.trim = (int8_t)((int16_t)osc.osccal - (int16_t)osc.factory);
    out.osc.dirty = !pscu_engine_store_calib(out.calib);
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
  bool board_kept = pscu_calib_keeps_board(stored, watchdog);
  if (board_kept) {
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
  // A learned record waits here until the window is closed and a store succeeds.
  bool store_due = false;
#if PSCU_PROFILE_TRACE
  // A serial line idles high. The boot line goes out before the first frame
  // is awaited, so it never collides with SUBQ. One buffer serves every line.
  pscu_port_indicator_on();
  uint8_t line[PSCU_TRACE_LINE_MAX];
  pscu_trace_boot_t report;
  report.build_id = (uint16_t)PSCU_BUILD_ID;
  report.region = (uint8_t)PSCU_CONFIGURED_REGION;
  report.vcd_filter = PSCU_VCD_FILTER_ENABLED;
  report.watchdog = watchdog;
  report.board = (uint8_t)board;
  report.board_kept = board_kept;
  report.board_changed = boot.board_changed;
  report.factory = osc.factory;
  report.osccal = osc.osccal;
  report.calib = stored;
  report.supply_raw = pscu_port_supply_raw();
  for (uint8_t index = 0U; index < PSCU_TRACE_BOOT_LINES; index++) {
    uint8_t len = pscu_trace_boot_line(&report, index, line);
    pscu_run_trace_send(line, len);
  }
  pscu_trace_t trace;
  pscu_trace_init(&trace, stored, osc.osccal);
#endif

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
      pscu_board_mode_t confirmed = pscu_engine_confirm_board(board);
      // A carrier found late goes into the record, so a watchdog reset that
      // keeps the recorded board never falls back to gating WFCK.
      store_due = store_due || (confirmed != board);
      board = confirmed;
      loop.calib.board = (uint8_t)board;
      pscu_engine_inject(board, PSCU_LAMP_STRINGS);
    }
    store_due = store_due || out.store;
    bool record_tried = store_due && out.quiet;
    if (record_tried) {
      store_due = !pscu_engine_store_calib(loop.calib);
    }
    bool trim_tried = osc.dirty && out.quiet;
    pscu_osc_store_t kept = pscu_osc_store(osc, loop.calib, out.quiet);
    // Anything learned this pass gets a debug tick.
    bool learned = out.store || (osc.osccal != osccal_before) || (kept.osc.dirty != osc.dirty);
#if PSCU_PROFILE_TRACE
    pscu_trace_store_t store =
        pscu_run_trace_merge(PSCU_TRACE_STORE_NONE, record_tried, !store_due);
    store = pscu_run_trace_merge(store, trim_tried, !kept.osc.dirty);
#else
    (void)record_tried;
    (void)trim_tried;
#endif
    osc = kept.osc;
    loop.calib = kept.calib;
    loop.indicator = learned ? pscu_indicator_note(loop.indicator) : loop.indicator;
#if PSCU_PROFILE_TRACE
    pscu_run_trace_note(&trace, &loop, &out, tick.elapsed_ms, board, osc.osccal, store, supply_ok);
    pscu_run_trace_after_pass(&trace, frame, line);
#else
    pscu_run_show(out.indicator_on);
#endif
    pscu_port_watchdog_reset();
  }
}
