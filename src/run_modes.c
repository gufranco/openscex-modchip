// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include <stdbool.h>
#include <stdint.h>

#include "port/port.h"
#include "pscu/board_mode.h"
#include "pscu/engine.h"
#include "pscu/inject.h"
#include "pscu/mode.h"
#include "pscu/run.h"
#include "pscu/subq.h"

#define PSCU_MODE_EEPROM_ADDR ((uint8_t)0U)
#define PSCU_MODE_WINDOW ((uint16_t)3000U)
#define PSCU_RESET_THRESHOLD ((uint16_t)2000U)
#define PSCU_LID_THRESHOLD ((uint16_t)1U)

static pscu_mode_t pscu_mode_restore(void) {
  uint8_t stored = pscu_port_eeprom_read(PSCU_MODE_EEPROM_ADDR);
  pscu_mode_t mode = PSCU_MODE_DEFAULT;

  if (stored < PSCU_MODE_COUNT) {
    mode = (pscu_mode_t)stored;
  }

  return mode;
}

static bool pscu_mode_gesture(void) {
  bool fire = false;

  if ((pscu_port_read_reset() == 0U) || (pscu_port_read_lid() == 0U)) {
    pscu_gesture_t reset_g = pscu_gesture_init();
    pscu_gesture_t lid_g = pscu_gesture_init();
    for (uint16_t i = 0U; i < PSCU_MODE_WINDOW; i++) {
      uint8_t reset_active = (uint8_t)((pscu_port_read_reset() == 0U) ? 1U : 0U);
      uint8_t lid_active = (uint8_t)((pscu_port_read_lid() == 0U) ? 1U : 0U);
      reset_g = pscu_gesture_step(reset_g, reset_active, PSCU_RESET_THRESHOLD);
      lid_g = pscu_gesture_step(lid_g, lid_active, PSCU_LID_THRESHOLD);
      if ((reset_g.fired != 0U) || (lid_g.fired != 0U)) {
        fire = true;
      }
      pscu_port_delay_ms(1U);
      pscu_port_watchdog_reset();
    }
  }

  return fire;
}

static pscu_mode_t pscu_mode_select(void) {
  pscu_mode_t mode = pscu_mode_restore();

  if (pscu_mode_gesture()) {
    mode = pscu_mode_next(mode);
    pscu_port_eeprom_write(PSCU_MODE_EEPROM_ADDR, (uint8_t)mode);
  }

  return mode;
}

void pscu_run(void) {
  pscu_board_mode_t board = pscu_engine_detect_board();
  pscu_mode_t cfg = pscu_mode_select();
  uint8_t counter = 0U;

  for (;;) {
    uint8_t frame[PSCU_SUBQ_FRAME_BYTES];
    pscu_engine_capture_frame(frame);
    counter = pscu_subq_update_counter(frame, counter);

    if (cfg == PSCU_MODE_OLD_MODCHIP) {
      pscu_engine_inject(board);
    } else if ((cfg != PSCU_MODE_DISABLED) &&
               pscu_should_inject(counter, PSCU_INJECT_TRIGGER)) {
      pscu_engine_inject(board);
      counter = pscu_counter_after_inject(PSCU_INJECT_TRIGGER, PSCU_INJECT_GAP);
    } else {
    }

    pscu_port_watchdog_reset();
  }
}
