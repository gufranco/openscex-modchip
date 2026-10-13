// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "trace_test.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "pscu/board_mode.h"
#include "pscu/calib.h"
#include "pscu/config.h"
#include "pscu/indicator.h"
#include "pscu/inject.h"
#include "pscu/region.h"
#include "pscu/trace.h"

// Host tests for the serial trace text. Every expected line is written out in
// full, as a USB serial adapter would show it, so a change to the wording or a
// value's format fails here first.

static pscu_check_fn g_check;

// 3.3 V on the bandgap scale: 1024 x 1100 mV / 3300 mV, about 341.
#define RAW_3V3 ((uint16_t)341U)
#define MV_3V3 "3303mV"
// 2.6 V, below the 2.75 V guard: about 433.
#define RAW_2V6 ((uint16_t)433U)
#define MV_2V6 "2601mV"

static pscu_calib_t record(uint8_t board, uint8_t trigger, bool frozen, int8_t trim) {
  pscu_calib_t calib = { board, trigger, frozen, trim };
  return calib;
}

// A pass that changes nothing against a freshly initialised trace.
static pscu_trace_pass_t still(void) {
  pscu_trace_pass_t pass;
  pass.elapsed_ms = 0U;
  pass.window = PSCU_WINDOW_CLOSED;
  pass.fire = false;
  pass.program = false;
  pass.disc_gone = false;
  pass.event = PSCU_INDICATOR_EVENT_NONE;
  pass.missed = false;
  pass.supply_ok = true;
  pass.counter = 0U;
  pass.sent = 0U;
  pass.board = (uint8_t)PSCU_BOARD_MODE_WFCK;
  pass.calib = record(1U, 12U, false, 0);
  pass.osccal = 0x50U;
  pass.store = PSCU_TRACE_STORE_NONE;
  pass.supply_raw = RAW_3V3;
  return pass;
}

static pscu_trace_t fresh(void) {
  pscu_trace_t trace;
  pscu_trace_init(&trace, record(1U, 12U, false, 0), 0x50U);
  return trace;
}

static bool same_line(const uint8_t *line, uint8_t len, const char *want) {
  return (len == (uint8_t)strlen(want)) && (memcmp(line, want, len) == 0);
}

// The next queued event, formatted, is exactly want.
static bool next_is(pscu_trace_t *trace, const char *want) {
  pscu_trace_event_t event;
  uint8_t line[PSCU_TRACE_LINE_MAX];
  bool taken = pscu_trace_pop(trace, &event);
  return taken && same_line(line, pscu_trace_format(&event, line), want);
}

static bool drained(pscu_trace_t *trace) {
  pscu_trace_event_t event;
  return !pscu_trace_pop(trace, &event);
}

// Take every queued event; the queue never holds more than its length plus
// the LOST event.
static void clear(pscu_trace_t *trace) {
  for (uint8_t n = 0U; n <= PSCU_TRACE_QUEUE; n++) {
    (void)drained(trace);
  }
}

// One pass from a fresh trace, then its lines in order and nothing more.
static bool tells(const pscu_trace_pass_t *pass, const char *first, const char *second) {
  pscu_trace_t trace = fresh();
  pscu_trace_pass(&trace, pass);
  bool ok = next_is(&trace, first);
  if (second != NULL) {
    ok = ok && next_is(&trace, second);
  }
  return ok && drained(&trace);
}

static void test_frame_line(void) {
  uint8_t frame[12] = { 0x41U, 0x00U, 0xA0U, 0x00U, 0x00U, 0x00U,
                        0x00U, 0x9BU, 0xCDU, 0xEFU, 0x64U, 0x49U };
  uint8_t line[PSCU_TRACE_LINE_MAX];
  uint8_t len = pscu_trace_format_frame(frame, line);
  g_check(same_line(line, len, "4100A0000000009BCDEF6449\n"),
          "trace: a frame line is its 12 bytes in upper-case hex");
}

