// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "pscu/engine.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "port/port.h"
#include "pscu/assert.h"
#include "pscu/board_mode.h"
#include "pscu/config.h"
#include "pscu/diag.h"
#include "pscu/region.h"
#include "pscu/subq.h"

// Samples of WFCK to observe before deciding the board era. Large enough that a
// live ~7.3 kHz clock produces far more than PSCU_DETECT_PULSES edges.
#define PSCU_DETECT_WINDOW ((uint16_t)10000U)
#define PSCU_DETECT_PULSES ((uint8_t)25U)
// Settle time before the detect window. The chip powers up with the console, and
// on a carrier board the WFCK oscillation may not have started yet; sampling too
// early would misread a modern board as a static legacy gate and pick the wrong
// injection method for the whole session. Read: PsNee V9.0 PSNee.ino:376 waits
// 300 ms "for WFCK to stabilize" before the same 10000-sample window, field-proven
// across PU-7 to PM-41.
#define PSCU_DETECT_SETTLE_MS ((uint16_t)300U)
// Upper bound on polling for an SQCK edge, so a dead clock can never hang the
// loop forever; the watchdog is also kicked while waiting.
#define PSCU_WAIT_MAX ((uint16_t)0xFFFFU)
// One SCEx bit cell is 4 ms (about 250 baud), the rate the mechacon expects.
#define PSCU_BIT_MS ((uint16_t)4U)
#define PSCU_SUBQ_BITS ((uint8_t)8U)
#define PSCU_SUBQ_MSB ((uint8_t)0x80U)
// SQCK idles high between frames and clocks one 96-bit burst per sector (75 per
// second, so a frame every ~13.3 ms). Before each capture the chip waits until
// SQCK has stayed high for one millisecond, which only happens in the gap between
// bursts, so the capture always starts on a frame's first bit. The millisecond
// is PsNee's own resync gap (Read: PsNee V9.0 PSNee.ino:716, a 1 ms delay before
// every capture "to prevent reading the tail end of the previous SUBQ packet").
// Polling for continuous idle rather than sleeping a fixed time also realigns
// when the previous capture ended inside a burst, as it can at boot or after the
// blocking injection. The poll count is the millisecond divided by the cost of
// one idle pass of pscu_wait_sqck_idle: 36 cycles, counted from the avr-gcc
// 14.2 -Os listing of that loop (the read and watchdog calls are rcall plus ret,
// 3 and 4 cycles on the ATtiny core; the 32-bit count adds the rest). Recount it
// if the loop changes. The count stays unsigned long so no cast narrows it.
#define PSCU_SQCK_IDLE_POLL_CYCLES (36UL)
#define PSCU_SQCK_IDLE_POLLS (F_CPU / (1000UL * PSCU_SQCK_IDLE_POLL_CYCLES))
// A frame that could not be captured is filled with this value. Its TNO and ZERO
// bytes are nonzero, so the pure SUBQ logic treats it as a miss and never as the
// program area.
#define PSCU_SUBQ_FAILED_BYTE ((uint8_t)0xFFU)

// SUBQ is clocked by SQCK; each bit is valid across one low-then-high cycle.
// These two helpers block until the next edge, bounded by PSCU_WAIT_MAX.
static bool pscu_wait_sqck_low(void) {
  bool found = false;
  for (uint16_t i = 0U; (i < PSCU_WAIT_MAX) && !found; i++) {
    if (pscu_port_read_sqck() == 0U) {
      found = true;
    } else {
      pscu_port_watchdog_reset();
    }
  }
  return found;
}

static bool pscu_wait_sqck_high(void) {
  bool found = false;
  for (uint16_t i = 0U; (i < PSCU_WAIT_MAX) && !found; i++) {
    if (pscu_port_read_sqck() != 0U) {
      found = true;
    } else {
      pscu_port_watchdog_reset();
    }
  }
  return found;
}

// Block until SQCK has read high for PSCU_SQCK_IDLE_POLLS consecutive polls,
// meaning the clock is in the gap between frames. Any low restarts the count. The
// whole wait is bounded by PSCU_WAIT_MAX polls, so a clock that never idles (or a
// line stuck low) fails the capture instead of hanging the loop.
static bool pscu_wait_sqck_idle(void) {
  uint32_t quiet = 0U;
  for (uint16_t i = 0U; (i < PSCU_WAIT_MAX) && (quiet < PSCU_SQCK_IDLE_POLLS); i++) {
    if (pscu_port_read_sqck() != 0U) {
      quiet = quiet + 1U;
    } else {
      quiet = 0U;
    }
    pscu_port_watchdog_reset();
  }
  return quiet >= PSCU_SQCK_IDLE_POLLS;
}

