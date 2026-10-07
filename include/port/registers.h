// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_PORT_REGISTERS_H
#define PSCU_PORT_REGISTERS_H

// Board profile: the one place that maps logical signals to the ATtiny84's
// registers and pins, so the rest of the firmware names signals, not bits. The
// SCEx signals sit on PORTA in PsNee's tested order (SQCK, SUBQ, DATA, then
// WFCK and the LED), read from its MCU.h. PORTB carries what only the 14-pin
// part can offer, which today is room to spare: PB0 to PB2 are unused. PB3
// stays RESET, so the chip remains ISP-programmable. PORT_INIT is the boot value
// of the PORTA register; no PORTA input needs a pull-up.
#if defined(__AVR_ATtiny84__) || defined(__AVR_ATtiny84A__) || defined(__AVR_ATtiny44__) || \
    defined(__AVR_ATtiny24__)

#define PSCU_PORT PORTA
#define PSCU_DDR DDRA
#define PSCU_PINREG PINA
#define PSCU_WDT_REG WDTCSR

#define PSCU_PIN_SQCK 0
#define PSCU_PIN_SUBQ 1
#define PSCU_PIN_DATA 2
#define PSCU_PIN_WFCK 3
#define PSCU_PIN_LED 4

// Boot values of the port registers. Every unused pin is an input with its
// pull-up on, so none floats and picks up noise or draws switching current:
// PA5 to PA7 on PORTA, PB0 to PB2 on PORTB. PB3 is RESET. An install made for
// an earlier release may still have its clock wire on PB0 or its lid wire on
// PB1; a pull-up of a few tens of kilohms on either is harmless, and the
// firmware never drives them.
#define PSCU_PORT_INIT 0xE0
#define PSCU_PORTB_INIT 0x07

#else
#error "unsupported MCU: the firmware targets the ATtiny84 family only"
#endif

#endif
