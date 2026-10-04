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

uint8_t pscu_port_read_subq(void);

uint8_t pscu_port_read_wfck(void);

void pscu_port_data_drive_low(void);

void pscu_port_data_release(void);

void pscu_port_led_on(void);

void pscu_port_led_off(void);

void pscu_port_delay_ms(uint16_t milliseconds);

void pscu_port_data_mirror_wfck_ms(uint16_t milliseconds);

#endif
