// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

// Hardware layer for the chip itself: reset cause, OSCCAL, supply measurement
// and EEPROM. port.S holds the console's signals.

#include <avr/io.h>

#include "port/registers.h"

	.text

// OSCCAL holds the factory value after reset. The caller changes it one step at
// a time (datasheet 2586Q, OSCCAL).
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

// Convert the bandgap against VCC. A conversion is at most 1600 cycles (25 ADC
// clocks at CK/64); give up after 1024 passes of 7 cycles and return 0. ADCL is
// read first, as the datasheet requires.
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
// coverage: unreachable block: only a faulty ADC never finishes.
	ldi r24, 0
	ldi r25, 0
	ret

17:
	in r24, _SFR_IO_ADDR(ADCL)
	in r25, _SFR_IO_ADDR(ADCH)
	ret
	.endfunc

// Return whether the last reset came from the watchdog and clear the reset
// flags. The watchdog stays on because pscu_port_init set WDE. r1 is zero.
	.global pscu_port_reset_was_watchdog
	.func pscu_port_reset_was_watchdog
pscu_port_reset_was_watchdog:
	in r24, _SFR_IO_ADDR(MCUSR)
	andi r24, (1 << WDRF)
	out _SFR_IO_ADDR(MCUSR), r1
	ret
	.endfunc

// Wait for EEPE to clear, up to 10 ms; a write takes 3.4 ms (datasheet 2586Q,
// Table 5-1). One pass is 7 cycles (wdr 1, sbis 2, sbiw 2, brne 2). r25 is 1
// once EEPE is clear and 0 on a timeout; r24, a caller's address, is kept.
#define PSCU_EEPROM_WAIT_POLLS ((F_CPU * 10UL) / (1000UL * 7UL))

	.func pscu_port_eeprom_wait
pscu_port_eeprom_wait:
	ldi r26, lo8(PSCU_EEPROM_WAIT_POLLS)
	ldi r27, hi8(PSCU_EEPROM_WAIT_POLLS)
	ldi r25, 1
15:
	wdr
	sbis _SFR_IO_ADDR(EECR), EEPE
	ret
// coverage: unreachable block: simavr finishes EEPROM writes at once.
	sbiw r26, 1
	brne 15b
	ldi r25, 0
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

// Clear EECR first: EEPM1:0 reset to an undefined value (datasheet 2586Q,
// EECR), and anything but 00 would only erase or only program the byte. Then set
// EEMPE, then EEPE within four cycles; interrupts are never enabled. Returns 1
// once the write has started, or 0 without touching EEAR or EEDR when the
// previous write never finished, which would corrupt both.
	.global pscu_port_eeprom_write
	.func pscu_port_eeprom_write
pscu_port_eeprom_write:
	rcall pscu_port_eeprom_wait
	tst r25
	breq 21f
	out _SFR_IO_ADDR(EECR), r1
	out _SFR_IO_ADDR(EEARH), r1
	out _SFR_IO_ADDR(EEARL), r24
	out _SFR_IO_ADDR(EEDR), r22
	sbi _SFR_IO_ADDR(EECR), EEMPE
	sbi _SFR_IO_ADDR(EECR), EEPE
	ldi r24, 1
	ret
// coverage: unreachable block: simavr finishes EEPROM writes at once.
21:
	ldi r24, 0
	ret
	.endfunc
