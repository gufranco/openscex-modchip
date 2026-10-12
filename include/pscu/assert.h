// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_ASSERT_H
#define PSCU_ASSERT_H

// Contract checks. The host test build (PSCU_DEBUG) aborts on failure; the
// firmware compiles them out. Keep every expression side-effect free.
#ifdef PSCU_DEBUG
void pscu_assert_fail(const char *file, int line);
#define PSCU_ASSERT(expr) ((expr) ? (void)0 : pscu_assert_fail(__FILE__, __LINE__))
#else
#define PSCU_ASSERT(expr) ((void)0)
#endif

#endif
