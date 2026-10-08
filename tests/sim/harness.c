// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "harness.h"

#include <stdio.h>
#include <string.h>

#include "avr_eeprom.h"
#include "avr_ioport.h"
#include "sim_cycle_timers.h"
#include "sim_elf.h"

// The console model every scenario drives: the frame clock, the WFCK
// carrier and the LED recorder, plus the DATA decoder.

const char SCEA_BITS[SCEX_BITS + 1] = "10011010100100111101001010111010010111110100";
const char SCEI_BITS[SCEX_BITS + 1] = "10011010100100111101001010111010010110110100";

int g_checks = 0;
int g_failures = 0;
int g_led_seen = 0;
uint64_t g_led_cycle = 0;
uint64_t g_pulse_rise[MAX_PULSES];
uint64_t g_pulse_len[MAX_PULSES];
int g_pulses = 0;
uint64_t g_rise = 0;
int g_strings = 0;
uint32_t g_freq = 8000000U;
// The SQCK half-period the harness clocks frames with, normally EDGE_NS.
uint64_t g_edge_ns = EDGE_NS;
// The data address the static data ends at in the image now running, read from
// its ELF symbols, and the fewest free bytes of RAM any run has left between
// the stack and that data.
static uint32_t g_ram_floor = 0U;
static int64_t g_min_free = INT64_MAX;
// When nonzero, frames start this many nanoseconds apart, a console's fixed
// sector rate; when zero, each frame is followed by FRAME_GAP_CYCLES.
uint64_t g_frame_period_ns = 0U;

// EEPROM contents for the next build_avr to load before the firmware starts,
// since the firmware reads its calibration record within its first cycles.
#define SIM_SEED_MAX 16U
static uint8_t g_seed[SIM_SEED_MAX];
static uint8_t g_seed_len = 0U;

void sim_seed_eeprom(const uint8_t *raw, uint8_t size) {
  uint8_t n = (size < SIM_SEED_MAX) ? size : (uint8_t)SIM_SEED_MAX;
  memcpy(g_seed, raw, n);
  g_seed_len = n;
}

void check(int cond, const char *name) {
  g_checks++;
  if (!cond) {
    g_failures++;
    (void)printf("FAIL: %s\n", name);
  }
}

uint64_t ms_cycles(uint32_t ms) {
  return ((uint64_t)g_freq * ms) / 1000U;
}

uint64_t ns_cycles(uint64_t ns) {
  return ((uint64_t)g_freq * ns) / 1000000000ULL;
}

// The first LED rise after boot marks the first region string, which the
// scenarios that decode or interrupt a string start from; nothing else lights
// the LED that early after boot. Every pulse is also recorded, and one whose
// length matches a region string is counted as one.
void on_led(struct avr_irq_t *irq, uint32_t value, void *param) {
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
  // A string lights the LED for 90 to 181 ms. The 40 ms heartbeat blip can
  // stretch by one loop pass, about 31 ms when no frame arrives, so it stays
  // under about 75 ms; 80 ms separates the two.
  if ((g_rise >= DETECT_CYCLES) && (len >= ms_cycles(80U)) && (len <= ms_cycles(200U))) {
    g_strings++;
  }
  g_rise = 0U;
}

// Count the first group of pulses at or after `from` whose length lies in
// [min_ms, max_ms], a group ending where the next such pulse starts more than
// 1.5 s after the previous one. A code is that many long flashes one second
// apart, then a 2 s pause, so this reads one repetition of a code.
int pulse_group(uint64_t from, uint32_t min_ms, uint32_t max_ms) {
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

int code_after(uint64_t from) {
  return pulse_group(from, 600U, 800U);
}

avr_cycle_count_t wfck_tick(avr_t *avr, avr_cycle_count_t when, void *param) {
  wfck_ctx_t *ctx = param;
  (void)avr;
  ctx->level = (uint8_t)(ctx->level ^ 1U);
  avr_raise_irq(ctx->irq, ctx->level);
  return when + ctx->half;
}

void run_to(avr_t *avr, uint64_t target) {
  while (avr->cycle < target) {
    int state = avr_run(avr);
    uint32_t sp = (uint32_t)avr->data[SIM_SPL_ADDR] | ((uint32_t)avr->data[SIM_SPH_ADDR] << 8);
    int64_t free_bytes = (int64_t)sp - (int64_t)g_ram_floor;
    g_min_free = (free_bytes < g_min_free) ? free_bytes : g_min_free;
    if ((state == cpu_Crashed) || (state == cpu_Done)) {
      break;
    }
  }
}

void run_cycles(avr_t *avr, uint64_t n) {
  run_to(avr, avr->cycle + n);
}

avr_irq_t *pin_irq(avr_t *avr, const target_t *t, uint8_t pin) {
  return avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ((uint32_t)t->port), pin);
}

// The firmware's port init runs within its first cycles; letting it run before
// the scenario starts keeps every scenario's pins in their post-init state.
#define PORT_INIT_CYCLES 2000U

// The end of the static data, the highest of the linker's __data_end and
// __bss_end symbols. Data addresses carry the 0x800000 offset avr-ld uses for
// the data space, which is masked off. An image with neither symbol is refused
// with a floor at the top of RAM, so the stack check fails rather than passing.
static uint32_t ram_floor(const elf_firmware_t *firmware) {
  uint32_t floor = 0U;
  int found = 0;
  for (uint32_t i = 0U; i < firmware->symbolcount; i++) {
    const avr_symbol_t *symbol = firmware->symbol[i];
    if ((strcmp(symbol->symbol, "__data_end") == 0) || (strcmp(symbol->symbol, "__bss_end") == 0)) {
      uint32_t addr = symbol->addr & 0xFFFFU;
      floor = (addr > floor) ? addr : floor;
      found = 1;
    }
  }
  return (found != 0) ? floor : 0xFFFFU;
}

