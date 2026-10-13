// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "pscu/trace.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pscu/assert.h"
#include "pscu/board_mode.h"
#include "pscu/calib.h"
#include "pscu/config.h"
#include "pscu/indicator.h"
#include "pscu/inject.h"
#include "pscu/region.h"
#include "pscu/subq.h"
#include "pscu/supply.h"

// Linked only into the trace image (Makefile, TRACE_C) and the host suite.

// Every line is plain ASCII, so any serial terminal shows it. An event line is
// "@<s>.<ms> <TAG><values>": seconds and milliseconds since power-on, a tag of
// up to eight capitals and spaces, then hex bytes, decimal counts or single
// letters. Frame lines are 24 hex digits, so the first character tells the two
// kinds apart. Each event is a few bytes in the queue; its text is worked out
// only when it is printed. The trace image is built with
// -fno-tree-switch-conversion, so the switch below stays code instead of a
// table of constants copied into the ATtiny85's 512 bytes of RAM.

// A line being written: the buffer and how much of it is used.
typedef struct {
  uint8_t *line;
  uint8_t len;
} pscu_trace_out_t;

// Four letters, two to a half, first letter high; a zero byte ends a shorter
// word. 16-bit halves, the width cppcheck's MISRA check gives avr-libc's
// uint32_t.
#define PSCU_TRACE_HALF(a, b) ((uint16_t)(((uint16_t)(uint8_t)(a) << 8U) | (uint16_t)(uint8_t)(b)))

// An event's text: its tag as up to eight letters, two to a half, and the
// recipe for its values.
typedef struct {
  uint16_t h1;
  uint16_t h2;
  uint16_t h3;
  uint16_t h4;
  uint16_t recipe;
} pscu_trace_text_t;

// What follows the tag, as up to four steps of four bits each, lowest first;
// a zero step ends it. Every step leads with a space except PSCU_TRACE_TO_B,
// which leads with '>', and PSCU_TRACE_GLUE_B, which joins the letter before.
#define PSCU_TRACE_HEX_A ((uint16_t)1U)
#define PSCU_TRACE_HEX_B ((uint16_t)2U)
#define PSCU_TRACE_DEC_A ((uint16_t)3U)
#define PSCU_TRACE_LETTER_A ((uint16_t)4U)
#define PSCU_TRACE_LETTER_B ((uint16_t)5U)
#define PSCU_TRACE_MV_C ((uint16_t)6U)
#define PSCU_TRACE_LETTER_C ((uint16_t)7U)
#define PSCU_TRACE_TRIM_C ((uint16_t)8U)
#define PSCU_TRACE_TO_B ((uint16_t)9U)
#define PSCU_TRACE_HEX_C ((uint16_t)10U)
#define PSCU_TRACE_GLUE_B ((uint16_t)11U)
#define PSCU_TRACE_NONE ((uint16_t)0U)
#define PSCU_TRACE_RECIPE(s1, s2, s3, s4)                         \
  ((uint16_t)((uint16_t)(s1) | (uint16_t)((uint16_t)(s2) << 4U) | \
              (uint16_t)((uint16_t)(s3) << 8U) | (uint16_t)((uint16_t)(s4) << 12U)))
#define PSCU_TRACE_STEP_BITS (4U)
#define PSCU_TRACE_STEP_MASK ((uint16_t)0x0FU)

#define PSCU_TRACE_MS_PER_S ((uint16_t)1000U)
#define PSCU_TRACE_HEX_LETTERS ((uint8_t)10U)

// The smallest bandgap reading whose millivolts fit 16 bits: 1024 x 1100 mV
// over 18 is 62577 mV, over 17 it would be 66258.
#define PSCU_TRACE_RAW_MIN ((uint16_t)18U)

static void pscu_trace_put(pscu_trace_out_t *out, uint8_t byte) {
  PSCU_ASSERT(out->len < PSCU_TRACE_LINE_MAX);
  out->line[out->len] = byte;
  out->len = (uint8_t)(out->len + 1U);
}

