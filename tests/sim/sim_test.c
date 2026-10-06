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
// checks it equals the expected region word. Timing is in CPU cycles, which is
// independent of the simulated clock frequency, so the same model runs across
// the oscillator tolerance band.

#define SUBQ_FRAME_BYTES 12
#define SUBQ_BITS 8
// Half-period of the SQCK clock we fake while shifting a SUBQ frame.
#define EDGE_CYCLES 60
// Idle-high SQCK gap after each frame. A console clocks one frame per sector at
// 75 Hz, so bursts are separated by most of ~13.3 ms; the firmware resyncs on a
// gap of at least 1 ms, and 5 ms at 8 MHz sits clearly inside the real gap.
#define FRAME_GAP_CYCLES 40000UL
// Cycles to let the firmware's boot-time board detection complete: the 300 ms
// WFCK settle (2.4M cycles at 8 MHz) plus the 10000-sample window, with margin.
#define DETECT_CYCLES 3200000UL
// A carrier that starts this long after power-on (150 ms at 8 MHz) is still
// inside the settle time, so it must be detected as a carrier board.
#define LATE_CARRIER_CYCLES 1200000UL
#define INJECT_CYCLES 7000000UL
#define TRIGGER_FRAMES 10
#define SCEX_BITS 44
// One SCEx bit cell is 4 ms; at 8 MHz that is 32000 cycles. The decoder samples
// at this spacing from the LED edge that marks injection start.
#define BIT_CYCLES 32000UL
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
static const char SCEA_BITS[SCEX_BITS + 1] = "10011010100100111101001010111010010111110100";

#define BIOS_AX_PIN 2
#define BIOS_AY_PIN 6
#define BIOS_DX_PIN 5
// SCPH-102 (one-phase) pulse count, Read from PsNee V9.0 settings.h, and a quiet
// stretch long enough for its 8 silent windows of 1500 polls.
#define BIOS_PULSES 47
#define BIOS_CONFIRM_CYCLES 800000UL
// SCPH-1000 (two-phase) pulse counts, Read from PsNee V9.0 settings.h, and a run
// long enough for its 222 second-phase silent windows before the AY pulses.
#define BIOS2_PULSES_1 91
#define BIOS2_PULSES_2 70
#define BIOS2_SILENCE2_CYCLES 12000000UL
// The override must start right after the final counted edge: well inside the
// pulse that edge belongs to, never on an earlier pulse.
#define BIOS_OVERRIDE_WINDOW_CYCLES 200U

static int g_checks = 0;
static int g_failures = 0;
static int g_led_seen = 0;
static uint64_t g_led_cycle = 0;
static int g_dx_output = 0;
static uint64_t g_dx_cycle[2] = { 0, 0 };
static uint8_t g_dx_level[2] = { 0, 0 };

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

