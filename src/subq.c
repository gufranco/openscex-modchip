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
// is the cue to arm injection. The SUBQ (Q-channel) byte layout is CTRL/ADR,
// TNO, then in the lead-in POINT and the running MIN, SEC, FRAME, a ZERO byte,
// and the pointed-to time. So frame[0] is the control byte, frame[1] the track
// number TNO (always 0x00 in the lead-in), frame[2] the POINT, and frame[3] the
// running minute in BCD.
static bool pscu_subq_lead_in_hit(const uint8_t *frame) {
  PSCU_ASSERT(frame != NULL);

  bool hit = false;

  if (pscu_subq_is_data_sector(frame[0])) {
    // POINT 0xA0 and above are the TOC markers (first track, last track, lead-out
    // start). POINT 0x01, the table entry for track 1, counts only near the end
    // of the lead-in, where the running minute is 98 or 99 or has wrapped to 00
    // through 02. The minute is BCD, so it never exceeds 0x99, and the window is
    // one unsigned compare: (minute - 3) wraps to 0x95..0xFF exactly for
    // 0x98..0xFF and 0x00..0x02. Read: the PsNee V7 lineage (UberNee .ino, four
    // versions) tests minute >= 0x98 || minute <= 0x02; kalymos PsNee V9.0
    // (PSNee.ino:540) writes the bound as 0xF5, which its own comment says covers
    // 0x98..0x02 but in fact only admits 0xF8..0x02 and so misses 98 and 99.
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

// The SCPH-5903 is the Asian model with a second interface that plays Video CDs.
// A Video CD lead-in also carries data-sector TOC frames, so the ordinary rule
// above would arm injection on a movie disc and put a region string on the bus
// for media that never asks for one. This variant accepts only the TOC markers
// POINT 0xA0..0xA2 (first track, last track, lead-out start), drops the point-01
// spiral window, and rejects any marker whose frame[3] is 0x02, the pattern PsNee
// attributes to a Video CD lead-in. Read: kalymos PsNee V9.0 (df52aec,
// PSNee.ino:471-502, its SCPH_5903 FilterSUBQSamples). Why 0x02 in the running
// minute singles out a Video CD is PsNee's empirical rule, Unknown here beyond it.
static bool pscu_subq_vcd_lead_in_hit(const uint8_t *frame) {
  PSCU_ASSERT(frame != NULL);

  bool toc_marker = (frame[2] >= 0xA0U) && (frame[2] <= 0xA2U);
  return pscu_subq_is_data_sector(frame[0]) && toc_marker && (frame[3] != 0x02U);
}

// Once injection is already armed (counter > 0), any further lead-in frame whose
// control byte marks audio (0x01) or data keeps it armed, so the lead-in reads
// between the TOC markers do not decay it while the console is checking. This
// runs only on framed frames, and framing requires TNO 0, so program-area reads
// during play never count here; they decay the counter, which is what re-arms
// injection after a disc swap.
static bool pscu_subq_tracking_hit(const uint8_t *frame, uint8_t counter) {
  PSCU_ASSERT(frame != NULL);

  bool hit = false;

  if (counter > 0U) {
    hit = (frame[0] == 0x01U) || pscu_subq_is_data_sector(frame[0]);
  }

  return hit;
}

// The program area is reached once the mechacon has accepted the region string
// and re-enabled read commands. What distinguishes a program-area Q frame is its
// track number TNO (frame[1]): a BCD value 01 through 99, never the lead-in's
// 0x00 and never the lead-out's 0xAA. The lead-in TOC entries carry POINT 01..99
// in frame[2] but TNO 0x00, so they are rejected here even though they name a
// track; that is exactly the frame the region check reads, and counting it would
// confirm success before the check had passed. The BCD test on the low nibble
// also rejects a misaligned or failed capture (the engine fills a failed frame
// with 0xFF). frame[6] is the Q-channel ZERO byte. Seeing such a frame after
// injection is the documented sign the region check passed, which the run loop
// uses to confirm success. Pure and host-tested.
bool pscu_subq_is_program_area(const uint8_t *frame) {
  PSCU_ASSERT(frame != NULL);

  uint8_t tno = frame[1];
  bool bcd_track = (tno >= 0x01U) && (tno <= 0x99U) && ((uint8_t)(tno & 0x0FU) <= 0x09U);
  bool zero_byte = frame[6] == 0x00U;
  bool content = (frame[0] == 0x01U) || pscu_subq_is_data_sector(frame[0]);
  return bcd_track && zero_byte && content;
}

// Mode 1 carries the position data every other test here reads, and fills at
// least 9 of any 10 consecutive frames on a spinning disc (Concluded: the Red
// Book rule as widely cited, not re-read for this project), so a gap in mode 1
// frames means the drive is not reading.
bool pscu_subq_is_valid(const uint8_t *frame) {
  PSCU_ASSERT(frame != NULL);

  return ((uint8_t)(frame[0] & 0x0FU) == 0x01U) && (frame[6] == 0x00U);
}

uint8_t pscu_subq_update_counter(const uint8_t *frame,
                                 uint8_t counter,
                                 bool vcd_filter,
                                 uint8_t ceiling) {
  PSCU_ASSERT(frame != NULL);

  // frame[1] is TNO, which is 0x00 only in the lead-in, and frame[6] is the
  // Q-channel ZERO byte. Requiring both accepts only well-formed lead-in frames:
  // a program-area frame, noise, or a misaligned capture cannot raise the counter.
  // The filter is a parameter rather than a compile-time switch so the host
  // suite drives both rules from one binary; the firmware passes a constant.
  bool framed = (frame[1] == 0x00U) && (frame[6] == 0x00U);
  bool lead_in = vcd_filter ? pscu_subq_vcd_lead_in_hit(frame) : pscu_subq_lead_in_hit(frame);
  bool hit = framed && (lead_in || pscu_subq_tracking_hit(frame, counter));
  uint8_t result = counter;

  // The counter is a leaky integrator: a hit raises it toward the inject
  // trigger, a miss decays it. This rides out single bad frames without losing
  // sync and re-arms on its own after a disc change re-reads the lead-in.
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
