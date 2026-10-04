// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_SUBQ_H
#define PSCU_SUBQ_H

#include <stdint.h>

// A SUBQ frame is 12 bytes as clocked off the disc. The hit counter saturates
// at 0xFF so a long data run cannot wrap it back below the inject trigger.
#define PSCU_SUBQ_FRAME_BYTES ((uint8_t)12U)
#define PSCU_SUBQ_COUNTER_MAX ((uint8_t)0xFFU)

// Fold one captured frame into the running counter and return the new value.
// Pure and host-tested; the firmware feeds it live frames. See subq.c.
uint8_t pscu_subq_update_counter(const uint8_t *frame, uint8_t counter);

#endif
