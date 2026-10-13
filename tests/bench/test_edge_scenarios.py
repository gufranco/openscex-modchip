# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Tests for the edge scenarios and the catalogue. Each scenario is checked for
the input that makes it worth running, not its exact timeline."""

import unittest
from itertools import pairwise

from tools.bench.catalogue import find
from tools.bench.edge_scenarios import (
    AUDIO_COPY_LEAD_IN,
    BAD_CRC_LOW,
    BAD_CRC_ZEROED,
    EDGE_SCENARIOS,
    Record,
)
from tools.bench.scenarios import Phase
from tools.bench.timeline import FRAME_NS, Signal, with_crc


def signals(name: str) -> list[Signal]:
    return [event.signal for event in find(name).build().events]


class RecordTest(unittest.TestCase):
    def test_a_record_ends_with_its_check_byte(self) -> None:
        # Act
        raw = Record(board=1, trigger=12, frozen=0, trim=-3).raw()

        # Assert
        self.assertEqual(raw[:5], [0xC8, 1, 12, 0, 0xFD])
        self.assertEqual(raw[5], 0x5A ^ 0xC8 ^ 1 ^ 12 ^ 0 ^ 0xFD)

    def test_a_damaged_record_keeps_the_given_check(self) -> None:
        # Act
        raw = Record(board=1, trigger=10, frozen=0, trim=0, check=0).raw()

        # Assert
        self.assertEqual(raw[5], 0)


class EdgeScenarioTest(unittest.TestCase):
    def test_every_edge_scenario_marks_its_lead_in_and_runs_past_its_end(
        self,
    ) -> None:
        # Act
        for scenario in EDGE_SCENARIOS:
            timeline = scenario.build()

            # Assert
            self.assertIn(Phase.LEAD_IN, scenario.phases, scenario.name)
            self.assertGreater(scenario.duration_ns, timeline.now_ns, scenario.name)

    def test_no_edge_scenario_is_judged_against_the_envelope(self) -> None:
        # Act
        judged = [s.name for s in EDGE_SCENARIOS if s.judged]

        # Assert
        self.assertEqual(judged, [])

    def test_phases_come_from_the_timeline_marks(self) -> None:
        # Arrange
        scenario = find("carrier-power-cycle")

        # Act
        marks = scenario.build().marks

        # Assert
        self.assertEqual(scenario.phases, {Phase(k): v for k, v in marks.items()})

    def test_a_power_cycle_scenario_switches_the_console_off_and_on(self) -> None:
        # Act
        names = signals("carrier-power-cycle")

        # Assert
        self.assertIn(Signal.POWER_CYCLE, names)

    def test_the_stored_record_scenario_seeds_several_records(self) -> None:
        # Act
        names = signals("stored-records")

        # Assert
        self.assertGreater(names.count(Signal.EEPROM_BYTE), 7 * 5)
        self.assertGreater(names.count(Signal.POWER_CYCLE), 5)

    def test_the_trim_scenarios_play_an_off_rate_console(self) -> None:
        # Act
        names = (
            "carrier-trim-fast",
            "carrier-trim-slow",
            "carrier-trim-cal7",
            "carrier-trim-off-window",
            "carrier-trim-in-play",
        )
        for name in names:
            frames = [
                e.time_ns
                for e in find(name).build().events
                if e.signal is Signal.SQCK and e.value == 0
            ]
            gaps = {b - a for a, b in pairwise(frames) if b - a > FRAME_NS // 2}

            # Assert
            self.assertTrue(any(abs(g - FRAME_NS) > FRAME_NS // 50 for g in gaps), name)

    def test_the_in_play_trim_turns_slow_only_after_the_result_has_shown(self) -> None:
        # Arrange
        scenario = find("carrier-trim-in-play")
        program = scenario.phases[Phase.PROGRAM]
        lows = [
            e.time_ns
            for e in scenario.build().events
            if e.signal is Signal.SQCK and e.value == 0 and e.time_ns >= program
        ]
        gaps = [(a, b - a) for a, b in pairwise(lows) if b - a > FRAME_NS // 2]
        nominal = gaps[0][1]

        # Act
        first_slow = next(
            at for at, gap in gaps if abs(gap - nominal) > FRAME_NS // 100
        )

        # Assert
        self.assertGreaterEqual(first_slow - program, 9 * 1_000_000_000)

    def test_the_trim_replay_scenario_sets_a_factory_near_cal7(self) -> None:
        # Arrange
        events = find("trim-replay").build().events

        # Act
        factories = [e.value for e in events if e.signal is Signal.OSCCAL_FACTORY]

        # Assert
        self.assertIn(0x7F, factories)

    def test_the_supply_scenario_drops_below_the_guard_and_recovers(self) -> None:
        # Arrange
        events = find("carrier-supply-hold").build().events

        # Act
        levels = [e.value for e in events if e.signal is Signal.VCC_MV]

        # Assert
        self.assertLess(min(levels), 2750)
        self.assertEqual(levels[-1], 5000)

    def test_the_low_supply_lead_in_starts_below_the_guard(self) -> None:
        # Arrange
        events = find("carrier-supply-low-lead-in").build().events

        # Act
        levels = [e.value for e in events if e.signal is Signal.VCC_MV]

        # Assert
        self.assertEqual(levels, [5000, 2600, 5000])

    def test_the_bad_crc_frames_fail_their_crc_each_its_own_way(self) -> None:
        # Arrange
        frames = (BAD_CRC_ZEROED, BAD_CRC_LOW)

        # Act
        sealed = [with_crc(frame[:10]) for frame in frames]

        # Assert
        self.assertNotEqual(sealed[0][10:], BAD_CRC_ZEROED[10:])
        self.assertEqual(sealed[1][10], BAD_CRC_LOW[10])
        self.assertNotEqual(sealed[1][11], BAD_CRC_LOW[11])

    def test_the_copy_permitted_audio_lead_in_is_framed_but_neither_kind(
        self,
    ) -> None:
        # Arrange
        control, track = AUDIO_COPY_LEAD_IN[0], AUDIO_COPY_LEAD_IN[1]

        # Act
        sealed = with_crc(AUDIO_COPY_LEAD_IN[:10])

        # Assert
        self.assertEqual(sealed, AUDIO_COPY_LEAD_IN)
        self.assertEqual(track, 0)
        self.assertNotEqual(control, 0x01)
        self.assertNotEqual(control & 0xD0, 0x40)

    def test_the_stuck_clock_scenario_holds_sqck_low_mid_frame(self) -> None:
        # Arrange
        events = find("sqck-stuck").build().events

        # Act
        lows = [e for e in events if e.signal is Signal.SQCK and e.value == 0]
        highs = {e.time_ns for e in events if e.signal is Signal.SQCK and e.value == 1}
        stuck = [
            e for e in lows if not any(0 < h - e.time_ns < 1_000_000 for h in highs)
        ]

        # Assert
        self.assertEqual(len(stuck), 1)
