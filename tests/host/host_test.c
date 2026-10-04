// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pscu/board_mode.h"
#include "pscu/inject.h"
#include "pscu/mode.h"
#include "pscu/region.h"
#include "pscu/subq.h"

static int g_checks = 0;
static int g_failures = 0;

static void check(bool cond, const char *name) {
  g_checks++;
  if (!cond) {
    g_failures++;
    (void)printf("FAIL: %s\n", name);
  }
}

static void region_to_bits(pscu_region_t region, char *out) {
  for (uint8_t i = 0U; i < PSCU_SCEX_BIT_COUNT; i++) {
    out[i] = pscu_region_bit(region, i) ? '1' : '0';
  }
  out[PSCU_SCEX_BIT_COUNT] = '\0';
}

static void test_region(void) {
  char bits[PSCU_SCEX_BIT_COUNT + 1U];

  region_to_bits(PSCU_REGION_NTSC_J, bits);
  check(strcmp(bits, "10011010100100111101001010111010010110110100") == 0,
        "region SCEI 44-bit LSB-first");

  region_to_bits(PSCU_REGION_NTSC_UC, bits);
  check(strcmp(bits, "10011010100100111101001010111010010111110100") == 0,
        "region SCEA 44-bit LSB-first");

  region_to_bits(PSCU_REGION_PAL, bits);
  check(strcmp(bits, "10011010100100111101001010111010010101110100") == 0,
        "region SCEE 44-bit LSB-first");
}

