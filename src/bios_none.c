// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "pscu/bios.h"

// No BIOS patch in this build: the ATtiny85 has no spare pins for it and most
// consoles do not need it. The no-op keeps main.c identical across builds, so
// whether the patch runs is decided entirely by which file the Makefile links.
void pscu_bios_patch(void) {
}
