// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <string.h>

#include "harness.h"
#include "scenarios.h"

// The trace profile (PROFILE=trace) sends, as 8N1 serial at 115200 baud on PB3,
// a six-line boot report, then every captured SUBQ frame as 24 hex digits and
// the chip's decisions as event lines starting with '@' (src/trace.c). This
// decodes the pin the way a USB serial adapter would and checks the lines.

#define TRACE_BAUD 115200U
#define TRACE_EDGES_MAX 65536
#define TRACE_TEXT_MAX 16384
#define TRACE_LINE_CHARS 24
#define TRACE_TOC_FRAMES 12
#define TRACE_TAIL_MS 50U
// Frames the chip skips while it syncs to the first gap (harness.h
// TRIGGER_SPARE_FRAMES), so at least this many TOC lines must arrive.
#define TRACE_TOC_LINES_MIN (TRACE_TOC_FRAMES - 3)

// The lead-in frame below as the harness seals it: CRC-16 CCITT 0x6449, the
// same bytes the host suite uses for a lead-in A0 frame.
static const char TRACE_TOC_HEX[] = "4100A0000000000000006449";
static const uint8_t TRACE_TOC[SUBQ_FRAME_BYTES] = {
  0x41U, 0x00U, 0xA0U, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

typedef struct {
  avr_t *avr;
  uint64_t at[TRACE_EDGES_MAX];
  uint8_t level[TRACE_EDGES_MAX];
  int count;
} trace_edges_t;

static trace_edges_t g_edges;

static void on_trace_pin(struct avr_irq_t *irq, uint32_t value, void *param) {
  (void)irq;
  trace_edges_t *edges = param;
  if (edges->count < TRACE_EDGES_MAX) {
    edges->at[edges->count] = edges->avr->cycle;
    edges->level[edges->count] = (uint8_t)(value & 1U);
    edges->count++;
  } else {
    g_record_overflows++;
  }
}

// The pin level at a cycle: the last edge at or before it. Samples only move
// forward, so the cursor walks the edge list once.
static uint8_t level_at(const trace_edges_t *edges, int *cursor, uint64_t cycle) {
  while (((*cursor + 1) < edges->count) && (edges->at[*cursor + 1] <= cycle)) {
    (*cursor)++;
  }
  return edges->level[*cursor];
}

// The cycle each decoded character's start bit fell on, so a line's arrival
// can be set against the time stamp it carries.
static uint64_t g_char_cycle[TRACE_TEXT_MAX];

// Decode 8N1 from the first time the line idles high (the pin powers up low):
// a falling edge starts a byte, each bit is sampled mid-cell, and the stop bit
// must be high. Returns the characters, or -1 on a framing error.
static int decode_serial(const trace_edges_t *edges, double bit_cycles, char *text, int max) {
  int len = 0;
  int framing_ok = 1;
  int first = 0;
  while ((first < edges->count) && (edges->level[first] == 0U)) {
    first++;
  }
  int cursor = first;
  uint64_t busy_until = 0U;
  for (int i = first; (i < edges->count) && framing_ok && (len < (max - 1)); i++) {
    if ((edges->level[i] == 0U) && (edges->at[i] >= busy_until)) {
      uint64_t start = edges->at[i];
      uint8_t byte = 0U;
      cursor = i;
      for (int bit = 0; bit < 8; bit++) {
        uint64_t mid = start + (uint64_t)(bit_cycles * (1.5 + (double)bit));
        byte = (uint8_t)(byte | (uint8_t)(level_at(edges, &cursor, mid) << bit));
      }
      busy_until = start + (uint64_t)(bit_cycles * 9.5);
      framing_ok = level_at(edges, &cursor, busy_until) == 1U;
      text[len] = (char)byte;
      g_char_cycle[len] = start;
      len++;
    }
  }
  text[len] = '\0';
  return framing_ok ? len : -1;
}

// The event lines of the last decode, in order, for the checks that follow.
#define TRACE_EVENTS_MAX 128
#define TRACE_EVENT_CHARS 40
static char g_events[TRACE_EVENTS_MAX][TRACE_EVENT_CHARS];
static uint64_t g_event_cycle[TRACE_EVENTS_MAX];
static int g_event_count;
// Frame lines that read as a failed capture, all F.
static int g_failed_lines;

// A frame line is 24 upper-case hex digits; an event line is "@<s>.<ms> " and
// a tag of capitals.
static int is_frame(const char *line) {
  return (strlen(line) == TRACE_LINE_CHARS) &&
         (strspn(line, "0123456789ABCDEF") == TRACE_LINE_CHARS);
}

static int is_event(const char *line) {
  const char *space = strchr(line, ' ');
  return (line[0] == '@') && (space != NULL) && (strlen(line) < TRACE_EVENT_CHARS) &&
         (space[1] >= 'A') && (space[1] <= 'Z');
}

// Count the decoded lines and how many are the lead-in frame, keep the event
// lines, and say whether every line is a frame or an event. strtok consumes
// text.
static int read_lines(char *text, int *lines, int *toc_lines) {
  int well_formed = 1;
  *lines = 0;
  *toc_lines = 0;
  g_event_count = 0;
  g_failed_lines = 0;
  for (char *line = strtok(text, "\n"); line != NULL; line = strtok(NULL, "\n")) {
    (*lines)++;
    int event = is_event(line);
    well_formed = well_formed && (is_frame(line) || event);
    *toc_lines += (strcmp(line, TRACE_TOC_HEX) == 0) ? 1 : 0;
    g_failed_lines += (is_frame(line) && (strspn(line, "F") == TRACE_LINE_CHARS)) ? 1 : 0;
    if (event && (g_event_count < TRACE_EVENTS_MAX)) {
      (void)snprintf(g_events[g_event_count], TRACE_EVENT_CHARS, "%s", line);
      g_event_cycle[g_event_count] = g_char_cycle[line - text];
      g_event_count++;
    }
  }
  return well_formed;
}

// The tag of event n, the text after "@<s>.<ms> ".
static const char *event_tag(int n) {
  const char *space = strchr(g_events[n], ' ');
  return (space != NULL) ? (space + 1) : "";
}

static int events_starting(const char *prefix) {
  int count = 0;
  for (int i = 0; i < g_event_count; i++) {
    count += (strncmp(event_tag(i), prefix, strlen(prefix)) == 0) ? 1 : 0;
  }
  return count;
}

static avr_t *trace_boot(const target_t *t, const char *elf, uint32_t freq) {
  avr_t *avr = build_avr(t, elf, freq);
  wfck_ctx_t ctx = { NULL, 1U, 0U };
  g_edges.avr = avr;
  g_edges.count = 0;
  avr_irq_register_notify(pin_irq(avr, t, t->indicator), on_trace_pin, &g_edges);
  boot_quiet(avr, t, 0, &ctx);
  return avr;
}

void scenario_trace(const target_t *t, const char *elf, uint32_t freq) {
  avr_t *avr = trace_boot(t, elf, freq);
  clock_frames(avr, t, TRACE_TOC, TRACE_TOC_FRAMES);
  run_cycles(avr, ms_cycles(TRACE_TAIL_MS));

  static char text[TRACE_TEXT_MAX];
  int len = decode_serial(&g_edges, (double)freq / (double)TRACE_BAUD, text, TRACE_TEXT_MAX);
  int lines = 0;
  int toc_lines = 0;
  int well_formed = (len > 0) && read_lines(text, &lines, &toc_lines);

  char label[112];
  (void)snprintf(label,
                 sizeof(label),
                 "trace: every line is a 24-digit hex frame, framed 8N1 (%d lines)",
                 lines);
  check(well_formed && (len != -1) && (len < (TRACE_TEXT_MAX - 1)), label);
  (void)snprintf(
      label, sizeof(label), "trace: the lead-in frames arrive as sent (%d lines)", toc_lines);
  check((toc_lines >= TRACE_TOC_LINES_MIN) && (toc_lines <= TRACE_TOC_FRAMES), label);

  // The boot report comes first: the build in this US image, a power-on reset,
  // the static gate detected (trace_boot drives no WFCK carrier), OSCCAL from
  // factory to now, the erased record and a good supply.
  static const char *const boot[] = {
    "BOOT US ", "RST PWR", "BRD G D", "OSC ", "REC - ", "SUP OK "
  };
  int in_order = g_event_count >= 6;
  for (int i = 0; in_order && (i < 6); i++) {
    in_order = (strncmp(event_tag(i), boot[i], strlen(boot[i])) == 0) &&
               (strncmp(g_events[i], "@0.000 ", 7U) == 0);
  }
  check(in_order, "trace: the boot report comes first, at time zero, in order");
}

// A lead-in long enough for several strings: the line must stay idle-high
// through each one and carry no byte inside it, so every line after a string
// still decodes (rule 7: the trace goes out in the gap after a frame, never
// inside a string).
#define TRACE_STRING_FRAMES 60
#define TRACE_DRAIN_FRAMES 8

void scenario_trace_strings(const target_t *t, const char *elf, uint32_t freq) {
  avr_t *avr = trace_boot(t, elf, freq);
  clock_frames(avr, t, TRACE_TOC, TRACE_STRING_FRAMES);
  // One event leaves per frame, so a few more frames carry out what is still
  // queued after the last string.
  const uint8_t silence[SUBQ_FRAME_BYTES] = { 0 };
  clock_frames(avr, t, silence, TRACE_DRAIN_FRAMES);
  run_cycles(avr, ms_cycles(TRACE_TAIL_MS));

  int inside = 0;
  for (int i = 0; i < g_edges.count; i++) {
    inside += in_burst(g_edges.at[i], BIT_CYCLES(freq));
  }
  static char text[TRACE_TEXT_MAX];
  int len = decode_serial(&g_edges, (double)freq / (double)TRACE_BAUD, text, TRACE_TEXT_MAX);
  int lines = 0;
  int toc_lines = 0;
  int well_formed = (len > 0) && read_lines(text, &lines, &toc_lines);

  char label[112];
  (void)snprintf(label,
                 sizeof(label),
                 "trace: lines stay framed across %d strings (%d lines)",
                 g_data_bursts,
                 lines);
  check((g_data_bursts >= 2) && well_formed && (len != -1), label);
  (void)snprintf(
      label, sizeof(label), "trace: no line goes out inside a string (%d edges inside)", inside);
  check(inside == 0, label);

  // Each string sent shows as one SCEX event, numbered from 1, after the window
  // opened; the static gate gives G.
  int scex = events_starting("SCEX ");
  int numbered = 1;
  int seen = 0;
  for (int i = 0; i < g_event_count; i++) {
    if (strncmp(event_tag(i), "SCEX ", 5U) == 0) {
      seen++;
      char want[24];
      (void)snprintf(want, sizeof(want), "SCEX %d G", seen);
      numbered = numbered && (strcmp(event_tag(i), want) == 0);
    }
  }
  (void)snprintf(label,
                 sizeof(label),
                 "trace: one numbered SCEX event per string (%d events, %d strings)",
                 scex,
                 g_data_bursts);
  check((scex == g_data_bursts) && numbered && (events_starting("OPEN ") >= 1), label);
}

// Decode the whole capture so far into g_events. Returns whether every line
// was framed and well formed.
static int trace_read(uint32_t freq) {
  static char text[TRACE_TEXT_MAX];
  int len = decode_serial(&g_edges, (double)freq / (double)TRACE_BAUD, text, TRACE_TEXT_MAX);
  int lines = 0;
  int toc_lines = 0;
  return (len > 0) && (len < (TRACE_TEXT_MAX - 1)) && read_lines(text, &lines, &toc_lines);
}

// The time stamp of event n, in milliseconds.
static uint64_t event_ms(int n) {
  unsigned s = 0U;
  unsigned ms = 0U;
  (void)sscanf(g_events[n], "@%u.%u", &s, &ms);
  return ((uint64_t)s * 1000U) + ms;
}

static int event_index(const char *prefix) {
  int found = -1;
  for (int i = 0; (i < g_event_count) && (found < 0); i++) {
    found = (strncmp(event_tag(i), prefix, strlen(prefix)) == 0) ? i : -1;
  }
  return found;
}

static int events_at_zero(void) {
  int count = 0;
  for (int i = 0; i < g_event_count; i++) {
    count += (strncmp(g_events[i], "@0.000 ", 7U) == 0) ? 1 : 0;
  }
  return count;
}

// A disc that never reads a lead-in, on a chip at nominal speed: nothing is
// learned, so no pass reports a store, and the boot report is the only thing
// at time zero, exactly six lines.
#define TRACE_QUIET_FRAMES 150

void scenario_trace_quiet(const target_t *t, const char *elf, uint32_t freq) {
  avr_t *avr = trace_boot(t, elf, freq);
  const uint8_t silence[SUBQ_FRAME_BYTES] = { 0 };
  g_frame_period_ns = SIM_SECTOR_NS;
  clock_frames(avr, t, silence, TRACE_QUIET_FRAMES);
  g_frame_period_ns = 0U;
  run_cycles(avr, ms_cycles(TRACE_TAIL_MS));
  int framed = trace_read(freq);

  char label[112];
  (void)snprintf(label,
                 sizeof(label),
                 "trace: the boot report is six lines at time zero (%d)",
                 events_at_zero());
  check(framed && (events_at_zero() == 6), label);
  (void)snprintf(label,
                 sizeof(label),
                 "trace: nothing learned shows no store (%d SAVE events)",
                 events_starting("SAVE "));
  check(framed && (events_starting("SAVE ") == 0), label);
}

// An RC 5 percent fast, timed against valid program-area frames: the first
// batch steps OSCCAL down, and with the window closed the trim is stored at
// once, which the trace reports as SAVE OK.
#define TRACE_TRIM_FRAMES 200
#define TRACE_RC_FAST_PERCENT 105U

void scenario_trace_trim(const target_t *t, const char *elf, uint32_t freq) {
  uint32_t fast = (uint32_t)(((uint64_t)freq * TRACE_RC_FAST_PERCENT) / 100U);
  avr_t *avr = trace_boot(t, elf, fast);
  const uint8_t play[SUBQ_FRAME_BYTES] = { 0x41U, 0x01U, 0x01U, 0x00U, 0x02U, 0,
                                           0,     0,     0x02U, 0,     0,     0 };
  g_frame_period_ns = SIM_SECTOR_NS;
  clock_frames(avr, t, play, TRACE_TRIM_FRAMES);
  g_frame_period_ns = 0U;
  run_cycles(avr, ms_cycles(TRACE_TAIL_MS));
  // The chip sends at its own 8 MHz count, so the line decodes at the
  // nominal rate whatever the simulated speed.
  int framed = trace_read(freq);

  char label[112];
  (void)snprintf(label,
                 sizeof(label),
                 "trace: a trim step is stored and reported (%d OSC, %d SAVE OK)",
                 events_starting("OSC "),
                 events_starting("SAVE OK"));
  check(framed && (events_starting("OSC ") >= 2) && (events_starting("SAVE OK") >= 1), label);
}

// Each event carries the time since power-on, kept from Timer1 across every
// pass. From the boot report to the disc leaving, a few seconds later, the
// stamps must match the simulated time between those lines to within the few
// milliseconds a line waits for its gap. With SQCK gone, every failed capture
// shows as an all-F frame line.
#define TRACE_CLOCK_FRAMES 150
#define TRACE_CLOCK_GONE_MS 2500U
#define TRACE_CLOCK_TOLERANCE_MS 20U

void scenario_trace_clock(const target_t *t, const char *elf, uint32_t freq) {
  avr_t *avr = trace_boot(t, elf, freq);
  const uint8_t silence[SUBQ_FRAME_BYTES] = { 0 };
  g_frame_period_ns = SIM_SECTOR_NS;
  clock_frames(avr, t, silence, TRACE_CLOCK_FRAMES);
  g_frame_period_ns = 0U;
  run_cycles(avr, ms_cycles(TRACE_CLOCK_GONE_MS));
  int framed = trace_read(freq);

  int gone = event_index("GONE");
  uint64_t stamped = (gone > 0) ? event_ms(gone) : 0U;
  uint64_t seen = (gone > 0) ? (((g_event_cycle[gone] - g_event_cycle[0]) * 1000U) / freq) : 0U;
  uint64_t apart = (stamped > seen) ? (stamped - seen) : (seen - stamped);
  char label[112];
  (void)snprintf(label,
                 sizeof(label),
                 "trace: event stamps keep time (GONE at %llu ms, seen at %llu ms)",
                 (unsigned long long)stamped,
                 (unsigned long long)seen);
  check(framed && (gone > 0) && (apart <= TRACE_CLOCK_TOLERANCE_MS), label);
  (void)snprintf(
      label, sizeof(label), "trace: a failed capture reads as all F (%d lines)", g_failed_lines);
  check(framed && (g_failed_lines > 0), label);
}
