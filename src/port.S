// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

// Hardware layer. Every register touch in the whole firmware lives here, behind
// the C prototypes in include/port/port.h. The C layers never include avr/io,
// because avr-libc reaches registers through casts MISRA forbids; isolating
// them in assembly keeps the C MISRA-clean and keeps the timing explicit.

#include <avr/io.h>

#include "port/registers.h"

// Busy-loop iteration counts, derived from F_CPU, the internal 8 MHz oscillator,
// which the run loop trims against the console's SUBQ frame rate. The delay inner loop is 4
// cycles. The mirror inner loop is (10 + shift) cycles, where shift is the pin
// distance WFCK-DATA that aligns the carrier bit onto DATA; computing it from the
// pins keeps the one-bit cell exactly one millisecond rather than assuming a
// fixed loop length.
#define PSCU_MIRROR_LOOP_CYCLES (10UL + (PSCU_PIN_WFCK - PSCU_PIN_DATA))
#define PSCU_DELAY_1MS_ITERS (F_CPU / 4000UL)
// A SUBQ edge wait gives up after 30 ms, the same bound as the idle wait in
// engine.c: long enough for the inter-frame gap before a burst's first edge (a
// frame every 13.3 ms at single speed), short enough that a stopped drive fails
// a capture quickly and the run loop keeps timing the silence that tells it the
// disc is gone. One wait pass is 7
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
// full 8 MHz regardless of the factory CKDIV8 fuse. LED is the only output at
// boot. The ADC is switched on to read the 1.1 V bandgap against VCC, which is
// how the chip measures its own supply: REFS 000 makes VCC the reference and
// MUX 1100 selects the bandgap (Read: ATtiny25/45/85 datasheet 2586Q, ADC input
// channel and voltage reference selections), and CK/64 gives the 125 kHz the
// datasheet asks for (50 to 200 kHz for full resolution). Switching it on here
// leaves the bandgap well over its start-up time before the first reading,
// which comes after board detection. Timer1 starts free-running at the clock
// divided by 16384, its slowest synchronous setting (Read: ATtiny25/45/85
// datasheet 2586Q, Table 12-5, CS13:0 = 1111), as the time base for the LED and
// the oscillator trim; it drives no pin and raises no interrupt. The two writes
// to the watchdog register are its own timed sequence: the first arms the
// change, the second enables the watchdog at a ~0.5 s timeout (WDP2|WDP0), so
// any hang longer than one loop pass resets the chip instead of wedging it. The
// watchdog runs from its own 128 kHz oscillator, so its timeout does not depend
// on the main clock or its trim.

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

// The gate on PU-18 and PU-20 boards. There WFCK is a static level rather than
// a carrier, and the chip holds it low for the whole region string, then
// releases it, as both long-deployed chips do: PsNee drives it as an output low
// around every legacy string (Read: PsNee V9.0 PSNee.ino:593-595 and :652-655),
// and Mayumi V4 makes its gate pin an output on legacy boards (Read: its binary).
// Same drive order as DATA: PORT cleared before DDR is set, so the line never
// glitches high. A carrier board never calls these; its WFCK stays an input.
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

// A free-running clock for the status LED and the oscillator trim. The
// ATtiny85's Timer1 is 8-bit: at the clock divided by 16384 it ticks every
// 2.048 ms at 8 MHz and wraps every 524 ms, longer than any pass of the run loop
// (a region string is 176 ms, a failed capture about 60 ms, the EEPROM store
// about 24 ms), so the difference of two readings is always the time between
// them. The LED patterns are drawn from this count, so they never cost a
// delay.
	.global pscu_port_ticks
	.func pscu_port_ticks
pscu_port_ticks:
	in r24, _SFR_IO_ADDR(TCNT1)
	ret
	.endfunc

// Report whether the last reset came from the watchdog, and clear the reset
// flags so the next reset reports its own cause. A watchdog reset means a pass
// hung, most likely a WFCK carrier that stopped mid-injection, and the LED shows
// it once at the next boot. Clearing WDRF also lifts the hardware override that
// forces WDE on; the watchdog stays enabled because pscu_port_init set WDE
// itself. r1 is the compiler's zero register.
// The oscillator calibration register. Hardware loads the factory value into it
// at every reset, so reading it at boot gives the factory value; writing it
// retunes the internal 8 MHz oscillator at once. The caller moves it one step at
// a time, as the datasheet asks (ATtiny25/45/85 datasheet 2586Q, OSCCAL: change in small steps, a
// cycle-to-cycle variation over 2 percent is unpredictable).
	.global pscu_port_osccal_read
	.func pscu_port_osccal_read
