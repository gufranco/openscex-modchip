// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_PORT_REGISTERS_H
#define PSCU_PORT_REGISTERS_H

// Board profile: the one place that maps logical signals to the ATtiny84's
// registers and pins, so the rest of the firmware names signals, not bits. The
// SCEx signals sit on PORTA in PsNee's tested order (SQCK, SUBQ, DATA, then
// WFCK and the LED), read from its MCU.h. PORTB carries what only the 14-pin
// part can offer: PB0 is CLKI, where the console clock enters, and PB1 is the
// lid line, which sits apart from the LED so neither displaces the other. PB3
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

// The lid line, on PORTB. It reads high while the lid is open, the polarity the
// Mayumi V4 firmware reads on its door input (Read: its GP0 wait loops at
// 0x127 and 0x1d9 spin while the line is high).
#define PSCU_PIN_LID 1

#define PSCU_PORT_INIT 0x00

#else
#error "unsupported MCU: the firmware targets the ATtiny84 family only"
#endif

#endif
