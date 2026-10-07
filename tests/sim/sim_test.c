// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "harness.h"
#include "scenarios.h"

// A software PlayStation, just enough to exercise the firmware end to end in
// simavr: it drives the console-side pins (SQCK, SUBQ, WFCK) and watches the
// firmware-side pins (DATA, LED), then decodes the injected bitstream and
// checks it equals the expected region word. A disc swap is a stretch with no
// SUBQ frame, as a drive that stops for the lid produces. The console side is
// timed in real time, converted to CPU cycles at the rate the simulated chip
// runs.

int main(int argc, char *argv[]) {
  if (argc < 3) {
    (void)fprintf(stderr, "usage: %s elf freq_hz [vcd_elf]\n", argv[0]);
    return 2;
  }
  const char *elf = argv[1];
  uint32_t freq = (uint32_t)strtoul(argv[2], NULL, 10);

  // The SCEx signals and the LED sit on the ATtiny85's PORTB. The image is driven through
  // the whole board-family matrix plus a non-TOC negative, so both board models
  // and both ends of the WFCK carrier band are verified, then through every
  // behaviour scenario.
  target_t t85 = { "attiny85", 'B', 0U, 1U, 2U, 3U, 4U };

  scenario_families(&t85, elf, freq);
  result_case(&t85, elf, freq, 1);
  result_case(&t85, elf, freq, 0);
  scenario_resync(&t85, elf, freq);
  scenario_late_carrier(&t85, elf, freq);
  scenario_carrier_after_boot(&t85, elf, freq);
  scenario_stall(&t85, elf, freq);
  scenario_multidisc(&t85, elf, freq);
  scenario_disc_presence(&t85, elf, freq);
  scenario_fast_sqck(&t85, elf, freq);
  scenario_boot_light(&t85, elf, freq);
  board_blinks_case(&t85, elf, freq, 0);
  board_blinks_case(&t85, elf, freq, 1);
  faults_case(&t85, elf, freq);
  scenario_supply(&t85, elf, freq);
  scenario_supply_dip(&t85, elf, freq);
  scenario_calib_cap(&t85, elf, freq);
  scenario_calib_board(&t85, elf, freq);
  scenario_calib_missed(&t85, elf, freq);
  scenario_gate(&t85, elf, freq, 0);
  scenario_gate(&t85, elf, freq, 1);
  scenario_string_gap(&t85, elf, freq);
  scenario_trim(&t85, elf, freq);

  // The optional third argument is the SCPH-5903 Video-CD image.
  if (argc >= 4) {
    scenario_vcd(argv[3], freq);
    scenario_calib_jp(&t85, argv[3], freq);
  }

  check_stack();
  return sim_report();
}
