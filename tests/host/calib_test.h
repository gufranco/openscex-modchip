// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_CALIB_TEST_H
#define PSCU_CALIB_TEST_H

#include <stdbool.h>

// The calibration suite lives in its own file to keep each test file readable;
// it reports through the main suite's check function so one run counts every
// check and fails on any of them.
typedef void (*pscu_check_fn)(bool cond, const char *name);

void calib_tests(pscu_check_fn check);

#endif
