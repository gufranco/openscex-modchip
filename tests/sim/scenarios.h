// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#ifndef PSCU_SIM_SCENARIOS_H
#define PSCU_SIM_SCENARIOS_H

#include <stdint.h>

#include "harness.h"

// Every scenario boots a fresh simulated chip, drives one behaviour end to
// end and records its checks through the harness.
void scenario_families(const target_t *t, const char *elf, uint32_t freq);
void result_case(const target_t *t, const char *elf, uint32_t freq, int program);
void scenario_resync(const target_t *t, const char *elf, uint32_t freq);
void scenario_late_carrier(const target_t *t, const char *elf, uint32_t freq);
void scenario_carrier_after_boot(const target_t *t, const char *elf, uint32_t freq);
void scenario_stall(const target_t *t, const char *elf, uint32_t freq);
void scenario_vcd(const char *elf, uint32_t freq);
void scenario_multidisc(const target_t *t, const char *elf, uint32_t freq);
void scenario_disc_presence(const target_t *t, const char *elf, uint32_t freq);
void scenario_fast_sqck(const target_t *t, const char *elf, uint32_t freq);
void scenario_boot_light(const target_t *t, const char *elf, uint32_t freq);
void board_blinks_case(const target_t *t, const char *elf, uint32_t freq, int modern);
void faults_case(const target_t *t, const char *elf, uint32_t freq);
void scenario_supply(const target_t *t, const char *elf, uint32_t freq);
void scenario_calib_cap(const target_t *t, const char *elf, uint32_t freq);
void scenario_calib_board(const target_t *t, const char *elf, uint32_t freq);
void scenario_calib_missed(const target_t *t, const char *elf, uint32_t freq);
void scenario_calib_jp(const target_t *t, const char *elf, uint32_t freq);
void scenario_trim(const target_t *t, const char *elf, uint32_t freq);
void scenario_gate(const target_t *t, const char *elf, uint32_t freq, int modern);
void scenario_string_gap(const target_t *t, const char *elf, uint32_t freq);

#endif
