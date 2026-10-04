// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "pscu/region.h"

#include "pscu/assert.h"

#define PSCU_SCEX_BYTE_COUNT ((uint8_t)6U)

static const uint8_t pscu_scex_table[PSCU_REGION_COUNT][PSCU_SCEX_BYTE_COUNT] = {
    {0x59U, 0xC9U, 0x4BU, 0x5DU, 0xDAU, 0x02U},
    {0x59U, 0xC9U, 0x4BU, 0x5DU, 0xFAU, 0x02U},
    {0x59U, 0xC9U, 0x4BU, 0x5DU, 0xEAU, 0x02U}};

uint8_t pscu_region_bit(pscu_region_t region, uint8_t index) {
  PSCU_ASSERT((uint8_t)region < PSCU_REGION_COUNT);
  PSCU_ASSERT(index < PSCU_SCEX_BIT_COUNT);

  uint8_t byte = pscu_scex_table[region][index >> 3U];
  uint8_t shifted = (uint8_t)(byte >> (index & 0x07U));

  return (uint8_t)(shifted & 0x01U);
}
