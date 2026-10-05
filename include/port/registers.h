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

#elif defined(__AVR_ATtiny84__) || defined(__AVR_ATtiny84A__) || defined(__AVR_ATtiny44__) || \
    defined(__AVR_ATtiny24__)

// ATtiny84 profile. The SCEx signals sit on PORTA exactly as on the 85, so the
// shared engine and assembly use them through these macros unchanged. The
// 14-pin part's spare pins carry the boot-ROM BIOS patch, which the full build
// adds. PB3 stays RESET.
#define PSCU_PORT PORTA
#define PSCU_DDR DDRA
#define PSCU_PINREG PINA
#define PSCU_WDT_REG WDTCSR

#define PSCU_PIN_SQCK 0
#define PSCU_PIN_SUBQ 1
#define PSCU_PIN_DATA 2
#define PSCU_PIN_WFCK 3
#define PSCU_PIN_LED 4
// BIOS-patch pins (full build only). DX, the data-bus override, is a spare
// PORTA pin alongside the SCEx signals. AX is the address line whose pulses the
// patch counts; AY is the second address line the two oldest Japanese models
// need for their second patch window. Both are read by polling, so neither has
// to be an interrupt pin, which is why all three fit without the second
// external interrupt the ATtiny84 lacks.
#define PSCU_PIN_DX 5
#define PSCU_PIN_AX 2
#define PSCU_PIN_AY 6

#define PSCU_PORT_INIT 0x00

#else
#error "unsupported MCU: no PSCU board profile"
#endif

#endif
