# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Tests for the scenario catalogue every chip runs.

Each scenario must be deterministic, end after its declared duration, and mark
the console phases the metrics judge against, in order.
"""

import unittest

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
        names = [scenario.name for scenario in SCENARIOS]

        self.assertEqual(len(names), len(set(names)))

    def test_every_scenario_fits_its_duration(self) -> None:
        late = [s.name for s in SCENARIOS if s.build().now_ns > s.duration_ns]

        self.assertEqual(late, [])

    def test_phase_marks_are_in_time_order(self) -> None:
        for scenario in SCENARIOS:
            marks = list(scenario.phases.values())

            self.assertEqual(marks, sorted(marks), scenario.name)

    def test_building_twice_gives_the_same_timeline(self) -> None:
        scenario = by_name("carrier-accept")

        first, second = scenario.build().lines(), scenario.build().lines()

        self.assertEqual(first, second)

    def test_a_gate_board_never_starts_the_carrier(self) -> None:
        timeline = by_name("gate-accept").build()

        halves = [e for e in timeline.events if e.signal is Signal.WFCK_HALF_NS]

        self.assertEqual(halves, [])

    def test_the_reread_scenario_marks_the_reread(self) -> None:
        scenario = by_name("carrier-reread")

        self.assertIn(Phase.REREAD, scenario.phases)

    def test_the_two_gate_scenarios_split_the_gate_boards(self) -> None:
        early = by_name("gate-accept-early").boards
        late = by_name("gate-accept").boards

        self.assertEqual(early, frozenset({Board.PU_7, Board.PU_8}))
        self.assertEqual(late, frozenset({Board.PU_18, Board.PU_20}))
        self.assertEqual(early | late, GATE_BOARDS)

    def test_every_carrier_scenario_stands_for_the_carrier_boards(self) -> None:
        boards = {s.boards for s in SCENARIOS if not s.name.startswith("gate-")}

        self.assertEqual(boards, {CARRIER_BOARDS})

    def test_a_gate_board_holds_the_sense_line_at_single_speed_until_play(
        self,
    ) -> None:
        scenario = by_name("gate-accept")
        timeline = scenario.build()

        levels = [
            (e.time_ns, e.value) for e in timeline.events if e.signal is Signal.SENSE
        ]

        self.assertEqual(levels, [(0, 0), (scenario.phases[Phase.PROGRAM], 1)])

    def test_a_carrier_board_strobes_the_sense_line(self) -> None:
        timeline = by_name("carrier-accept").build()

        lows = [e for e in timeline.events if e.signal is Signal.SENSE and e.value == 0]

        self.assertGreater(len(lows), 100)

    def test_gate_and_carrier_boards_do_not_overlap(self) -> None:
        shared = GATE_BOARDS & CARRIER_BOARDS

        self.assertEqual(shared, frozenset())

    def test_an_unknown_name_is_refused(self) -> None:
        with self.assertRaises(KeyError):
            by_name("no-such-scenario")
