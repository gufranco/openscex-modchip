// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "pscu/subq.h"

#include <stddef.h>

#include "pscu/assert.h"

bool pscu_subq_is_data_sector(uint8_t control) {
  return (uint8_t)(control & 0xD0U) == 0x40U;
}

static bool pscu_subq_lead_in_hit(const uint8_t *frame) {
  if (!pscu_subq_is_data_sector(frame[0])) {
    return false;
  }
  if (frame[2] >= 0xA0U) {
    return true;
  }
  if (frame[2] != 0x01U) {
    return false;
  }
  return (uint8_t)(frame[3] - 0x03U) >= 0xF5U;
}

static bool pscu_subq_tracking_hit(const uint8_t *frame, uint8_t counter) {
  if (counter == 0U) {
    return false;
  }
  return (frame[0] == 0x01U) || pscu_subq_is_data_sector(frame[0]);
}

uint8_t pscu_subq_update_counter(const uint8_t *frame, uint8_t counter) {
  PSCU_ASSERT(frame != NULL);

  bool framed = (frame[1] == 0x00U) && (frame[6] == 0x00U);
  bool hit = framed &&
             (pscu_subq_lead_in_hit(frame) || pscu_subq_tracking_hit(frame, counter));

  if (hit) {
    return (counter < PSCU_SUBQ_COUNTER_MAX) ? (uint8_t)(counter + 1U) : counter;
  }
  if (counter > 0U) {
    return (uint8_t)(counter - 1U);
  }
  return counter;
}
