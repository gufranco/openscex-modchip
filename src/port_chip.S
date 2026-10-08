// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

// Hardware layer, chip half: the registers that concern the ATtiny85 itself
// rather than the console's signals. The reset cause, the oscillator
// calibration, the supply measurement and the EEPROM that holds the
// calibration record. Split from port.S, which keeps the signals, so neither
// file grows past what one reading can hold; both sit behind the same C
// prototypes in include/port/port.h, and C never includes avr/io for the reason
// port.S gives.

#include <avr/io.h>

#include "port/registers.h"

	.text

// The oscillator calibration register. Hardware loads the factory value into it
// at every reset, so reading it at boot gives the factory value; writing it
// retunes the internal 8 MHz oscillator at once. The caller moves it one step at
// a time, as the datasheet asks (ATtiny25/45/85 datasheet 2586Q, OSCCAL: change
// in small steps, a cycle-to-cycle variation over 2 percent is unpredictable).
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
// coverage: unreachable block: only a conversion that never ends, a faulty ADC,
// gets here; simavr's ADC, like a working one, always finishes.
	ldi r24, 0
	ldi r25, 0
	ret

17:
	in r24, _SFR_IO_ADDR(ADCL)
	in r25, _SFR_IO_ADDR(ADCH)
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
// coverage: unreachable block: simavr completes an EEPROM write at once, so
// EEPE is always clear here; on the chip a byte takes 3.4 ms and this loop runs.
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
