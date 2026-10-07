// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <string.h>

#include "avr_eeprom.h"
#include "harness.h"
#include "scenarios.h"
#include "sim_cycle_timers.h"

// Calibration scenarios: what the chip learns per console, across boots.

// Per-console calibration, read and seeded through simavr's EEPROM. The record
// layout mirrors include/pscu/calib.h: magic, board, cap, trigger, frozen, check,
// where check folds the five bytes into a fixed seed. Seeding happens right
// after build_avr, long before the firmware reads the record after detection.
#define CALIB_BYTES 6
#define CALIB_MAGIC 0xC5U
#define CALIB_SEED 0x5AU
#define CALIB_TRIGGER 10U
#define CALIB_TRIGGER_STEP 2U
#define CALIB_TRIGGER_MAX 30U
#define CALIB_CAP_MAX 16U
#define CALIB_CAP_MARGIN 4U
#define CALIB_SETTLE_FRAMES 120
#define CALIB_LONG_TOC_FRAMES 700
#define CALIB_REPLAY_MS 10000U

static void read_calib(avr_t *avr, uint8_t *raw) {
  avr_eeprom_desc_t desc = { .ee = raw, .offset = 0, .size = CALIB_BYTES };
  (void)avr_ioctl(avr, AVR_IOCTL_EEPROM_GET, &desc);
}

static void seed_calib(avr_t *avr, uint8_t board, uint8_t cap, uint8_t trigger, uint8_t frozen) {
  uint8_t raw[CALIB_BYTES] = { CALIB_MAGIC, board, cap, trigger, frozen, 0U };
  raw[5] = (uint8_t)(CALIB_SEED ^ raw[0] ^ raw[1] ^ raw[2] ^ raw[3] ^ raw[4]);
  avr_eeprom_desc_t desc = { .ee = raw, .offset = 0, .size = CALIB_BYTES };
  (void)avr_ioctl(avr, AVR_IOCTL_EEPROM_SET, &desc);
}

static int calib_valid(const uint8_t *raw) {
  uint8_t check_byte = (uint8_t)(CALIB_SEED ^ raw[0] ^ raw[1] ^ raw[2] ^ raw[3] ^ raw[4]);
  return (raw[0] == CALIB_MAGIC) && (raw[5] == check_byte);
}

static void calib_check(const target_t *t, int ok, const char *what) {
  char label[112];
  (void)snprintf(label, sizeof(label), "calib %s: %s", t->mcu, what);
  check(ok, label);
}

// Disc 1 is accepted after a few strings, so the chip stores the cap as that
// count plus the margin and probes one step later. Disc 2 never reaches the
// program area: the chip sends exactly the learned cap, not the fixed 16, and
// the refusal stores the full cap again and steps the start back, frozen.
void scenario_calib_cap(const target_t *t, const char *elf, uint32_t freq) {
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, 0U };
  avr_irq_register_notify(pin_irq(avr, t, t->led), on_led, avr);
  boot_quiet(avr, t, 0, &ctx);
  const uint8_t toc[SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  const uint8_t play[SUBQ_FRAME_BYTES] = { 0x41U, 0x01U, 0x01U, 0x00U, 0x02U, 0,
                                           0,     0,     0x02U, 0,     0,     0 };

  clock_until_inject(avr, t, toc);
  clock_frames(avr, t, play, CALIB_SETTLE_FRAMES);
  int disc1 = g_strings;
  uint8_t raw[CALIB_BYTES] = { 0 };
  read_calib(avr, raw);
  calib_check(
      t, calib_valid(raw) && (raw[1] == 0U), "a fresh chip stores a valid record and its board");
  calib_check(t,
              (disc1 >= 1) && (raw[2] == (uint8_t)(disc1 + (int)CALIB_CAP_MARGIN)),
              "an accepted disc stores its string count plus the margin as the cap");
  calib_check(t,
              (raw[3] == (CALIB_TRIGGER + CALIB_TRIGGER_STEP)) && (raw[4] == 0U),
              "an accepted disc probes one step later");

  swap_disc(avr);
  int disc2 = strings_while(avr, t, toc, CALIB_LONG_TOC_FRAMES);
  calib_check(t, disc2 == (int)raw[2], "the next disc gets at most the learned cap");
  read_calib(avr, raw);
  calib_check(t,
              (raw[2] == CALIB_CAP_MAX) && (raw[3] == CALIB_TRIGGER) && (raw[4] == 1U),
              "a refusal restores the full cap and steps the start back, frozen");
}