avr_t *build_avr(const target_t *t, const char *elf, uint32_t freq) {
  elf_firmware_t firmware;
  memset(&firmware, 0, sizeof(firmware));
  (void)elf_read_firmware(elf, &firmware);
  g_ram_floor = ram_floor(&firmware);

  avr_t *avr = avr_make_mcu_by_name(t->mcu);
  avr_init(avr);
  avr_load_firmware(avr, &firmware);
  sim_t85_install(avr);
  if (g_seed_len > 0U) {
    avr_eeprom_desc_t desc = { .ee = g_seed, .offset = 0, .size = g_seed_len };
    (void)avr_ioctl(avr, AVR_IOCTL_EEPROM_SET, &desc);
    g_seed_len = 0U;
  }
  avr->frequency = freq;
  g_freq = freq;
  g_led_seen = 0;
  g_led_cycle = 0U;
  g_pulses = 0;
  g_rise = 0U;
  g_strings = 0;
  run_cycles(avr, PORT_INIT_CYCLES);
  return avr;
}

uint8_t data_ddr(avr_t *avr, const target_t *t) {
  avr_ioport_state_t state;
  (void)avr_ioctl(avr, AVR_IOCTL_IOPORT_GETSTATE((uint32_t)t->port), &state);
  return (uint8_t)((state.ddr >> t->data) & 1U);
}

uint8_t data_pin(avr_t *avr, const target_t *t) {
  avr_ioport_state_t state;
  (void)avr_ioctl(avr, AVR_IOCTL_IOPORT_GETSTATE((uint32_t)t->port), &state);
  return (uint8_t)((state.port >> t->data) & 1U) & (uint8_t)((state.ddr >> t->data) & 1U);
}

void clock_frame(avr_t *avr, const target_t *t, const uint8_t *frame) {
  avr_irq_t *sqck = pin_irq(avr, t, t->sqck);
  avr_irq_t *subq = pin_irq(avr, t, t->subq);
  uint64_t frame_start = avr->cycle;
  // An injection can start while a frame is being clocked: before each string on
  // a gate board the firmware watches WFCK again for a few milliseconds, so the
  // string may begin after the next frame has started. The console keeps
  // clocking SUBQ then, and the chip ignores it, but the decoder samples DATA
  // forward from the LED edge, so the rest of the frame is dropped the moment the
  // LED marks a string; clocking it to the end would run past the first cells.
  int led_before = g_led_seen;
  for (int byte = 0; (byte < SUBQ_FRAME_BYTES) && !((g_led_seen != 0) && (led_before == 0));
       byte++) {
    for (int bit = 0; (bit < SUBQ_BITS) && !((g_led_seen != 0) && (led_before == 0)); bit++) {
      avr_raise_irq(subq, (uint8_t)((frame[byte] >> bit) & 1U));
      avr_raise_irq(sqck, 0U);
      run_cycles(avr, ns_cycles(g_edge_ns));
      avr_raise_irq(sqck, 1U);
      run_cycles(avr, ns_cycles(g_edge_ns));
    }
  }
  // Idle the clock for the inter-frame gap, but stop early the moment the LED
  // marks the start of an injection inside it, for the same reason.
  uint64_t paced = frame_start + ns_cycles(g_frame_period_ns);
  uint64_t gap_end = (g_frame_period_ns != 0U) ? paced : (avr->cycle + FRAME_GAP_CYCLES);
  while ((avr->cycle < gap_end) && !((g_led_seen != 0) && (led_before == 0))) {
    run_cycles(avr, 500U);
  }
}

// Clock lead-in frames until the first string starts, with the spare frames
// TRIGGER_SPARE_FRAMES allows, and stop there so no frame overlaps the string.
// A frame that would not arm injection is clocked the full count, which only
// makes a negative case stricter.
void clock_until_inject(avr_t *avr, const target_t *t, const uint8_t *frame) {
  for (int i = 0; (i < (TRIGGER_FRAMES + TRIGGER_SPARE_FRAMES)) && (g_led_seen == 0); i++) {
    clock_frame(avr, t, frame);
  }
}

void boot_quiet(avr_t *avr, const target_t *t, int modern, wfck_ctx_t *ctx) {
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
void decode_region(avr_t *avr, const target_t *t, int modern, uint64_t bit_cycles, char *out) {
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

void clock_frames(avr_t *avr, const target_t *t, const uint8_t *frame, int count) {
  for (int i = 0; i < count; i++) {
    clock_frame(avr, t, frame);
  }
}

int strings_while(avr_t *avr, const target_t *t, const uint8_t *frame, int count) {
  int before = g_strings;
  clock_frames(avr, t, frame, count);
  return g_strings - before;
}

void swap_disc(avr_t *avr) {
  run_cycles(avr, MD_SWAP_CYCLES);
}

void check_stack(void) {
  char label[96];
  (void)snprintf(label,
                 sizeof(label),
                 "stack: at least %d bytes of RAM stay free (%lld left)",
                 SIM_STACK_MARGIN,
                 (long long)g_min_free);
  check(g_min_free >= SIM_STACK_MARGIN, label);
}

int sim_report(void) {
  (void)printf("%d checks, %d failures\n", g_checks, g_failures);
  return (g_failures == 0) ? 0 : 1;
}
