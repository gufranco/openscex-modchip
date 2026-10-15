// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "pscu/indicator.h"

#include <stdbool.h>
#include <stdint.h>

#include "pscu/assert.h"

// Helpers update the state through a pointer: by-value copies of this struct
// do not fit the flash. The public step stays by value.

// A pattern is count flashes of on_ms, each followed by a gap, then a pause.
static uint32_t pscu_indicator_cycle_ms(uint32_t count, uint32_t on_ms, uint32_t pause_ms) {
  return (count * (on_ms + PSCU_INDICATOR_GAP_MS)) + pause_ms;
}

// Every cycle is under 65536 ms (the longest is 37 s), so the slot arithmetic
// runs in 16 bits.
static bool pscu_indicator_flash_lit(uint32_t count,
                                     uint32_t on_ms,
                                     uint32_t pause_ms,
                                     uint32_t phase_ms) {
  uint16_t at = (uint16_t)(phase_ms % pscu_indicator_cycle_ms(count, on_ms, pause_ms));
  uint16_t slot = (uint16_t)(on_ms + PSCU_INDICATOR_GAP_MS);
  return ((uint32_t)(at / slot) < count) && ((uint32_t)(at % slot) < on_ms);
}

static void pscu_indicator_enter(pscu_indicator_t *state,
                                 pscu_indicator_stage_t stage,
                                 uint8_t code,
                                 bool live) {
  state->stage = stage;
  state->code = code;
  state->live = live;
  state->phase_ms = 0U;
}

pscu_indicator_t pscu_indicator_init(uint8_t board_blinks,
                                     uint8_t replay_code,
                                     pscu_indicator_profile_t profile) {
  PSCU_ASSERT((board_blinks == 1U) || (board_blinks == 2U));
  PSCU_ASSERT(replay_code <= PSCU_INDICATOR_CODE_SUPPLY);
  PSCU_ASSERT((profile == PSCU_INDICATOR_PROFILE_DEBUG) ||
              (profile == PSCU_INDICATOR_PROFILE_FINAL));

  pscu_indicator_t state = { PSCU_INDICATOR_BOARD, 0U, false,   board_blinks,
                             replay_code,          0U, profile, 0U };
  return state;
}

static bool pscu_indicator_final(pscu_indicator_profile_t profile) {
  return profile == PSCU_INDICATOR_PROFILE_FINAL;
}

// One bit per code in the told mask.
static uint8_t pscu_indicator_bit(uint8_t code) {
  return (uint8_t)(1U << code);
}

// Final shows an accepted disc as a chirp and spaces the supply code by the
// backoff.
static uint32_t pscu_indicator_code_on_ms(bool final, uint8_t code) {
  bool chirp = final && (code == PSCU_INDICATOR_CODE_ACCEPTED);
  return chirp ? PSCU_INDICATOR_CHIRP_MS : PSCU_INDICATOR_LONG_MS;
}

static uint32_t pscu_indicator_code_pause_ms(bool final, uint8_t code) {
  bool backoff = final && (code == PSCU_INDICATOR_CODE_SUPPLY);
  return backoff ? PSCU_INDICATOR_SUPPLY_BACKOFF_MS : PSCU_INDICATOR_PAUSE_MS;
}

// How many times a disc result plays before the display goes dark.
static uint32_t pscu_indicator_result_repeats(bool final, uint8_t code) {
  bool accepted = code == PSCU_INDICATOR_CODE_ACCEPTED;
  uint32_t final_repeats = accepted ? 1U : PSCU_INDICATOR_FINAL_REFUSED_REPEATS;
  return final ? final_repeats : PSCU_INDICATOR_RESULT_REPEATS;
}

static uint32_t pscu_indicator_board_on_ms(bool final) {
  return final ? PSCU_INDICATOR_CHIRP_MS : PSCU_INDICATOR_SHORT_MS;
}

