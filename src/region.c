// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "pscu/region.h"

#include "pscu/assert.h"

// The SCEx magic word is 44 bits, which is not a whole number of bytes, so
// each region's word is held in 6 bytes (48 bits). Only the low 44 are ever
// injected; the 4 high bits of the last byte are padding and never read.
#define PSCU_SCEX_BYTE_COUNT ((uint8_t)6U)

uint8_t pscu_region_bit(pscu_region_t region, uint8_t index) {
  // Row order matches pscu_region_t: Japan, America, Europe. The three words
  // are identical except for byte 4 (0xDA / 0xFA / 0xEA); bytes 0-3 and 5 are
  // the shared "SCE" framing. Bytes are stored LSB-first, the order the drive
  // clocks them in, so no reversal is needed at injection time.
  static const uint8_t table[PSCU_REGION_COUNT][PSCU_SCEX_BYTE_COUNT] = {
      {0x59U, 0xC9U, 0x4BU, 0x5DU, 0xDAU, 0x02U},
      {0x59U, 0xC9U, 0x4BU, 0x5DU, 0xFAU, 0x02U},
      {0x59U, 0xC9U, 0x4BU, 0x5DU, 0xEAU, 0x02U}};

  PSCU_ASSERT((uint8_t)region < PSCU_REGION_COUNT);
  PSCU_ASSERT(index < PSCU_SCEX_BIT_COUNT);

  // index >> 3 picks the byte, index & 7 picks the bit inside it, counting from
  // the least significant bit. This is the single DATA-line level for pulse
  // number `index` of the 44-bit sequence.
  uint8_t byte = table[region][index >> 3U];
  uint8_t shifted = (uint8_t)(byte >> (index & 0x07U));
  uint8_t result = (uint8_t)(shifted & 0x01U);

  PSCU_ASSERT(result <= 1U);

  return result;
}
