// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "pscu/subq.h"

#include <stdbool.h>
#include <stddef.h>

#include "pscu/assert.h"

// A SUBQ control byte marks a data sector when its ADR/control nibble pattern
// is 0x4x with the data bit set. Masking 0xD0 and comparing to 0x40 isolates
// that pattern while ignoring the copy and pre-emphasis flags.
static bool pscu_subq_is_data_sector(uint8_t control) {
  return (uint8_t)(control & 0xD0U) == 0x40U;
}

// A lead-in frame is where the console performs its region check, so seeing one
// is the cue to arm injection. frame[0] is the control byte, frame[2] the track
// number, frame[3] the index within the track.
static bool pscu_subq_lead_in_hit(const uint8_t *frame) {
  PSCU_ASSERT(frame != NULL);

  bool hit = false;

  if (pscu_subq_is_data_sector(frame[0])) {
    // Track numbers 0xA0 and above are the TOC/lead-in markers. Track 01 is the
    // program-area start, which counts only in its lead-in window: frame[3]
    // between 0xF8 and 0xFF, tested as (frame[3] - 3) wrapping to >= 0xF5.
    if (frame[2] >= 0xA0U) {
      hit = true;
    } else if (frame[2] == 0x01U) {
      hit = (uint8_t)(frame[3] - 0x03U) >= 0xF5U;
    } else {
      hit = false;
    }
  }

  return hit;
}

// Once injection is already armed (counter > 0), ordinary program-area reads of
// track 01 or any data sector keep it armed, so the console keeps seeing the
// region string as it spins up rather than losing it after one lead-in.
static bool pscu_subq_tracking_hit(const uint8_t *frame, uint8_t counter) {
  PSCU_ASSERT(frame != NULL);

  bool hit = false;

  if (counter > 0U) {
    hit = (frame[0] == 0x01U) || pscu_subq_is_data_sector(frame[0]);
  }

  return hit;
}

uint8_t pscu_subq_update_counter(const uint8_t *frame, uint8_t counter) {
  PSCU_ASSERT(frame != NULL);

  // frame[1] and frame[6] are zero only on a well-formed SUBQ frame; a nonzero
  // value means noise or a misaligned capture, which must not move the counter.
  bool framed = (frame[1] == 0x00U) && (frame[6] == 0x00U);
  bool hit = framed && (pscu_subq_lead_in_hit(frame) || pscu_subq_tracking_hit(frame, counter));
  uint8_t result = counter;

  // The counter is a leaky integrator: a hit raises it toward the inject
  // trigger, a miss decays it. This rides out single bad frames without losing
  // sync and re-arms on its own after a disc change re-reads the lead-in.
  if (hit && (counter < PSCU_SUBQ_COUNTER_MAX)) {
    result = (uint8_t)(counter + 1U);
  }
  if ((!hit) && (counter > 0U)) {
    result = (uint8_t)(counter - 1U);
  }

  // Each call moves the counter by at most one step in either direction.
  PSCU_ASSERT((uint8_t)((result - counter) + 1U) <= 2U);

  return result;
}
