// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "avr_eeprom.h"
#include "avr_ioport.h"
#include "sim_avr.h"
#include "sim_cycle_timers.h"
#include "sim_elf.h"
#include "sim_irq.h"

// A software PlayStation, just enough to exercise the firmware end to end in
// simavr: it drives the console-side pins (SQCK, SUBQ, WFCK) and watches the
// firmware-side pins (DATA, LED), then decodes the injected bitstream and
// checks it equals the expected region word. It also drives the lid line, a
// mandatory wire on the ATtiny84's PORTB. Timing is in CPU cycles at the console
// clock the firmware is built for, 4.2336 MHz, where one cycle is about 236 ns;
// the comments give each figure in time at that clock.

#define SUBQ_FRAME_BYTES 12
#define SUBQ_BITS 8
// Half-period of the SQCK clock we fake while shifting a SUBQ frame.
#define EDGE_CYCLES 60
// A fast SQCK the firmware must still follow: 20 cycles, about 4.7 us per half
// period at 4.2336 MHz. The assembly capture follows down to about 2.8 us; the
// earlier C capture failed below about 13 us, so this frame rate pins the margin.
#define FAST_EDGE_CYCLES 20
// Idle-high SQCK gap after each frame. A console clocks one frame per sector at
// 75 Hz, so bursts are separated by most of ~13.3 ms; the firmware resyncs on a
// gap of at least 1 ms, and 9.4 ms sits clearly inside the real gap.
#define FRAME_GAP_CYCLES 40000UL
// Cycles to let the firmware's boot complete before the first frame: the 300 ms
// WFCK settle and the 10000-sample window under the boot light (about 0.4 s),
// then the board blinks, at most two 300 ms flashes with their gaps (1.2 s).
// 1.7 s covers both with margin.
#define DETECT_CYCLES 7200000UL
// A carrier that starts this long after power-on (283 ms) is still inside the
// 300 ms settle time, so it must be detected as a carrier board.
#define LATE_CARRIER_CYCLES 1200000UL
#define INJECT_CYCLES 7000000UL
#define TRIGGER_FRAMES 10
// The chip syncs to SUBQ by waiting for the idle gap before a frame, so a frame
// that starts while it is still qualifying the gap is skipped, as on a console,
// which streams frames without end. The harness sends a fixed number, so it
// allows a few spare lead-in frames and stops the moment injection starts.
#define TRIGGER_SPARE_FRAMES 3
#define SCEX_BITS 44
// One SCEx bit cell is 4 ms, so a quarter of a thousandth of the clock rate in
// cycles. The decoder samples at this spacing from the LED edge that marks
// injection start.
#define BIT_CYCLES(freq) ((uint64_t)(freq) / 250U)
// WFCK carrier the modern-board model oscillates at: ~7.3 kHz during init, and
// ~14.6 kHz during a 2x data read. The band between them is the real variation
// the carrier-mirror injection timing must tolerate, so both ends are tested.
#define WFCK_HZ 7300UL
#define WFCK_READ_HZ 14600UL
// The adaptive build times a modern bit cell as this many WFCK periods (Read,
// PsNee), so the decoder derives the modern bit length from the carrier period
// instead of a fixed cycle count. Legacy bits stay the fixed MCU-delay length.
#define WFCK_PERIODS_PER_BIT 30UL
#define LED_DEADLINE 1000000UL

typedef struct {
  const char *mcu;
  char port;
  uint8_t sqck;
  uint8_t subq;
  uint8_t data;
  uint8_t led;
  uint8_t wfck;
} target_t;

// The America (SCEA) word as the 44 DATA levels the firmware should emit,
// LSB-first. This is the default build's single configured region, so it is
// the only word the firmware emits; the decoder reconstructs it and compares. A
// correct decode on both board models proves the encoder and the bit timing.

// The lid line: PB1, read high while the lid is open. The harness drives it on
// every boot, closed, so each scenario starts as a console with a disc in.
#define LID_PORT 'B'
#define LID_PIN 1
static const char SCEA_BITS[SCEX_BITS + 1] = "10011010100100111101001010111010010111110100";

