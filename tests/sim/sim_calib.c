// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "avr_eeprom.h"
#include "harness.h"
#include "scenarios.h"
#include "sim_cycle_timers.h"

// Calibration scenarios: what the chip learns per console, across boots.

// The EEPROM record, laid out as in include/pscu/calib.h: magic, board,
// trigger, frozen, trim, check.
#define CALIB_BYTES 6
#define CALIB_MAGIC 0xC8U
#define CALIB_SEED 0x5AU
#define CALIB_TRIGGER 10U
#define CALIB_TRIGGER_STEP 2U
#define CALIB_TRIGGER_MAX 30U
#define CALIB_STEALTH_STRINGS 16
#define CALIB_SETTLE_FRAMES 120
#define CALIB_LONG_TOC_FRAMES 700
#define CALIB_REPLAY_MS 10000U

static void read_calib(avr_t *avr, uint8_t *raw) {
  avr_eeprom_desc_t desc = { .ee = raw, .offset = 0, .size = CALIB_BYTES };
  (void)avr_ioctl(avr, AVR_IOCTL_EEPROM_GET, &desc);
}

static uint8_t calib_check_byte(const uint8_t *raw) {
  return (uint8_t)(CALIB_SEED ^ raw[0] ^ raw[1] ^ raw[2] ^ raw[3] ^ raw[4]);
}

static void seed_calib(uint8_t board, uint8_t trigger, uint8_t frozen, int8_t trim) {
  uint8_t raw[CALIB_BYTES] = { CALIB_MAGIC, board, trigger, frozen, (uint8_t)trim, 0U };
  raw[5] = calib_check_byte(raw);
  sim_seed_eeprom(raw, CALIB_BYTES);
}

static int calib_valid(const uint8_t *raw) {
  return (raw[0] == CALIB_MAGIC) && (raw[5] == calib_check_byte(raw));
}

static void calib_check(const target_t *t, int ok, const char *what) {
  char label[112];
  (void)snprintf(label, sizeof(label), "calib %s: %s", t->mcu, what);
  check(ok, label);
}

// Disc 1 is accepted, so the start moves one step later. Disc 2 is refused: it
// still gets the whole stealth cap, and the start steps back, frozen.
void scenario_calib_cap(const target_t *t, const char *elf, uint32_t freq) {
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, 0U };
  avr_irq_register_notify(pin_irq(avr, t, t->indicator), on_indicator, avr);
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
              (disc1 >= 1) && (raw[2] == (CALIB_TRIGGER + CALIB_TRIGGER_STEP)) && (raw[3] == 0U),
              "an accepted disc probes one step later");

  swap_disc(avr);
  int disc2 = strings_while(avr, t, toc, CALIB_LONG_TOC_FRAMES);
  calib_check(t, disc2 == CALIB_STEALTH_STRINGS, "a refused disc gets the whole stealth cap");
  read_calib(avr, raw);
  calib_check(
      t, (raw[2] == CALIB_TRIGGER) && (raw[3] == 1U), "a refusal steps the start back, frozen");
}

// A record from a carrier board meets a gate board: the boot shows code 6 and
// the learned values restart from the defaults.
void scenario_calib_board(const target_t *t, const char *elf, uint32_t freq) {
  seed_calib(1U, 14U, 1U, 0);
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, 0U };
  avr_irq_register_notify(pin_irq(avr, t, t->indicator), on_indicator, avr);
  boot_quiet(avr, t, 0, &ctx);
  run_cycles(avr, ms_cycles(CALIB_REPLAY_MS));
  calib_check(t, code_after(0U) == 6, "a board change replays code 6 at boot");
  uint8_t raw[CALIB_BYTES] = { 0 };
  read_calib(avr, raw);
  calib_check(t,
              calib_valid(raw) && (raw[1] == 0U) && (raw[2] == CALIB_TRIGGER) && (raw[3] == 0U),
              "a board change restarts the learned values");
}

// The lead-in read ends before the counter reaches the learned start: code 2,
// and the start steps back, frozen.
#define MISSED_TOC_FRAMES 20
#define MISSED_DECAY_FRAMES 40

void scenario_calib_missed(const target_t *t, const char *elf, uint32_t freq) {
  seed_calib(0U, CALIB_TRIGGER_MAX, 0U, 0);
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, 0U };
  avr_irq_register_notify(pin_irq(avr, t, t->indicator), on_indicator, avr);
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
              (raw[2] == (CALIB_TRIGGER_MAX - CALIB_TRIGGER_STEP)) && (raw[3] == 1U),
              "a missed window steps the start back, frozen");
}

