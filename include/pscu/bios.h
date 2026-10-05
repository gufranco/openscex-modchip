// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_BIOS_H
#define PSCU_BIOS_H

// Run the boot-ROM BIOS patch once at power-on, before the SCEx loop starts.
// On builds without the patch (the ATtiny85, or an ATtiny84 built without a
// BIOS model) this is a no-op, so main.c calls it unconditionally and the
// Makefile decides whether the real implementation or the stub is linked.
void pscu_bios_patch(void);

#endif
