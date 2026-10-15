// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_SIM_HARNESS_H
#define PSCU_SIM_HARNESS_H

#include <stdint.h>

#include "sim_avr.h"
#include "sim_irq.h"
#include "sim_t85.h"

// A minimal PlayStation for simavr: it drives SQCK, SUBQ and WFCK, watches DATA
// and the indicator, and decodes the injected word. Console timing is in real time,
// converted to cycles at the simulated clock, so a fast or slow RC can be run.

#define SUBQ_FRAME_BYTES 12
#define SUBQ_BITS 8
// Half-period of the SQCK clock we fake while shifting a SUBQ frame: 14.2 us.
#define EDGE_NS 14170ULL
// A fast SQCK the firmware must still follow: 4.7 us per half period.
#define FAST_EDGE_NS 4720ULL
// Idle-high SQCK after each frame: 9.4 ms, inside the ~13.3 ms of a 75 Hz
// sector and above the firmware's 1 ms resync gap.
#define FRAME_GAP_CYCLES ns_cycles(9450000ULL)
// Boot time before the first frame: the 300 ms WFCK settle, the ~0.4 s detect
// window and up to two 300 ms board blinks.
#define DETECT_CYCLES ns_cycles(1700000000ULL)
// A carrier that starts 283 ms after power-on, inside the 300 ms settle time.
#define LATE_CARRIER_CYCLES ns_cycles(283000000ULL)
#define TRIGGER_FRAMES 10
// A frame that starts while the chip is still qualifying the idle gap is
// skipped, so the harness allows a few spare lead-in frames.
#define TRIGGER_SPARE_FRAMES 3
#define SCEX_BITS 44
// One 4 ms SCEx bit cell, in cycles.
#define BIT_CYCLES(freq) ((uint64_t)(freq) / 250U)
// WFCK frequencies: ~7.3 kHz during init and ~14.6 kHz during a 2x read; both
// ends of the band are tested.
#define WFCK_HZ 7300UL
#define WFCK_READ_HZ 14600UL
// A carrier bit cell is this many WFCK periods (PsNee).
#define WFCK_PERIODS_PER_BIT 30UL
// How long to wait for an expected string to start: 236 ms.
#define INDICATOR_DEADLINE ns_cycles(236000000ULL)

typedef struct {
  const char *mcu;
  char port;
  uint8_t sqck;
  uint8_t subq;
  uint8_t data;
  uint8_t indicator;
  uint8_t wfck;
} target_t;

// The America (SCEA) word as the 44 DATA levels the firmware emits, LSB first.
extern const char SCEA_BITS[SCEX_BITS + 1];

// The Japan (SCEI) word, emitted by the Japanese and SCPH-5903 Video-CD builds.
extern const char SCEI_BITS[SCEX_BITS + 1];

// The Europe (SCEE) word. The three words differ only in their fifth byte:
// 0xFA, 0xEA and 0xDA, LSB first (PsNee V9.0 PSNee.ino L577-L580).
extern const char SCEE_BITS[SCEX_BITS + 1];

// How many DATA direction changes and bursts a run keeps for decoding: 16
// strings of at most 44 changes each, with room to spare.
#define MAX_DDR_LOG 4096
#define MAX_BURSTS 64

// Every indicator pulse as rise cycle and length. A string lights the indicator for 90 to
// 181 ms, so a pulse of 80 to 200 ms after boot counts as one.
#define MAX_PULSES 512

// The stack pointer, SPL at 0x5D and SPH at 0x5E (datasheet 2586Q, register
// summary). The lowest stack must stay SIM_STACK_MARGIN bytes above the static
// data; the measured worst case leaves 229.
#define SIM_SPL_ADDR 0x5DU
#define SIM_SPH_ADDR 0x5EU
#define SIM_STACK_MARGIN 96
// The 75 Hz single-speed sector rate a console reads the lead-in at.
#define SIM_SECTOR_NS 13333333ULL

// How long a swap leaves SUBQ silent: 2 s, past the 1.5 s disc-gone bound.
#define MD_SWAP_CYCLES ns_cycles(2000000000ULL)

