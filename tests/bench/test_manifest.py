# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Tests for verifying third-party firmware against the artifact manifest by
SHA-256."""

import hashlib
import json
import tempfile
import unittest
from pathlib import Path

from tools.bench.manifest import ROOT, Status, check, load, parse_entry


def write_manifest(root: Path, content: bytes) -> None:
    entry = {
        "id": "chip-x",
        "kind": "pic-image",
        "path": "images/chip.hex",
        "sha256": hashlib.sha256(content).hexdigest(),
        "license": "none stated",
        "redistributable": False,
        "obtain": "download chip-x from its guide",
    }
    (root / "artifacts.manifest.json").write_text(json.dumps({"artifacts": [entry]}))


class LoadTest(unittest.TestCase):
    def test_the_project_manifest_loads_every_entry(self) -> None:
        artifacts = load(ROOT)

        self.assertIn("mayumi-v4-12c508a-usa", artifacts)
        self.assertEqual(artifacts["psnee-v9"].license, "Unlicense")
        self.assertTrue(artifacts["psnee-v9"].redistributable)


GOOD_ENTRY: dict[str, object] = {
    "id": "chip-x",
    "kind": "pic-source",
    "path": "chip.asm",
    "sha256": "0" * 64,
    "license": "none stated",
    "redistributable": False,
    "obtain": "from its guide",
    "defines": ["_INT_OSC=_IntRC_OSC"],
}


def refusal(entry: object) -> str:
    """The message parse_entry raises for an entry, or "" when it accepts it."""
    try:
        parse_entry(entry, "entry 1")
    except ValueError as error:
        return str(error)
    return ""


class ParseEntryTest(unittest.TestCase):
    def test_a_good_entry_keeps_its_defines_as_a_tuple(self) -> None:
        # Act
        artifact = parse_entry(GOOD_ENTRY, "entry 1")

        # Assert
        self.assertEqual(artifact.defines, ("_INT_OSC=_IntRC_OSC",))

    def test_each_malformed_entry_is_refused_with_its_reason(self) -> None:
        # Arrange
        cases = {
            "not an object": (["chip-x"], "entry 1: an entry must be an object"),
            "unknown field": (
                {**GOOD_ENTRY, "sha265": "x"},
                "entry 1: unknown fields ['sha265']",
            ),
            "unknown kind": (
                {**GOOD_ENTRY, "kind": "rom"},
                "entry 1: unknown kind 'rom'",
            ),
            "missing hash": (
                {k: v for k, v in GOOD_ENTRY.items() if k != "sha256"},
                "entry 1: pic-source needs ['sha256']",
            ),
            "short hash": (
                {**GOOD_ENTRY, "sha256": "abc"},
                "entry 1: sha256 must be 64 lowercase hex digits",
            ),
            "number path": (
                {**GOOD_ENTRY, "path": 7},
                "entry 1: path must be a string",
            ),
            "text flag": (
                {**GOOD_ENTRY, "redistributable": "no"},
                "entry 1: redistributable must be true or false",
            ),
            "string defines": (
                {**GOOD_ENTRY, "defines": "_INT_OSC"},
                "entry 1: defines must be a list of strings",
            ),
            "source without tree": (
                {
                    "id": "src",
                    "kind": "git-source",
                    "url": "https://example.invalid/x.git",
                    "commit": "1" * 40,
                    "license": "MIT",
                    "redistributable": True,
                    "obtain": "fetched",
                },
                "entry 1: git-source needs ['tree']",
            ),
        }

        # Act
        refused = {name: refusal(entry) for name, (entry, _) in cases.items()}

        # Assert
        self.assertEqual(refused, {name: reason for name, (_, reason) in cases.items()})


class LoadShapeTest(unittest.TestCase):
    def setUp(self) -> None:
        self._dir = tempfile.TemporaryDirectory()
        self.root = Path(self._dir.name)

    def tearDown(self) -> None:
        self._dir.cleanup()

    def load_error(self, data: object) -> str:
        (self.root / "artifacts.manifest.json").write_text(json.dumps(data))
        with self.assertRaises(ValueError) as raised:
            load(self.root)
        return str(raised.exception)

    def test_a_manifest_without_an_artifacts_list_is_refused(self) -> None:
        # Arrange
        data: dict[str, object] = {"artifact": []}

        # Act
        message = self.load_error(data)

        # Assert
        self.assertEqual(message, "artifacts.manifest.json: needs an artifacts list")

    def test_a_repeated_id_is_refused(self) -> None:
        # Arrange
        data = {"artifacts": [GOOD_ENTRY, GOOD_ENTRY]}

        # Act
        message = self.load_error(data)

        # Assert
        self.assertEqual(message, "artifacts.manifest.json: an artifact id is repeated")


class CheckTest(unittest.TestCase):
    def setUp(self) -> None:
        self._dir = tempfile.TemporaryDirectory()
        self.root = Path(self._dir.name)
        write_manifest(self.root, b"good image")
        (self.root / "images").mkdir()

    def tearDown(self) -> None:
        self._dir.cleanup()

    def test_matching_bytes_are_ready(self) -> None:
        (self.root / "images" / "chip.hex").write_bytes(b"good image")

        result = check(self.root, load(self.root)["chip-x"])

        self.assertIs(result.status, Status.READY)

    def test_a_missing_file_says_where_and_how_to_get_it(self) -> None:
        result = check(self.root, load(self.root)["chip-x"])

        self.assertIs(result.status, Status.MISSING)
        self.assertIn("images/chip.hex", result.message)
        self.assertIn("download chip-x", result.message)

    def test_different_bytes_name_both_hashes(self) -> None:
        (self.root / "images" / "chip.hex").write_bytes(b"other image")

        result = check(self.root, load(self.root)["chip-x"])

        self.assertIs(result.status, Status.MISMATCH)
        self.assertIn(hashlib.sha256(b"good image").hexdigest(), result.message)
        self.assertIn(hashlib.sha256(b"other image").hexdigest(), result.message)

    def test_a_source_entry_is_not_a_file_to_check(self) -> None:
        result = check(ROOT, load(ROOT)["psnee-v9"])

        self.assertIs(result.status, Status.READY)
