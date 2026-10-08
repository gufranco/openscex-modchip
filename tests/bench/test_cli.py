# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Tests for the bench command line.

The end-to-end test runs ours through two short scenarios in the real runner,
so it needs the pinned toolchain; the argument tests need nothing.
"""

import contextlib
import io
import shutil
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path

from tests.bench.test_envelope import BASE
from tools.bench.catalogue import find
from tools.bench.chips import by_name as chip
from tools.bench.cli import bench, evidence, judge, main, reached, select, verdict
from tools.bench.envelope import Finding, Verdict
from tools.bench.manifest import ROOT
from tools.bench.report import Coverage
from tools.bench.run import Run
from tools.bench.scenarios import by_name as scenario


class SelectTest(unittest.TestCase):
    def test_no_selection_means_every_name(self) -> None:
        chosen = select(None, ["a", "b"])

        self.assertEqual(chosen, ["a", "b"])

    def test_a_selection_keeps_the_given_order(self) -> None:
        chosen = select("b,a", ["a", "b"])

        self.assertEqual(chosen, ["b", "a"])

    def test_an_unknown_name_is_refused_with_the_known_ones(self) -> None:
        with self.assertRaises(ValueError) as caught:
            select("c", ["a", "b"])

        self.assertIn("a, b", str(caught.exception))


class EvidenceTest(unittest.TestCase):
    def test_a_chip_missing_one_of_the_scenario_boards_is_not_evidence(self) -> None:
        gate = scenario("gate-accept-early")
        runs = [
            Run(chip("mayumi-v4"), gate, BASE, frozenset()),
            Run(chip("psnee-attiny85"), gate, replace(BASE, valid=9), frozenset()),
        ]

        proven = evidence(runs, gate)

        self.assertEqual([m.valid for m in proven], [9])

    def test_ours_and_other_scenarios_are_never_evidence(self) -> None:
        gate = scenario("gate-accept")
        runs = [
            Run(chip("ours"), gate, BASE, frozenset()),
            Run(chip("mayumi-v4"), scenario("carrier-accept"), BASE, frozenset()),
        ]

        proven = evidence(runs, gate)

        self.assertEqual(proven, [])


class JudgeTest(unittest.TestCase):
    def test_a_judged_scenario_is_held_to_the_envelope(self) -> None:
        gate = scenario("gate-accept")
        runs = [
            Run(chip("ours"), gate, BASE, frozenset()),
            Run(chip("psnee-attiny85"), gate, BASE, frozenset()),
        ]

        findings = judge(runs)

        self.assertEqual({f.scenario for f in findings}, {"gate-accept"})

    def test_a_coverage_scenario_is_never_held_to_the_envelope(self) -> None:
        stuck = find("sqck-stuck")
        runs = [
            Run(chip("ours"), stuck, replace(BASE, during_play=9), frozenset()),
            Run(chip("psnee-attiny85"), stuck, BASE, frozenset()),
        ]

        findings = judge(runs)

        self.assertEqual(findings, [])


class BenchTest(unittest.TestCase):
    def test_a_chip_without_its_image_is_skipped_with_the_reason(self) -> None:
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            shutil.copy(ROOT / "artifacts.manifest.json", root)

            result = bench(root, ["mayumi-v4"], ["no-disc"], root)

        self.assertEqual(result.runs, [])
        self.assertIn("quade.co", result.skips["mayumi-v4"])


class VerdictTest(unittest.TestCase):
    def test_a_broken_rule_is_printed_and_fails_the_run(self) -> None:
        finding = Finding("gate-accept", "during_play", Verdict.BROKEN, 5, None, 3)

        with contextlib.redirect_stdout(io.StringIO()) as out:
            code = verdict([finding])

        self.assertEqual(code, 1)
        self.assertIn("BROKEN gate-accept during_play: 5", out.getvalue())

    def test_met_and_unchecked_rules_pass_the_run(self) -> None:
        findings = [
            Finding("a", "x", Verdict.MET, 1, None, 1),
            Finding("a", "y", Verdict.UNCHECKED, None, None, None),
        ]

        code = verdict(findings)

        self.assertEqual(code, 0)


class ReachedTest(unittest.TestCase):
    def test_ours_with_an_uncovered_range_fails_the_run(self) -> None:
        left = Coverage("ours", 3, 4, ((0x10, 0x12),))

        with contextlib.redirect_stdout(io.StringIO()) as out:
            code = reached([left])

        self.assertEqual(code, 1)
        self.assertIn("UNCOVERED ours 0x10-0x12", out.getvalue())

    def test_another_chip_left_uncovered_is_only_reported(self) -> None:
        left = Coverage("mayumi-v4", 3, 4, ((0x10, 0x10),))

        code = reached([left])

        self.assertEqual(code, 0)


class MainTest(unittest.TestCase):
    def test_an_unknown_chip_exits_with_a_usage_error(self) -> None:
        with contextlib.redirect_stderr(io.StringIO()) as err:
            code = main(["bench", "--chips", "nonesuch"])

        self.assertEqual(code, 2)
        self.assertIn("nonesuch", err.getvalue())

    def test_ours_alone_writes_a_report_and_passes(self) -> None:
        with tempfile.TemporaryDirectory() as work:
            out = Path(work) / "report.md"
            with contextlib.redirect_stdout(io.StringIO()):
                code = main(
                    [
                        "bench",
                        "--chips",
                        "ours",
                        "--scenarios",
                        "carrier-accept,no-disc",
                        "--out",
                        str(out),
                        "--workdir",
                        str(Path(work) / "not-yet-made"),
                    ]
                )
            text = out.read_text()

        self.assertEqual(code, 0)
        self.assertIn("| ours | carrier-accept | yes |", text)
        self.assertIn("| ours | no-disc | no |", text)
