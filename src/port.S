// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

// Hardware layer for the console's signals: port, SUBQ capture, injection and
// timer. Registers live only in assembly, since avr-libc's register casts break
// MISRA; port_chip.S holds the chip's housekeeping.

#include <avr/io.h>

#include "port/registers.h"

// Delay loop iterations per millisecond, from F_CPU; the inner loop is 4 cycles.
#define PSCU_DELAY_1MS_ITERS (F_CPU / 4000UL)
// SUBQ edge wait timeout, 30 ms as in engine.c. One pass is 6 cycles (skipping
// sbis or sbic 2, sbiw 2, taken brne 2).
#define PSCU_SQCK_WAIT_POLLS ((F_CPU * 30UL) / (1000UL * 6UL))
// Carrier-board bit cell: 30 WFCK rising edges, about 4 ms at 7.3 kHz (PsNee
// PerformInjectionSequence).
#define PSCU_WFCK_PERIODS_PER_BIT 30

// Shift a PINREG snapshot in <reg> right by <count>, then mask to <mask>.
.macro pscu_shr_mask reg, count, mask
	.rept \count
	lsr \reg
	.endr
	andi \reg, \mask
.endm

	.text

// Clear the clock prescaler (8 MHz whatever CKDIV8 says), make PB3 the only
// output, start the ADC on the bandgap against VCC at CK/64 (125 kHz), Timer1
// at clk/16384 (datasheet 2586Q, Table 12-5), and the watchdog at about 0.5 s.

	.global pscu_port_init
	.func pscu_port_init
pscu_port_init:
	ldi r24, 0x80
	out _SFR_IO_ADDR(CLKPR), r24
	ldi r24, 0x00
	out _SFR_IO_ADDR(CLKPR), r24
	ldi r24, (1 << PSCU_PIN_LED)
	out _SFR_IO_ADDR(PSCU_DDR), r24
	ldi r24, PSCU_PORT_INIT
	out _SFR_IO_ADDR(PSCU_PORT), r24
	ldi r24, (1 << CS13) | (1 << CS12) | (1 << CS11) | (1 << CS10)
	out _SFR_IO_ADDR(TCCR1), r24
	ldi r24, (1 << MUX3) | (1 << MUX2)
	out _SFR_IO_ADDR(ADMUX), r24
	ldi r24, (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1)
	out _SFR_IO_ADDR(ADCSRA), r24
	wdr
	ldi r24, (1 << WDCE) | (1 << WDE)
	out _SFR_IO_ADDR(PSCU_WDT_REG), r24
	ldi r24, (1 << WDE) | (1 << WDP2) | (1 << WDP0)
	out _SFR_IO_ADDR(PSCU_WDT_REG), r24
	ret
	.endfunc

	.global pscu_port_watchdog_reset
	.func pscu_port_watchdog_reset
pscu_port_watchdog_reset:
	wdr
	ret
	.endfunc

// Input reads return the masked pin bit, not 0 or 1.
	.global pscu_port_read_sqck
	.func pscu_port_read_sqck
pscu_port_read_sqck:
	in r24, _SFR_IO_ADDR(PSCU_PINREG)
	andi r24, (1 << PSCU_PIN_SQCK)
	ret
	.endfunc

	.global pscu_port_read_wfck
	.func pscu_port_read_wfck
pscu_port_read_wfck:
	in r24, _SFR_IO_ADDR(PSCU_PINREG)
	andi r24, (1 << PSCU_PIN_WFCK)
	ret
	.endfunc

// DATA is open-drain style: low or high-Z. PORT is cleared before DDR is set, so
// the line never glitches high.
	.global pscu_port_data_drive_low
	.func pscu_port_data_drive_low
pscu_port_data_drive_low:
	cbi _SFR_IO_ADDR(PSCU_PORT), PSCU_PIN_DATA
	sbi _SFR_IO_ADDR(PSCU_DDR), PSCU_PIN_DATA
	ret
	.endfunc

	.global pscu_port_data_release
	.func pscu_port_data_release
pscu_port_data_release:
	cbi _SFR_IO_ADDR(PSCU_DDR), PSCU_PIN_DATA
	cbi _SFR_IO_ADDR(PSCU_PORT), PSCU_PIN_DATA
	ret
	.endfunc

// The WFCK gate on legacy boards, held low for each string (PsNee V9.0
// PSNee.ino:593-595 and :652-655). Same drive order as DATA.
	.global pscu_port_gate_drive_low
	.func pscu_port_gate_drive_low
pscu_port_gate_drive_low:
	cbi _SFR_IO_ADDR(PSCU_PORT), PSCU_PIN_WFCK
	sbi _SFR_IO_ADDR(PSCU_DDR), PSCU_PIN_WFCK
	ret
	.endfunc

	.global pscu_port_gate_release
	.func pscu_port_gate_release
pscu_port_gate_release:
	cbi _SFR_IO_ADDR(PSCU_DDR), PSCU_PIN_WFCK
	cbi _SFR_IO_ADDR(PSCU_PORT), PSCU_PIN_WFCK
	ret
	.endfunc

	.global pscu_port_led_on
	.func pscu_port_led_on
