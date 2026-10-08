# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""The scenarios that walk a chip with a sense line through its state machine.

Mayumi V4 and MM3 read a console signal on GP4, the SPEED line on gate boards
and the XLAT strobe on carrier boards, and also watch the lid and a reset
button. Their code is a state machine timed in windows of 130 ms to 2.8 s,
with a reset or lid exit inside every loop. These scenarios are built from
that code, read in the commented MM3 12F629 port, so each branch is taken by
a deliberate sequence of SPEED spells, reset presses and lid openings. Every
chip plays them, so the envelope still compares like with like; a chip with
no sense line, reset or lid input meets them as ordinary discs.
"""

from tools.bench.edge_scenarios import (
    MS,
    QUICK_LEAD_IN_FRAMES,
    QUICK_PLAY_FRAMES,
    carrier,
    disc,
    edge,
)
from tools.bench.scenarios import (
    BOOT_NS,
    DOUBLE_SPEED,
    LATE_GATE_BOARDS,
    SECOND_NS,
    SINGLE_SPEED,
    Phase,
)
from tools.bench.timeline import FRAME_NS, LEAD_IN, PROGRAM, Signal, Timeline

# SPEED spells, low then high in milliseconds, one on each side of every
# window Mayumi V4's gate-board path times GP4 against (130 ms, 250 ms, 750
# ms, 1.5 s, 2.75 s and 2.8 s of quiet, read from the MM3 12F629 port's
# LAB_15B..LAB_1C8), so each branch of that state machine is taken.
SPEED_SPELLS_MS = (
    (50, 300),
    (200, 300),
    (400, 1000),
    (1000, 1000),
    (2000, 3000),
    (3500, 300),
    (5000, 3000),
)
# Holding reset longer than Mayumi V4's 2 s boot check (10 passes of 200 ms)
# steps its mode; four holds walk through modes 2, 3, 4 and back to 1.
RESET_HOLD_NS = 3_000 * MS
RESET_HOLDS = 4
LID_OPEN_NS = 2 * SECOND_NS
LONG_PLAY_FRAMES = 30 * 75
RESET_TAP_MS = 300
LID_TAP_MS = 500
# Paths through the gate-board state machine of Mayumi V4 and MM3, one per
# power cycle, each a series of (action, milliseconds) after an ordinary disc.
# Read from the MM3 12F629 port: a low of 0.75 to 2.25 s then a high reaches
# LAB_15B with VAR_14 still set; a low under 130 ms there takes LAB_18D; a high
# of 2.8 s clears VAR_13; a reset tap or a lid opening inside a timed loop
# takes that loop's exit. The low spells place a tap or lid inside each loop:
# LAB_190 for its first 750 ms, LAB_1A3 to 2.25 s, LAB_1B5 to 5 s, then the
# burst of three.
GATE_PATHS = (
    (
        ("low", 1000),
        ("high", 500),
        ("low", 50),
        ("high", 3000),
        ("low", 600),
        ("high", 1000),
    ),
    (
        ("low", 1000),
        ("high", 500),
        ("low", 50),
        ("high", 3000),
        ("low", 200),
        ("high", 1000),
    ),
    (("low", 1000), ("high", 500), ("low", 200), ("high", 1000)),
    (("low", 1000), ("high", 500), ("high", 3000), ("low", 200), ("high", 1000)),
    (
        ("low", 1000),
        ("high", 500),
        ("low", 30),
        ("tap", 0),
        ("low", 300),
        ("high", 500),
    ),
    (
        ("low", 1000),
        ("high", 500),
        ("low", 30),
        ("lid", 0),
        ("low", 300),
        ("high", 500),
    ),
    (
        ("low", 1000),
        ("high", 500),
        ("low", 50),
        ("high", 3000),
        ("low", 160),
        ("tap", 0),
    ),
    (
        ("low", 1000),
        ("high", 500),
        ("low", 50),
        ("high", 3000),
        ("low", 160),
        ("lid", 0),
    ),
    (("boot_tap", 1000),),
    (("boot_tap", 1800),),
    (("boot_tap", 2100),),
    (("boot_tap", 2400),),
    (("lid", 0), ("high", 1000)),
    (("low", 300), ("high", 500), ("tap", 0), ("high", 500)),
    (("low", 100), ("tap", 0), ("low", 300), ("high", 500)),
    (("low", 100), ("lid", 0), ("low", 300), ("high", 500)),
    (("low", 1000), ("tap", 0), ("low", 300), ("high", 500)),
    (("low", 1000), ("lid", 0), ("low", 300), ("high", 500)),
    (("low", 3000), ("tap", 0), ("low", 300), ("high", 500)),
    (("low", 3000), ("lid", 0), ("low", 300), ("high", 500)),
    (("low", 5300), ("tap", 0), ("low", 300), ("high", 500)),
    (("low", 5300), ("lid", 0), ("low", 300), ("high", 500)),
    (("low", 3000), ("lid_tap", 0), ("high", 1000)),
    (
        ("hold", 0),
        ("low", 2000),
        ("high", 500),
        ("low", 300),
        ("high", 26000),
        ("low", 5300),
        ("high", 1000),
    ),
    (("boot_lid", 3000),),
)
# The same for the carrier-board path: its 34 strings, the 24 after a lid
# closes, and its endless gated phase, each met by a reset tap and a lid; a
# reset while it waits for the lid to close; and drive pauses, which stop the
# XLAT strobes, landing at different points of a gated string, where its
# stealth checks abort it.
PAUSE_STEPS = tuple(
    step for offset in range(8) for step in (("pause", 20), ("play", 37 * offset + 200))
)
CARRIER_PATHS = (
    (("play", 2000), ("tap", 0), ("play", 2000)),
    (("play", 2000), ("lid", 0), ("play", 3000)),
    (("play", 2000), ("lid_tap", 0), ("play", 2000)),
    (("play", 10000), ("tap", 0), ("play", 2000)),
    (("play", 10000), ("lid", 0), ("play", 9000), ("tap", 0), ("play", 1000)),
    (("play", 10000), ("lid", 0), ("play", 1000), ("lid", 0), ("play", 1000)),
    (("play", 10000), *PAUSE_STEPS, ("play", 1000)),
)


def _frames_ms(timeline: Timeline, frame: tuple[int, ...], ms: int) -> Timeline:
    return timeline.frames(frame, max(1, round(ms * MS / FRAME_NS)))


def _gatedisc(timeline: Timeline, mark: bool) -> Timeline:
    """A gate-board disc: SPEED low through the lead-in, high in play."""
    if mark:
        timeline.mark(Phase.LEAD_IN)
    timeline.set(Signal.SENSE, SINGLE_SPEED).frames(LEAD_IN, QUICK_LEAD_IN_FRAMES)
    if mark:
        timeline.mark(Phase.PROGRAM)
    return timeline.set(Signal.SENSE, DOUBLE_SPEED).frames(PROGRAM, QUICK_PLAY_FRAMES)


def _gate_speed_spells() -> Timeline:
    timeline = _gatedisc(Timeline().set(Signal.SENSE, SINGLE_SPEED).idle(BOOT_NS), True)
    for low, high in SPEED_SPELLS_MS:
        _frames_ms(timeline.set(Signal.SENSE, SINGLE_SPEED), PROGRAM, low)
        _frames_ms(timeline.set(Signal.SENSE, DOUBLE_SPEED), PROGRAM, high)
    timeline.set(Signal.LID, 1).idle(LID_OPEN_NS).set(Signal.LID, 0)
    timeline.set(Signal.SENSE, SINGLE_SPEED).frames(LEAD_IN, QUICK_LEAD_IN_FRAMES)
    return timeline.set(Signal.SENSE, DOUBLE_SPEED).frames(PROGRAM, LONG_PLAY_FRAMES)


def _path_step(timeline: Timeline, action: str, ms: int) -> None:
    """One step of a path: a SPEED level held while frames go on, play on a
    carrier board, a reset tap, or the lid opened and closed again."""
    if action == "low":
        _frames_ms(timeline.set(Signal.SENSE, SINGLE_SPEED), LEAD_IN, ms)
    elif action == "high":
        _frames_ms(timeline.set(Signal.SENSE, DOUBLE_SPEED), PROGRAM, ms)
    elif action == "play":
        _frames_ms(timeline, PROGRAM, ms)
    elif action == "tap":
        timeline.set(Signal.RESET, 0)
        _frames_ms(timeline, PROGRAM, RESET_TAP_MS).set(Signal.RESET, 1)
    elif action == "hold":
        _hold_reset(timeline, PROGRAM)
    elif action == "pause":
        timeline.idle(ms * MS)
    elif action == "lid_tap":
        timeline.set(Signal.LID, 1).idle(LID_TAP_MS * MS)
        timeline.set(Signal.RESET, 0).idle(RESET_TAP_MS * MS).set(Signal.RESET, 1)
        timeline.idle(LID_TAP_MS * MS).set(Signal.LID, 0)
    else:
        timeline.set(Signal.LID, 1).idle(LID_TAP_MS * MS).set(Signal.LID, 0)


def _boot_tap(timeline: Timeline, after_ms: int) -> None:
    """A reset tap after_ms into a boot, in the waits a chip with a reset
    button runs right after power-on and after detecting the board."""
    timeline.idle(after_ms * MS).set(Signal.RESET, 0).idle(RESET_TAP_MS * MS)
    timeline.set(Signal.RESET, 1).idle(BOOT_NS)


def _sense_paths(on_carrier: bool) -> Timeline:
    """Every path in GATE_PATHS or CARRIER_PATHS, each after a power cycle
    and an ordinary disc."""
    paths = CARRIER_PATHS if on_carrier else GATE_PATHS
    timeline = carrier() if on_carrier else Timeline().set(Signal.SENSE, SINGLE_SPEED)
    for index, path in enumerate(paths):
        if index:
            timeline.set(Signal.POWER_CYCLE, 1)
        if path[0][0] == "boot_tap":
            _boot_tap(timeline, path[0][1])
            continue
        if path[0][0] == "boot_lid":
            timeline.set(Signal.LID, 1).idle(path[0][1] * MS).set(Signal.LID, 0)
            timeline.idle(BOOT_NS)
            continue
        timeline.idle(BOOT_NS)
        if on_carrier:
            disc(timeline, index == 0)
        else:
            _gatedisc(timeline, index == 0)
        for action, ms in path:
            _path_step(timeline, action, ms)
    return timeline


def _hold_reset(timeline: Timeline, frame: tuple[int, ...]) -> Timeline:
    timeline.set(Signal.RESET, 0)
    _frames_ms(timeline, frame, RESET_HOLD_NS // MS)
    return timeline.set(Signal.RESET, 1)


def _reset_modes(on_carrier: bool) -> Timeline:
    """Reset held at power-on, then during the first lead-in's strings, then
    twice more, stepping a chip with a reset-button mode switch through every
    mode; each mode then meets a disc."""
    start = carrier() if on_carrier else Timeline().set(Signal.SENSE, SINGLE_SPEED)
    timeline = start.set(Signal.RESET, 0).idle(RESET_HOLD_NS).set(Signal.RESET, 1)
    timeline.idle(BOOT_NS).mark(Phase.LEAD_IN).frames(LEAD_IN, 40)
    for _ in range(RESET_HOLDS - 1):
        _hold_reset(timeline, LEAD_IN).idle(SECOND_NS)
        if on_carrier:
            disc(timeline, False)
        else:
            _gatedisc(timeline, False)
    return timeline


SENSE_SCENARIOS = (
    edge(
        "gate-speed-spells",
        "SPEED spells of every length a gate-board chip times, a lid cycle, play",
        _gate_speed_spells,
        LATE_GATE_BOARDS,
    ),
    edge(
        "gate-reset-modes",
        "reset held at power-on and three times more on a gate board, then discs",
        lambda: _reset_modes(on_carrier=False),
        LATE_GATE_BOARDS,
    ),
    edge(
        "gate-sense-paths",
        "SPEED spells, reset taps and lid openings down each gate-board path",
        lambda: _sense_paths(on_carrier=False),
        LATE_GATE_BOARDS,
    ),
    edge(
        "carrier-sense-paths",
        "reset taps and lid openings in each phase of the carrier-board path",
        lambda: _sense_paths(on_carrier=True),
    ),
    edge(
        "carrier-reset-modes",
        "reset held at power-on and three times more on a carrier board, then discs",
        lambda: _reset_modes(on_carrier=True),
    ),
)
