# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Decode SCEx strings from what one chip did to DATA, whatever its CPU.

A runner reports DATA as changes of two facts: whether the chip drives the pin,
and the level it drives; the line counts as low only while the chip drives it
low. Chips send a one in one of two ways: gate mode releases DATA for the whole
cell, carrier mode mirrors the WFCK carrier, so a one shows as either a
released stretch or a train of short pulses.

A trace in which DATA ever toggles at carrier speed is decoded in carrier mode,
else in gate mode, because what "low" means differs. In gate mode a long low
run can only be a zero: the idle line is released and high. In carrier mode a
chip may hold DATA low whenever it sends no one, between strings as well as
for zeros (Mayumi V4 does, measured 2026-10-07), so only the carrier trains are
unambiguous. A burst whose implied cell is outside 2 to 8 ms, half and double
the 4 ms every chip aims at, is activity but no string: a long hold or endless
toggling, which the metrics then count as drive outside strings. Each mode
frames a string on its unambiguous runs, using the
SCEx layout (scex.py): every string's first zero is bit 1 and its trailing
zeros start at bit 42, so in gate mode those two edges span 41 cells; its
first one is bit 0 and its last one bit 41, so in carrier mode the trains span
the 42 cells from the start to bit 41. That span gives the cell length and the
start for any chip and any clock. The gate span ends where the trailing zeros
start, not where they end, because a chip may keep DATA low long after the
string: PsNee does for about 63 ms (measured in the bench, 2026-10-08).
"""

from dataclasses import dataclass

from tools.bench.scex import SCEX_BITS, Region, identify

LONG_RUN_NS = 1_000_000
CELL_MIN_NS = 2_000_000
CELL_MAX_NS = 8_000_000
CARRIER_EDGE_NS = 200_000
CARRIER_MIN_EDGES = 4
LAST_ONE_CELL = 41
BURST_GAP_NS = 40_000_000
FIRST_ZERO_CELL = 1
LAST_ZERO_CELL = 42


@dataclass(frozen=True, slots=True)
class Change:
    """DATA from this time on: driven or released, and the driven level."""

    time_ns: int
    driven: bool
    level: bool


@dataclass(frozen=True, slots=True)
class Decoded:
    """One string as the console would have seen it on DATA.

    cell_spread_ns is the furthest any zero's edge sits from the cell grid the
    string's own span defines: zero for a clean string, larger for cells of
    uneven length.
    """

    start_ns: int
    end_ns: int
    cell_ns: int
    bits: str
    region: Region | None
    carrier: bool
    cell_spread_ns: int


def low_runs(changes: list[Change], end_ns: int) -> list[tuple[int, int]]:
    """Return the intervals during which the chip held DATA low."""
    runs = []
    since = None
    for change in changes:
        low = change.driven and not change.level
        if low and since is None:
            since = change.time_ns
        elif not low and since is not None:
            runs.append((since, change.time_ns))
            since = None
    if since is not None:
        runs.append((since, end_ns))
    return runs


def _bursts(runs: list[tuple[int, int]]) -> list[list[tuple[int, int]]]:
    """Group runs into strings: a gap, or a run, of 40 ms or more ends one.

    The longest high stretch inside a string is five ones, 20 ms, and the
    longest low stretch is shorter still; strings are at least 67 ms apart
    (PsNee and Mayumi gaps), so 40 ms splits them cleanly. A run that long can
    only be a hold after the last bit, so it closes its string, and whatever
    follows starts the next one even when the line was released only briefly.
    """
    bursts: list[list[tuple[int, int]]] = []
    for run in runs:
        joins = bool(bursts) and run[0] - bursts[-1][-1][1] < BURST_GAP_NS
        held = bool(bursts) and bursts[-1][-1][1] - bursts[-1][-1][0] >= BURST_GAP_NS
        if joins and not held:
            bursts[-1].append(run)
        else:
            bursts.append([run])
    return bursts


def _decode_gate(burst: list[tuple[int, int]]) -> Decoded | None:
    zeros = [run for run in burst if run[1] - run[0] >= LONG_RUN_NS]
    if not zeros:
        return None
    first, last = zeros[0][0], zeros[-1][0]
    cell = (last - first) // (LAST_ZERO_CELL - FIRST_ZERO_CELL)
    if not CELL_MIN_NS <= cell <= CELL_MAX_NS:
        return None
    start = first - FIRST_ZERO_CELL * cell
    bits = "".join(
        "0"
        if any(lo <= start + index * cell + cell // 2 < hi for lo, hi in zeros)
        else "1"
        for index in range(SCEX_BITS)
    )
    edges = [edge for run in zeros[:-1] for edge in run] + [last]
    spread = max(
        abs((edge - start) - round((edge - start) / cell) * cell) for edge in edges
    )
    return Decoded(
        start_ns=start,
        end_ns=start + SCEX_BITS * cell,
        cell_ns=cell,
        bits=bits,
        region=identify(bits),
        carrier=False,
        cell_spread_ns=spread,
    )


def carrier_trains(changes: list[Change]) -> list[tuple[int, int]]:
    """Return the stretches where DATA toggles at carrier speed.

    A train is a run of driven level changes each within 200 us of the last,
    three times a 7.3 kHz carrier's half period and far below a 4 ms bit cell.
    It ends half a period after its last edge, where the cell itself ends.
    A train needs at least four edges, two carrier periods: a single short
    pulse is a glitch, such as the 2 us high Mayumi V4 drives before a long
    low (measured in the bench, 2026-10-08), while one cell of ones at the
    slowest carrier, 7.3 kHz, already holds about 58 edges.
    """
    edges = [c.time_ns for c in changes if c.driven]
    trains = []
    first = previous = None
    for edge in edges:
        if previous is not None and edge - previous < CARRIER_EDGE_NS:
            first = previous if first is None else first
        elif first is not None:
            trains.append((first, previous))
            first = None
        previous = edge
    if first is not None:
        trains.append((first, previous))
    return [
        (start, end + (end - start) // (_edges_in(edges, start, end) - 1))
        for start, end in trains
        if _edges_in(edges, start, end) >= CARRIER_MIN_EDGES
    ]


def _edges_in(edges: list[int], start: int, end: int) -> int:
    return sum(1 for edge in edges if start <= edge <= end)


def _decode_carrier(burst: list[tuple[int, int]]) -> Decoded | None:
    first, last = burst[0][0], burst[-1][1]
    cell = (last - first) // (LAST_ONE_CELL + 1)
    if not CELL_MIN_NS <= cell <= CELL_MAX_NS:
        return None
    bits = "".join(
        "1"
        if any(lo <= first + index * cell + cell // 2 < hi for lo, hi in burst)
        else "0"
        for index in range(SCEX_BITS)
    )
    edges = [edge for run in burst for edge in run]
    spread = max(
        abs((edge - first) - round((edge - first) / cell) * cell) for edge in edges
    )
    return Decoded(
        start_ns=first,
        end_ns=first + SCEX_BITS * cell,
        cell_ns=cell,
        bits=bits,
        region=identify(bits),
        carrier=True,
        cell_spread_ns=spread,
    )


def decode(changes: list[Change], end_ns: int) -> list[Decoded]:
    """Decode every string in a trace, in time order."""
    trains = carrier_trains(changes)
    if trains:
        decoded = [_decode_carrier(burst) for burst in _bursts(trains)]
    else:
        decoded = [_decode_gate(b) for b in _bursts(low_runs(changes, end_ns))]
    return [string for string in decoded if string is not None]
