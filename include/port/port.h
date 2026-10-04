// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_PORT_H
#define PSCU_PORT_H

#include <stdint.h>

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