// The Japan (SCEI) word, which the SCPH-5903 Video-CD build emits: that console
// is NTSC-J, so its image is built with REGION=jp.
static const char SCEI_BITS[SCEX_BITS + 1] = "10011010100100111101001010111010010110110100";

static int g_checks = 0;
static int g_failures = 0;
static int g_led_seen = 0;
static uint64_t g_led_cycle = 0;
// Every LED pulse the firmware draws, as rise cycle and length. A region string
// lights the LED for 90 to 181 ms, every status pattern for longer or shorter, so
// a pulse of 60 to 200 ms that starts after boot counts as one string.
#define MAX_PULSES 512
static uint64_t g_pulse_rise[MAX_PULSES];
static uint64_t g_pulse_len[MAX_PULSES];
static int g_pulses = 0;
static uint64_t g_rise = 0;
static int g_strings = 0;
static uint32_t g_freq = 4233600U;
// The SQCK half-period the harness clocks frames with, normally EDGE_CYCLES.
static uint64_t g_edge_cycles = EDGE_CYCLES;

typedef struct {
  avr_irq_t *irq;
  uint8_t level;
  uint32_t half;
} wfck_ctx_t;

static void check(int cond, const char *name) {
  g_checks++;
  if (!cond) {
    g_failures++;
    (void)printf("FAIL: %s\n", name);
  }
}

static uint64_t ms_cycles(uint32_t ms) {
  return ((uint64_t)g_freq * ms) / 1000U;
}

// The first LED rise after boot marks the first region string, which the
// scenarios that decode or interrupt a string start from; nothing else lights
// the LED that early after boot. Every pulse is also recorded, and one whose
// length matches a region string is counted as one.
static void on_led(struct avr_irq_t *irq, uint32_t value, void *param) {
  avr_t *avr = param;
  (void)irq;
  if (value != 0U) {
    g_rise = avr->cycle;
    if ((avr->cycle >= DETECT_CYCLES) && (g_led_seen == 0)) {
      g_led_seen = 1;
      g_led_cycle = avr->cycle;
    }
    return;
  }
  if (g_rise == 0U) {
    return;
  }
  uint64_t len = avr->cycle - g_rise;
  if (g_pulses < MAX_PULSES) {
    g_pulse_rise[g_pulses] = g_rise;
    g_pulse_len[g_pulses] = len;
    g_pulses++;
  }
  if ((g_rise >= DETECT_CYCLES) && (len >= ms_cycles(60U)) && (len <= ms_cycles(200U))) {
    g_strings++;
  }
  g_rise = 0U;
}

// Count the first group of pulses at or after `from` whose length lies in
// [min_ms, max_ms], a group ending where the next such pulse starts more than
// 1.5 s after the previous one. A code is that many long flashes one second
// apart, then a 2 s pause, so this reads one repetition of a code.
static int pulse_group(uint64_t from, uint32_t min_ms, uint32_t max_ms) {
  int count = 0;
  uint64_t last = 0U;
  for (int i = 0; i < g_pulses; i++) {
    if ((g_pulse_rise[i] < from) || (g_pulse_len[i] < ms_cycles(min_ms)) ||
        (g_pulse_len[i] > ms_cycles(max_ms))) {
      continue;
    }
    if ((count > 0) && ((g_pulse_rise[i] - last) > ms_cycles(1500U))) {
      break;
    }
    count++;
    last = g_pulse_rise[i];
  }
  return count;
}

static void clock_frames(avr_t *avr, const target_t *t, const uint8_t *frame, int count);

static int code_after(uint64_t from) {
  return pulse_group(from, 600U, 800U);
}

static avr_cycle_count_t wfck_tick(avr_t *avr, avr_cycle_count_t when, void *param) {
  wfck_ctx_t *ctx = param;
  (void)avr;
  ctx->level = (uint8_t)(ctx->level ^ 1U);
  avr_raise_irq(ctx->irq, ctx->level);
  return when + ctx->half;
}

static void run_to(avr_t *avr, uint64_t target) {
  while (avr->cycle < target) {
    int state = avr_run(avr);
    if ((state == cpu_Crashed) || (state == cpu_Done)) {
      break;
    }
  }
}

