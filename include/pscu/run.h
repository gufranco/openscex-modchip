// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_RUN_H
#define PSCU_RUN_H

#include <stdint.h>

// Inject once the counter reaches 10 consecutive-ish SUBQ hits, then drop it by
// 5 as cool-off so re-injection needs 5 fresh hits. The pair spaces repeated
// injections without fully disarming across a brief SUBQ dropout.
#define PSCU_INJECT_TRIGGER ((uint8_t)10U)
#define PSCU_INJECT_GAP ((uint8_t)5U)

// The firmware entry loop; never returns.
void pscu_run(void);

#endif
