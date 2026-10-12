// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "pscu/led.h"

#include <stdbool.h>
#include <stdint.h>

#include "pscu/assert.h"

// Helpers update the state through a pointer: by-value copies of this struct
// do not fit the flash. The public step stays by value.

// A pattern is count flashes of on_ms, each followed by a gap, then a pause.
static uint32_t pscu_led_cycle_ms(uint32_t count, uint32_t on_ms, uint32_t pause_ms) {
  return (count * (on_ms + PSCU_LED_GAP_MS)) + pause_ms;
}

// Every cycle is under 65536 ms (the longest is 37 s), so the slot arithmetic
// runs in 16 bits.
static bool pscu_led_flash_lit(uint32_t count,
                               uint32_t on_ms,
                               uint32_t pause_ms,
                               uint32_t phase_ms) {
  uint16_t at = (uint16_t)(phase_ms % pscu_led_cycle_ms(count, on_ms, pause_ms));
  uint16_t slot = (uint16_t)(on_ms + PSCU_LED_GAP_MS);
  return ((uint32_t)(at / slot) < count) && ((uint32_t)(at % slot) < on_ms);
}

static void pscu_led_enter(pscu_led_t *state, pscu_led_stage_t stage, uint8_t code, bool live) {
  state->stage = stage;
  state->code = code;
  state->live = live;
  state->phase_ms = 0U;
}

pscu_led_t pscu_led_init(uint8_t board_blinks, uint8_t replay_code, pscu_led_profile_t profile) {
  PSCU_ASSERT((board_blinks == 1U) || (board_blinks == 2U));
  PSCU_ASSERT(replay_code <= PSCU_LED_CODE_SUPPLY);
  PSCU_ASSERT((profile == PSCU_LED_PROFILE_DEBUG) || (profile == PSCU_LED_PROFILE_FINAL));

  pscu_led_t state = { PSCU_LED_BOARD, 0U, false, board_blinks, replay_code, 0U, profile, 0U };
  return state;
}

static bool pscu_led_final(pscu_led_profile_t profile) {
  return profile == PSCU_LED_PROFILE_FINAL;
}

// One bit per code in the told mask.
static uint8_t pscu_led_bit(uint8_t code) {
  return (uint8_t)(1U << code);
}

// Final shows an accepted disc as a chirp and spaces the supply code by the
// backoff.
static uint32_t pscu_led_code_on_ms(bool final, uint8_t code) {
  bool chirp = final && (code == PSCU_LED_CODE_ACCEPTED);
  return chirp ? PSCU_LED_CHIRP_MS : PSCU_LED_LONG_MS;
}

static uint32_t pscu_led_code_pause_ms(bool final, uint8_t code) {
  bool backoff = final && (code == PSCU_LED_CODE_SUPPLY);
  return backoff ? PSCU_LED_SUPPLY_BACKOFF_MS : PSCU_LED_PAUSE_MS;
}

// How many times a disc result plays before the display goes dark.
static uint32_t pscu_led_result_repeats(bool final, uint8_t code) {
  bool accepted = code == PSCU_LED_CODE_ACCEPTED;
  uint32_t final_repeats = accepted ? 1U : PSCU_LED_FINAL_REFUSED_REPEATS;
  return final ? final_repeats : PSCU_LED_RESULT_REPEATS;
}

static uint32_t pscu_led_board_on_ms(bool final) {
  return final ? PSCU_LED_CHIRP_MS : PSCU_LED_SHORT_MS;
}

// An event always wins. Otherwise a live fault takes over any stage but the
// boot patterns, unless already told, and a cleared fault returns to the
// heartbeat.
static void pscu_led_apply(pscu_led_t *state, pscu_led_event_t event, uint8_t fault) {
  bool idle = (state->stage == PSCU_LED_WAIT) || (state->stage == PSCU_LED_INJECT) ||
              (state->stage == PSCU_LED_DARK);
  bool other_code = (state->stage == PSCU_LED_CODE) && (state->code != fault);
  bool told = (state->told & pscu_led_bit(fault)) != 0U;

  if (event == PSCU_LED_EVENT_FIRED) {
    pscu_led_enter(state, PSCU_LED_INJECT, 0U, false);
  } else if (event == PSCU_LED_EVENT_ACCEPTED) {
    pscu_led_enter(state, PSCU_LED_CODE, PSCU_LED_CODE_ACCEPTED, false);
  } else if (event == PSCU_LED_EVENT_REFUSED) {
    pscu_led_enter(state, PSCU_LED_CODE, PSCU_LED_CODE_REFUSED, false);
  } else if ((fault != 0U) && (idle || other_code) && !told) {
    pscu_led_enter(state, PSCU_LED_CODE, fault, true);
  } else if ((fault == 0U) && (state->stage == PSCU_LED_CODE) && state->live) {
    pscu_led_enter(state, PSCU_LED_WAIT, 0U, false);
  } else {
  }
}

