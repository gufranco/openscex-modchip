// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "disc_test.h"

#include <stdbool.h>
#include <stdint.h>

#include "pscu/inject.h"
#include "pscu/subq.h"

// Host tests for disc presence without a lid wire: gone only after a real
// stretch of silence, and a valid frame ends that stretch at once.

// Rule 5's disc-gone bound, pinned so a change to it fails the build instead of
// passing every case that uses the symbol: 1.5 s, longer than any seek or
// reread pause and shorter than a disc swap.
_Static_assert(PSCU_DISC_GONE_MS == 1500U, "a disc is gone after 1.5 s without a frame");

static pscu_check_fn g_check;

static void test_valid_frames(void) {
  uint8_t lead_in[PSCU_SUBQ_FRAME_BYTES] = {
    0x41U, 0x00U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0x64U, 0x49U
  };
  uint8_t audio[PSCU_SUBQ_FRAME_BYTES] = { 0x01U, 0x01U, 0x01U, 0, 0, 0, 0, 0, 0, 0, 0x3CU, 0x4AU };
  uint8_t idle[PSCU_SUBQ_FRAME_BYTES] = { 0 };
  uint8_t failed[PSCU_SUBQ_FRAME_BYTES] = { 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU,
                                            0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU };
  uint8_t mode2[PSCU_SUBQ_FRAME_BYTES] = { 0x42U, 0x00U, 0x00U, 0, 0, 0, 0, 0, 0, 0, 0x53U, 0x6FU };
  uint8_t nonzero[PSCU_SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0, 0,     0,
                                             0x01U, 0,     0,     0, 0x12U, 0xFDU };

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

// A CXD2938Q (PU-23, PM-41) also clocks "New SCEx" packets out on the SUBQ
// line: three header bytes, one SCEx character ORed into bytes 3 to 9, two
// level bytes (psx-spx, CXD2938Q New SCEx). None may pass as a disc frame,
// count toward the lead-in, or look like the program area that closes the
// window. The header and level bytes are undocumented, so the worst case
// copies a lead-in header and leaves the ZERO byte clear: then only the CRC
// stands between it and a lead-in frame, and the level bytes in its place
// fail it.
static void test_scex_packets(void) {
  uint8_t letters[4] = { 0x53U, 0x43U, 0x45U, 0x41U };
  uint8_t headers[3][3] = { { 0x00U, 0x00U, 0x00U },
                            { 0xFFU, 0xFFU, 0xFFU },
                            { 0x41U, 0x00U, 0xA0U } };
  bool any_valid = false;
  bool any_program = false;
  bool any_hit = false;

  for (uint8_t h = 0U; h < 3U; h++) {
    for (uint8_t c = 0U; c < 4U; c++) {
      uint8_t packet[PSCU_SUBQ_FRAME_BYTES] = { 0 };
      packet[0] = headers[h][0];
      packet[1] = headers[h][1];
      packet[2] = headers[h][2];
      for (uint8_t at = 3U; at <= 9U; at++) {
        packet[at] = letters[c];
      }
      if (h == 2U) {
        packet[6] = 0x00U;
      }
      packet[10] = 0x7FU;
      packet[11] = 0x7FU;
      any_valid = any_valid || pscu_subq_is_valid(packet);
      any_program = any_program || pscu_subq_is_program_area(packet);
      any_hit = any_hit || (pscu_subq_update_counter(packet, 5U, false, 0xFFU) != 4U);
    }
  }

  g_check(!any_valid, "disc: a New SCEx packet is not a disc frame");
  g_check(!any_program, "disc: a New SCEx packet is not the program area");
  g_check(!any_hit, "disc: a New SCEx packet counts as a lead-in miss");
}

void disc_tests(pscu_check_fn check) {
  g_check = check;
  test_valid_frames();
  test_presence();
  test_scex_packets();
}
