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
// Upper bound on polling for an SQCK edge, so a dead clock can never hang the
// loop forever; the watchdog is also kicked while waiting.
#define PSCU_WAIT_MAX ((uint16_t)0xFFFFU)
// One SCEx bit cell is 4 ms (about 250 baud), the rate the mechacon expects.
#define PSCU_BIT_MS ((uint16_t)4U)
#define PSCU_SUBQ_BITS ((uint8_t)8U)
#define PSCU_SUBQ_MSB ((uint8_t)0x80U)

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

// SUBQ arrives least-significant-bit first, so each new bit is shifted in at the
// top (MSB) and the byte shifts right, leaving the first bit in bit 0 after 8.
static uint8_t pscu_capture_byte(void) {
  uint8_t value = 0U;
  for (uint8_t bit = 0U; bit < PSCU_SUBQ_BITS; bit++) {
    (void)pscu_wait_sqck_low();
    (void)pscu_wait_sqck_high();
    value = (uint8_t)(value >> 1U);
    if (pscu_port_read_subq() != 0U) {
      value = (uint8_t)(value | PSCU_SUBQ_MSB);
    }
  }
  return value;
}

// Drive one SCEx bit onto DATA. A zero is always a hard low. A one is high-Z on
// legacy boards (the gate is static, so releasing the line reads as high), but
// on modern boards DATA must instead mirror the live WFCK carrier, because the
// console samples DATA against that clock rather than a static level.
static void pscu_inject_bit(uint8_t bit_value, pscu_board_mode_t mode) {
  PSCU_ASSERT(bit_value <= 1U);
  PSCU_ASSERT((mode == PSCU_BOARD_MODE_GATE) || (mode == PSCU_BOARD_MODE_WFCK));

  if (bit_value == 0U) {
    pscu_port_data_drive_low();
    pscu_port_delay_ms(PSCU_BIT_MS);
  } else if (mode == PSCU_BOARD_MODE_WFCK) {
    pscu_port_data_mirror_wfck_ms(PSCU_BIT_MS);
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

// Run once at boot: watch WFCK across the detect window and classify the board.
// The result picks the injection method (gate high-Z vs WFCK mirror) for the
// rest of the session.
pscu_board_mode_t pscu_engine_detect_board(void) {
  pscu_board_detect_t state = pscu_board_detect_init();
  for (uint16_t i = 0U; i < PSCU_DETECT_WINDOW; i++) {
    state = pscu_board_detect_step(state, pscu_port_read_wfck());
    pscu_port_watchdog_reset();
  }
  return pscu_board_detect_mode(state, PSCU_DETECT_PULSES);
}

void pscu_engine_capture_frame(uint8_t *frame) {
  PSCU_ASSERT(frame != NULL);

  for (uint8_t byte = 0U; byte < PSCU_SUBQ_FRAME_BYTES; byte++) {
    frame[byte] = pscu_capture_byte();
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

// Read the previous flight recorder, advance it, and write it back. Reading
// before writing is what lets the session count accumulate across power cycles;
// the pure codec in diag.c owns the byte layout, so this function only moves
// bytes over the EEPROM port primitives.
void pscu_engine_log_session(pscu_board_mode_t board, uint8_t injects) {
  PSCU_ASSERT((board == PSCU_BOARD_MODE_GATE) || (board == PSCU_BOARD_MODE_WFCK));

  uint8_t raw[PSCU_DIAG_EEPROM_BYTES];
  for (uint8_t i = 0U; i < PSCU_DIAG_EEPROM_BYTES; i++) {
    raw[i] = pscu_port_eeprom_read((uint8_t)(PSCU_DIAG_EEPROM_ADDR + i));
  }
  pscu_diag_record_t previous = pscu_diag_decode(raw);
  pscu_diag_record_t record = pscu_diag_build(board, previous.sessions, injects);
  pscu_diag_encode(record, raw);
  for (uint8_t i = 0U; i < PSCU_DIAG_EEPROM_BYTES; i++) {
    pscu_port_eeprom_write((uint8_t)(PSCU_DIAG_EEPROM_ADDR + i), raw[i]);
  }
}