// A record stored on a carrier board meets a gate board: the chip moved or its
// WFCK wire is intermittent. The boot replays code 7 and the learned values
// restart from the defaults under the new board.
void scenario_calib_board(const target_t *t, const char *elf, uint32_t freq) {
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, 0U };
  seed_calib(avr, 1U, 7U, 14U, 1U);
  avr_irq_register_notify(pin_irq(avr, t, t->led), on_led, avr);
  boot_quiet(avr, t, 0, &ctx);
  run_cycles(avr, ms_cycles(CALIB_REPLAY_MS));
  calib_check(t, code_after(0U) == 7, "a board change replays code 7 at boot");
  uint8_t raw[CALIB_BYTES] = { 0 };
  read_calib(avr, raw);
  calib_check(t,
              calib_valid(raw) && (raw[1] == 0U) && (raw[2] == CALIB_CAP_MAX) &&
                  (raw[3] == CALIB_TRIGGER) && (raw[4] == 0U),
              "a board change restarts the learned values");
}

// A learned start at the bound meets a disc whose lead-in read ends before the
// counter gets there: no string is ever sent. The chip reads it as a missed
// window, shows code 2 and steps the start back, frozen, so the next disc gets
// its string.
#define MISSED_TOC_FRAMES 20
#define MISSED_DECAY_FRAMES 40

void scenario_calib_missed(const target_t *t, const char *elf, uint32_t freq) {
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, 0U };
  seed_calib(avr, 0U, CALIB_CAP_MAX, CALIB_TRIGGER_MAX, 0U);
  avr_irq_register_notify(pin_irq(avr, t, t->led), on_led, avr);
  boot_quiet(avr, t, 0, &ctx);
  const uint8_t toc[SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  const uint8_t silence[SUBQ_FRAME_BYTES] = { 0 };
  uint64_t mark = avr->cycle;
  clock_frames(avr, t, toc, MISSED_TOC_FRAMES);
  clock_frames(avr, t, silence, MISSED_DECAY_FRAMES);
  run_cycles(avr, ms_cycles(4000U));
  calib_check(t, g_strings == 0, "a start later than the lead-in read sends nothing");
  calib_check(t, code_after(mark) == 2, "a missed window shows code 2");
  uint8_t raw[CALIB_BYTES] = { 0 };
  read_calib(avr, raw);
  calib_check(t,
              (raw[3] == (CALIB_TRIGGER_MAX - CALIB_TRIGGER_STEP)) && (raw[4] == 1U),
              "a missed window steps the start back, frozen");
}

// A Japanese build never probes the start point: PsNee warns against a trigger
// above 11 on Japanese models. An accepted disc still teaches the cap, and the
// probe freezes at the default trigger.
void scenario_calib_jp(const target_t *t, const char *elf, uint32_t freq) {
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, 0U };
  avr_irq_register_notify(pin_irq(avr, t, t->led), on_led, avr);
  boot_quiet(avr, t, 0, &ctx);
  const uint8_t toc[SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  const uint8_t play[SUBQ_FRAME_BYTES] = { 0x41U, 0x01U, 0x01U, 0x00U, 0x02U, 0,
                                           0,     0,     0x02U, 0,     0,     0 };
  clock_until_inject(avr, t, toc);
  clock_frames(avr, t, play, CALIB_SETTLE_FRAMES);
  uint8_t raw[CALIB_BYTES] = { 0 };
  read_calib(avr, raw);
  calib_check(t,
              calib_valid(raw) && (raw[2] == (uint8_t)(g_strings + (int)CALIB_CAP_MARGIN)) &&
                  (raw[3] == CALIB_TRIGGER) && (raw[4] == 1U),
              "a Japanese build learns the cap but keeps the default start");
}