static void test_still_pass(void) {
  pscu_trace_t trace = fresh();
  pscu_trace_pass_t pass = still();
  pscu_trace_pass(&trace, &pass);
  g_check(drained(&trace), "trace: a pass that changes nothing tells nothing");
}

static void test_presence(void) {
  pscu_trace_pass_t gone = still();
  gone.disc_gone = true;
  gone.elapsed_ms = 1500U;
  g_check(tells(&gone, "@1.500 GONE\n", NULL), "trace: the disc leaving is told with its time");

  pscu_trace_t trace = fresh();
  pscu_trace_pass(&trace, &gone);
  (void)drained(&trace);
  pscu_trace_pass_t back = still();
  back.elapsed_ms = 7U;
  pscu_trace_pass(&trace, &back);
  g_check(next_is(&trace, "@1.507 DISC\n") && drained(&trace),
          "trace: a disc arriving is told, milliseconds padded to three digits");
}

static void test_window(void) {
  pscu_trace_pass_t open = still();
  open.window = PSCU_WINDOW_OPEN;
  open.counter = 0x0CU;
  g_check(tells(&open, "@0.000 OPEN 0C 0C\n", NULL),
          "trace: the window opening gives the counter and the start in hex");

  pscu_trace_pass_t held = still();
  held.window = PSCU_WINDOW_HELD;
  held.counter = 0xABU;
  held.supply_raw = RAW_2V6;
  g_check(tells(&held, "@0.000 HOLD AB 0C " MV_2V6 "\n", NULL),
          "trace: a held window gives the counter, the start and the supply");

  pscu_trace_t trace = fresh();
  pscu_trace_pass(&trace, &open);
  (void)drained(&trace);
  pscu_trace_pass_t program = still();
  program.program = true;
  pscu_trace_pass(&trace, &program);
  g_check(next_is(&trace, "@0.000 SHUT P 00\n") && drained(&trace),
          "trace: a program-area frame shuts the window with reason P");

  pscu_trace_pass(&trace, &open);
  (void)drained(&trace);
  pscu_trace_pass_t gone = still();
  gone.disc_gone = true;
  pscu_trace_pass(&trace, &gone);
  g_check(next_is(&trace, "@0.000 GONE\n") && next_is(&trace, "@0.000 SHUT G 00\n"),
          "trace: the disc leaving shuts the window with reason G");

  pscu_trace_pass_t dropped = still();
  dropped.window = PSCU_WINDOW_OPEN;
  dropped.disc_gone = true;
  pscu_trace_pass(&trace, &dropped);
  (void)drained(&trace);
  dropped.window = PSCU_WINDOW_CLOSED;
  dropped.program = true;
  dropped.counter = 0x03U;
  pscu_trace_pass(&trace, &dropped);
  g_check(next_is(&trace, "@0.000 SHUT G 03\n"), "trace: a gone disc outranks a program frame");

  pscu_trace_t fell = fresh();
  pscu_trace_pass(&fell, &open);
  (void)drained(&fell);
  pscu_trace_pass_t below = still();
  below.counter = 0x02U;
  pscu_trace_pass(&fell, &below);
  g_check(next_is(&fell, "@0.000 SHUT D 02\n") && drained(&fell),
          "trace: the counter falling back shuts the window with reason D");
}

static void test_strings(void) {
  pscu_trace_pass_t first = still();
  first.window = PSCU_WINDOW_OPEN;
  first.counter = 0x0CU;
  first.fire = true;
  first.sent = 1U;
  g_check(tells(&first, "@0.000 OPEN 0C 0C\n", "@0.000 SCEX 1 W\n"),
          "trace: a string gives its number and the board mode");

  pscu_trace_pass_t gate = still();
  gate.fire = true;
  gate.sent = (uint8_t)(PSCU_STEALTH_STRINGS - 1U);
  gate.board = (uint8_t)PSCU_BOARD_MODE_GATE;
  g_check(tells(&gate, "@0.000 SCEX 15 G\n", NULL), "trace: a string below the cap is not the cap");

  pscu_trace_pass_t last = still();
  last.fire = true;
  last.sent = PSCU_STEALTH_STRINGS;
  g_check(tells(&last, "@0.000 SCEX 16 W\n", NULL), "trace: the last string allowed is number 16");
}

