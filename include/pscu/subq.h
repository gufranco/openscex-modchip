// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_SUBQ_H
#define PSCU_SUBQ_H

#include <stdint.h>

#define PSCU_SUBQ_FRAME_BYTES ((uint8_t)12U)
#define PSCU_SUBQ_COUNTER_MAX ((uint8_t)0xFFU)

uint8_t pscu_subq_update_counter(const uint8_t *frame, uint8_t counter);

#endif
