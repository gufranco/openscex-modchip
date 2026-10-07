// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

// Hardware layer. Every register touch in the whole firmware lives here, behind
// the C prototypes in include/port/port.h. The C layers never include avr/io,
// because avr-libc reaches registers through casts MISRA forbids; isolating
// them in assembly keeps the C MISRA-clean and keeps the timing explicit.

#include <avr/io.h>

#include "port/registers.h"

// Busy-loop iteration counts, derived from F_CPU, the console clock on CLKI, so
// every delay is locked to the console's own crystal. The delay inner loop is 4
// cycles. The mirror inner loop is (10 + shift) cycles, where shift is the pin
// distance WFCK-DATA that aligns the carrier bit onto DATA; computing it from the
// pins keeps the one-bit cell exactly one millisecond rather than assuming a
// fixed loop length.
#define PSCU_MIRROR_LOOP_CYCLES (10UL + (PSCU_PIN_WFCK - PSCU_PIN_DATA))
#define PSCU_DELAY_1MS_ITERS (F_CPU / 4000UL)
// A SUBQ edge wait gives up after 30 ms, the same bound as the idle wait in
// engine.c: long enough for the inter-frame gap before a burst's first edge (a
// frame every 13.3 ms at single speed), short enough that a stopped drive fails
// a capture quickly and the run loop keeps reading the lid. One wait pass is 7
// cycles (wdr 1, the skipping sbis or sbic 2, sbiw 2, a taken brne 2).
#define PSCU_SQCK_WAIT_POLLS ((F_CPU * 30UL) / (1000UL * 7UL))
#define PSCU_MIRROR_1MS_ITERS (F_CPU / (PSCU_MIRROR_LOOP_CYCLES * 1000UL))

// Shift a PINREG snapshot already in <reg> right by <count> bit positions, then
// mask to <mask>, bringing one pin to bit 0 or onto the DATA bit position. This
// is the single idiom the WFCK read and mirror paths repeat six times; the macro
// expands to the identical lsr/andi stream, so the emitted machine code is byte
// for byte unchanged while the call sites read as one named operation. The count
// is a build-time pin distance from registers.h, so it follows the board profile.
.macro pscu_shr_mask reg, count, mask
	.rept \count
	lsr \reg
	.endr
	andi \reg, \mask
.endm

	.text

// Bring the chip up. Writing 0x80 then 0x00 to CLKPR within four cycles is the
// AVR timed sequence that clears the clock prescaler, so the core runs at the
// full console clock regardless of the factory CKDIV8 fuse. LED is the only
// output at boot. The lid input gets its pull-up, so a missing or broken lid
// wire reads as open and the chip stays silent rather than injecting blind.
// Timer1 starts free-running at the clock divided by 1024 as the LED's time
// base; it drives no pin and raises no interrupt. The two writes to the watchdog register are its own timed sequence: the first
// arms the change, the second enables the watchdog at a ~0.5 s timeout
// (WDP2|WDP0), so any hang longer than one loop pass resets the chip instead of
// wedging it. The watchdog runs from its own 128 kHz oscillator, so its timeout
// does not depend on the console clock.

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
	sbi _SFR_IO_ADDR(PORTB), PSCU_PIN_LID
	out _SFR_IO_ADDR(TCCR1A), r1
	ldi r24, (1 << CS12) | (1 << CS10)
	out _SFR_IO_ADDR(TCCR1B), r24
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

// The three input reads each return the masked pin bit, not a clean 0/1, so the
// C side only ever compares them against zero. Masking in hardware here avoids
// a separate shift and keeps the read a single `in`.
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

// DATA is open-collector-style: a logic zero is an active low (clear PORT, then
// set DDR to output), a logic one is high-Z (clear DDR back to input and clear
// PORT so no pull-up drives it). Clearing PORT before setting DDR on the drive
// path avoids a momentary high glitch on the bus.
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

// Blocking millisecond delay. r24:r25 is the millisecond count from the caller;
// the inner loop burns PSCU_DELAY_1MS_ITERS to spend one millisecond, kicking
// the watchdog once per millisecond so a long injection never trips the reset.
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

// Drive DATA as a copy of the live WFCK input for the given milliseconds, the
// modern-board way to send a logic one: the console samples DATA against WFCK,
// so DATA must track that carrier rather than sit at a static level. Each pass
// reads the port, shifts the WFCK bit down to the DATA bit position (the shift
// count is the pin-number distance, computed at assemble time by .rept so it is
// correct for whichever board profile is built), masks it, and writes it back
// into DATA without disturbing the other port bits.
	.global pscu_port_data_mirror_wfck_ms
	.func pscu_port_data_mirror_wfck_ms
pscu_port_data_mirror_wfck_ms:
	sbiw r24, 0
	breq 6f
	sbi _SFR_IO_ADDR(PSCU_DDR), PSCU_PIN_DATA
4:
	ldi r26, lo8(PSCU_MIRROR_1MS_ITERS)
	ldi r27, hi8(PSCU_MIRROR_1MS_ITERS)
5:
	in r18, _SFR_IO_ADDR(PSCU_PINREG)
	pscu_shr_mask r18, (PSCU_PIN_WFCK-PSCU_PIN_DATA), (1<<PSCU_PIN_DATA)
	in r19, _SFR_IO_ADDR(PSCU_PORT)
	andi r19, (~(1 << PSCU_PIN_DATA)) & 0xFF
	or r19, r18
	out _SFR_IO_ADDR(PSCU_PORT), r19
	sbiw r26, 1
	brne 5b
	wdr
	sbiw r24, 1
	brne 4b
6:
	ret
	.endfunc

