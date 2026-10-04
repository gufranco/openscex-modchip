// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_RUN_H
#define PSCU_RUN_H

#include <stdint.h>

// The region-check window is considered open once the leaky SUBQ counter has
// built up to this many hits, which filters boot noise before the chip emits.
#define PSCU_INJECT_TRIGGER ((uint8_t)10U)

// The firmware entry loop; never returns.
void pscu_run(void);

#endif
