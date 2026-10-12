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

// More ticks than nominal means the RC runs fast, so OSCCAL comes down. One
// notch per dead band of error, at most PSCU_TRIM_MAX_STEP.
static int8_t pscu_trim_verdict(uint32_t ticks, uint32_t expected) {
  uint32_t band_ticks = expected / PSCU_TRIM_DEADBAND_DIVISOR;
  int32_t band = (int32_t)band_ticks;
  int32_t error = (int32_t)ticks - (int32_t)expected;
  // Subtract one band per pass instead of dividing.
  int8_t size = 0;
  for (uint8_t pass = 0U; pass < (uint8_t)PSCU_TRIM_MAX_STEP; pass++) {
    if (error > band) {
      size--;
      error -= band;
    } else if (error < -band) {
      size++;
      error += band;
    } else {
    }
  }
  return size;
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

// Within PSCU_TRIM_MAX_OFFSET of factory and in the same CAL7 half. A wrapped
// value fails these checks too, so no overflow test is needed.
static bool pscu_trim_allowed(uint8_t factory, uint8_t value) {
  int16_t offset = (int16_t)((int16_t)value - (int16_t)factory);
  bool same_range = ((uint8_t)(value ^ factory) & PSCU_TRIM_RANGE_BIT) == 0U;
  bool near =
      (offset >= -(int16_t)PSCU_TRIM_MAX_OFFSET) && (offset <= (int16_t)PSCU_TRIM_MAX_OFFSET);
  return same_range && near;
}

uint8_t pscu_trim_apply(uint8_t factory, uint8_t current, int8_t adjust) {
  PSCU_ASSERT((adjust >= -PSCU_TRIM_MAX_STEP) && (adjust <= PSCU_TRIM_MAX_STEP));

  // Walk one notch at a time and stop at the first one not allowed.
  uint8_t raw = (uint8_t)adjust;
  bool down = (raw & PSCU_TRIM_RANGE_BIT) != 0U;
  uint8_t notches = down ? (uint8_t)(0U - raw) : raw;
  uint8_t moved = current;
  bool open = true;
  for (uint8_t n = 0U; (n < notches) && open; n++) {
    uint8_t next = down ? (uint8_t)(moved - 1U) : (uint8_t)(moved + 1U);
    open = pscu_trim_allowed(factory, next);
    moved = open ? next : moved;
  }
  return moved;
}
