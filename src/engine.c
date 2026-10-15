// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "pscu/engine.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "port/port.h"
#include "pscu/assert.h"
#include "pscu/board_mode.h"
#include "pscu/calib.h"
#include "pscu/config.h"
#include "pscu/region.h"
#include "pscu/subq.h"
#include "pscu/supply.h"
#include "pscu/trim.h"

// WFCK samples at boot: 52.5 ms at 42 cycles a sample, measured in simavr
// (scenario_detect_timing), about 386 falling edges of a 7.35 kHz carrier.
// PsNee V9.0 counts the same 10000 samples in its own loop.
#define PSCU_DETECT_WINDOW ((uint16_t)10000U)
// Edge run that proves a carrier (mayumi_v4_12c508a_any.asm:566,
// multimode_v3_12c508a_any.asm:626).
#define PSCU_DETECT_PULSES ((uint8_t)25U)
#define PSCU_GUARD_WINDOW ((uint16_t)2500U)
// Lets WFCK start before detection (PsNee V9.0 PSNee.ino:376).
#define PSCU_DETECT_SETTLE_MS ((uint16_t)300U)
// One SCEx bit cell, about 250 baud, the rate the mechacon expects.
#define PSCU_BIT_MS ((uint16_t)4U)
// Polls of SQCK high that mark the gap between frames: 1 ms (PsNee V9.0
// PSNee.ino:716). 43 cycles per poll, measured in simavr; scenario_gap_check
// fails if it moves. Rounded up, plus one: n polls span n - 1 intervals.
#define PSCU_SQCK_IDLE_POLL_CYCLES (43UL)
#define PSCU_SQCK_IDLE_POLLS                                \
  (((F_CPU + (1000UL * PSCU_SQCK_IDLE_POLL_CYCLES) - 1UL) / \
    (1000UL * PSCU_SQCK_IDLE_POLL_CYCLES)) +                \
   1UL)
// Timeout for the idle wait and the edge waits: over the 13.3 ms frame period,
// so a stopped drive fails a capture within about 60 ms.
#define PSCU_WAIT_MS (30UL)
#define PSCU_SQCK_IDLE_WAIT_POLLS ((F_CPU * PSCU_WAIT_MS) / (1000UL * PSCU_SQCK_IDLE_POLL_CYCLES))
// Fills a failed frame; nonzero TNO and ZERO bytes make it a miss.
#define PSCU_SUBQ_FAILED_BYTE ((uint8_t)0xFFU)

