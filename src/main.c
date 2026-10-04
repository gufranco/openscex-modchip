// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include <stdbool.h>
#include <stdint.h>

#include "port/port.h"
#include "pscu/board_mode.h"
#include "pscu/inject.h"
#include "pscu/region.h"
#include "pscu/subq.h"

#define PSCU_INJECT_TRIGGER ((uint8_t)10U)
#define PSCU_INJECT_GAP ((uint8_t)5U)
#define PSCU_DETECT_WINDOW ((uint16_t)10000U)
#define PSCU_DETECT_PULSES ((uint8_t)25U)
#define PSCU_WAIT_MAX ((uint16_t)0xFFFFU)
#define PSCU_BIT_MS ((uint16_t)4U)
#define PSCU_INTER_REGION_MS ((uint16_t)90U)
#define PSCU_SUBQ_BITS ((uint8_t)8U)
#define PSCU_SUBQ_MSB ((uint8_t)0x80U)

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

static void pscu_capture_frame(uint8_t *frame) {
  for (uint8_t byte = 0U; byte < PSCU_SUBQ_FRAME_BYTES; byte++) {
    frame[byte] = pscu_capture_byte();
  }
}

static void pscu_inject_bit(uint8_t bit_value, pscu_board_mode_t mode) {
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
  for (uint8_t bit = 0U; bit < PSCU_SCEX_BIT_COUNT; bit++) {
    uint8_t bit_value = pscu_region_bit(region, bit);
    pscu_inject_bit(bit_value, mode);
  }
}

static void pscu_inject_all(pscu_board_mode_t mode) {
  pscu_port_led_on();
  for (uint8_t region = 0U; region < PSCU_REGION_COUNT; region++) {
    pscu_inject_region((pscu_region_t)region, mode);
    pscu_port_data_drive_low();
    pscu_port_delay_ms(PSCU_INTER_REGION_MS);
  }
  pscu_port_data_release();
  pscu_port_led_off();
}

static pscu_board_mode_t pscu_detect_board(void) {
  pscu_board_detect_t state = pscu_board_detect_init();
  for (uint16_t i = 0U; i < PSCU_DETECT_WINDOW; i++) {
    state = pscu_board_detect_step(state, pscu_port_read_wfck());
    pscu_port_watchdog_reset();
  }
  return pscu_board_detect_mode(state, PSCU_DETECT_PULSES);
}

int main(void) {
  pscu_port_init();
  pscu_board_mode_t mode = pscu_detect_board();
  uint8_t counter = 0U;

  for (;;) {
    uint8_t frame[PSCU_SUBQ_FRAME_BYTES];
    pscu_capture_frame(frame);
    counter = pscu_subq_update_counter(frame, counter);
    if (pscu_should_inject(counter, PSCU_INJECT_TRIGGER)) {
      pscu_inject_all(mode);
      counter = pscu_counter_after_inject(PSCU_INJECT_TRIGGER, PSCU_INJECT_GAP);
    }
    pscu_port_watchdog_reset();
  }
}
