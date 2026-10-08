# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""The console situations every chip is put through.

A scenario builds a timeline and marks the console phases the metrics judge:
when the lead-in read starts, when the console reaches the program area (the
moment a real console would have accepted a string), and, where the scenario
has one, an anti-mod lead-in reread or a disc swap. The console here does not
react to the chip: the program area arrives at its mark whatever the chip did,
and the metrics then ask whether a valid string arrived before it.

Timing choices: the boot idle is 2.5 s because Mayumi V4 waits about 2 s
before its first string (measured in the bench, 2026-10-07) and the firmware
here detects its board within 0.4 s. A lead-in of 8 s covers the slowest
first string seen while staying under the 20 s region-check budget the LED
code 4 assumes. WFCK runs at 7.3 kHz, the carrier the simulator harness and
PsNee use for a single-speed read, and 14.6 kHz for a double-speed read.
XLAT, Mayumi's sense line, pulses four times per frame read; its real cadence
is Unknown. That GP4 senses the mechacon's command port is Concluded from the
quade.co PM-41(2) diagram ("Stealth: Pin 3", near IC304 pin 44) and the
psx-spx HC05 pinout (pin 43 DATA, 44 XLAT, 45 CLOK).
"""

from collections.abc import Callable
from dataclasses import dataclass
from enum import StrEnum

from tools.bench.timeline import (
    AUDIO_LEAD_IN,
    FRAME_NS,
    LEAD_IN,
    PROGRAM,
    Signal,
    Timeline,
)

SECOND_NS = 1_000_000_000
BOOT_NS = 2 * SECOND_NS + SECOND_NS // 2
LEAD_IN_FRAMES = 8 * 75
PLAY_FRAMES = 10 * 75
REREAD_FRAMES = 4 * 75
SWAP_SILENCE_NS = 2 * SECOND_NS
HALF_7K3_NS = SECOND_NS // (2 * 7_300)
HALF_14K6_NS = SECOND_NS // (2 * 14_600)
XLAT_PER_FRAME = 4
TAIL_NS = SECOND_NS


class Phase(StrEnum):
    """A console phase mark, in nanoseconds from power-on."""

    LEAD_IN = "lead_in"
    PROGRAM = "program"
    REREAD = "reread"
    SWAP = "swap"
    SECOND_LEAD_IN = "second_lead_in"
    SECOND_PROGRAM = "second_program"


class Board(StrEnum):
    """A console mainboard family, as the chips' own guides name them."""

    PU_7 = "PU-7"
    PU_8 = "PU-8"
    PU_18 = "PU-18"
    PU_20 = "PU-20"
    PU_22 = "PU-22"
    PU_23 = "PU-23"
    PM_41 = "PM-41"
    PM_41_2 = "PM-41(2)"


# Which boards a scenario's signals stand for. The PU-7 to PU-20 hold WFCK
# static, the gate the chip pulls low; the PU-22 and later run it as a
# carrier the chip mirrors (Read: PsNee V9.0 PSNee.ino:372-406, BoardDetection).
# The bench cannot tell a PU-7 from a PU-20 by its signals, so the gate
# scenario stands for all four, and a chip that misses any of them is not
# evidence for it.
GATE_BOARDS = frozenset({Board.PU_7, Board.PU_8, Board.PU_18, Board.PU_20})
CARRIER_BOARDS = frozenset(Board) - GATE_BOARDS


@dataclass(frozen=True, slots=True)
class Scenario:
    """A named console situation: its timeline builder, phase marks, and the
    boards whose signals it plays."""

    name: str
    description: str
    duration_ns: int
    phases: dict[Phase, int]
    build: Callable[[], Timeline]
    boards: frozenset[Board]


def _boot(half_ns: int) -> Timeline:
    timeline = Timeline(xlat_per_frame=XLAT_PER_FRAME)
    if half_ns:
        timeline.set(Signal.WFCK_HALF_NS, half_ns)
    return timeline.idle(BOOT_NS)


def _accept(half_ns: int) -> Timeline:
    return _boot(half_ns).frames(LEAD_IN, LEAD_IN_FRAMES).frames(PROGRAM, PLAY_FRAMES)