static uint8_t pscu_trace_digit(uint8_t nibble) {
  uint8_t base = (nibble < PSCU_TRACE_HEX_LETTERS) ? (uint8_t)'0' : (uint8_t)((uint8_t)'A' - 10U);
  return (uint8_t)(base + nibble);
}

static void pscu_trace_hex(pscu_trace_out_t *out, uint8_t value) {
  pscu_trace_put(out, pscu_trace_digit((uint8_t)(value >> 4U)));
  pscu_trace_put(out, pscu_trace_digit((uint8_t)(value & 0x0FU)));
}

// A value in decimal, padded with zeros to at least width digits. Five digits
// hold any 16-bit value, so the loop ends within five passes.
static void pscu_trace_decimal(pscu_trace_out_t *out, uint16_t value, uint8_t width) {
  PSCU_ASSERT(width <= 5U);
  uint8_t digits[5];
  uint8_t count = 0U;
  uint16_t rest = value;
  do {
    digits[count] = (uint8_t)((uint8_t)'0' + (uint8_t)(rest % 10U));
    count = (uint8_t)(count + 1U);
    rest = (uint16_t)(rest / 10U);
  } while ((rest != 0U) || (count < width));
  while (count > 0U) {
    count = (uint8_t)(count - 1U);
    pscu_trace_put(out, digits[count]);
  }
}

// The supply in millivolts from a bandgap reading, 0 for a failed reading.
static uint16_t pscu_trace_supply_mv(uint16_t raw) {
  uint16_t mv = 0U;
  if (raw >= PSCU_TRACE_RAW_MIN) {
    mv = (uint16_t)((PSCU_SUPPLY_SCALE * PSCU_SUPPLY_BANDGAP_MV) / (uint32_t)raw);
  }
  return mv;
}

// W for the WFCK carrier, G for the static gate, - for none recorded yet.
static uint8_t pscu_trace_board(uint8_t board) {
  uint8_t letter;
  if (board == (uint8_t)PSCU_BOARD_MODE_WFCK) {
    letter = (uint8_t)'W';
  } else if (board == (uint8_t)PSCU_BOARD_MODE_GATE) {
    letter = (uint8_t)'G';
  } else {
    letter = (uint8_t)'-';
  }
  return letter;
}

static void pscu_trace_text(
    pscu_trace_text_t *text, uint16_t h1, uint16_t h2, uint16_t h3, uint16_t h4, uint16_t recipe) {
  text->h1 = h1;
  text->h2 = h2;
  text->h3 = h3;
  text->h4 = h4;
  text->recipe = recipe;
}

#define PSCU_TRACE_SAY(a, b, c, d, e, f, g, h, recipe) \
  pscu_trace_text(text,                                \
                  PSCU_TRACE_HALF((a), (b)),           \
                  PSCU_TRACE_HALF((c), (d)),           \
                  PSCU_TRACE_HALF((e), (f)),           \
                  PSCU_TRACE_HALF((g), (h)),           \
                  (recipe))
#define PSCU_TRACE_HEX_AB PSCU_TRACE_RECIPE(PSCU_TRACE_HEX_A, PSCU_TRACE_HEX_B, 0U, 0U)
#define PSCU_TRACE_MV PSCU_TRACE_RECIPE(PSCU_TRACE_MV_C, 0U, 0U, 0U)

