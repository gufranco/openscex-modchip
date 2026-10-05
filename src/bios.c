// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "pscu/bios.h"

#include <stdbool.h>
#include <stdint.h>

#include "port/port.h"

// Overall safety bounds so a console that never produces the expected AX
// pattern resets through the watchdog rather than hanging the patch forever
// (Power-of-Ten rule 2: every loop has a fixed upper bound).
#define PSCU_BIOS_EDGE_MAX ((uint16_t)0xFFFFU)
#define PSCU_BIOS_WINDOW_MAX ((uint16_t)0xFFFFU)

// Block until AX reads the wanted level, bounded, kicking the watchdog while it
// waits. Single-exit via the done flag, matching the engine's wait helpers.
static void pscu_bios_wait_level(uint8_t level) {
  bool done = false;
  for (uint16_t i = 0U; (i < PSCU_BIOS_EDGE_MAX) && !done; i++) {
    uint8_t now = (uint8_t)((pscu_port_bios_ax() != 0U) ? 1U : 0U);
    if (now == level) {
      done = true;
    } else {
      pscu_port_watchdog_reset();
    }
  }
}

// One silence window: AX must stay low for the whole PSCU_BIOS_SILENCE poll
// count. Any high reading means an address pulse arrived, so the window did not
// qualify as silent.
static bool pscu_bios_window_silent(void) {
  bool silent = true;
  for (uint16_t i = 0U; (i < PSCU_BIOS_SILENCE) && silent; i++) {
    if (pscu_port_bios_ax() != 0U) {
      silent = false;
    }
  }
  return silent;
}

void pscu_bios_patch(void) {
  // Align execution to an AX rising edge so the later pulse counting starts
  // from a known point in the memory cycle.
  pscu_bios_wait_level(0U);
  pscu_bios_wait_level(1U);

  // The target boot stage is preceded by a known number of silent AX windows.
  // Count PSCU_BIOS_CONFIRMS of them; a window broken by a pulse simply does
  // not count, and the chip waits for quiet before trying the next.
  uint8_t confirms = 0U;
  for (uint16_t attempt = 0U; (attempt < PSCU_BIOS_WINDOW_MAX) && (confirms < PSCU_BIOS_CONFIRMS);
       attempt++) {
    if (pscu_bios_window_silent()) {
      confirms = (uint8_t)(confirms + 1U);
    } else {
      pscu_bios_wait_level(0U);
    }
    pscu_port_watchdog_reset();
  }

  // Count PSCU_BIOS_PULSES address pulses and, on the last one, override the
  // data bus for the configured window. This part is cycle-accurate, so it
  // lives entirely in assembly.
  pscu_port_bios_override((uint8_t)PSCU_BIOS_PULSES);

#if PSCU_BIOS_TWO_PHASE
  // The two oldest Japanese BIOSes read the region a second time. After its own
  // run of silent windows, count the second pulse train on AY and override DX
  // again. Only built for those models; one external interrupt is enough
  // because both windows are polled, not driven by edge interrupts.
  uint8_t confirms_two = 0U;
  for (uint16_t attempt = 0U;
       (attempt < PSCU_BIOS_WINDOW_MAX) && (confirms_two < PSCU_BIOS_CONFIRMS_2);
       attempt++) {
    if (pscu_bios_window_silent()) {
      confirms_two = (uint8_t)(confirms_two + 1U);
    } else {
      pscu_bios_wait_level(0U);
    }
    pscu_port_watchdog_reset();
  }

  pscu_port_bios_override_ay((uint8_t)PSCU_BIOS_PULSES_2);
#endif
}