pscu_port_led_on:
	sbi _SFR_IO_ADDR(PSCU_PORT), PSCU_PIN_LED
	ret
	.endfunc

	.global pscu_port_led_off
	.func pscu_port_led_off
pscu_port_led_off:
	cbi _SFR_IO_ADDR(PSCU_PORT), PSCU_PIN_LED
	ret
	.endfunc

// Busy-wait r25:r24 milliseconds, kicking the watchdog each millisecond.
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

// Clock r22 SUBQ bytes into r25:r24, LSB first, sampling after each SQCK rising
// edge. Returns 1 for a whole frame, 0 on any edge timeout. In assembly to follow
// a fast SQCK. The watchdog is kicked once, so a frame slower than 0.5 s resets.
	.global pscu_port_capture_frame
	.func pscu_port_capture_frame
pscu_port_capture_frame:
	wdr
	movw r26, r24
30:
	ldi r21, 8
	clr r18
31:
	ldi r30, lo8(PSCU_SQCK_WAIT_POLLS)
	ldi r31, hi8(PSCU_SQCK_WAIT_POLLS)
32:
	sbis _SFR_IO_ADDR(PSCU_PINREG), PSCU_PIN_SQCK
	rjmp 33f
	sbiw r30, 1
	brne 32b
	rjmp 36f
33:
	ldi r30, lo8(PSCU_SQCK_WAIT_POLLS)
	ldi r31, hi8(PSCU_SQCK_WAIT_POLLS)
34:
	sbic _SFR_IO_ADDR(PSCU_PINREG), PSCU_PIN_SQCK
	rjmp 35f
	sbiw r30, 1
	brne 34b
	rjmp 36f
35:
	lsr r18
	sbic _SFR_IO_ADDR(PSCU_PINREG), PSCU_PIN_SUBQ
	ori r18, 0x80
	dec r21
	brne 31b
	st X+, r18
	dec r22
	brne 30b
	ldi r24, 1
	ret
36:
	ldi r24, 0
	ret
	.endfunc

// Timer1: one tick per 2.048 ms, wrapping every 524 ms, longer than any pass.
	.global pscu_port_ticks
	.func pscu_port_ticks
pscu_port_ticks:
	in r24, _SFR_IO_ADDR(TCNT1)
	ret
	.endfunc

// Bit cells timed by counting WFCK rising edges; r18 holds the last level. The
// watchdog is kicked only on an edge, so a stalled carrier resets the chip and
// releases DATA. wdr keeps the flags, so it can sit between subi and breq.

// Zero-bit cell: wait without touching DATA, which the caller drove low.
	.func pscu_port_wfck_wait_periods
pscu_port_wfck_wait_periods:
	cpi r24, 0
	breq 19f
	in r19, _SFR_IO_ADDR(PSCU_PINREG)
	mov r18, r19
	pscu_shr_mask r18, PSCU_PIN_WFCK, 1
18:
	in r19, _SFR_IO_ADDR(PSCU_PINREG)
	mov r22, r19
	pscu_shr_mask r22, PSCU_PIN_WFCK, 1
	cp r22, r18
	breq 20f
	mov r18, r22
	tst r22
	breq 20f
	subi r24, 1
	wdr
	breq 19f
20:
	rjmp 18b
19:
	ret
	.endfunc

// One-bit cell: copy WFCK onto DATA while counting periods.
	.func pscu_port_wfck_mirror_periods
pscu_port_wfck_mirror_periods:
	cpi r24, 0
	breq 16f
	sbi _SFR_IO_ADDR(PSCU_DDR), PSCU_PIN_DATA
	in r19, _SFR_IO_ADDR(PSCU_PINREG)
	mov r18, r19
	pscu_shr_mask r18, PSCU_PIN_WFCK, 1
15:
	in r19, _SFR_IO_ADDR(PSCU_PINREG)
	mov r20, r19
	pscu_shr_mask r20, (PSCU_PIN_WFCK-PSCU_PIN_DATA), (1<<PSCU_PIN_DATA)
	in r21, _SFR_IO_ADDR(PSCU_PORT)
	andi r21, (~(1 << PSCU_PIN_DATA)) & 0xFF
	or r21, r20
	out _SFR_IO_ADDR(PSCU_PORT), r21
	mov r22, r19
	pscu_shr_mask r22, PSCU_PIN_WFCK, 1
	cp r22, r18
	breq 17f
	mov r18, r22
	tst r22
	breq 17f
	subi r24, 1
	wdr
	breq 16f
17:
	rjmp 15b
16:
	ret
	.endfunc

// Public bit-cell entry points, as tail calls.
	.global pscu_port_bit_hold_low
	.func pscu_port_bit_hold_low
pscu_port_bit_hold_low:
	ldi r24, PSCU_WFCK_PERIODS_PER_BIT
	rjmp pscu_port_wfck_wait_periods
	.endfunc

	.global pscu_port_bit_mirror
	.func pscu_port_bit_mirror
pscu_port_bit_mirror:
	ldi r24, PSCU_WFCK_PERIODS_PER_BIT
	rjmp pscu_port_wfck_mirror_periods
	.endfunc