// Each kind's tag, with its word when it has one, and the recipe for its
// values. One switch, so the text of every line is in one place.
static void pscu_trace_kind_text(pscu_trace_kind_t kind, pscu_trace_text_t *text) {
  switch (kind) {
    case PSCU_TRACE_DISC:
      PSCU_TRACE_SAY('D', 'I', 'S', 'C', 0, 0, 0, 0, PSCU_TRACE_NONE);
      break;
    case PSCU_TRACE_GONE:
      PSCU_TRACE_SAY('G', 'O', 'N', 'E', 0, 0, 0, 0, PSCU_TRACE_NONE);
      break;
    case PSCU_TRACE_OPEN:
      PSCU_TRACE_SAY('O', 'P', 'E', 'N', 0, 0, 0, 0, PSCU_TRACE_HEX_AB);
      break;
    case PSCU_TRACE_HOLD:
      PSCU_TRACE_SAY('H',
                     'O',
                     'L',
                     'D',
                     0,
                     0,
                     0,
                     0,
                     PSCU_TRACE_RECIPE(PSCU_TRACE_HEX_A, PSCU_TRACE_HEX_B, PSCU_TRACE_MV_C, 0U));
      break;
    case PSCU_TRACE_SHUT:
      PSCU_TRACE_SAY('S',
                     'H',
                     'U',
                     'T',
                     0,
                     0,
                     0,
                     0,
                     PSCU_TRACE_RECIPE(PSCU_TRACE_LETTER_A, PSCU_TRACE_HEX_B, 0U, 0U));
      break;
    case PSCU_TRACE_SCEX:
      PSCU_TRACE_SAY('S',
                     'C',
                     'E',
                     'X',
                     0,
                     0,
                     0,
                     0,
                     PSCU_TRACE_RECIPE(PSCU_TRACE_DEC_A, PSCU_TRACE_LETTER_B, 0U, 0U));
      break;
    case PSCU_TRACE_PASS:
      PSCU_TRACE_SAY('P', 'A', 'S', 'S', 0, 0, 0, 0, PSCU_TRACE_NONE);
      break;
    case PSCU_TRACE_FAIL:
      PSCU_TRACE_SAY('F', 'A', 'I', 'L', 0, 0, 0, 0, PSCU_TRACE_NONE);
      break;
    case PSCU_TRACE_MISS:
      PSCU_TRACE_SAY('M', 'I', 'S', 'S', 0, 0, 0, 0, PSCU_TRACE_HEX_AB);
      break;
    case PSCU_TRACE_SUP_OK:
      PSCU_TRACE_SAY('S', 'U', 'P', ' ', 'O', 'K', 0, 0, PSCU_TRACE_MV);
      break;
    case PSCU_TRACE_SUP_LOW:
      PSCU_TRACE_SAY('S', 'U', 'P', ' ', 'L', 'O', 'W', 0, PSCU_TRACE_MV);
      break;
    case PSCU_TRACE_REC:
      PSCU_TRACE_SAY(
          'R',
          'E',
          'C',
          0,
          0,
          0,
          0,
          0,
          PSCU_TRACE_RECIPE(
              PSCU_TRACE_LETTER_B, PSCU_TRACE_HEX_A, PSCU_TRACE_LETTER_C, PSCU_TRACE_TRIM_C));
      break;
    case PSCU_TRACE_SAVE_OK:
      PSCU_TRACE_SAY('S', 'A', 'V', 'E', ' ', 'O', 'K', 0, PSCU_TRACE_NONE);
      break;
    case PSCU_TRACE_SAVE_NO:
      PSCU_TRACE_SAY('S', 'A', 'V', 'E', ' ', 'N', 'O', 0, PSCU_TRACE_MV);
      break;
    case PSCU_TRACE_OSC:
      PSCU_TRACE_SAY('O',
                     'S',
                     'C',
                     0,
                     0,
                     0,
                     0,
                     0,
                     PSCU_TRACE_RECIPE(PSCU_TRACE_HEX_A, PSCU_TRACE_TO_B, 0U, 0U));
      break;
    case PSCU_TRACE_BOOT:
      PSCU_TRACE_SAY(
          'B',
          'O',
          'O',
          'T',
          0,
          0,
          0,
          0,
          PSCU_TRACE_RECIPE(PSCU_TRACE_LETTER_A, PSCU_TRACE_GLUE_B, PSCU_TRACE_HEX_C, 0U));
      break;
    case PSCU_TRACE_RST_PWR:
      PSCU_TRACE_SAY('R', 'S', 'T', ' ', 'P', 'W', 'R', 0, PSCU_TRACE_NONE);
      break;
    case PSCU_TRACE_RST_WDT:
      PSCU_TRACE_SAY('R', 'S', 'T', ' ', 'W', 'D', 'T', 0, PSCU_TRACE_NONE);
      break;
    case PSCU_TRACE_BRD:
      PSCU_TRACE_SAY('B',
                     'R',
                     'D',
                     0,
                     0,
                     0,
                     0,
                     0,
                     PSCU_TRACE_RECIPE(PSCU_TRACE_LETTER_B, PSCU_TRACE_LETTER_A, 0U, 0U));
      break;
    case PSCU_TRACE_LOST:
    default:
      PSCU_TRACE_SAY(
          'L', 'O', 'S', 'T', 0, 0, 0, 0, PSCU_TRACE_RECIPE(PSCU_TRACE_DEC_A, 0U, 0U, 0U));
      break;
  }
}

