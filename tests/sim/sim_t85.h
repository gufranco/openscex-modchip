// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef SIM_T85_H
#define SIM_T85_H

#include <stddef.h>
#include <stdint.h>

#include "sim_avr.h"
#include "sim_io.h"

// What simavr's ATtiny85 model lacks, shared by the console harness and the
// bench's AVR runner.

// OSCCAL at data address 0x51 (datasheet 2586Q, register summary). simavr has
// no factory value, so a mid-range one is preset; writes do not change speed.
#define SIM_OSCCAL_ADDR 0x51U
// EECR, I/O 0x1C (datasheet 2586Q, register summary): EEPE is bit 1, EEPM1:0
// bits 5:4, whose reset value the datasheet gives as undefined.
#define SIM_EECR_ADDR 0x3CU
#define SIM_EECR_EEPE 0x02U
#define SIM_EECR_EEPM 0x30U
// Timer1: TCCR1 at 0x50, TCNT1 at 0x4F (datasheet 2586Q, register summary).
// simavr does not run it, so TCNT1 reads are computed from the cycle count:
// CK divided by 2^(CS - 1).
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

// Preset OSCCAL and replace the TCNT1 read slot; simavr refuses a second hook
// on that address.
static inline void sim_t85_install(avr_t *avr) {
  avr->data[SIM_OSCCAL_ADDR] = SIM_OSCCAL_FACTORY;
  avr->io[AVR_DATA_TO_IO(SIM_TCNT1_ADDR)].r.c = sim_t85_tcnt1_read;
  avr->io[AVR_DATA_TO_IO(SIM_TCNT1_ADDR)].r.param = NULL;
}

#endif
