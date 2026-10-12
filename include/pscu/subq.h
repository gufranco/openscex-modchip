// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_SUBQ_H
#define PSCU_SUBQ_H

#include <stdbool.h>
#include <stdint.h>

// A SUBQ frame: ten data bytes, then their CRC.
#define PSCU_SUBQ_FRAME_BYTES ((uint8_t)12U)
#define PSCU_SUBQ_DATA_BYTES ((uint8_t)10U)

// Fold one frame into the running counter and return the new value. vcd_filter
// selects the SCPH-5903 rule, which arms only on TOC markers, never on a Video
// CD lead-in. A hit never lifts the counter past ceiling.
uint8_t pscu_subq_update_counter(const uint8_t *frame,
                                 uint8_t counter,
                                 bool vcd_filter,
                                 uint8_t ceiling);

// True for a valid program-area frame: TNO is a BCD track 01..99. The mechacon
// reads it only after accepting the region string, so it confirms the check
// passed. Lead-in (TNO 0x00), lead-out (0xAA) and bad-CRC frames never count.
bool pscu_subq_is_program_area(const uint8_t *frame);

// True for a frame from a spinning disc: Q mode 1 (ADR 1 in the control byte's
// low nibble), ZERO byte 0x00 and a matching CRC. A failed capture (all 0xFF),
// an idle bus (all 0x00) and mode 2 or 3 frames do not count.
bool pscu_subq_is_valid(const uint8_t *frame);

#endif
