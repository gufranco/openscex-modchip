// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "pscu/trim.h"

#include <stdbool.h>
#include <stdint.h>

#include "pscu/assert.h"

pscu_trim_t pscu_trim_init(void) {
  pscu_trim_t state = { 0U, 0U };
  return state;
}

// More ticks than nominal means the RC counted faster than real time, so it
// runs fast and OSCCAL must come down; fewer means it runs slow. Inside the dead
// band the trim holds still, so a chip within 1 percent never dithers.
static int8_t pscu_trim_verdict(uint32_t ticks, uint32_t expected) {
  uint32_t band = expected / PSCU_TRIM_DEADBAND_DIVISOR;
  int8_t adjust = 0;
  if (ticks > (expected + band)) {
    adjust = -1;
  } else if ((ticks + band) < expected) {
    adjust = 1;
  } else {
  }
  return adjust;
}

pscu_trim_step_t pscu_trim_step(pscu_trim_t state,
                                bool sample,
                                uint16_t delta,
                                pscu_trim_ref_t ref) {
  PSCU_ASSERT(ref.low < ref.high);
  PSCU_ASSERT(state.frames < PSCU_TRIM_FRAMES);

  pscu_trim_step_t out;
  out.state = state;
  out.adjust = 0;
  if (sample && (delta >= ref.low) && (delta <= ref.high)) {
    out.state.frames = (uint16_t)(state.frames + 1U);
    out.state.ticks = state.ticks + delta;
  }
  if (out.state.frames == PSCU_TRIM_FRAMES) {
    out.adjust = pscu_trim_verdict(out.state.ticks, ref.expected);
    out.state = pscu_trim_init();
  }
  return out;
}

// Signed distance from the factory value, in OSCCAL steps.
static int16_t pscu_trim_offset(uint8_t factory, uint8_t value) {
  return (int16_t)((int16_t)value - (int16_t)factory);
}

uint8_t pscu_trim_apply(uint8_t factory, uint8_t current, int8_t adjust) {
  PSCU_ASSERT((adjust >= -1) && (adjust <= 1));

  // A step below 0 or above 255 wraps to the far end of the register, which is
  // always the other CAL7 range or far past the offset bound, so the two checks
  // below refuse it with no separate overflow test.
  int16_t moved = (int16_t)((int16_t)current + (int16_t)adjust);
  uint8_t next = (uint8_t)moved;
  int16_t offset = pscu_trim_offset(factory, next);
  bool same_range = ((uint8_t)(next ^ factory) & PSCU_TRIM_RANGE_BIT) == 0U;
  bool near =
      (offset >= -(int16_t)PSCU_TRIM_MAX_OFFSET) && (offset <= (int16_t)PSCU_TRIM_MAX_OFFSET);
  return (same_range && near) ? next : current;
}
