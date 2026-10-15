# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT
"""The board ships what a builder needs to order and assemble it, and its
layout is symmetric (AGENTS rule 16).

Each KiCad project in hardware/ needs hardware/<board>-bom.csv and
hardware/fab/<board>-gerbers.zip, which the READMEs link, and its BOM must
match its schematic. The chip, the serial header and the row of wire pads sit
on the board's vertical centre line.
"""

import csv
import math
import re
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HARDWARE = ROOT / "hardware"

# A placed symbol starts with "(symbol (lib_id"; library copies under
# lib_symbols do not.
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


# A footprint block, its placement, its reference and its pads' local offsets.
FOOTPRINT = re.compile(r"\n\t\(footprint ")
PLACE = re.compile(r"\(at ([-\d.]+) ([-\d.]+)(?: ([-\d.]+))?\)")
PAD = re.compile(r'\(pad "[^"]*"[^\n]*\n\s*\(at ([-\d.]+) ([-\d.]+)')
EDGE = re.compile(r"\((?:start|end|mid) ([-\d.]+) [-\d.]+\)")
CENTRED = ("U1", "J3")
WIRE_PADS = ("TP1", "TP2", "TP3", "TP4", "TP5", "TP6")
SYMMETRY_MM = 0.05


def pad_positions(block: str) -> list[tuple[float, float]]:
    """Where a footprint's pads sit on the board. KiCad stores each pad's
    offset unrotated and turns it by the footprint's angle, counterclockwise
    with y pointing down."""
    found = PLACE.search(block)
    if found is None:
        return []
    x, y = float(found.group(1)), float(found.group(2))
    angle = math.radians(float(found.group(3) or 0))
    return [
        (
            x + dx * math.cos(angle) + dy * math.sin(angle),
            y - dx * math.sin(angle) + dy * math.cos(angle),
        )
        for dx, dy in ((float(a), float(b)) for a, b in PAD.findall(block))
    ]


def pads_by_reference(text: str) -> dict[str, list[tuple[float, float]]]:
    """Every footprint's pad positions, by reference."""
    found: dict[str, list[tuple[float, float]]] = {}
    for block in FOOTPRINT.split(text)[1:]:
        reference = REFERENCE.search(block)
        if reference is not None:
            found[reference.group(1)] = pad_positions(block)
    return found


def centre_line(text: str) -> float:
    """The x of the board outline's vertical centre line."""
    edges = text[text.index("(gr_") :]
    xs = [
        float(x)
        for item in edges.split("(gr_")[1:]
        if "Edge.Cuts" in item
        for x in EDGE.findall(item)
    ]
    return (min(xs) + max(xs)) / 2


def off_centre(text: str) -> dict[str, float]:
    """How far each part that belongs on the centre line sits from it."""
    pads = pads_by_reference(text)
    centre = centre_line(text)
    groups = {ref: pads[ref] for ref in CENTRED}
    groups["wire pads"] = [p for ref in WIRE_PADS for p in pads[ref]]
    return {
        name: round(abs(sum(x for x, _ in points) / len(points) - centre), 3)
        for name, points in groups.items()
    }


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
        # Arrange
        text = (
            '(symbol (lib_id "Device:R") (in_bom yes)'
            ' (property "Reference" "R1")\n'
            '(symbol (lib_id "Connector:TestPoint") (in_bom no)'
            ' (property "Reference" "TP1")\n'
        )

        # Act
        found = schematic_references(text)

        # Assert
        self.assertEqual(found, {"R1"})

    def test_a_library_copy_is_not_a_placed_symbol(self) -> None:
        # Arrange
        text = (
            '(lib_symbols (symbol "Device:R" (in_bom yes)'
            ' (property "Reference" "R")))\n'
        )

        # Act
        found = schematic_references(text)

        # Assert
        self.assertEqual(found, set())

    def test_a_grouped_bom_row_counts_each_reference(self) -> None:
        # Arrange
        text = '"References","Qty","Value"\n"R1,R2","2","1k"\n"C1","1","100nF"\n'

        # Act
        refs, quantity = bom_references(text)

        # Assert
        self.assertEqual((refs, quantity), ({"R1", "R2", "C1"}, 3))


class BoardTest(unittest.TestCase):
    def test_there_are_boards_to_check(self) -> None:
        # Arrange
        expected = ["openscex"]

        # Act
        names = boards()

        # Assert
        self.assertEqual(names, expected)

    def test_every_board_has_a_bom_matching_its_schematic(self) -> None:
        # Act
        mismatched = {}
        for name in boards():
            schematic = (HARDWARE / f"{name}.kicad_sch").read_text()
            wanted = schematic_references(schematic)
            refs, quantity = listed(name)
            if refs != wanted or quantity != len(wanted):
                mismatched[name] = (sorted(wanted), sorted(refs), quantity)

        # Assert
        self.assertEqual(mismatched, {})

    def test_every_board_has_a_complete_fabrication_archive(self) -> None:
        # Act
        missing = {}
        for name in boards():
            members = archived(name)
            absent = [name + part for part in FAB_PARTS if name + part not in members]
            if absent:
                missing[name] = absent

        # Assert
        self.assertEqual(missing, {})

    def test_the_board_is_symmetric_about_its_centre_line(self) -> None:
        # Arrange
        text = (HARDWARE / "openscex.kicad_pcb").read_text()

        # Act
        offsets = off_centre(text)

        # Assert
        self.assertEqual(
            {name: offset for name, offset in offsets.items() if offset > SYMMETRY_MM},
            {},
        )

    def test_a_rotated_pad_is_placed_by_the_footprint_angle(self) -> None:
        # Arrange
        block = '(at 119.62 106 -90)\n(pad "2" thru_hole circle\n\t(at 0 2.54 270)'

        # Act
        placed = pad_positions(block)

        # Assert
        rounded = [(round(x, 2), round(y, 2)) for x, y in placed]
        self.assertEqual(rounded, [(117.08, 106.0)])