// End the stages that time out. A finished result goes dark for play. In final,
// a live fault other than the supply plays once and is marked told.
static void pscu_led_advance(pscu_led_t *state) {
  bool final = pscu_led_final(state->profile);
  uint32_t short_done = pscu_led_cycle_ms(state->board, pscu_led_board_on_ms(final), 0U);
  uint32_t code_cycle = pscu_led_cycle_ms(state->code,
                                          pscu_led_code_on_ms(final, state->code),
                                          pscu_led_code_pause_ms(final, state->code));
  uint32_t replay_done = pscu_led_cycle_ms(state->replay, PSCU_LED_LONG_MS, PSCU_LED_PAUSE_MS);
  uint32_t result_done = pscu_led_result_repeats(final, state->code) * code_cycle;
  bool coding = state->stage == PSCU_LED_CODE;
  bool once = final && (state->code != PSCU_LED_CODE_SUPPLY);
  uint8_t told = (uint8_t)(state->told | pscu_led_bit(state->code));

  if ((state->stage == PSCU_LED_BOARD) && (state->phase_ms >= short_done)) {
    pscu_led_enter(state, (state->replay != 0U) ? PSCU_LED_REPLAY : PSCU_LED_WAIT, 0U, false);
  } else if ((state->stage == PSCU_LED_REPLAY) && (state->phase_ms >= replay_done)) {
    pscu_led_enter(state, PSCU_LED_WAIT, 0U, false);
  } else if (coding && !state->live && (state->phase_ms >= result_done)) {
    pscu_led_enter(state, PSCU_LED_DARK, 0U, false);
  } else if (coding && state->live && once && (state->phase_ms >= code_cycle)) {
    pscu_led_enter(state, PSCU_LED_WAIT, 0U, false);
    state->told = told;
  } else {
  }
}

// The heartbeat blips at the end of each period; final waits in silence. All
// patterns share one draw call; a stage without one keeps count 0 and the 2 s
// pause, so the cycle is never zero.
static bool pscu_led_lit(const pscu_led_t *state) {
  bool final = pscu_led_final(state->profile);
  bool beat = (state->phase_ms % PSCU_LED_BEAT_PERIOD_MS) >=
              (PSCU_LED_BEAT_PERIOD_MS - PSCU_LED_BEAT_ON_MS);
  uint32_t count = 0U;
  uint32_t on_ms = PSCU_LED_LONG_MS;
  uint32_t pause_ms = PSCU_LED_PAUSE_MS;
  bool lit = false;
  if (state->stage == PSCU_LED_BOARD) {
    count = state->board;
    on_ms = pscu_led_board_on_ms(final);
    pause_ms = 0U;
  } else if (state->stage == PSCU_LED_REPLAY) {
    count = state->replay;
  } else if (state->stage == PSCU_LED_CODE) {
    count = state->code;
    on_ms = pscu_led_code_on_ms(final, state->code);
    pause_ms = pscu_led_code_pause_ms(final, state->code);
  } else if (state->stage == PSCU_LED_WAIT) {
    lit = !final && beat;
  } else if (state->stage == PSCU_LED_DARK) {
    lit = (state->code != 0U) && (state->phase_ms < PSCU_LED_TICK_MS);
  } else {
  }
  return lit || pscu_led_flash_lit(count, on_ms, pause_ms, state->phase_ms);
}

pscu_led_t pscu_led_disc_arrived(pscu_led_t state) {
  pscu_led_t next = state;
  bool result = (state.stage == PSCU_LED_CODE) && !state.live;
  bool leftover = (state.stage == PSCU_LED_INJECT) || (state.stage == PSCU_LED_DARK);
  next.told = 0U;
  if (result || leftover) {
    pscu_led_enter(&next, PSCU_LED_WAIT, 0U, false);
  }
  return next;
}

// A tick restarts the dark stage with code 1.
pscu_led_t pscu_led_note(pscu_led_t state) {
  pscu_led_t next = state;
  if (!pscu_led_final(state.profile) && (state.stage == PSCU_LED_DARK)) {
    pscu_led_enter(&next, PSCU_LED_DARK, 1U, false);
  }
  return next;
}

pscu_led_step_t pscu_led_step(pscu_led_t state,
                              pscu_led_event_t event,
                              uint8_t fault,
                              uint32_t elapsed_ms) {
  PSCU_ASSERT(fault <= PSCU_LED_CODE_SUPPLY);

  // Saturating add.
  pscu_led_step_t out;
  out.state = state;
  uint32_t sum = state.phase_ms + elapsed_ms;
  // coverage: unreachable: saturating needs one stage to last 49.7 days.
  out.state.phase_ms = (sum < state.phase_ms) ? 0xFFFFFFFFUL : sum;
  pscu_led_apply(&out.state, event, fault);
  pscu_led_advance(&out.state);
  out.on = pscu_led_lit(&out.state);
  return out;
}

uint8_t pscu_led_fault(
    bool supply_low, bool seen, uint32_t quiet_ms, uint32_t since_ms, bool framed, bool armed) {
  uint8_t fault = 0U;
  bool install_check = (quiet_ms >= PSCU_LED_NO_SQCK_MS) && (quiet_ms < PSCU_LED_NO_SQCK_UNTIL_MS);
  if (supply_low) {
    fault = PSCU_LED_CODE_SUPPLY;
  } else if (!seen && install_check) {
    fault = PSCU_LED_CODE_NO_SQCK;
  } else if (framed && !armed && (since_ms >= PSCU_LED_NO_CHECK_MS)) {
    fault = PSCU_LED_CODE_NO_CHECK;
  } else {
  }
  return fault;
}
