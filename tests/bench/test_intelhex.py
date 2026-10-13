# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Tests for the Intel HEX reader the bench lays images out with.

The records are written by hand with their checksums worked out: the record's
bytes, checksum included, sum to zero modulo 256.
"""

import unittest

from tools.bench.intelhex import memory

END = ":00000001FF\n"


class MemoryTest(unittest.TestCase):
    def test_a_linear_base_moves_the_data(self) -> None:
        # Arrange
        text = ":020000040001F9\n:0100000000FF\n" + END

        # Act
        filled = memory(text)

        # Assert
        self.assertEqual(filled, {0x10000: 0x00})

    def test_a_segment_base_is_shifted_by_four(self) -> None:
        # Arrange
        text = ":020000020001FB\n:0100000022DD\n" + END

        # Act
        filled = memory(text)

        # Assert
        self.assertEqual(filled, {0x10: 0x22})

    def test_a_start_address_record_places_nothing(self) -> None:
        # Arrange
        text = ":04000005000000CD2A\n:0100020011EC\n" + END

        # Act
        filled = memory(text)

        # Assert
        self.assertEqual(filled, {0x02: 0x11})

    def test_records_after_the_end_record_are_ignored(self) -> None:
        # Arrange
        text = ":0100020011EC\n" + END + "trailing text\n"

        # Act
        filled = memory(text)

        # Assert
        self.assertEqual(filled, {0x02: 0x11})

    def test_each_malformed_file_is_refused_with_its_line(self) -> None:
        # Arrange
        cases = {
            "no colon": ("0100020011EC\n" + END, "line 1: a record starts with ':'"),
            "not hex": (":01000200G1EC\n" + END, "line 1: not hexadecimal"),
            "short": (
                ":0200020011EB\n" + END,
                "line 1: length does not match its byte count",
            ),
            "header only": (
                ":0000\n" + END,
                "line 1: length does not match its byte count",
            ),
            "checksum": (":0100020011ED\n" + END, "line 1: checksum does not add up"),
            "truncated": (":0100020011EC\n", "no end record: the file is truncated"),
        }

        # Act
        refused = {name: _refusal(text) for name, (text, _) in cases.items()}

        # Assert
        self.assertEqual(refused, {name: reason for name, (_, reason) in cases.items()})


def _refusal(text: str) -> str:
    """The message memory raises for a file, or "" when it reads it."""
    try:
        memory(text)
    except ValueError as error:
        return str(error)
    return ""