static void on_led(struct avr_irq_t *irq, uint32_t value, void *param) {
  avr_t *avr = param;
  (void)irq;
  if ((value != 0U) && (g_led_seen == 0)) {
    g_led_seen = 1;
    g_led_cycle = avr->cycle;
  }
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

static avr_t *build_avr(const target_t *t, const char *elf, uint32_t freq) {
  elf_firmware_t firmware;
  memset(&firmware, 0, sizeof(firmware));
  (void)elf_read_firmware(elf, &firmware);

  avr_t *avr = avr_make_mcu_by_name(t->mcu);
  avr_init(avr);
  avr_load_firmware(avr, &firmware);
  avr->frequency = freq;
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
      run_cycles(avr, EDGE_CYCLES);
      avr_raise_irq(sqck, 1U);
      run_cycles(avr, EDGE_CYCLES);
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
  for (int i = 0; i < TRIGGER_FRAMES; i++) {
    clock_frame(avr, t, (trigger != 0) ? toc : audio);
  }

  uint64_t deadline = avr->cycle + LED_DEADLINE;
  while ((g_led_seen == 0) && (avr->cycle < deadline)) {
    run_cycles(avr, 2000U);
  }

  // A legacy bit is a fixed MCU-delay cell; an adaptive modern bit is
  // WFCK_PERIODS_PER_BIT carrier periods, so its length scales with the carrier
  // frequency and the decoder must measure it from wfck_hz, not a constant.
  uint64_t bit_cycles =
      (modern != 0) ? ((uint64_t)WFCK_PERIODS_PER_BIT * freq / wfck_hz) : BIT_CYCLES;
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

// Watch the ATtiny84 PORTA direction register: the BIOS patch overrides the
// data bus by switching DX to an output for a few cycles, so a direction-change
// IRQ with the DX bit set is the override firing. Catching it by IRQ, not by
// polling, means the three-cycle window is never missed. For each of the first
// two overrides it records when it fired and the level DX was driven to, read
// from the port register at that instant (the port bit is set before the
// direction switches, so it already holds the driven level).
static void on_dx_direction(struct avr_irq_t *irq, uint32_t value, void *param) {
  avr_t *avr = param;
  (void)irq;
  if (((value >> BIOS_DX_PIN) & 1U) != 0U) {
    if (g_dx_output < 2) {
      avr_ioport_state_t state;
      (void)avr_ioctl(avr, AVR_IOCTL_IOPORT_GETSTATE('A'), &state);
      g_dx_cycle[g_dx_output] = avr->cycle;
      g_dx_level[g_dx_output] = (uint8_t)((state.port >> BIOS_DX_PIN) & 1U);
    }
    g_dx_output = g_dx_output + 1;
  }
}

static void check_override(int index, uint64_t edge_cycle, uint8_t level, const char *name) {
  char label[96];
  int on_time = (g_dx_output > index) && (g_dx_cycle[index] > edge_cycle) &&
                ((g_dx_cycle[index] - edge_cycle) < BIOS_OVERRIDE_WINDOW_CYCLES);
  (void)snprintf(label, sizeof(label), "attiny84 bios %s: fires on the final counted edge", name);
  check(on_time, label);
  (void)snprintf(
      label, sizeof(label), "attiny84 bios %s: drives DX %s", name, (level != 0U) ? "high" : "low");
  check((g_dx_output > index) && (g_dx_level[index] == level), label);
}

// Drive the ATtiny84 address line AX (on PORTB) through what the patch expects:
// align to a rising edge, hold quiet long enough for the silent-window count,
// then emit the model's pulse count. After the final pulse the firmware must
// drive the DX override, which on_dx_direction records. This checks the port
// mechanism (count pulses, then override); the cycle-exact constants themselves
// are hardware values and stay unverified until a console.
static void scenario_bios(const char *elf, uint32_t freq) {
  target_t t84 = { "attiny84", 'A', 0U, 1U, 2U, 4U, 3U };
  avr_t *avr = build_avr(&t84, elf, freq);
  g_dx_output = 0;

  avr_irq_t *direction = avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('A'), IOPORT_IRQ_DIRECTION_ALL);
  avr_irq_register_notify(direction, on_dx_direction, avr);
  avr_irq_t *ax = avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('B'), BIOS_AX_PIN);

  avr_raise_irq(ax, 0U);
  run_cycles(avr, 3000U);
  avr_raise_irq(ax, 1U);
  run_cycles(avr, 3000U);
  avr_raise_irq(ax, 0U);
  run_cycles(avr, BIOS_CONFIRM_CYCLES);

  uint64_t last_rise = 0U;
  for (int p = 0; p < BIOS_PULSES; p++) {
    avr_raise_irq(ax, 1U);
    last_rise = avr->cycle;
    run_cycles(avr, 200U);
    avr_raise_irq(ax, 0U);
    run_cycles(avr, 200U);
  }
  run_cycles(avr, 4000U);

  check(g_dx_output == 1, "attiny84 bios: one override after the pulse count");
  check_override(0, last_rise, 0U, "one-phase");
}

