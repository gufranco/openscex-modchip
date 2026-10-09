# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""The anti-modchip checks games run, as console timelines.

The SCEx string lives only in the lead-in (psx-spx, cdromdrive.md, 19h,04h).
A game that wants to know whether a modchip is fitted seeks into the program
area, where an original disc carries no string, resets the CD controller's
SCEx counters with 19h,04h, waits, and reads them back with 19h,05h; any
nonzero count, even a partial "Sxxx", means a chip is sending (psx-spx,
cdromformat.md, the anti-modchip sequence, Read). That probe is the PROBE
window here: whatever a chip drives on DATA inside it is what the game sees.

The first version (APv1, PoPoRoGue and other Japanese titles, per aprip) is
the probe alone. The second (APv2, Dino Crisis onwards, every region) first
runs ReadTOC, which resets the drive's licensed-disc status, then GetID,
which fails on an unlicensed disc unless a full string arrives during that
lead-in re-read, and only then seeks to the middle of the data track for the
probe (tonyhax docs/ap_v2.c, decompiled, and aprip readme, Read; the probe
after the seek is Concluded from the operations the decompilation lists).
So the second version needs a chip that sends in the re-read and is silent
in the probe: the REREAD and PROBE windows here.

Timing choices, none measured on a console (Unknown): the re-read lasts the
4 s the core re-read scenario uses, the seek 1 s, and the probe 2 s, the
upper end of the "wait 1-2 seconds" psx-spx gives for counter results. The
probe plays CD-DA, which runs at single speed (psx-spx, Setmode bit 7 clear),
so on a static-gate board the SPEED line Mayumi V4 and MM3 sense is low
there, as it is during the lead-in.

These scenarios are judged by the showcase properties, never against the
envelope: their point is a check the field-proven chips may fail.
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
    """Set the SPEED level a gate board's chip senses; a carrier board's
    sense line is the command strobe, which the frames already pulse."""
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