// The trim, a two's-complement byte, with its sign.
static void pscu_trace_trim(pscu_trace_out_t *out, uint8_t raw) {
  int16_t magnitude = (int16_t)(int8_t)raw;
  if (magnitude < 0) {
    pscu_trace_put(out, (uint8_t)'-');
    magnitude = (int16_t)(-magnitude);
  }
  pscu_trace_decimal(out, (uint16_t)magnitude, 1U);
}

// One recipe step.
static void pscu_trace_step(pscu_trace_out_t *out, const pscu_trace_event_t *event, uint16_t step) {
  if (step == PSCU_TRACE_TO_B) {
    pscu_trace_put(out, (uint8_t)'>');
  } else if (step != PSCU_TRACE_GLUE_B) {
    pscu_trace_put(out, (uint8_t)' ');
  } else {
    // Joined to the letter before.
  }
  switch (step) {
    case PSCU_TRACE_HEX_A:
      pscu_trace_hex(out, event->a);
      break;
    case PSCU_TRACE_DEC_A:
      pscu_trace_decimal(out, event->a, 1U);
      break;
    case PSCU_TRACE_LETTER_A:
      pscu_trace_put(out, event->a);
      break;
    case PSCU_TRACE_LETTER_B:
    case PSCU_TRACE_GLUE_B:
      pscu_trace_put(out, event->b);
      break;
    case PSCU_TRACE_MV_C:
      pscu_trace_decimal(out, event->c, 1U);
      pscu_trace_put(out, (uint8_t)'m');
      pscu_trace_put(out, (uint8_t)'V');
      break;
    case PSCU_TRACE_LETTER_C:
      pscu_trace_put(out, (uint8_t)(event->c >> 8U));
      break;
    case PSCU_TRACE_TRIM_C:
      pscu_trace_trim(out, (uint8_t)(event->c & 0xFFU));
      break;
    case PSCU_TRACE_HEX_C:
      pscu_trace_hex(out, (uint8_t)(event->c >> 8U));
      pscu_trace_hex(out, (uint8_t)(event->c & 0xFFU));
      break;
    default:
      // HEX_B and TO_B both print b in hex.
      pscu_trace_hex(out, event->b);
      break;
  }
}

// Slots past count are never read, so the queue is left as it is.
void pscu_trace_init(pscu_trace_t *trace, pscu_calib_t calib, uint8_t osccal) {
  PSCU_ASSERT(trace != NULL);
  trace->head = 0U;
  trace->count = 0U;
  trace->lost = 0U;
  trace->now.s = 0U;
  trace->now.ms = 0U;
  trace->window = PSCU_WINDOW_CLOSED;
  trace->gone = false;
  trace->supply_ok = true;
  trace->calib = calib;
  trace->osccal = osccal;
}

// Queue one event at the current time; a full queue counts it as lost, and the
// count saturates.
static void pscu_trace_push(
    pscu_trace_t *trace, pscu_trace_kind_t kind, uint8_t a, uint8_t b, uint16_t c) {
  if (trace->count < PSCU_TRACE_QUEUE) {
    pscu_trace_event_t *slot =
        &trace->queue[(uint8_t)((uint8_t)(trace->head + trace->count) & PSCU_TRACE_QUEUE_MASK)];
    slot->at = trace->now;
    slot->kind = kind;
    slot->a = a;
    slot->b = b;
    slot->c = c;
    trace->count = (uint8_t)(trace->count + 1U);
  } else if (trace->lost < 0xFFU) {
    trace->lost = (uint8_t)(trace->lost + 1U);
  } else {
    trace->lost = 0xFFU;
  }
}

