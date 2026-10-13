// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_RUN_H
#define PSCU_RUN_H

#include <stdint.h>

// SUBQ counter value that opens the region-check window; filters boot noise.
// PsNee V9.0's REQUEST_INJECT_TRIGGER 10 (PSNee.ino L44), under its limit of 11
// for Japanese consoles (L48).
#define PSCU_INJECT_TRIGGER ((uint8_t)10U)

// Frames watched for the program area after injecting before the session is
// recorded as unconfirmed: 0.85 s at 75 Hz, long past the 5-frame stealth gap so
// a burst still running is never judged, and well inside indicator code 4's
// 20 s. A design bound, not yet tuned on a console.
#define PSCU_CONFIRM_FRAMES ((uint8_t)64U)

// The firmware entry loop; never returns.
void pscu_run(void);

#endif