// The two oldest Japanese models override twice. Drive the first pulse train on
// AX as before, then, after the longer second silent gap, the second train on
// AY. The first window must fire on the last AX rising edge and drive DX high;
// the second must fire on the last AY falling edge and drive DX low.
static void scenario_bios_two_phase(const char *elf, uint32_t freq) {
  target_t t84 = { "attiny84", 'A', 0U, 1U, 2U, 4U, 3U };
  avr_t *avr = build_avr(&t84, elf, freq);
  g_dx_output = 0;

  avr_irq_t *direction = avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('A'), IOPORT_IRQ_DIRECTION_ALL);
  avr_irq_register_notify(direction, on_dx_direction, avr);
  avr_irq_t *ax = avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('B'), BIOS_AX_PIN);
  avr_irq_t *ay = avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('A'), BIOS_AY_PIN);

  avr_raise_irq(ax, 0U);
  run_cycles(avr, 3000U);
  avr_raise_irq(ax, 1U);
  run_cycles(avr, 3000U);
  avr_raise_irq(ax, 0U);
  run_cycles(avr, BIOS_CONFIRM_CYCLES);
  uint64_t last_ax_rise = 0U;
  for (int p = 0; p < BIOS2_PULSES_1; p++) {
    avr_raise_irq(ax, 1U);
    last_ax_rise = avr->cycle;
    run_cycles(avr, 200U);
    avr_raise_irq(ax, 0U);
    run_cycles(avr, 200U);
  }

  run_cycles(avr, BIOS2_SILENCE2_CYCLES);
  uint64_t last_ay_fall = 0U;
  for (int p = 0; p < BIOS2_PULSES_2; p++) {
    avr_raise_irq(ay, 1U);
    run_cycles(avr, 200U);
    avr_raise_irq(ay, 0U);
    last_ay_fall = avr->cycle;
    run_cycles(avr, 200U);
  }
  run_cycles(avr, 4000U);

  check(g_dx_output == 2, "attiny84 bios: both patch windows override the data bus");
  check_override(0, last_ax_rise, 1U, "two-phase first window");
  check_override(1, last_ay_fall, 0U, "two-phase second window");
}

// Prove the in-field diagnostics recorder and that closed-loop confirmation
// resolves and records, both ways. Boot a legacy board and drive TOC frames to
// arm and fire injection. Then either clock silence frames, so no program area
// appears and the confirmation FSM times out (unconfirmed), or clock real
// program-area frames, TNO 01, as a console does once it accepts the region
// string (confirmed). Reading the five EEPROM bytes back the way an installer
// would with avrdude checks the record: magic, legacy board, one session, a
// nonzero injection count, and the confirmation byte. Injection blocks for
// 44 bits at 4 ms, so the frames clocked meanwhile are lost; the firmware then
// resyncs on the next inter-frame gap and reads the following frames cleanly.
#define DIAG_TOC_FRAMES 12
#define DIAG_AFTER_FRAMES 120
#define DIAG_WRITE_CYCLES 3000000UL

static void read_record(avr_t *avr, uint8_t *raw) {
  avr_eeprom_desc_t desc = { .ee = raw, .offset = 0, .size = 5 };
  (void)avr_ioctl(avr, AVR_IOCTL_EEPROM_GET, &desc);
}

