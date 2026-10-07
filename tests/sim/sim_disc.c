// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <string.h>

#include "avr_ioport.h"
#include "harness.h"
#include "scenarios.h"
#include "sim_cycle_timers.h"

// Disc-handling scenarios: multi-disc swaps, the lid as a hard stop, and every
// LED stage from the boot light to the live fault codes.

// A multi-disc game, end to end. Each phase counts region strings by LED rises.
// The first program-area frame must stop the burst; a table-of-contents re-read
// with the lid shut, however long, as an anti-mod check might do, must stay
// silent; and every lid open and close must re-arm the chip so the next disc is
// injected, including a swap fast enough that the drive never visibly stops.
// The harness clocks a frame about every 12 ms, and the firmware may miss frames
// while an injection blocks, so phase lengths sit well past the trigger.
#define MD_TOC_FRAMES 40
#define MD_PLAY_FRAMES 30
#define MD_REREAD_FRAMES 520
#define MD_SETTLE_FRAMES 3
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
  reread += strings_while(avr, t, play, MD_PLAY_FRAMES);
  md_check(t, reread == 0, "a long TOC re-read with the lid shut stays silent", reread);

  swap_disc(avr);
  int disc2 = strings_while(avr, t, toc, MD_TOC_FRAMES);
  md_check(t, disc2 >= 1, "disc 2 after a lid open and close is injected", disc2);
  clock_frames(avr, t, play, MD_PLAY_FRAMES);

  swap_disc(avr);
  int disc3 = strings_while(avr, t, toc, MD_TOC_FRAMES);
  md_check(t, disc3 >= 1, "disc 3 after another swap is injected", disc3);
}

// The lid is the chip's hard stop. With the lid open the chip never injects,
// whatever SUBQ shows; and when the lid opens partway through a string, DATA
// must be released within about one 4 ms bit cell and stay released for the rest
// of what would have been the string, not keep driving the remaining 40-odd
// bits. The lid opens 3 bit cells in; from 2 bit cells after that, DATA is
// sampled every quarter bit cell to the end of the string, so a single sample
// cannot pass by landing on a high-Z one bit.
#define LID_OPEN_AT_BITS 3U
#define LID_GRACE_BITS 2U

void scenario_lid(const target_t *t, const char *elf, uint32_t freq) {
  const uint8_t toc[SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  char label[96];

  avr_t *open_avr = build_avr(t, elf, freq);
  wfck_ctx_t open_ctx = { NULL, 1U, 0U };
  g_led_seen = 0;
  avr_irq_register_notify(pin_irq(open_avr, t, t->led), on_led, open_avr);
  boot_quiet(open_avr, t, 0, &open_ctx);
  avr_raise_irq(lid_irq(open_avr), 1U);
  uint64_t opened = open_avr->cycle;
  clock_frames(open_avr, t, toc, MD_TOC_FRAMES);
  run_cycles(open_avr, ms_cycles(3000U));
  (void)snprintf(label, sizeof(label), "lid %s: no injection while the lid is open", t->mcu);
  check(g_strings == 0, label);
  (void)snprintf(label, sizeof(label), "lid %s: an open lid shows code 3", t->mcu);
  check(code_after(opened) == 3, label);

  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, 0U };
  g_led_seen = 0;
  g_led_cycle = 0;
  avr_irq_register_notify(pin_irq(avr, t, t->led), on_led, avr);
  boot_quiet(avr, t, 0, &ctx);
  clock_until_inject(avr, t, toc);
  uint64_t deadline = avr->cycle + LED_DEADLINE;
  while ((g_led_seen == 0) && (avr->cycle < deadline)) {
    run_cycles(avr, 2000U);
  }
  (void)snprintf(label, sizeof(label), "lid %s: injection starts with the lid shut", t->mcu);
  check(g_led_seen != 0, label);

  uint64_t bit = BIT_CYCLES(freq);
  run_to(avr, g_led_cycle + (LID_OPEN_AT_BITS * bit));
  avr_raise_irq(lid_irq(avr), 1U);
  uint64_t from = avr->cycle + (LID_GRACE_BITS * bit);
  uint64_t until = g_led_cycle + ((uint64_t)SCEX_BITS * bit);
  int driven = 0;
  for (uint64_t c = from; c < until; c += bit / 4U) {
    run_to(avr, c);
    if (data_ddr(avr, t) != 0U) {
      driven = 1;
    }
  }
  (void)snprintf(
      label, sizeof(label), "lid %s: DATA stays released once the lid opens mid-string", t->mcu);
  check(driven == 0, label);
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

// The live faults, each judged since the lid closed: no SUBQ frame at all for
// 5 s is code 4 (clock or SQCK wiring), frames that never show a region check
// for 20 s are code 5 (SUBQ wiring, or a disc with no data lead-in).
#define NO_CHECK_FRAMES 2200

void faults_case(const target_t *t, const char *elf, uint32_t freq) {
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, 0U };
  avr_irq_register_notify(pin_irq(avr, t, t->led), on_led, avr);
  boot_quiet(avr, t, 0, &ctx);
  run_cycles(avr, ms_cycles(10000U));
  char label[96];
  (void)snprintf(label, sizeof(label), "led %s: no SQCK shows code 4", t->mcu);
  check(code_after(DETECT_CYCLES) == 4, label);

  avr_t *quiet = build_avr(t, elf, freq);
  avr_irq_register_notify(pin_irq(quiet, t, t->led), on_led, quiet);
  boot_quiet(quiet, t, 0, &ctx);
  const uint8_t silence[SUBQ_FRAME_BYTES] = { 0 };
  clock_frames(quiet, t, silence, NO_CHECK_FRAMES);
  (void)snprintf(label, sizeof(label), "led %s: frames but no region check shows code 5", t->mcu);
  check(code_after(DETECT_CYCLES) == 5, label);
}