// Clock in r22 bytes of SUBQ into the buffer at r25:r24, least significant bit
// first, sampling SUBQ right after each SQCK rising edge, as PsNee does. The caller
// has already waited for the idle gap, so the first falling edge starts the frame.
// Returns 1 in r24 for a whole frame, 0 the moment any edge wait times out, and
// the caller then discards the partial frame. The whole frame lives in assembly
// because the gap the firmware can leave between two edges sets the fastest SQCK
// it can follow: each wait reacts within one 7-cycle pass and the work between
// the rising edge and the next falling-edge wait is about a dozen cycles, where
// the earlier C capture spent over 50. The watchdog is kicked on every pass, so a
// frame that keeps trickling in can never trip it, and every wait is bounded.
	.global pscu_port_capture_frame
	.func pscu_port_capture_frame
pscu_port_capture_frame:
	movw r26, r24
30:
	ldi r21, 8
	clr r18
31:
	ldi r30, lo8(PSCU_SQCK_WAIT_POLLS)
	ldi r31, hi8(PSCU_SQCK_WAIT_POLLS)
32:
	wdr
	sbis _SFR_IO_ADDR(PSCU_PINREG), PSCU_PIN_SQCK
	rjmp 33f
	sbiw r30, 1
	brne 32b
	rjmp 36f
33:
	ldi r30, lo8(PSCU_SQCK_WAIT_POLLS)
	ldi r31, hi8(PSCU_SQCK_WAIT_POLLS)
34:
	wdr
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

// A free-running clock for the status LED. Timer1 counts the console clock
// divided by 1024, one tick every 242 us at 4.2336 MHz, and wraps every 15.8 s,
// far longer than any pass of the run loop, so the difference of two readings is
// always the time between them. Reading TCNT1L first latches TCNT1H, which the
// datasheet requires for a consistent 16-bit read. The LED patterns are drawn
// from this count, so they never cost a delay.
	.global pscu_port_ticks
	.func pscu_port_ticks
pscu_port_ticks:
	in r24, _SFR_IO_ADDR(TCNT1L)
	in r25, _SFR_IO_ADDR(TCNT1H)
	ret
	.endfunc

// Report whether the last reset came from the watchdog, and clear the reset
// flags so the next reset reports its own cause. A watchdog reset means a pass
// hung, most likely a WFCK carrier that stopped mid-injection, and the LED shows
// it once at the next boot. Clearing WDRF also lifts the hardware override that
// forces WDE on; the watchdog stays enabled because pscu_port_init set WDE
// itself. r1 is the compiler's zero register.
	.global pscu_port_reset_was_watchdog
	.func pscu_port_reset_was_watchdog
pscu_port_reset_was_watchdog:
	in r24, _SFR_IO_ADDR(MCUSR)
	andi r24, (1 << WDRF)
	out _SFR_IO_ADDR(MCUSR), r1
	ret
	.endfunc

#if PSCU_TIMING_ADAPTIVE

// Adaptive injection timing. On a modern board the WFCK line is a live carrier,
// so instead of timing a bit cell with an MCU delay, which drifts with the RC
// oscillator, these routines count whole WFCK periods. A bit cell is
// PSCU_WFCK_PERIODS_PER_BIT rising edges, so the cell length tracks the console
// clock exactly and is immune to MCU frequency error. r18 holds the previous
// WFCK level so only a low-to-high transition counts. The watchdog is kicked
// only when a rising edge is counted, never on an idle pass: if the carrier
// stalls (lid opened mid-injection, mechacon halt), no edge arrives, the watchdog
// expires after about 0.5 s and resets the chip, and the reset returns DATA and
// every other pin to high-Z. Kicking it on every pass would instead hold DATA
// driven until power-off. wdr leaves the status flags alone, so it can sit
// between the counting subi and its breq.

// Hold for one bit cell without touching DATA; the caller has already driven it
// low, so this is the zero-bit cell on a modern board.
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

// Drive DATA as a copy of the WFCK carrier for one bit cell, the one-bit cell on
// a modern board. Each pass mirrors the current WFCK level onto DATA and counts
// the period, so the mirrored carrier and the cell length share the same clock.
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

#endif

// One modern-board bit cell, selected at build time. The adaptive build counts
// WFCK periods; the fixed build falls back to the millisecond delay that was
// exercised on hardware. Each is a tail call, so the wrapper adds no return
// overhead. The 4 is the 4 ms bit cell (Read, PsNee BIT_DELAY).
	.global pscu_port_bit_hold_low
	.func pscu_port_bit_hold_low
pscu_port_bit_hold_low:
#if PSCU_TIMING_ADAPTIVE
	ldi r24, PSCU_WFCK_PERIODS_PER_BIT
	rjmp pscu_port_wfck_wait_periods
#else
	ldi r24, lo8(4)
	ldi r25, hi8(4)
	rjmp pscu_port_delay_ms
#endif
	.endfunc

	.global pscu_port_bit_mirror
	.func pscu_port_bit_mirror
pscu_port_bit_mirror:
#if PSCU_TIMING_ADAPTIVE
	ldi r24, PSCU_WFCK_PERIODS_PER_BIT
	rjmp pscu_port_wfck_mirror_periods
#else
	ldi r24, lo8(4)
	ldi r25, hi8(4)
	rjmp pscu_port_data_mirror_wfck_ms
#endif
	.endfunc

// The lid line sits on PORTB, apart from the SCEx signals on PORTA, so it reads
// PINB directly and returns the masked bit: nonzero while the lid is open.
	.global pscu_port_read_lid
	.func pscu_port_read_lid
pscu_port_read_lid:
	in r24, _SFR_IO_ADDR(PINB)
	andi r24, (1 << PSCU_PIN_LID)
	ret
	.endfunc
