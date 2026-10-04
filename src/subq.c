// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "pscu/subq.h"

#include <stdbool.h>
#include <stddef.h>

#include "pscu/assert.h"

static bool pscu_subq_is_data_sector(uint8_t control) {
  return (uint8_t)(control & 0xD0U) == 0x40U;
}

static bool pscu_subq_lead_in_hit(const uint8_t *frame) {
  PSCU_ASSERT(frame != NULL);

  bool hit = false;

  if (pscu_subq_is_data_sector(frame[0])) {
    if (frame[2] >= 0xA0U) {
      hit = true;
    } else if (frame[2] == 0x01U) {
      hit = (uint8_t)(frame[3] - 0x03U) >= 0xF5U;
    } else {
      hit = false;
    }
  }

  return hit;
}

static bool pscu_subq_tracking_hit(const uint8_t *frame, uint8_t counter) {
  PSCU_ASSERT(frame != NULL);

  bool hit = false;

  if (counter > 0U) {
    hit = (frame[0] == 0x01U) || pscu_subq_is_data_sector(frame[0]);
  }

  return hit;
}

uint8_t pscu_subq_update_counter(const uint8_t *frame, uint8_t counter) {
  PSCU_ASSERT(frame != NULL);

  bool framed = (frame[1] == 0x00U) && (frame[6] == 0x00U);
  bool hit = framed &&
             (pscu_subq_lead_in_hit(frame) || pscu_subq_tracking_hit(frame, counter));
  uint8_t result = counter;

  if (hit && (counter < PSCU_SUBQ_COUNTER_MAX)) {
    result = (uint8_t)(counter + 1U);
  }
  if ((!hit) && (counter > 0U)) {
    result = (uint8_t)(counter - 1U);
  }

  PSCU_ASSERT((uint8_t)((result - counter) + 1U) <= 2U);

  return result;
}
