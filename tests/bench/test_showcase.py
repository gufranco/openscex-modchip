# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Tests for the showcase: properties judged per chip from its runs, the
results file, and the README tables written from it."""

import tempfile
import unittest
from dataclasses import replace
from functools import partial
from pathlib import Path

from tests.bench.test_envelope import BASE
from tools.bench.catalogue import find
from tools.bench.chips import CHIPS
from tools.bench.chips import by_name as chip
from tools.bench.manifest import ROOT
from tools.bench.metrics import Metrics
from tools.bench.run import Run
from tools.bench.showcase import (
    END,
    PROPERTIES,
    READMES,
    RESULTS,
    START,
    Outcome,
    block,
    complete,
    evaluate,
    failures,
    from_json,
    main,
    rewrite,
    table,
    to_json,
    words,
)


def run(chip_name: str, scenario: str, metrics: Metrics = BASE) -> Run:
    return Run(chip(chip_name), find(scenario), metrics, frozenset())


def outcome(results: dict[str, dict[str, Outcome]], name: str, key: str) -> Outcome:
    return results[name][key]


class EvaluateTest(unittest.TestCase):
    def test_a_chip_driving_data_in_the_probe_is_detected(self) -> None:
        # Arrange
        runs = [
            run("psnee-attiny85", "antimod-v1-carrier", replace(BASE, probe_strings=5))
        ]

        # Act
        results = evaluate(runs)

        # Assert
        self.assertEqual(
            outcome(results, "psnee-attiny85", "antimod-v1"), Outcome.FAILED
        )

    def test_a_silent_probe_passes(self) -> None:
        # Arrange
        runs = [run("ours", "antimod-v1-carrier", replace(BASE, probe_strings=0))]

        # Act
        results = evaluate(runs)

        # Assert
        self.assertEqual(outcome(results, "ours", "antimod-v1"), Outcome.HELD)

    def test_one_failing_board_fails_the_property(self) -> None:
        # Arrange
        runs = [
            run("ours", "antimod-v2-carrier", replace(BASE, reread_valid=2)),
            run("ours", "antimod-v2-gate", replace(BASE, reread_valid=0)),
        ]

        # Act
        results = evaluate(runs)

        # Assert
        self.assertEqual(outcome(results, "ours", "antimod-v2-reauth"), Outcome.FAILED)

    def test_a_chip_built_for_none_of_the_boards_is_not_applicable(self) -> None:
        # Arrange
        runs = [
            run("mayumi-v4", "antimod-v1-gate-early", replace(BASE, probe_strings=9))
        ]

        # Act
        results = evaluate(runs)

        # Assert
        self.assertEqual(outcome(results, "mayumi-v4", "antimod-v1"), Outcome.NA)

    def test_a_chip_made_for_some_of_the_boards_is_judged(self) -> None:
        # Arrange
        runs = [
            run("onechip-12c508a", "antimod-v1-carrier", replace(BASE, probe_strings=1))
        ]

        # Act
        results = evaluate(runs)

        # Assert
        self.assertEqual(
            outcome(results, "onechip-12c508a", "antimod-v1"), Outcome.FAILED
        )

    def test_only_a_valid_string_after_a_swap_rearms(self) -> None:
        # Arrange
        garbled = replace(BASE, second_window=2, second_window_valid=0)
        runs = [run("ours", "carrier-swap", garbled)]

        # Act
        results = evaluate(runs)

        # Assert
        self.assertEqual(outcome(results, "ours", "swap"), Outcome.FAILED)

    def test_each_property_fails_on_the_one_behaviour_it_judges(self) -> None:
        # Arrange
        holds = {prop.key: prop.holds for prop in PROPERTIES}
        failing = [
            ("antimod-v1", replace(BASE, probe_strings=1)),
            ("antimod-v2", replace(BASE, probe_strings=1)),
            ("antimod-v2-reauth", replace(BASE, reread_valid=0)),
            ("silent-in-play", replace(BASE, during_play=1)),
            ("pins-float", replace(BASE, data_driven_outside_ns=1)),
            ("pins-float", replace(BASE, gate_driven_outside_ns=1)),
            ("pins-float", replace(BASE, pulled_ns=1)),
            ("stuck-sqck", replace(BASE, would_accept=False)),
            ("watchdog", replace(BASE, would_accept=False)),
            ("watchdog", replace(BASE, gate_driven_ns=1)),
            ("swap", replace(BASE, second_window_valid=0)),
        ]
        passing = replace(BASE, reread_valid=1, second_window_valid=1)

        # Act
        judged = [(key, holds[key](metrics)) for key, metrics in failing]
        held = {key: check(passing) for key, check in holds.items()}

        # Assert
        self.assertEqual(
            (judged, held),
            ([(key, False) for key, _ in failing], dict.fromkeys(holds, True)),
        )

    def test_a_property_with_no_run_is_not_applicable(self) -> None:
        # Act
        results = evaluate([run("ours", "antimod-v1-carrier")])

        # Assert
        self.assertEqual(outcome(results, "ours", "stuck-sqck"), Outcome.NA)

    def test_every_property_says_its_title_and_mechanism_in_every_language(
        self,
    ) -> None:
        # Arrange
        said = words()

        # Act
        missing = [
            (p.key, field, lang)
            for p in PROPERTIES
            for field in ("title", "mechanism")
            for lang in READMES
            if not said.said(p.key, field, lang)
        ]

        # Assert
        self.assertEqual(missing, [])
        self.assertTrue(all(p.sources for p in PROPERTIES))

    def test_every_property_scenario_is_in_the_catalogue(self) -> None:
        # Arrange
        names = [name for p in PROPERTIES for name in p.scenarios]

        # Act
        found = [find(name).name for name in names]

        # Assert
        self.assertEqual(found, names)


class FailuresTest(unittest.TestCase):
    def test_only_ours_failures_are_listed(self) -> None:
        # Arrange
        first = PROPERTIES[0].key
        results = {
            "ours": {p.key: Outcome.HELD for p in PROPERTIES} | {first: Outcome.FAILED},
            "psnee-attiny85": {p.key: Outcome.FAILED for p in PROPERTIES},
        }

        # Act
        failed = failures(results)

        # Assert
        self.assertEqual(failed, [first])

    def test_a_property_ours_was_never_judged_on_is_a_failure(self) -> None:
        # Arrange
        results = {"ours": {p.key: Outcome.HELD for p in PROPERTIES[1:]}}

        # Act
        failed = failures(results)

        # Assert
        self.assertEqual(failed, [PROPERTIES[0].key])


def full_results() -> dict[str, dict[str, Outcome]]:
    """Every catalogue chip holding every property."""
    return {c.name: {p.key: Outcome.HELD for p in PROPERTIES} for c in CHIPS}


class CompleteTest(unittest.TestCase):
    def test_results_for_every_chip_and_property_are_kept(self) -> None:
        # Arrange
        results = full_results()

        # Act
        kept = complete(results)

        # Assert
        self.assertEqual(kept, results)

    def test_each_partial_result_is_refused_with_its_gap(self) -> None:
        # Arrange
        full = full_results()
        no_chip = {k: v for k, v in full.items() if k != "mayumi-v4"}
        no_property = {
            **full,
            "ours": {k: v for k, v in full["ours"].items() if k != PROPERTIES[0].key},
        }
        stranger = {**full, "nonesuch": full["ours"]}
        cases = {
            "chip": (no_chip, "chips missing ['mayumi-v4'], unknown []"),
            "property": (no_property, f"ours: no outcome for ['{PROPERTIES[0].key}']"),
            "unknown": (stranger, "chips missing [], unknown ['nonesuch']"),
        }

        # Act
        refused = {name: _refusal(given) for name, (given, _) in cases.items()}

        # Assert
        self.assertEqual(refused, {name: said for name, (_, said) in cases.items()})


def _refusal(results: dict[str, dict[str, Outcome]]) -> str:
    """The message complete raises for some results, or "" when it keeps them."""
    try:
        complete(results)
    except ValueError as error:
        return str(error)
    return ""


class ResultsFileTest(unittest.TestCase):
    def test_results_survive_a_round_trip(self) -> None:
        # Arrange
        results = evaluate([run("ours", "antimod-v1-carrier")])

        # Act
        restored = from_json(to_json(results))

        # Assert
        self.assertEqual(restored, results)


class TableTest(unittest.TestCase):
    def setUp(self) -> None:
        self.results = evaluate(
            [
                run("ours", "antimod-v1-carrier"),
                run(
                    "psnee-attiny85",
                    "antimod-v1-carrier",
                    replace(BASE, probe_strings=1),
                ),
            ]
        )

    def test_the_table_has_a_row_per_property_and_a_column_per_chip(self) -> None:
        # Act
        lines = table(self.results, "en")

        # Assert
        self.assertEqual(len(lines), 2 + len(PROPERTIES))
        self.assertIn("openscex", lines[0])
        self.assertIn("psnee-attiny85", lines[0])

    def test_each_language_labels_the_outcomes_in_its_own_words(self) -> None:
        # Act
        rows = {lang: "\n".join(table(self.results, lang)) for lang in ("ja", "zh")}

        # Assert
        self.assertNotIn("pass", rows["ja"])
        self.assertNotIn("pass", rows["zh"])

    def test_rewrite_replaces_only_the_marked_block(self) -> None:
        # Arrange
        text = f"before\n{START}\nold\n{END}\nafter\n"

        # Act
        updated = rewrite(text, ["new"])

        # Assert
        self.assertEqual(updated, f"before\n{START}\nnew\n{END}\nafter\n")

    def test_rewrite_refuses_a_text_without_the_markers(self) -> None:
        # Arrange
        readme = "no markers here\n"

        # Act
        call = partial(rewrite, readme, ["new"])

        # Assert
        self.assertRaises(ValueError, call)


class MainTest(unittest.TestCase):
    def test_a_run_rewrites_the_results_file_and_every_readme(self) -> None:
        # Arrange
        results = full_results()
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "bench").mkdir()
            for name in READMES.values():
                (root / name).write_text(f"{START}\nstale-block\n{END}\n")
            source = root / "run.json"
            source.write_text(to_json(results))

            # Act
            status = main(["showcase", str(source), "--root", str(root)])

            # Assert
            written = from_json((root / RESULTS).read_text())
            tables = [(root / n).read_text() for n in READMES.values()]
        self.assertEqual(status, 0)
        self.assertEqual(written, results)
        self.assertTrue(all("stale-block" not in text for text in tables))


class ReadmeSyncTest(unittest.TestCase):
    def test_every_readme_shows_exactly_the_recorded_results(self) -> None:
        # Arrange
        results = from_json((ROOT / RESULTS).read_text())

        # Act
        stale = [
            name
            for lang, name in READMES.items()
            if rewrite((ROOT / name).read_text(), block(results, lang))
            != (ROOT / name).read_text()
        ]

        # Assert
        self.assertEqual(stale, [])
