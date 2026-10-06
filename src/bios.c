// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "pscu/bios.h"

#include <stdbool.h>
#include <stdint.h>

#include "port/port.h"

// Overall safety bounds (Power-of-Ten rule 2: every loop has a fixed upper
// bound). Every wait returns whether it saw what it waited for, and any failure
// abandons the patch and hands over to the SCEx run loop: overriding the data
// bus at an unknown boot stage would corrupt the boot rather than patch it, so
// giving up is the safe outcome, never guessing.
//
// The first edge waits are long because the chip powers up with the console and
// the console CPU starts reading its boot ROM only some time later. Read: the
// PsNee changelog of 16 July 2017 expects the address signal "~1 second after
// power on"; three times that is the bound here. The count is that time divided
// by the cost of one waiting pass of pscu_bios_wait_level, 38 cycles, counted
// from the avr-gcc 14.2 -Os listing of the one-phase build (the 32-bit counter
// compare and the watchdog call dominate); recount it if the loop changes.
#define PSCU_BIOS_POLL_CYCLES (38UL)
#define PSCU_BIOS_START_POLLS ((uint32_t)((3UL * F_CPU) / PSCU_BIOS_POLL_CYCLES))
// Later waits sit inside the boot ROM's own activity, where the next AX edge is
// microseconds away; 65535 polls (about 0.15 s at 8 MHz) is ample.
#define PSCU_BIOS_EDGE_MAX ((uint32_t)0xFFFFU)
#define PSCU_BIOS_WINDOW_MAX ((uint16_t)0xFFFFU)

// The assembly counts the pulses after the first one of each train, so every
// train must have at least two.
#if (PSCU_BIOS_PULSES < 2)
#error "PSCU_BIOS_PULSES must be at least 2"
#endif
#if PSCU_BIOS_TWO_PHASE && (PSCU_BIOS_PULSES_2 < 2)
#error "PSCU_BIOS_PULSES_2 must be at least 2"
#endif

// Block until the address line (AY when ay is true, otherwise AX) reads the
// wanted level, for at most max_polls polls, kicking the watchdog while it
// waits. Returns whether the level was seen.
static bool pscu_bios_wait_level(bool ay, uint8_t level, uint32_t max_polls) {
  bool done = false;
  for (uint32_t i = 0U; (i < max_polls) && !done; i++) {
    uint8_t raw = ay ? pscu_port_bios_ay() : pscu_port_bios_ax();
    uint8_t now = (uint8_t)((raw != 0U) ? 1U : 0U);
    if (now == level) {
      done = true;
    } else {
      pscu_port_watchdog_reset();
    }
  }
  return done;
}

// One silence window: AX is polled PSCU_BIOS_SILENCE times and the window counts
// as silent while no more than PSCU_BIOS_NOISE_TOLERANCE highs appear. A brief
// line glitch is tolerated so it does not reset the boot-stage detection, but a
// real address pulse train quickly exceeds the tolerance and breaks the window.
static bool pscu_bios_window_silent(void) {
  uint16_t highs = 0U;
  for (uint16_t i = 0U; (i < PSCU_BIOS_SILENCE) && (highs <= PSCU_BIOS_NOISE_TOLERANCE); i++) {
    if (pscu_port_bios_ax() != 0U) {
      highs = (uint16_t)(highs + 1U);
    }
  }
  return highs <= PSCU_BIOS_NOISE_TOLERANCE;
}

// Count target silent windows. A window broken by a pulse does not count; the
// chip waits for AX to drop before trying the next. Returns false if AX never
// drops or the windows never reach the target, so the caller does not override
// at a stage it could not identify.
static bool pscu_bios_count_silence(uint8_t target) {
  uint8_t confirms = 0U;
  bool ok = true;
  for (uint16_t attempt = 0U; (attempt < PSCU_BIOS_WINDOW_MAX) && (confirms < target) && ok;
       attempt++) {
    if (pscu_bios_window_silent()) {
      confirms = (uint8_t)(confirms + 1U);
    } else {
      ok = pscu_bios_wait_level(false, 0U, PSCU_BIOS_EDGE_MAX);
    }
    pscu_port_watchdog_reset();
  }
  return ok && (confirms >= target);
}

void pscu_bios_patch(void) {
  // Patch only on a power-up boot. After a watchdog reset the console is already
  // running, so reading the reset cause comes first and skips everything else.
  bool ok = pscu_port_reset_was_watchdog() == 0U;

  // Align execution to an AX rising edge so the later pulse counting starts
  // from a known point in the memory cycle.
  if (ok) {
    ok = pscu_bios_wait_level(false, 0U, PSCU_BIOS_START_POLLS);
  }
  if (ok) {
    ok = pscu_bios_wait_level(false, 1U, PSCU_BIOS_START_POLLS);
  }

  // The target boot stage is preceded by a known number of silent AX windows.
  if (ok) {
    ok = pscu_bios_count_silence((uint8_t)PSCU_BIOS_CONFIRMS);
  }

  // Wait here, bounded and with the watchdog fed, for the first pulse of the
  // train: it can start some time after the last counted window. The assembly
  // then counts the remaining PSCU_BIOS_PULSES - 1 pulses, whose gaps are
  // microseconds, and on the last one overrides the data bus for the configured
  // window. That part is cycle-accurate, so it lives entirely in assembly.
  if (ok) {
    ok = pscu_bios_wait_level(false, 0U, PSCU_BIOS_START_POLLS);
  }
  if (ok) {
    ok = pscu_bios_wait_level(false, 1U, PSCU_BIOS_START_POLLS);
  }
  if (ok) {
    pscu_port_bios_override((uint8_t)(PSCU_BIOS_PULSES - 1U));
  }

#if PSCU_BIOS_TWO_PHASE
  // The two oldest Japanese BIOSes read the region a second time. After its own
  // run of silent windows, wait for the first falling edge of the second pulse
  // train on AY, then let the assembly count the rest and override DX again.
  // Only built for those models; one external interrupt is enough because both
  // windows are polled, not driven by edge interrupts.
  if (ok) {
    ok = pscu_bios_count_silence((uint8_t)PSCU_BIOS_CONFIRMS_2);
  }
  if (ok) {
    ok = pscu_bios_wait_level(true, 1U, PSCU_BIOS_START_POLLS);
  }
  if (ok) {
    ok = pscu_bios_wait_level(true, 0U, PSCU_BIOS_START_POLLS);
  }
  if (ok) {
    pscu_port_bios_override_ay((uint8_t)(PSCU_BIOS_PULSES_2 - 1U));
  }
#endif
}
