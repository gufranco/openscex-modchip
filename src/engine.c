// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "pscu/engine.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "port/port.h"
#include "pscu/assert.h"
#include "pscu/board_mode.h"
#include "pscu/calib.h"
#include "pscu/config.h"
#include "pscu/region.h"
#include "pscu/subq.h"
#include "pscu/trim.h"

// Samples of WFCK to observe before deciding the board era. Large enough that a
// live ~7.3 kHz clock produces far more than PSCU_DETECT_PULSES edges.
#define PSCU_DETECT_WINDOW ((uint16_t)10000U)
#define PSCU_DETECT_PULSES ((uint8_t)25U)
#define PSCU_GUARD_WINDOW ((uint16_t)2500U)
// Settle time before the detect window. The chip powers up with the console, and
// on a carrier board the WFCK oscillation may not have started yet; sampling too
// early would misread a modern board as a static legacy gate and pick the wrong
// injection method for the whole session. Read: PsNee V9.0 PSNee.ino:376 waits
// 300 ms "for WFCK to stabilize" before the same 10000-sample window, field-proven
// across PU-7 to PM-41.
#define PSCU_DETECT_SETTLE_MS ((uint16_t)300U)
// One SCEx bit cell is 4 ms (about 250 baud), the rate the mechacon expects.
#define PSCU_BIT_MS ((uint16_t)4U)
// SQCK idles high between frames and clocks one 96-bit burst per sector (75 per
// second, so a frame every ~13.3 ms). Before each capture the chip waits until
// SQCK has stayed high for one millisecond, which only happens in the gap between
// bursts, so the capture always starts on a frame's first bit. The millisecond
// is PsNee's own resync gap (Read: PsNee V9.0 PSNee.ino:716, a 1 ms delay before
// every capture "to prevent reading the tail end of the previous SUBQ packet").
// Polling for continuous idle rather than sleeping a fixed time also realigns
// when the previous capture ended inside a burst, as it can at boot or after the
// blocking injection. The poll count is the millisecond divided by the cost of
// one idle pass of pscu_wait_sqck_idle with SQCK high: 43 cycles, measured in
// simavr on the avr-gcc 14.2 -Os -flto ATtiny85 image (2026-10-08), where the
// loop is inlined into the run loop. The cost follows where the compiler keeps
// the loop's counters, which any change to the run loop can move: it has read
// 41, 55, 56 and 43 cycles as the code around it changed, and a stale value
// makes the check too long, a slice in which a disc's first frame after a
// silent pass can begin unseen, or too short. The sim check scenario_gap_check
// times the whole check in the built image and fails when this cost moves, so
// the constant is re-measured with each such change. The count is rounded up,
// plus one poll: n polls span only n - 1 intervals, so the first and last high
// reads must be a full millisecond apart. It stays unsigned long so no cast
// narrows it.
#define PSCU_SQCK_IDLE_POLL_CYCLES (43UL)
#define PSCU_SQCK_IDLE_POLLS                                \
  (((F_CPU + (1000UL * PSCU_SQCK_IDLE_POLL_CYCLES) - 1UL) / \
    (1000UL * PSCU_SQCK_IDLE_POLL_CYCLES)) +                \
   1UL)
// The idle wait gives up after 30 ms, as do the edge waits inside the assembly
// frame capture. That still covers the longest real wait, the inter-frame gap
// before a burst's first edge (a frame every 13.3 ms at single speed), yet a
// stopped drive fails a capture within about 60 ms, so the run loop keeps
// timing the silence that tells it the disc is gone.
// The poll count is 30 ms divided by the idle pass measured above.
#define PSCU_WAIT_MS (30UL)
#define PSCU_SQCK_IDLE_WAIT_POLLS ((F_CPU * PSCU_WAIT_MS) / (1000UL * PSCU_SQCK_IDLE_POLL_CYCLES))
// A frame that could not be captured is filled with this value. Its TNO and ZERO
// bytes are nonzero, so the pure SUBQ logic treats it as a miss and never as the
// program area.
#define PSCU_SUBQ_FAILED_BYTE ((uint8_t)0xFFU)