static void test_subq_counter(void) {
  uint8_t lead_in[PSCU_SUBQ_FRAME_BYTES] = {0x41U, 0, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  check(pscu_subq_update_counter(lead_in, 0U) == 1U, "lead-in A0 increments");

  uint8_t unframed[PSCU_SUBQ_FRAME_BYTES] = {0x41U, 0x01U, 0xA0U, 0, 0, 0,
                                             0,     0,     0,     0, 0, 0};
  check(pscu_subq_update_counter(unframed, 5U) == 4U, "bad framing decays");

  uint8_t marker6[PSCU_SUBQ_FRAME_BYTES] = {0x41U, 0, 0xA0U, 0, 0, 0,
                                            0x01U, 0, 0,     0, 0, 0};
  check(pscu_subq_update_counter(marker6, 5U) == 4U,
        "second sync marker nonzero decays");

  uint8_t audio[PSCU_SUBQ_FRAME_BYTES] = {0x01U, 0, 0x02U, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  check(pscu_subq_update_counter(audio, 3U) == 4U,
        "track 01 keeps counter when synced");
  check(pscu_subq_update_counter(audio, 0U) == 0U, "track 01 does not start sync");

  uint8_t spiral[PSCU_SUBQ_FRAME_BYTES] = {0x41U, 0, 0x01U, 0x00U, 0, 0,
                                           0,     0, 0,     0,     0, 0};
  check(pscu_subq_update_counter(spiral, 0U) == 1U, "track 01 spiral start hits");

  uint8_t spiral_miss[PSCU_SUBQ_FRAME_BYTES] = {0x41U, 0, 0x01U, 0x05U, 0, 0,
                                                0,     0, 0,     0,     0, 0};
  check(pscu_subq_update_counter(spiral_miss, 0U) == 0U,
        "track 01 out of window misses");

  uint8_t mid_point[PSCU_SUBQ_FRAME_BYTES] = {0x41U, 0, 0x50U, 0, 0, 0,
                                              0,     0, 0,     0, 0, 0};
  check(pscu_subq_update_counter(mid_point, 0U) == 0U,
        "data sector mid-point does not start sync");
  check(pscu_subq_update_counter(mid_point, 4U) == 5U,
        "data sector keeps counter when synced");

  uint8_t other[PSCU_SUBQ_FRAME_BYTES] = {0x00U, 0, 0x50U, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  check(pscu_subq_update_counter(other, 3U) == 2U,
        "non-data non-audio sample decays while synced");

  uint8_t empty[PSCU_SUBQ_FRAME_BYTES] = {0};
  check(pscu_subq_update_counter(empty, 0U) == 0U, "decay floors at zero");

  check(pscu_subq_update_counter(lead_in, 0xFFU) == 0xFFU, "counter clamps at max");
}

static pscu_board_detect_t feed(const uint8_t *samples, uint16_t count) {
  pscu_board_detect_t state = pscu_board_detect_init();
  for (uint16_t i = 0U; i < count; i++) {
    state = pscu_board_detect_step(state, samples[i]);
  }
  return state;
}

static void test_board_mode(void) {
  uint8_t high[8] = {1U, 1U, 1U, 1U, 1U, 1U, 1U, 1U};
  check(pscu_board_detect_mode(feed(high, 8U), 3U) == PSCU_BOARD_MODE_GATE,
        "static high is legacy gate");

  uint8_t osc[6] = {1U, 0U, 1U, 0U, 1U, 0U};
  check(pscu_board_detect_mode(feed(osc, 6U), 3U) == PSCU_BOARD_MODE_WFCK,
        "oscillating is wfck mode");

  uint8_t low_run[3] = {0U, 0U, 0U};
  check(pscu_board_detect_mode(feed(low_run, 3U), 2U) == PSCU_BOARD_MODE_GATE,
        "one low pulse is not enough");

  check(pscu_board_detect_mode(feed(high, 8U), 0U) == PSCU_BOARD_MODE_WFCK,
        "zero threshold is wfck");
}

static void test_board_saturates(void) {
  pscu_board_detect_t state = pscu_board_detect_init();
  for (uint16_t i = 0U; i < 600U; i++) {
    state = pscu_board_detect_step(state, (uint8_t)(i & 1U));
  }

  check(state.pulses == 0xFFU, "pulse count saturates at max");
  check(pscu_board_detect_mode(state, 25U) == PSCU_BOARD_MODE_WFCK,
        "saturated count is wfck mode");
}

static uint8_t gesture_fires(const uint8_t *samples, uint16_t count,
                             uint16_t threshold) {
  pscu_gesture_t state = pscu_gesture_init();
  uint8_t fired = 0U;
  for (uint16_t i = 0U; i < count; i++) {
    state = pscu_gesture_step(state, samples[i], threshold);
    if (state.fired != 0U) {
      fired = 1U;
    }
  }
  return fired;
}

static void test_mode(void) {
  check(pscu_mode_next(PSCU_MODE_DEFAULT) == PSCU_MODE_ALT_TIMING,
        "default cycles to alt timing");
  check(pscu_mode_next(PSCU_MODE_ALT_TIMING) == PSCU_MODE_OLD_MODCHIP,
        "alt timing cycles to old modchip");
  check(pscu_mode_next(PSCU_MODE_OLD_MODCHIP) == PSCU_MODE_DISABLED,
        "old modchip cycles to disabled");
  check(pscu_mode_next(PSCU_MODE_DISABLED) == PSCU_MODE_DEFAULT,
        "disabled cycles back to default");

  uint8_t hold_then_release[4] = {1U, 1U, 1U, 0U};
  check(gesture_fires(hold_then_release, 4U, 3U) == 1U,
        "hold to threshold then release fires");

  uint8_t short_hold[3] = {1U, 1U, 0U};
  check(gesture_fires(short_hold, 3U, 3U) == 0U, "short hold does not fire");

  uint8_t lid_cycle[2] = {1U, 0U};
  check(gesture_fires(lid_cycle, 2U, 1U) == 1U, "lid open then close fires");

  uint8_t held_only[4] = {1U, 1U, 1U, 1U};
  check(gesture_fires(held_only, 4U, 2U) == 0U, "hold without release does not fire");

  uint8_t idle[3] = {0U, 0U, 0U};
  check(gesture_fires(idle, 3U, 1U) == 0U, "no activity does not fire");
}

static void test_mode_saturate(void) {
  pscu_gesture_t state = pscu_gesture_init();
  for (uint32_t i = 0U; i < 70000U; i++) {
    state = pscu_gesture_step(state, 1U, 5U);
  }
  check(state.held == 0xFFFFU, "held count saturates");

  state = pscu_gesture_step(state, 0U, 5U);
  check(state.fired == 1U, "release after saturated hold fires");
}

static void test_inject(void) {
  check(pscu_should_inject(10U, 10U), "inject at trigger");
  check(!pscu_should_inject(9U, 10U), "no inject below trigger");
  check(pscu_should_inject(11U, 10U), "inject above trigger");
  check(pscu_counter_after_inject(10U, 5U) == 5U, "cool-off is trigger minus gap");
  check(pscu_counter_after_inject(10U, 8U) == 2U, "cool-off with larger gap");
}

int main(void) {
  test_region();
  test_subq_counter();
  test_board_mode();
  test_board_saturates();
  test_mode();
  test_mode_saturate();
  test_inject();

  (void)printf("%d checks, %d failures\n", g_checks, g_failures);
  return (g_failures == 0) ? 0 : 1;
}
