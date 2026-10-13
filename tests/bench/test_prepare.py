# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Tests for preparing third-party firmware: gpasm, avra, HEX to ELF, and
UberNee against a pinned Arduino core. Needs the toolchain image, no network.
"""

import subprocess
import tempfile
import unittest
from dataclasses import replace
from functools import partial
from pathlib import Path

from tools.bench.chips import by_name
from tools.bench.manifest import Artifact
from tools.bench.prepare import (
    UBERNEE_EDITS,
    assemble,
    assemble_avr,
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

# The smallest source with what Liss's file needs: a .device line after a form
# feed, a part file on avra's include path, a .def alias and a macro.
AVR_SOURCE = """\
\f
.device AT90S1200
.include "1200def.inc"
.def A1 = r16
.macro LOAD
\tldi A1,@0
.endmacro
start:
\tLOAD 3
\tout DDRD,A1
\trjmp start
"""

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
        # Act
        image = hex_image(":0100020011EC\n:00000001FF\n")

        # Assert
        self.assertEqual(image, bytes([0xFF, 0xFF, 0x11]))

    def test_segment_and_linear_bases_move_the_records(self) -> None:
        # Arrange
        text = (
            ":020000020001FB\n:0100000022DD\n:020000040000FA\n:0100010033CB\n"
            ":00000001FF\n"
        )

        # Act
        image = hex_image(text)

        # Assert
        self.assertEqual((image[0x10], image[0x01]), (0x22, 0x33))


class EditSketchTest(unittest.TestCase):
    def test_every_configuration_line_is_replaced(self) -> None:
        # Act
        edited = edit_sketch(SKETCH)

        # Assert
        self.assertIn("#define SELECT_MAGICKEY SCEA", edited)
        self.assertIn("const bool LGT8F328P = no", edited)
        self.assertEqual(len(UBERNEE_EDITS), 2)

    def test_a_sketch_missing_a_line_is_refused(self) -> None:
        # Arrange
        given = "void setup() {}\n"

        # Act
        call = partial(edit_sketch, given)

        # Assert
        self.assertRaises(ValueError, call)


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
        # Arrange
        artifact = Artifact(
            "crow",
            "pic-source",
            "none",
            False,
            "t",
            path="crow.asm",
            defines=("MY_OSC=_IntRC_OSC",),
        )
        chip = by_name("old-crow-12c508")

        # Act
        reason = assemble(self.root, artifact, chip, PIC_SOURCE.encode(), self.work)

        # Assert
        self.assertIsNone(reason)
        self.assertTrue((self.root / chip.firmware).is_file())

    def test_a_pic_source_that_fails_says_why(self) -> None:
        # Arrange
        artifact = Artifact("crow", "pic-source", "none", False, "t", path="crow.asm")
        chip = by_name("old-crow-12c508")

        # Act
        reason = assemble(self.root, artifact, chip, b"\tbogus\n", self.work)

        # Assert
        self.assertIn("gpasm", str(reason))

    def test_an_avr_source_assembles_into_an_elf_holding_its_code(self) -> None:
        # Arrange
        chip = by_name("liss-at90s1200")

        # Act
        reason = assemble_avr(self.root, chip, AVR_SOURCE.encode(), self.work)

        # Assert
        listing = subprocess.run(
            ["avr-objdump", "-d", str(self.root / chip.firmware)],
            check=True,
            capture_output=True,
            text=True,
        ).stdout
        self.assertIsNone(reason)
        self.assertIn("rjmp", listing)

    def test_an_avr_source_that_fails_says_why(self) -> None:
        # Arrange
        text = b".device AT90S1200\n\tbogus\n"

        # Act
        reason = assemble_avr(self.root, by_name("liss-at90s1200"), text, self.work)

        # Assert
        self.assertIn("avra", str(reason))

    def test_an_avr_source_keeps_its_part_instruction_check(self) -> None:
        # Arrange
        text = AVR_SOURCE.replace("\trjmp start", "\tpush A1").encode()

        # Act
        reason = assemble_avr(self.root, by_name("liss-at90s1200"), text, self.work)

        # Assert
        self.assertIn("avra", str(reason))

    def test_an_avr_image_becomes_an_elf_holding_its_code(self) -> None:
        # Arrange
        chip = by_name("modavr-attiny13")

        # Act
        avr_elf(self.root, chip, AVR_HEX.encode(), self.work)

        # Assert
        listing = subprocess.run(
            ["avr-objdump", "-d", str(self.root / chip.firmware)],
            check=True,
            capture_output=True,
            text=True,
        ).stdout
        self.assertIn("rjmp", listing)

    def test_ubernee_builds_against_the_pinned_core(self) -> None:
        # Arrange
        sketch, core = self.sources()
        chip = by_name("ubernee-atmega328p")

        # Act
        reason = build_ubernee(self.root, sketch, core, chip, self.work)

        # Assert
        self.assertIsNone(reason)
        self.assertTrue((self.root / chip.firmware).is_file())

    def test_a_second_build_reuses_the_clones(self) -> None:
        # Arrange
        sketch, core = self.sources()
        chip = by_name("ubernee-atmega328p")
        build_ubernee(self.root, sketch, core, chip, self.work)

        # Act
        reason = build_ubernee(self.root, sketch, core, chip, self.work)

        # Assert
        self.assertIsNone(reason)

    def test_a_core_with_a_different_tree_is_refused(self) -> None:
        # Arrange
        sketch, core = self.sources()
        wrong = replace(core, tree="0" * 40)

        # Act
        reason = build_ubernee(
            self.root, sketch, wrong, by_name("ubernee-atmega328p"), self.work
        )

        # Assert
        self.assertIn("tree", str(reason))