// An event always wins. Otherwise a live fault takes over any stage but the
// boot patterns, unless already told, and a cleared fault returns to the
// heartbeat.
static void pscu_indicator_apply(pscu_indicator_t *state,
                                 pscu_indicator_event_t event,
                                 uint8_t fault) {
  bool idle = (state->stage == PSCU_INDICATOR_WAIT) || (state->stage == PSCU_INDICATOR_INJECT) ||
              (state->stage == PSCU_INDICATOR_DARK);
  bool other_code = (state->stage == PSCU_INDICATOR_CODE) && (state->code != fault);
  bool told = (state->told & pscu_indicator_bit(fault)) != 0U;

  if (event == PSCU_INDICATOR_EVENT_FIRED) {
    pscu_indicator_enter(state, PSCU_INDICATOR_INJECT, 0U, false);
  } else if (event == PSCU_INDICATOR_EVENT_ACCEPTED) {
    pscu_indicator_enter(state, PSCU_INDICATOR_CODE, PSCU_INDICATOR_CODE_ACCEPTED, false);
  } else if (event == PSCU_INDICATOR_EVENT_REFUSED) {
    pscu_indicator_enter(state, PSCU_INDICATOR_CODE, PSCU_INDICATOR_CODE_REFUSED, false);
  } else if ((fault != 0U) && (idle || other_code) && !told) {
    pscu_indicator_enter(state, PSCU_INDICATOR_CODE, fault, true);
  } else if ((fault == 0U) && (state->stage == PSCU_INDICATOR_CODE) && state->live) {
    pscu_indicator_enter(state, PSCU_INDICATOR_WAIT, 0U, false);
  } else {
  }
}

// End the stages that time out. A finished result goes dark for play. In final,
// a live fault other than the supply plays once and is marked told.
static void pscu_indicator_advance(pscu_indicator_t *state) {
  bool final = pscu_indicator_final(state->profile);
  uint32_t short_done =
      pscu_indicator_cycle_ms(state->board, pscu_indicator_board_on_ms(final), 0U);
  uint32_t code_cycle = pscu_indicator_cycle_ms(state->code,
                                                pscu_indicator_code_on_ms(final, state->code),
                                                pscu_indicator_code_pause_ms(final, state->code));
  uint32_t replay_done =
      pscu_indicator_cycle_ms(state->replay, PSCU_INDICATOR_LONG_MS, PSCU_INDICATOR_PAUSE_MS);
  uint32_t result_done = pscu_indicator_result_repeats(final, state->code) * code_cycle;
  bool coding = state->stage == PSCU_INDICATOR_CODE;
  bool once = final && (state->code != PSCU_INDICATOR_CODE_SUPPLY);
  uint8_t told = (uint8_t)(state->told | pscu_indicator_bit(state->code));

  if ((state->stage == PSCU_INDICATOR_BOARD) && (state->phase_ms >= short_done)) {
    pscu_indicator_enter(
        state, (state->replay != 0U) ? PSCU_INDICATOR_REPLAY : PSCU_INDICATOR_WAIT, 0U, false);
  } else if ((state->stage == PSCU_INDICATOR_REPLAY) && (state->phase_ms >= replay_done)) {
    pscu_indicator_enter(state, PSCU_INDICATOR_WAIT, 0U, false);
  } else if (coding && !state->live && (state->phase_ms >= result_done)) {
    pscu_indicator_enter(state, PSCU_INDICATOR_DARK, 0U, false);
  } else if (coding && state->live && once && (state->phase_ms >= code_cycle)) {
    pscu_indicator_enter(state, PSCU_INDICATOR_WAIT, 0U, false);
    state->told = told;
  } else {
  }
}

