# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Tests for preparing third-party firmware the bench builds itself: PIC
sources assembled with gpasm, an AVR image converted to an ELF, and UberNee
built against a pinned Arduino core.

The build tests run the pinned toolchain on small fixtures in local git
repositories, so they need the toolchain image and no network.
"""

import subprocess
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path

from tools.bench.chips import by_name
from tools.bench.manifest import Artifact
from tools.bench.prepare import (
    UBERNEE_EDITS,
    assemble,
    avr_elf,
    build_ubernee,
    edit_sketch,
    hex_image,
)

PIC_SOURCE = """\
\tlist p=12c508
\tinclude "p12c508.inc"
\t__config _CP_OFF & _WDT_OFF & _MCLRE_OFF & MY_OSC
\torg 0
start\tgoto start
\tend
"""

AVR_HEX = ":0400000000C0FFCF6E\n:00000001FF\n"

SKETCH = """\
#define SELECT_MAGICKEY SCEE    // region
#define yes true;
#define no false;
const bool LGT8F328P = yes      // board
void setup() {}
void loop() {}
"""

CORE_FILES = {
    "cores/arduino/Arduino.h": (
        "#include <avr/io.h>\nvoid setup(void);\nvoid loop(void);\n"
    ),
    "cores/arduino/main.cpp": (
        '#include "Arduino.h"\nint main(void) { setup(); for (;;) { loop(); } }\n'
    ),
    "cores/arduino/hooks.c": "void yield(void) {}\n",
    "variants/standard/pins_arduino.h": "#define NUM_DIGITAL_PINS 20\n",
}


def git(repo: Path, *args: str) -> str:
    return subprocess.run(
        ["git", *args], cwd=repo, check=True, capture_output=True, text=True
    ).stdout.strip()


def repository(path: Path, files: dict[str, str]) -> Path:
    for name, text in files.items():
        target = path / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text)
    git(path, "init", "--quiet")
    git(path, "add", ".")
    git(path, "-c", "user.name=t", "-c", "user.email=t@t", "commit", "-qm", "f")
    return path


def source(repo: Path, artifact_id: str, sketch: str | None = None) -> Artifact:
    return Artifact(
        id=artifact_id,
        kind="git-source",
        license="test",
        redistributable=True,
        obtain="test",
        url=str(repo),
        commit=git(repo, "rev-parse", "HEAD"),
        tree=git(repo, "rev-parse", "HEAD^{tree}"),
        sketch=sketch,
    )


class HexImageTest(unittest.TestCase):
    def test_records_are_placed_with_gaps_erased(self) -> None:
        image = hex_image(":0100020011EC\n:00000001FF\n")

        self.assertEqual(image, bytes([0xFF, 0xFF, 0x11]))

    def test_segment_and_linear_bases_move_the_records(self) -> None:
        text = ":020000020001FB\n:0100000022DD\n:020000040000FA\n:0100010033CB\n"

        image = hex_image(text)

        self.assertEqual((image[0x10], image[0x01]), (0x22, 0x33))


class EditSketchTest(unittest.TestCase):
    def test_every_configuration_line_is_replaced(self) -> None:
        edited = edit_sketch(SKETCH)

        self.assertIn("#define SELECT_MAGICKEY SCEA", edited)
        self.assertIn("const bool LGT8F328P = no", edited)
        self.assertEqual(len(UBERNEE_EDITS), 2)

    def test_a_sketch_missing_a_line_is_refused(self) -> None:
        with self.assertRaises(ValueError):
            edit_sketch("void setup() {}\n")


class BuildTest(unittest.TestCase):
    def setUp(self) -> None:
        self._dir = tempfile.TemporaryDirectory()
        self.base = Path(self._dir.name)
        self.root = self.base / "root"
        (self.root / "build" / "bench").mkdir(parents=True)
        self.work = self.base / "work"
        self.work.mkdir()

    def tearDown(self) -> None:
        self._dir.cleanup()

    def sources(self) -> tuple[Artifact, Artifact]:
        core = source(repository(self.base / "core", CORE_FILES), "arduino-core")
        sketch_repo = repository(self.base / "ub", {"UberNee/U.ino": SKETCH})
        return source(sketch_repo, "ubernee", "UberNee/U.ino"), core

    def test_a_pic_source_assembles_with_its_defines(self) -> None:
        (self.root / "crow.asm").write_text(PIC_SOURCE)
        artifact = Artifact(
            "crow",
            "pic-source",
            "none",
            False,
            "t",
            path="crow.asm",
            defines=["MY_OSC=_IntRC_OSC"],
        )
        chip = by_name("old-crow-12c508")

        reason = assemble(self.root, artifact, chip)

        self.assertIsNone(reason)
        self.assertTrue((self.root / chip.firmware).is_file())

    def test_a_pic_source_that_fails_says_why(self) -> None:
        (self.root / "crow.asm").write_text("\tbogus\n")
        artifact = Artifact("crow", "pic-source", "none", False, "t", path="crow.asm")

        reason = assemble(self.root, artifact, by_name("old-crow-12c508"))

        self.assertIn("gpasm", str(reason))

    def test_an_avr_image_becomes_an_elf_holding_its_code(self) -> None:
        (self.root / "m.hex").write_text(AVR_HEX)
        artifact = Artifact("m", "avr-image", "none", False, "t", path="m.hex")
        chip = by_name("modavr-attiny13")

        avr_elf(self.root, artifact, chip, self.work)

        listing = subprocess.run(
            ["avr-objdump", "-d", str(self.root / chip.firmware)],
            check=True,
            capture_output=True,
            text=True,
        ).stdout
        self.assertIn("rjmp", listing)

    def test_ubernee_builds_against_the_pinned_core(self) -> None:
        sketch, core = self.sources()
        chip = by_name("ubernee-atmega328p")

        reason = build_ubernee(self.root, sketch, core, chip, self.work)

        self.assertIsNone(reason)
        self.assertTrue((self.root / chip.firmware).is_file())

    def test_a_second_build_reuses_the_clones(self) -> None:
        sketch, core = self.sources()
        chip = by_name("ubernee-atmega328p")
        build_ubernee(self.root, sketch, core, chip, self.work)

        reason = build_ubernee(self.root, sketch, core, chip, self.work)

        self.assertIsNone(reason)

    def test_a_core_with_a_different_tree_is_refused(self) -> None:
        sketch, core = self.sources()
        wrong = replace(core, tree="0" * 40)

        reason = build_ubernee(
            self.root, sketch, wrong, by_name("ubernee-atmega328p"), self.work
        )

        self.assertIn("tree", str(reason))
