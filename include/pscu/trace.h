// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_TRACE_H
#define PSCU_TRACE_H

#include <stdbool.h>
#include <stdint.h>

#include "pscu/calib.h"
#include "pscu/indicator.h"
#include "pscu/inject.h"

// The trace profile's text, kept pure so the host suite checks every line.
// PROFILE=trace sends, at 115200 baud 8N1, the boot report as six event lines,
// then after every captured frame its 24 hex digits and at most one queued
// event line, so the chip's decisions arrive beside the frames that caused
// them. tools/serial_trace.py turns a captured log into a timeline and a
// diagnosis.

// Longest line any formatter writes, newline included: a held window at
// 18 hours, "@65535.999 HOLD FF FF 65535mV\n", is 30 characters.
#define PSCU_TRACE_LINE_MAX ((uint8_t)32U)

// Lines in the boot report (pscu_trace_boot_line).
#define PSCU_TRACE_BOOT_LINES ((uint8_t)6U)

// Events waiting for a gap. One leaves per frame and a pass rarely queues more
// than three, as when a disc is resolved, learned and stored; past this many
// they are counted as lost, which keeps the queue's RAM small on a 512-byte
// part. A power of two, so the ring index wraps with a mask.
#define PSCU_TRACE_QUEUE ((uint8_t)4U)
#define PSCU_TRACE_QUEUE_MASK ((uint8_t)(PSCU_TRACE_QUEUE - 1U))
_Static_assert((PSCU_TRACE_QUEUE & PSCU_TRACE_QUEUE_MASK) == 0U,
               "the trace queue length is a power of two");

// Why the window shut, sent as one letter: P a program-area frame, G the disc
// leaving, D the SUBQ counter falling back below the start.
#define PSCU_TRACE_SHUT_PROGRAM ((uint8_t)0x50U)
#define PSCU_TRACE_SHUT_GONE ((uint8_t)0x47U)
#define PSCU_TRACE_SHUT_DROPPED ((uint8_t)0x44U)

// Time since power-on, split so every number on a line prints with 16-bit
// arithmetic: whole seconds, saturating after 18 hours, and milliseconds.
typedef struct {
  uint16_t s;
  uint16_t ms;
} pscu_trace_time_t;

// What an event reports. Its text is in src/trace.c, pscu_trace_format.
typedef enum {
  PSCU_TRACE_DISC = 0,
  PSCU_TRACE_GONE = 1,
  PSCU_TRACE_OPEN = 2,
  PSCU_TRACE_HOLD = 3,
  PSCU_TRACE_SHUT = 4,
  PSCU_TRACE_SCEX = 5,
  PSCU_TRACE_PASS = 6,
  PSCU_TRACE_FAIL = 7,
  PSCU_TRACE_MISS = 8,
  PSCU_TRACE_SUP_OK = 9,
  PSCU_TRACE_SUP_LOW = 10,
  PSCU_TRACE_REC = 11,
  PSCU_TRACE_SAVE_OK = 12,
  PSCU_TRACE_SAVE_NO = 13,
  PSCU_TRACE_OSC = 14,
  PSCU_TRACE_LOST = 15,
  PSCU_TRACE_BOOT = 16,
  PSCU_TRACE_RST_PWR = 17,
  PSCU_TRACE_RST_WDT = 18,
  PSCU_TRACE_BRD = 19
} pscu_trace_kind_t;

// One event: when, what, and up to three values whose meaning depends on the
// kind; letters are chosen when the event is queued.
typedef struct {
  pscu_trace_time_t at;
  pscu_trace_kind_t kind;
  uint8_t a;
  uint8_t b;
  uint16_t c;
} pscu_trace_event_t;

// A store this pass: none tried, every try written, or one refused.
typedef enum {
  PSCU_TRACE_STORE_NONE = 0,
  PSCU_TRACE_STORE_DONE = 1,
  PSCU_TRACE_STORE_REFUSED = 2
} pscu_trace_store_t;

// What one run-loop pass decided and observed, gathered by run.c.
typedef struct {
  uint32_t elapsed_ms;
  pscu_window_t window;
  bool fire;
  bool program;
  bool disc_gone;
  pscu_indicator_event_t event;
  bool missed;
  bool supply_ok;
  uint8_t counter;
  uint8_t sent;
  uint8_t board;
  pscu_calib_t calib;
  uint8_t osccal;
  pscu_trace_store_t store;
  uint16_t supply_raw;
} pscu_trace_pass_t;

// The queue and what the last pass looked like, so only changes are told.
typedef struct {
  pscu_trace_event_t queue[PSCU_TRACE_QUEUE];
  uint8_t head;
  uint8_t count;
  uint8_t lost;
  pscu_trace_time_t now;
  pscu_window_t window;
  bool gone;
  bool supply_ok;
  pscu_calib_t calib;
  uint8_t osccal;
} pscu_trace_t;

// What the boot report says: the build, the reset, the board and how it was
// chosen, the oscillator, the record read from EEPROM, and the supply.
typedef struct {
  uint16_t build_id;
  uint8_t region;
  bool vcd_filter;
  bool watchdog;
  uint8_t board;
  bool board_kept;
  bool board_changed;
  uint8_t factory;
  uint8_t osccal;
  pscu_calib_t calib;
  uint16_t supply_raw;
} pscu_trace_boot_t;

void pscu_trace_init(pscu_trace_t *trace, pscu_calib_t calib, uint8_t osccal);

// Fold one pass in: advance the clock and queue an event for each change.
void pscu_trace_pass(pscu_trace_t *trace, const pscu_trace_pass_t *pass);

// Take the oldest event, or a LOST event once the queue has drained after an
// overflow. False when nothing is waiting.
bool pscu_trace_pop(pscu_trace_t *trace, pscu_trace_event_t *event);

// Each writes one line, newline included, into line and returns its length.
// line holds at least PSCU_TRACE_LINE_MAX bytes.
uint8_t pscu_trace_format(const pscu_trace_event_t *event, uint8_t *line);
uint8_t pscu_trace_format_frame(const uint8_t *frame, uint8_t *line);
// Line index of the boot report, 0 to PSCU_TRACE_BOOT_LINES - 1, at time zero.
uint8_t pscu_trace_boot_line(const pscu_trace_boot_t *boot, uint8_t index, uint8_t *line);

#endif
