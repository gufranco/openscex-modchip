// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_ASSERT_H
#define PSCU_ASSERT_H

// Contract checks for the Power-of-Ten assertion discipline. Under the host
// test build (PSCU_DEBUG) a failed assert aborts so a test catches it; in the
// release firmware it compiles to nothing, so assertions cost zero flash and
// never run on the console. Keep every assert expression side-effect free.
#ifdef PSCU_DEBUG
void pscu_assert_fail(const char *file, int line);
#define PSCU_ASSERT(expr) ((expr) ? (void)0 : pscu_assert_fail(__FILE__, __LINE__))
#else
#define PSCU_ASSERT(expr) ((void)0)
#endif

#endif
