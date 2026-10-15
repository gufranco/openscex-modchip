# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Tests for reading a runner's trace into per-pin changes."""

import unittest

from tools.bench.decode import Change
from tools.bench.trace import driven_ns, parse, pulled_ns


class ParseTest(unittest.TestCase):
    def test_lines_split_by_output_and_end(self) -> None:
        # Arrange
        lines = ["0 data 0 0", "10 gate 1 0", "20 data 1 0", "35 end 0 0"]

        # Act
        trace = parse(lines)

        # Assert
        self.assertEqual(trace.data, (Change(0, False, False), Change(20, True, False)))
        self.assertEqual(trace.gate, (Change(10, True, False),))
        self.assertEqual(trace.end_ns, 35)

    def test_an_unknown_output_is_ignored(self) -> None:
        # Act
        trace = parse(["5 led 1 1", "9 end 0 0"])

        # Assert
        self.assertEqual((trace.data, trace.gate), ((), ()))


class DrivenTest(unittest.TestCase):
    def test_pull_lines_are_kept_per_console_line(self) -> None:
        # Arrange
        lines = [
            "0 pull-sqck 1 1",
            "50 pull-sqck 0 0",
            "60 pull-subq 1 1",
            "90 end 0 0",
        ]

        # Act
        trace = parse(lines)

        # Assert
        self.assertEqual(sorted(trace.pulls), ["sqck", "subq"])
        self.assertEqual(pulled_ns(trace), 50 + 30)

    def test_a_trace_without_pull_lines_pulls_nothing(self) -> None:
        # Act
        trace = parse(["0 data 1 0", "10 end 0 0"])

        # Assert
        self.assertEqual(pulled_ns(trace), 0)

    def test_driven_time_outside_excluded_spans(self) -> None:
        # Arrange
        changes = [Change(0, True, False), Change(100, False, False)]

        # Act
        total = driven_ns(changes, 200, [(20, 40)])

        # Assert
        self.assertEqual(total, 80)

    def test_a_pin_that_never_changed_was_never_driven(self) -> None:
        # Act
        total = driven_ns([], 200, [])

        # Assert
        self.assertEqual(total, 0)

    def test_a_pin_still_driven_at_the_end_counts_to_the_end(self) -> None:
        # Arrange
        changes = [Change(150, True, True)]

        # Act
        total = driven_ns(changes, 200, [])

        # Assert
        self.assertEqual(total, 50)

    def test_a_trace_without_its_end_line_is_refused(self) -> None:
        # Arrange
        lines = ["0 data 1 0", "10 data 0 1"]

        # Act
        with self.assertRaises(ValueError) as raised:
            parse(lines)

        # Assert
        self.assertEqual(
            str(raised.exception), "trace has no end line: the runner stopped early"
        )

    def test_a_level_other_than_zero_or_one_is_refused(self) -> None:
        # Arrange
        lines = ["0 data 1 0", "10 data 1 x", "20 end 0 0"]

        # Act
        with self.assertRaises(ValueError) as raised:
            parse(lines)

        # Assert
        self.assertEqual(
            str(raised.exception),
            "trace line 2: expected time, output, driven, level",
        )

    def test_a_malformed_line_is_refused_with_its_number(self) -> None:
        # Arrange
        lines = ["0 data 1 0", "10 data 0", "20 end 0 0"]

        # Act
        with self.assertRaises(ValueError) as raised:
            parse(lines)

        # Assert
        self.assertEqual(
            str(raised.exception),
            "trace line 2: expected time, output, driven, level",
        )
