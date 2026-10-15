# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Tests for sorting a chip's unexecuted instructions into unreachable, with a
reason, and still owed a scenario."""

import unittest

from tools.bench.reach import (
    BLOCK_MARKER,
    MARKER,
    RUNTIME_REASON,
    Entry,
    account,
    by_line,
    by_table,
    markers,
)


class MarkersTest(unittest.TestCase):
    def test_a_marker_names_the_next_code_line(self) -> None:
        # Arrange
        text = f"int a;\n// {MARKER} only after 49.7 days\n\n  x = y;\n"

        # Act
        found = markers({"src/a.c": text})

        # Assert
        self.assertEqual(found, {("src/a.c", 4): "only after 49.7 days"})

    def test_a_reason_runs_on_over_the_comment_lines_after_it(self) -> None:
        # Arrange
        text = f"// {MARKER} a stuck ADC,\n// never a working one\nldi r24, 0\n"

        # Act
        found = markers({"src/b.S": text})

        # Assert
        self.assertEqual(found, {("src/b.S", 3): "a stuck ADC, never a working one"})

    def test_a_block_marker_names_every_code_line_to_the_next_blank(self) -> None:
        # Arrange
        head = f"// {BLOCK_MARKER} a stuck ADC\n"
        text = head + "\tldi r24, 0\n\tldi r25, 0\n\tret\n\n\tnop\n"

        # Act
        found = markers({"src/b.S": text})

        # Assert
        self.assertEqual(
            found,
            {
                ("src/b.S", 2): "a stuck ADC",
                ("src/b.S", 3): "a stuck ADC",
                ("src/b.S", 4): "a stuck ADC",
            },
        )

    def test_an_assembly_reason_runs_on_over_semicolon_comments(self) -> None:
        # Arrange
        text = f"; {MARKER} a stuck ADC,\n; never a working one\nldi r24, 0\n"

        # Act
        found = markers({"src/b.S": text})

        # Assert
        self.assertEqual(found, {("src/b.S", 3): "a stuck ADC, never a working one"})

    def test_a_file_without_markers_names_nothing(self) -> None:
        # Act
        found = markers({"src/c.c": "int a;\n"})

        # Assert
        self.assertEqual(found, {})


class ByLineTest(unittest.TestCase):
    def test_uncovered_instructions_on_a_marked_line_are_excluded(self) -> None:
        # Arrange
        lines = {0x10: ("src/a.c", 4), 0x12: ("src/a.c", 4), 0x14: ("src/a.c", 5)}

        # Act
        excluded = by_line([0x10, 0x14], lines, {("src/a.c", 4): "why"})

        # Assert
        self.assertEqual(excluded, {0x10: "why"})

    def test_an_instruction_without_source_is_runtime(self) -> None:
        # Arrange
        lines = {0x2: None, 0x44: None}

        # Act
        excluded = by_line([0x2, 0x44], lines, {})

        # Assert
        self.assertEqual(excluded, {0x2: RUNTIME_REASON, 0x44: RUNTIME_REASON})

    def test_a_sourceless_instruction_outside_runtime_code_is_not_excused(self) -> None:
        # Arrange
        lines = {0x2: None, 0x300: None}

        # Act
        excluded = by_line([0x2, 0x300], lines, {}, frozenset({0x2}))

        # Assert
        self.assertEqual(excluded, {0x2: RUNTIME_REASON})


class ByTableTest(unittest.TestCase):
    def test_a_line_entry_and_an_address_entry_each_exclude(self) -> None:
        # Arrange
        lines = {0x10: ("PSNee.ino", 120), 0x20: ("PSNee.ino", 121)}
        table = [
            Entry(line="PSNee.ino:120", start=None, end=None, reason="lid code"),
            Entry(line=None, start=0x30, end=0x32, reason="calibration word"),
        ]

        # Act
        excluded = by_table([0x10, 0x20, 0x31], lines, table)

        # Assert
        self.assertEqual(excluded, {0x10: "lid code", 0x31: "calibration word"})

    def test_a_line_entry_matches_its_file_wherever_it_was_fetched(self) -> None:
        # Arrange
        lines = {0x10: (".work/clone/PSNee/PSNee.ino", 597)}
        table = [Entry(line="PSNee.ino:597", start=None, end=None, reason="loop")]

        # Act
        excluded = by_table([0x10], lines, table)

        # Assert
        self.assertEqual(excluded, {0x10: "loop"})

    def test_a_file_entry_excuses_every_line_of_that_file(self) -> None:
        # Arrange
        lines = {
            0x10: ("/w/arduino-core/cores/arduino/Tone.cpp", 547),
            0x12: ("/w/arduino-core/cores/arduino/Tone.cpp", 558),
            0x14: ("/w/ubernee-build/sketch.ino", 244),
        }
        table = [Entry(line=None, start=None, end=None, reason="core", file="Tone.cpp")]

        # Act
        excluded = by_table([0x10, 0x12, 0x14], lines, table)

        # Assert
        self.assertEqual(excluded, {0x10: "core", 0x12: "core"})

    def test_an_entry_with_neither_line_nor_range_excuses_nothing(self) -> None:
        # Arrange
        table = [Entry(line=None, start=None, end=None, reason="malformed")]

        # Act
        excluded = by_table([0x10], {}, table)

        # Assert
        self.assertEqual(excluded, {})


class AccountTest(unittest.TestCase):
    def test_every_instruction_is_executed_excluded_or_remaining(self) -> None:
        # Act
        reach = account(frozenset({0, 2, 4, 6}), frozenset({0, 2}), {4: "why"})

        # Assert
        self.assertEqual(reach.executed, 2)
        self.assertEqual(reach.excluded, {4: "why"})
        self.assertEqual(reach.remaining, (6,))
        self.assertAlmostEqual(reach.percent, 75.0)

    def test_an_exclusion_on_an_executed_instruction_does_not_count(self) -> None:
        # Act
        reach = account(frozenset({0, 2}), frozenset({0, 2}), {2: "why"})

        # Assert
        self.assertEqual(reach.excluded, {})
        self.assertEqual(reach.percent, 100.0)

    def test_an_empty_image_is_fully_reached(self) -> None:
        # Act
        reach = account(frozenset(), frozenset(), {})

        # Assert
        self.assertEqual(reach.percent, 100.0)
