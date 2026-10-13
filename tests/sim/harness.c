// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "harness.h"

#include <stdio.h>
#include <string.h>

#include "avr_eeprom.h"
#include "avr_ioport.h"
#include "sim_cycle_timers.h"
#include "sim_elf.h"
#include "sim_io.h"

// The console model every scenario drives: the frame clock, the WFCK
// carrier and the indicator recorder, plus the DATA decoder.

const char SCEA_BITS[SCEX_BITS + 1] = "10011010100100111101001010111010010111110100";
const char SCEI_BITS[SCEX_BITS + 1] = "10011010100100111101001010111010010110110100";
const char SCEE_BITS[SCEX_BITS + 1] = "10011010100100111101001010111010010101110100";

int g_checks = 0;
int g_failures = 0;
int g_indicator_seen = 0;
uint64_t g_indicator_cycle = 0;
uint64_t g_pulse_rise[MAX_PULSES];
uint64_t g_pulse_len[MAX_PULSES];
int g_pulses = 0;
uint64_t g_rise = 0;
int g_strings = 0;
int g_data_drives = 0;
int g_data_strays = 0;
int g_data_bursts = 0;
int g_lit_bursts = 0;
int g_eeprom_writes = 0;
int g_eeprom_mode_bad = 0;
// Every change of DATA's direction bit after boot, and where each burst began,
// so every string of a burst can be decoded after the run.
static uint64_t g_ddr_at[MAX_DDR_LOG];
static uint8_t g_ddr_level[MAX_DDR_LOG];
static int g_ddr_count = 0;
static uint64_t g_burst_start[MAX_BURSTS];
static uint64_t g_burst_end[MAX_BURSTS];
uint32_t g_freq = 8000000U;
// The SQCK half-period the harness clocks frames with, normally EDGE_NS.
uint64_t g_edge_ns = EDGE_NS;
// The end of the static data in the running image, and the fewest free bytes
// any run has left between it and the stack.
static uint32_t g_ram_floor = 0U;
static int64_t g_min_free = INT64_MAX;
// Where two port functions sit in the loaded image (see harness.h).
uint32_t g_addr_read_sqck = 0U;
uint32_t g_addr_capture = 0U;
// When nonzero, frames start this many nanoseconds apart; when zero, each frame
// is followed by FRAME_GAP_CYCLES.
uint64_t g_frame_period_ns = 0U;
// When zero, frames go out unsealed, so a scenario can send a bad CRC.
int g_seal_frames = 1;

// EEPROM contents for the next build_avr to load before the firmware starts.
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

// DATA as the console sees it, read from its direction bit: a string is the only
// time the chip drives DATA, so strings are counted here, not from the lamp. On a
// gate board a one cell is high-Z, so a string is a run of drives with gaps of up
// to five one cells, 20 ms; drives closer than DATA_GAP_MS belong to one burst.
// A burst of 80 to 200 ms is a string; anything else is a stray drive.
#define DATA_GAP_MS 40U
#define DATA_STRING_MIN_MS 80U
#define DATA_STRING_MAX_MS 200U
static uint8_t g_data_bit = 0U;
static uint8_t g_lamp_bit = 0U;
static uint32_t g_port = 0U;
static int g_data_driven = 0;
static int g_data_open = 0;
static int g_data_counted = 0;
static uint64_t g_data_start = 0U;
static uint64_t g_data_last = 0U;

// Close the open burst: it was a string, or it was stray.
static void close_burst(void) {
  if (g_data_open == 0) {
    return;
  }
  uint64_t span = g_data_last - g_data_start;
  if ((span < ms_cycles(DATA_STRING_MIN_MS)) || (span > ms_cycles(DATA_STRING_MAX_MS))) {
    g_data_strays++;
  }
  g_data_open = 0;
}

static void on_data_direction(struct avr_irq_t *irq, uint32_t value, void *param) {
  avr_t *avr = param;
  (void)irq;
  int driven = (int)((value >> g_data_bit) & 1U);
  if ((driven == g_data_driven) || (avr->cycle < DETECT_CYCLES)) {
    g_data_driven = driven;
    return;
  }
  g_data_driven = driven;
  if (g_ddr_count < MAX_DDR_LOG) {
    g_ddr_at[g_ddr_count] = avr->cycle;
    g_ddr_level[g_ddr_count] = (uint8_t)driven;
    g_ddr_count++;
  }
  if (driven != 0) {
    g_data_drives++;
    if ((g_data_open != 0) && ((avr->cycle - g_data_last) > ms_cycles(DATA_GAP_MS))) {
      close_burst();
    }
    if (g_data_open == 0) {
      g_data_open = 1;
      g_data_counted = 0;
      g_data_start = avr->cycle;
      if (g_data_bursts < MAX_BURSTS) {
        g_burst_start[g_data_bursts] = avr->cycle;
      }
      g_data_bursts++;
      avr_ioport_state_t state;
      (void)avr_ioctl(avr, AVR_IOCTL_IOPORT_GETSTATE(g_port), &state);
      g_lit_bursts += (int)((state.port >> g_lamp_bit) & 1U);
    }
    return;
  }
  g_data_last = avr->cycle;
  if ((g_data_bursts > 0) && (g_data_bursts <= MAX_BURSTS)) {
    g_burst_end[g_data_bursts - 1] = avr->cycle;
  }
  if ((g_data_counted == 0) && ((g_data_last - g_data_start) >= ms_cycles(DATA_STRING_MIN_MS))) {
    g_strings++;
    g_data_counted = 1;
  }
}

