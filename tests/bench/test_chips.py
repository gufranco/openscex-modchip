# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Tests for the chip catalogue the bench runs.

Every chip must name a firmware the bench can find or build, and the
third-party ones must be listed in the artifact manifest, so a chip cannot be
added without stating where its bytes come from.
"""

import unittest

from tools.bench.chips import CHIPS, Simulator, by_name
from tools.bench.manifest import ROOT, load
from tools.bench.scenarios import Board
from tools.bench.scex import Region


class CatalogueTest(unittest.TestCase):
    def test_names_are_unique(self) -> None:
        names = [chip.name for chip in CHIPS]

        self.assertEqual(len(names), len(set(names)))

    def test_every_chip_maps_data(self) -> None:
        missing = [chip.name for chip in CHIPS if "data=" not in chip.pins]

        self.assertEqual(missing, [])

    def test_third_party_images_are_in_the_manifest(self) -> None:
        artifacts = load(ROOT)

        unlisted = [
            chip.name
            for chip in CHIPS
            if chip.artifact is not None and chip.artifact not in artifacts
        ]

        self.assertEqual(unlisted, [])

    def test_ours_is_not_counted_as_field_proven(self) -> None:
        ours = by_name("ours")

        self.assertFalse(ours.field_proven)
        self.assertIs(ours.simulator, Simulator.AVR)

    def test_mayumi_v4_does_not_support_the_first_two_boards(self) -> None:
        boards = by_name("mayumi-v4").boards

        self.assertNotIn(Board.PU_7, boards)
        self.assertNotIn(Board.PU_8, boards)
        self.assertIn(Board.PU_18, boards)
        self.assertIn(Board.PM_41_2, boards)

    def test_ours_and_psnee_support_every_board(self) -> None:
        names = ["ours", "psnee-attiny85", "psnee-atmega328p"]

        partial = [n for n in names if by_name(n).boards != frozenset(Board)]

        self.assertEqual(partial, [])

    def test_the_recorded_chips_never_bound_ours(self) -> None:
        recorded = [
            "old-crow-12c508",
            "modavr-attiny13",
            "ubernee-atmega328p",
            "onechip-12c508a",
        ]

        proven = [n for n in recorded if by_name(n).field_proven]

        self.assertEqual(proven, [])

    def test_onechip_is_a_pal_psone_chip(self) -> None:
        onechip = by_name("onechip-12c508a")

        self.assertIs(onechip.region, Region.EUROPE)
        self.assertEqual(onechip.boards, frozenset({Board.PM_41, Board.PM_41_2}))

    def test_every_other_chip_sends_the_usa_string(self) -> None:
        regions = {c.region for c in CHIPS if c.name != "onechip-12c508a"}

        self.assertEqual(regions, {Region.AMERICA})

    def test_an_unknown_name_is_refused(self) -> None:
        with self.assertRaises(KeyError):
            by_name("no-such-chip")
