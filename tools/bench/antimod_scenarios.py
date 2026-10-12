# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""The anti-modchip checks games run, as console timelines.

A game seeks into the program area, resets the SCEx counters with 19h,04h and
reads them with 19h,05h; any count means a chip is sending (psx-spx
cdromformat.md). That is the PROBE window. APv1 is the probe alone. APv2 first
re-reads the lead-in with ReadTOC and needs a full string there (tonyhax
docs/ap_v2.c, aprip readme): the REREAD window.

Durations are not measured: re-read 4 s, seek 1 s, probe 2 s (psx-spx: "wait
1-2 seconds"). The probe plays CD-DA at single speed, so SPEED is low.
Judged by the showcase only.
"""

from collections.abc import Callable

from tools.bench.edge_scenarios import carrier, edge
from tools.bench.scenarios import (
    BOOT_NS,
    CARRIER_BOARDS,
    DOUBLE_SPEED,
    EARLY_GATE_BOARDS,
    LATE_GATE_BOARDS,
    LEAD_IN_FRAMES,
    REREAD_FRAMES,
    SINGLE_SPEED,
    Board,
    Phase,
    Scenario,
)
from tools.bench.timeline import LEAD_IN, PROGRAM, Signal, Timeline

PLAY_FRAMES = 3 * 75
SEEK_FRAMES = 75
PROBE_FRAMES = 2 * 75
TAIL_FRAMES = 75


def _speed(timeline: Timeline, on_carrier: bool, level: int) -> Timeline:
    """Set the SPEED level a gate board's chip senses; carrier boards need
    nothing, as the frames pulse XLAT."""
    return timeline if on_carrier else timeline.set(Signal.SENSE, level)


def _start(on_carrier: bool) -> Timeline:
    if on_carrier:
        return carrier().idle(BOOT_NS)
    return Timeline().set(Signal.SENSE, SINGLE_SPEED).idle(BOOT_NS)


def _played(on_carrier: bool) -> Timeline:
    """Boot, the lead-in at single speed, then the game running at double."""
    timeline = _start(on_carrier).mark(Phase.LEAD_IN).frames(LEAD_IN, LEAD_IN_FRAMES)
    timeline.mark(Phase.PROGRAM)
    return _speed(timeline, on_carrier, DOUBLE_SPEED).frames(PROGRAM, PLAY_FRAMES)


def _probe(timeline: Timeline, on_carrier: bool) -> Timeline:
    """The counter probe: CD-DA play in the program area at single speed."""
    _speed(timeline.mark(Phase.PROBE), on_carrier, SINGLE_SPEED)
    timeline.frames(PROGRAM, PROBE_FRAMES).mark(Phase.AFTER_PROBE)
    return timeline.frames(PROGRAM, TAIL_FRAMES)


def _version_one(on_carrier: bool) -> Callable[[], Timeline]:
    return lambda: _probe(_played(on_carrier), on_carrier)


def _version_two(on_carrier: bool) -> Callable[[], Timeline]:
    def build() -> Timeline:
        timeline = _played(on_carrier).mark(Phase.REREAD)
        _speed(timeline, on_carrier, SINGLE_SPEED).frames(LEAD_IN, REREAD_FRAMES)
        timeline.mark(Phase.SECOND_PROGRAM)
        _speed(timeline, on_carrier, DOUBLE_SPEED).frames(PROGRAM, SEEK_FRAMES)
        return _probe(timeline, on_carrier)

    return build


_FAMILIES: tuple[tuple[str, frozenset[Board], bool], ...] = (
    ("carrier", CARRIER_BOARDS, True),
    ("gate-early", EARLY_GATE_BOARDS, False),
    ("gate", LATE_GATE_BOARDS, False),
)


def _family(version: str, describe: str) -> tuple[Scenario, ...]:
    make = _version_one if version == "v1" else _version_two
    return tuple(
        edge(
            f"antimod-{version}-{suffix}",
            f"{describe}, {suffix} board",
            make(on),
            boards,
        )
        for suffix, boards, on in _FAMILIES
    )


ANTIMOD_SCENARIOS = _family(
    "v1", "anti-mod v1: after play, a counter probe in the program area"
) + _family(
    "v2",
    "anti-mod v2: after play, a lead-in re-read that must carry a string, "
    "then a counter probe in the middle of the disc",
)