// A Japanese build never probes the start point; PsNee warns against a trigger
// above 11 on Japanese models.
void scenario_calib_jp(const target_t *t, const char *elf, uint32_t freq) {
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, 0U };
  avr_irq_register_notify(pin_irq(avr, t, t->indicator), on_indicator, avr);
  boot_quiet(avr, t, 0, &ctx);
  const uint8_t toc[SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  const uint8_t play[SUBQ_FRAME_BYTES] = { 0x41U, 0x01U, 0x01U, 0x00U, 0x02U, 0,
                                           0,     0,     0x02U, 0,     0,     0 };
  clock_until_inject(avr, t, toc);
  clock_frames(avr, t, play, CALIB_SETTLE_FRAMES);
  uint8_t raw[CALIB_BYTES] = { 0 };
  read_calib(avr, raw);
  calib_check(t,
              calib_valid(raw) && (raw[2] == CALIB_TRIGGER) && (raw[3] == 1U),
              "a Japanese build keeps the default start");
}

// The oscillator trim. Running the sim 5 percent faster than the 8 MHz the
// firmware assumes models a fast RC. simavr ignores OSCCAL writes, so this
// checks the step direction, the stored value and the boot replay.
#define TRIM_TOC_FRAMES 200
// Silent frames to drain the counter so the window closes and the trim is
// stored.
#define TRIM_DECAY_FRAMES 260

static uint8_t osccal(avr_t *avr) {
  return avr->data[SIM_OSCCAL_ADDR];
}

// Every OSCCAL write must change the clock by at most 2 percent (datasheet
// 2586Q, OSCCAL), so the firmware steps one notch per write. Writes in one
// pass sit a few dozen cycles apart; passes sit about 100000 cycles apart.
#define TRIM_SAME_PASS_CYCLES 1000U

typedef struct {
  unsigned writes;
  unsigned largest_step;
  unsigned run;
  unsigned longest_run;
  avr_cycle_count_t last_cycle;
} osccal_watch_t;

// simavr hands a hooked write to this callback instead of storing it.
static void osccal_write(struct avr_t *avr, avr_io_addr_t addr, uint8_t v, void *param) {
  osccal_watch_t *watch = (osccal_watch_t *)param;
  int change = (int)v - (int)avr->data[addr];
  unsigned step = (unsigned)((change < 0) ? -change : change);
  watch->largest_step = (step > watch->largest_step) ? step : watch->largest_step;
  bool same_pass =
      (watch->writes > 0U) && ((avr->cycle - watch->last_cycle) < TRIM_SAME_PASS_CYCLES);
  watch->run = same_pass ? (watch->run + 1U) : 1U;
  watch->longest_run = (watch->run > watch->longest_run) ? watch->run : watch->longest_run;
  watch->writes++;
  watch->last_cycle = avr->cycle;
  avr->data[addr] = v;
}

static void watch_osccal(avr_t *avr, osccal_watch_t *watch) {
  avr->io[AVR_DATA_TO_IO(SIM_OSCCAL_ADDR)].w.c = osccal_write;
  avr->io[AVR_DATA_TO_IO(SIM_OSCCAL_ADDR)].w.param = watch;
}

