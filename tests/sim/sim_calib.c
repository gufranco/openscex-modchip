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
// layout mirrors include/pscu/calib.h: magic, board, cap, trigger, frozen, trim,
// check, where check folds the six bytes into a fixed seed. A seed is handed to
// the harness before build_avr, which loads it before the firmware starts.
#define CALIB_BYTES 7
#define CALIB_MAGIC 0xC6U
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

static uint8_t calib_check_byte(const uint8_t *raw) {
  return (uint8_t)(CALIB_SEED ^ raw[0] ^ raw[1] ^ raw[2] ^ raw[3] ^ raw[4] ^ raw[5]);
}

static void seed_calib(uint8_t board, uint8_t cap, uint8_t trigger, uint8_t frozen, int8_t trim) {
  uint8_t raw[CALIB_BYTES] = { CALIB_MAGIC, board, cap, trigger, frozen, (uint8_t)trim, 0U };
  raw[6] = calib_check_byte(raw);
  sim_seed_eeprom(raw, CALIB_BYTES);
}

static int calib_valid(const uint8_t *raw) {
  return (raw[0] == CALIB_MAGIC) && (raw[6] == calib_check_byte(raw));
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
  seed_calib(1U, 7U, 14U, 1U, 0);
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, 0U };
  avr_irq_register_notify(pin_irq(avr, t, t->led), on_led, avr);
  boot_quiet(avr, t, 0, &ctx);
  run_cycles(avr, ms_cycles(CALIB_REPLAY_MS));
  calib_check(t, code_after(0U) == 6, "a board change replays code 6 at boot");
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
  seed_calib(0U, CALIB_CAP_MAX, CALIB_TRIGGER_MAX, 0U, 0);
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, 0U };
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

// The oscillator trim. simavr runs the chip at whatever rate the harness names,
// and the firmware believes 8 MHz, so naming 5 percent more models an RC that
// runs 5 percent fast: 75 Hz frames then span more Timer1 ticks than nominal.
// The disc is accepted first, so the long lead-in reread that follows, like an
// anti-mod check's, gets no string and yields only frame samples; the trim must
// step OSCCAL the right way, and store it once the window has closed. simavr does not change speed
// on an OSCCAL write, so this checks direction, the stored value and the boot replay, not the
// frequency.
#define TRIM_TOC_FRAMES 200
// Enough silent frames for the counter, filled by the lead-in read, to drain
// below the trigger so the window closes and the trim can be stored.
#define TRIM_DECAY_FRAMES 260

static uint8_t osccal(avr_t *avr) {
  return avr->data[SIM_OSCCAL_ADDR];
}

static void trim_case(const target_t *t, const char *elf, uint32_t freq, int percent) {
  uint32_t actual = (uint32_t)(((uint64_t)freq * (uint64_t)(100 + percent)) / 100U);
  avr_t *avr = build_avr(t, elf, actual);
  wfck_ctx_t ctx = { NULL, 1U, 0U };
  boot_quiet(avr, t, 0, &ctx);
  const uint8_t toc[SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  const uint8_t silence[SUBQ_FRAME_BYTES] = { 0 };
  const uint8_t play[SUBQ_FRAME_BYTES] = { 0x41U, 0x01U, 0x01U, 0x00U, 0x02U, 0,
                                           0,     0,     0x02U, 0,     0,     0 };
  clock_until_inject(avr, t, toc);
  clock_frames(avr, t, play, CALIB_SETTLE_FRAMES);
  g_frame_period_ns = SIM_SECTOR_NS;
  clock_frames(avr, t, toc, TRIM_TOC_FRAMES);
  clock_frames(avr, t, silence, TRIM_DECAY_FRAMES);
  g_frame_period_ns = 0U;
  uint8_t raw[CALIB_BYTES] = { 0 };
  read_calib(avr, raw);
  int8_t stored = (int8_t)raw[5];
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
}

void scenario_trim(const target_t *t, const char *elf, uint32_t freq) {
  trim_case(t, elf, freq, 5);
  trim_case(t, elf, freq, -5);
  trim_case(t, elf, freq, 0);

  seed_calib(0U, CALIB_CAP_MAX, CALIB_TRIGGER, 0U, -3);
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, 0U };
  boot_quiet(avr, t, 0, &ctx);
  calib_check(
      t, osccal(avr) == (uint8_t)(SIM_OSCCAL_FACTORY - 3U), "the stored trim is applied at boot");
}

// The supply guard. simavr models the bandgap channel against the supply it is
// given (Verified: a probe read 225 at 5 V, 341 at 3.3 V and 401 at 2.8 V). A
// chip run at 2.6 V, under the limit, must send no string through a whole
// lead-in that reaches its start point, show code 7, and leave the stored
// record as it was: the console did run its check, so the empty window is not a
// miss that should move the start back. Raised to 3.3 V on the same run, the
// next lead-in must be injected, since the guard holds no state of its own.
// One code-7 cycle lasts 9 s and the watch can start anywhere in one, so 20 s
// always holds a whole one.
#define LOW_SUPPLY_MV 2600U
#define GOOD_SUPPLY_MV 3300U
#define SUPPLY_TRIGGER 12U
#define SUPPLY_CAP 8U
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
  seed_calib(0U, SUPPLY_CAP, SUPPLY_TRIGGER, 0U, 0);
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, 0U };
  set_supply(avr, LOW_SUPPLY_MV);
  avr_irq_register_notify(pin_irq(avr, t, t->led), on_led, avr);
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
  calib_check(
      t,
      calib_valid(raw) && (raw[2] == SUPPLY_CAP) && (raw[3] == SUPPLY_TRIGGER) && (raw[4] == 0U),
      "a window the supply kept shut teaches nothing");
  calib_check(t, good >= 1, "injects once the supply is back at 3.3 V");
}

// A single dip of the supply inside a burst must hold the burst, not end it: the
// count and the session carry on, so the disc gets the learned cap and no more.
// The cap is seeded small and frozen so the burst is short and the start point
// stays put. Five frames at 2.6 V land between two strings, then the supply is
// back at 3.3 V for a lead-in far longer than the rest of the burst.
#define DIP_CAP 6U
#define DIP_BEFORE_FRAMES 40
#define DIP_FRAMES 5
#define DIP_AFTER_FRAMES 300

void scenario_supply_dip(const target_t *t, const char *elf, uint32_t freq) {
  const uint8_t toc[SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  seed_calib(0U, DIP_CAP, CALIB_TRIGGER, 1U, 0);
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, 0U };
  avr_irq_register_notify(pin_irq(avr, t, t->led), on_led, avr);
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
  calib_check(t, (before >= 1) && (before < (int)DIP_CAP), what);
  (void)snprintf(
      what, sizeof(what), "a supply dip mid-burst keeps the cap (%d strings)", g_strings);
  calib_check(t, g_strings == (int)DIP_CAP, what);
}
