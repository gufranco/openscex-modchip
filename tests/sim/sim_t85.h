// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef SIM_T85_H
#define SIM_T85_H

#include <stddef.h>
#include <stdint.h>

#include "sim_avr.h"
#include "sim_io.h"

// What simavr's ATtiny85 model lacks and every simulated run of an ATtiny85
// image needs, shared by the console harness and the bench's AVR runner so the
// two cannot drift apart.

// OSCCAL, the oscillator calibration register, at data address 0x51 (Read:
// ATtiny25/45/85 datasheet 2586Q, register summary, 0x31 (0x51)). simavr does not
// load a factory value, so a mid-range one is preset for the firmware to read
// as factory at boot; writes to it do not change simavr's speed.
#define SIM_OSCCAL_ADDR 0x51U
// Timer1 on the ATtiny85: TCCR1 at 0x50 and TCNT1 at 0x4F (Read: ATtiny25/45/85
// datasheet 2586Q, register summary). simavr's ATtiny85 model does not run this
// timer at any prescaler (measured: TCNT1 held one value through 100000 cycles
// for every CS setting), so TCNT1 reads are answered from the cycle count and
// the prescaler TCCR1 selects, as the datasheet describes for the synchronous
// clocking mode: CK divided by 2^(CS - 1).
#define SIM_TCCR1_ADDR 0x50U
#define SIM_TCNT1_ADDR 0x4FU
#define SIM_OSCCAL_FACTORY 0x50U

// The free-running count Timer1 would hold: stopped at CS 0, else the cycle
// count divided by the selected prescaler, in the timer's 8 bits.
static inline uint8_t sim_t85_tcnt1_read(struct avr_t *avr, avr_io_addr_t addr, void *param) {
  (void)addr;
  (void)param;
  uint8_t cs = (uint8_t)(avr->data[SIM_TCCR1_ADDR] & 0x0FU);
  uint8_t count = 0U;
  if (cs != 0U) {
    count = (uint8_t)(avr->cycle >> (cs - 1U));
  }
  return count;
}

// Preset OSCCAL and take over the TCNT1 read slot. simavr already hooks that
// address with a timer model that never counts, and refuses a second hook, so
// the read slot is replaced outright.
static inline void sim_t85_install(avr_t *avr) {
  avr->data[SIM_OSCCAL_ADDR] = SIM_OSCCAL_FACTORY;
  avr->io[AVR_DATA_TO_IO(SIM_TCNT1_ADDR)].r.c = sim_t85_tcnt1_read;
  avr->io[AVR_DATA_TO_IO(SIM_TCNT1_ADDR)].r.param = NULL;
}

#endif
