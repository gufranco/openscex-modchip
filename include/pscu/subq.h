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
// Pure and host-tested; the firmware feeds it live frames. See subq.c.
uint8_t pscu_subq_update_counter(const uint8_t *frame, uint8_t counter);

// True when the frame shows the console reading the program area, a numbered
// content track at a normal index, which the mechacon only allows once it has
// accepted the region string. The run loop treats seeing this after injection
// as confirmation that the region check passed. Pure and host-tested.
bool pscu_subq_is_program_area(const uint8_t *frame);

#endif
