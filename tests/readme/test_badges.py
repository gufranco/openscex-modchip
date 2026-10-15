# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""The numbers in the README badge line match what they count, and every
README table row has as many cells as its header.

The flash figure is the default image's text plus data as avr-size reports
it; the mutant figure is what tools/mutate.py generates; the board families
are the bench's Board list, the regions PSCU_REGION_COUNT, and the MISRA
deviations the cppcheck suppressions in the firmware, which must stay none.
The flash and mutant checks run after `make all`, which builds the image.
"""

import re
import subprocess
import unittest
from pathlib import Path

from tools.bench.scenarios import Board
from tools.mutate import LOGIC_FILES, generate_mutants

ROOT = Path(__file__).resolve().parents[2]
READMES = ("README.md", "README.ja.md", "README.zh.md")
IMAGE = ROOT / "build" / "attiny85" / "release" / "openscex-modchip-attiny85.elf"
FLASH = re.compile(r"\[\*\*(\d+)\*\*\]\(Makefile\)")
MUTANTS = re.compile(r"\[\*\*(\d+)/(\d+)\*\*\]\(tools/mutate\.py\)")
BOARDS = re.compile(r"\[\*\*(\d+)\*\*\]\(assets/psnee\)")
REGIONS = re.compile(r"\[\*\*(\d+)\*\*\]\(src/region\.c\)")
DEVIATIONS = re.compile(r"\[\*\*(\d+)\*\*\]\(AGENTS\.md\)")
REGION_COUNT = re.compile(r"#define PSCU_REGION_COUNT \(\(uint8_t\)(\d+)U\)")
UNESCAPED_PIPE = re.compile(r"(?<!\\)\|")
SIZE_TIMEOUT_S = 60


def flash_bytes(image: Path) -> int:
    """Text plus data of an AVR image, the bytes it takes in flash."""
    listing = subprocess.run(
        ["avr-size", str(image)],
        check=True,
        capture_output=True,
        text=True,
        timeout=SIZE_TIMEOUT_S,
    ).stdout.splitlines()
    text, data = listing[1].split()[:2]
    return int(text) + int(data)


def mutant_count(root: Path) -> int:
    """How many mutants the host mutation suite generates."""
    return sum(
        len(generate_mutants((root / name).read_text(), name)) for name in LOGIC_FILES
    )


def ragged_rows(text: str) -> list[int]:
    """Line numbers of table rows whose cell count differs from their header.
    GitHub splits a row on every unescaped pipe, code spans included."""
    ragged: list[int] = []
    width: int | None = None
    for number, raw in enumerate(text.splitlines(), start=1):
        line = raw.strip()
        if not line.startswith("|"):
            width = None
            continue
        cells = len(UNESCAPED_PIPE.findall(line)) - 1
        if width is None:
            width = cells
        elif cells != width:
            ragged.append(number)
    return ragged


class RaggedRowsTest(unittest.TestCase):
    def test_a_pipe_inside_a_code_span_splits_the_row(self) -> None:
        # Arrange
        text = "| a | b |\n|:--|:--|\n| `x|y` | z |\n"

        # Act
        ragged = ragged_rows(text)

        # Assert
        self.assertEqual(ragged, [3])

    def test_an_escaped_pipe_keeps_the_row_whole(self) -> None:
        # Arrange
        text = "| a | b |\n|:--|:--|\n| `x\\|y` | z |\n"

        # Act
        ragged = ragged_rows(text)

        # Assert
        self.assertEqual(ragged, [])


class BadgeTest(unittest.TestCase):
    def test_every_readme_states_the_default_image_size(self) -> None:
        # Arrange
        size = flash_bytes(IMAGE)

        # Act
        stated = {name: FLASH.findall((ROOT / name).read_text()) for name in READMES}

        # Assert
        self.assertEqual(stated, {name: [str(size)] for name in READMES})

    def test_every_readme_states_the_mutant_count(self) -> None:
        # Arrange
        total = str(mutant_count(ROOT))

        # Act
        stated = {name: MUTANTS.findall((ROOT / name).read_text()) for name in READMES}

        # Assert
        self.assertEqual(stated, {name: [(total, total)] for name in READMES})

    def test_every_readme_states_the_counted_boards_regions_and_deviations(
        self,
    ) -> None:
        # Arrange
        regions = REGION_COUNT.findall((ROOT / "include/pscu/region.h").read_text())
        suppressions = sum(
            path.read_text().count("cppcheck-suppress")
            for folder in ("src", "include")
            for path in sorted((ROOT / folder).rglob("*"))
            if path.is_file()
        )
        counted = ([str(len(Board))], regions, [str(suppressions)])

        # Act
        stated = {
            name: tuple(
                pattern.findall((ROOT / name).read_text())
                for pattern in (BOARDS, REGIONS, DEVIATIONS)
            )
            for name in READMES
        }

        # Assert
        self.assertEqual(stated, dict.fromkeys(READMES, counted))

    def test_every_readme_table_row_matches_its_header(self) -> None:
        # Arrange
        texts = {name: (ROOT / name).read_text() for name in READMES}

        # Act
        ragged = {name: ragged_rows(text) for name, text in texts.items()}

        # Assert
        self.assertEqual({k: v for k, v in ragged.items() if v}, {})
