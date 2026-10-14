// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "harness.h"
#include "scenarios.h"

// The simulator suite's entry point.

int main(int argc, char *argv[]) {
  // Every image is required: a missing one would skip its scenarios and still
  // report a clean run.
  if (argc != 8) {
    (void)fprintf(
        stderr, "usage: %s elf freq_hz vcd_elf final_elf trace_elf eu_elf jp_elf\n", argv[0]);
    return 2;
  }
  const char *elf = argv[1];
  uint32_t freq = (uint32_t)strtoul(argv[2], NULL, 10);

  // The SCEx signals and the indicator sit on PORTB. The image runs the board-family
  // matrix, then every behaviour scenario.
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
  scenario_store_low_supply(&t85, elf, freq);
  scenario_torn_record(&t85, elf, freq);
  scenario_eeprom_mode(&t85, elf, freq);
  scenario_gate(&t85, elf, freq, 0);
  scenario_gate(&t85, elf, freq, 1);
  scenario_string_gap(&t85, elf, freq);
  scenario_gap_check(&t85, elf, freq);
  scenario_detect_timing(&t85, elf, freq);
  scenario_bad_crc(&t85, elf, freq);
  scenario_slow_sqck(&t85, elf, freq);
  scenario_trim(&t85, elf, freq);
  scenario_trim_cal7(&t85, elf, freq);
  scenario_gap_polls(&t85, elf, freq);
  scenario_detect_samples(&t85, elf, freq);
  scenario_string_cells(&t85, elf, freq);
  scenario_dark_play(&t85, elf, freq);
  scenario_trim_outage(&t85, elf, freq);

  // The SCPH-5903 Video-CD image.
  scenario_vcd(argv[3], freq);
  scenario_calib_jp(&t85, argv[3], freq);

  // The final and trace images of the default build, checked against the
  // default (debug) image.
  scenario_profiles(&t85, elf, argv[4], argv[5], freq);
  scenario_trace(&t85, argv[5], freq);
  scenario_trace_strings(&t85, argv[5], freq);
  scenario_trace_quiet(&t85, argv[5], freq);
  scenario_trace_trim(&t85, argv[5], freq);
  scenario_trace_clock(&t85, argv[5], freq);

  // One image per region; the default image is the American one.
  scenario_region(&t85, elf, SCEA_BITS, "us", freq);
  scenario_region(&t85, argv[6], SCEE_BITS, "eu", freq);
  scenario_region(&t85, argv[7], SCEI_BITS, "jp", freq);

  check_stack();
  return sim_report();
}
