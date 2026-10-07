// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_TRIM_TEST_H
#define PSCU_TRIM_TEST_H

#include "calib_test.h"

// The oscillator trim suite, reporting through the main suite's check function.
void trim_tests(pscu_check_fn check);

#endif
