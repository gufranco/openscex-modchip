// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "pscu/led.h"

#include <stdbool.h>
#include <stdint.h>

#include "pscu/assert.h"

// A flash pattern is count flashes of on_ms, each followed by a gap, then a
// pause before it repeats. Phase is folded into one cycle, then split into
// flash slots: lit for the first on_ms of a slot that belongs to a flash. Pure
// arithmetic on elapsed time, so the pattern never needs a delay to draw.
static uint32_t pscu_led_cycle_ms(uint32_t count, uint32_t on_ms, uint32_t pause_ms) {
  return (count * (on_ms + PSCU_LED_GAP_MS)) + pause_ms;
}

static bool pscu_led_flash_lit(uint32_t count,
                               uint32_t on_ms,
                               uint32_t pause_ms,
                               uint32_t phase_ms) {
  uint32_t at = phase_ms % pscu_led_cycle_ms(count, on_ms, pause_ms);
  uint32_t slot = on_ms + PSCU_LED_GAP_MS;
  return ((at / slot) < count) && ((at % slot) < on_ms);
}

static pscu_led_t pscu_led_enter(pscu_led_t state,
                                 pscu_led_stage_t stage,
                                 uint8_t code,
                                 bool live) {
  pscu_led_t next = state;
  next.stage = stage;
  next.code = code;
  next.live = live;
  next.phase_ms = 0U;
  return next;
}

pscu_led_t pscu_led_init(uint8_t board_blinks, uint8_t replay_code) {
  PSCU_ASSERT((board_blinks == 1U) || (board_blinks == 2U));
  PSCU_ASSERT(replay_code <= PSCU_LED_CODE_BOARD_CHANGED);

  pscu_led_t state = { PSCU_LED_BOARD, 0U, false, board_blinks, replay_code, 0U };
  return state;
}

// An event always wins: a region string takes the LED for injection, a resolved
// session shows its result. Without one, a live fault takes over the waiting,
// injecting or dark display, or replaces a different code; when the fault
// clears, a live code drops back to the heartbeat. The boot blinks and the
// replay are left to finish, so they always read cleanly.
static pscu_led_t pscu_led_apply(pscu_led_t state, pscu_led_event_t event, uint8_t fault) {
  pscu_led_t next = state;
  bool idle = (state.stage == PSCU_LED_WAIT) || (state.stage == PSCU_LED_INJECT) ||
              (state.stage == PSCU_LED_DARK);
  bool other_code = (state.stage == PSCU_LED_CODE) && (state.code != fault);

  if (event == PSCU_LED_EVENT_FIRED) {
    next = pscu_led_enter(state, PSCU_LED_INJECT, 0U, false);
  } else if (event == PSCU_LED_EVENT_ACCEPTED) {
    next = pscu_led_enter(state, PSCU_LED_CODE, PSCU_LED_CODE_ACCEPTED, false);
  } else if (event == PSCU_LED_EVENT_REFUSED) {
    next = pscu_led_enter(state, PSCU_LED_CODE, PSCU_LED_CODE_REFUSED, false);
  } else if ((fault != 0U) && (idle || other_code)) {
    next = pscu_led_enter(state, PSCU_LED_CODE, fault, true);
  } else if ((fault == 0U) && (state.stage == PSCU_LED_CODE) && state.live) {
    next = pscu_led_enter(state, PSCU_LED_WAIT, 0U, false);
  } else {
  }
  return next;
}

// Stages that end on their own: the boot blinks after one pass, the replay after
// one cycle, a disc result after three. A finished result goes dark, which is
// what keeps the LED off during play.
static pscu_led_t pscu_led_advance(pscu_led_t state) {
  pscu_led_t next = state;
  uint32_t short_done = (uint32_t)state.board * (PSCU_LED_SHORT_MS + PSCU_LED_GAP_MS);
  uint32_t long_cycle = pscu_led_cycle_ms(state.code, PSCU_LED_LONG_MS, PSCU_LED_PAUSE_MS);
  uint32_t replay_done = pscu_led_cycle_ms(state.replay, PSCU_LED_LONG_MS, PSCU_LED_PAUSE_MS);

  if ((state.stage == PSCU_LED_BOARD) && (state.phase_ms >= short_done)) {
    next = pscu_led_enter(state, (state.replay != 0U) ? PSCU_LED_REPLAY : PSCU_LED_WAIT, 0U, false);
  } else if ((state.stage == PSCU_LED_REPLAY) && (state.phase_ms >= replay_done)) {
    next = pscu_led_enter(state, PSCU_LED_WAIT, 0U, false);
  } else if ((state.stage == PSCU_LED_CODE) && !state.live &&
             (state.phase_ms >= (PSCU_LED_RESULT_REPEATS * long_cycle))) {
    next = pscu_led_enter(state, PSCU_LED_DARK, 0U, false);
  } else {
  }
  return next;
}

// The waiting heartbeat blips at the end of each period, so the first blip comes
// a full period after the display starts waiting rather than right away.
static bool pscu_led_lit(pscu_led_t state) {
  bool lit = false;
  if (state.stage == PSCU_LED_BOARD) {
    lit = pscu_led_flash_lit(state.board, PSCU_LED_SHORT_MS, 0U, state.phase_ms);
  } else if (state.stage == PSCU_LED_REPLAY) {
    lit = pscu_led_flash_lit(state.replay, PSCU_LED_LONG_MS, PSCU_LED_PAUSE_MS, state.phase_ms);
  } else if (state.stage == PSCU_LED_CODE) {
    lit = pscu_led_flash_lit(state.code, PSCU_LED_LONG_MS, PSCU_LED_PAUSE_MS, state.phase_ms);
  } else if (state.stage == PSCU_LED_WAIT) {
    lit = (state.phase_ms % PSCU_LED_BEAT_PERIOD_MS) >=
          (PSCU_LED_BEAT_PERIOD_MS - PSCU_LED_BEAT_ON_MS);
  } else {
  }
  return lit;
}

pscu_led_step_t pscu_led_step(pscu_led_t state,
                              pscu_led_event_t event,
                              uint8_t fault,
                              uint32_t elapsed_ms) {
  PSCU_ASSERT(fault <= PSCU_LED_CODE_BOARD_CHANGED);

  // Saturating add: unsigned addition wraps, so a sum below the old phase means
  // it overflowed and the phase pins at the maximum instead of restarting the
  // pattern. A zero step leaves the sum equal to the old phase, never pinned.
  pscu_led_t timed = state;
  uint32_t sum = state.phase_ms + elapsed_ms;
  timed.phase_ms = (sum < state.phase_ms) ? 0xFFFFFFFFUL : sum;

  pscu_led_step_t out;
  out.state = pscu_led_advance(pscu_led_apply(timed, event, fault));
  out.on = pscu_led_lit(out.state);
  return out;
}

uint8_t pscu_led_fault(bool lid_open, uint32_t ms_since_close, bool framed, bool armed) {
  uint8_t fault = 0U;
  if (lid_open) {
    fault = PSCU_LED_CODE_LID;
  } else if (!framed && (ms_since_close >= PSCU_LED_NO_SQCK_MS)) {
    fault = PSCU_LED_CODE_NO_SQCK;
  } else if (framed && !armed && (ms_since_close >= PSCU_LED_NO_CHECK_MS)) {
    fault = PSCU_LED_CODE_NO_CHECK;
  } else {
  }
  return fault;
}