static void trim_case(const target_t *t, const char *elf, uint32_t freq, int percent) {
  uint32_t actual = (uint32_t)(((uint64_t)freq * (uint64_t)(100 + percent)) / 100U);
  avr_t *avr = build_avr(t, elf, actual);
  osccal_watch_t watch = { 0U, 0U, 0U, 0U, 0U };
  watch_osccal(avr, &watch);
  wfck_ctx_t ctx = { NULL, 1U, 0U };
  boot_quiet(avr, t, 0, &ctx);
  const uint8_t toc[SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  const uint8_t silence[SUBQ_FRAME_BYTES] = { 0 };
  const uint8_t play[SUBQ_FRAME_BYTES] = { 0x41U, 0x01U, 0x01U, 0x00U, 0x02U, 0,
                                           0,     0,     0x02U, 0,     0,     0 };
  // Frames at the disc's real 75 Hz: faster frames would read as a slow RC.
  g_frame_period_ns = SIM_SECTOR_NS;
  clock_until_inject(avr, t, toc);
  clock_frames(avr, t, play, CALIB_SETTLE_FRAMES);
  clock_frames(avr, t, toc, TRIM_TOC_FRAMES);
  clock_frames(avr, t, silence, TRIM_DECAY_FRAMES);
  g_frame_period_ns = 0U;
  uint8_t raw[CALIB_BYTES] = { 0 };
  read_calib(avr, raw);
  int8_t stored = (int8_t)raw[4];
  int8_t moved = (int8_t)((int)osccal(avr) - (int)SIM_OSCCAL_FACTORY);

  char what[96];
  if (percent > 0) {
    (void)snprintf(what, sizeof(what), "a fast RC steps OSCCAL down and stores it (%d)", moved);
    calib_check(t, calib_valid(raw) && (moved < 0) && (stored == moved), what);
  } else if (percent < 0) {
    (void)snprintf(what, sizeof(what), "a slow RC steps OSCCAL up and stores it (%d)", moved);
    calib_check(t, calib_valid(raw) && (moved > 0) && (stored == moved), what);
  } else {
    calib_check(t, (moved == 0) && (stored == 0), "a nominal RC leaves OSCCAL alone");
  }
  // The sim's speed never follows OSCCAL, so every batch asks for the largest
  // step, and each must still land one notch per write.
  if (percent != 0) {
    unsigned walked = (unsigned)((moved < 0) ? -moved : moved);
    (void)snprintf(what,
                   sizeof(what),
                   "every OSCCAL write moves one notch, %u in one pass",
                   watch.longest_run);
    calib_check(t,
                (watch.largest_step == 1U) && (watch.writes == walked) && (watch.longest_run > 1U),
                what);
  }
}

void scenario_trim(const target_t *t, const char *elf, uint32_t freq) {
  trim_case(t, elf, freq, 5);
  trim_case(t, elf, freq, -5);
  trim_case(t, elf, freq, 0);

  seed_calib(0U, CALIB_TRIGGER, 0U, -3);
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, 0U };
  boot_quiet(avr, t, 0, &ctx);
  calib_check(
      t, osccal(avr) == (uint8_t)(SIM_OSCCAL_FACTORY - 3U), "the stored trim is applied at boot");
}

// The supply guard. simavr's bandgap reads 225 at 5 V, 341 at 3.3 V and 401 at
// 2.8 V. At 2.6 V no string goes out, the indicator shows code 7 and the record is
// unchanged; back at 3.3 V the next lead-in is injected.
#define LOW_SUPPLY_MV 2600U
#define GOOD_SUPPLY_MV 3300U
#define SUPPLY_TRIGGER 12U
#define SUPPLY_TOC_FRAMES 60
#define SUPPLY_WATCH_MS 20000U

static int longest_code_after(uint64_t from) {
  int longest = 0;
  for (int i = 0; i < g_pulses; i++) {
    int group = (g_pulse_rise[i] >= from) ? pulse_group(g_pulse_rise[i], 600U, 800U) : 0;
    longest = (group > longest) ? group : longest;
  }
  return longest;
}

static void set_supply(avr_t *avr, uint32_t mv) {
  avr->vcc = mv;
  avr->avcc = mv;
}

void scenario_supply(const target_t *t, const char *elf, uint32_t freq) {
  const uint8_t toc[SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  const uint8_t silence[SUBQ_FRAME_BYTES] = { 0 };
  seed_calib(0U, SUPPLY_TRIGGER, 0U, 0);
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, 0U };
  set_supply(avr, LOW_SUPPLY_MV);
  avr_irq_register_notify(pin_irq(avr, t, t->indicator), on_indicator, avr);
  boot_quiet(avr, t, 0, &ctx);
  uint64_t mark = avr->cycle;
  clock_frames(avr, t, toc, SUPPLY_TOC_FRAMES);
  clock_frames(avr, t, silence, MISSED_DECAY_FRAMES);
  run_cycles(avr, ms_cycles(SUPPLY_WATCH_MS));
  int low = g_strings;
  int code = longest_code_after(mark);
  uint8_t raw[CALIB_BYTES] = { 0 };
  read_calib(avr, raw);
  set_supply(avr, GOOD_SUPPLY_MV);
  int good = strings_while(avr, t, toc, SUPPLY_TOC_FRAMES);

  char what[64];
  (void)snprintf(what, sizeof(what), "no string at 2.6 V (%d strings)", low);
  calib_check(t, low == 0, what);
  (void)snprintf(what, sizeof(what), "a low supply shows code 7 (%d)", code);
  calib_check(t, code == 7, what);
  calib_check(t,
              calib_valid(raw) && (raw[2] == SUPPLY_TRIGGER) && (raw[3] == 0U),
              "a window the supply kept shut teaches nothing");
  calib_check(t, good >= 1, "injects once the supply is back at 3.3 V");
}

