// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_REGION_H
#define PSCU_REGION_H

#include <stdint.h>

// The SCEx word is 44 bits; there are three regional words.
#define PSCU_SCEX_BIT_COUNT ((uint8_t)44U)
#define PSCU_REGION_COUNT ((uint8_t)3U)

// Region order is fixed and doubles as the table row index in region.c:
// Japan (SCEI), America (SCEA), Europe (SCEE). The enum values are deliberate,
// not incidental, so do not reorder them.
typedef enum { PSCU_REGION_NTSC_J = 0, PSCU_REGION_NTSC_UC = 1, PSCU_REGION_PAL = 2 } pscu_region_t;

// Return bit `index` (0..43) of `region`'s SCEx word, as 0 or 1.
uint8_t pscu_region_bit(pscu_region_t region, uint8_t index);

#endif
