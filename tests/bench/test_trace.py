# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Tests for reading a runner's trace into per-pin changes."""

import unittest

from tools.bench.decode import Change
from tools.bench.trace import driven_ns, parse


class ParseTest(unittest.TestCase):
    def test_lines_split_by_output_and_end(self) -> None:
        lines = ["0 data 0 0", "10 gate 1 0", "20 data 1 0", "35 end 0 0"]

        trace = parse(lines)

        self.assertEqual(trace.data, [Change(0, False, False), Change(20, True, False)])
        self.assertEqual(trace.gate, [Change(10, True, False)])
        self.assertEqual(trace.end_ns, 35)

    def test_an_unknown_output_is_ignored(self) -> None:
        trace = parse(["5 led 1 1", "9 end 0 0"])

        self.assertEqual((trace.data, trace.gate), ([], []))


class DrivenTest(unittest.TestCase):
    def test_driven_time_outside_excluded_spans(self) -> None:
        changes = [Change(0, True, False), Change(100, False, False)]

        total = driven_ns(changes, 200, [(20, 40)])

        self.assertEqual(total, 80)

    def test_a_pin_that_never_changed_was_never_driven(self) -> None:
        total = driven_ns([], 200, [])

        self.assertEqual(total, 0)

    def test_a_pin_still_driven_at_the_end_counts_to_the_end(self) -> None:
        changes = [Change(150, True, True)]

        total = driven_ns(changes, 200, [])

        self.assertEqual(total, 50)