// A brief supply dip inside a burst holds the burst rather than ending it, so
// the disc gets the stealth cap once, not twice.
#define DIP_BEFORE_FRAMES 40
#define DIP_FRAMES 5
#define DIP_AFTER_FRAMES 400

void scenario_supply_dip(const target_t *t, const char *elf, uint32_t freq) {
  const uint8_t toc[SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  seed_calib(0U, CALIB_TRIGGER, 1U, 0);
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, 0U };
  avr_irq_register_notify(pin_irq(avr, t, t->indicator), on_indicator, avr);
  boot_quiet(avr, t, 0, &ctx);
  clock_until_inject(avr, t, toc);
  clock_frames(avr, t, toc, DIP_BEFORE_FRAMES);
  int before = g_strings;
  set_supply(avr, LOW_SUPPLY_MV);
  clock_frames(avr, t, toc, DIP_FRAMES);
  set_supply(avr, GOOD_SUPPLY_MV);
  clock_frames(avr, t, toc, DIP_AFTER_FRAMES);

  char what[80];
  (void)snprintf(what, sizeof(what), "the dip came mid-burst (%d strings before it)", before);
  calib_check(t, (before >= 1) && (before < CALIB_STEALTH_STRINGS), what);
  (void)snprintf(
      what, sizeof(what), "a supply dip mid-burst keeps the cap (%d strings)", g_strings);
  calib_check(t, g_strings == CALIB_STEALTH_STRINGS, what);
}

// A store during a supply sag is skipped, the classic cause of a corrupt EEPROM
// (Liss V1.4, liss-stealth_v1.4_at90s1200_all.asm:121-122). The record must
// keep the seeded start.
void scenario_store_low_supply(const target_t *t, const char *elf, uint32_t freq) {
  seed_calib(0U, CALIB_TRIGGER, 0U, 0);
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, 0U };
  avr_irq_register_notify(pin_irq(avr, t, t->indicator), on_indicator, avr);
  boot_quiet(avr, t, 0, &ctx);
  const uint8_t toc[SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  const uint8_t play[SUBQ_FRAME_BYTES] = { 0x41U, 0x01U, 0x01U, 0x00U, 0x02U, 0,
                                           0,     0,     0x02U, 0,     0,     0 };
  clock_until_inject(avr, t, toc);
  set_supply(avr, LOW_SUPPLY_MV);
  clock_frames(avr, t, play, CALIB_SETTLE_FRAMES);
  uint8_t raw[CALIB_BYTES] = { 0 };
  read_calib(avr, raw);

  calib_check(t, g_strings >= 1, "the disc got its string before the supply sagged");
  calib_check(t,
              calib_valid(raw) && (raw[2] == CALIB_TRIGGER) && (raw[3] == 0U),
              "a session resolved at 2.6 V writes nothing to EEPROM");
}

// A torn record, a new trigger and trim under the old check byte, is ignored:
// no trim, defaults, and a valid record written back at boot.
#define TORN_OLD_TRIGGER 14U
#define TORN_NEW_TRIGGER 20U
#define TORN_TRIM (-3)

void scenario_torn_record(const target_t *t, const char *elf, uint32_t freq) {
  uint8_t torn[CALIB_BYTES] = { CALIB_MAGIC, 0U, TORN_OLD_TRIGGER, 0U, (uint8_t)TORN_TRIM, 0U };
  torn[5] = calib_check_byte(torn);
  torn[2] = TORN_NEW_TRIGGER;
  sim_seed_eeprom(torn, CALIB_BYTES);
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, 0U };
  boot_quiet(avr, t, 0, &ctx);
  uint8_t raw[CALIB_BYTES] = { 0 };
  read_calib(avr, raw);

  calib_check(t, osccal(avr) == SIM_OSCCAL_FACTORY, "a torn record applies no trim");
  calib_check(t,
              calib_valid(raw) && (raw[2] == CALIB_TRIGGER) && (raw[3] == 0U) && (raw[4] == 0U),
              "a torn record is replaced by the defaults at boot");
}
