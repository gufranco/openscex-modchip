// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <stdlib.h>

#include "pscu/assert.h"

void pscu_assert_fail(const char *file, int line) {
  (void)fprintf(stderr, "PSCU_ASSERT failed: %s:%d\n", file, line);
  abort();
}