// DATA's direction at a cycle, from the log: the last change at or before it.
static uint8_t logged_ddr(uint64_t cycle) {
  uint8_t level = 0U;
  for (int i = 0; (i < g_ddr_count) && (g_ddr_at[i] <= cycle); i++) {
    level = g_ddr_level[i];
  }
  return level;
}

// The lamp rise that opened the string whose first drive is at `first`: on a
// gate board the lamp lights one bit cell before DATA is first driven, since
// every word starts with a one, which is high-Z. Zero when there is none.
static uint64_t string_start(uint64_t first, uint64_t bit_cycles) {
  for (int i = 0; i < g_pulses; i++) {
    uint64_t rise = g_pulse_rise[i];
    if ((rise <= first) && ((first - rise) <= (bit_cycles + (bit_cycles / 2U)))) {
      return rise;
    }
  }
  return 0U;
}

int decode_bursts(uint64_t bit_cycles, const char *expect) {
  int matched = 0;
  int bursts = (g_data_bursts < MAX_BURSTS) ? g_data_bursts : MAX_BURSTS;
  for (int b = 0; b < bursts; b++) {
    uint64_t start = string_start(g_burst_start[b], bit_cycles);
    if (start == 0U) {
      continue;
    }
    char word[SCEX_BITS + 1];
    for (int k = 0; k < SCEX_BITS; k++) {
      uint64_t mid = start + ((uint64_t)k * bit_cycles) + (bit_cycles / 2U);
      word[k] = (logged_ddr(mid) != 0U) ? '0' : '1';
    }
    word[SCEX_BITS] = '\0';
    matched += (strcmp(word, expect) == 0) ? 1 : 0;
  }
  return matched;
}

// Every EECR write that starts an EEPROM write (EEPE) is checked for the mode
// it starts in: EEPM1:0 must be 00, erase and write, or the byte is only erased
// or only programmed (datasheet 2586Q, EECR). simavr chains this hook after its
// own EEPROM model, so the write itself still happens.
static void on_eecr_write(avr_t *avr, avr_io_addr_t addr, uint8_t value, void *param) {
  (void)avr;
  (void)addr;
  (void)param;
  if ((value & SIM_EECR_EEPE) != 0U) {
    g_eeprom_writes++;
    g_eeprom_mode_bad += ((value & SIM_EECR_EEPM) != 0U) ? 1 : 0;
  }
}

int in_burst(uint64_t cycle, uint64_t lead) {
  int bursts = (g_data_bursts < MAX_BURSTS) ? g_data_bursts : MAX_BURSTS;
  for (int b = 0; b < bursts; b++) {
    if ((cycle + lead >= g_burst_start[b]) && (cycle <= g_burst_end[b])) {
      return 1;
    }
  }
  return 0;
}

int data_strays(const avr_t *avr) {
  if ((g_data_open != 0) && (g_data_driven == 0) &&
      ((avr->cycle - g_data_last) > ms_cycles(DATA_GAP_MS))) {
    close_burst();
  }
  return g_data_strays;
}

