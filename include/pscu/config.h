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

// Video-CD filter for the SCPH-5903. The Makefile's VCD_FILTER knob always
// defines PSCU_VCD_FILTER as 0 or 1 on the command line; a build outside the
// Makefile gets the ordinary filter. The SCPH-5903 is NTSC-J, so this pairs
// with REGION=jp.
#ifndef PSCU_VCD_FILTER
#define PSCU_VCD_FILTER 0
#endif
#define PSCU_VCD_FILTER_ENABLED (PSCU_VCD_FILTER != 0)

// Upper bound on region strings emitted per arming. The console latches the
// region within a few reads, so this only has to be large enough to be
// reliable; its real purpose is a safety cap so a stuck window can never make
// the chip drive the bus forever. Silence during play, not this number, is
// what makes it stealth.
#define PSCU_STEALTH_STRINGS ((uint8_t)16U)

// Frames between two strings. A pressed disc does not carry its region string
// back to back, and neither long-deployed chip sends it so: PsNee V9.0 needs
// five more lead-in hits before the next string (Read: PSNee.ino:53 and :736,
// REQUEST_INJECT_GAP 5), and Mayumi V4 pauses about 72 ms (Read: its binary,
// disassembled for this project). Five frames is 67 ms at the 75 Hz subcode
// rate, between the two.
#define PSCU_STEALTH_GAP_FRAMES ((uint8_t)5U)

#endif
