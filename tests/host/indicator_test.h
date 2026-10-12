// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_INDICATOR_TEST_H
#define PSCU_INDICATOR_TEST_H

#include "calib_test.h"

// The status indicator and the supply guard, reporting through the main suite's check.
void indicator_tests(pscu_check_fn check);

#endif