// Wait until SQCK has read high for PSCU_SQCK_IDLE_POLLS consecutive polls,
// giving up after 30 ms.
static bool pscu_wait_sqck_idle(void) {
  uint32_t quiet = 0U;
  uint32_t left = PSCU_SQCK_IDLE_WAIT_POLLS;
  bool idle = false;
  // One exit test: reaching the quiet count empties the budget. A second loop
  // condition would add a branch no test can reach.
  while (left != 0U) {
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

// Drive one SCEx bit onto DATA. A zero is low. A one is high-Z on a gate board
// and a copy of WFCK on a carrier board. Carrier cells count WFCK periods; gate
// cells are a fixed delay.
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

// A string always runs to its end: 176 ms on a gate board, and up to 181 ms
// mirrored on a carrier, where 44 bits of 30 WFCK periods at the slowest
// carrier rate, about 7.3 kHz, take longer. If WFCK stops mid-string, the
// watchdog releases DATA within about 0.5 s.
static void pscu_inject_region(pscu_region_t region, pscu_board_mode_t mode) {
  PSCU_ASSERT((uint8_t)region < PSCU_REGION_COUNT);

  for (uint8_t bit = 0U; bit < PSCU_SCEX_BIT_COUNT; bit++) {
    pscu_inject_bit(pscu_region_bit(region, bit), mode);
  }
}

// Sample WFCK window times and classify the board.
static pscu_board_mode_t pscu_sample_wfck(uint16_t window) {
  pscu_board_detect_t state = pscu_board_detect_init(PSCU_DETECT_PULSES);
  for (uint16_t i = 0U; i < window; i++) {
    state = pscu_board_detect_step(state, pscu_port_read_wfck());
    pscu_port_watchdog_reset();
  }
  return pscu_board_detect_mode(state);
}

// Boot-time board detection, about 0.4 s. With lamp set (debug profile) the indicator
// is lit for the wait, showing the chip is alive.
pscu_board_mode_t pscu_engine_detect_board(bool lamp) {
  if (lamp) {
    pscu_port_indicator_on();
  }
  pscu_port_delay_ms(PSCU_DETECT_SETTLE_MS);
  pscu_board_mode_t mode = pscu_sample_wfck(PSCU_DETECT_WINDOW);
  pscu_port_indicator_off();
  return mode;
}

// Before each string on a gate board, check WFCK again so a carrier that started
// late is never driven. 2500 samples, 13.1 ms at 42 cycles a sample, under one
// 13.3 ms frame; a carrier gives about 96 edges.
pscu_board_mode_t pscu_engine_confirm_board(pscu_board_mode_t board) {
  pscu_board_mode_t confirmed = board;
  if (board == PSCU_BOARD_MODE_GATE) {
    confirmed = pscu_sample_wfck(PSCU_GUARD_WINDOW);
  }
  return confirmed;
}

// Wait for the inter-frame gap, then clock in one frame. On failure the frame is
// filled with PSCU_SUBQ_FAILED_BYTE; a partial frame is never passed on.
bool pscu_engine_capture_frame(uint8_t *frame) {
  PSCU_ASSERT(frame != NULL);

  bool ok = pscu_wait_sqck_idle();
  if (ok) {
    ok = pscu_port_capture_frame(frame, PSCU_SUBQ_FRAME_BYTES) != 0U;
  }
  if (!ok) {
    (void)memset(frame, (int)PSCU_SUBQ_FAILED_BYTE, PSCU_SUBQ_FRAME_BYTES);
  }
  return ok;
}

// Emit the configured region word, then release DATA, and on a gate board WFCK,
// to high-Z. The lamp is set before the first cell and cleared after the last,
// only when it was lit: in the trace build PB3 is a serial line that idles high,
// and driving it low after a string would read as a break that garbles the next
// line.
void pscu_engine_inject(pscu_board_mode_t board, bool lamp) {
  PSCU_ASSERT((board == PSCU_BOARD_MODE_GATE) || (board == PSCU_BOARD_MODE_WFCK));

  bool gate = board == PSCU_BOARD_MODE_GATE;
  if (lamp) {
    pscu_port_indicator_on();
  }
  if (gate) {
    pscu_port_gate_drive_low();
  }
  pscu_inject_region(PSCU_CONFIGURED_REGION, board);
  pscu_port_data_release();
  if (gate) {
    pscu_port_gate_release();
  }
  if (lamp) {
    pscu_port_indicator_off();
  }
}

pscu_calib_t pscu_engine_load_calib(void) {
  pscu_calib_record_t record;
  for (uint8_t at = 0U; at < PSCU_CALIB_BYTES; at++) {
    record.bytes[at] = pscu_port_eeprom_read(at);
  }
  return pscu_calib_decode(record);
}

// Write only changed bytes, check byte last, so a torn write fails the check.
// Nothing is written on a low supply, the usual cause of a torn record, and the
// writes stop at the first byte that could not start, so the check byte never
// lands over a stale byte.
bool pscu_engine_store_calib(pscu_calib_t calib) {
  bool written = pscu_supply_ok(pscu_port_supply_raw());
  if (written) {
    pscu_calib_record_t record;
    record = pscu_calib_encode(calib);
    for (uint8_t at = 0U; (at < PSCU_CALIB_BYTES) && written; at++) {
      if (pscu_port_eeprom_read(at) != record.bytes[at]) {
        uint8_t started = pscu_port_eeprom_write(at, record.bytes[at]);
        // coverage: unreachable: simavr finishes EEPROM writes at once, so a
        // write that cannot start never happens there.
        written = started != 0U;
      }
    }
  }
  return written;
}

// Replay the stored trim one notch at a time through the same bounds as a live
// trim.
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
