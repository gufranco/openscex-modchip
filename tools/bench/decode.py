# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Decode SCEx strings from what one chip did to DATA, whatever its CPU.

A one is either DATA released for the cell (gate mode) or a train of pulses
mirroring WFCK (carrier mode). Each burst is decoded in its own mode: a chip
moved from a carrier board to a gate board, or one that changes mode after a
power cycle, sends both kinds in one trace. Strings are framed on the SCEx
layout (scex.py), which gives each string's cell length and start. A cell
outside 2 to 8 ms is no string.
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

    cell_spread_ns is the furthest any zero's edge sits from the string's own
    cell grid: zero for a clean string.
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

    A string's longest stretch is 20 ms and strings are at least 67 ms apart
    (PsNee and Mayumi gaps).
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

    A train is at least four driven edges, each within 200 us of the last,
    three half periods of a 7.3 kHz carrier. Fewer edges are a glitch.
    """
    edges = [c.time_ns for c in changes if c.driven]
    trains: list[tuple[int, int]] = []
    first: int | None = None
    previous: int | None = None
    for edge in edges:
        if previous is not None and edge - previous < CARRIER_EDGE_NS:
            first = previous if first is None else first
        elif first is not None and previous is not None:
            trains.append((first, previous))
            first = None
        previous = edge
    if first is not None and previous is not None:
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
    """Decode every string in a trace, in time order. Low runs inside a carrier
    burst are its zero cells, so only the runs outside every carrier burst are
    decoded in gate mode."""
    carrier_bursts = _bursts(carrier_trains(changes))
    spans = [(burst[0][0], burst[-1][1]) for burst in carrier_bursts]
    gate_runs = [
        run
        for run in low_runs(changes, end_ns)
        if not any(run[0] < high and low < run[1] for low, high in spans)
    ]
    decoded = [_decode_carrier(burst) for burst in carrier_bursts] + [
        _decode_gate(burst) for burst in _bursts(gate_runs)
    ]
    strings = [string for string in decoded if string is not None]
    return sorted(strings, key=lambda string: string.start_ns)
