// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_SUPPLY_H
#define PSCU_SUPPLY_H

#include <stdbool.h>
#include <stdint.h>

// Software supply guard. The standard ATtiny85 is rated for 8 MHz only from
// 2.7 V (Read: ATtiny25/45/85 datasheet 2586Q, speed grade). Brown-out detection
// at 2.7 V is a fuse an installer can forget, so the firmware also measures its
// own supply before every string and sends nothing while it is low: a chip run
// below its rating could put a garbled string or a stuck level on the bus.
//
// The reading is the bandgap measured against VCC, raw = 1024 x 1.1 V / VCC, so
// a lower supply gives a higher reading. The bandgap is nominally 1.1 V with a
// device spread of about 0.1 V either way (Concluded: the datasheet's
// characteristics table, as extracted, gives 1.0 V at its low end), so the limit
// sits at 2.9 V nominal, which keeps the true cut-off at 2.64 V or above even on
// a low-bandgap part, and at 3.16 V at most on a high one, below the 3.3 V and
// 5 V rails a console offers (Unknown until the tap voltages are measured).
// A reading equal to 6 V or more is impossible for a chip rated to 5.5 V and
// means the measurement failed, so it also blocks injection: the guard fails
// closed.
#define PSCU_SUPPLY_BANDGAP_MV ((uint32_t)1100U)
#define PSCU_SUPPLY_LOW_MV ((uint32_t)2900U)
#define PSCU_SUPPLY_HIGH_MV ((uint32_t)6000U)
#define PSCU_SUPPLY_SCALE ((uint32_t)1024U)
#define PSCU_SUPPLY_RAW_LOW_LIMIT \
  ((uint16_t)((PSCU_SUPPLY_SCALE * PSCU_SUPPLY_BANDGAP_MV) / PSCU_SUPPLY_LOW_MV))
#define PSCU_SUPPLY_RAW_HIGH_LIMIT \
  ((uint16_t)((PSCU_SUPPLY_SCALE * PSCU_SUPPLY_BANDGAP_MV) / PSCU_SUPPLY_HIGH_MV))

// True when the reading shows a supply from 2.9 V up to under 6 V: readings
// from PSCU_SUPPLY_RAW_HIGH_LIMIT + 1 to PSCU_SUPPLY_RAW_LOW_LIMIT.
bool pscu_supply_ok(uint16_t raw);

#endif