static void scenario_diag(const target_t *t, const char *elf, uint32_t freq, int program) {
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, (uint32_t)(freq / (2UL * WFCK_HZ)) };
  g_led_seen = 0;
  g_led_cycle = 0;
  avr_irq_register_notify(pin_irq(avr, t, t->led), on_led, avr);
  boot_quiet(avr, t, 0, &ctx);

  uint8_t toc[SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  uint8_t silence[SUBQ_FRAME_BYTES] = { 0 };
  uint8_t play[SUBQ_FRAME_BYTES] = { 0x41U, 0x01U, 0x01U, 0x00U, 0x02U, 0, 0, 0, 0x02U, 0, 0, 0 };
  for (int i = 0; i < DIAG_TOC_FRAMES; i++) {
    clock_frame(avr, t, toc);
  }
  for (int i = 0; i < DIAG_AFTER_FRAMES; i++) {
    clock_frame(avr, t, (program != 0) ? play : silence);
  }
  run_cycles(avr, DIAG_WRITE_CYCLES);

  uint8_t raw[5] = { 0, 0, 0, 0, 0 };
  read_record(avr, raw);

  const char *tag = (strcmp(t->mcu, "attiny85") == 0) ? "85" : "84";
  const char *path = (program != 0) ? "confirmed" : "unconfirmed";
  char label[96];
  (void)snprintf(label, sizeof(label), "diag %s %s: injection ran before logging", tag, path);
  check(g_led_seen != 0, label);
  (void)snprintf(label, sizeof(label), "diag %s %s: recorder magic written", tag, path);
  check(raw[0] == 0x50U, label);
  (void)snprintf(label, sizeof(label), "diag %s %s: board recorded as legacy gate", tag, path);
  check(raw[1] == 0U, label);
  (void)snprintf(label, sizeof(label), "diag %s %s: one session on a fresh eeprom", tag, path);
  check(raw[2] == 1U, label);
  (void)snprintf(label, sizeof(label), "diag %s %s: injection count recorded", tag, path);
  check(raw[3] >= 1U, label);
  (void)snprintf(label, sizeof(label), "diag %s %s: confirmation byte", tag, path);
  check(raw[4] == ((program != 0) ? 1U : 0U), label);
}

// Each arming is its own session. Within one power cycle, drive a first disc
// whose check is never confirmed, let the window close, then a second disc whose
// program area is reached: the recorder must hold two sessions and the second
// one's outcome, not stop after the first session of the power cycle.
static void scenario_diag_two_sessions(const target_t *t, const char *elf, uint32_t freq) {
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, (uint32_t)(freq / (2UL * WFCK_HZ)) };
  g_led_seen = 0;
  g_led_cycle = 0;
  avr_irq_register_notify(pin_irq(avr, t, t->led), on_led, avr);
  boot_quiet(avr, t, 0, &ctx);

  uint8_t toc[SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  uint8_t silence[SUBQ_FRAME_BYTES] = { 0 };
  uint8_t play[SUBQ_FRAME_BYTES] = { 0x41U, 0x01U, 0x01U, 0x00U, 0x02U, 0, 0, 0, 0x02U, 0, 0, 0 };
  for (int i = 0; i < DIAG_TOC_FRAMES; i++) {
    clock_frame(avr, t, toc);
  }
  for (int i = 0; i < DIAG_AFTER_FRAMES; i++) {
    clock_frame(avr, t, silence);
  }
  for (int i = 0; i < DIAG_TOC_FRAMES; i++) {
    clock_frame(avr, t, toc);
  }
  for (int i = 0; i < DIAG_AFTER_FRAMES; i++) {
    clock_frame(avr, t, play);
  }
  run_cycles(avr, DIAG_WRITE_CYCLES);

  uint8_t raw[5] = { 0, 0, 0, 0, 0 };
  read_record(avr, raw);
  const char *tag = (strcmp(t->mcu, "attiny85") == 0) ? "85" : "84";
  char label[96];
  (void)snprintf(label, sizeof(label), "diag %s two discs: two sessions recorded", tag);
  check(raw[2] == 2U, label);
  (void)snprintf(label, sizeof(label), "diag %s two discs: second session confirmed", tag);
  check(raw[4] == 1U, label);
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
    run_cycles(avr, EDGE_CYCLES);
    avr_raise_irq(sqck, 1U);
    run_cycles(avr, EDGE_CYCLES);
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

  const char *tag = (strcmp(t->mcu, "attiny85") == 0) ? "85" : "84";
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
  for (int i = 0; i < TRIGGER_FRAMES; i++) {
    clock_frame(avr, t, toc);
  }
  uint64_t deadline = avr->cycle + LED_DEADLINE;
  while ((g_led_seen == 0) && (avr->cycle < deadline)) {
    run_cycles(avr, 2000U);
  }

  const char *tag = (strcmp(t->mcu, "attiny85") == 0) ? "85" : "84";
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
  for (int i = 0; i < TRIGGER_FRAMES; i++) {
    clock_frame(avr, t, toc);
  }
  uint64_t deadline = avr->cycle + LED_DEADLINE;
  while ((g_led_seen == 0) && (avr->cycle < deadline)) {
    run_cycles(avr, 2000U);
  }

  const char *tag = (strcmp(t->mcu, "attiny85") == 0) ? "85" : "84";
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
}

