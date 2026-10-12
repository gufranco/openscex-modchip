// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "pscu/supply.h"

#include <stdbool.h>
#include <stdint.h>

// Accepts 188 (5991 mV) to 409 (2754 mV), inclusive.
bool pscu_supply_ok(uint16_t raw) {
  return (raw > PSCU_SUPPLY_RAW_HIGH_LIMIT) && (raw <= PSCU_SUPPLY_RAW_LOW_LIMIT);
}
