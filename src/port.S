// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

// Hardware layer. Every register touch in the whole firmware lives here, behind
// the C prototypes in include/port/port.h. The C layers never include avr/io,
// because avr-libc reaches registers through casts MISRA forbids; isolating
// them in assembly keeps the C MISRA-clean and keeps the timing explicit.

#include <avr/io.h>

#include "port/registers.h"

// Busy-loop iteration counts, derived from F_CPU so the delays recalibrate
// automatically for the internal 8 MHz build and the external-clock build. The
// delay inner loop is 4 cycles. The mirror inner loop is (10 + shift) cycles,
// where shift is the pin distance WFCK-DATA that aligns the carrier bit onto
// DATA (2 on the ATtiny85, 1 on the ATtiny84); computing it from the pins keeps
// the one-bit cell exactly one millisecond on either chip rather than assuming
// a fixed loop length.
#define PSCU_MIRROR_LOOP_CYCLES (10UL + (PSCU_PIN_WFCK - PSCU_PIN_DATA))
#define PSCU_DELAY_1MS_ITERS (F_CPU / 4000UL)
#define PSCU_MIRROR_1MS_ITERS (F_CPU / (PSCU_MIRROR_LOOP_CYCLES * 1000UL))

	.text

// Bring the chip up. Writing 0x80 then 0x00 to CLKPR within four cycles is the
// AVR timed sequence that clears the clock prescaler, forcing a true 8 MHz core
// regardless of the factory CKDIV8 fuse. LED is the only output at boot; the
// board profile's PORT_INIT enables any pull-ups the variant needs. The two
// writes to the watchdog register are its own timed sequence: the first arms
// the change, the second enables the watchdog at a ~0.5 s timeout (WDP2|WDP0),
// so any hang longer than one loop pass resets the chip instead of wedging it.

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

	.global pscu_port_read_subq
	.func pscu_port_read_subq
pscu_port_read_subq:
	in r24, _SFR_IO_ADDR(PSCU_PINREG)
	andi r24, (1 << PSCU_PIN_SUBQ)
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
	.rept (PSCU_PIN_WFCK - PSCU_PIN_DATA)
	lsr r18
	.endr
	andi r18, (1 << PSCU_PIN_DATA)
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

// EEPROM primitives for the diagnostics recorder. The AVR EEPROM is reached
// through EECR/EEDR/EEAR, identical on the ATtiny85 and ATtiny84, so one copy
// serves both. Each call first spins until EEPE clears, meaning any earlier
// write has finished, kicking the watchdog so the wait can never wedge the chip.
// r1 is the compiler's fixed zero register, so the high address byte is cleared
// by writing it; our addresses are always below 256. The firmware calls these
// only after it has gone idle, so the millisecond-scale write latency is spent
// when nothing is time-critical.
	.global pscu_port_eeprom_read
	.func pscu_port_eeprom_read
pscu_port_eeprom_read:
13:
	wdr
	sbic _SFR_IO_ADDR(EECR), EEPE
	rjmp 13b
	out _SFR_IO_ADDR(EEARH), r1
	out _SFR_IO_ADDR(EEARL), r24
	sbi _SFR_IO_ADDR(EECR), EERE
	in r24, _SFR_IO_ADDR(EEDR)
	ret
	.endfunc

// Write sets the address and data, then performs the AVR timed sequence: set
// EEMPE, then set EEPE within four cycles. Two back-to-back sbi instructions are
// two cycles each, so the second lands inside the window. The firmware never
// enables interrupts, so nothing can break the sequence apart.
	.global pscu_port_eeprom_write
	.func pscu_port_eeprom_write
pscu_port_eeprom_write:
14:
	wdr
	sbic _SFR_IO_ADDR(EECR), EEPE
	rjmp 14b
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
// WFCK level so only a low-to-high transition counts; the watchdog is kicked
// every pass so a stalled carrier resets the chip rather than wedging it.

// Hold for one bit cell without touching DATA; the caller has already driven it
// low, so this is the zero-bit cell on a modern board.
	.func pscu_port_wfck_wait_periods
pscu_port_wfck_wait_periods:
	cpi r24, 0
	breq 19f
	in r19, _SFR_IO_ADDR(PSCU_PINREG)
	mov r18, r19
	.rept PSCU_PIN_WFCK
	lsr r18
	.endr
	andi r18, 1