typedef struct {
  avr_irq_t *irq;
  uint8_t level;
  uint32_t half;
} wfck_ctx_t;

// Harness state shared by every scenario.
extern int g_checks;
extern int g_failures;
extern int g_indicator_seen;
extern uint64_t g_indicator_cycle;
extern uint64_t g_pulse_rise[MAX_PULSES];
extern uint64_t g_pulse_len[MAX_PULSES];
extern int g_pulses;
extern uint64_t g_rise;
// Strings counted from DATA's direction bit; DATA drives after boot; DATA
// bursts that are not string-shaped.
extern int g_strings;
extern int g_data_drives;
extern int g_data_strays;
// DATA bursts, and those that began with the lamp already lit.
extern int g_data_bursts;
extern int g_lit_bursts;
// When set, build_avr calls this once the image is loaded and before its first
// instruction, so a scenario can hook a register the boot code writes.
extern void (*g_on_build)(avr_t *avr, void *param);
extern void *g_on_build_param;
// EEPROM writes started, and those started with EEPM1:0 not 00.
extern int g_eeprom_writes;
extern int g_eeprom_mode_bad;
extern uint32_t g_freq;
extern uint64_t g_edge_ns;
extern uint64_t g_frame_period_ns;
extern int g_seal_frames;
// Flash addresses of two port functions, read from the image's symbols so a
// scenario can time firmware code; zero when absent.
extern uint32_t g_addr_read_sqck;
extern uint32_t g_addr_capture;
extern uint32_t g_addr_read_wfck;

void check(int cond, const char *name);
uint64_t ms_cycles(uint32_t ms);
uint64_t ns_cycles(uint64_t ns);
void on_indicator(struct avr_irq_t *irq, uint32_t value, void *param);
int pulse_group(uint64_t from, uint32_t min_ms, uint32_t max_ms);
int code_after(uint64_t from);
avr_cycle_count_t wfck_tick(avr_t *avr, avr_cycle_count_t when, void *param);
void sim_step(avr_t *avr);
void run_to(avr_t *avr, uint64_t target);
void run_cycles(avr_t *avr, uint64_t n);
avr_irq_t *pin_irq(avr_t *avr, const target_t *t, uint8_t pin);
avr_t *build_avr(const target_t *t, const char *elf, uint32_t freq);
uint8_t data_ddr(avr_t *avr, const target_t *t);
uint8_t data_pin(avr_t *avr, const target_t *t);
void clock_frame(avr_t *avr, const target_t *t, const uint8_t *frame);
void clock_until_inject(avr_t *avr, const target_t *t, const uint8_t *frame);
void clock_frames(avr_t *avr, const target_t *t, const uint8_t *frame, int count);
int strings_while(avr_t *avr, const target_t *t, const uint8_t *frame, int count);
void swap_disc(avr_t *avr);
void boot_quiet(avr_t *avr, const target_t *t, int modern, wfck_ctx_t *ctx);
void decode_region(avr_t *avr, const target_t *t, int modern, uint64_t bit_cycles, char *out);
// The DATA bursts so far that were not one string: shorter than 80 ms, longer
// than 200 ms, or two strings run together. Closes a burst that has ended.
int data_strays(const avr_t *avr);
// Decode every gate-board DATA burst of the run into a 44-bit word, one sample
// per bit cell from the lamp rise that opened it, and count those equal to
// `expect`.
int decode_bursts(uint64_t bit_cycles, const char *expect);
// Whether a cycle falls inside a DATA burst, from `lead` cycles before its first
// drive to its last release; a string opens with a high-Z cell, so pass one bit.
int in_burst(uint64_t cycle, uint64_t lead);

// Check that the stack never came within SIM_STACK_MARGIN bytes of the data.
void check_stack(void);
// The process exit status for the run: 0 when every check passed.
int sim_report(void);

// Load these bytes into EEPROM at the next build_avr, before the firmware runs.
void sim_seed_eeprom(const uint8_t *raw, uint8_t size);

#endif
