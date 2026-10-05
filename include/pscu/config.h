// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_CONFIG_H
#define PSCU_CONFIG_H

// port.S includes this through the C preprocessor to pick up the cycle-count
// constants below, so the C-only declarations are hidden from the assembler;
// the plain integer macros it needs stay visible.
#ifndef __ASSEMBLER__
#include <stdint.h>

#include "pscu/region.h"
#endif

// Compile-time region. To defeat the check the chip emits the console's own
// region string, so the build is region-specific: one of REGION=jp|us|eu
// defines PSCU_REGION_JP/US/EU on the command line. Emitting only the one
// configured region (never all three) is part of being stealth: the chip puts
// exactly what that console expects on the bus and nothing else. Default is
// America when nothing is defined.
#if defined(PSCU_REGION_JP)
#define PSCU_CONFIGURED_REGION PSCU_REGION_NTSC_J
#elif defined(PSCU_REGION_EU)
#define PSCU_CONFIGURED_REGION PSCU_REGION_PAL
#else
#define PSCU_CONFIGURED_REGION PSCU_REGION_NTSC_UC
#endif

// Upper bound on region strings emitted per arming. The console latches the
// region within a few reads, so this only has to be large enough to be
// reliable; its real purpose is a safety cap so a stuck window can never make
// the chip drive the bus forever. Silence during play, not this number, is
// what makes it stealth.
#define PSCU_STEALTH_STRINGS ((uint8_t)16U)

// The boot-ROM BIOS patch constants (PSCU_BIOS_ENABLED and, when enabled, the
// per-model PSCU_BIOS_SILENCE/CONFIRMS/PULSES/OFFSET_CYCLES/OVERRIDE_CYCLES)
// are supplied on the command line by the Makefile's BIOS knob, not here: only
// the selected model's values then exist, and assembly as well as C can read
// them. See the BIOS block in the Makefile and bios.c.

#endif