18:
	in r19, _SFR_IO_ADDR(PSCU_PINREG)
	mov r22, r19
	.rept PSCU_PIN_WFCK
	lsr r22
	.endr
	andi r22, 1
	cp r22, r18
	breq 20f
	mov r18, r22
	tst r22
	breq 20f
	subi r24, 1
	breq 19f
20:
	wdr
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
	.rept PSCU_PIN_WFCK
	lsr r18
	.endr
	andi r18, 1
15:
	in r19, _SFR_IO_ADDR(PSCU_PINREG)
	mov r20, r19
	.rept (PSCU_PIN_WFCK - PSCU_PIN_DATA)
	lsr r20
	.endr
	andi r20, (1 << PSCU_PIN_DATA)
	in r21, _SFR_IO_ADDR(PSCU_PORT)
	andi r21, (~(1 << PSCU_PIN_DATA)) & 0xFF
	or r21, r20
	out _SFR_IO_ADDR(PSCU_PORT), r21
	mov r22, r19
	.rept PSCU_PIN_WFCK
	lsr r22
	.endr
	andi r22, 1
	cp r22, r18
	breq 17f
	mov r18, r22
	tst r22
	breq 17f
	subi r24, 1
	breq 16f
17:
	wdr
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

#if PSCU_BIOS_ENABLED

// Read the address line AX. Unlike the SCEx signals on PORTA, AX is on PB2
// (INT0), so it reads PINB directly and returns the masked bit.
	.global pscu_port_bios_ax
	.func pscu_port_bios_ax
pscu_port_bios_ax:
	in r24, _SFR_IO_ADDR(PINB)
	andi r24, (1 << PSCU_PIN_AX)
	ret
	.endfunc

// Count r24 rising edges on AX, then override the data-bus bit DX. On the final
// edge the cycle-accurate window begins: wait the model's bit-offset, drive DX
// low (switch it to output over a preset-low port bit), hold for the override
// window, then release to high-Z. The offset and override lengths are fixed nop
// runs sized at assemble time from the per-model constants, so the timing is
// exact and carries no loop overhead. The watchdog is kicked while waiting for
// an edge, since a slow console boot can leave a long quiet gap, but never
// inside the offset-and-override window where the timing must be exact.
	.global pscu_port_bios_override
	.func pscu_port_bios_override
pscu_port_bios_override:
9:
	wdr
	sbic _SFR_IO_ADDR(PINB), PSCU_PIN_AX
	rjmp 9b
10:
	wdr
	sbis _SFR_IO_ADDR(PINB), PSCU_PIN_AX
	rjmp 10b
	subi r24, 1
	brne 9b
	.rept PSCU_BIOS_OFFSET_CYCLES
	nop
	.endr
	cbi _SFR_IO_ADDR(PSCU_PORT), PSCU_PIN_DX
	sbi _SFR_IO_ADDR(PSCU_DDR), PSCU_PIN_DX
	.rept PSCU_BIOS_OVERRIDE_CYCLES
	nop
	.endr
	cbi _SFR_IO_ADDR(PSCU_DDR), PSCU_PIN_DX
	ret
	.endfunc

#if PSCU_BIOS_TWO_PHASE

// The second override for the two-phase models, identical in shape to the
// first but counting edges on AY and using the second window's offset and
// length. DX is the same data-bus line.
	.global pscu_port_bios_override_ay
	.func pscu_port_bios_override_ay
pscu_port_bios_override_ay:
11:
	wdr
	sbic _SFR_IO_ADDR(PINA), PSCU_PIN_AY
	rjmp 11b
12:
	wdr
	sbis _SFR_IO_ADDR(PINA), PSCU_PIN_AY
	rjmp 12b
	subi r24, 1
	brne 11b
	.rept PSCU_BIOS_OFFSET_2_CYCLES
	nop
	.endr
	cbi _SFR_IO_ADDR(PSCU_PORT), PSCU_PIN_DX
	sbi _SFR_IO_ADDR(PSCU_DDR), PSCU_PIN_DX
	.rept PSCU_BIOS_OVERRIDE_2_CYCLES
	nop
	.endr
	cbi _SFR_IO_ADDR(PSCU_DDR), PSCU_PIN_DX
	ret
	.endfunc

#endif

#endif
