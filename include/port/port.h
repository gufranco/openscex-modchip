// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_PORT_H
#define PSCU_PORT_H

#include <stdint.h>

// Every hardware primitive the logic and engine layers are allowed to call.
// All of these are implemented in src/port.S; no C file touches a register
// directly. Reads return the masked pin bit (nonzero means high); the two DATA
// calls give an active low or a high-Z release; the two delays busy-wait and
// kick the watchdog; mirror_wfck drives DATA as a copy of the WFCK carrier.
void pscu_port_init(void);

void pscu_port_watchdog_reset(void);

uint8_t pscu_port_read_sqck(void);

// Clock in `count` SUBQ bytes, least significant bit first, sampling on each
// SQCK rising edge. Returns nonzero for a whole frame and zero as soon as any
// edge wait times out after 30 ms. The caller waits for the inter-frame gap
// first, so the first falling edge starts the frame.
uint8_t pscu_port_capture_frame(uint8_t *frame, uint8_t count);

uint8_t pscu_port_read_wfck(void);

void pscu_port_data_drive_low(void);

void pscu_port_data_release(void);

// Hold the WFCK gate low for a legacy-board string, and release it to high-Z.
// Only gate boards use these; on a carrier board WFCK stays an input.
void pscu_port_gate_drive_low(void);

void pscu_port_gate_release(void);

void pscu_port_led_on(void);

void pscu_port_led_off(void);

void pscu_port_delay_ms(uint16_t milliseconds);

void pscu_port_data_mirror_wfck_ms(uint16_t milliseconds);

// One modern-board (WFCK carrier) injection bit cell. hold_low holds DATA, which
// the caller has already driven low, for one bit; mirror drives DATA as a copy
// of the WFCK carrier for one bit. In the adaptive build (default) both time the
// cell by counting WFCK periods, so the modern bit cell is locked to the console
// clock and immune to the MCU RC oscillator drifting; in the fixed build both
// fall back to the compile-time millisecond delay. Legacy boards never call
// these, because a static WFCK has no period to count.
void pscu_port_bit_hold_low(void);

void pscu_port_bit_mirror(void);

// Timer1 ticks, one per 16384 clocks, wrapping at 256: the time base for the
// LED and the oscillator trim, read without waiting.
uint8_t pscu_port_ticks(void);

// Nonzero when the last reset came from the watchdog; clears the reset flags.
uint8_t pscu_port_reset_was_watchdog(void);

// The 10-bit ADC reading of the 1.1 V bandgap against VCC, 1024 x 1.1 V / VCC,
// or 0 if the conversion did not finish in time.
uint16_t pscu_port_supply_raw(void);

// The oscillator calibration register: the factory value at boot, written one
// step at a time to trim the internal 8 MHz oscillator.
uint8_t pscu_port_osccal_read(void);

void pscu_port_osccal_write(uint8_t value);

// One EEPROM byte, for the calibration record. Each call first waits, bounded at
// 10 ms, for any earlier write to finish. A write starts the hardware's 3.4 ms
// erase-and-write and returns; the firmware writes only at boot after board
// detection or after a disc's session has resolved, never inside the injection
// window, so the latency never lands on a timing path.
uint8_t pscu_port_eeprom_read(uint8_t address);

void pscu_port_eeprom_write(uint8_t address, uint8_t value);

#endif
