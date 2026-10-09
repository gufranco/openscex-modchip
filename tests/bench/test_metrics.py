# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Tests for the per-run metrics the report and the envelope compare.

Strings are synthesized in gate mode at chosen times, so each metric can be
checked against the console phase it is judged by.
"""

import unittest

from tools.bench.decode import Change
from tools.bench.metrics import GATE_MARGIN_NS, framed, measure
from tools.bench.scenarios import Phase
from tools.bench.scex import Region, region_bits
from tools.bench.trace import Trace

CELL = 4_000_000
SECOND = 1_000_000_000


def string_at(start: int, bits: str | None = None) -> list[Change]:
    pattern = bits if bits is not None else region_bits(Region.AMERICA)
    changes = [
        Change(start + index * CELL, bit == "0", False)
        for index, bit in enumerate(pattern)
    ]
    return [*changes, Change(start + len(pattern) * CELL, False, False)]


PHASES = {
    Phase.LEAD_IN: 1 * SECOND,
    Phase.PROGRAM: 3 * SECOND,
    Phase.REREAD: 6 * SECOND,
}


class MeasureTest(unittest.TestCase):
    def test_strings_are_counted_by_phase(self) -> None:
        data = (
            string_at(int(1.5 * SECOND))
            + string_at(2 * SECOND)
            + string_at(4 * SECOND)
            + string_at(7 * SECOND)
        )
        trace = Trace(data, [], 9 * SECOND)

        metrics = measure(trace, PHASES, Region.AMERICA)

        self.assertEqual(metrics.strings, 4)
        self.assertEqual(metrics.valid, 4)
        self.assertEqual(metrics.before_program, 2)
        self.assertEqual(metrics.during_play, 1)
        self.assertEqual(metrics.during_reread, 1)
        self.assertTrue(metrics.would_accept)
        self.assertEqual(metrics.first_latency_ns, SECOND // 2)

    def test_a_string_before_the_lead_in_is_counted_apart(self) -> None:
        trace = Trace(string_at(0) + string_at(2 * SECOND), [], 5 * SECOND)

        metrics = measure(trace, PHASES, Region.AMERICA)

        self.assertEqual(metrics.before_lead_in, 1)
        self.assertEqual(metrics.before_program, 1)

    def test_a_wrong_region_neither_counts_as_valid_nor_accepts(self) -> None:
        trace = Trace(string_at(2 * SECOND, region_bits(Region.EUROPE)), [], 5 * SECOND)

        metrics = measure(trace, PHASES, Region.AMERICA)

        self.assertEqual((metrics.valid, metrics.garbled), (0, 0))
        self.assertFalse(metrics.would_accept)
        self.assertIsNone(metrics.first_latency_ns)

    def test_a_string_that_ends_after_the_program_mark_does_not_accept(self) -> None:
        trace = Trace(string_at(3 * SECOND - CELL), [], 5 * SECOND)

        metrics = measure(trace, PHASES, Region.AMERICA)

        self.assertFalse(metrics.would_accept)

    def test_a_garbled_string_is_counted(self) -> None:
        bits = region_bits(Region.AMERICA)[:30] + "0"

        metrics = measure(
            Trace(string_at(2 * SECOND, bits), [], 5 * SECOND), PHASES, Region.AMERICA
        )

        self.assertEqual(metrics.garbled, 1)

    def test_no_string_leaves_cell_figures_empty(self) -> None:
        metrics = measure(Trace([], [], 5 * SECOND), PHASES, Region.AMERICA)

        self.assertEqual(metrics.strings, 0)
        self.assertIsNone(metrics.cell_min_ns)
        self.assertIsNone(metrics.carrier)

    def test_drive_outside_strings_and_gate_drive_are_timed(self) -> None:
        data = [
            Change(0, True, False),
            Change(100, False, False),
            *string_at(2 * SECOND),
        ]
        gate = [Change(10, True, False), Change(60, False, False)]

        metrics = measure(Trace(data, gate, 5 * SECOND), PHASES, Region.AMERICA)

        self.assertEqual(metrics.data_driven_outside_ns, 100)
        self.assertEqual(metrics.gate_driven_ns, 50)
        self.assertEqual(metrics.cell_min_ns, CELL)

    def test_strings_inside_the_probe_window_are_counted_apart(self) -> None:
        phases = {
            **PHASES,
            Phase.PROBE: 8 * SECOND,
            Phase.AFTER_PROBE: 10 * SECOND,
        }
        europe = region_bits(Region.EUROPE)
        data = (
            string_at(int(7.5 * SECOND))
            + string_at(int(8.2 * SECOND))
            + string_at(int(8.6 * SECOND), europe)
            + string_at(int(10.5 * SECOND))
        )

        metrics = measure(Trace(data, [], 12 * SECOND), phases, Region.AMERICA)

        self.assertEqual(metrics.probe_strings, 2)

    def test_a_held_line_without_a_string_counts_nothing_in_the_probe(
        self,
    ) -> None:
        phases = {**PHASES, Phase.PROBE: 8 * SECOND, Phase.AFTER_PROBE: 10 * SECOND}
        data = [Change(7 * SECOND, True, False), Change(11 * SECOND, False, False)]

        metrics = measure(Trace(data, [], 12 * SECOND), phases, Region.AMERICA)

        self.assertEqual(metrics.probe_strings, 0)

    def test_only_valid_strings_in_the_reread_count_as_reauthentication(
        self,
    ) -> None:
        europe = region_bits(Region.EUROPE)
        data = (
            string_at(2 * SECOND)
            + string_at(int(6.5 * SECOND))
            + string_at(7 * SECOND, europe)
        )

        metrics = measure(Trace(data, [], 9 * SECOND), PHASES, Region.AMERICA)

        self.assertEqual(metrics.during_reread, 2)
        self.assertEqual(metrics.reread_valid, 1)

    def test_gate_drive_outside_strings_and_pull_ups_are_timed(self) -> None:
        start = 2 * SECOND
        end = start + 44 * CELL
        gate = [
            Change(start - 1_000, True, False),
            Change(end + 1_000, False, False),
            Change(end + CELL + 100, True, False),
            Change(end + CELL + 300, False, False),
        ]
        pulls = {"sqck": [Change(0, True, True), Change(700, False, False)]}
        trace = Trace(string_at(start), gate, 5 * SECOND, pulls)

        metrics = measure(trace, PHASES, Region.AMERICA)

        self.assertEqual(metrics.gate_driven_outside_ns, 200)
        self.assertEqual(metrics.pulled_ns, 700)

    def test_a_gate_held_between_strings_counts_beyond_the_margins(self) -> None:
        start = 2 * SECOND
        second = start + 74 * CELL
        gate = [Change(start, True, False), Change(second + 44 * CELL, False, False)]
        trace = Trace(string_at(start) + string_at(second), gate, 5 * SECOND)

        metrics = measure(trace, PHASES, Region.AMERICA)

        self.assertEqual(metrics.gate_driven_outside_ns, 30 * CELL - 2 * GATE_MARGIN_NS)


class FramedTest(unittest.TestCase):
    def test_spans_closer_than_two_margins_merge(self) -> None:
        spans = [(100_000_000, 200_000_000), (205_000_000, 300_000_000)]

        merged = framed(spans)

        self.assertEqual(
            merged, [(100_000_000 - GATE_MARGIN_NS, 300_000_000 + GATE_MARGIN_NS)]
        )

    def test_distant_spans_stay_apart(self) -> None:
        spans = [(100_000_000, 200_000_000), (300_000_000, 400_000_000)]

        merged = framed(spans)

        self.assertEqual(len(merged), 2)