// The first indicator rise after boot marks the first region string; every
// pulse is recorded for the indicator-code checks.
void on_indicator(struct avr_irq_t *irq, uint32_t value, void *param) {
  avr_t *avr = param;
  (void)irq;
  if (value != 0U) {
    g_rise = avr->cycle;
    if ((avr->cycle >= DETECT_CYCLES) && (g_indicator_seen == 0)) {
      g_indicator_seen = 1;
      g_indicator_cycle = avr->cycle;
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
  g_rise = 0U;
}

// Count the first group of pulses at or after `from` with a length in
// [min_ms, max_ms]; a gap over 1.5 s ends the group. Reads one repetition of
// an indicator code.
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

// Let the firmware's port init run before the scenario starts.
#define PORT_INIT_CYCLES 2000U

// The end of the static data: the higher of __data_end and __bss_end, without
// avr-ld's 0x800000 data offset. With neither symbol the floor is the top of
// RAM, so the stack check fails.
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

// The flash address of a named function, zero when the image lacks it.
static uint32_t symbol_addr(const elf_firmware_t *firmware, const char *name) {
  for (uint32_t i = 0U; i < firmware->symbolcount; i++) {
    if (strcmp(firmware->symbol[i]->symbol, name) == 0) {
      return firmware->symbol[i]->addr;
    }
  }
  return 0U;
}

avr_t *build_avr(const target_t *t, const char *elf, uint32_t freq) {
  elf_firmware_t firmware;
  memset(&firmware, 0, sizeof(firmware));
  (void)elf_read_firmware(elf, &firmware);
  g_ram_floor = ram_floor(&firmware);
  g_addr_read_sqck = symbol_addr(&firmware, "pscu_port_read_sqck");
  g_addr_capture = symbol_addr(&firmware, "pscu_port_capture_frame");

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
  g_indicator_seen = 0;
  g_indicator_cycle = 0U;
  g_pulses = 0;
  g_rise = 0U;
  g_strings = 0;
  g_data_drives = 0;
  g_data_strays = 0;
  g_data_bursts = 0;
  g_lit_bursts = 0;
  g_ddr_count = 0;
  g_eeprom_writes = 0;
  g_eeprom_mode_bad = 0;
  avr_register_io_write(avr, SIM_EECR_ADDR, on_eecr_write, NULL);
  g_data_bit = t->data;
  g_lamp_bit = t->indicator;
  g_port = (uint32_t)t->port;
  g_data_driven = 0;
  g_data_open = 0;
  g_data_counted = 0;
  avr_irq_register_notify(
      avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ((uint32_t)t->port), IOPORT_IRQ_DIRECTION_ALL),
      on_data_direction,
      avr);
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

// Append the Q frame's CRC: CRC-16 CCITT from zero over the first ten bytes,
// inverted, high byte first.
static void seal_frame(const uint8_t *frame, uint8_t *sealed) {
  uint16_t crc = 0U;
  for (int byte = 0; byte < SUBQ_FRAME_BYTES; byte++) {
    sealed[byte] = frame[byte];
  }
  for (int byte = 0; byte < (SUBQ_FRAME_BYTES - 2); byte++) {
    crc = (uint16_t)(crc ^ (uint16_t)((uint16_t)frame[byte] << 8U));
    for (int bit = 0; bit < 8; bit++) {
      crc = ((crc & 0x8000U) != 0U) ? (uint16_t)((uint16_t)(crc << 1U) ^ 0x1021U)
                                    : (uint16_t)(crc << 1U);
    }
  }
  crc = (uint16_t)(crc ^ 0xFFFFU);
  sealed[SUBQ_FRAME_BYTES - 2] = (uint8_t)(crc >> 8U);
  sealed[SUBQ_FRAME_BYTES - 1] = (uint8_t)(crc & 0xFFU);
}

void clock_frame(avr_t *avr, const target_t *t, const uint8_t *unsealed) {
  uint8_t frame[SUBQ_FRAME_BYTES];
  seal_frame(unsealed, frame);
  if (g_seal_frames == 0) {
    (void)memcpy(frame, unsealed, SUBQ_FRAME_BYTES);
  }
  avr_irq_t *sqck = pin_irq(avr, t, t->sqck);
  avr_irq_t *subq = pin_irq(avr, t, t->subq);
  uint64_t frame_start = avr->cycle;
  // A string can start mid-frame on a gate board. The decoder samples DATA
  // from the indicator edge, so the rest of the frame is dropped once the indicator lights.
  int indicator_before = g_indicator_seen;
  for (int byte = 0;
       (byte < SUBQ_FRAME_BYTES) && !((g_indicator_seen != 0) && (indicator_before == 0));
       byte++) {
    for (int bit = 0; (bit < SUBQ_BITS) && !((g_indicator_seen != 0) && (indicator_before == 0));
         bit++) {
      avr_raise_irq(subq, (uint8_t)((frame[byte] >> bit) & 1U));
      avr_raise_irq(sqck, 0U);
      run_cycles(avr, ns_cycles(g_edge_ns));
      avr_raise_irq(sqck, 1U);
      run_cycles(avr, ns_cycles(g_edge_ns));
    }
  }
  // Idle for the inter-frame gap, stopping early if a string starts.
  uint64_t paced = frame_start + ns_cycles(g_frame_period_ns);
  uint64_t gap_end = (g_frame_period_ns != 0U) ? paced : (avr->cycle + FRAME_GAP_CYCLES);
  while ((avr->cycle < gap_end) && !((g_indicator_seen != 0) && (indicator_before == 0))) {
    run_cycles(avr, 500U);
  }
}

// Clock lead-in frames until the first string starts, plus the spare frames,
// and stop there so no frame overlaps the string.
void clock_until_inject(avr_t *avr, const target_t *t, const uint8_t *frame) {
  for (int i = 0; (i < (TRIGGER_FRAMES + TRIGGER_SPARE_FRAMES)) && (g_indicator_seen == 0); i++) {
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

// Rebuild the 44-bit word from DATA, one sample per bit cell from the indicator
// marker. On a carrier board a one is DATA mirroring WFCK; on a legacy board a
// one is high-Z (DDR clear) and a zero is driven low.
void decode_region(avr_t *avr, const target_t *t, int modern, uint64_t bit_cycles, char *out) {
  for (int k = 0; k < SCEX_BITS; k++) {
    uint64_t base = g_indicator_cycle + ((uint64_t)k * bit_cycles);
    uint8_t bit;
    if (modern != 0) {
      // A carrier one oscillates within the cell, so sample finely across the
      // central 40 percent and treat any high as a one.
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
