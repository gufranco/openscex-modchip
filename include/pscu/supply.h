// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_SUPPLY_H
#define PSCU_SUPPLY_H

#include <stdbool.h>
#include <stdint.h>

// Supply guard: no string is sent below 2.75 V, since 8 MHz needs 2.7 V
// (datasheet 2586Q, speed grade). raw = 1024 x 1.1 V / VCC, so lower supply
// reads higher; the bandgap varies by about 0.1 V per part. A reading at 6 V
// or more is a failed measurement and also blocks injection.
#define PSCU_SUPPLY_BANDGAP_MV ((uint32_t)1100U)
#define PSCU_SUPPLY_LOW_MV ((uint32_t)2750U)
#define PSCU_SUPPLY_HIGH_MV ((uint32_t)6000U)
#define PSCU_SUPPLY_SCALE ((uint32_t)1024U)
#define PSCU_SUPPLY_RAW_LOW_LIMIT \
  ((uint16_t)((PSCU_SUPPLY_SCALE * PSCU_SUPPLY_BANDGAP_MV) / PSCU_SUPPLY_LOW_MV))
#define PSCU_SUPPLY_RAW_HIGH_LIMIT \
  ((uint16_t)((PSCU_SUPPLY_SCALE * PSCU_SUPPLY_BANDGAP_MV) / PSCU_SUPPLY_HIGH_MV))

// True for a supply from 2.75 V up to under 6 V.
bool pscu_supply_ok(uint16_t raw);

#endif
