// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_PORT_REGISTERS_H
#define PSCU_PORT_REGISTERS_H

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

#elif defined(__AVR_ATtiny84__) || defined(__AVR_ATtiny84A__) || \
    defined(__AVR_ATtiny44__) || defined(__AVR_ATtiny24__)

#define PSCU_PORT PORTA
#define PSCU_DDR DDRA
#define PSCU_PINREG PINA
#define PSCU_WDT_REG WDTCSR

#define PSCU_PIN_SQCK 0
#define PSCU_PIN_SUBQ 1
#define PSCU_PIN_DATA 2
#define PSCU_PIN_WFCK 3
#define PSCU_PIN_LED 4
#define PSCU_PIN_LID 5
#define PSCU_PIN_RESET 6

#else
#error "unsupported MCU: no PSCU board profile"
#endif

#endif
