// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "pscu/inject.h"

#include "pscu/assert.h"

// Inject once the leaky SUBQ counter has reached the trigger, meaning the
// console has settled on the region-check window and wants the string now.
bool pscu_should_inject(uint8_t counter, uint8_t trigger) {
  PSCU_ASSERT(trigger > 0U);

  return counter >= trigger;
}

// After injecting, drop the counter back by `gap` instead of to zero. This sets
// a cool-off: the next inject needs `gap` fresh hits, which spaces repeated
// injections without fully disarming, so a brief SUBQ dropout does not restart
// from cold.
uint8_t pscu_counter_after_inject(uint8_t trigger, uint8_t gap) {
  PSCU_ASSERT(gap < trigger);

  uint8_t result = (uint8_t)(trigger - gap);

  PSCU_ASSERT((result >= 1U) && (result <= trigger));

  return result;
}