// Block until SQCK has read high for PSCU_SQCK_IDLE_POLLS consecutive polls,
// meaning the clock is in the gap between frames. Any low restarts the count. The
// whole wait is bounded by 30 ms of polls, so a clock that never idles (or a
// line stuck low) fails the capture instead of hanging the loop.
static bool pscu_wait_sqck_idle(void) {
  uint32_t quiet = 0U;
  uint32_t left = PSCU_SQCK_IDLE_WAIT_POLLS;
  bool idle = false;
  // The loop has one exit test, the polls left. Reaching the quiet count empties
  // the budget instead of being a second loop condition: with two conditions
  // the compiler tests the bound first and then the quiet count again, a branch
  // taken only when the gap completes on the very last poll, which no console
  // timing can be made to hit on purpose.
  while (left > 0U) {
    if (pscu_port_read_sqck() != 0U) {
      quiet = quiet + 1U;
    } else {
      quiet = 0U;
    }
    idle = quiet >= PSCU_SQCK_IDLE_POLLS;
    left = idle ? 0U : (left - 1U);
    pscu_port_watchdog_reset();
  }
  return idle;
}

// Drive one SCEx bit onto DATA. A zero is always a hard low. A one is high-Z on
// legacy boards (the gate is static, so releasing the line reads as high), but
// on modern boards DATA must instead mirror the live WFCK carrier, because the
// console samples DATA against that clock rather than a static level. On a
// modern board the bit cell is timed by the port layer against WFCK (adaptive
// build) or a fixed delay (fixed build); on a legacy board the cell is always a
// fixed delay, since a static WFCK offers no period to count.
static void pscu_inject_bit(uint8_t bit_value, pscu_board_mode_t mode) {
  PSCU_ASSERT(bit_value <= 1U);
  PSCU_ASSERT((mode == PSCU_BOARD_MODE_GATE) || (mode == PSCU_BOARD_MODE_WFCK));

  if (bit_value == 0U) {
    pscu_port_data_drive_low();
    if (mode == PSCU_BOARD_MODE_WFCK) {
      pscu_port_bit_hold_low();
    } else {
      pscu_port_delay_ms(PSCU_BIT_MS);
    }
  } else if (mode == PSCU_BOARD_MODE_WFCK) {
    pscu_port_bit_mirror();
  } else {
    pscu_port_data_release();
    pscu_port_delay_ms(PSCU_BIT_MS);
  }
  pscu_port_watchdog_reset();
}

// A string always runs to its end, 176 ms. If the disc leaves meanwhile the
// rest of the string reaches a mechacon that is no longer reading; on a carrier
// board a stopped drive also stops WFCK, and the watchdog then releases DATA
// within about 0.5 s.
static void pscu_inject_region(pscu_region_t region, pscu_board_mode_t mode) {
  PSCU_ASSERT((uint8_t)region < PSCU_REGION_COUNT);

  for (uint8_t bit = 0U; bit < PSCU_SCEX_BIT_COUNT; bit++) {
    pscu_inject_bit(pscu_region_bit(region, bit), mode);
  }
}

// Watch WFCK for window samples and classify what it shows: enough falling
// edges mean a live carrier, too few a static gate.
static pscu_board_mode_t pscu_sample_wfck(uint16_t window) {
  pscu_board_detect_t state = pscu_board_detect_init();
  for (uint16_t i = 0U; i < window; i++) {
    state = pscu_board_detect_step(state, pscu_port_read_wfck());
    pscu_port_watchdog_reset();
  }
  return pscu_board_detect_mode(state, PSCU_DETECT_PULSES);
}

// Run once at boot: let WFCK settle, then watch it across the detect window and
// classify the board. The result picks the injection method (gate high-Z vs
// WFCK mirror) for the rest of the session. The LED is lit for exactly this
// wait, about 0.4 s, which the chip spends anyway: an installer who sees it at
// power-on knows the chip has power, a running clock and its firmware,
// before any disc is read. Lighting it here adds no delay and sits on no timing
// path, and with no LED fitted nothing changes. lamp is false in the final
// profile, where a buzzer would otherwise sound for the whole 0.4 s; the board
// chirps that follow tell the same story.
pscu_board_mode_t pscu_engine_detect_board(bool lamp) {
  if (lamp) {
    pscu_port_led_on();
  }
  pscu_port_delay_ms(PSCU_DETECT_SETTLE_MS);
  pscu_board_mode_t mode = pscu_sample_wfck(PSCU_DETECT_WINDOW);
  pscu_port_led_off();
  return mode;
}

