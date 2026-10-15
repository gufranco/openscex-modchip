// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <string.h>

#include "avr_ioport.h"
#include "harness.h"
#include "scenarios.h"
#include "sim_cycle_timers.h"

// Disc-handling scenarios: multi-disc swaps, disc presence without a lid wire,
// and every indicator stage from the boot light to the live fault codes.

// A multi-disc game. The first program-area frame stops the burst; a TOC
// re-read, as the anti-mod v2 check does, is served again under the same cap;
// a swap re-arms the chip for the next disc.
#define MD_TOC_FRAMES 40
#define MD_PLAY_FRAMES 30
#define MD_REREAD_FRAMES 520
// Frames to let a string in flight finish before counting resumes: a string
// lasts 177 ms, about 15 frames.
#define MD_SETTLE_FRAMES 20
// The per-arming cap, PSCU_STEALTH_STRINGS in include/pscu/config.h.
#define MD_CAP 16
static void md_check(const target_t *t, int ok, const char *what, int strings) {
  char label[112];
  (void)snprintf(label, sizeof(label), "multi-disc %s: %s (%d strings)", t->mcu, what, strings);
  check(ok, label);
}

void scenario_multidisc(const target_t *t, const char *elf, uint32_t freq) {
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, (uint32_t)(freq / (2UL * WFCK_HZ)) };
  g_indicator_seen = 0;
  g_indicator_cycle = 0;
  avr_irq_register_notify(pin_irq(avr, t, t->indicator), on_indicator, avr);
  boot_quiet(avr, t, 0, &ctx);

  const uint8_t toc[SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  const uint8_t play[SUBQ_FRAME_BYTES] = { 0x41U, 0x01U, 0x01U, 0x00U, 0x02U, 0,
                                           0,     0,     0x02U, 0,     0,     0 };

  int disc1 = strings_while(avr, t, toc, MD_TOC_FRAMES);
  md_check(t, disc1 >= 1, "disc 1 is injected", disc1);

  // A string already under way began before play and only finishes in it, so
  // play is judged by the strings that begin on DATA once it starts: none.
  int bursts_before_play = g_data_bursts;
  clock_frames(avr, t, play, MD_SETTLE_FRAMES);
  int after_accept = strings_while(avr, t, play, MD_PLAY_FRAMES);
  int begun = g_data_bursts - bursts_before_play;
  md_check(t, begun == 0, "no string starts once the program area is read", begun);
  md_check(t, after_accept == 0, "no string once the program area is read", after_accept);

  int reread = strings_while(avr, t, toc, MD_REREAD_FRAMES);
  md_check(
      t, reread == MD_CAP, "a long TOC re-read of the same disc gets the whole cap again", reread);
  int bursts_before_replay = g_data_bursts;
  clock_frames(avr, t, play, MD_SETTLE_FRAMES);
  int replay = strings_while(avr, t, play, MD_PLAY_FRAMES);
  int rebegun = g_data_bursts - bursts_before_replay;
  md_check(t, rebegun == 0, "no string starts in play after the re-read", rebegun);
  md_check(t, replay == 0, "play after the re-read stays silent", replay);

  swap_disc(avr);
  int disc2 = strings_while(avr, t, toc, MD_TOC_FRAMES);
  md_check(t, disc2 >= 1, "disc 2 after a swap is injected", disc2);
  clock_frames(avr, t, play, MD_PLAY_FRAMES);

  swap_disc(avr);
  int disc3 = strings_while(avr, t, toc, MD_TOC_FRAMES);
  md_check(t, disc3 >= 1, "disc 3 after another swap is injected", disc3);
  clock_frames(avr, t, play, MD_SETTLE_FRAMES);
  // Rule 5 seen from the console: across three discs, a re-read and play, DATA
  // was driven only inside whole strings, and the debug lamp was lit as each
  // one began. Lamp pulses are not counted as strings: one can run into a result
  // code and outlast any string.
  md_check(t, data_strays(avr) == 0, "DATA is driven only inside whole strings", g_strings);
  md_check(t,
           (g_data_bursts > 0) && (g_lit_bursts == g_data_bursts),
           "the lamp is lit as each string starts on DATA",
           g_strings);
}

// Without a lid wire, a swap is a stretch with no valid SUBQ frame. A pause
// under the 1.5 s bound keeps the disc and its session.
#define SHORT_PAUSE_MS 800U

