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

// The lid line: PB1, read high while the lid is open. The harness drives it on
// every boot, closed, so each scenario starts as a console with a disc in.
#define LID_PORT 'B'
#define LID_PIN 1

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
// a pulse of 60 to 200 ms that starts after boot counts as one string.
#define MAX_PULSES 512

// How long the lid stays open on a swap: 236 ms, far shorter than a person
// takes, so the re-arm cannot depend on the drive stopping.
#define MD_LID_OPEN_CYCLES 1000000UL

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
extern uint64_t g_edge_cycles;

void check(int cond, const char *name);
uint64_t ms_cycles(uint32_t ms);
void on_led(struct avr_irq_t *irq, uint32_t value, void *param);
int pulse_group(uint64_t from, uint32_t min_ms, uint32_t max_ms);
int code_after(uint64_t from);
avr_cycle_count_t wfck_tick(avr_t *avr, avr_cycle_count_t when, void *param);
void run_to(avr_t *avr, uint64_t target);
void run_cycles(avr_t *avr, uint64_t n);
avr_irq_t *pin_irq(avr_t *avr, const target_t *t, uint8_t pin);
avr_irq_t *lid_irq(avr_t *avr);
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
int sim_report(void);

#endif