static void run_cycles(avr_t *avr, uint64_t n) {
  run_to(avr, avr->cycle + n);
}

static avr_irq_t *pin_irq(avr_t *avr, const target_t *t, uint8_t pin) {
  return avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ((uint32_t)t->port), pin);
}

static avr_irq_t *lid_irq(avr_t *avr) {
  return avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ((uint32_t)LID_PORT), LID_PIN);
}

// The firmware's port init turns on the lid pull-up, which simavr reflects on the
// pin until an external level is driven again. So the harness lets init run,
// then pulses the lid line high and back low: the console's closed-lid level
// then holds over the weak pull-up, as on the real board, and every scenario
// starts with a disc in.
#define PORT_INIT_CYCLES 2000U

static avr_t *build_avr(const target_t *t, const char *elf, uint32_t freq) {
  elf_firmware_t firmware;
  memset(&firmware, 0, sizeof(firmware));
  (void)elf_read_firmware(elf, &firmware);

  avr_t *avr = avr_make_mcu_by_name(t->mcu);
  avr_init(avr);
  avr_load_firmware(avr, &firmware);
  avr->frequency = freq;
  g_freq = freq;
  g_led_seen = 0;
  g_led_cycle = 0U;
  g_pulses = 0;
  g_rise = 0U;
  g_strings = 0;
  avr_irq_t *lid = lid_irq(avr);
  avr_raise_irq(lid, 0U);
  run_cycles(avr, PORT_INIT_CYCLES);
  avr_raise_irq(lid, 1U);
  avr_raise_irq(lid, 0U);
  return avr;
}

static uint8_t data_ddr(avr_t *avr, const target_t *t) {
  avr_ioport_state_t state;
  (void)avr_ioctl(avr, AVR_IOCTL_IOPORT_GETSTATE((uint32_t)t->port), &state);
  return (uint8_t)((state.ddr >> t->data) & 1U);
}

static uint8_t data_pin(avr_t *avr, const target_t *t) {
  avr_ioport_state_t state;
  (void)avr_ioctl(avr, AVR_IOCTL_IOPORT_GETSTATE((uint32_t)t->port), &state);
  return (uint8_t)((state.port >> t->data) & 1U) & (uint8_t)((state.ddr >> t->data) & 1U);
}

static void clock_frame(avr_t *avr, const target_t *t, const uint8_t *frame) {
  avr_irq_t *sqck = pin_irq(avr, t, t->sqck);
  avr_irq_t *subq = pin_irq(avr, t, t->subq);
  for (int byte = 0; byte < SUBQ_FRAME_BYTES; byte++) {
    for (int bit = 0; bit < SUBQ_BITS; bit++) {
      avr_raise_irq(subq, (uint8_t)((frame[byte] >> bit) & 1U));
      avr_raise_irq(sqck, 0U);
      run_cycles(avr, g_edge_cycles);
      avr_raise_irq(sqck, 1U);
      run_cycles(avr, g_edge_cycles);
    }
  }
  // Idle the clock for the inter-frame gap, but stop early the moment the LED
  // marks the start of an injection inside it: the decoder samples DATA forward
  // from that edge and cannot sample cycles the gap has already run past.
  int led_before = g_led_seen;
  uint64_t gap_end = avr->cycle + FRAME_GAP_CYCLES;
  while ((avr->cycle < gap_end) && !((g_led_seen != 0) && (led_before == 0))) {
    run_cycles(avr, 500U);
  }
}

// Clock lead-in frames until the first string starts, with the spare frames
// TRIGGER_SPARE_FRAMES allows, and stop there so no frame overlaps the string.
// A frame that would not arm injection is clocked the full count, which only
// makes a negative case stricter.
static void clock_until_inject(avr_t *avr, const target_t *t, const uint8_t *frame) {
  for (int i = 0; (i < (TRIGGER_FRAMES + TRIGGER_SPARE_FRAMES)) && (g_led_seen == 0); i++) {
    clock_frame(avr, t, frame);
  }
}