static void test_outcomes(void) {
  pscu_trace_pass_t accepted = still();
  accepted.event = PSCU_INDICATOR_EVENT_ACCEPTED;
  g_check(tells(&accepted, "@0.000 PASS\n", NULL), "trace: an accepted disc is PASS");

  pscu_trace_pass_t refused = still();
  refused.event = PSCU_INDICATOR_EVENT_REFUSED;
  g_check(tells(&refused, "@0.000 FAIL\n", NULL), "trace: a refused disc is FAIL");

  pscu_trace_pass_t missed = still();
  missed.event = PSCU_INDICATOR_EVENT_REFUSED;
  missed.missed = true;
  missed.counter = 0x0BU;
  g_check(tells(&missed, "@0.000 MISS 0B 0C\n", NULL),
          "trace: a lead-in that missed the start gives how far it got");

  pscu_trace_pass_t fired = still();
  fired.event = PSCU_INDICATOR_EVENT_FIRED;
  pscu_trace_t trace = fresh();
  pscu_trace_pass(&trace, &fired);
  g_check(drained(&trace), "trace: a string being sent resolves nothing");
}

static void test_supply(void) {
  pscu_trace_pass_t low = still();
  low.supply_ok = false;
  low.supply_raw = RAW_2V6;
  g_check(tells(&low, "@0.000 SUP LOW " MV_2V6 "\n", NULL),
          "trace: the supply falling below the guard is told with its voltage");

  pscu_trace_t trace = fresh();
  pscu_trace_pass(&trace, &low);
  (void)drained(&trace);
  pscu_trace_pass(&trace, &low);
  g_check(drained(&trace), "trace: a supply that stays low is told once");
  pscu_trace_pass_t back = still();
  pscu_trace_pass(&trace, &back);
  g_check(next_is(&trace, "@0.000 SUP OK " MV_3V3 "\n"), "trace: the supply recovering is told");
}

static void test_record(void) {
  pscu_trace_pass_t learned = still();
  learned.calib = record(0U, 0x0EU, true, -3);
  g_check(tells(&learned, "@0.000 REC G 0E F -3\n", NULL),
          "trace: a learned record gives board, start, frozen and trim");

  pscu_trace_pass_t probing = still();
  probing.calib = record(1U, 12U, false, 7);
  g_check(tells(&probing, "@0.000 REC W 0C P 7\n", NULL),
          "trace: a trim alone changing the record is told");

  pscu_trace_pass_t board = still();
  board.calib = record(PSCU_CALIB_BOARD_NONE, 12U, false, 0);
  g_check(tells(&board, "@0.000 REC - 0C P 0\n", NULL),
          "trace: a record with no board shows a dash");

  pscu_trace_pass_t trigger = still();
  trigger.calib = record(1U, 13U, false, 0);
  g_check(tells(&trigger, "@0.000 REC W 0D P 0\n", NULL), "trace: a start alone changing is told");

  pscu_trace_pass_t frozen = still();
  frozen.calib = record(1U, 12U, true, 0);
  g_check(tells(&frozen, "@0.000 REC W 0C F 0\n", NULL), "trace: freezing alone is told");

  pscu_trace_pass_t saved = still();
  saved.store = PSCU_TRACE_STORE_DONE;
  g_check(tells(&saved, "@0.000 SAVE OK\n", NULL), "trace: a written record is SAVE OK");

  pscu_trace_pass_t refused = still();
  refused.store = PSCU_TRACE_STORE_REFUSED;
  refused.supply_raw = RAW_2V6;
  g_check(tells(&refused, "@0.000 SAVE NO " MV_2V6 "\n", NULL),
          "trace: a refused store gives the supply");

  pscu_trace_pass_t moved = still();
  moved.osccal = 0x4FU;
  g_check(tells(&moved, "@0.000 OSC 50>4F\n", NULL), "trace: OSCCAL moving gives from and to");

  pscu_trace_t trace = fresh();
  pscu_trace_pass(&trace, &moved);
  (void)drained(&trace);
  pscu_trace_pass(&trace, &moved);
  g_check(drained(&trace), "trace: OSCCAL staying put is not told again");
}

