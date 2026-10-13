# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Tests for the SCEx bit strings, which must match tests/sim/harness.c."""

import unittest

from tools.bench.scex import SCEX_BITS, Region, identify, region_bits

SCEA = "10011010100100111101001010111010010111110100"
SCEI = "10011010100100111101001010111010010110110100"


class RegionBitsTest(unittest.TestCase):
    def test_america_matches_the_harness_string(self) -> None:
        # Act
        bits = region_bits(Region.AMERICA)

        # Assert
        self.assertEqual(bits, SCEA)

    def test_japan_matches_the_harness_string(self) -> None:
        # Act
        bits = region_bits(Region.JAPAN)

        # Assert
        self.assertEqual(bits, SCEI)

    def test_every_region_is_44_bits_and_distinct(self) -> None:
        # Act
        strings = {region_bits(region) for region in Region}

        # Assert
        self.assertEqual(len(strings), 3)
        self.assertEqual({len(bits) for bits in strings}, {SCEX_BITS})


class IdentifyTest(unittest.TestCase):
    def test_a_region_string_is_named(self) -> None:
        # Act
        region = identify(region_bits(Region.EUROPE))

        # Assert
        self.assertEqual(region, Region.EUROPE)

    def test_a_corrupted_string_is_no_region(self) -> None:
        # Arrange
        bits = "0" + SCEA[1:]

        # Act
        region = identify(bits)

        # Assert
        self.assertIsNone(region)