static void boot_quiet(avr_t *avr, const target_t *t, int modern, wfck_ctx_t *ctx) {
  avr_raise_irq(pin_irq(avr, t, t->sqck), 1U);
  avr_raise_irq(pin_irq(avr, t, t->subq), 0U);
  avr_raise_irq(pin_irq(avr, t, t->wfck), 1U);
  if (modern != 0) {
    ctx->irq = pin_irq(avr, t, t->wfck);
    ctx->level = 1U;
    avr_cycle_timer_register(avr, ctx->half, wfck_tick, ctx);
  }
  run_cycles(avr, DETECT_CYCLES);
}

// Reconstruct the 44-bit word the firmware drove, reading DATA once per bit
// cell starting from the LED injection marker. The two board models encode a
// logic one differently, so each is decoded on its own terms: on a modern
// board a one is DATA mirroring the WFCK carrier, seen as the pin going high
// somewhere mid-cell (sampled across s = 3..7 tenths); on a legacy board a one
// is high-Z, seen as DATA left as an input (DDR clear), a zero as driven low.
static void decode_region(
    avr_t *avr, const target_t *t, int modern, uint64_t bit_cycles, char *out) {
  for (int k = 0; k < SCEX_BITS; k++) {
    uint64_t base = g_led_cycle + ((uint64_t)k * bit_cycles);
    uint8_t bit;
    if (modern != 0) {
      // A modern one is DATA mirroring the WFCK carrier, so it oscillates
      // within the cell. Sampling only a few points can alias onto the low
      // phase and misread it; step finely across the central 40 percent of the
      // cell and treat any high as a one, which always catches the carrier.
      uint8_t any_high = 0U;
      uint64_t lo = base + ((3U * bit_cycles) / 10U);
      uint64_t hi = base + ((7U * bit_cycles) / 10U);
      for (uint64_t c = lo; c <= hi; c += 64U) {
        run_to(avr, c);
        if (data_pin(avr, t) != 0U) {
          any_high = 1U;
        }
      }
      bit = any_high;
    } else {
      run_to(avr, base + (bit_cycles / 2U));
      bit = (uint8_t)((data_ddr(avr, t) != 0U) ? 0U : 1U);
    }
    out[k] = (bit != 0U) ? '1' : '0';
  }
  out[SCEX_BITS] = '\0';
}

