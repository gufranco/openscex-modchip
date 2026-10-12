// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_CONFIG_H
#define PSCU_CONFIG_H

// port.S includes this file too, so C-only declarations are hidden from the
// assembler.
#ifndef __ASSEMBLER__
#include <stdint.h>

#include "pscu/region.h"
#endif

// Compile-time region, set by REGION=jp|us|eu. The chip emits only this
// region's string. Defaults to America.
#if defined(PSCU_REGION_JP)
#define PSCU_CONFIGURED_REGION PSCU_REGION_NTSC_J
#elif defined(PSCU_REGION_EU)
#define PSCU_CONFIGURED_REGION PSCU_REGION_PAL
#else
#define PSCU_CONFIGURED_REGION PSCU_REGION_NTSC_UC
#endif

// Video CD filter for the SCPH-5903 (NTSC-J, so use with REGION=jp). Set by
// the Makefile's VCD_FILTER knob; off by default.
#ifndef PSCU_VCD_FILTER
#define PSCU_VCD_FILTER 0
#endif
#define PSCU_VCD_FILTER_ENABLED (PSCU_VCD_FILTER != 0)

// Indicator profile on PB3, set by the Makefile's PROFILE knob; debug by
// default. Only debug lights the pin during board detection and each string.
// The pin is never written inside a string.
#ifndef PSCU_PROFILE_FINAL
#define PSCU_PROFILE_FINAL 0
#endif
#define PSCU_LAMP_STRINGS (PSCU_PROFILE_FINAL == 0)

// Most region strings per arming: a safety cap so a stuck window cannot drive
// the bus forever. The console latches the region within a few reads.
#define PSCU_STEALTH_STRINGS ((uint8_t)16U)

// Frames between two strings: 67 ms at 75 Hz (PsNee V9.0 REQUEST_INJECT_GAP 5;
// Mayumi V4 pauses about 72 ms).
#define PSCU_STEALTH_GAP_FRAMES ((uint8_t)5U)

#endif
