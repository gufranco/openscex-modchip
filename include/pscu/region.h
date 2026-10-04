// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_REGION_H
#define PSCU_REGION_H

#include <stdint.h>

#define PSCU_SCEX_BIT_COUNT ((uint8_t)44U)
#define PSCU_REGION_COUNT ((uint8_t)3U)

typedef enum {
  PSCU_REGION_NTSC_J = 0,
  PSCU_REGION_NTSC_UC = 1,
  PSCU_REGION_PAL = 2
} pscu_region_t;

uint8_t pscu_region_bit(pscu_region_t region, uint8_t index);

#endif
