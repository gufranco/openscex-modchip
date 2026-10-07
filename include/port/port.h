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

// Timer1 ticks, one per 1024 console clocks, wrapping at 65536: the LED's time
// base, read without waiting.
uint16_t pscu_port_ticks(void);

// Nonzero when the last reset came from the watchdog; clears the reset flags.
uint8_t pscu_port_reset_was_watchdog(void);

// The lid line: nonzero while the lid is open. A swap is seen here directly, so
// the run loop re-arms injection for the next disc on the close, and the
// injection path stops a string the moment the lid opens.
uint8_t pscu_port_read_lid(void);

#endif
