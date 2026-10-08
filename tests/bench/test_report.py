# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Tests for the bench report: coverage per chip and the rendered tables.

The rendered text is checked for the facts it must carry, a chip's row, a
skip reason, a failed rule, rather than byte for byte, so the layout can
change without rewriting every test.
"""

import unittest
from dataclasses import replace

from tests.bench.test_envelope import BASE
from tools.bench.chips import by_name
from tools.bench.envelope import Finding, Verdict
from tools.bench.report import coverage, render
from tools.bench.run import Run
from tools.bench.scenarios import Board
from tools.bench.scenarios import by_name as scenario


class CoverageTest(unittest.TestCase):
    def test_every_instruction_executed_is_full_coverage(self) -> None:
        result = coverage(
            "c", frozenset({0, 2, 4}), [frozenset({0, 2}), frozenset({4})]
        )

        self.assertEqual((result.executed, result.total), (3, 3))
        self.assertEqual(result.uncovered, ())
        self.assertEqual(result.percent, 100.0)

    def test_adjacent_unexecuted_instructions_merge_into_one_range(self) -> None:
        result = coverage("c", frozenset({0, 2, 6, 8, 12}), [frozenset({0, 12})])

        self.assertEqual(result.uncovered, ((2, 8),))
        self.assertEqual(result.executed, 2)

    def test_execution_outside_the_image_is_not_counted(self) -> None:
        result = coverage("c", frozenset({0, 1}), [frozenset({0, 1, 0x1FF})])

        self.assertEqual(result.executed, 2)

    def test_an_empty_image_is_fully_reached_without_dividing_by_zero(self) -> None:
        result = coverage("c", frozenset(), [])

        self.assertEqual(result.percent, 100.0)

    def test_excused_instructions_leave_the_uncovered_ranges(self) -> None:
        result = coverage(
            "c", frozenset({0, 2, 4, 6}), [frozenset({0})], {2: "why", 4: "why"}
        )

        self.assertEqual(result.uncovered, ((6, 6),))
        self.assertEqual(result.excluded, (("why", 2),))
        self.assertEqual(result.percent, 75.0)


class RenderTest(unittest.TestCase):
    def setUp(self) -> None:
        self.run = Run(
            by_name("ours"), scenario("carrier-accept"), BASE, frozenset({0})
        )

    def test_a_run_appears_as_a_row_with_its_chip_and_scenario(self) -> None:
        text = render([self.run], {}, [], [])

        self.assertIn("| ours | carrier-accept | yes |", text)

    def test_each_row_says_whether_the_chip_supports_the_boards(self) -> None:
        early_only = replace(by_name("mayumi-v4"), boards=frozenset({Board.PU_7}))
        mayumi = Run(early_only, scenario("gate-accept-early"), BASE, frozenset())
        carrier = Run(by_name("mayumi-v4"), scenario("no-disc"), BASE, frozenset())

        text = render([self.run, mayumi, carrier], {}, [], [])

        self.assertRegex(text, r"\| ours \| carrier-accept \|.*\| all \|\n")
        self.assertRegex(text, r"\| mayumi-v4 \| gate-accept-early \|.*\| partly \|\n")
        self.assertRegex(text, r"\| mayumi-v4 \| no-disc \|.*\| all \|\n")

    def test_a_chip_made_for_none_of_the_boards_says_so(self) -> None:
        stranger = replace(by_name("ours"), name="stranger", boards=frozenset())
        run = Run(stranger, scenario("gate-accept"), BASE, frozenset())

        text = render([run], {}, [], [])

        self.assertRegex(text, r"\| stranger \| gate-accept \|.*\| none \|\n")

    def test_a_skipped_chip_is_listed_with_its_reason(self) -> None:
        text = render([], {"mayumi-v4": "image missing; see quade.co"}, [], [])

        self.assertIn("mayumi-v4: image missing; see quade.co", text)

    def test_a_failed_rule_is_listed_with_its_bounds(self) -> None:
        finding = Finding("swap", "during_play", Verdict.BROKEN, 5, None, 3)

        text = render([], {}, [finding], [])

        self.assertIn("| swap | during_play | broken | 5 | - | 3 |", text)

    def test_the_verdict_counts_are_summarised(self) -> None:
        findings = [
            Finding("a", "x", Verdict.MET, 1, None, 1),
            Finding("a", "y", Verdict.UNCHECKED, 1, None, None),
        ]

        text = render([], {}, findings, [])

        self.assertIn("1 met, 0 broken, 1 unchecked", text)

    def test_coverage_lists_each_uncovered_range(self) -> None:
        result = coverage("ours", frozenset({0, 2, 4}), [frozenset({0})])

        text = render([], {}, [], [result])

        self.assertIn("| ours | 1 | 0 | 3 | 33.3 |", text)
        self.assertIn("ours: 0x2-0x4", text)

    def test_one_uncovered_instruction_never_reads_as_whole(self) -> None:
        image = frozenset(range(0, 2 * 3802, 2))
        result = coverage("ours", image, [image - {0}])

        text = render([], {}, [], [result])

        self.assertIn("| ours | 3801 | 0 | 3802 | 99.9 |", text)

    def test_coverage_lists_each_reason_with_its_count(self) -> None:
        result = coverage(
            "ours", frozenset({0, 2}), [frozenset({0})], {2: "a stuck ADC"}
        )

        text = render([], {}, [], [result])

        self.assertIn("| ours | 1 | 1 | 2 | 100.0 |", text)
        self.assertIn("- ours, 1: a stuck ADC", text)
