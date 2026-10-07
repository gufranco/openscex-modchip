// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_LED_TEST_H
#define PSCU_LED_TEST_H

#include "calib_test.h"

// The status LED and the supply guard, reporting through the main suite's check.
void led_tests(pscu_check_fn check);

#endif