pscu_port_osccal_read:
	in r24, _SFR_IO_ADDR(OSCCAL)
	ret
	.endfunc

	.global pscu_port_osccal_write
	.func pscu_port_osccal_write
pscu_port_osccal_write:
	out _SFR_IO_ADDR(OSCCAL), r24
	ret
	.endfunc

// One supply reading: start a conversion of the bandgap against VCC and wait
// for it, bounded. A conversion takes 13 ADC clocks, 25 for the first after the
// ADC is enabled, at most 1600 CPU cycles at CK/64 (Read: the same datasheet);
// the wait gives up after 1024 passes of 7 cycles, about 900 us, and then
// returns 0, a reading the caller treats as implausible. ADCL is read first,
// which locks ADCH until it is read, as the datasheet requires for a 10-bit
// result. r26 and r27 are call-clobbered under the avr-gcc ABI.
	.global pscu_port_supply_raw
	.func pscu_port_supply_raw
pscu_port_supply_raw:
	sbi _SFR_IO_ADDR(ADCSRA), ADSC
	ldi r26, lo8(1024)
	ldi r27, hi8(1024)
16:
	wdr
	sbis _SFR_IO_ADDR(ADCSRA), ADSC
	rjmp 17f
	sbiw r26, 1
	brne 16b
	ldi r24, 0
	ldi r25, 0
	ret
17:
	in r24, _SFR_IO_ADDR(ADCL)
	in r25, _SFR_IO_ADDR(ADCH)
	ret
	.endfunc

	.global pscu_port_reset_was_watchdog
	.func pscu_port_reset_was_watchdog
pscu_port_reset_was_watchdog:
	in r24, _SFR_IO_ADDR(MCUSR)
	andi r24, (1 << WDRF)
	out _SFR_IO_ADDR(MCUSR), r1
	ret
	.endfunc

// EEPROM primitives for the calibration record. The EEPROM is reached through
// EECR, EEDR and EEAR. Before touching it, each call waits for EEPE to clear,
// meaning any earlier write has finished. An erase-and-write takes 3.4 ms (Read:
// ATtiny25/45/85 datasheet 2586Q, Table 5-1, EEPM 00, the mode reset
// leaves selected), so the wait gives up after 10 ms rather than trusting the
// flag forever; one pass is 6 cycles (wdr 1, the non-skipping sbis 1, sbiw 2, a
// taken brne 2). The watchdog is kicked inside the wait, which the bound keeps
// from ever hiding a hang. r26 and r27 are call-clobbered under the avr-gcc ABI.
// r1 is the compiler's zero register, so the high address byte is cleared by
// writing it; the record sits below address 256.
#define PSCU_EEPROM_WAIT_POLLS ((F_CPU * 10UL) / (1000UL * 6UL))

	.func pscu_port_eeprom_wait
pscu_port_eeprom_wait:
	ldi r26, lo8(PSCU_EEPROM_WAIT_POLLS)
	ldi r27, hi8(PSCU_EEPROM_WAIT_POLLS)
15:
	wdr
	sbis _SFR_IO_ADDR(EECR), EEPE
	ret
	sbiw r26, 1
	brne 15b
	ret
	.endfunc

	.global pscu_port_eeprom_read
	.func pscu_port_eeprom_read
pscu_port_eeprom_read:
	rcall pscu_port_eeprom_wait
	out _SFR_IO_ADDR(EEARH), r1
	out _SFR_IO_ADDR(EEARL), r24
	sbi _SFR_IO_ADDR(EECR), EERE
	in r24, _SFR_IO_ADDR(EEDR)
	ret
	.endfunc

// Write sets the address and data, then performs the AVR timed sequence: set
// EEMPE, then set EEPE within four cycles. Two back-to-back sbi instructions are
// two cycles each, so the second lands inside the window. The firmware never
// enables interrupts, so nothing can break the sequence apart. The call returns
// as soon as the write has started; the hardware finishes it in the background,
// and the next EEPROM call waits for it.
	.global pscu_port_eeprom_write
	.func pscu_port_eeprom_write
pscu_port_eeprom_write:
	rcall pscu_port_eeprom_wait
	out _SFR_IO_ADDR(EEARH), r1
	out _SFR_IO_ADDR(EEARL), r24
	out _SFR_IO_ADDR(EEDR), r22
	sbi _SFR_IO_ADDR(EECR), EEMPE
	sbi _SFR_IO_ADDR(EECR), EEPE
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

