# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Tests for the JSON shape checks the bench's readers use."""

import json
import unittest
from collections.abc import Callable
from functools import partial

from tools.bench.run import _entry
from tools.bench.shapes import items, mapping, texts
from tools.bench.showcase import TEXT, from_json, parse_words


def refusal(check: Callable[[], object]) -> str:
    """The message a check raises, or "" when it accepts its input."""
    try:
        check()
    except ValueError as error:
        return str(error)
    return ""


class ShapesTest(unittest.TestCase):
    def test_each_wrong_shape_is_refused_with_its_place(self) -> None:
        # Arrange
        cases: dict[str, tuple[Callable[[], object], str]] = {
            "list as object": (lambda: mapping([], "x"), "x: expected an object"),
            "number key": (lambda: mapping({1: "a"}, "x"), "x: expected an object"),
            "number text": (lambda: texts({"a": 1}, "x"), "x: ['a'] must be strings"),
            "object as list": (lambda: items({}, "x"), "x: expected a list"),
            "list of text": (lambda: items(["a"], "x"), "x[0]: expected an object"),
        }

        # Act
        refused = {name: refusal(check) for name, (check, _) in cases.items()}

        # Assert
        self.assertEqual(refused, {name: reason for name, (_, reason) in cases.items()})

    def test_good_shapes_pass_through(self) -> None:
        # Arrange
        value = [{"a": "b"}]

        # Act
        checked = items(value, "x")

        # Assert
        self.assertEqual(checked, [{"a": "b"}])


class ShowcaseShapeTest(unittest.TestCase):
    def test_text_missing_a_language_is_refused(self) -> None:
        # Arrange
        raw = json.loads(TEXT.read_text())
        raw["heading"] = {k: v for k, v in raw["heading"].items() if k != "zh"}

        # Act
        message = refusal(lambda: parse_words(raw))

        # Assert
        self.assertEqual(message, "heading: no text for ['zh']")

    def test_text_missing_an_outcome_is_refused(self) -> None:
        # Arrange
        raw = json.loads(TEXT.read_text())
        raw["outcome"]["en"] = {
            k: v for k, v in raw["outcome"]["en"].items() if k != "n/a"
        }

        # Act
        message = refusal(lambda: parse_words(raw))

        # Assert
        self.assertEqual(message, "outcome.en: no text for ['n/a']")

    def test_results_naming_an_unknown_property_are_refused(self) -> None:
        # Arrange
        text = json.dumps({"ours": {"swap": "pass", "teleport": "pass"}})

        # Act
        message = refusal(lambda: from_json(text))

        # Assert
        self.assertEqual(message, "ours: unknown properties ['teleport']")


class ExclusionShapeTest(unittest.TestCase):
    def test_each_exclusion_shape_is_read(self) -> None:
        # Arrange
        given = [
            {"line": "a.c:3", "reason": "r"},
            {"start": "0x10", "end": "0x1f", "reason": "r"},
            {"file": "wiring.c", "reason": "r"},
        ]

        # Act
        entries = [_entry(item, "x") for item in given]

        # Assert
        self.assertEqual(
            [(e.line, e.start, e.end, e.file) for e in entries],
            [
                ("a.c:3", None, None, None),
                (None, 16, 31, None),
                (None, None, None, "wiring.c"),
            ],
        )

    def test_a_malformed_exclusion_is_refused(self) -> None:
        # Arrange
        cases: list[dict[str, object]] = [
            {"line": "a.c:3"},
            {"start": "0x10", "reason": "r"},
            {"line": 3, "reason": "r"},
        ]

        # Act
        refused = [refusal(partial(_entry, item, "x")) for item in cases]

        # Assert
        self.assertEqual(
            refused, ["x: needs reason and a line, a start and end, or a file"] * 3
        )
