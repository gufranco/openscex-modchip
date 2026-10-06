// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_SUBQ_H
#define PSCU_SUBQ_H

#include <stdbool.h>
#include <stdint.h>

// A SUBQ frame is 12 bytes as clocked off the disc. The hit counter saturates
// at 0xFF so a long data run cannot wrap it back below the inject trigger.
#define PSCU_SUBQ_FRAME_BYTES ((uint8_t)12U)
#define PSCU_SUBQ_COUNTER_MAX ((uint8_t)0xFFU)

// Fold one captured frame into the running counter and return the new value.
// vcd_filter selects the SCPH-5903 rule, which arms only on the TOC markers and
// never on a Video CD lead-in. Pure and host-tested; the firmware feeds it live
// frames. See subq.c.
uint8_t pscu_subq_update_counter(const uint8_t *frame, uint8_t counter, bool vcd_filter);

// What one capture says about the drive, as the stealth latch needs it. LEAD_IN
// is a well-formed lead-in frame (TNO and ZERO byte 0x00, audio or data content),
// PROGRAM a program-area frame, LOST anything else the drive clocked out (seek
// noise, a stopping disc, a malformed frame), and SILENT a capture that failed
// because SQCK never clocked, which the run loop reports since the frame itself
// carries no sign of it.
typedef enum {
  PSCU_FRAME_LEAD_IN = 0,
  PSCU_FRAME_PROGRAM = 1,
  PSCU_FRAME_LOST = 2,
  PSCU_FRAME_SILENT = 3
} pscu_frame_kind_t;

// Classify one captured frame as LEAD_IN, PROGRAM or LOST. Pure and host-tested.
pscu_frame_kind_t pscu_subq_frame_kind(const uint8_t *frame);

// True when the frame shows the console reading the program area: a content
// frame whose track number TNO is a BCD track 01..99, which the mechacon only
// allows once it has accepted the region string. Lead-in (TNO 0x00) and lead-out
// (TNO 0xAA) frames never qualify. The run loop treats seeing this after
// injection as confirmation that the region check passed. Pure and host-tested.
bool pscu_subq_is_program_area(const uint8_t *frame);

#endif
