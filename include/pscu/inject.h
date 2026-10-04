// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_INJECT_H
#define PSCU_INJECT_H

#include <stdbool.h>
#include <stdint.h>

bool pscu_should_inject(uint8_t counter, uint8_t trigger);

uint8_t pscu_counter_after_inject(uint8_t trigger, uint8_t gap);

#endif