// The three shapes an event takes: no values, two bytes, or a voltage.
static void pscu_trace_note(pscu_trace_t *trace, pscu_trace_kind_t kind) {
  pscu_trace_push(trace, kind, 0U, 0U, 0U);
}

static void pscu_trace_note2(pscu_trace_t *trace, pscu_trace_kind_t kind, uint8_t a, uint8_t b) {
  pscu_trace_push(trace, kind, a, b, 0U);
}

static void pscu_trace_note_mv(pscu_trace_t *trace, pscu_trace_kind_t kind, uint16_t mv) {
  pscu_trace_push(trace, kind, 0U, 0U, mv);
}

bool pscu_trace_pop(pscu_trace_t *trace, pscu_trace_event_t *event) {
  PSCU_ASSERT((trace != NULL) && (event != NULL));
  bool taken = trace->count > 0U;
  if (taken) {
    *event = trace->queue[trace->head];
    trace->head = (uint8_t)((uint8_t)(trace->head + 1U) & PSCU_TRACE_QUEUE_MASK);
    trace->count = (uint8_t)(trace->count - 1U);
  } else if (trace->lost > 0U) {
    event->at = trace->now;
    event->kind = PSCU_TRACE_LOST;
    event->a = trace->lost;
    event->b = 0U;
    event->c = 0U;
    trace->lost = 0U;
    taken = true;
  } else {
    taken = false;
  }
  return taken;
}

// Advance the clock by one pass: whole seconds and the remainder are added
// apart, so no sum can wrap, and the seconds saturate.
static void pscu_trace_tick(pscu_trace_t *trace, uint32_t elapsed_ms) {
  uint32_t whole = elapsed_ms / (uint32_t)PSCU_TRACE_MS_PER_S;
  uint16_t part = (uint16_t)(elapsed_ms % (uint32_t)PSCU_TRACE_MS_PER_S);
  uint16_t ms = (uint16_t)(trace->now.ms + part);
  if (ms >= PSCU_TRACE_MS_PER_S) {
    ms = (uint16_t)(ms - PSCU_TRACE_MS_PER_S);
    whole = whole + 1U;
  }
  uint16_t room = (uint16_t)(0xFFFFU - trace->now.s);
  trace->now.ms = ms;
  if (whole >= (uint32_t)room) {
    trace->now.s = 0xFFFFU;
  } else {
    trace->now.s = (uint16_t)(trace->now.s + (uint16_t)whole);
  }
}

static uint8_t pscu_trace_shut_reason(const pscu_trace_pass_t *pass) {
  uint8_t reason;
  if (pass->disc_gone) {
    reason = PSCU_TRACE_SHUT_GONE;
  } else if (pass->program) {
    reason = PSCU_TRACE_SHUT_PROGRAM;
  } else {
    reason = PSCU_TRACE_SHUT_DROPPED;
  }
  return reason;
}

// The disc arriving or leaving, and the window changing state: open with the
// counter and the start, held with the supply as well, or shut with the reason
// and the counter.
static void pscu_trace_disc(pscu_trace_t *trace, const pscu_trace_pass_t *pass) {
  if (pass->disc_gone != trace->gone) {
    pscu_trace_note(trace, pass->disc_gone ? PSCU_TRACE_GONE : PSCU_TRACE_DISC);
  }
  trace->gone = pass->disc_gone;
  if (pass->window != trace->window) {
    uint16_t mv = pscu_trace_supply_mv(pass->supply_raw);
    if (pass->window == PSCU_WINDOW_OPEN) {
      pscu_trace_note2(trace, PSCU_TRACE_OPEN, pass->counter, pass->calib.trigger);
    } else if (pass->window == PSCU_WINDOW_HELD) {
      pscu_trace_push(trace, PSCU_TRACE_HOLD, pass->counter, pass->calib.trigger, mv);
    } else {
      pscu_trace_note2(trace, PSCU_TRACE_SHUT, pscu_trace_shut_reason(pass), pass->counter);
    }
  }
  trace->window = pass->window;
}

