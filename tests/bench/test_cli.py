# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Tests for the bench command line; the end-to-end test needs the pinned
toolchain."""

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
from tools.bench.cli import (
    Bench,
    applies,
    bench,
    evidence,
    exit_status,
    judge,
    main,
    reached,
    select,
    showcased,
    verdict,
)
from tools.bench.envelope import Finding, Verdict
from tools.bench.manifest import ROOT
from tools.bench.report import Coverage
from tools.bench.run import Run
from tools.bench.scenarios import by_name as scenario
from tools.bench.showcase import Outcome


class SelectTest(unittest.TestCase):
    def test_no_selection_means_every_name(self) -> None:
        # Act
        chosen = select(None, ["a", "b"])

        # Assert
        self.assertEqual(chosen, ["a", "b"])

    def test_a_selection_keeps_the_given_order(self) -> None:
        # Act
        chosen = select("b,a", ["a", "b"])

        # Assert
        self.assertEqual(chosen, ["b", "a"])

    def test_an_unknown_name_is_refused_with_the_known_ones(self) -> None:
        # Act
        with self.assertRaises(ValueError) as caught:
            select("c", ["a", "b"])

        # Assert
        self.assertIn("a, b", str(caught.exception))


class EvidenceTest(unittest.TestCase):
    def test_a_chip_missing_one_of_the_scenario_boards_is_not_evidence(self) -> None:
        # Arrange
        gate = scenario("gate-accept-early")
        runs = [
            Run(chip("mayumi-v4"), gate, BASE, frozenset()),
            Run(chip("psnee-attiny85"), gate, replace(BASE, valid=9), frozenset()),
        ]

        # Act
        proven = evidence(runs, gate)

        # Assert
        self.assertEqual([m.valid for m in proven], [9])

    def test_ours_and_other_scenarios_are_never_evidence(self) -> None:
        # Arrange
        gate = scenario("gate-accept")
        runs = [
            Run(chip("ours"), gate, BASE, frozenset()),
            Run(chip("mayumi-v4"), scenario("carrier-accept"), BASE, frozenset()),
        ]

        # Act
        proven = evidence(runs, gate)

        # Assert
        self.assertEqual(proven, [])


class AppliesTest(unittest.TestCase):
    def test_a_chip_without_the_driven_line_skips_the_scenario(self) -> None:
        # Act
        skipped = applies(chip("psnee-attiny85"), find("gate-sense-paths"))

        # Assert
        self.assertFalse(skipped)

    def test_a_chip_that_reads_the_line_runs_it(self) -> None:
        # Act
        ran = applies(chip("mayumi-v4"), find("gate-sense-paths"))

        # Assert
        self.assertTrue(ran)

    def test_ours_runs_every_scenario(self) -> None:
        # Act
        ran = applies(chip("ours"), find("gate-sense-paths"))

        # Assert
        self.assertTrue(ran)

    def test_a_scenario_driving_no_special_line_runs_everywhere(self) -> None:
        # Act
        ran = applies(chip("psnee-attiny85"), find("carrier-accept"))

        # Assert
        self.assertTrue(ran)


class JudgeTest(unittest.TestCase):
    def test_a_judged_scenario_is_held_to_the_envelope(self) -> None:
        # Arrange
        gate = scenario("gate-accept")
        runs = [
            Run(chip("ours"), gate, BASE, frozenset()),
            Run(chip("psnee-attiny85"), gate, BASE, frozenset()),
        ]

        # Act
        findings = judge(runs)

        # Assert
        self.assertEqual({f.scenario for f in findings}, {"gate-accept"})

    def test_a_coverage_scenario_is_never_held_to_the_envelope(self) -> None:
        # Arrange
        stuck = find("sqck-stuck")
        runs = [
            Run(chip("ours"), stuck, replace(BASE, during_play=9), frozenset()),
            Run(chip("psnee-attiny85"), stuck, BASE, frozenset()),
        ]

        # Act
        findings = judge(runs)

        # Assert
        self.assertEqual(findings, [])


class BenchTest(unittest.TestCase):
    def test_a_chip_without_its_image_is_skipped_with_the_reason(self) -> None:
        # Arrange
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            shutil.copy(ROOT / "artifacts.manifest.json", root)

            # Act
            result = bench(root, ["mayumi-v4"], ["no-disc"], root)

        # Assert
        self.assertEqual(result.runs, ())
        self.assertIn("quade.co", result.skips["mayumi-v4"])


class VerdictTest(unittest.TestCase):
    def test_a_broken_rule_is_printed_and_fails_the_run(self) -> None:
        # Arrange
        finding = Finding("gate-accept", "during_play", Verdict.BROKEN, 5, None, 3)

        # Act
        with contextlib.redirect_stdout(io.StringIO()) as out:
            code = verdict([finding])

        # Assert
        self.assertEqual(code, 1)
        self.assertIn("BROKEN gate-accept during_play: 5", out.getvalue())

    def test_met_and_unchecked_rules_pass_the_run(self) -> None:
        # Arrange
        findings = [
            Finding("a", "x", Verdict.MET, 1, None, 1),
            Finding("a", "y", Verdict.UNCHECKED, None, None, None),
        ]

        # Act
        code = verdict(findings)

        # Assert
        self.assertEqual(code, 0)


class ReachedTest(unittest.TestCase):
    def test_ours_with_an_uncovered_range_fails_the_run(self) -> None:
        # Arrange
        left = Coverage("ours", 3, 4, ((0x10, 0x12),))

        # Act
        with contextlib.redirect_stdout(io.StringIO()) as out:
            code = reached([left])

        # Assert
        self.assertEqual(code, 1)
        self.assertIn("UNCOVERED ours 0x10-0x12", out.getvalue())

    def test_another_chip_left_uncovered_is_only_reported(self) -> None:
        # Arrange
        left = Coverage("mayumi-v4", 3, 4, ((0x10, 0x10),))

        # Act
        code = reached([left])

        # Assert
        self.assertEqual(code, 0)


class ShowcasedTest(unittest.TestCase):
    def test_ours_failing_a_property_fails_the_run(self) -> None:
        # Arrange
        results = {"ours": {"antimod-v1": Outcome.FAILED}}

        # Act
        with contextlib.redirect_stdout(io.StringIO()) as out:
            status = showcased(results)

        # Assert
        self.assertEqual(status, 1)
        self.assertIn("SHOWCASE ours fails antimod-v1", out.getvalue())

    def test_another_chip_failing_is_only_reported(self) -> None:
        # Arrange
        results = {
            "ours": {"antimod-v1": Outcome.HELD},
            "psnee-attiny85": {"antimod-v1": Outcome.FAILED},
        }

        # Act
        status = showcased(results)

        # Assert
        self.assertEqual(status, 0)


class ExitStatusTest(unittest.TestCase):
    def test_a_full_run_fails_on_a_showcase_property(self) -> None:
        # Arrange
        result = Bench((), {}, (), ())
        failing = {"ours": {"antimod-v1": Outcome.FAILED}}

        # Act
        with contextlib.redirect_stdout(io.StringIO()):
            status = exit_status(result, failing, whole=True)

        # Assert
        self.assertEqual(status, 1)

    def test_ours_asked_for_but_skipped_fails_the_run(self) -> None:
        # Arrange
        result = Bench((), {"ours": "ours: build/x.elf is missing"}, (), ())

        # Act
        with contextlib.redirect_stdout(io.StringIO()) as out:
            status = exit_status(result, {}, whole=False)

        # Assert
        self.assertEqual(status, 1)
        self.assertIn("SKIPPED ours", out.getvalue())

    def test_another_chip_skipped_does_not_fail_the_run(self) -> None:
        # Arrange
        result = Bench((), {"mayumi-v4": "image missing"}, (), ())

        # Act
        status = exit_status(result, {}, whole=False)

        # Assert
        self.assertEqual(status, 0)

    def test_a_partial_run_ignores_the_showcase(self) -> None:
        # Arrange
        result = Bench((), {}, (), ())
        failing = {"ours": {"antimod-v1": Outcome.FAILED}}

        # Act
        status = exit_status(result, failing, whole=False)

        # Assert
        self.assertEqual(status, 0)


class MainTest(unittest.TestCase):
    def test_an_unknown_chip_exits_with_a_usage_error(self) -> None:
        # Act
        with contextlib.redirect_stderr(io.StringIO()) as err:
            code = main(["bench", "--chips", "nonesuch"])

        # Assert
        self.assertEqual(code, 2)
        self.assertIn("nonesuch", err.getvalue())

    def test_ours_alone_writes_a_report_and_passes(self) -> None:
        # Act
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

        # Assert
        self.assertEqual(code, 0)
        self.assertIn("| ours | carrier-accept | yes |", text)
        self.assertIn("| ours | no-disc | no |", text)
