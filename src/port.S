// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include <avr/io.h>

#include "port/registers.h"

#define PSCU_DELAY_1MS_ITERS (F_CPU / 4000UL)

	.text

	.global pscu_port_init
	.func pscu_port_init
pscu_port_init:
	ldi r24, 0x80
	out _SFR_IO_ADDR(CLKPR), r24
	ldi r24, 0x00
	out _SFR_IO_ADDR(CLKPR), r24
	ldi r24, (1 << PSCU_PIN_LED)
	out _SFR_IO_ADDR(DDRB), r24
	ldi r24, 0x00
	out _SFR_IO_ADDR(PORTB), r24
	wdr
	ldi r24, (1 << WDCE) | (1 << WDE)
	out _SFR_IO_ADDR(WDTCR), r24
	ldi r24, (1 << WDE) | (1 << WDP2) | (1 << WDP0)
	out _SFR_IO_ADDR(WDTCR), r24
	ret
	.endfunc

	.global pscu_port_watchdog_reset
	.func pscu_port_watchdog_reset
pscu_port_watchdog_reset:
	wdr
	ret
	.endfunc

	.global pscu_port_read_sqck
	.func pscu_port_read_sqck
pscu_port_read_sqck:
	in r24, _SFR_IO_ADDR(PINB)
	andi r24, (1 << PSCU_PIN_SQCK)
	ret
	.endfunc

	.global pscu_port_read_subq
	.func pscu_port_read_subq
pscu_port_read_subq:
	in r24, _SFR_IO_ADDR(PINB)
	andi r24, (1 << PSCU_PIN_SUBQ)
	ret
	.endfunc

	.global pscu_port_read_wfck
	.func pscu_port_read_wfck
pscu_port_read_wfck:
	in r24, _SFR_IO_ADDR(PINB)
	andi r24, (1 << PSCU_PIN_WFCK)
	ret
	.endfunc

	.global pscu_port_data_drive_low
	.func pscu_port_data_drive_low
pscu_port_data_drive_low:
	cbi _SFR_IO_ADDR(PORTB), PSCU_PIN_DATA
	sbi _SFR_IO_ADDR(DDRB), PSCU_PIN_DATA
	ret
	.endfunc

	.global pscu_port_data_release
	.func pscu_port_data_release
pscu_port_data_release:
	cbi _SFR_IO_ADDR(DDRB), PSCU_PIN_DATA
	cbi _SFR_IO_ADDR(PORTB), PSCU_PIN_DATA
	ret
	.endfunc

	.global pscu_port_led_on
	.func pscu_port_led_on
pscu_port_led_on:
	sbi _SFR_IO_ADDR(PORTB), PSCU_PIN_LED
	ret
	.endfunc

	.global pscu_port_led_off
	.func pscu_port_led_off
pscu_port_led_off:
	cbi _SFR_IO_ADDR(PORTB), PSCU_PIN_LED
	ret
	.endfunc

	.global pscu_port_delay_ms
	.func pscu_port_delay_ms
pscu_port_delay_ms:
	sbiw r24, 0
	breq 2f
1:
	ldi r26, lo8(PSCU_DELAY_1MS_ITERS)
	ldi r27, hi8(PSCU_DELAY_1MS_ITERS)
3:
	sbiw r26, 1
	brne 3b
	wdr
	sbiw r24, 1
	brne 1b
2:
	ret
	.endfunc
