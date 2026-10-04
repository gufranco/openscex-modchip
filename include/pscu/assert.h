// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_ASSERT_H
#define PSCU_ASSERT_H

#ifdef PSCU_DEBUG
void pscu_assert_fail(const char *file, int line, const char *expr);
#define PSCU_ASSERT(expr) \
  ((expr) ? (void)0 : pscu_assert_fail(__FILE__, __LINE__, #expr))
#else
#define PSCU_ASSERT(expr) ((void)0)
#endif

#endif