// The BIOS patch must never stop the SCEx part from running. Two failures of the
// AX line are driven on a one-phase BIOS image, then ordinary TOC frames, and
// the SCEx injection must still fire:
// - AX never toggles (dead or miswired pad): the first-edge wait gives up after
//   its 3 s bound and the patch hands over to the run loop.
// - AX stops partway through the counted pulse train: the override loop stops
//   kicking the watchdog, the watchdog resets the chip, and on that reboot the
//   patch is skipped because the reset came from the watchdog. The window is
//   short enough that a reboot which retried the patch, and so sat in the 3 s
//   first-edge wait, would miss it.
#define BIOS_DEAD_AX_CYCLES 30000000UL
#define BIOS_PARTIAL_PULSES 10
#define BIOS_WDT_REBOOT_CYCLES 8000000UL

// Two frames more than the trigger: after a simulated watchdog reset simavr
// drops the externally driven pin levels, so SQCK reads low until the next frame
// drives it and the firmware resyncs one frame late, as it would on hardware
// only if it booted mid-burst.
static void run_toc_until_led(avr_t *avr, const target_t *t) {
  uint8_t toc[SUBQ_FRAME_BYTES] = { 0x41U, 0x00U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  for (int i = 0; i < (TRIGGER_FRAMES + 2); i++) {
    clock_frame(avr, t, toc);
  }
  uint64_t deadline = avr->cycle + LED_DEADLINE;
  while ((g_led_seen == 0) && (avr->cycle < deadline)) {
    run_cycles(avr, 2000U);
  }
}

static avr_t *boot_bios_image(const target_t *t, const char *elf, uint32_t freq) {
  avr_t *avr = build_avr(t, elf, freq);
  g_led_seen = 0;
  g_led_cycle = 0;
  g_dx_output = 0;
  avr_irq_register_notify(pin_irq(avr, t, t->led), on_led, avr);
  avr_irq_t *direction = avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('A'), IOPORT_IRQ_DIRECTION_ALL);
  avr_irq_register_notify(direction, on_dx_direction, avr);
  avr_raise_irq(pin_irq(avr, t, t->sqck), 1U);
  avr_raise_irq(pin_irq(avr, t, t->subq), 0U);
  avr_raise_irq(pin_irq(avr, t, t->wfck), 1U);
  return avr;
}

static void scenario_bios_dead_ax(const char *elf, uint32_t freq) {
  target_t t84 = { "attiny84", 'A', 0U, 1U, 2U, 4U, 3U };
  avr_t *avr = boot_bios_image(&t84, elf, freq);
  avr_raise_irq(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('B'), BIOS_AX_PIN), 0U);
  run_cycles(avr, BIOS_DEAD_AX_CYCLES);
  run_toc_until_led(avr, &t84);
  check(g_led_seen != 0, "attiny84 bios dead AX: falls through to SCEx injection");
  check(g_dx_output == 0, "attiny84 bios dead AX: never overrides the data bus");
}

