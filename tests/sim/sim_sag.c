// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "sim_sag.h"

#define SAG_LOW_MV 2600U
#define SAG_IDLE 0
#define SAG_LOW 1
#define SAG_RECOVERED 2

typedef struct {
  int stage;
  int drives_at_sag;
  uint32_t vcc;
  uint32_t avcc;
} sag_t;

static sag_t g_sag = { SAG_IDLE, 0, 0U, 0U };

static void sag_step(avr_t *avr) {
  if ((g_sag.stage == SAG_IDLE) && (avr->pc == g_addr_read_wfck)) {
    g_sag.stage = SAG_LOW;
    g_sag.drives_at_sag = g_data_drives;
    g_sag.vcc = avr->vcc;
    g_sag.avcc = avr->avcc;
    avr->vcc = SAG_LOW_MV;
    avr->avcc = SAG_LOW_MV;
  } else if ((g_sag.stage == SAG_LOW) && (g_data_drives > g_sag.drives_at_sag)) {
    g_sag.stage = SAG_RECOVERED;
    avr->vcc = g_sag.vcc;
    avr->avcc = g_sag.avcc;
  }
}

void sim_sag_carrier_write(void) {
  g_sag = (sag_t){ SAG_IDLE, 0, 0U, 0U };
  g_on_step = sag_step;
}

int sim_sag_recovered(void) {
  return g_sag.stage == SAG_RECOVERED;
}
