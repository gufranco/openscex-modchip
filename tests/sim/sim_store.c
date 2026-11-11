// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include <stdio.h>

#include "avr_eeprom.h"
#include "harness.h"
#include "scenarios.h"
#include "sim_cycle_timers.h"
#include "sim_sag.h"

#define STORE_MAGIC 0xC8U
#define STORE_BOARD_GATE 0U
#define STORE_BOARD_CARRIER 1U
#define STORE_SETTLE_MS 500U
#define STORE_GONE_MS 2000U
#define STORE_SETTLE_FRAMES 120
#define STORE_QUIET_FRAMES 60
#define STORE_PASS_SLACK 1
#define STORE_ADCSRA_ADDR 0x26U
#define STORE_ADSC_BIT 0x40U

typedef struct {
  uint8_t magic;
  uint8_t board;
} store_record_t;

typedef struct {
  int busy;
  int ends;
} store_reads_t;

static store_reads_t g_reads = { 0, 0 };

static const uint8_t STORE_TOC[SUBQ_FRAME_BYTES] = {
  0x41U, 0x00U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0
};
static const uint8_t STORE_PLAY[SUBQ_FRAME_BYTES] = { 0x41U, 0x01U, 0x01U, 0x00U, 0x02U, 0,
                                                      0,     0,     0x02U, 0,     0,     0 };

static store_record_t read_record(avr_t *avr) {
  uint8_t raw[2] = { 0U, 0U };
  avr_eeprom_desc_t desc = { .ee = raw, .offset = 0, .size = 2 };
  (void)avr_ioctl(avr, AVR_IOCTL_EEPROM_GET, &desc);
  store_record_t record = { raw[0], raw[1] };
  return record;
}

static void store_check(const target_t *t, int ok, const char *what) {
  char label[112];
  (void)snprintf(label, sizeof(label), "store %s: %s", t->mcu, what);
  check(ok, label);
}

static void count_supply_reads(avr_t *avr) {
  int busy = (avr->data[STORE_ADCSRA_ADDR] & STORE_ADSC_BIT) != 0U;
  g_reads.ends += ((g_reads.busy != 0) && (busy == 0)) ? 1 : 0;
  g_reads.busy = busy;
}

void scenario_store_carrier_boot(const target_t *t, const char *elf, uint32_t freq) {
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, (uint32_t)(freq / (2UL * WFCK_HZ)) };
  boot_quiet(avr, t, 1, &ctx);
  run_cycles(avr, ms_cycles(STORE_SETTLE_MS));
  store_record_t record = read_record(avr);
  store_check(t,
              (record.magic == STORE_MAGIC) && (record.board == STORE_BOARD_CARRIER),
              "a carrier found at boot is stored as board 1");
}

void scenario_store_upgrade_refused(const target_t *t, const char *elf, uint32_t freq) {
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, (uint32_t)(freq / (2UL * WFCK_HZ)) };
  boot_quiet(avr, t, 0, &ctx);
  ctx.irq = pin_irq(avr, t, t->wfck);
  avr_cycle_timer_register(avr, ctx.half, wfck_tick, &ctx);
  sim_sag_carrier_write();
  clock_until_inject(avr, t, STORE_TOC);
  store_record_t during = read_record(avr);
  run_cycles(avr, ms_cycles(STORE_GONE_MS));
  g_on_step = NULL;
  store_record_t after = read_record(avr);
  store_check(t,
              sim_sag_recovered() && (during.board == STORE_BOARD_GATE),
              "a carrier write refused at 2.6 V leaves the gate record");
  store_check(t,
              (after.magic == STORE_MAGIC) && (after.board == STORE_BOARD_CARRIER),
              "the refused carrier record is written once the supply is back");
}

void scenario_store_once(const target_t *t, const char *elf, uint32_t freq) {
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, 0U };
  boot_quiet(avr, t, 0, &ctx);
  clock_until_inject(avr, t, STORE_TOC);
  clock_frames(avr, t, STORE_PLAY, STORE_SETTLE_FRAMES);
  g_reads = (store_reads_t){ 0, 0 };
  g_on_step = count_supply_reads;
  clock_frames(avr, t, STORE_PLAY, STORE_QUIET_FRAMES);
  g_on_step = NULL;
  char what[96];
  (void)snprintf(what,
                 sizeof(what),
                 "a stored record is not stored again (%d supply reads in %d frames)",
                 g_reads.ends,
                 STORE_QUIET_FRAMES);
  store_check(t, g_reads.ends <= (STORE_QUIET_FRAMES + STORE_PASS_SLACK), what);
}
