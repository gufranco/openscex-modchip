// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "pscu/inject.h"

#include "pscu/assert.h"

bool pscu_should_inject(uint8_t counter, uint8_t trigger) {
  PSCU_ASSERT(trigger > 0U);

  return counter >= trigger;
}

uint8_t pscu_counter_after_inject(uint8_t trigger, uint8_t gap) {
  PSCU_ASSERT(gap < trigger);

  uint8_t result = (uint8_t)(trigger - gap);

  PSCU_ASSERT((result >= 1U) && (result <= trigger));

  return result;
}
