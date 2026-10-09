# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Tests for the catalogue that joins every scenario set."""

import unittest

from tools.bench.antimod_scenarios import ANTIMOD_SCENARIOS
from tools.bench.catalogue import CATALOGUE, find
from tools.bench.edge_scenarios import EDGE_SCENARIOS
from tools.bench.scenarios import SCENARIOS
from tools.bench.sense_scenarios import SENSE_SCENARIOS


class CatalogueTest(unittest.TestCase):
    def test_the_catalogue_holds_every_set_with_unique_names(self) -> None:
        names = [s.name for s in CATALOGUE]

        expected = (
            len(SCENARIOS)
            + len(EDGE_SCENARIOS)
            + len(SENSE_SCENARIOS)
            + len(ANTIMOD_SCENARIOS)
        )
        self.assertEqual(len(CATALOGUE), expected)
        self.assertEqual(len(names), len(set(names)))

    def test_find_reaches_a_scenario_of_each_set(self) -> None:
        names = [
            "carrier-accept",
            "carrier-power-cycle",
            "gate-sense-paths",
            "antimod-v2-carrier",
        ]

        found = [find(name).name for name in names]

        self.assertEqual(found, names)

    def test_an_unknown_name_is_refused(self) -> None:
        with self.assertRaises(KeyError):
            find("no-such-scenario")