// A string with its number and board mode, then the disc's outcome: accepted,
// refused, or a lead-in that never reached the start, with the counter it got
// to and the start it needed. String PSCU_STEALTH_STRINGS is the last one this
// opening allows.
static void pscu_trace_session(pscu_trace_t *trace, const pscu_trace_pass_t *pass) {
  if (pass->fire) {
    pscu_trace_note2(trace, PSCU_TRACE_SCEX, pass->sent, pscu_trace_board(pass->board));
  }
  if (pass->event == PSCU_INDICATOR_EVENT_ACCEPTED) {
    pscu_trace_note(trace, PSCU_TRACE_PASS);
  } else if (pass->missed) {
    pscu_trace_note2(trace, PSCU_TRACE_MISS, pass->counter, trace->calib.trigger);
  } else if (pass->event == PSCU_INDICATOR_EVENT_REFUSED) {
    pscu_trace_note(trace, PSCU_TRACE_FAIL);
  } else {
    // Nothing resolved this pass.
  }
}

static bool pscu_trace_same_calib(pscu_calib_t a, pscu_calib_t b) {
  return (a.board == b.board) && (a.trigger == b.trigger) && (a.frozen == b.frozen) &&
         (a.trim == b.trim);
}

// The record's F or P in the high byte of c and its trim in the low byte.
static uint16_t pscu_trace_record_c(pscu_calib_t calib) {
  uint16_t state = calib.frozen ? PSCU_TRACE_HALF('F', 0) : PSCU_TRACE_HALF('P', 0);
  return (uint16_t)(state | (uint16_t)(uint8_t)calib.trim);
}

// The supply crossing the guard either way (below it no string goes out and no
// record is written, indicator code 7), the record learned, a store tried, and
// OSCCAL moving.
static void pscu_trace_chip(pscu_trace_t *trace, const pscu_trace_pass_t *pass) {
  uint16_t mv = pscu_trace_supply_mv(pass->supply_raw);
  if (pass->supply_ok != trace->supply_ok) {
    pscu_trace_note_mv(trace, pass->supply_ok ? PSCU_TRACE_SUP_OK : PSCU_TRACE_SUP_LOW, mv);
  }
  trace->supply_ok = pass->supply_ok;
  if (!pscu_trace_same_calib(pass->calib, trace->calib)) {
    pscu_trace_push(trace,
                    PSCU_TRACE_REC,
                    pass->calib.trigger,
                    pscu_trace_board(pass->calib.board),
                    pscu_trace_record_c(pass->calib));
  }
  trace->calib = pass->calib;
  if (pass->store != PSCU_TRACE_STORE_NONE) {
    bool refused = pass->store == PSCU_TRACE_STORE_REFUSED;
    pscu_trace_note_mv(trace, refused ? PSCU_TRACE_SAVE_NO : PSCU_TRACE_SAVE_OK, mv);
  }
  if (pass->osccal != trace->osccal) {
    pscu_trace_note2(trace, PSCU_TRACE_OSC, trace->osccal, pass->osccal);
  }
  trace->osccal = pass->osccal;
}

void pscu_trace_pass(pscu_trace_t *trace, const pscu_trace_pass_t *pass) {
  PSCU_ASSERT((trace != NULL) && (pass != NULL));
  pscu_trace_tick(trace, pass->elapsed_ms);
  pscu_trace_disc(trace, pass);
  pscu_trace_session(trace, pass);
  pscu_trace_chip(trace, pass);
}

uint8_t pscu_trace_format(const pscu_trace_event_t *event, uint8_t *line) {
  PSCU_ASSERT((event != NULL) && (line != NULL));
  pscu_trace_out_t out = { line, 0U };
  pscu_trace_text_t text;
  pscu_trace_kind_text(event->kind, &text);
  uint16_t halves[4] = { text.h1, text.h2, text.h3, text.h4 };
  pscu_trace_put(&out, (uint8_t)'@');
  pscu_trace_decimal(&out, event->at.s, 1U);
  pscu_trace_put(&out, (uint8_t)'.');
  pscu_trace_decimal(&out, event->at.ms, 3U);
  pscu_trace_put(&out, (uint8_t)' ');
  bool going = true;
  for (uint8_t n = 0U; (n < 8U) && going; n++) {
    uint16_t half = halves[n >> 1U];
    uint8_t letter = ((n & 1U) == 0U) ? (uint8_t)(half >> 8U) : (uint8_t)(half & 0xFFU);
    going = letter != 0U;
    if (going) {
      pscu_trace_put(&out, letter);
    }
  }
  // A 16-bit recipe is empty after four shifts, so the loop ends by then.
  for (uint16_t recipe = text.recipe; recipe != 0U;
       recipe = (uint16_t)(recipe >> PSCU_TRACE_STEP_BITS)) {
    pscu_trace_step(&out, event, (uint16_t)(recipe & PSCU_TRACE_STEP_MASK));
  }
  pscu_trace_put(&out, (uint8_t)'\n');
  return out.len;
}