static void scenario_bios_stalled_train(const char *elf, uint32_t freq) {
  target_t t84 = { "attiny84", 'A', 0U, 1U, 2U, 4U, 3U };
  avr_t *avr = boot_bios_image(&t84, elf, freq);
  avr_irq_t *ax = avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('B'), BIOS_AX_PIN);
  avr_raise_irq(ax, 0U);
  run_cycles(avr, 3000U);
  avr_raise_irq(ax, 1U);
  run_cycles(avr, 3000U);
  avr_raise_irq(ax, 0U);
  run_cycles(avr, BIOS_CONFIRM_CYCLES);
  for (int p = 0; p < BIOS_PARTIAL_PULSES; p++) {
    avr_raise_irq(ax, 1U);
    run_cycles(avr, 200U);
    avr_raise_irq(ax, 0U);
    run_cycles(avr, 200U);
  }
  run_cycles(avr, BIOS_WDT_REBOOT_CYCLES);
  run_toc_until_led(avr, &t84);
  check(g_led_seen != 0, "attiny84 bios stalled train: watchdog reboot skips the patch, SCEx runs");
  check(g_dx_output == 0, "attiny84 bios stalled train: never overrides the data bus");
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
  { "PU-7 to PU-20 legacy gate", 0, WFCK_HZ },
  { "PU-22 to PM-41 carrier 7.3kHz", 1, WFCK_HZ },
  { "PU-22 to PM-41 carrier 14.6kHz", 1, WFCK_READ_HZ },
};
#define FAMILY_COUNT ((int)(sizeof(FAMILIES) / sizeof(FAMILIES[0])))

int main(int argc, char *argv[]) {
  if (argc < 4) {
    (void)fprintf(stderr, "usage: %s elf85 elf84 freq_hz [elf84bios]\n", argv[0]);
    return 2;
  }
  const char *elf85 = argv[1];
  const char *elf84 = argv[2];
  uint32_t freq = (uint32_t)strtoul(argv[3], NULL, 10);

  // Both chips run the same SCEx stealth firmware on different ports (85 PORTB,
  // 84 PORTA). Each image is driven through the whole board-family matrix plus a
  // non-TOC negative, so every family and both ends of the WFCK carrier band are
  // verified per chip. The 84's BIOS patch has its own scenario.
  target_t t85 = { "attiny85", 'B', 0U, 1U, 2U, 3U, 4U };
  target_t t84 = { "attiny84", 'A', 0U, 1U, 2U, 4U, 3U };

  const target_t *images[2] = { &t85, &t84 };
  const char *elfs[2] = { elf85, elf84 };
  for (int chip = 0; chip < 2; chip++) {
    for (int f = 0; f < FAMILY_COUNT; f++) {
      scenario_inject(images[chip],
                      elfs[chip],
                      freq,
                      FAMILIES[f].modern,
                      1,
                      FAMILIES[f].wfck_hz,
                      FAMILIES[f].name);
    }
    scenario_inject(images[chip], elfs[chip], freq, 0, 0, WFCK_HZ, "legacy non-TOC negative");
  }

  if (freq == 8000000U) {
    scenario_diag(&t85, elf85, freq, 0);
    scenario_diag(&t84, elf84, freq, 0);
    scenario_diag(&t85, elf85, freq, 1);
    scenario_diag(&t84, elf84, freq, 1);
    scenario_diag_two_sessions(&t85, elf85, freq);
    scenario_resync(&t85, elf85, freq);
    scenario_resync(&t84, elf84, freq);
    scenario_late_carrier(&t85, elf85, freq);
    scenario_late_carrier(&t84, elf84, freq);
    scenario_stall(&t85, elf85, freq);
    scenario_stall(&t84, elf84, freq);
  }

  // The optional fourth and fifth arguments are the ATtiny84 BIOS images,
  // single-phase then two-phase; both are slow, so simtest passes them at one
  // clock only.
  if (argc >= 5) {
    scenario_bios(argv[4], freq);
    scenario_bios_dead_ax(argv[4], freq);
    scenario_bios_stalled_train(argv[4], freq);
  }
  if (argc >= 6) {
    scenario_bios_two_phase(argv[5], freq);
  }

  (void)printf("%d checks, %d failures\n", g_checks, g_failures);
  return (g_failures == 0) ? 0 : 1;
}
