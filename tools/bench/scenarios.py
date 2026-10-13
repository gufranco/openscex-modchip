# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""The console situations every chip is put through.

A scenario builds a timeline and marks the phases the metrics judge: lead-in,
program area, and where present a reread or a swap. The console does not react
to the chip; the metrics ask whether a valid string came before the program
area.

The boot idle is 2.5 s, since Mayumi V4 waits about 2 s before its first string
(bench measurement). The 8 s lead-in stays under the 20 s LED code 4 budget.
WFCK runs at 7.3 kHz single speed and 14.6 kHz double speed (PsNee).

The sense line Mayumi V4 and MM3 read on GP4 is XLAT on carrier boards (quade.co
PM-41(2) diagram; psx-spx HC05 pinout, pin 44), pulsed four times per frame,
real cadence unknown. On gate boards it is SPEED, IC304 pin 27 (PU-18 service
manual sheet 3; psx-spx), low through the lead-in and high in the program area;
that polarity is unmeasured.
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
STROBES_PER_FRAME = 4
SINGLE_SPEED = 0
DOUBLE_SPEED = 1
TAIL_NS = SECOND_NS


class Phase(StrEnum):
    """A console phase mark, in nanoseconds from power-on."""

    LEAD_IN = "lead_in"
    PROGRAM = "program"
    REREAD = "reread"
    SWAP = "swap"
    SECOND_LEAD_IN = "second_lead_in"
    SECOND_PROGRAM = "second_program"
    PROBE = "probe"
    AFTER_PROBE = "after_probe"


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


# Which boards a scenario's signals stand for: PU-7 to PU-20 hold WFCK static,
# PU-22 and later run it as a carrier (PsNee V9.0 PSNee.ino:372-406). Mayumi V4
# needs the PU-18's 4.2336 MHz mechacon clock, not the PU-8's 4.0000 MHz
# (psx-spx HC05 pinouts), so the early and late gate boards are split.
EARLY_GATE_BOARDS = frozenset({Board.PU_7, Board.PU_8})
LATE_GATE_BOARDS = frozenset({Board.PU_18, Board.PU_20})
GATE_BOARDS = EARLY_GATE_BOARDS | LATE_GATE_BOARDS
CARRIER_BOARDS = frozenset(Board) - GATE_BOARDS


@dataclass(frozen=True, slots=True)
class Scenario:
    """A named console situation: its timeline builder, phase marks, and the
    boards it plays. Only a judged scenario holds ours to the envelope; the
    others exist to reach code."""

    name: str
    description: str
    duration_ns: int
    phases: dict[Phase, int]
    build: Callable[[], Timeline]
    boards: frozenset[Board]
    judged: bool = True
    drives: str | None = None


def _boot(half_ns: int) -> Timeline:
    timeline = Timeline(strobes_per_frame=STROBES_PER_FRAME)
    timeline.set(Signal.WFCK_HALF_NS, half_ns)
    return timeline.idle(BOOT_NS)


def _gate_accept() -> Timeline:
    timeline = Timeline().set(Signal.SENSE, SINGLE_SPEED).idle(BOOT_NS)
    timeline.frames(LEAD_IN, LEAD_IN_FRAMES).set(Signal.SENSE, DOUBLE_SPEED)
    return timeline.frames(PROGRAM, PLAY_FRAMES)


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
        "gate-accept-early",
        "PU-7 or PU-8 board, static gate: lead-in, then play",
        _PLAY_END + TAIL_NS,
        _ACCEPT_PHASES,
        _gate_accept,
        EARLY_GATE_BOARDS,
    ),
    Scenario(
        "gate-accept",
        "PU-18 or PU-20 board, static gate: lead-in, then play",
        _PLAY_END + TAIL_NS,
        _ACCEPT_PHASES,
        _gate_accept,
        LATE_GATE_BOARDS,
    ),
    Scenario(
        "carrier-reread",
        "anti-mod check: the lead-in is read again during play",
        _REREAD_END + PLAY_FRAMES * FRAME_NS + TAIL_NS,
        {**_ACCEPT_PHASES, Phase.REREAD: _PLAY_END, Phase.SECOND_PROGRAM: _REREAD_END},
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
