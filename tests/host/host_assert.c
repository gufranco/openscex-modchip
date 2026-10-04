// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <stdlib.h>

#include "pscu/assert.h"

// Host-build backing for PSCU_ASSERT: print where it failed and abort so the
// test run exits nonzero and a broken contract cannot pass silently. The
// release firmware never links this; there PSCU_ASSERT compiles to nothing.
void pscu_assert_fail(const char *file, int line) {
  (void)fprintf(stderr, "PSCU_ASSERT failed: %s:%d\n", file, line);
  abort();
}
