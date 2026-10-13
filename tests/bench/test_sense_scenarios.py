# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Tests for the sense-line scenarios: the SPEED spells, reset presses and lid
openings that walk Mayumi V4 and MM3 through their state machines."""

import unittest

from tools.bench.scenarios import DOUBLE_SPEED, SINGLE_SPEED, Phase, Scenario
from tools.bench.sense_scenarios import (
    CARRIER_PATHS,
    GATE_PATHS,
    SENSE_SCENARIOS,
    SPEED_SPELLS_MS,
)
from tools.bench.timeline import Signal


def scenario(name: str) -> Scenario:
    return next(s for s in SENSE_SCENARIOS if s.name == name)


class SenseScenarioTest(unittest.TestCase):
    def test_every_sense_scenario_marks_its_first_disc(self) -> None:
        # Act
        marked = [s.name for s in SENSE_SCENARIOS if Phase.LEAD_IN in s.phases]

        # Assert
        self.assertEqual(marked, [s.name for s in SENSE_SCENARIOS])

    def test_every_sense_scenario_drives_the_sense_line(self) -> None:
        # Act
        drives = {s.drives for s in SENSE_SCENARIOS}

        # Assert
        self.assertEqual(drives, {"sense"})

    def test_no_sense_scenario_is_judged_against_the_envelope(self) -> None:
        # Act
        judged = [s.name for s in SENSE_SCENARIOS if s.judged]

        # Assert
        self.assertEqual(judged, [])

    def test_the_speed_spells_alternate_levels_once_per_spell(self) -> None:
        # Arrange
        events = scenario("gate-speed-spells").build().events

        # Act
        sense = [e.value for e in events if e.signal is Signal.SENSE]
        lows = [value for value in sense if value == SINGLE_SPEED]

        # Assert
        self.assertGreaterEqual(len(lows), len(SPEED_SPELLS_MS) + 2)

    def test_the_gate_paths_power_cycle_between_paths(self) -> None:
        # Arrange
        events = scenario("gate-sense-paths").build().events

        # Act
        cycles = [e for e in events if e.signal is Signal.POWER_CYCLE]

        # Assert
        self.assertEqual(len(cycles), len(GATE_PATHS) - 1)

    def test_the_carrier_paths_press_reset_and_open_the_lid(self) -> None:
        # Act
        names = [e.signal for e in scenario("carrier-sense-paths").build().events]

        # Assert
        self.assertIn(Signal.RESET, names)
        self.assertIn(Signal.LID, names)
        self.assertEqual(names.count(Signal.POWER_CYCLE), len(CARRIER_PATHS) - 1)

    def test_the_reset_modes_hold_reset_four_times(self) -> None:
        # Arrange
        events = scenario("gate-reset-modes").build().events

        # Act
        presses = [e for e in events if e.signal is Signal.RESET and e.value == 0]

        # Assert
        self.assertEqual(len(presses), 4)

    def test_a_gate_disc_plays_at_double_speed(self) -> None:
        # Arrange
        events = scenario("gate-reset-modes").build().events

        # Act
        levels = {e.value for e in events if e.signal is Signal.SENSE}

        # Assert
        self.assertEqual(levels, {SINGLE_SPEED, DOUBLE_SPEED})