// The heartbeat blips at the end of each period; final waits in silence. All
// patterns share one draw call; a stage without one keeps count 0 and the 2 s
// pause, so the cycle is never zero.
static bool pscu_indicator_lit(const pscu_indicator_t *state) {
  bool final = pscu_indicator_final(state->profile);
  bool beat = (state->phase_ms % PSCU_INDICATOR_BEAT_PERIOD_MS) >=
              (PSCU_INDICATOR_BEAT_PERIOD_MS - PSCU_INDICATOR_BEAT_ON_MS);
  uint32_t count = 0U;
  uint32_t on_ms = PSCU_INDICATOR_LONG_MS;
  uint32_t pause_ms = PSCU_INDICATOR_PAUSE_MS;
  bool lit = false;
  if (state->stage == PSCU_INDICATOR_BOARD) {
    count = state->board;
    on_ms = pscu_indicator_board_on_ms(final);
    pause_ms = 0U;
  } else if (state->stage == PSCU_INDICATOR_REPLAY) {
    count = state->replay;
  } else if (state->stage == PSCU_INDICATOR_CODE) {
    count = state->code;
    on_ms = pscu_indicator_code_on_ms(final, state->code);
    pause_ms = pscu_indicator_code_pause_ms(final, state->code);
  } else if (state->stage == PSCU_INDICATOR_WAIT) {
    lit = !final && beat;
  } else if (state->stage == PSCU_INDICATOR_DARK) {
    lit = (state->code != 0U) && (state->phase_ms < PSCU_INDICATOR_TICK_MS);
  } else {
  }
  return lit || pscu_indicator_flash_lit(count, on_ms, pause_ms, state->phase_ms);
}

// The stages a new disc ends besides a shown result, as a bit set by stage:
// a string wait left behind and the dark play stage. One test for both keeps
// one path in the image; a string wait outlives its disc only when a session
// resolved while its burst was held below the start, which the bench cannot
// stage, so a separate path for it would never be executed there.
#define PSCU_INDICATOR_LEFTOVER \
  ((uint8_t)((1U << (uint8_t)PSCU_INDICATOR_INJECT) | (1U << (uint8_t)PSCU_INDICATOR_DARK)))

pscu_indicator_t pscu_indicator_disc_arrived(pscu_indicator_t state) {
  pscu_indicator_t next = state;
  bool result = (state.stage == PSCU_INDICATOR_CODE) && !state.live;
  bool leftover = ((PSCU_INDICATOR_LEFTOVER >> (uint8_t)state.stage) & 1U) != 0U;
  next.told = 0U;
  if (result || leftover) {
    pscu_indicator_enter(&next, PSCU_INDICATOR_WAIT, 0U, false);
  }
  return next;
}

// A tick restarts the dark stage with code 1.
pscu_indicator_t pscu_indicator_note(pscu_indicator_t state) {
  pscu_indicator_t next = state;
  if (!pscu_indicator_final(state.profile) && (state.stage == PSCU_INDICATOR_DARK)) {
    pscu_indicator_enter(&next, PSCU_INDICATOR_DARK, 1U, false);
  }
  return next;
}

pscu_indicator_step_t pscu_indicator_step(pscu_indicator_t state,
                                          pscu_indicator_event_t event,
                                          uint8_t fault,
                                          uint32_t elapsed_ms) {
  PSCU_ASSERT(fault <= PSCU_INDICATOR_CODE_SUPPLY);

  // Saturating add.
  pscu_indicator_step_t out;
  out.state = state;
  uint32_t sum = state.phase_ms + elapsed_ms;
  // coverage: unreachable: saturating needs one stage to last 49.7 days.
  out.state.phase_ms = (sum < state.phase_ms) ? 0xFFFFFFFFUL : sum;
  pscu_indicator_apply(&out.state, event, fault);
  pscu_indicator_advance(&out.state);
  out.on = pscu_indicator_lit(&out.state);
  return out;
}

uint8_t pscu_indicator_fault(
    bool supply_low, bool seen, uint32_t quiet_ms, uint32_t since_ms, bool framed, bool armed) {
  uint8_t fault = 0U;
  bool install_check =
      (quiet_ms >= PSCU_INDICATOR_NO_SQCK_MS) && (quiet_ms < PSCU_INDICATOR_NO_SQCK_UNTIL_MS);
  if (supply_low) {
    fault = PSCU_INDICATOR_CODE_SUPPLY;
  } else if (!seen && install_check) {
    fault = PSCU_INDICATOR_CODE_NO_SQCK;
  } else if (framed && !armed && (since_ms >= PSCU_INDICATOR_NO_CHECK_MS)) {
    fault = PSCU_INDICATOR_CODE_NO_CHECK;
  } else {
  }
  return fault;
}