static void scenario_inject(const target_t *t,
                            const char *elf,
                            uint32_t freq,
                            int modern,
                            int trigger,
                            uint32_t wfck_hz,
                            const char *family) {
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, (uint32_t)(freq / (2UL * wfck_hz)) };
  g_led_seen = 0;
  g_led_cycle = 0;
  avr_irq_register_notify(pin_irq(avr, t, t->led), on_led, avr);
  boot_quiet(avr, t, modern, &ctx);

  // A lead-in frame that must arm injection (control 0x41 = data sector,
  // track 0xA0 = TOC) versus an ordinary audio frame that must not (control
  // 0x01, track 0x02). frame[1] and frame[6] are zero so both parse as framed.
  uint8_t toc[SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  uint8_t audio[SUBQ_FRAME_BYTES] = { 0x01U, 0x00U, 0x02U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  clock_until_inject(avr, t, (trigger != 0) ? toc : audio);

  uint64_t deadline = avr->cycle + LED_DEADLINE;
  while ((g_led_seen == 0) && (avr->cycle < deadline)) {
    run_cycles(avr, 2000U);
  }

  // A legacy bit is a fixed MCU-delay cell; an adaptive modern bit is
  // WFCK_PERIODS_PER_BIT carrier periods, so its length scales with the carrier
  // frequency and the decoder must measure it from wfck_hz, not a constant.
  uint64_t bit_cycles =
      (modern != 0) ? ((uint64_t)WFCK_PERIODS_PER_BIT * freq / wfck_hz) : BIT_CYCLES(freq);
  const char *tag = family;
  char label[96];
  char decoded[SCEX_BITS + 1];
  if (trigger != 0) {
    (void)snprintf(label, sizeof(label), "%s: inject triggered at %u Hz", tag, freq);
    check(g_led_seen != 0, label);
    if (g_led_seen != 0) {
      decode_region(avr, t, modern, bit_cycles, decoded);
      (void)snprintf(label, sizeof(label), "%s: decodes SCEA at %u Hz", tag, freq);
      check(strcmp(decoded, SCEA_BITS) == 0, label);
      if (strcmp(decoded, SCEA_BITS) != 0) {
        (void)printf("  expected %s\n  decoded  %s\n", SCEA_BITS, decoded);
      }
    }
  } else {
    (void)snprintf(label, sizeof(label), "%s: non-TOC does not inject at %u Hz", tag, freq);
    check(g_led_seen == 0, label);
  }
}

// The LED reports each disc's result after its check: one long flash repeated
// for a disc the console accepted, two for one it never accepted, each code
// three times and then dark. A legacy board is driven with TOC frames to arm
// injection, then with program-area frames (accepted) or framed silence until
// the confirmation wait expires (refused).
#define RESULT_TOC_FRAMES 12
#define RESULT_AFTER_FRAMES 120

static void result_case(const target_t *t, const char *elf, uint32_t freq, int program) {
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, 0U };
  avr_irq_register_notify(pin_irq(avr, t, t->led), on_led, avr);
  boot_quiet(avr, t, 0, &ctx);

  const uint8_t toc[SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  const uint8_t silence[SUBQ_FRAME_BYTES] = { 0 };
  const uint8_t play[SUBQ_FRAME_BYTES] = { 0x41U, 0x01U, 0x01U, 0x00U, 0x02U, 0,
                                           0,     0,     0x02U, 0,     0,     0 };
  clock_frames(avr, t, toc, RESULT_TOC_FRAMES);
  uint64_t mark = avr->cycle;
  clock_frames(avr, t, (program != 0) ? play : silence, RESULT_AFTER_FRAMES);
  run_cycles(avr, ms_cycles(4000U));

  int expected = (program != 0) ? 1 : 2;
  char label[96];
  (void)snprintf(label,
                 sizeof(label),
                 "led %s: %s disc shows code %d",
                 t->mcu,
                 (program != 0) ? "accepted" : "refused",
                 expected);
  check((g_led_seen != 0) && (code_after(mark) == expected), label);
}

// A capture that starts inside a burst must not stay misaligned. Present a
// partial burst first, as a console already mid-sector would at the moment the
// firmware starts listening, then ordinary TOC frames. Without resync every
// later capture straddles two frames by the partial's length, nothing ever
// parses as framed, and injection never fires; with it the firmware loses at
// most one frame, realigns on the next inter-frame gap, and injects.
#define PARTIAL_BURST_BITS 37
#define RESYNC_FRAMES (TRIGGER_FRAMES + 3)

static void clock_partial(avr_t *avr, const target_t *t, int bits) {
  avr_irq_t *sqck = pin_irq(avr, t, t->sqck);
  avr_irq_t *subq = pin_irq(avr, t, t->subq);
  for (int bit = 0; bit < bits; bit++) {
    avr_raise_irq(subq, (uint8_t)(bit & 1));
    avr_raise_irq(sqck, 0U);
    run_cycles(avr, g_edge_cycles);
    avr_raise_irq(sqck, 1U);
    run_cycles(avr, g_edge_cycles);
  }
}

static void scenario_resync(const target_t *t, const char *elf, uint32_t freq) {
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, (uint32_t)(freq / (2UL * WFCK_HZ)) };
  g_led_seen = 0;
  g_led_cycle = 0;
  avr_irq_register_notify(pin_irq(avr, t, t->led), on_led, avr);
  boot_quiet(avr, t, 0, &ctx);

  uint8_t toc[SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  clock_partial(avr, t, PARTIAL_BURST_BITS);
  for (int i = 0; i < RESYNC_FRAMES; i++) {
    clock_frame(avr, t, toc);
  }
  uint64_t deadline = avr->cycle + LED_DEADLINE;
  while ((g_led_seen == 0) && (avr->cycle < deadline)) {
    run_cycles(avr, 2000U);
  }

  const char *tag = t->mcu;
  char label[96];
  (void)snprintf(label, sizeof(label), "resync %s: injects after a capture starts mid-burst", tag);
  check(g_led_seen != 0, label);
}

// A carrier board whose WFCK starts oscillating after the chip has powered up
// must still be detected as a carrier board. The oscillation starts 150 ms after
// power-on: a detect window taken immediately would see a static line, choose the
// legacy gate, and send every logic one as high-Z instead of the carrier mirror,
// so the modern decode would fail. The settle delay lets the carrier start first.
static void scenario_late_carrier(const target_t *t, const char *elf, uint32_t freq) {
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, (uint32_t)(freq / (2UL * WFCK_HZ)) };
  g_led_seen = 0;
  g_led_cycle = 0;
  avr_irq_register_notify(pin_irq(avr, t, t->led), on_led, avr);
  avr_raise_irq(pin_irq(avr, t, t->sqck), 1U);
  avr_raise_irq(pin_irq(avr, t, t->subq), 0U);
  avr_raise_irq(pin_irq(avr, t, t->wfck), 1U);
  run_cycles(avr, LATE_CARRIER_CYCLES);
  ctx.irq = pin_irq(avr, t, t->wfck);
  avr_cycle_timer_register(avr, ctx.half, wfck_tick, &ctx);
  run_to(avr, DETECT_CYCLES);

  uint8_t toc[SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  clock_until_inject(avr, t, toc);
  uint64_t deadline = avr->cycle + LED_DEADLINE;
  while ((g_led_seen == 0) && (avr->cycle < deadline)) {
    run_cycles(avr, 2000U);
  }

  const char *tag = t->mcu;
  char label[96];
  char decoded[SCEX_BITS + 1] = { 0 };
  (void)snprintf(label, sizeof(label), "late carrier %s: injection triggered", tag);
  check(g_led_seen != 0, label);
  if (g_led_seen != 0) {
    decode_region(avr, t, 1, (uint64_t)WFCK_PERIODS_PER_BIT * freq / WFCK_HZ, decoded);
    (void)snprintf(label, sizeof(label), "late carrier %s: detected as a carrier board", tag);
    check(strcmp(decoded, SCEA_BITS) == 0, label);
  }
}

// A carrier that stops mid-injection must not leave DATA driven. The adaptive
// bit cell counts WFCK rising edges and kicks the watchdog only on a counted
// edge, so when the carrier stalls the watchdog expires and resets the chip,
// which returns DATA to high-Z. Stop the carrier about 25 ms into an injection
// (a few bit cells in, while DATA is driven) and check it is released within
// the next 0.75 s, comfortably past the ~0.5 s watchdog timeout.
#define STALL_AFTER_CYCLES 200000UL
#define STALL_WAIT_CYCLES 6000000UL

static void scenario_stall(const target_t *t, const char *elf, uint32_t freq) {
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, (uint32_t)(freq / (2UL * WFCK_HZ)) };
  g_led_seen = 0;
  g_led_cycle = 0;
  avr_irq_register_notify(pin_irq(avr, t, t->led), on_led, avr);
  boot_quiet(avr, t, 1, &ctx);

  uint8_t toc[SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  clock_until_inject(avr, t, toc);
  uint64_t deadline = avr->cycle + LED_DEADLINE;
  while ((g_led_seen == 0) && (avr->cycle < deadline)) {
    run_cycles(avr, 2000U);
  }

  const char *tag = t->mcu;
  char label[96];
  (void)snprintf(label, sizeof(label), "stall %s: injection started", tag);
  check(g_led_seen != 0, label);
  run_to(avr, g_led_cycle + STALL_AFTER_CYCLES);
  (void)snprintf(label, sizeof(label), "stall %s: DATA driven while the carrier runs", tag);
  check(data_ddr(avr, t) != 0U, label);

  avr_cycle_timer_cancel(avr, wfck_tick, &ctx);
  run_cycles(avr, STALL_WAIT_CYCLES);
  (void)snprintf(label, sizeof(label), "stall %s: DATA released after the carrier stops", tag);
  check(data_ddr(avr, t) == 0U, label);

  uint64_t rebooted = avr->cycle;
  run_cycles(avr, ms_cycles(9000U));
  (void)snprintf(label, sizeof(label), "stall %s: the next boot shows watchdog code 6", tag);
  check(code_after(rebooted) == 6, label);
}

// The board-family matrix. The firmware has no per-family code path, only the
// legacy static gate and the live WFCK carrier, so each family collapses to one
// of those with its carrier frequency. Distinct behaviours only, no duplicate
// runs: the two carrier rows differ by the init and read frequencies that bound
// the WFCK band.
typedef struct {
  const char *name;
  int modern;
  uint32_t wfck_hz;
} board_family_t;

static const board_family_t FAMILIES[] = {
  { "PU-18 and PU-20 legacy gate", 0, WFCK_HZ },
  { "PU-22 to PM-41 carrier 7.3kHz", 1, WFCK_HZ },
  { "PU-22 to PM-41 carrier 14.6kHz", 1, WFCK_READ_HZ },
};
#define FAMILY_COUNT ((int)(sizeof(FAMILIES) / sizeof(FAMILIES[0])))

// Boot one legacy-board image, clock copies of one lead-in frame until a string
// starts or the spare frames run out, and report whether the LED marked an
// injection; when it did and an expected word is given, decode DATA and compare
// it. Each call is a fresh power-on, so one frame kind is judged on its own.
static void vcd_case(
    const char *elf, uint32_t freq, const uint8_t *frame, const char *expect, const char *what) {
  target_t t84 = { "attiny84", 'A', 0U, 1U, 2U, 4U, 3U };
  avr_t *avr = build_avr(&t84, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, 0U };
  g_led_seen = 0;
  g_led_cycle = 0;
  avr_irq_register_notify(pin_irq(avr, &t84, t84.led), on_led, avr);
  boot_quiet(avr, &t84, 0, &ctx);
  clock_until_inject(avr, &t84, frame);
  uint64_t deadline = avr->cycle + LED_DEADLINE;
  while ((g_led_seen == 0) && (avr->cycle < deadline)) {
    run_cycles(avr, 2000U);
  }

  char label[96];
  (void)snprintf(label, sizeof(label), "vcd build: %s at %u Hz", what, freq);
  if (expect == NULL) {
    check(g_led_seen == 0, label);
    return;
  }
  check(g_led_seen != 0, label);
  if (g_led_seen != 0) {
    char decoded[SCEX_BITS + 1];
    decode_region(avr, &t84, 0, BIT_CYCLES(freq), decoded);
    (void)snprintf(label, sizeof(label), "vcd build: decodes SCEI at %u Hz", freq);
    check(strcmp(decoded, expect) == 0, label);
  }
}

// The SCPH-5903 build (REGION=jp VCD_FILTER=on) must still unlock a game, whose
// lead-in carries the TOC markers, and must stay silent on a Video CD, whose
// lead-in marker carries 0x02 in frame[3]. The point-01 spiral frame that arms
// the ordinary build must not arm this one either. Read: PsNee V9.0 SCPH_5903
// FilterSUBQSamples, PSNee.ino:471-502.
static void scenario_vcd(const char *elf, uint32_t freq) {
  const uint8_t game[SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0x00U, 0, 0, 0, 0, 0, 0, 0, 0 };
  const uint8_t vcd[SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0x02U, 0, 0, 0, 0, 0, 0, 0, 0 };
  const uint8_t spiral[SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0x01U, 0x98U, 0, 0, 0, 0, 0, 0, 0, 0 };
  vcd_case(elf, freq, game, SCEI_BITS, "game TOC injects");
  vcd_case(elf, freq, vcd, NULL, "video CD lead-in does not inject");
  vcd_case(elf, freq, spiral, NULL, "point-01 spiral does not inject");
}

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
// How long the lid stays open on a swap: 236 ms, far shorter than a person
// takes, so the re-arm cannot depend on the drive stopping.
#define MD_LID_OPEN_CYCLES 1000000UL

static void clock_frames(avr_t *avr, const target_t *t, const uint8_t *frame, int count) {
  for (int i = 0; i < count; i++) {
    clock_frame(avr, t, frame);
  }
}

static int strings_while(avr_t *avr, const target_t *t, const uint8_t *frame, int count) {
  int before = g_strings;
  clock_frames(avr, t, frame, count);
  return g_strings - before;
}

static void md_check(const target_t *t, int ok, const char *what, int strings) {
  char label[112];
  (void)snprintf(label, sizeof(label), "multi-disc %s: %s (%d strings)", t->mcu, what, strings);
  check(ok, label);
}

static void swap_disc(avr_t *avr) {
  avr_raise_irq(lid_irq(avr), 1U);
  run_cycles(avr, MD_LID_OPEN_CYCLES);
  avr_raise_irq(lid_irq(avr), 0U);
}

static void scenario_multidisc(const target_t *t, const char *elf, uint32_t freq) {
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

static void scenario_lid(const target_t *t, const char *elf, uint32_t freq) {
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

// The console's real SQCK rate is not documented anywhere this project cites,
// so the capture has to keep a wide margin. Drive a whole injection on a legacy
// board with frames clocked at FAST_EDGE_CYCLES and require the region word to
// decode, then restore the normal rate for the scenarios that follow.
static void scenario_fast_sqck(const target_t *t, const char *elf, uint32_t freq) {
  g_edge_cycles = FAST_EDGE_CYCLES;
  scenario_inject(t, elf, freq, 0, 1, WFCK_HZ, "fast SQCK, 4.7 us half period");
  g_edge_cycles = EDGE_CYCLES;
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

static void scenario_boot_light(const target_t *t, const char *elf, uint32_t freq) {
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
static void board_blinks_case(const target_t *t, const char *elf, uint32_t freq, int modern) {
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

static void faults_case(const target_t *t, const char *elf, uint32_t freq) {
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
static void scenario_calib_cap(const target_t *t, const char *elf, uint32_t freq) {
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
static void scenario_calib_board(const target_t *t, const char *elf, uint32_t freq) {
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

static void scenario_calib_missed(const target_t *t, const char *elf, uint32_t freq) {
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

int main(int argc, char *argv[]) {
  if (argc < 3) {
    (void)fprintf(stderr, "usage: %s elf freq_hz [vcd_elf]\n", argv[0]);
    return 2;
  }
  const char *elf = argv[1];
  uint32_t freq = (uint32_t)strtoul(argv[2], NULL, 10);

  // The SCEx signals sit on the ATtiny84's PORTA. The image is driven through
  // the whole board-family matrix plus a non-TOC negative, so both board models
  // and both ends of the WFCK carrier band are verified, then through every
  // behaviour scenario.
  target_t t84 = { "attiny84", 'A', 0U, 1U, 2U, 4U, 3U };

  for (int f = 0; f < FAMILY_COUNT; f++) {
    scenario_inject(&t84, elf, freq, FAMILIES[f].modern, 1, FAMILIES[f].wfck_hz, FAMILIES[f].name);
  }
  scenario_inject(&t84, elf, freq, 0, 0, WFCK_HZ, "legacy non-TOC negative");
  result_case(&t84, elf, freq, 1);
  result_case(&t84, elf, freq, 0);
  scenario_resync(&t84, elf, freq);
  scenario_late_carrier(&t84, elf, freq);
  scenario_stall(&t84, elf, freq);
  scenario_multidisc(&t84, elf, freq);
  scenario_lid(&t84, elf, freq);
  scenario_fast_sqck(&t84, elf, freq);
  scenario_boot_light(&t84, elf, freq);
  board_blinks_case(&t84, elf, freq, 0);
  board_blinks_case(&t84, elf, freq, 1);
  faults_case(&t84, elf, freq);
  scenario_calib_cap(&t84, elf, freq);
  scenario_calib_board(&t84, elf, freq);
  scenario_calib_missed(&t84, elf, freq);

  // The optional third argument is the SCPH-5903 Video-CD image.
  if (argc >= 4) {
    scenario_vcd(argv[3], freq);
  }

  (void)printf("%d checks, %d failures\n", g_checks, g_failures);
  return (g_failures == 0) ? 0 : 1;
}
