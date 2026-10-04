// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_PORT_REGISTERS_H
#define PSCU_PORT_REGISTERS_H

// Board profile: the one place that maps logical signals to a specific chip's
// registers and pins, so the rest of the firmware is chip-agnostic. The pin
// numbers here are PsNee's tested ATtiny85 (ATTINY_X5) assignment, read from
// its MCU.h, so existing PsNee and Mayumi wiring guides match this chip. The
// whole X5 family shares the layout. PORT_INIT is the boot value of the port
// register, which enables any pull-ups a variant needs (none on the 85).
#if defined(__AVR_ATtiny85__) || defined(__AVR_ATtiny45__) || defined(__AVR_ATtiny25__)

#define PSCU_PORT PORTB
#define PSCU_DDR DDRB
#define PSCU_PINREG PINB
#define PSCU_WDT_REG WDTCR

#define PSCU_PIN_SQCK 0
#define PSCU_PIN_SUBQ 1
#define PSCU_PIN_DATA 2
#define PSCU_PIN_LED 3
#define PSCU_PIN_WFCK 4

#define PSCU_PORT_INIT 0x00

#else
#error "unsupported MCU: no PSCU board profile"
#endif

#endif
