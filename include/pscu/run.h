// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_RUN_H
#define PSCU_RUN_H

#include <stdint.h>

#define PSCU_INJECT_TRIGGER ((uint8_t)10U)
#define PSCU_INJECT_GAP ((uint8_t)5U)

void pscu_run(void);

#endif
