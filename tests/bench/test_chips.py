# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Tests for the chip catalogue: every firmware can be found or built, and
third-party ones are in the manifest."""

import unittest
from functools import partial

from tools.bench.chips import CHIPS, Simulator, by_name
from tools.bench.manifest import ROOT, load
from tools.bench.scenarios import GATE_BOARDS, Board
from tools.bench.scex import Region


class CatalogueTest(unittest.TestCase):
    def test_names_are_unique(self) -> None:
        # Act
        names = [chip.name for chip in CHIPS]

        # Assert
        self.assertEqual(len(names), len(set(names)))

    def test_every_chip_maps_data(self) -> None:
        # Act
        missing = [chip.name for chip in CHIPS if "data=" not in chip.pins]

        # Assert
        self.assertEqual(missing, [])

    def test_third_party_images_are_in_the_manifest(self) -> None:
        # Arrange
        artifacts = load(ROOT)

        # Act
        unlisted = [
            chip.name
            for chip in CHIPS
            if chip.artifact is not None and chip.artifact not in artifacts
        ]

        # Assert
        self.assertEqual(unlisted, [])

    def test_ours_is_not_counted_as_field_proven(self) -> None:
        # Act
        ours = by_name("ours")

        # Assert
        self.assertFalse(ours.field_proven)
        self.assertIs(ours.simulator, Simulator.AVR)

    def test_mayumi_v4_does_not_support_the_first_two_boards(self) -> None:
        # Act
        boards = by_name("mayumi-v4").boards

        # Assert
        self.assertNotIn(Board.PU_7, boards)
        self.assertNotIn(Board.PU_8, boards)
        self.assertIn(Board.PU_18, boards)
        self.assertIn(Board.PM_41_2, boards)

    def test_ours_and_psnee_support_every_board(self) -> None:
        # Arrange
        names = ["ours", "psnee-attiny85", "psnee-atmega328p"]

        # Act
        partial = [n for n in names if by_name(n).boards != frozenset(Board)]

        # Assert
        self.assertEqual(partial, [])

    def test_the_recorded_chips_never_bound_ours(self) -> None:
        # Arrange
        recorded = [
            "old-crow-12c508",
            "modavr-attiny13",
            "ubernee-atmega328p",
            "onechip-12c508a",
            "liss-at90s1200",
        ]

        # Act
        proven = [n for n in recorded if by_name(n).field_proven]

        # Assert
        self.assertEqual(proven, [])

    def test_onechip_is_a_pal_psone_chip(self) -> None:
        # Act
        onechip = by_name("onechip-12c508a")

        # Assert
        self.assertIs(onechip.region, Region.EUROPE)
        self.assertEqual(onechip.boards, frozenset({Board.PM_41, Board.PM_41_2}))

    def test_liss_is_a_pal_static_gate_chip(self) -> None:
        # Act
        liss = by_name("liss-at90s1200")

        # Assert
        self.assertIs(liss.region, Region.EUROPE)
        self.assertEqual(liss.boards, GATE_BOARDS)

    def test_every_other_chip_sends_the_usa_string(self) -> None:
        # Act
        pal = {"onechip-12c508a", "liss-at90s1200"}
        regions = {c.region for c in CHIPS if c.name not in pal}

        # Assert
        self.assertEqual(regions, {Region.AMERICA})

    def test_an_unknown_name_is_refused(self) -> None:
        # Arrange
        given = "no-such-chip"

        # Act
        call = partial(by_name, given)

        # Assert
        self.assertRaises(KeyError, call)
