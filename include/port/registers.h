// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_PORT_REGISTERS_H
#define PSCU_PORT_REGISTERS_H

// Maps signals to ATtiny85 pins, in PsNee's order (PsNee MCU.h) so its wiring
// guides apply. PB5 stays RESET for ISP. PORT_INIT is PORTB at boot: no
// pull-ups, indicator off, except that the trace profile's serial line idles
// high from the first instruction, so an adapter never reads the boot as a
// break and a stray zero byte.
#if defined(__AVR_ATtiny85__) || defined(__AVR_ATtiny45__) || defined(__AVR_ATtiny25__)

#define PSCU_PORT PORTB
#define PSCU_DDR DDRB
#define PSCU_PINREG PINB
#define PSCU_WDT_REG WDTCR

#define PSCU_PIN_SQCK 0
#define PSCU_PIN_SUBQ 1
#define PSCU_PIN_DATA 2
#define PSCU_PIN_INDICATOR 3
#define PSCU_PIN_WFCK 4

#if PSCU_PROFILE_TRACE
#define PSCU_PORT_INIT (1 << PSCU_PIN_INDICATOR)
#else
#define PSCU_PORT_INIT 0x00
#endif

#else
#error "unsupported MCU: the firmware targets the ATtiny85 family only"
#endif

#endif
