// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <string.h>

#include "avr_ioport.h"
#include "harness.h"
#include "scenarios.h"
#include "sim_cycle_timers.h"

// Disc-handling scenarios: multi-disc swaps, disc presence without a lid wire,
// and every LED stage from the boot light to the live fault codes.

// A multi-disc game, end to end. Each phase counts region strings by LED rises.
// The first program-area frame must stop the burst. A table-of-contents re-read
// of the same disc after play, as the anti-mod v2 check's ReadTOC does, clears
// the drive's licensed status, so it must be served again, under the same cap,
// and play after it must stay silent. Every swap, seen as SUBQ going silent
// past the disc-gone bound, must re-arm the chip so the next disc is injected.
// The harness clocks a frame about every 12 ms, and the firmware may miss frames
// while an injection blocks, so phase lengths sit well past the trigger.
#define MD_TOC_FRAMES 40
#define MD_PLAY_FRAMES 30
#define MD_REREAD_FRAMES 520
// Settle frames let a string already in flight when the TOC phase ends finish
// before strings are counted again: a string lasts 177 ms, about 15 harness
// frames, and is counted when it ends.
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
  g_led_seen = 0;
  g_led_cycle = 0;
  avr_irq_register_notify(pin_irq(avr, t, t->led), on_led, avr);
  boot_quiet(avr, t, 0, &ctx);

  const uint8_t toc[SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  const uint8_t play[SUBQ_FRAME_BYTES] = { 0x41U, 0x01U, 0x01U, 0x00U, 0x02U, 0,
                                           0,     0,     0x02U, 0,     0,     0 };

  int disc1 = strings_while(avr, t, toc, MD_TOC_FRAMES);
  md_check(t, disc1 >= 1, "disc 1 is injected", disc1);

  clock_frames(avr, t, play, MD_SETTLE_FRAMES);
  int after_accept = strings_while(avr, t, play, MD_PLAY_FRAMES);
  md_check(t, after_accept == 0, "no string once the program area is read", after_accept);

  int reread = strings_while(avr, t, toc, MD_REREAD_FRAMES);
  md_check(t,
           (reread >= 1) && (reread <= MD_CAP),
           "a long TOC re-read of the same disc is served again, within the cap",
           reread);
  clock_frames(avr, t, play, MD_SETTLE_FRAMES);
  int replay = strings_while(avr, t, play, MD_PLAY_FRAMES);
  md_check(t, replay == 0, "play after the re-read stays silent", replay);

  swap_disc(avr);
  int disc2 = strings_while(avr, t, toc, MD_TOC_FRAMES);
  md_check(t, disc2 >= 1, "disc 2 after a swap is injected", disc2);
  clock_frames(avr, t, play, MD_PLAY_FRAMES);

  swap_disc(avr);
  int disc3 = strings_while(avr, t, toc, MD_TOC_FRAMES);
  md_check(t, disc3 >= 1, "disc 3 after another swap is injected", disc3);
}

// Without a lid wire, a swap is a stretch with no valid SUBQ frame. A pause
// shorter than the 1.5 s bound, as a seek or a brief stall on the same disc
// gives, keeps the disc and its session (host loop tests), and a lead-in read
// after it is served like any later lead-in read of the same disc. A swap past
// the bound re-arms, which the multi-disc scenario covers.
#define SHORT_PAUSE_MS 800U

void scenario_disc_presence(const target_t *t, const char *elf, uint32_t freq) {
  const uint8_t toc[SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  const uint8_t play[SUBQ_FRAME_BYTES] = { 0x41U, 0x01U, 0x01U, 0x00U, 0x02U, 0,
                                           0,     0,     0x02U, 0,     0,     0 };
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, 0U };
  avr_irq_register_notify(pin_irq(avr, t, t->led), on_led, avr);
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
  check((g_led_seen != 0) && (reread >= 1), label);

  // A long reread leaves the counter high. After a swap, the next disc's first
  // frames are not lead-in, as a drive spinning up and seeking gives; with the
  // counter emptied by the swap they must not open the window.
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

// The boot light: at power-on the LED must come on, stay on through the WFCK
// settle and detect wait, and go off before the first frame is read, so an
// installer sees power, clock and firmware are alive without a disc. It must
// also never stay on: after the wait it is dark until a region string is sent.
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
  // build_avr already ran the firmware through its port init, and the boot light
  // comes on within those first cycles, so read the LED level now as well as
  // watching for the later edges.
  avr_ioport_state_t state;
  (void)avr_ioctl(avr, AVR_IOCTL_IOPORT_GETSTATE((uint32_t)t->port), &state);
  if (((state.port >> t->led) & 1U) != 0U) {
    g_boot_on = avr->cycle;
  }
  avr_irq_register_notify(pin_irq(avr, t, t->led), on_boot_led, avr);
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
  avr_irq_register_notify(pin_irq(avr, t, t->led), on_led, avr);
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

// The live faults: no valid SUBQ frame at all for 5 s after power-on is code 3
// (SQCK, SUBQ or power wiring, or no disc); valid frames that never show a
// region check for 20 s are code 4, here an audio CD, whose lead-in frames are
// valid but never data, so the counter never rises.
#define NO_CHECK_FRAMES 2200

void faults_case(const target_t *t, const char *elf, uint32_t freq) {
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, 0U };
  avr_irq_register_notify(pin_irq(avr, t, t->led), on_led, avr);
  boot_quiet(avr, t, 0, &ctx);
  run_cycles(avr, ms_cycles(10000U));
  char label[96];
  (void)snprintf(label, sizeof(label), "led %s: no SUBQ shows code 3", t->mcu);
  check(code_after(DETECT_CYCLES) == 3, label);

  avr_t *quiet = build_avr(t, elf, freq);
  avr_irq_register_notify(pin_irq(quiet, t, t->led), on_led, quiet);
  boot_quiet(quiet, t, 0, &ctx);
  const uint8_t audio_lead_in[SUBQ_FRAME_BYTES] = {
    0x01U, 0x00U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0
  };
  clock_frames(quiet, t, audio_lead_in, NO_CHECK_FRAMES);
  (void)snprintf(label, sizeof(label), "led %s: an audio CD shows code 4", t->mcu);
  check(code_after(DETECT_CYCLES) == 4, label);
}
