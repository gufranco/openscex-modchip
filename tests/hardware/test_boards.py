# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT
"""Every carrier board ships what a builder needs to order and assemble it.

A board in hardware/ is a KiCad project, named by its .kicad_pro file. Next to
it there must be a bill of materials, hardware/<board>-bom.csv, and a Gerber
and drill archive, hardware/fab/<board>-gerbers.zip, because the READMEs link
both. The bill of materials is checked against the schematic it came from: a
part added to the schematic and missed in the export, or a stale export left
after a part was removed, fails here before a builder orders the wrong set.
"""

import csv
import re
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HARDWARE = ROOT / "hardware"

# A placed symbol in a schematic starts with "(symbol (lib_id"; the library
# copies under lib_symbols start with "(symbol \"" instead and are skipped.
PLACED = re.compile(r"\(symbol \(lib_id ")
IN_BOM = re.compile(r"\(in_bom (yes|no)\)")
REFERENCE = re.compile(r'\(property "Reference" "([^"]+)"')

# The layers a board house needs: both copper layers, both masks, both silk
# layers, the outline and the plated drill file.
FAB_PARTS = (
    "-F_Cu.gtl",
    "-B_Cu.gbl",
    "-F_Mask.gts",
    "-B_Mask.gbs",
    "-F_Silkscreen.gto",
    "-B_Silkscreen.gbo",
    "-Edge_Cuts.gm1",
    "-PTH.drl",
)


def boards() -> list[str]:
    """The board names, one per KiCad project in hardware/."""
    return sorted(p.stem for p in HARDWARE.glob("*.kicad_pro"))


def schematic_references(text: str) -> set[str]:
    """The references of the schematic's symbols that belong in the BOM."""
    found: set[str] = set()
    chunks = PLACED.split(text)[1:]
    for chunk in chunks:
        in_bom = IN_BOM.search(chunk)
        reference = REFERENCE.search(chunk)
        if in_bom and reference and in_bom.group(1) == "yes":
            found.add(reference.group(1))
    return found


def bom_references(text: str) -> tuple[set[str], int]:
    """The references a BOM lists, and the sum of its Qty column."""
    rows = list(csv.DictReader(text.splitlines()))
    refs = {ref for row in rows for ref in row["References"].split(",")}
    return refs, sum(int(row["Qty"]) for row in rows)


def listed(name: str) -> tuple[set[str], int]:
    """What the board's BOM lists, or nothing when the file is missing."""
    bom = HARDWARE / f"{name}-bom.csv"
    return bom_references(bom.read_text()) if bom.exists() else (set(), 0)


def archived(name: str) -> set[str]:
    """The files in the board's fabrication archive, or none when it is missing."""
    archive = HARDWARE / "fab" / f"{name}-gerbers.zip"
    return set(zipfile.ZipFile(archive).namelist()) if archive.exists() else set()


class ParsingTest(unittest.TestCase):
    def test_a_symbol_kept_out_of_the_bom_is_not_a_reference(self) -> None:
        text = (
            '(symbol (lib_id "Device:R") (in_bom yes)'
            ' (property "Reference" "R1")\n'
            '(symbol (lib_id "Connector:TestPoint") (in_bom no)'
            ' (property "Reference" "TP1")\n'
        )

        found = schematic_references(text)

        self.assertEqual(found, {"R1"})

    def test_a_library_copy_is_not_a_placed_symbol(self) -> None:
        text = (
            '(lib_symbols (symbol "Device:R" (in_bom yes)'
            ' (property "Reference" "R")))\n'
        )

        found = schematic_references(text)

        self.assertEqual(found, set())

    def test_a_grouped_bom_row_counts_each_reference(self) -> None:
        text = '"References","Qty","Value"\n"R1,R2","2","1k"\n"C1","1","100nF"\n'

        refs, quantity = bom_references(text)

        self.assertEqual((refs, quantity), ({"R1", "R2", "C1"}, 3))


class BoardTest(unittest.TestCase):
    def test_there_are_boards_to_check(self) -> None:
        names = boards()

        self.assertEqual(names, ["openscex-bare", "openscex-carrier", "openscex-mini"])

    def test_every_board_has_a_bom_matching_its_schematic(self) -> None:
        mismatched = {}
        for name in boards():
            schematic = (HARDWARE / f"{name}.kicad_sch").read_text()
            wanted = schematic_references(schematic)
            refs, quantity = listed(name)
            if refs != wanted or quantity != len(wanted):
                mismatched[name] = (sorted(wanted), sorted(refs), quantity)

        self.assertEqual(mismatched, {})

    def test_every_board_has_a_complete_fabrication_archive(self) -> None:
        missing = {}
        for name in boards():
            members = archived(name)
            absent = [name + part for part in FAB_PARTS if name + part not in members]
            if absent:
                missing[name] = absent

        self.assertEqual(missing, {})
