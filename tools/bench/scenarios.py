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
The sense line Mayumi V4 and MM3 read on GP4 depends on the board. On
carrier boards it is the mechacon's command strobe, XLAT, pulsed four times
per frame read here; the real cadence is Unknown. That it is XLAT there is
Concluded from the quade.co PM-41(2) diagram ("Stealth: Pin 3", near IC304
pin 44) and the psx-spx HC05 pinout (pin 43 DATA, 44 XLAT, 45 CLOK). On the
static-gate boards it is the mechacon's SPEED output, IC304 pin 27, which
switches the spindle driver between single and double speed (Read: PU-18
service manual schematic sheet 3; psx-spx, SPEED to IC722 pin 3, the motor
driver). That GP4 sits on it there is Concluded: the chips' gate-board code
reads GP4 as a slow level, keeps sending while it is low and times low and
high spells of 130 ms to 2.75 s, and older Mayumi chips sensed this "X1/X2
speed control line" (psdevwiki). SPEED is held low, single speed, from power
on through the lead-in, and goes high, double speed, when the program area
is read: the region check reads the lead-in wobble at single speed, the
owner's recollection of a slower disc at that moment, and data is read at
double speed. That polarity and the switch point are Concluded, not
measured.
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
# The PU-7 and PU-8 and the PU-18 and PU-20 play the same signals here, but
# not the same chips: Mayumi V4 is timed by the mechacon clock, which the
# PU-8 takes from its own 4.0000 MHz oscillator and the PU-18 derives as
# 4.2336 MHz from the CD DSP (Read: psx-spx HC05 pinouts), so it supports only
# the later pair. Each pair gets its own scenario, so MM3 is the evidence on
# the early boards and both chips on the later ones.
EARLY_GATE_BOARDS = frozenset({Board.PU_7, Board.PU_8})
LATE_GATE_BOARDS = frozenset({Board.PU_18, Board.PU_20})
GATE_BOARDS = EARLY_GATE_BOARDS | LATE_GATE_BOARDS
CARRIER_BOARDS = frozenset(Board) - GATE_BOARDS


@dataclass(frozen=True, slots=True)
class Scenario:
    """A named console situation: its timeline builder, phase marks, and the
    boards whose signals it plays. A judged scenario holds ours to the
    field-proven envelope; one that is not exists to reach code, such as a
    run of power cycles whose later discs all count as play, and comparing
    its counts with a field-proven chip's would measure the script, not the
    chip."""

    name: str
    description: str
    duration_ns: int
    phases: dict[Phase, int]
    build: Callable[[], Timeline]
    boards: frozenset[Board]
    judged: bool = True


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
