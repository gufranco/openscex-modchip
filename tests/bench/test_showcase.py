# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Tests for the showcase: properties judged per chip from its runs, the
results file, and the README tables written from it."""

import tempfile
import unittest
from dataclasses import replace
from pathlib import Path

from tests.bench.test_envelope import BASE
from tools.bench.catalogue import find
from tools.bench.chips import by_name as chip
from tools.bench.run import Run
from tools.bench.showcase import (
    END,
    PROPERTIES,
    READMES,
    RESULTS,
    START,
    Outcome,
    evaluate,
    failures,
    from_json,
    main,
    rewrite,
    table,
    to_json,
    words,
)


def run(chip_name: str, scenario: str, **metrics: object) -> Run:
    return Run(chip(chip_name), find(scenario), replace(BASE, **metrics), frozenset())


def outcome(results: dict[str, dict[str, Outcome]], name: str, key: str) -> Outcome:
    return results[name][key]


class EvaluateTest(unittest.TestCase):
    def test_a_chip_driving_data_in_the_probe_is_detected(self) -> None:
        runs = [run("psnee-attiny85", "antimod-v1-carrier", probe_strings=5)]

        results = evaluate(runs)

        self.assertEqual(
            outcome(results, "psnee-attiny85", "antimod-v1"), Outcome.FAILED
        )

    def test_a_silent_probe_passes(self) -> None:
        runs = [run("ours", "antimod-v1-carrier", probe_strings=0)]

        results = evaluate(runs)

        self.assertEqual(outcome(results, "ours", "antimod-v1"), Outcome.HELD)

    def test_one_failing_board_fails_the_property(self) -> None:
        runs = [
            run("ours", "antimod-v2-carrier", reread_valid=2),
            run("ours", "antimod-v2-gate", reread_valid=0),
        ]

        results = evaluate(runs)

        self.assertEqual(outcome(results, "ours", "antimod-v2-reauth"), Outcome.FAILED)

    def test_a_chip_built_for_none_of_the_boards_is_not_applicable(self) -> None:
        runs = [run("mayumi-v4", "antimod-v1-gate-early", probe_strings=9)]

        results = evaluate(runs)

        self.assertEqual(outcome(results, "mayumi-v4", "antimod-v1"), Outcome.NA)

    def test_a_chip_made_for_some_of_the_boards_is_judged(self) -> None:
        runs = [run("onechip-12c508a", "antimod-v1-carrier", probe_strings=1)]

        results = evaluate(runs)

        self.assertEqual(
            outcome(results, "onechip-12c508a", "antimod-v1"), Outcome.FAILED
        )

    def test_a_property_with_no_run_is_not_applicable(self) -> None:
        results = evaluate([run("ours", "antimod-v1-carrier")])

        self.assertEqual(outcome(results, "ours", "stuck-sqck"), Outcome.NA)

    def test_every_property_says_its_title_and_mechanism_in_every_language(
        self,
    ) -> None:
        said = words()

        missing = [
            (p.key, field, lang)
            for p in PROPERTIES
            for field in ("title", "mechanism")
            for lang in READMES
            if not said.said(p.key, field, lang)
        ]

        self.assertEqual(missing, [])
        self.assertTrue(all(p.source for p in PROPERTIES))

    def test_every_property_scenario_is_in_the_catalogue(self) -> None:
        names = [name for p in PROPERTIES for name in p.scenarios]

        found = [find(name).name for name in names]

        self.assertEqual(found, names)


class FailuresTest(unittest.TestCase):
    def test_only_ours_failures_are_listed(self) -> None:
        runs = [
            run("ours", "antimod-v1-carrier", probe_strings=1),
            run("psnee-attiny85", "antimod-v1-carrier", probe_strings=1),
        ]

        failed = failures(evaluate(runs))

        self.assertEqual(failed, ["antimod-v1"])


class ResultsFileTest(unittest.TestCase):
    def test_results_survive_a_round_trip(self) -> None:
        results = evaluate([run("ours", "antimod-v1-carrier")])

        restored = from_json(to_json(results))

        self.assertEqual(restored, results)


class TableTest(unittest.TestCase):
    def setUp(self) -> None:
        self.results = evaluate(
            [
                run("ours", "antimod-v1-carrier"),
                run("psnee-attiny85", "antimod-v1-carrier", probe_strings=1),
            ]
        )

    def test_the_table_has_a_row_per_property_and_a_column_per_chip(self) -> None:
        lines = table(self.results, "en")

        self.assertEqual(len(lines), 2 + len(PROPERTIES))
        self.assertIn("openscex", lines[0])
        self.assertIn("psnee-attiny85", lines[0])

    def test_each_language_labels_the_outcomes_in_its_own_words(self) -> None:
        rows = {lang: "\n".join(table(self.results, lang)) for lang in ("ja", "zh")}

        self.assertNotIn("pass", rows["ja"])
        self.assertNotIn("pass", rows["zh"])

    def test_rewrite_replaces_only_the_marked_block(self) -> None:
        text = f"before\n{START}\nold\n{END}\nafter\n"

        updated = rewrite(text, ["new"])

        self.assertEqual(updated, f"before\n{START}\nnew\n{END}\nafter\n")

    def test_rewrite_refuses_a_text_without_the_markers(self) -> None:
        with self.assertRaises(ValueError):
            rewrite("no markers here\n", ["new"])


class MainTest(unittest.TestCase):
    def test_a_run_rewrites_the_results_file_and_every_readme(self) -> None:
        results = evaluate([run("ours", "antimod-v1-carrier")])
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "bench").mkdir()
            for name in READMES.values():
                (root / name).write_text(f"{START}\nold\n{END}\n")
            source = root / "run.json"
            source.write_text(to_json(results))

            status = main(["showcase", str(source), "--root", str(root)])

            written = from_json((root / RESULTS).read_text())
            tables = [(root / n).read_text() for n in READMES.values()]
        self.assertEqual(status, 0)
        self.assertEqual(written, results)
        self.assertTrue(all("old" not in text for text in tables))

