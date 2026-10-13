// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_TRACE_TEST_H
#define PSCU_TRACE_TEST_H

#include "calib_test.h"

// The serial trace suite, reporting through the main suite's check function.
void trace_tests(pscu_check_fn check);

#endif