static void test_clock(void) {
  pscu_trace_t trace = fresh();
  pscu_trace_pass_t pass = still();
  pass.elapsed_ms = 999U;
  pscu_trace_pass(&trace, &pass);
  pass.elapsed_ms = 2U;
  pass.osccal = 0x51U;
  pscu_trace_pass(&trace, &pass);
  g_check(next_is(&trace, "@1.001 OSC 50>51\n"), "trace: milliseconds carry into seconds");

  pscu_trace_t late = fresh();
  pscu_trace_pass_t hours = still();
  hours.elapsed_ms = 0xFFFFFFFFUL;
  pscu_trace_pass(&late, &hours);
  hours.elapsed_ms = 0xFFFFFFFFUL;
  hours.osccal = 0x52U;
  pscu_trace_pass(&late, &hours);
  g_check(next_is(&late, "@65535.590 OSC 50>52\n"), "trace: the seconds saturate");
}

static void test_queue(void) {
  pscu_trace_t trace = fresh();
  pscu_trace_pass_t pass = still();
  for (uint8_t n = 0U; n < (uint8_t)(PSCU_TRACE_QUEUE + 2U); n++) {
    pass.osccal = (uint8_t)(0x60U + n);
    pscu_trace_pass(&trace, &pass);
  }
  g_check(next_is(&trace, "@0.000 OSC 50>60\n") && next_is(&trace, "@0.000 OSC 60>61\n") &&
              next_is(&trace, "@0.000 OSC 61>62\n") && next_is(&trace, "@0.000 OSC 62>63\n"),
          "trace: queued events leave oldest first");
  g_check(next_is(&trace, "@0.000 LOST 2\n") && drained(&trace),
          "trace: events past a full queue are counted once it drains");

  pscu_trace_t flood = fresh();
  for (uint16_t n = 0U; n < 300U; n++) {
    pass.osccal = (uint8_t)n;
    pscu_trace_pass(&flood, &pass);
  }
  for (uint8_t n = 0U; n < PSCU_TRACE_QUEUE; n++) {
    (void)next_is(&flood, "");
  }
  g_check(next_is(&flood, "@0.000 LOST 255\n"), "trace: the lost count saturates at 255");

  pscu_trace_t wrap = fresh();
  for (uint8_t n = 0U; n < 6U; n++) {
    pass.osccal = (uint8_t)(0x10U + n);
    pscu_trace_pass(&wrap, &pass);
    (void)next_is(&wrap, "");
  }
  pass.osccal = 0x20U;
  pscu_trace_pass(&wrap, &pass);
  g_check(next_is(&wrap, "@0.000 OSC 15>20\n") && drained(&wrap),
          "trace: the ring wraps past its end");
}

// The values a queued event carries, beyond what its line prints, are zero, so
// a reader of the queue never sees a stale value.
static bool popped(pscu_trace_t *trace, uint8_t a, uint8_t b, uint16_t c) {
  pscu_trace_event_t event;
  bool taken = pscu_trace_pop(trace, &event);
  return taken && (event.a == a) && (event.b == b) && (event.c == c);
}

