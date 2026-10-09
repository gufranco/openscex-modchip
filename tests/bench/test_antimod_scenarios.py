# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Tests for the anti-mod scenarios: the probe window both versions open in
the program area, and the lead-in re-read only the second one forces."""

import unittest

from tools.bench.antimod_scenarios import ANTIMOD_SCENARIOS, PROBE_FRAMES
from tools.bench.scenarios import (
    CARRIER_BOARDS,
    DOUBLE_SPEED,
    EARLY_GATE_BOARDS,
    LATE_GATE_BOARDS,
    SINGLE_SPEED,
    Phase,
)
from tools.bench.timeline import FRAME_NS, Signal


def scenario(name: str):  # noqa: ANN201
    return next(s for s in ANTIMOD_SCENARIOS if s.name == name)


class AntimodScenarioTest(unittest.TestCase):
    def test_both_versions_cover_every_board_family(self) -> None:
        boards = {
            version: [s.boards for s in ANTIMOD_SCENARIOS if version in s.name]
            for version in ("v1", "v2")
        }

        expected = [CARRIER_BOARDS, EARLY_GATE_BOARDS, LATE_GATE_BOARDS]
        self.assertEqual(boards, {"v1": expected, "v2": expected})

    def test_every_probe_window_lasts_the_probe(self) -> None:
        spans = {
            s.name: s.phases[Phase.AFTER_PROBE] - s.phases[Phase.PROBE]
            for s in ANTIMOD_SCENARIOS
        }

        self.assertEqual(set(spans.values()), {PROBE_FRAMES * FRAME_NS})

    def test_only_the_second_version_rereads_the_lead_in(self) -> None:
        rereads = {s.name: Phase.REREAD in s.phases for s in ANTIMOD_SCENARIOS}

        self.assertEqual(
            {name for name, reread in rereads.items() if reread},
            {s.name for s in ANTIMOD_SCENARIOS if "v2" in s.name},
        )

    def test_the_reread_comes_after_play_and_before_the_probe(self) -> None:
        phases = scenario("antimod-v2-carrier").phases

        order = sorted(phases, key=phases.__getitem__)

        self.assertEqual(
            order,
            [
                Phase.LEAD_IN,
                Phase.PROGRAM,
                Phase.REREAD,
                Phase.SECOND_PROGRAM,
                Phase.PROBE,
                Phase.AFTER_PROBE,
            ],
        )

    def test_a_gate_board_plays_the_probe_at_single_speed(self) -> None:
        timeline = scenario("antimod-v1-gate").build()
        probe = scenario("antimod-v1-gate").phases[Phase.PROBE]

        levels = [e.value for e in timeline.events if e.signal is Signal.SENSE]
        last_before = [
            e.value
            for e in timeline.events
            if e.signal is Signal.SENSE and e.time_ns <= probe
        ][-1]

        self.assertIn(DOUBLE_SPEED, levels)
        self.assertEqual(last_before, SINGLE_SPEED)

    def test_no_antimod_scenario_is_judged_against_the_envelope(self) -> None:
        judged = [s.name for s in ANTIMOD_SCENARIOS if s.judged]

        self.assertEqual(judged, [])
