// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "pscu/subq.h"

#include <stdbool.h>
#include <stddef.h>

#include "pscu/assert.h"

// CRC-16 CCITT over the ten data bytes, bitwise to save flash. The disc stores
// it inverted.
static uint16_t pscu_subq_crc(const uint8_t *frame) {
  uint16_t crc = 0U;

  for (uint8_t i = 0U; i < PSCU_SUBQ_DATA_BYTES; i++) {
    crc = (uint16_t)(crc ^ (uint16_t)((uint16_t)frame[i] << 8U));
    for (uint8_t bit = 0U; bit < 8U; bit++) {
      bool top = (uint16_t)(crc & 0x8000U) != 0U;
      crc = (uint16_t)(crc << 1U);
      if (top) {
        crc = (uint16_t)(crc ^ 0x1021U);
      }
    }
  }

  return (uint16_t)(crc ^ 0xFFFFU);
}

// The last two bytes hold the CRC of the first ten, inverted, high byte first
// (ECMA-130 section 22.3.6).
static bool pscu_subq_crc_ok(const uint8_t *frame) {
  PSCU_ASSERT(frame != NULL);

  uint16_t crc = pscu_subq_crc(frame);
  return (frame[10] == (uint8_t)(crc >> 8U)) && (frame[11] == (uint8_t)(crc & 0xFFU));
}

// Data track, ignoring the copy and pre-emphasis flags.
static bool pscu_subq_is_data_sector(uint8_t control) {
  return (uint8_t)(control & 0xD0U) == 0x40U;
}

// A lead-in TOC frame, where the console runs its region check. frame[0] is
// CTRL/ADR, frame[1] TNO, frame[2] POINT, frame[3] the running minute in BCD.
static bool pscu_subq_lead_in_hit(const uint8_t *frame) {
  PSCU_ASSERT(frame != NULL);

  bool hit = false;

  if (pscu_subq_is_data_sector(frame[0])) {
    // POINT 0xA0 and up are TOC markers. POINT 0x01 counts only at minute 98
    // to 02, one wrapping compare (UberNee: minute >= 0x98 || minute <= 0x02).
    if (frame[2] >= 0xA0U) {
      hit = true;
    } else if (frame[2] == 0x01U) {
      hit = (uint8_t)(frame[3] - 0x03U) >= 0x95U;
    } else {
      hit = false;
    }
  }

  return hit;
}

// SCPH-5903 (plays Video CDs): only POINT 0xA0..0xA2, and not with frame[3] =
// 0x02, which marks a Video CD lead-in (PsNee V9.0 PSNee.ino:471-502).
static bool pscu_subq_vcd_lead_in_hit(const uint8_t *frame) {
  PSCU_ASSERT(frame != NULL);

  bool toc_marker = (frame[2] >= 0xA0U) && (frame[2] <= 0xA2U);
  return pscu_subq_is_data_sector(frame[0]) && toc_marker && (frame[3] != 0x02U);
}

// Once armed, any audio or data lead-in frame keeps the counter up between TOC
// markers. Only called on lead-in frames (TNO 0).
static bool pscu_subq_tracking_hit(const uint8_t *frame, uint8_t counter) {
  PSCU_ASSERT(frame != NULL);

  bool hit = false;

  if (counter > 0U) {
    hit = (frame[0] == 0x01U) || pscu_subq_is_data_sector(frame[0]);
  }

  return hit;
}

// Keyed on TNO, not POINT: lead-in TOC entries name tracks in POINT but have
// TNO 0x00. frame[6] is the ZERO byte.
bool pscu_subq_is_program_area(const uint8_t *frame) {
  PSCU_ASSERT(frame != NULL);

  uint8_t tno = frame[1];
  bool bcd_track = (tno >= 0x01U) && (tno <= 0x99U) && ((uint8_t)(tno & 0x0FU) <= 0x09U);
  bool zero_byte = frame[6] == 0x00U;
  bool content = (frame[0] == 0x01U) || pscu_subq_is_data_sector(frame[0]);
  return bcd_track && zero_byte && content && pscu_subq_crc_ok(frame);
}

// At least 9 of any 10 frames on a spinning disc are mode 1 (Red Book).
bool pscu_subq_is_valid(const uint8_t *frame) {
  PSCU_ASSERT(frame != NULL);

  return ((uint8_t)(frame[0] & 0x0FU) == 0x01U) && (frame[6] == 0x00U) && pscu_subq_crc_ok(frame);
}

uint8_t pscu_subq_update_counter(const uint8_t *frame,
                                 uint8_t counter,
                                 bool vcd_filter,
                                 uint8_t ceiling) {
  PSCU_ASSERT(frame != NULL);

  // Only well-formed lead-in frames count: TNO 0x00, ZERO byte 0x00, good CRC.
  bool framed = (frame[1] == 0x00U) && (frame[6] == 0x00U) && pscu_subq_crc_ok(frame);
  bool lead_in = vcd_filter ? pscu_subq_vcd_lead_in_hit(frame) : pscu_subq_lead_in_hit(frame);
  bool hit = framed && (lead_in || pscu_subq_tracking_hit(frame, counter));
  uint8_t result = counter;

  // Leaky integrator: a hit adds one, a miss takes one off.
  if (hit && (counter < ceiling)) {
    result = (uint8_t)(counter + 1U);
  }
  if ((!hit) && (counter > 0U)) {
    result = (uint8_t)(counter - 1U);
  }

  // Each call moves the counter by at most one step in either direction.
  PSCU_ASSERT((uint8_t)((result - counter) + 1U) <= 2U);

  return result;
}