static void test_event_values(void) {
  pscu_trace_t trace = fresh();
  pscu_trace_pass_t pass = still();
  pass.event = PSCU_INDICATOR_EVENT_ACCEPTED;
  pass.disc_gone = true;
  pscu_trace_pass(&trace, &pass);
  g_check(popped(&trace, 0U, 0U, 0U) && popped(&trace, 0U, 0U, 0U),
          "trace: GONE and PASS carry no values");

  pscu_trace_t two = fresh();
  pscu_trace_pass_t open = still();
  open.window = PSCU_WINDOW_OPEN;
  open.counter = 0x0DU;
  open.fire = true;
  open.sent = 3U;
  open.osccal = 0x4CU;
  pscu_trace_pass(&two, &open);
  g_check(popped(&two, 0x0DU, 0x0CU, 0U) && popped(&two, 3U, (uint8_t)'W', 0U) &&
              popped(&two, 0x50U, 0x4CU, 0U),
          "trace: OPEN, SCEX and OSC carry two values and no voltage");

  pscu_trace_t shut = fresh();
  pscu_trace_pass(&shut, &open);
  clear(&shut);
  pscu_trace_pass_t closed = still();
  closed.counter = 0x01U;
  closed.missed = true;
  closed.event = PSCU_INDICATOR_EVENT_REFUSED;
  closed.osccal = 0x4CU;
  pscu_trace_pass(&shut, &closed);
  g_check(popped(&shut, (uint8_t)'D', 0x01U, 0U) && popped(&shut, 0x01U, 0x0CU, 0U),
          "trace: SHUT and MISS carry two values and no voltage");

  pscu_trace_t fail = fresh();
  pscu_trace_pass_t refused = still();
  refused.event = PSCU_INDICATOR_EVENT_REFUSED;
  pscu_trace_pass(&fail, &refused);
  g_check(popped(&fail, 0U, 0U, 0U), "trace: FAIL carries no values");

  pscu_trace_t sup = fresh();
  pscu_trace_pass_t low = still();
  low.supply_ok = false;
  low.supply_raw = RAW_2V6;
  low.store = PSCU_TRACE_STORE_DONE;
  pscu_trace_pass(&sup, &low);
  g_check(popped(&sup, 0U, 0U, 2601U) && popped(&sup, 0U, 0U, 2601U),
          "trace: SUP and SAVE carry only the voltage");
}

static void test_lost_one(void) {
  pscu_trace_t trace = fresh();
  pscu_trace_pass_t pass = still();
  for (uint8_t n = 0U; n < (uint8_t)(PSCU_TRACE_QUEUE + 1U); n++) {
    pass.osccal = (uint8_t)(0x30U + n);
    pscu_trace_pass(&trace, &pass);
  }
  for (uint8_t n = 0U; n < PSCU_TRACE_QUEUE; n++) {
    (void)popped(&trace, 0U, 0U, 0U);
  }
  pscu_trace_event_t lost;
  uint8_t line[PSCU_TRACE_LINE_MAX];
  bool taken = pscu_trace_pop(&trace, &lost);
  g_check(taken && same_line(line, pscu_trace_format(&lost, line), "@0.000 LOST 1\n") &&
              (lost.b == 0U) && (lost.c == 0U),
          "trace: a single lost event is counted, with no other values");
}

static void test_second_boundary(void) {
  pscu_trace_t trace = fresh();
  pscu_trace_pass_t pass = still();
  pass.elapsed_ms = 999U;
  pscu_trace_pass(&trace, &pass);
  pass.elapsed_ms = 1U;
  pass.osccal = 0x51U;
  pscu_trace_pass(&trace, &pass);
  g_check(next_is(&trace, "@1.000 OSC 50>51\n"), "trace: a full thousand milliseconds is a second");
}

