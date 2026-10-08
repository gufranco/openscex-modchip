# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Tests for the console timeline the runners play into a chip.

The SUBQ encoding must match the simulator harness bit for bit (least
significant bit first, SUBQ set before SQCK falls, one frame every 1/75 s), or
ours would behave differently in the bench than in tests/sim.
"""

import unittest

from tools.bench.timeline import (
    FRAME_NS,
    LEAD_IN,
    Signal,
    Timeline,
)


class FrameTest(unittest.TestCase):
    def test_a_frame_clocks_96_bits_lsb_first(self) -> None:
        timeline = Timeline()

        timeline.frame(LEAD_IN)

        subq = [e.value for e in timeline.events if e.signal is Signal.SUBQ]
        falls = [e for e in timeline.events if e.signal is Signal.SQCK and e.value == 0]
        self.assertEqual(len(subq), 96)
        self.assertEqual(len(falls), 96)
        self.assertEqual(subq[:8], [1, 0, 0, 0, 0, 0, 1, 0])

    def test_frames_start_one_75th_of_a_second_apart(self) -> None:
        timeline = Timeline()

        timeline.frames(LEAD_IN, 2)

        starts = [e.time_ns for e in timeline.events if e.signal is Signal.SUBQ]
        self.assertEqual(starts[96] - starts[0], FRAME_NS)
        self.assertEqual(timeline.now_ns, 2 * FRAME_NS)

    def test_subq_is_set_before_the_falling_edge(self) -> None:
        timeline = Timeline()

        timeline.frame(LEAD_IN)

        first_subq = next(e for e in timeline.events if e.signal is Signal.SUBQ)
        first_fall = next(
            e for e in timeline.events if e.signal is Signal.SQCK and e.value == 0
        )
        self.assertLessEqual(first_subq.time_ns, first_fall.time_ns)


class ControlTest(unittest.TestCase):
    def test_set_and_idle_move_the_cursor(self) -> None:
        timeline = Timeline()

        timeline.set(Signal.LID, 1).idle(1_000).set(Signal.LID, 0)

        self.assertEqual(
            [(e.time_ns, e.signal, e.value) for e in timeline.events],
            [(0, Signal.LID, 1), (1_000, Signal.LID, 0)],
        )

    def test_sense_strobes_ride_on_each_frame_when_enabled(self) -> None:
        timeline = Timeline(strobes_per_frame=2, strobe_width_ns=4_000)

        timeline.frame(LEAD_IN)

        lows = [
            e.time_ns
            for e in timeline.events
            if e.signal is Signal.SENSE and e.value == 0
        ]
        self.assertEqual(len(lows), 2)
        self.assertEqual(lows[1] - lows[0], FRAME_NS // 2)

    def test_lines_are_time_sorted_text(self) -> None:
        timeline = Timeline().set(Signal.WFCK_HALF_NS, 68_000)

        text = timeline.lines()

        self.assertEqual(text, ["0 wfck_half_ns 68000"])
