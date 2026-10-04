# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools import mutate


class GenerateMutantsTest(unittest.TestCase):
    def test_skips_preprocessor_and_comment_lines(self) -> None:
        source = "#include <stdint.h>\n// a == b\nint x;"

        mutants = mutate.generate_mutants(source)

        self.assertEqual(mutants, [])

    def test_skips_assert_guard_lines(self) -> None:
        source = "  PSCU_ASSERT(index < COUNT);"

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
            sorted(["L1 ge->gt col25", "L1 le->lt col33"]),
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


class CompileAndRunTest(unittest.TestCase):
    def test_returns_false_when_compile_fails(self) -> None:
        with mock.patch.object(
            mutate.subprocess,
            "run",
            return_value=subprocess.CompletedProcess([], 1),
        ) as run:
            result = mutate._compile_and_run(Path("/repo"), Path("/tmp/bin"))

        self.assertFalse(result)
        run.assert_called_once()

    def test_returns_true_when_compile_and_run_pass(self) -> None:
        outcomes = [
            subprocess.CompletedProcess([], 0),
            subprocess.CompletedProcess([], 0),
        ]

        with mock.patch.object(mutate.subprocess, "run", side_effect=outcomes):
            result = mutate._compile_and_run(Path("/repo"), Path("/tmp/bin"))

        self.assertTrue(result)

    def test_returns_false_when_run_fails(self) -> None:
        outcomes = [
            subprocess.CompletedProcess([], 0),
            subprocess.CompletedProcess([], 1),
        ]

        with mock.patch.object(mutate.subprocess, "run", side_effect=outcomes):
            result = mutate._compile_and_run(Path("/repo"), Path("/tmp/bin"))

        self.assertFalse(result)


class MutantKilledTest(unittest.TestCase):
    def test_restores_original_and_reports_killed(self) -> None:
        source_file = self._write("int x == y;")
        mutant = mutate.Mutant("L1 eq->ne col7", "int x != y;")

        with mock.patch.object(mutate, "_compile_and_run", return_value=False):
            killed = mutate._mutant_killed(
                source_file.parent, source_file, mutant, source_file.parent / "bin"
            )

        self.assertTrue(killed)
        self.assertEqual(source_file.read_text(), "int x == y;")

    def test_surviving_mutant_still_restores_file(self) -> None:
        source_file = self._write("int y;")
        mutant = mutate.Mutant("L1", "int z;")

        with mock.patch.object(mutate, "_compile_and_run", return_value=True):
            killed = mutate._mutant_killed(
                source_file.parent, source_file, mutant, source_file.parent / "bin"
            )

        self.assertFalse(killed)
        self.assertEqual(source_file.read_text(), "int y;")

    def _write(self, text: str) -> Path:
        directory = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, directory)
        path = directory / "mutant.c"
        path.write_text(text)
        return path


class RunTest(unittest.TestCase):
    def test_raises_when_baseline_fails(self) -> None:
        with (
            mock.patch.object(mutate, "_compile_and_run", return_value=False),
            self.assertRaises(RuntimeError),
        ):
            mutate.run(Path("/repo"))

    def test_counts_killed_and_survivors(self) -> None:
        mutants = [mutate.Mutant("La", "a"), mutate.Mutant("Lb", "b")]

        killed_pattern = [True, False] * len(mutate.LOGIC_FILES)

        with (
            mock.patch.object(mutate, "_compile_and_run", return_value=True),
            mock.patch.object(mutate, "generate_mutants", return_value=mutants),
            mock.patch.object(Path, "read_text", return_value="src"),
            mock.patch.object(mutate, "_mutant_killed", side_effect=killed_pattern),
        ):
            result = mutate.run(mutate.ROOT)

        per_file = 2
        self.assertEqual(result.total, len(mutate.LOGIC_FILES) * per_file)
        self.assertEqual(result.killed, len(mutate.LOGIC_FILES))
        self.assertTrue(all("Lb" in survivor for survivor in result.survivors))


class MainTest(unittest.TestCase):
    def test_returns_zero_when_all_killed(self) -> None:
        result = mutate.Result(total=3, killed=3, survivors=())

        with mock.patch.object(mutate, "run", return_value=result):
            code = mutate.main(["mutate.py"])

        self.assertEqual(code, 0)

    def test_returns_one_and_prints_survivors(self) -> None:
        result = mutate.Result(total=3, killed=2, survivors=("src/x.c: L1",))

        with mock.patch.object(mutate, "run", return_value=result):
            code = mutate.main(["mutate.py"])

        self.assertEqual(code, 1)

    def test_returns_one_when_no_mutants(self) -> None:
        result = mutate.Result(total=0, killed=0, survivors=())

        with mock.patch.object(mutate, "run", return_value=result):
            code = mutate.main(["mutate.py"])

        self.assertEqual(code, 1)


if __name__ == "__main__":
    unittest.main()
