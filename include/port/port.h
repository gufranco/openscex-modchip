// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_PORT_H
#define PSCU_PORT_H

#include <stdint.h>

// Hardware primitives, implemented in src/port.S. Reads return the masked pin
// bit (nonzero means high). DATA is driven low or released to high-Z. Delays
// busy-wait and kick the watchdog.
void pscu_port_init(void);

void pscu_port_watchdog_reset(void);

uint8_t pscu_port_read_sqck(void);

// Clock in `count` SUBQ bytes, LSB first, on SQCK rising edges. Returns zero if
// any edge wait times out (30 ms). Call it after the inter-frame gap.
uint8_t pscu_port_capture_frame(uint8_t *frame, uint8_t count);

uint8_t pscu_port_read_wfck(void);

void pscu_port_data_drive_low(void);

void pscu_port_data_release(void);

// Hold the WFCK gate low for a legacy-board string, and release it to high-Z.
// Never used on a carrier board.
void pscu_port_gate_drive_low(void);

void pscu_port_gate_release(void);

void pscu_port_led_on(void);

void pscu_port_led_off(void);

void pscu_port_delay_ms(uint16_t milliseconds);

// One WFCK-carrier bit cell: hold_low keeps DATA low, mirror copies WFCK onto
// DATA. Both count WFCK periods, so the cell follows the console clock, not the
// RC oscillator.
void pscu_port_bit_hold_low(void);

void pscu_port_bit_mirror(void);

// Timer1 ticks, one per 16384 clocks, wrapping at 256.
uint8_t pscu_port_ticks(void);

// Nonzero when the last reset came from the watchdog; clears the reset flags.
uint8_t pscu_port_reset_was_watchdog(void);

// ADC reading of the 1.1 V bandgap against VCC, or 0 on timeout.
uint16_t pscu_port_supply_raw(void);

// OSCCAL, written one step at a time.
uint8_t pscu_port_osccal_read(void);

void pscu_port_osccal_write(uint8_t value);

// One EEPROM byte. Each call first waits up to 10 ms for a pending write; a
// write takes 3.4 ms (datasheet) and is never issued inside the injection
// window.
uint8_t pscu_port_eeprom_read(uint8_t address);

void pscu_port_eeprom_write(uint8_t address, uint8_t value);

#endif
