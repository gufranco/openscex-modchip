// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

// Compiled once per REGION by `make hosttest`, never linked: the build's
// region define must select that region's string, since the logic layer is
// region-agnostic and only engine.c reads the choice. The Makefile passes the
// region it expects as PSCU_EXPECTED_REGION.

#include "pscu/config.h"

_Static_assert(PSCU_CONFIGURED_REGION == PSCU_EXPECTED_REGION,
               "the REGION define selects that region's string");