// The gate method holds WFCK low for a string, which on a carrier board would
// fight the console's own clock. Boot detection decides from WFCK as it is in
// the first 0.4 s, so a carrier that starts later reads as a gate. Before every
// string on a board taken for a gate, WFCK is watched again with the same edge
// threshold; a live carrier means the detection was wrong, and the board is a
// carrier board from then on, injected by mirroring, with WFCK never driven.
// The window is a quarter of the boot one, 2500 samples of 30 cycles each
// (Concluded: the sampling loop in the ATtiny85 listing), 9.4 ms at 8 MHz. A
// 7.3 kHz carrier gives about 69 edges in that time against the 25 needed, and
// the delay before the string stays under one 13.3 ms frame. On a carrier board
// it is never run.
pscu_board_mode_t pscu_engine_confirm_board(pscu_board_mode_t board) {
  pscu_board_mode_t confirmed = board;
  if (board == PSCU_BOARD_MODE_GATE) {
    confirmed = pscu_sample_wfck(PSCU_GUARD_WINDOW);
  }
  return confirmed;
}

// Wait for the inter-frame gap, then clock in one whole frame. The bits are
// clocked in by the port layer in assembly, where the time between edges is
// short enough to follow a fast SQCK. If the gap never comes or any edge times
// out, the frame is filled with PSCU_SUBQ_FAILED_BYTE so the logic layer reads it
// as a miss; a partial frame is never passed on. Returns whether a whole frame
// arrived, which the LED uses to tell a dead SQCK from a disc with no check.
bool pscu_engine_capture_frame(uint8_t *frame) {
  PSCU_ASSERT(frame != NULL);

  bool ok = pscu_wait_sqck_idle();
  if (ok) {
    ok = pscu_port_capture_frame(frame, PSCU_SUBQ_FRAME_BYTES) != 0U;
  }
  if (!ok) {
    for (uint8_t byte = 0U; byte < PSCU_SUBQ_FRAME_BYTES; byte++) {
      frame[byte] = PSCU_SUBQ_FAILED_BYTE;
    }
  }
  return ok;
}

// Emit exactly one region word, the one this build was configured for, then
// release DATA to high-Z. Sending only the console's own region (never all
// three) is a stealth choice: the bus carries precisely what that console
// expects and nothing more. The run loop calls this only inside the check
// window and only up to the stealth cap, so DATA is high-Z and the LED off
// during normal play. On a gate board the WFCK gate is held low for the string
// and released with DATA, so both lines are high-Z between strings. lamp lights
// the pin for the string in the debug profile only; it is set before the first
// cell and cleared after the last, so it never sits inside the bit timing.
void pscu_engine_inject(pscu_board_mode_t board, bool lamp) {
  PSCU_ASSERT((board == PSCU_BOARD_MODE_GATE) || (board == PSCU_BOARD_MODE_WFCK));

  bool gate = board == PSCU_BOARD_MODE_GATE;
  if (lamp) {
    pscu_port_led_on();
  }
  if (gate) {
    pscu_port_gate_drive_low();
  }
  pscu_inject_region(PSCU_CONFIGURED_REGION, board);
  pscu_port_data_release();
  if (gate) {
    pscu_port_gate_release();
  }
  pscu_port_led_off();
}

pscu_calib_t pscu_engine_load_calib(void) {
  pscu_calib_record_t record;
  for (uint8_t at = 0U; at < PSCU_CALIB_BYTES; at++) {
    record.bytes[at] = pscu_port_eeprom_read(at);
  }
  return pscu_calib_decode(record);
}

// Each byte is compared before it is written, so a record that did not change
// writes nothing, and one learned value costs two writes, the value and the
// check byte. The check byte goes last: power lost before it lands leaves a
// record whose check fails, which reads as the defaults rather than as a mix.
void pscu_engine_store_calib(pscu_calib_t calib) {
  pscu_calib_record_t record;
  record = pscu_calib_encode(calib);
  for (uint8_t at = 0U; at < PSCU_CALIB_BYTES; at++) {
    if (pscu_port_eeprom_read(at) != record.bytes[at]) {
      pscu_port_eeprom_write(at, record.bytes[at]);
    }
  }
}

// The stored trim is replayed step by step from the factory value, through the
// same bounded step the run loop uses, so a corrupted trim can never move the
// oscillator further than a live trim could. At most 16 steps, each a single
// OSCCAL LSB, keep every cycle-to-cycle change small.
uint8_t pscu_engine_apply_trim(int8_t trim) {
  uint8_t factory = pscu_port_osccal_read();
  uint8_t value = factory;
  int8_t step = (trim < 0) ? (int8_t)-1 : (int8_t)1;
  uint8_t steps = (uint8_t)((trim < 0) ? -trim : trim);
  for (uint8_t i = 0U; i < steps; i++) {
    value = pscu_trim_apply(factory, value, step);
    pscu_port_osccal_write(value);
  }
  return factory;
}
