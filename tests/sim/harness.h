// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_SIM_HARNESS_H
#define PSCU_SIM_HARNESS_H

#include <stdint.h>

#include "sim_avr.h"
#include "sim_irq.h"

// A software PlayStation, just enough to exercise the firmware end to end in
// simavr: it drives the console-side pins (SQCK, SUBQ, WFCK) and watches the
// firmware-side pins (DATA, LED), then decodes the injected bitstream and
// checks it equals the expected region word. A disc swap is a stretch with no
// SUBQ frame, as a drive that stops for the lid produces. The console side is
// timed in real time, converted to CPU cycles at the rate the simulated chip runs, so the same
// console drives a nominal 8 MHz chip or one whose RC runs fast or slow.

#define SUBQ_FRAME_BYTES 12
#define SUBQ_BITS 8
// Half-period of the SQCK clock we fake while shifting a SUBQ frame: 14.2 us.
#define EDGE_NS 14170ULL
// A fast SQCK the firmware must still follow: 4.7 us per half period. The
// assembly capture followed down to about 2.8 us at 4.2336 MHz; the earlier C
// capture failed below about 13 us, so this frame rate pins the margin.
#define FAST_EDGE_NS 4720ULL
// Idle-high SQCK gap after each frame. A console clocks one frame per sector at
// 75 Hz, so bursts are separated by most of ~13.3 ms; the firmware resyncs on a
// gap of at least 1 ms, and 9.4 ms sits clearly inside the real gap.
#define FRAME_GAP_CYCLES ns_cycles(9450000ULL)
// Time to let the firmware's boot complete before the first frame: the 300 ms
// WFCK settle and the 10000-sample window under the boot light (about 0.4 s),
// then the board blinks, at most two 300 ms flashes with their gaps (1.2 s).
// 1.7 s covers both with margin.
#define DETECT_CYCLES ns_cycles(1700000000ULL)
// A carrier that starts this long after power-on (283 ms) is still inside the
// 300 ms settle time, so it must be detected as a carrier board.
#define LATE_CARRIER_CYCLES ns_cycles(283000000ULL)
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
// How long to wait for an expected string to start: 236 ms.
#define LED_DEADLINE ns_cycles(236000000ULL)

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
extern const char SCEA_BITS[SCEX_BITS + 1];

// The Japan (SCEI) word, which the SCPH-5903 Video-CD build emits: that console
// is NTSC-J, so its image is built with REGION=jp.
extern const char SCEI_BITS[SCEX_BITS + 1];

// Every LED pulse the firmware draws, as rise cycle and length. A region string
// lights the LED for 90 to 181 ms, every status pattern for longer or shorter, so
// a pulse of 80 to 200 ms that starts after boot counts as one string.
#define MAX_PULSES 512

// OSCCAL, the oscillator calibration register, at data address 0x51 (Read:
// ATtiny25/45/85 datasheet 2586Q, register summary, 0x31 (0x51)). simavr does not
// load a factory value, so the harness presets a mid-range one the firmware
// reads as factory at boot; writes to it do not change simavr's speed.
#define SIM_OSCCAL_ADDR 0x51U
// Timer1 on the ATtiny85: TCCR1 at 0x50 and TCNT1 at 0x4F (Read: ATtiny25/45/85
// datasheet 2586Q, register summary). simavr's ATtiny85 model does not run this
// timer at any prescaler (measured: TCNT1 held one value through 100000 cycles
// for every CS setting), so the harness answers TCNT1 reads itself, from the
// cycle count and the prescaler TCCR1 selects, as the datasheet describes for
// the synchronous clocking mode: CK divided by 2^(CS - 1).
#define SIM_TCCR1_ADDR 0x50U
#define SIM_TCNT1_ADDR 0x4FU
#define SIM_OSCCAL_FACTORY 0x50U
// The stack pointer, SPL at data address 0x5D and SPH at 0x5E (Read: ATtiny25/45/85
// datasheet 2586Q, register summary, 0x3D (0x5D) and 0x3E (0x5E)). The ATtiny85
// has 512 bytes of SRAM, no heap, and no other RAM user than the static data the
// linker places at its bottom, so the stack can grow down only as far as the end
// of that data. The harness watches the stack pointer on every instruction and
// requires the lowest it reaches to stay this many bytes above the data: the
// worst case measured across the whole suite left 229 bytes (2026-10-07), so 96
// still catches a change that eats most of the margin, before it corrupts data.
#define SIM_SPL_ADDR 0x5DU
#define SIM_SPH_ADDR 0x5EU
#define SIM_STACK_MARGIN 96
// The 75 Hz single-speed sector rate a console reads the lead-in at.
#define SIM_SECTOR_NS 13333333ULL

// How long a swap leaves SUBQ silent: 2 s, the drive stopped while the lid is
// open, beyond the firmware's 1.5 s disc-gone bound and shorter than a person
// takes to change a disc.
#define MD_SWAP_CYCLES ns_cycles(2000000000ULL)

typedef struct {
  avr_irq_t *irq;
  uint8_t level;
  uint32_t half;
} wfck_ctx_t;

// Harness state shared by every scenario: the check tally, the first LED rise
// after boot that marks a string, every LED pulse, the string count, the clock
// the image runs at, and the SQCK half-period frames are clocked with.
extern int g_checks;
extern int g_failures;
extern int g_led_seen;
extern uint64_t g_led_cycle;
extern uint64_t g_pulse_rise[MAX_PULSES];
extern uint64_t g_pulse_len[MAX_PULSES];
extern int g_pulses;
extern uint64_t g_rise;
extern int g_strings;
extern uint32_t g_freq;
extern uint64_t g_edge_ns;
extern uint64_t g_frame_period_ns;

void check(int cond, const char *name);
uint64_t ms_cycles(uint32_t ms);
uint64_t ns_cycles(uint64_t ns);
void on_led(struct avr_irq_t *irq, uint32_t value, void *param);
int pulse_group(uint64_t from, uint32_t min_ms, uint32_t max_ms);
int code_after(uint64_t from);
avr_cycle_count_t wfck_tick(avr_t *avr, avr_cycle_count_t when, void *param);
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

// The process exit status for the run: 0 when every check passed.
// Check that the stack never came within SIM_STACK_MARGIN bytes of the static
// data in any scenario run so far.
void check_stack(void);
int sim_report(void);

// Load these bytes into EEPROM at the next build_avr, before the firmware runs.
void sim_seed_eeprom(const uint8_t *raw, uint8_t size);

#endif
