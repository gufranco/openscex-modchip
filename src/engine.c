// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "pscu/engine.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "port/port.h"
#include "pscu/assert.h"
#include "pscu/board_mode.h"
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
// Idle gap between the three region words, matching the console's inter-string
// spacing so each word is read as a separate attempt.
#define PSCU_INTER_REGION_MS ((uint16_t)90U)
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

// Emit all three region words back to back. The console only boots a disc whose
// region it recognises, so sending Japan, America and Europe in turn unlocks
// every region. The LED is lit for the burst as a visible injection marker and
// DATA is left released (high-Z) afterwards so it never fights the bus.
void pscu_engine_inject(pscu_board_mode_t board) {
  PSCU_ASSERT((board == PSCU_BOARD_MODE_GATE) || (board == PSCU_BOARD_MODE_WFCK));

  pscu_port_led_on();
  for (uint8_t region = 0U; region < PSCU_REGION_COUNT; region++) {
    pscu_inject_region((pscu_region_t)region, board);
    pscu_port_data_drive_low();
    pscu_port_delay_ms(PSCU_INTER_REGION_MS);
  }
  pscu_port_data_release();
  pscu_port_led_off();
}