uint8_t pscu_trace_format_frame(const uint8_t *frame, uint8_t *line) {
  PSCU_ASSERT((frame != NULL) && (line != NULL));
  pscu_trace_out_t out = { line, 0U };
  for (uint8_t byte = 0U; byte < PSCU_SUBQ_FRAME_BYTES; byte++) {
    pscu_trace_hex(&out, frame[byte]);
  }
  pscu_trace_put(&out, (uint8_t)'\n');
  return out.len;
}

// The region as two letters in a and b.
static void pscu_trace_region(pscu_trace_event_t *event, uint8_t region) {
  if (region == (uint8_t)PSCU_REGION_NTSC_J) {
    event->a = (uint8_t)'J';
    event->b = (uint8_t)'P';
  } else if (region == (uint8_t)PSCU_REGION_PAL) {
    event->a = (uint8_t)'E';
    event->b = (uint8_t)'U';
  } else {
    event->a = (uint8_t)'U';
    event->b = (uint8_t)'S';
  }
}

// The boot report, one line per index, each an event at time zero:
//   BOOT <JP|US|EU> <build>
//   RST <PWR|WDT>
//   BRD <W|G> <K kept after a watchdog reset | D detected | C detected, and
//       not the board recorded>
//   OSC <factory>><applied>
//   REC <board> <start> <F frozen|P probing> <trim>, as read from EEPROM
//   SUP <OK|LOW> <supply>mV
// The Video CD filter shows as a V after the build: "BOOT JP C0DE V".
uint8_t pscu_trace_boot_line(const pscu_trace_boot_t *boot, uint8_t index, uint8_t *line) {
  PSCU_ASSERT((boot != NULL) && (index < PSCU_TRACE_BOOT_LINES));
  pscu_trace_event_t event;
  event.at.s = 0U;
  event.at.ms = 0U;
  event.kind = PSCU_TRACE_BOOT;
  event.c = boot->build_id;
  pscu_trace_region(&event, boot->region);
  if (index == 1U) {
    event.kind = boot->watchdog ? PSCU_TRACE_RST_WDT : PSCU_TRACE_RST_PWR;
  } else if (index == 2U) {
    uint8_t how = boot->board_changed ? (uint8_t)'C' : (uint8_t)'D';
    event.kind = PSCU_TRACE_BRD;
    event.a = boot->board_kept ? (uint8_t)'K' : how;
    event.b = pscu_trace_board(boot->board);
  } else if (index == 3U) {
    event.kind = PSCU_TRACE_OSC;
    event.a = boot->factory;
    event.b = boot->osccal;
  } else if (index == 4U) {
    event.kind = PSCU_TRACE_REC;
    event.a = boot->calib.trigger;
    event.b = pscu_trace_board(boot->calib.board);
    event.c = pscu_trace_record_c(boot->calib);
  } else if (index == 5U) {
    event.kind = pscu_supply_ok(boot->supply_raw) ? PSCU_TRACE_SUP_OK : PSCU_TRACE_SUP_LOW;
    event.c = pscu_trace_supply_mv(boot->supply_raw);
  } else {
    // Index 0, the BOOT line, as set above.
  }
  uint8_t len = pscu_trace_format(&event, line);
  if ((index == 0U) && boot->vcd_filter) {
    line[len - 1U] = (uint8_t)' ';
    line[len] = (uint8_t)'V';
    line[len + 1U] = (uint8_t)'\n';
    len = (uint8_t)(len + 2U);
  }
  return len;
}
