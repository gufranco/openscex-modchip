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

// BIOS-patch primitives, implemented only in the ATtiny84 full build. One reads
// the address line AX; the other counts the given number of AX rising edges and
// then drives the data-bus override DX for the configured, cycle-accurate
// window. They are declared here for every build but referenced only by the
// BIOS patch, so non-patch builds neither link nor call them.
uint8_t pscu_port_bios_ax(void);

void pscu_port_bios_override(uint8_t pulses);

// The second override, driven by the AY address line, for the two oldest
// Japanese models whose BIOS reads the region twice. Built only in two-phase
// BIOS builds, otherwise neither linked nor called.
void pscu_port_bios_override_ay(uint8_t pulses);

#endif