static pscu_trace_boot_t boot_of(void) {
  pscu_trace_boot_t boot;
  boot.build_id = 0x1A2BU;
  boot.region = (uint8_t)PSCU_REGION_NTSC_UC;
  boot.vcd_filter = false;
  boot.watchdog = false;
  boot.board = (uint8_t)PSCU_BOARD_MODE_WFCK;
  boot.board_kept = false;
  boot.board_changed = false;
  boot.factory = 0x50U;
  boot.osccal = 0x4EU;
  boot.calib = record(PSCU_CALIB_BOARD_NONE, 12U, false, -2);
  boot.supply_raw = RAW_3V3;
  return boot;
}

static bool boot_line_is(const pscu_trace_boot_t *boot, uint8_t index, const char *want) {
  uint8_t line[PSCU_TRACE_LINE_MAX];
  return same_line(line, pscu_trace_boot_line(boot, index, line), want);
}

static void test_boot(void) {
  pscu_trace_boot_t boot = boot_of();
  g_check(boot_line_is(&boot, 0U, "@0.000 BOOT US 1A2B\n") &&
              boot_line_is(&boot, 1U, "@0.000 RST PWR\n") &&
              boot_line_is(&boot, 2U, "@0.000 BRD W D\n") &&
              boot_line_is(&boot, 3U, "@0.000 OSC 50>4E\n") &&
              boot_line_is(&boot, 4U, "@0.000 REC - 0C P -2\n") &&
              boot_line_is(&boot, 5U, "@0.000 SUP OK " MV_3V3 "\n"),
          "trace: a fresh chip's boot report");

  pscu_trace_boot_t jp = boot_of();
  jp.build_id = 0xC0DEU;
  jp.region = (uint8_t)PSCU_REGION_NTSC_J;
  jp.vcd_filter = true;
  jp.watchdog = true;
  jp.board = (uint8_t)PSCU_BOARD_MODE_GATE;
  jp.board_kept = true;
  jp.supply_raw = RAW_2V6;
  g_check(boot_line_is(&jp, 0U, "@0.000 BOOT JP C0DE V\n") &&
              boot_line_is(&jp, 1U, "@0.000 RST WDT\n") &&
              boot_line_is(&jp, 2U, "@0.000 BRD G K\n") &&
              boot_line_is(&jp, 5U, "@0.000 SUP LOW " MV_2V6 "\n"),
          "trace: a Video CD build after a watchdog reset keeps its board");

  pscu_trace_boot_t eu = boot_of();
  eu.region = (uint8_t)PSCU_REGION_PAL;
  eu.board_changed = true;
  g_check(
      boot_line_is(&eu, 0U, "@0.000 BOOT EU 1A2B\n") && boot_line_is(&eu, 2U, "@0.000 BRD W C\n"),
      "trace: a European build on a board other than the one recorded");
}

// The supply on the boot report's SUP line, for a given bandgap reading.
static bool supply_reads(uint16_t raw, const char *want) {
  pscu_trace_boot_t boot = boot_of();
  boot.supply_raw = raw;
  return boot_line_is(&boot, 5U, want);
}

static void test_supply_mv(void) {
  g_check(supply_reads(0U, "@0.000 SUP LOW 0mV\n"), "trace: a zero reading is no voltage");
  g_check(supply_reads(17U, "@0.000 SUP LOW 0mV\n"), "trace: a reading past 65 V is no voltage");
  g_check(supply_reads(18U, "@0.000 SUP LOW 62577mV\n"),
          "trace: the highest voltage that fits is shown");
  g_check(supply_reads(1024U, "@0.000 SUP LOW 1100mV\n"),
          "trace: the full-scale reading is the bandgap");
}

void trace_tests(pscu_check_fn check) {
  g_check = check;
  test_frame_line();
  test_still_pass();
  test_presence();
  test_window();
  test_strings();
  test_outcomes();
  test_supply();
  test_record();
  test_clock();
  test_queue();
  test_event_values();
  test_lost_one();
  test_second_boundary();
  test_boot();
  test_supply_mv();
}
