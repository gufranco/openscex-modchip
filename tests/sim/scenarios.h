// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_SIM_SCENARIOS_H
#define PSCU_SIM_SCENARIOS_H

#include <stdint.h>

#include "harness.h"

// Each scenario boots a fresh simulated chip and drives one behaviour.
void scenario_families(const target_t *t, const char *elf, uint32_t freq);
void result_case(const target_t *t, const char *elf, uint32_t freq, int program);
void scenario_resync(const target_t *t, const char *elf, uint32_t freq);
void scenario_late_carrier(const target_t *t, const char *elf, uint32_t freq);
void scenario_carrier_after_boot(const target_t *t, const char *elf, uint32_t freq);
void scenario_stall(const target_t *t, const char *elf, uint32_t freq);
void scenario_carrier_after_boot_stall(const target_t *t, const char *elf, uint32_t freq);
void scenario_vcd(const char *elf, uint32_t freq);
void scenario_multidisc(const target_t *t, const char *elf, uint32_t freq);
void scenario_disc_presence(const target_t *t, const char *elf, uint32_t freq);
void scenario_fast_sqck(const target_t *t, const char *elf, uint32_t freq);
void scenario_boot_light(const target_t *t, const char *elf, uint32_t freq);
void board_blinks_case(const target_t *t, const char *elf, uint32_t freq, int modern);
void faults_case(const target_t *t, const char *elf, uint32_t freq);
void scenario_supply(const target_t *t, const char *elf, uint32_t freq);
void scenario_supply_dip(const target_t *t, const char *elf, uint32_t freq);
void scenario_calib_cap(const target_t *t, const char *elf, uint32_t freq);
void scenario_calib_board(const target_t *t, const char *elf, uint32_t freq);
void scenario_calib_missed(const target_t *t, const char *elf, uint32_t freq);
void scenario_calib_jp(const target_t *t, const char *elf, uint32_t freq);
void scenario_store_low_supply(const target_t *t, const char *elf, uint32_t freq);
void scenario_torn_record(const target_t *t, const char *elf, uint32_t freq);
void scenario_eeprom_mode(const target_t *t, const char *elf, uint32_t freq);
void scenario_trim(const target_t *t, const char *elf, uint32_t freq);
void scenario_trim_cal7(const target_t *t, const char *elf, uint32_t freq);
void scenario_gate(const target_t *t, const char *elf, uint32_t freq, int modern);
void scenario_string_gap(const target_t *t, const char *elf, uint32_t freq);
void scenario_gap_check(const target_t *t, const char *elf, uint32_t freq);
void scenario_detect_timing(const target_t *t, const char *elf, uint32_t freq);
void scenario_bad_crc(const target_t *t, const char *elf, uint32_t freq);
void scenario_slow_sqck(const target_t *t, const char *elf, uint32_t freq);
// One build per region: every string of a full burst carries that region's
// word and no other (rule 6).
void scenario_region(
    const target_t *t, const char *elf, const char *expect, const char *name, uint32_t freq);
// The final and trace profiles against the debug one (tests/sim/sim_profile.c);
// trace_elf may be NULL.
void scenario_profiles(const target_t *t,
                       const char *debug_elf,
                       const char *final_elf,
                       const char *trace_elf,
                       uint32_t freq);
// The trace profile's serial output (tests/sim/sim_trace.c).
void scenario_trace(const target_t *t, const char *elf, uint32_t freq);
// The trace line across a burst of strings.
void scenario_trace_strings(const target_t *t, const char *elf, uint32_t freq);
// The trace with nothing learned, after a trim step, and across a disc leaving.
void scenario_trace_quiet(const target_t *t, const char *elf, uint32_t freq);
void scenario_trace_trim(const target_t *t, const char *elf, uint32_t freq);
void scenario_trace_clock(const target_t *t, const char *elf, uint32_t freq);
// Exact counts and lengths in the platform C (tests/sim/sim_timing.c).
void scenario_gap_polls(const target_t *t, const char *elf, uint32_t freq);
void scenario_detect_samples(const target_t *t, const char *elf, uint32_t freq);
void scenario_string_cells(const target_t *t, const char *elf, uint32_t freq);
void scenario_dark_play(const target_t *t, const char *elf, uint32_t freq);
void scenario_trim_outage(const target_t *t, const char *elf, uint32_t freq);

#endif
