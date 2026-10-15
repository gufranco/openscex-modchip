# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Every Python test reads as Arrange, Act, Assert.

Each test function carries the bare `# Act` and `# Assert` markers in that
order, with `# Arrange` before them when the test sets anything up, so a
reader finds the call under test and its checks without reading the setup.
"""

import ast
import unittest
from pathlib import Path

TESTS = Path(__file__).resolve().parent
MARKERS = ("# Arrange", "# Act", "# Assert")


def unmarked(source: str) -> list[str]:
    """The test functions in a source whose markers are missing or out of
    order."""
    lines = source.split("\n")
    wrong: list[str] = []
    for node in ast.walk(ast.parse(source)):
        if not isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
            continue
        if not node.name.startswith("test_"):
            continue
        body = [line.strip() for line in lines[node.lineno : node.end_lineno]]
        found = [line for line in body if line in MARKERS]
        if found not in (list(MARKERS), list(MARKERS[1:])):
            wrong.append(node.name)
    return wrong


class UnmarkedTest(unittest.TestCase):
    def test_markers_out_of_order_are_reported(self) -> None:
        # Arrange
        source = "def test_x():\n    # Assert\n    a = 1\n    # Act\n    b = 2\n"

        # Act
        wrong = unmarked(source)

        # Assert
        self.assertEqual(wrong, ["test_x"])

    def test_act_and_assert_alone_are_enough(self) -> None:
        # Arrange
        source = "def test_x():\n    # Act\n    a = 1\n\n    # Assert\n    b = 2\n"

        # Act
        wrong = unmarked(source)

        # Assert
        self.assertEqual(wrong, [])

    def test_an_unmarked_async_test_is_reported(self) -> None:
        # Arrange
        source = "async def test_x():\n    a = 1\n"

        # Act
        wrong = unmarked(source)

        # Assert
        self.assertEqual(wrong, ["test_x"])


class StructureTest(unittest.TestCase):
    def test_every_python_test_is_marked(self) -> None:
        # Arrange
        files = sorted(TESTS.rglob("test_*.py"))

        # Act
        wrong = {
            str(path.relative_to(TESTS)): names
            for path in files
            if (names := unmarked(path.read_text()))
        }

        # Assert
        self.assertEqual(wrong, {})
