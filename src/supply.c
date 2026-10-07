// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "pscu/supply.h"

#include <stdbool.h>
#include <stdint.h>

// 388 is the last reading at or above 2.9 V (2903 mV), and 188 the first under
// 6 V (5991 mV); both bounds are inclusive.
bool pscu_supply_ok(uint16_t raw) {
  return (raw > PSCU_SUPPLY_RAW_HIGH_LIMIT) && (raw <= PSCU_SUPPLY_RAW_LOW_LIMIT);
}
