# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Tests for decoding SCEx strings from any chip's DATA trace.

Traces are synthesized from the bit strings in both forms a chip may use: gate
mode, where a zero holds DATA low and a one releases it, and carrier mode,
where a one mirrors the WFCK carrier. The decoder must recover the region, the
start and the bit cell from either, with no per-chip tuning.
"""

import unittest

from tools.bench.decode import Change, decode, low_runs
from tools.bench.scex import Region, region_bits

CELL = 4_000_000
HALF = 68_000


def gate_string(bits: str, start: int, cell: int = CELL) -> list[Change]:
    """DATA for one string in gate mode: driven low for a zero, released else."""
    changes = []
    for index, bit in enumerate(bits):
        at = start + index * cell
        changes.append(Change(at, bit == "0", False))
    changes.append(Change(start + len(bits) * cell, False, False))
    return changes


def carrier_string(bits: str, start: int, cell: int = CELL) -> list[Change]:
    """DATA for one string in carrier mode: a one toggles at the WFCK rate."""
    changes = []
    for index, bit in enumerate(bits):
        at = start + index * cell
        if bit == "0":
            changes.append(Change(at, True, False))
            continue
        for edge in range(cell // HALF):
            changes.append(Change(at + edge * HALF, True, edge % 2 == 1))
    changes.append(Change(start + len(bits) * cell, False, False))
    return changes


class LowRunsTest(unittest.TestCase):
    def test_low_runs_follow_driven_low_only(self) -> None:
        changes = [
            Change(10, True, False),
            Change(20, True, True),
            Change(30, False, False),
            Change(40, True, False),
        ]

        runs = low_runs(changes, 50)

        self.assertEqual(runs, [(10, 20), (40, 50)])


class GateModeTest(unittest.TestCase):
    def test_one_string_decodes_with_its_region_and_cell(self) -> None:
        changes = gate_string(region_bits(Region.AMERICA), 1_000_000_000)

        strings = decode(changes, 2_000_000_000)

        self.assertEqual(len(strings), 1)
        self.assertEqual(strings[0].region, Region.AMERICA)
        self.assertEqual(strings[0].start_ns, 1_000_000_000)
        self.assertEqual(strings[0].cell_ns, CELL)
        self.assertFalse(strings[0].carrier)

    def test_strings_with_a_held_low_tail_decode_one_by_one(self) -> None:
        bits = region_bits(Region.AMERICA)
        period = 240_000_000
        tail = 63_000_000

        def held(start: int) -> list[Change]:
            string = gate_string(bits, start)
            end = string[-1].time_ns + tail
            return [*string[:-1], Change(end, False, False)]

        changes = [*held(0), *held(period), *held(2 * period)]

        strings = decode(changes, 3 * period)

        self.assertEqual([s.region for s in strings], [Region.AMERICA] * 3)
        self.assertEqual([s.start_ns for s in strings], [0, period, 2 * period])
        self.assertEqual([s.cell_ns for s in strings], [CELL] * 3)

    def test_a_lone_glitch_does_not_turn_a_gate_trace_into_carrier(self) -> None:
        glitch = [Change(900_000_000, True, True), Change(900_002_000, True, False)]
        changes = [*gate_string(region_bits(Region.AMERICA), 0), *glitch]

        strings = decode(changes, 1_000_000_000)

        self.assertEqual([s.region for s in strings], [Region.AMERICA])
        self.assertFalse(strings[0].carrier)

    def test_a_slow_cell_is_measured(self) -> None:
        changes = gate_string(region_bits(Region.EUROPE), 0, 4_180_000)

        strings = decode(changes, 1_000_000_000)

        self.assertEqual(strings[0].region, Region.EUROPE)
        self.assertEqual(strings[0].cell_ns, 4_180_000)

    def test_two_strings_apart_are_two_decodes(self) -> None:
        bits = region_bits(Region.JAPAN)
        first = gate_string(bits, 0)
        second = gate_string(bits, 250_000_000)

        strings = decode(first + second, 1_000_000_000)

        self.assertEqual([s.start_ns for s in strings], [0, 250_000_000])
        self.assertEqual({s.region for s in strings}, {Region.JAPAN})

    def test_a_truncated_string_has_no_region(self) -> None:
        bits = region_bits(Region.AMERICA)[:30] + "0"

        strings = decode(gate_string(bits, 0), 1_000_000_000)

        self.assertEqual(len(strings), 1)
        self.assertIsNone(strings[0].region)

    def test_a_long_low_hold_alone_is_no_string(self) -> None:
        changes = [Change(0, True, False), Change(900_000_000, False, False)]

        strings = decode(changes, 1_000_000_000)

        self.assertEqual(strings, [])

    def test_endless_fast_toggling_is_no_string(self) -> None:
        changes = [
            Change(t, True, t % 4_000 == 0) for t in range(0, 900_000_000, 2_000)
        ]

        strings = decode(changes, 1_000_000_000)

        self.assertEqual(strings, [])

    def test_short_glitches_alone_are_no_string(self) -> None:
        changes = [Change(0, True, False), Change(100_000, False, False)]

        strings = decode(changes, 1_000_000_000)

        self.assertEqual(strings, [])


class CarrierModeTest(unittest.TestCase):
    def test_a_mirrored_string_decodes_and_is_marked_carrier(self) -> None:
        changes = carrier_string(region_bits(Region.AMERICA), 500_000_000)

        strings = decode(changes, 2_000_000_000)

        self.assertEqual(len(strings), 1)
        self.assertEqual(strings[0].region, Region.AMERICA)
        self.assertEqual(strings[0].start_ns, 500_000_000)
        self.assertTrue(strings[0].carrier)

    def test_idle_low_between_strings_still_frames_each_string(self) -> None:
        bits = region_bits(Region.AMERICA)
        first = carrier_string(bits, 400_000_000)
        second = carrier_string(bits, 660_000_000)
        idle_low = [Change(0, True, False)]
        held = [
            Change(c.time_ns, True, False) if not c.driven else c
            for c in first + second
        ]

        strings = decode(idle_low + held, 2_000_000_000)

        self.assertEqual([s.start_ns for s in strings], [400_000_000, 660_000_000])
        self.assertEqual({s.region for s in strings}, {Region.AMERICA})


class GateSpreadTest(unittest.TestCase):
    def test_cell_spread_reports_uneven_cells(self) -> None:
        bits = region_bits(Region.AMERICA)
        changes = gate_string(bits, 0)
        stretched = [
            Change(c.time_ns + 200_000, c.driven, c.level)
            if c.time_ns >= 20 * CELL
            else c
            for c in changes
        ]

        strings = decode(stretched, 1_000_000_000)

        self.assertGreater(strings[0].cell_spread_ns, 0)