void scenario_disc_presence(const target_t *t, const char *elf, uint32_t freq) {
  const uint8_t toc[SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  const uint8_t play[SUBQ_FRAME_BYTES] = { 0x41U, 0x01U, 0x01U, 0x00U, 0x02U, 0,
                                           0,     0,     0x02U, 0,     0,     0 };
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, 0U };
  avr_irq_register_notify(pin_irq(avr, t, t->indicator), on_indicator, avr);
  boot_quiet(avr, t, 0, &ctx);
  clock_until_inject(avr, t, toc);
  clock_frames(avr, t, play, MD_SETTLE_FRAMES + MD_PLAY_FRAMES);
  run_cycles(avr, ms_cycles(SHORT_PAUSE_MS));
  int reread = strings_while(avr, t, toc, MD_TOC_FRAMES);
  char label[96];
  (void)snprintf(label,
                 sizeof(label),
                 "disc %s: a lead-in read after a short pause is served (%d strings)",
                 t->mcu,
                 reread);
  check((g_indicator_seen != 0) && (reread >= 1), label);

  // After a swap the next disc's first frames are not lead-in; with the counter
  // emptied they must not open the window.
  const uint8_t lead_out[SUBQ_FRAME_BYTES] = { 0x41U, 0xAAU, 0x01U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  clock_frames(avr, t, toc, MD_REREAD_FRAMES);
  swap_disc(avr);
  int spin_up = strings_while(avr, t, lead_out, MD_TOC_FRAMES);
  (void)snprintf(label,
                 sizeof(label),
                 "disc %s: a new disc's spin-up opens no window (%d strings)",
                 t->mcu,
                 spin_up);
  check(spin_up == 0, label);
}

// The boot light: on at power-on, through the settle and detect wait, and off
// before the first frame is read.
static uint64_t g_boot_on = 0;
static uint64_t g_boot_off = 0;

static void on_boot_led(struct avr_irq_t *irq, uint32_t value, void *param) {
  avr_t *avr = param;
  (void)irq;
  if ((value != 0U) && (g_boot_on == 0U)) {
    g_boot_on = avr->cycle;
  }
  if ((value == 0U) && (g_boot_on != 0U) && (g_boot_off == 0U)) {
    g_boot_off = avr->cycle;
  }
}

void scenario_boot_light(const target_t *t, const char *elf, uint32_t freq) {
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, 0U };
  g_boot_on = 0U;
  g_boot_off = 0U;
  // The boot light comes on during the port init build_avr already ran, so
  // read the indicator level now.
  avr_ioport_state_t state;
  (void)avr_ioctl(avr, AVR_IOCTL_IOPORT_GETSTATE((uint32_t)t->port), &state);
  if (((state.port >> t->indicator) & 1U) != 0U) {
    g_boot_on = avr->cycle;
  }
  avr_irq_register_notify(pin_irq(avr, t, t->indicator), on_boot_led, avr);
  boot_quiet(avr, t, 0, &ctx);

  uint64_t lit = g_boot_off - g_boot_on;
  uint64_t settle = (uint64_t)freq * 3U / 10U;
  char label[96];
  (void)snprintf(label, sizeof(label), "boot light %s: lights at power-on", t->mcu);
  check(g_boot_on != 0U, label);
  (void)snprintf(label, sizeof(label), "boot light %s: stays on through the 300 ms settle", t->mcu);
  check((g_boot_off != 0U) && (lit >= settle), label);
  (void)snprintf(label, sizeof(label), "boot light %s: off before detection ends", t->mcu);
  check((g_boot_off != 0U) && (g_boot_off < DETECT_CYCLES), label);
}

// After the boot light, short blinks name the board the chip detected: one for a
// static gate (PU-18, PU-20), two for a WFCK carrier (PU-22 and later).
void board_blinks_case(const target_t *t, const char *elf, uint32_t freq, int modern) {
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, (uint32_t)(freq / (2UL * WFCK_HZ)) };
  avr_irq_register_notify(pin_irq(avr, t, t->indicator), on_indicator, avr);
  boot_quiet(avr, t, modern, &ctx);
  int blinks = pulse_group(0U, 250U, 350U);
  char label[96];
  (void)snprintf(label,
                 sizeof(label),
                 "boot %s: %s board shows %d short blinks",
                 t->mcu,
                 (modern != 0) ? "carrier" : "gate",
                 (modern != 0) ? 2 : 1);
  check(blinks == ((modern != 0) ? 2 : 1), label);
}

// Live faults: no valid SUBQ frame for 5 s is code 3; valid frames without a
// region check for 20 s, here an audio CD, are code 4.
#define NO_CHECK_FRAMES 2200

void faults_case(const target_t *t, const char *elf, uint32_t freq) {
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, 0U };
  avr_irq_register_notify(pin_irq(avr, t, t->indicator), on_indicator, avr);
  boot_quiet(avr, t, 0, &ctx);
  run_cycles(avr, ms_cycles(10000U));
  char label[96];
  (void)snprintf(label, sizeof(label), "indicator %s: no SUBQ shows code 3", t->mcu);
  check(code_after(DETECT_CYCLES) == 3, label);

  avr_t *quiet = build_avr(t, elf, freq);
  avr_irq_register_notify(pin_irq(quiet, t, t->indicator), on_indicator, quiet);
  boot_quiet(quiet, t, 0, &ctx);
  const uint8_t audio_lead_in[SUBQ_FRAME_BYTES] = {
    0x01U, 0x00U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0
  };
  clock_frames(quiet, t, audio_lead_in, NO_CHECK_FRAMES);
  (void)snprintf(label, sizeof(label), "indicator %s: an audio CD shows code 4", t->mcu);
  check(code_after(DETECT_CYCLES) == 4, label);
}
