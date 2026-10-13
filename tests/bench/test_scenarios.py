# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Tests for the scenario catalogue: deterministic, bounded, phases in order."""

import unittest
from functools import partial

from tools.bench.scenarios import (
    CARRIER_BOARDS,
    GATE_BOARDS,
    SCENARIOS,
    Board,
    Phase,
    by_name,
)
from tools.bench.timeline import Signal


class CatalogueTest(unittest.TestCase):
    def test_names_are_unique(self) -> None:
        # Act
        names = [scenario.name for scenario in SCENARIOS]

        # Assert
        self.assertEqual(len(names), len(set(names)))

    def test_every_scenario_fits_its_duration(self) -> None:
        # Act
        late = [s.name for s in SCENARIOS if s.build().now_ns > s.duration_ns]

        # Assert
        self.assertEqual(late, [])

    def test_phase_marks_are_in_time_order(self) -> None:
        # Act
        for scenario in SCENARIOS:
            marks = list(scenario.phases.values())

            # Assert
            self.assertEqual(marks, sorted(marks), scenario.name)

    def test_building_twice_gives_the_same_timeline(self) -> None:
        # Arrange
        scenario = by_name("carrier-accept")

        # Act
        first, second = scenario.build().lines(), scenario.build().lines()

        # Assert
        self.assertEqual(first, second)

    def test_a_gate_board_never_starts_the_carrier(self) -> None:
        # Arrange
        timeline = by_name("gate-accept").build()

        # Act
        halves = [e for e in timeline.events if e.signal is Signal.WFCK_HALF_NS]

        # Assert
        self.assertEqual(halves, [])

    def test_the_reread_scenario_marks_the_reread(self) -> None:
        # Act
        scenario = by_name("carrier-reread")

        # Assert
        self.assertIn(Phase.REREAD, scenario.phases)

    def test_the_two_gate_scenarios_split_the_gate_boards(self) -> None:
        # Act
        early = by_name("gate-accept-early").boards
        late = by_name("gate-accept").boards

        # Assert
        self.assertEqual(early, frozenset({Board.PU_7, Board.PU_8}))
        self.assertEqual(late, frozenset({Board.PU_18, Board.PU_20}))
        self.assertEqual(early | late, GATE_BOARDS)

    def test_every_carrier_scenario_stands_for_the_carrier_boards(self) -> None:
        # Act
        boards = {s.boards for s in SCENARIOS if not s.name.startswith("gate-")}

        # Assert
        self.assertEqual(boards, {CARRIER_BOARDS})

    def test_a_gate_board_holds_the_sense_line_at_single_speed_until_play(
        self,
    ) -> None:
        # Arrange
        scenario = by_name("gate-accept")
        timeline = scenario.build()

        # Act
        levels = [
            (e.time_ns, e.value) for e in timeline.events if e.signal is Signal.SENSE
        ]

        # Assert
        self.assertEqual(levels, [(0, 0), (scenario.phases[Phase.PROGRAM], 1)])

    def test_a_carrier_board_strobes_the_sense_line(self) -> None:
        # Arrange
        timeline = by_name("carrier-accept").build()

        # Act
        lows = [e for e in timeline.events if e.signal is Signal.SENSE and e.value == 0]

        # Assert
        self.assertGreater(len(lows), 100)

    def test_gate_and_carrier_boards_do_not_overlap(self) -> None:
        # Act
        shared = GATE_BOARDS & CARRIER_BOARDS

        # Assert
        self.assertEqual(shared, frozenset())

    def test_an_unknown_name_is_refused(self) -> None:
        # Arrange
        given = "no-such-scenario"

        # Act
        call = partial(by_name, given)

        # Assert
        self.assertRaises(KeyError, call)
