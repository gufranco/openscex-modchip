# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Tests for the SCEx bit strings the bench decodes against.

The expected strings are the ones the simulator harness has checked against
the firmware since the first release (tests/sim/harness.c), so the bench and
the harness agree on what a correct string is.
"""

import unittest

from tools.bench.scex import SCEX_BITS, Region, identify, region_bits

SCEA = "10011010100100111101001010111010010111110100"
SCEI = "10011010100100111101001010111010010110110100"


class RegionBitsTest(unittest.TestCase):
    def test_america_matches_the_harness_string(self) -> None:
        bits = region_bits(Region.AMERICA)

        self.assertEqual(bits, SCEA)

    def test_japan_matches_the_harness_string(self) -> None:
        bits = region_bits(Region.JAPAN)

        self.assertEqual(bits, SCEI)

    def test_every_region_is_44_bits_and_distinct(self) -> None:
        strings = {region_bits(region) for region in Region}

        self.assertEqual(len(strings), 3)
        self.assertEqual({len(bits) for bits in strings}, {SCEX_BITS})


class IdentifyTest(unittest.TestCase):
    def test_a_region_string_is_named(self) -> None:
        region = identify(region_bits(Region.EUROPE))

        self.assertEqual(region, Region.EUROPE)

    def test_a_corrupted_string_is_no_region(self) -> None:
        bits = "0" + SCEA[1:]

        region = identify(bits)

        self.assertIsNone(region)
