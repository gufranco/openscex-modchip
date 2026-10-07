// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "disc_test.h"

#include <stdbool.h>
#include <stdint.h>

#include "pscu/inject.h"
#include "pscu/subq.h"

// Host tests for knowing a disc is there without a lid wire. A swap must read as
// gone only after a real stretch of silence, and a valid frame must end that
// stretch at once, or the chip either stays latched silent for the next disc or
// re-arms during an anti-mod reread of the same one.

static pscu_check_fn g_check;

static void test_valid_frames(void) {
  uint8_t lead_in[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  uint8_t audio[PSCU_SUBQ_FRAME_BYTES] = { 0x01U, 0x01U, 0x01U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  uint8_t idle[PSCU_SUBQ_FRAME_BYTES] = { 0 };
  uint8_t failed[PSCU_SUBQ_FRAME_BYTES] = { 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU,
                                            0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU };
  uint8_t mode2[PSCU_SUBQ_FRAME_BYTES] = { 0x42U, 0x00U, 0x00U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  uint8_t nonzero[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0, 0, 0, 0x01U, 0, 0, 0, 0, 0 };

  g_check(pscu_subq_is_valid(lead_in), "disc: a mode 1 lead-in frame is valid");
  g_check(pscu_subq_is_valid(audio), "disc: a mode 1 audio frame is valid");
  g_check(!pscu_subq_is_valid(idle), "disc: an idle bus is not a frame");
  g_check(!pscu_subq_is_valid(failed), "disc: a failed capture is not a frame");
  g_check(!pscu_subq_is_valid(mode2), "disc: a mode 2 frame does not count");
  g_check(!pscu_subq_is_valid(nonzero), "disc: a frame with a nonzero ZERO byte does not count");
}

static void test_presence(void) {
  pscu_presence_t start = pscu_presence_init();
  g_check(!pscu_presence_gone(start) && !start.seen, "disc: power-on is neither gone nor seen");

  pscu_presence_t almost = pscu_presence_step(start, false, PSCU_DISC_GONE_MS - 1U);
  g_check(!pscu_presence_gone(almost), "disc: silence just short of the bound is not a swap");
  pscu_presence_t gone = pscu_presence_step(almost, false, 1U);
  g_check(pscu_presence_gone(gone), "disc: silence reaching the bound is a swap");

  pscu_presence_t back = pscu_presence_step(gone, true, 30U);
  g_check(!pscu_presence_gone(back) && (back.quiet_ms == 0U) && back.seen,
          "disc: one valid frame ends the silence at once");
  pscu_presence_t still = pscu_presence_step(back, false, 0U);
  g_check(still.quiet_ms == 0U, "disc: a pass that took no time adds no silence");
  g_check(pscu_presence_step(back, false, 25U).seen, "disc: seen stays once a frame arrived");

  pscu_presence_t full = { 0xFFFFFFF0UL, true };
  g_check(pscu_presence_step(full, false, 0x100U).quiet_ms == 0xFFFFFFFFUL,
          "disc: the silence count saturates");
}

void disc_tests(pscu_check_fn check) {
  g_check = check;
  test_valid_frames();
  test_presence();
}
