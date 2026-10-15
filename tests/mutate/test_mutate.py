# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Tests for the mutation tool, run for real on a tiny C project in a temp
directory."""

import contextlib
import io
import shutil
import tempfile
import time
import unittest
from pathlib import Path

from tools import mutate

# One mutable operator, so exactly one mutant, "gt->ge".
LOGIC = "int above(int a) { return a > 3; }\n"
# Checks the boundary, so the a >= 3 mutant makes it fail: the mutant is killed.
STRONG_TEST = (
    "int above(int a);\n"
    "int main(void) { return !((above(3) == 0) && (above(4) == 1)); }\n"
)
# Checks only far from the boundary, so the a >= 3 mutant still passes it.
WEAK_TEST = "int above(int a);\nint main(void) { return !(above(10) == 1); }\n"
# Fails on the unmutated code.
FAILING_TEST = "int above(int a);\nint main(void) { return above(10); }\n"
SUITE = mutate.Suite(("logic.c",), ("test.c",), mutate.CC, ("-std=c17",))


class GenerateMutantsTest(unittest.TestCase):
    def test_skips_preprocessor_and_comment_lines(self) -> None:
        # Arrange
        source = "#include <stdint.h>\n// a == b\nint x;"

        # Act
        mutants = mutate.generate_mutants(source)

        # Assert
        self.assertEqual(mutants, [])

    def test_skips_assert_guard_lines(self) -> None:
        # Arrange
        source = "  PSCU_ASSERT(index < COUNT);"

        # Act
        mutants = mutate.generate_mutants(source)

        # Assert
        self.assertEqual(mutants, [])

    def test_skips_compile_time_assertions(self) -> None:
        # Arrange
        source = '_Static_assert((A + B) <= 0x100U, "bound");'

        # Act
        mutants = mutate.generate_mutants(source)

        # Assert
        self.assertEqual(mutants, [])

    def test_equality_operator_is_mutated_both_ways(self) -> None:
        # Arrange
        source = "if (a == b) {}\nif (c != d) {}"

        # Act
        labels = [mutant.label for mutant in mutate.generate_mutants(source)]

        # Assert
        self.assertIn("L1 eq->ne col7", labels)
        self.assertIn("L2 ne->eq col7", labels)

    def test_mutant_source_replaces_only_one_occurrence(self) -> None:
        # Arrange
        source = "x == y == z;"

        # Act
        sources = [mutant.source for mutant in mutate.generate_mutants(source)]

        # Assert
        self.assertIn("x != y == z;", sources)
        self.assertIn("x == y != z;", sources)

    def test_relational_mutations_ignore_shifts_and_arrows(self) -> None:
        # Arrange
        source = "a >> 1; b << 2; p->q; r >= s; t <= u;"

        # Act
        labels = [mutant.label for mutant in mutate.generate_mutants(source)]

        # Assert
        self.assertEqual(
            sorted(labels),
            sorted(
                [
                    "L1 shr->shl col3",
                    "L1 shl->shr col11",
                    "L1 ge->gt col25",
                    "L1 le->lt col33",
                ]
            ),
        )

    def test_single_relational_operators_are_widened(self) -> None:
        # Arrange
        source = "if (a < b && c > d) {}"

        # Act
        labels = [mutant.label for mutant in mutate.generate_mutants(source)]

        # Assert
        self.assertIn("L1 lt->le col7", labels)
        self.assertIn("L1 gt->ge col16", labels)
        self.assertIn("L1 and->or col11", labels)

    def test_logical_or_is_mutated(self) -> None:
        # Arrange
        source = "if (a || b) {}"

        # Act
        sources = [mutant.source for mutant in mutate.generate_mutants(source)]

        # Assert
        self.assertEqual(sources, ["if (a && b) {}"])

    def test_binary_arithmetic_and_bitwise_operators_are_swapped(self) -> None:
        # Arrange
        source = "x = a + b - c & d | e ^ f;"

        # Act
        sources = {mutant.source for mutant in mutate.generate_mutants(source)}

        # Assert
        self.assertTrue(
            {
                "x = a - b - c & d | e ^ f;",
                "x = a + b + c & d | e ^ f;",
                "x = a + b - c | d | e ^ f;",
                "x = a + b - c & d & e ^ f;",
                "x = a + b - c & d | e | f;",
            }
            <= sources
        )

    def test_unary_minus_and_arrows_are_left_alone(self) -> None:
        # Arrange
        source = "x = -a; y = p->q;"

        # Act
        mutants = mutate.generate_mutants(source)

        # Assert
        self.assertEqual(mutants, [])

    def test_an_unsigned_literal_is_bumped_in_its_own_base(self) -> None:
        # Arrange
        source = "x = 0x0FU + 9U;"

        # Act
        sources = {mutant.source for mutant in mutate.generate_mutants(source)}

        # Assert
        self.assertTrue({"x = 0x10U + 9U;", "x = 0x0FU + 10U;"} <= sources)

    def test_a_long_unsigned_literal_keeps_its_suffix(self) -> None:
        # Arrange
        source = "x = 0xFFFFFFFFUL + 7ULL;"

        # Act
        sources = {mutant.source for mutant in mutate.generate_mutants(source)}

        # Assert
        self.assertTrue(
            {"x = 0x100000000UL + 7ULL;", "x = 0xFFFFFFFFUL + 8ULL;"} <= sources
        )

    def test_a_defined_value_is_mutated_but_other_directives_are_not(self) -> None:
        # Arrange
        source = "#define LIMIT ((uint8_t)6U)\n#if LIMIT == 6U\n#endif"

        # Act
        sources = [mutant.source for mutant in mutate.generate_mutants(source)]

        # Assert
        self.assertEqual(
            sources, ["#define LIMIT ((uint8_t)7U)\n#if LIMIT == 6U\n#endif"]
        )

    def test_booleans_are_flipped(self) -> None:
        # Arrange
        source = "a = true; b = false;"

        # Act
        sources = {mutant.source for mutant in mutate.generate_mutants(source)}

        # Assert
        self.assertEqual(sources, {"a = false; b = false;", "a = true; b = true;"})

    def test_a_listed_equivalent_mutant_is_skipped_only_in_its_file(self) -> None:
        # Arrange
        source = "  out.missed = false;"

        # Act
        listed = mutate.generate_mutants(source, "src/loop.c")
        elsewhere = mutate.generate_mutants(source, "src/calib.c")

        # Assert
        self.assertEqual((len(listed), len(elsewhere)), (0, 1))

    def test_every_equivalent_fragment_holds_on_its_stated_lines_only(self) -> None:
        # Arrange
        equivalents = mutate.EQUIVALENTS

        # Act
        wrong = [
            (e.path, e.fragment, e.lines, found)
            for e in equivalents
            for found in [
                sum(
                    e.fragment in line
                    for line in (mutate.ROOT / e.path).read_text().split("\n")
                )
            ]
            if found != e.lines
        ]

        # Assert
        self.assertEqual(wrong, [])

    def test_the_replay_test_on_the_board_stage_exit_is_mutated(self) -> None:
        # Arrange
        source = (mutate.ROOT / "src/indicator.c").read_text()

        # Act
        mutated = [
            m
            for m in mutate.generate_mutants(source, "src/indicator.c")
            if "(state->replay != 1U)" in m.source
        ]

        # Assert
        self.assertEqual(len(mutated), 1)

    def test_every_equivalent_names_a_reason(self) -> None:
        # Arrange
        equivalents = mutate.EQUIVALENTS

        # Act
        missing = [e for e in equivalents if not e.reason]

        # Assert
        self.assertEqual(missing, [])


def project(test: str, logic: str = LOGIC) -> Path:
    """A temp directory holding logic.c and test.c, removed by the caller."""
    root = Path(tempfile.mkdtemp())
    (root / "logic.c").write_text(logic)
    (root / "test.c").write_text(test)
    return root


class ProjectTestCase(unittest.TestCase):
    def make(self, test: str, logic: str = LOGIC) -> Path:
        root = project(test, logic)
        self.addCleanup(shutil.rmtree, root)
        return root


class CompileAndRunTest(ProjectTestCase):
    def test_passing_suite_compiles_and_runs(self) -> None:
        # Arrange
        root = self.make(STRONG_TEST)

        # Act
        passed = mutate._compile_and_run(root, root / "bin", SUITE)

        # Assert
        self.assertTrue(passed)

    def test_failing_suite_is_reported(self) -> None:
        # Arrange
        root = self.make(FAILING_TEST)

        # Act
        passed = mutate._compile_and_run(root, root / "bin", SUITE)

        # Assert
        self.assertFalse(passed)

    def test_a_suite_that_never_ends_is_stopped_and_reported(self) -> None:
        # Arrange
        root = self.make("int main(void) { for (;;) { } }\n")
        hanging = mutate.Suite(("logic.c",), ("test.c",), mutate.CC, ("-std=c17",), 1)

        # Act
        passed = mutate._compile_and_run(root, root / "bin", hanging)

        # Assert
        self.assertFalse(passed)

    def test_code_that_does_not_compile_is_reported(self) -> None:
        # Arrange
        root = self.make(STRONG_TEST, "int above(int a) { return a > ; }\n")

        # Act
        passed = mutate._compile_and_run(root, root / "bin", SUITE)

        # Assert
        self.assertFalse(passed)


class MutantKilledTest(ProjectTestCase):
    def test_killed_mutant_is_reported_and_file_restored(self) -> None:
        # Arrange
        root = self.make(STRONG_TEST)
        mutant = mutate.generate_mutants(LOGIC)[0]

        # Act
        killed = mutate._mutant_killed(
            root, root / "logic.c", mutant, root / "bin", SUITE
        )

        # Assert
        self.assertEqual((killed, (root / "logic.c").read_text()), (True, LOGIC))

    def test_surviving_mutant_is_reported_and_file_restored(self) -> None:
        # Arrange
        root = self.make(WEAK_TEST)
        mutant = mutate.generate_mutants(LOGIC)[0]

        # Act
        killed = mutate._mutant_killed(
            root, root / "logic.c", mutant, root / "bin", SUITE
        )

        # Assert
        self.assertEqual((killed, (root / "logic.c").read_text()), (False, LOGIC))

    def test_file_is_restored_when_the_build_raises(self) -> None:
        # Arrange
        root = self.make(STRONG_TEST)
        mutant = mutate.generate_mutants(LOGIC)[0]
        broken = mutate.Suite(("logic.c",), ("test.c",), "/nonexistent/cc", ())

        # Act
        with self.assertRaises(FileNotFoundError) as raised:
            mutate._mutant_killed(root, root / "logic.c", mutant, root / "bin", broken)

        # Assert
        self.assertEqual(
            (raised.exception.filename, (root / "logic.c").read_text()),
            ("/nonexistent/cc", LOGIC),
        )


class RunTest(ProjectTestCase):
    def test_raises_when_baseline_fails(self) -> None:
        # Arrange
        root = self.make(FAILING_TEST)

        # Act
        with self.assertRaises(RuntimeError) as raised:
            mutate.run(root, SUITE)

        # Assert
        self.assertEqual(
            str(raised.exception), "baseline host suite does not pass; cannot mutate"
        )

    def test_strong_suite_kills_every_mutant(self) -> None:
        # Arrange
        root = self.make(STRONG_TEST)

        # Act
        result = mutate.run(root, SUITE)

        # Assert
        self.assertEqual(result, mutate.Result(total=1, killed=1, survivors=()))

    def test_weak_suite_reports_the_survivor(self) -> None:
        # Arrange
        root = self.make(WEAK_TEST)

        # Act
        result = mutate.run(root, SUITE)

        # Assert
        self.assertEqual(
            result,
            mutate.Result(total=1, killed=0, survivors=("logic.c: L1 gt->ge col29",)),
        )


class MainTest(ProjectTestCase):
    def test_returns_zero_when_all_killed(self) -> None:
        # Arrange
        root = self.make(STRONG_TEST)
        output = io.StringIO()

        # Act
        with contextlib.redirect_stdout(output):
            code = mutate.main(["mutate.py"], root, SUITE)

        # Assert
        self.assertEqual((code, output.getvalue()), (0, "mutation score 1/1 killed\n"))

    def test_returns_one_and_prints_survivors(self) -> None:
        # Arrange
        root = self.make(WEAK_TEST)
        output = io.StringIO()

        # Act
        with contextlib.redirect_stdout(output):
            code = mutate.main(["mutate.py"], root, SUITE)

        # Assert
        self.assertEqual(
            (code, output.getvalue()),
            (1, "SURVIVED logic.c: L1 gt->ge col29\nmutation score 0/1 killed\n"),
        )

    def test_returns_one_when_no_mutants(self) -> None:
        # Arrange
        root = self.make(
            "int above(int a);\nint main(void) { return above(0); }\n",
            "int above(int a) { return a; }\n",
        )
        output = io.StringIO()

        # Act
        with contextlib.redirect_stdout(output):
            code = mutate.main(["mutate.py"], root, SUITE)

        # Assert
        self.assertEqual((code, output.getvalue()), (1, "mutation score 0/0 killed\n"))


def command_suite(command: str, timeout_s: int = 60) -> mutate.CommandSuite:
    """A suite over logic.c whose verdict is a shell command run in the project."""
    return mutate.CommandSuite(("logic.c",), ("sh", "-c", command), timeout_s)


# Fails once logic.c no longer holds the original comparison.
STRONG_COMMAND = "grep -q 'a > 3' logic.c"


class CommandSuiteTest(ProjectTestCase):
    def test_a_command_that_sees_the_mutant_kills_it(self) -> None:
        # Arrange
        root = self.make(STRONG_TEST)

        # Act
        result = mutate.run_command_suite(root, command_suite(STRONG_COMMAND))

        # Assert
        self.assertEqual(result, mutate.Result(total=1, killed=1, survivors=()))
        self.assertEqual((root / "logic.c").read_text(), LOGIC)

    def test_a_command_blind_to_the_mutant_reports_it(self) -> None:
        # Arrange
        root = self.make(STRONG_TEST)

        # Act
        result = mutate.run_command_suite(root, command_suite("true"))

        # Assert
        self.assertEqual(result.survivors, ("logic.c: L1 gt->ge col29",))

    def test_a_failing_baseline_command_stops_the_run(self) -> None:
        # Arrange
        root = self.make(STRONG_TEST)

        # Act
        def act() -> None:
            mutate.run_command_suite(root, command_suite("false"))

        # Assert
        self.assertRaises(RuntimeError, act)

    def test_a_command_past_its_limit_counts_as_failing(self) -> None:
        # Arrange
        root = self.make(STRONG_TEST)

        # Act
        passed = mutate._command_passes(root, command_suite("sleep 5", 1))

        # Assert
        self.assertFalse(passed)

    def test_mutants_go_into_a_copy_and_never_into_the_tree(self) -> None:
        # Arrange
        root = self.make(STRONG_TEST)
        suite = command_suite(f"cmp -s logic.c '{root / 'logic.c'}'")

        # Act
        result = mutate.run_command_suite(root, suite)

        # Assert
        self.assertEqual(result, mutate.Result(total=1, killed=1, survivors=()))

    def test_a_timeout_also_stops_what_the_command_started(self) -> None:
        # Arrange
        root = self.make(STRONG_TEST)
        marker = root / "late"
        suite = command_suite(f"(sleep 3; touch '{marker}') & sleep 30", 1)

        # Act
        passed = mutate._command_passes(root, suite)
        time.sleep(4)

        # Assert
        self.assertEqual((passed, marker.exists()), (False, False))

    def test_main_runs_the_command_suite_when_asked(self) -> None:
        # Arrange
        root = self.make(STRONG_TEST)
        output = io.StringIO()

        # Act
        with contextlib.redirect_stdout(output):
            code = mutate.main(
                ["mutate.py", "--platform"], root, SUITE, command_suite(STRONG_COMMAND)
            )

        # Assert
        self.assertEqual((code, output.getvalue()), (0, "mutation score 1/1 killed\n"))


if __name__ == "__main__":
    unittest.main()