// SUBQ arrives least-significant-bit first, so each new bit is shifted in at the
// top (MSB) and the byte shifts right, leaving the first bit in bit 0 after 8.
// Both edge waits are always made and the sample is taken right after the rising
// edge, exactly as in the hardware-tested capture, so the time from edge to
// sample is unchanged; a timed-out wait only marks the byte failed, and the loop
// stops at the next bit boundary rather than clocking garbage.
static bool pscu_capture_byte(uint8_t *value) {
  uint8_t byte = 0U;
  bool ok = true;
  for (uint8_t bit = 0U; (bit < PSCU_SUBQ_BITS) && ok; bit++) {
    bool low = pscu_wait_sqck_low();
    bool high = pscu_wait_sqck_high();
    byte = (uint8_t)(byte >> 1U);
    if (pscu_port_read_subq() != 0U) {
      byte = (uint8_t)(byte | PSCU_SUBQ_MSB);
    }
    ok = low && high;
  }
  *value = byte;
  return ok;
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

static void pscu_inject_region(pscu_region_t region, pscu_board_mode_t mode) {
  PSCU_ASSERT((uint8_t)region < PSCU_REGION_COUNT);

  for (uint8_t bit = 0U; bit < PSCU_SCEX_BIT_COUNT; bit++) {
    uint8_t bit_value = pscu_region_bit(region, bit);
    pscu_inject_bit(bit_value, mode);
  }
}

// Run once at boot: let WFCK settle, then watch it across the detect window and
// classify the board. The result picks the injection method (gate high-Z vs
// WFCK mirror) for the rest of the session.
pscu_board_mode_t pscu_engine_detect_board(void) {
  pscu_port_delay_ms(PSCU_DETECT_SETTLE_MS);
  pscu_board_detect_t state = pscu_board_detect_init();
  for (uint16_t i = 0U; i < PSCU_DETECT_WINDOW; i++) {
    state = pscu_board_detect_step(state, pscu_port_read_wfck());
    pscu_port_watchdog_reset();
  }
  return pscu_board_detect_mode(state, PSCU_DETECT_PULSES);
}

// Wait for the inter-frame gap, then clock in one whole frame. If the gap never
// comes or any edge times out, the frame is filled with PSCU_SUBQ_FAILED_BYTE so
// the logic layer reads it as a miss; a partial frame is never passed on.
void pscu_engine_capture_frame(uint8_t *frame) {
  PSCU_ASSERT(frame != NULL);

  bool ok = pscu_wait_sqck_idle();
  for (uint8_t byte = 0U; (byte < PSCU_SUBQ_FRAME_BYTES) && ok; byte++) {
    ok = pscu_capture_byte(&frame[byte]);
  }
  if (!ok) {
    for (uint8_t byte = 0U; byte < PSCU_SUBQ_FRAME_BYTES; byte++) {
      frame[byte] = PSCU_SUBQ_FAILED_BYTE;
    }
  }
}

// Emit exactly one region word, the one this build was configured for, then
// release DATA to high-Z. Sending only the console's own region (never all
// three) is a stealth choice: the bus carries precisely what that console
// expects and nothing more. The run loop calls this only inside the check
// window and only up to the stealth cap, so DATA is high-Z and the LED off
// during normal play.
void pscu_engine_inject(pscu_board_mode_t board) {
  PSCU_ASSERT((board == PSCU_BOARD_MODE_GATE) || (board == PSCU_BOARD_MODE_WFCK));

  pscu_port_led_on();
  pscu_inject_region(PSCU_CONFIGURED_REGION, board);
  pscu_port_data_release();
  pscu_port_led_off();
}

// Read the previous flight recorder, advance it, and write back only the bytes
// that changed. Reading before writing is what lets the session count accumulate
// across sessions and power cycles; the pure codec in diag.c owns the byte
// layout, so this function only moves bytes over the EEPROM port primitives.
// Skipping unchanged bytes spares their erase/write cycles: the magic and board
// bytes are written once per install, and only the counters wear.
void pscu_engine_log_session(pscu_board_mode_t board, uint8_t injects, uint8_t confirmed) {
  PSCU_ASSERT((board == PSCU_BOARD_MODE_GATE) || (board == PSCU_BOARD_MODE_WFCK));

  uint8_t stored[PSCU_DIAG_EEPROM_BYTES];
  for (uint8_t i = 0U; i < PSCU_DIAG_EEPROM_BYTES; i++) {
    stored[i] = pscu_port_eeprom_read((uint8_t)(PSCU_DIAG_EEPROM_ADDR + i));
  }
  pscu_diag_record_t previous = pscu_diag_decode(stored);
  pscu_diag_record_t record = pscu_diag_build(board, previous.sessions, injects, confirmed);
  uint8_t next[PSCU_DIAG_EEPROM_BYTES];
  pscu_diag_encode(record, next);
  for (uint8_t i = 0U; i < PSCU_DIAG_EEPROM_BYTES; i++) {
    if (next[i] != stored[i]) {
      pscu_port_eeprom_write((uint8_t)(PSCU_DIAG_EEPROM_ADDR + i), next[i]);
    }
  }
}
