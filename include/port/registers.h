// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_PORT_REGISTERS_H
#define PSCU_PORT_REGISTERS_H

// Board profile: the one place that maps logical signals to the ATtiny85's
// registers and pins, so the rest of the firmware names signals, not bits. The
// whole design fits the 8-pin part now that it needs no clock and no lid wire:
// the four SCEx signals and the LED sit on PORTB in PsNee's tested ATtiny85
// order (SQCK, SUBQ, DATA, LED, WFCK), read from its MCU.h, so PsNee wiring
// guides match this chip pin for pin. PB5 stays RESET, so the chip remains
// ISP-programmable. PORT_INIT is the boot value of PORTB: every pin is in use,
// so no pull-up is needed and the LED starts off.
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
#error "unsupported MCU: the firmware targets the ATtiny85 family only"
#endif

#endif
