// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_LOOP_TEST_H
#define PSCU_LOOP_TEST_H

#include "calib_test.h"

// The run loop's per-pass decisions, reporting through the main suite's check.
void loop_tests(pscu_check_fn check);

#endif
