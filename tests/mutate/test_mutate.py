# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Tests for the mutation tool, run for real on a tiny C project in a temp
directory."""

import contextlib
import io
import shutil
import tempfile
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
        source = "#include <stdint.h>\n// a == b\nint x;"

        mutants = mutate.generate_mutants(source)

        self.assertEqual(mutants, [])

    def test_skips_assert_guard_lines(self) -> None:
        source = "  PSCU_ASSERT(index < COUNT);"

        mutants = mutate.generate_mutants(source)

        self.assertEqual(mutants, [])

    def test_skips_compile_time_assertions(self) -> None:
        source = '_Static_assert((A + B) <= 0x100U, "bound");'

        mutants = mutate.generate_mutants(source)

        self.assertEqual(mutants, [])

    def test_equality_operator_is_mutated_both_ways(self) -> None:
        source = "if (a == b) {}\nif (c != d) {}"

        labels = [mutant.label for mutant in mutate.generate_mutants(source)]

        self.assertIn("L1 eq->ne col7", labels)
        self.assertIn("L2 ne->eq col7", labels)

    def test_mutant_source_replaces_only_one_occurrence(self) -> None:
        source = "x == y == z;"

        sources = [mutant.source for mutant in mutate.generate_mutants(source)]

        self.assertIn("x != y == z;", sources)
        self.assertIn("x == y != z;", sources)

    def test_relational_mutations_ignore_shifts_and_arrows(self) -> None:
        source = "a >> 1; b << 2; p->q; r >= s; t <= u;"

        labels = [mutant.label for mutant in mutate.generate_mutants(source)]

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
        source = "if (a < b && c > d) {}"

        labels = [mutant.label for mutant in mutate.generate_mutants(source)]

        self.assertIn("L1 lt->le col7", labels)
        self.assertIn("L1 gt->ge col16", labels)
        self.assertIn("L1 and->or col11", labels)

    def test_logical_or_is_mutated(self) -> None:
        source = "if (a || b) {}"

        sources = [mutant.source for mutant in mutate.generate_mutants(source)]

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
        root = self.make(STRONG_TEST)

        passed = mutate._compile_and_run(root, root / "bin", SUITE)

        self.assertTrue(passed)

    def test_failing_suite_is_reported(self) -> None:
        root = self.make(FAILING_TEST)

        passed = mutate._compile_and_run(root, root / "bin", SUITE)

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
        root = self.make(STRONG_TEST, "int above(int a) { return a > ; }\n")

        passed = mutate._compile_and_run(root, root / "bin", SUITE)

        self.assertFalse(passed)


class MutantKilledTest(ProjectTestCase):
    def test_killed_mutant_is_reported_and_file_restored(self) -> None:
        root = self.make(STRONG_TEST)
        mutant = mutate.generate_mutants(LOGIC)[0]

        killed = mutate._mutant_killed(
            root, root / "logic.c", mutant, root / "bin", SUITE
        )

        self.assertEqual((killed, (root / "logic.c").read_text()), (True, LOGIC))

    def test_surviving_mutant_is_reported_and_file_restored(self) -> None:
        root = self.make(WEAK_TEST)
        mutant = mutate.generate_mutants(LOGIC)[0]

        killed = mutate._mutant_killed(
            root, root / "logic.c", mutant, root / "bin", SUITE
        )

        self.assertEqual((killed, (root / "logic.c").read_text()), (False, LOGIC))

    def test_file_is_restored_when_the_build_raises(self) -> None:
        root = self.make(STRONG_TEST)
        mutant = mutate.generate_mutants(LOGIC)[0]
        broken = mutate.Suite(("logic.c",), ("test.c",), "/nonexistent/cc", ())

        with self.assertRaises(FileNotFoundError):
            mutate._mutant_killed(root, root / "logic.c", mutant, root / "bin", broken)
        self.assertEqual((root / "logic.c").read_text(), LOGIC)


class RunTest(ProjectTestCase):
    def test_raises_when_baseline_fails(self) -> None:
        root = self.make(FAILING_TEST)

        with self.assertRaises(RuntimeError):
            mutate.run(root, SUITE)

    def test_strong_suite_kills_every_mutant(self) -> None:
        root = self.make(STRONG_TEST)

        result = mutate.run(root, SUITE)

        self.assertEqual(result, mutate.Result(total=1, killed=1, survivors=()))

    def test_weak_suite_reports_the_survivor(self) -> None:
        root = self.make(WEAK_TEST)

        result = mutate.run(root, SUITE)

        self.assertEqual(
            result,
            mutate.Result(total=1, killed=0, survivors=("logic.c: L1 gt->ge col29",)),
        )


class MainTest(ProjectTestCase):
    def test_returns_zero_when_all_killed(self) -> None:
        root = self.make(STRONG_TEST)
        output = io.StringIO()

        with contextlib.redirect_stdout(output):
            code = mutate.main(["mutate.py"], root, SUITE)

        self.assertEqual((code, output.getvalue()), (0, "mutation score 1/1 killed\n"))

    def test_returns_one_and_prints_survivors(self) -> None:
        root = self.make(WEAK_TEST)
        output = io.StringIO()

        with contextlib.redirect_stdout(output):
            code = mutate.main(["mutate.py"], root, SUITE)

        self.assertEqual(
            (code, output.getvalue()),
            (1, "SURVIVED logic.c: L1 gt->ge col29\nmutation score 0/1 killed\n"),
        )

    def test_returns_one_when_no_mutants(self) -> None:
        root = self.make(
            "int above(int a);\nint main(void) { return above(0); }\n",
            "int above(int a) { return a; }\n",
        )
        output = io.StringIO()

        with contextlib.redirect_stdout(output):
            code = mutate.main(["mutate.py"], root, SUITE)

        self.assertEqual((code, output.getvalue()), (1, "mutation score 0/0 killed\n"))


if __name__ == "__main__":
    unittest.main()
