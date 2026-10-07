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

// True when the frame shows the console reading the program area: a content
// frame whose track number TNO is a BCD track 01..99, which the mechacon only
// allows once it has accepted the region string. Lead-in (TNO 0x00) and lead-out
// (TNO 0xAA) frames never qualify. The run loop treats seeing this after
// injection as confirmation that the region check passed. Pure and host-tested.
bool pscu_subq_is_program_area(const uint8_t *frame);

// True for a frame a spinning disc produces: Q mode 1 (ADR 1 in the low nibble
// of the control byte) with the ZERO byte at 0x00. Lead-in, program area and
// lead-out all qualify; a failed capture (all 0xFF), an idle bus (all 0x00)
// and the occasional mode 2 or 3 frame do not. A run of frames without one is
// how the chip knows the drive has stopped. Pure and host-tested.
bool pscu_subq_is_valid(const uint8_t *frame);

#endif
