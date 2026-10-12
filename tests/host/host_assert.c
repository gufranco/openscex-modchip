// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <stdlib.h>

#include "pscu/assert.h"

// Host backing for PSCU_ASSERT: print where it failed and abort. The firmware
// never links this; there PSCU_ASSERT compiles to nothing.
void pscu_assert_fail(const char *file, int line) {
  (void)fprintf(stderr, "PSCU_ASSERT failed: %s:%d\n", file, line);
  abort();
}