def _reread() -> Timeline:
    timeline = _accept(HALF_7K3_NS)
    return timeline.frames(LEAD_IN, REREAD_FRAMES).frames(PROGRAM, PLAY_FRAMES)


def _swap() -> Timeline:
    timeline = _accept(HALF_7K3_NS)
    timeline.set(Signal.LID, 1).idle(SWAP_SILENCE_NS).set(Signal.LID, 0)
    return timeline.frames(LEAD_IN, LEAD_IN_FRAMES).frames(PROGRAM, PLAY_FRAMES)


def _no_disc() -> Timeline:
    return _boot(HALF_7K3_NS).idle(LEAD_IN_FRAMES * FRAME_NS)


def _audio() -> Timeline:
    return _boot(HALF_7K3_NS).frames(AUDIO_LEAD_IN, LEAD_IN_FRAMES)


_LEAD_IN_AT = BOOT_NS
_PROGRAM_AT = BOOT_NS + LEAD_IN_FRAMES * FRAME_NS
_PLAY_END = _PROGRAM_AT + PLAY_FRAMES * FRAME_NS
_REREAD_END = _PLAY_END + REREAD_FRAMES * FRAME_NS
_SECOND_LEAD_IN = _PLAY_END + SWAP_SILENCE_NS
_SECOND_PROGRAM = _SECOND_LEAD_IN + LEAD_IN_FRAMES * FRAME_NS

_ACCEPT_PHASES = {Phase.LEAD_IN: _LEAD_IN_AT, Phase.PROGRAM: _PROGRAM_AT}

SCENARIOS = (
    Scenario(
        "carrier-accept",
        "PU-22 and later board, 7.3 kHz carrier: lead-in, then play",
        _PLAY_END + TAIL_NS,
        _ACCEPT_PHASES,
        lambda: _accept(HALF_7K3_NS),
        CARRIER_BOARDS,
    ),
    Scenario(
        "carrier-double-speed",
        "PU-22 and later board read at double speed, 14.6 kHz carrier",
        _PLAY_END + TAIL_NS,
        _ACCEPT_PHASES,
        lambda: _accept(HALF_14K6_NS),
        CARRIER_BOARDS,
    ),
    Scenario(
        "gate-accept",
        "PU-7 to PU-20 board, static gate: lead-in, then play",
        _PLAY_END + TAIL_NS,
        _ACCEPT_PHASES,
        lambda: _accept(0),
        GATE_BOARDS,
    ),
    Scenario(
        "carrier-reread",
        "anti-mod check: the lead-in is read again during play",
        _REREAD_END + PLAY_FRAMES * FRAME_NS + TAIL_NS,
        {**_ACCEPT_PHASES, Phase.REREAD: _PLAY_END},
        _reread,
        CARRIER_BOARDS,
    ),
    Scenario(
        "carrier-swap",
        "multi-disc swap: lid open and drive stopped for 2 s, new lead-in",
        _SECOND_PROGRAM + PLAY_FRAMES * FRAME_NS + TAIL_NS,
        {
            **_ACCEPT_PHASES,
            Phase.SWAP: _PLAY_END,
            Phase.SECOND_LEAD_IN: _SECOND_LEAD_IN,
            Phase.SECOND_PROGRAM: _SECOND_PROGRAM,
        },
        _swap,
        CARRIER_BOARDS,
    ),
    Scenario(
        "no-disc",
        "console on with the lid closed and no disc",
        _PROGRAM_AT + TAIL_NS,
        {Phase.LEAD_IN: _LEAD_IN_AT},
        _no_disc,
        CARRIER_BOARDS,
    ),
    Scenario(
        "audio-cd",
        "audio CD: lead-in frames that never ask for a region string",
        _PROGRAM_AT + TAIL_NS,
        {Phase.LEAD_IN: _LEAD_IN_AT},
        _audio,
        CARRIER_BOARDS,
    ),
)


def by_name(name: str) -> Scenario:
    """Find a scenario by name; an unknown name raises KeyError."""
    for scenario in SCENARIOS:
        if scenario.name == name:
            return scenario
    raise KeyError(name)
