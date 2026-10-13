// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <string.h>

#include "harness.h"
#include "scenarios.h"

// The trace profile (PROFILE=trace) sends every captured SUBQ frame on PB3 as
// 8N1 serial at 115200 baud, one line of 24 hex digits each. This decodes the
// pin the way a USB serial adapter would and checks the lines.

#define TRACE_BAUD 115200U
#define TRACE_EDGES_MAX 65536
#define TRACE_TEXT_MAX 4096
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
      len++;
    }
  }
  text[len] = '\0';
  return framing_ok ? len : -1;
}

// Count the decoded lines, how many are the lead-in frame, and whether every one
// is 24 upper-case hex digits. strtok consumes text.
static int read_lines(char *text, int *lines, int *toc_lines) {
  int well_formed = 1;
  *lines = 0;
  *toc_lines = 0;
  for (char *line = strtok(text, "\n"); line != NULL; line = strtok(NULL, "\n")) {
    (*lines)++;
    well_formed = well_formed && (strlen(line) == TRACE_LINE_CHARS) &&
                  (strspn(line, "0123456789ABCDEF") == TRACE_LINE_CHARS);
    *toc_lines += (strcmp(line, TRACE_TOC_HEX) == 0) ? 1 : 0;
  }
  return well_formed;
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
}

// A lead-in long enough for several strings: the line must stay idle-high
// through each one and carry no byte inside it, so every line after a string
// still decodes (rule 7: the trace goes out in the gap after a frame, never
// inside a string).
#define TRACE_STRING_FRAMES 60

void scenario_trace_strings(const target_t *t, const char *elf, uint32_t freq) {
  avr_t *avr = trace_boot(t, elf, freq);
  clock_frames(avr, t, TRACE_TOC, TRACE_STRING_FRAMES);
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
}
