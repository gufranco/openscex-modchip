# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Integration tests for running chips through scenarios.

These run the real runners on real firmware inside the pinned toolchain: ours
as built by `make all`, and a small PIC program of this project's own,
assembled here with gpasm, so no third-party firmware is needed. PsNee's
fetch-and-build path is exercised on a throwaway local git repository holding a
minimal sketch, which keeps the test offline while checking the tree-id guard.
"""

import hashlib
import json
import os
import shutil
import subprocess
import tempfile
import unittest
from dataclasses import asdict, replace
from pathlib import Path

from tools.bench.chips import Chip, Simulator, by_name
from tools.bench.manifest import ROOT, Artifact
from tools.bench.run import (
    _hex_bytes,
    _instruction_address,
    build_psnee,
    prepare,
    program_addresses,
    run,
)
from tools.bench.scenarios import by_name as scenario

# A 12F629 program that drives GP1 (package pin 6, DATA in the pin maps) low
# and high forever with no delay: plenty of DATA activity, never a string.
FIXTURE_ASM = """
    list p=12f629
    include "p12f629.inc"
    __config _INTRC_OSC_NOCLKOUT & _WDT_OFF & _MCLRE_OFF
    org 0
    bsf STATUS, RP0
    movlw b'11111101'
    movwf TRISIO
    bcf STATUS, RP0
loop
    bcf GPIO, 1
    bsf GPIO, 1
    goto loop
    end
