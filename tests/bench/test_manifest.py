# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Tests for verifying third-party firmware against the artifact manifest.

The bench never trusts a file by its name: the SHA-256 of the exact bytes
decides, and a missing or different file must say what the user has and what
to do, never just "hash mismatch".
"""

import hashlib
import json
import tempfile
import unittest
from pathlib import Path

from tools.bench.manifest import ROOT, Status, check, load


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
