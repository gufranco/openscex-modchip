# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Tests for the per-run metrics the report and the envelope compare.

Strings are synthesized in gate mode at chosen times, so each metric can be
checked against the console phase it is judged by.
"""

import unittest

from tools.bench.decode import Change
from tools.bench.metrics import measure
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
