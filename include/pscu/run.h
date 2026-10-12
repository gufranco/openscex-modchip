// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_RUN_H
#define PSCU_RUN_H

#include <stdint.h>

// SUBQ counter value that opens the region-check window; filters boot noise.
#define PSCU_INJECT_TRIGGER ((uint8_t)10U)

// Frames watched for the program area after injecting before the session is
// recorded as unconfirmed.
#define PSCU_CONFIRM_FRAMES ((uint8_t)64U)

// The firmware entry loop; never returns.
void pscu_run(void);

#endif