"""

# A sketch with its own main, standing in for PsNee in the build test.
FIXTURE_SKETCH = "int main(void) {\n  for (;;) {\n  }\n}\n"


def git(cwd: Path, *args: str) -> str:
    return subprocess.run(
        [
            "git",
            "-c",
            "user.name=Test",
            "-c",
            "user.email=test@example.invalid",
            "-c",
            "commit.gpgsign=false",
            *args,
        ],
        cwd=cwd,
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()


class OursTest(unittest.TestCase):
    def test_ours_is_ready_once_built(self) -> None:
        reason = prepare(ROOT, by_name("ours"), Path(tempfile.gettempdir()))

        self.assertIsNone(reason)

    def test_ours_accepts_the_carrier_lead_in_and_stays_silent_in_play(self) -> None:
        with tempfile.TemporaryDirectory() as work:
            result = run(ROOT, by_name("ours"), scenario("carrier-accept"), Path(work))

        self.assertTrue(result.metrics.would_accept)
        self.assertEqual(result.metrics.during_play, 0)
        self.assertTrue(result.metrics.carrier)
        self.assertGreater(len(result.executed), 100)

    def test_ours_program_addresses_hold_every_instruction_start(self) -> None:
        addresses = program_addresses(ROOT, by_name("ours"))

        self.assertGreater(len(addresses), 1000)
        self.assertIn(0, addresses)
        self.assertTrue(all(address % 2 == 0 for address in addresses))


class PicRunnerTest(unittest.TestCase):
    def test_a_pic_program_drives_data_and_is_measured(self) -> None:
        with tempfile.TemporaryDirectory() as work:
            folder = Path(work)
            (folder / "fixture.asm").write_text(FIXTURE_ASM)
            subprocess.run(
                ["gpasm", "-o", "fixture.hex", "fixture.asm"],
                cwd=folder,
                check=True,
                capture_output=True,
            )
            chip = Chip(
                name="fixture",
                simulator=Simulator.PIC,
                cpu="p12f629",
                clock_hz=4_000_000,
                pins="data=6",
                firmware=str(folder / "fixture.hex"),
                artifact=None,
                field_proven=False,
                origin="test fixture",
                boards=frozenset(),
            )

            result = run(ROOT, chip, scenario("no-disc"), folder)
            addresses = program_addresses(ROOT, chip)

        self.assertEqual(result.metrics.strings, 0)
        self.assertGreater(result.metrics.data_driven_outside_ns, 0)
        self.assertEqual(addresses, frozenset(range(7)))
        self.assertLessEqual(result.executed, addresses)


class PrepareTest(unittest.TestCase):
    def test_missing_ours_says_to_build_first(self) -> None:
        with tempfile.TemporaryDirectory() as work:
            reason = prepare(Path(work), by_name("ours"), Path(work))

        self.assertIn("make all", str(reason))

    def test_a_missing_image_is_skipped_with_how_to_get_it(self) -> None:
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            shutil.copy(ROOT / "artifacts.manifest.json", root)

            reason = prepare(root, by_name("mayumi-v4"), root)

        self.assertIn("quade.co", str(reason))

    def test_a_present_image_with_the_right_bytes_is_ready(self) -> None:
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            target = root / "image.hex"
            target.write_bytes(b"bytes")
            entry = {
                "id": "img",
                "kind": "pic-image",
                "path": "image.hex",
                "sha256": hashlib.sha256(b"bytes").hexdigest(),
                "license": "test",
                "redistributable": True,
                "obtain": "test",
            }
            (root / "artifacts.manifest.json").write_text(
                json.dumps({"artifacts": [entry]})
            )
            chip = replace(by_name("mayumi-v4"), artifact="img", firmware="image.hex")

            reason = prepare(root, chip, root)

        self.assertIsNone(reason)


class PsneeBuildTest(unittest.TestCase):
    def setUp(self) -> None:
        self._dir = tempfile.TemporaryDirectory()
        self.base = Path(self._dir.name)
        self.repo = self.base / "upstream"
        (self.repo / "PSNee").mkdir(parents=True)
        (self.repo / "PSNee" / "PSNee.ino").write_text(FIXTURE_SKETCH)
        git(self.repo, "init", "--quiet")
        git(self.repo, "add", ".")
        git(self.repo, "commit", "--quiet", "-m", "sketch")
        self.root = self.base / "root"
        (self.root / "build" / "bench").mkdir(parents=True)
        self.work = self.base / "work"
        self.work.mkdir()

    def tearDown(self) -> None:
        self._dir.cleanup()

    def artifact(self, tree: str) -> Artifact:
        return Artifact(
            id="psnee-v9",
            kind="git-source",
            license="Unlicense",
            redistributable=True,
            obtain="test",
            url=str(self.repo),
            commit=git(self.repo, "rev-parse", "HEAD"),
            tree=tree,
        )

    def test_the_pinned_tree_builds_both_targets(self) -> None:
        tree = git(self.repo, "rev-parse", "HEAD^{tree}")

        reason = build_psnee(self.root, self.artifact(tree), self.work)

        self.assertIsNone(reason)
        self.assertTrue((self.root / "build/bench/psnee-attiny85.elf").is_file())
        self.assertTrue((self.root / "build/bench/psnee-atmega328p.elf").is_file())

    def test_a_relative_workdir_still_finds_the_sketch(self) -> None:
        tree = git(self.repo, "rev-parse", "HEAD^{tree}")
        relative = Path(os.path.relpath(self.work))

        reason = build_psnee(self.root, self.artifact(tree), relative)

        self.assertIsNone(reason)

    def test_a_second_build_reuses_the_clone(self) -> None:
        tree = git(self.repo, "rev-parse", "HEAD^{tree}")
        build_psnee(self.root, self.artifact(tree), self.work)

        reason = build_psnee(self.root, self.artifact(tree), self.work)

        self.assertIsNone(reason)
        self.assertTrue((self.work / "psnee-src" / ".git").is_dir())

    def test_a_different_tree_is_refused(self) -> None:
        reason = build_psnee(self.root, self.artifact("0" * 40), self.work)

        self.assertIn("expected", str(reason))

    def test_prepare_builds_a_missing_psnee_image(self) -> None:
        tree = git(self.repo, "rev-parse", "HEAD^{tree}")
        manifest = {"artifacts": [asdict(self.artifact(tree))]}
        (self.root / "artifacts.manifest.json").write_text(json.dumps(manifest))

        first = prepare(self.root, by_name("psnee-attiny85"), self.work)
        second = prepare(self.root, by_name("psnee-attiny85"), self.work)

        self.assertIsNone(first)
        self.assertIsNone(second)


class ListingTest(unittest.TestCase):
    def test_an_instruction_line_gives_its_address(self) -> None:
        address = _instruction_address("  1a4:\t0f 92       \tpush\tr0")

        self.assertEqual(address, 0x1A4)

    def test_labels_headers_and_data_lines_give_none(self) -> None:
        lines = [
            "00000000 <__vectors>:",
            "",
            "Disassembly of section .text:",
            " zz:\tx",
        ]

        addresses = [_instruction_address(line) for line in lines]

        self.assertEqual(addresses, [None, None, None, None])


class HexTest(unittest.TestCase):
    def test_an_extended_address_record_moves_the_base(self) -> None:
        with tempfile.TemporaryDirectory() as work:
            path = Path(work) / "x.hex"
            path.write_text(":020000040001F9\n:0100000000FF\n:00000001FF\n")

            addresses = _hex_bytes(path)

        self.assertEqual(addresses, {0x10000})
