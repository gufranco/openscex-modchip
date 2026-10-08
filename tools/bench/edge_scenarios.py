# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""The console situations that reach what the plain scenarios never do.

The accept, reread and swap scenarios exercise a chip the way a working
console does. Instruction coverage then shows what they leave dark: in this
firmware, a console that moves to the program area right after it accepts,
a second boot onto what the first one stored, a chip moved to another board,
a WFCK carrier that stalls mid-string, a supply that sags, a console whose
clock runs off, a stored record that is damaged or at its limits, and SUBQ
frames of every kind the decoder sorts. Each scenario here is one of those
situations, played to every chip, so the field-proven chips meet it too and
the envelope still compares like with like.

Phases come from the timeline's own marks, so a scenario's judge points sit
exactly where its builder put them. A power cycle marks the swap point, and
the second boot's lead-in and program area are its second marks.

The stored records follow this firmware's layout (include/pscu/calib.h): a
magic byte 0xC6, board, string cap, trigger, frozen flag, trim offset, then a
check byte, 0x5A exclusive-ored with the six before it. Other chips ignore
the EEPROM bytes; they read no such record.
"""

from collections.abc import Callable
from dataclasses import dataclass

from tools.bench.scenarios import (
    BOOT_NS,
    CARRIER_BOARDS,
    HALF_7K3_NS,
    SECOND_NS,
    STROBES_PER_FRAME,
    TAIL_NS,
    Board,
    Phase,
    Scenario,
)
from tools.bench.timeline import (
    AUDIO_LEAD_IN,
    EDGE_NS,
    FRAME_NS,
    LEAD_IN,
    LEAD_OUT,
    PROGRAM,
    Event,
    Signal,
    Timeline,
    dry_run,
)

RECORD_MAGIC = 0xC6
RECORD_SEED = 0x5A
BOARD_CARRIER = 1
QUICK_LEAD_IN_FRAMES = 2 * 75
FULL_BURST_LEAD_IN_FRAMES = 270
QUICK_PLAY_FRAMES = 4 * 75
RESULT_PLAY_FRAMES = 17 * 75
DRIVE_STOP_NS = 3 * SECOND_NS
REPLAY_IDLE_NS = 22 * SECOND_NS
QUIET_NS = 26 * SECOND_NS
SUPPLY_LOW_MV = 2600
SUPPLY_GOOD_MV = 5000
CAL7_FACTORY = 0x7F
SILENCE = (0,) * 12
FAST_CONSOLE = 0.95
SLOW_CONSOLE = 1.05
STUCK_AFTER_BYTES = 6
STUCK_NS = 100 * 1_000_000
MS = 1_000_000
LATE_CARRIER_NS = 4_000 * MS


@dataclass(frozen=True, slots=True)
class Record:
    """A calibration record as this firmware stores it; check None means the
    correct check byte, any other value a damaged one."""

    board: int
    cap: int
    trigger: int
    frozen: int
    trim: int
    magic: int = RECORD_MAGIC
    check: int | None = None

    def raw(self) -> list[int]:
        """The record's seven EEPROM bytes."""
        body = [self.magic, self.board, self.cap, self.trigger, self.frozen]
        body = [*body, self.trim & 0xFF]
        computed = RECORD_SEED
        for byte in body:
            computed ^= byte
        return [*body, computed if self.check is None else self.check]


def _seed(timeline: Timeline, stored: Record) -> Timeline:
    for address, byte in enumerate(stored.raw()):
        timeline.set(Signal.EEPROM_BYTE, address * 256 + byte)
    return timeline


def carrier() -> Timeline:
    timeline = Timeline(strobes_per_frame=STROBES_PER_FRAME)
    return timeline.set(Signal.WFCK_HALF_NS, HALF_7K3_NS)


def disc(
    timeline: Timeline,
    first: bool,
    play: int = QUICK_PLAY_FRAMES,
    lead_in: int = QUICK_LEAD_IN_FRAMES,
) -> Timeline:
    """A disc a console accepts at once: a short lead-in, then the program
    area, as a console that moves on the moment the check passes."""
    lead, program = (
        (Phase.LEAD_IN, Phase.PROGRAM)
        if first
        else (Phase.SECOND_LEAD_IN, Phase.SECOND_PROGRAM)
    )
    timeline.mark(lead).frames(LEAD_IN, lead_in)
    return timeline.mark(program).frames(PROGRAM, play)


def _power_cycle(timeline: Timeline) -> Timeline:
    return timeline.mark(Phase.SWAP).set(Signal.POWER_CYCLE, 1).idle(BOOT_NS)


def _quick_accept() -> Timeline:
    return disc(carrier().idle(BOOT_NS), True, RESULT_PLAY_FRAMES)


def _power_cycle_twice() -> Timeline:
    first = carrier().idle(BOOT_NS)
    timeline = disc(first, True, QUICK_PLAY_FRAMES, FULL_BURST_LEAD_IN_FRAMES)
    return disc(_power_cycle(timeline), False)


def _board_change() -> Timeline:
    timeline = disc(carrier().idle(BOOT_NS), True)
    timeline.set(Signal.WFCK_HALF_NS, 0).set(Signal.WFCK, 1)
    return disc(_power_cycle(timeline).idle(REPLAY_IDLE_NS), False)


def _watchdog() -> Timeline:
    """The carrier stalls for 1 s just as the first string starts, while the
    drive keeps reading: SUBQ frames go on, only WFCK stops."""
    timeline = carrier().idle(BOOT_NS).mark(Phase.LEAD_IN).frames(LEAD_IN, 12)
    timeline.set(Signal.WFCK_HALF_NS, 0).set(Signal.WFCK, 1).frames(LEAD_IN, 75)
    timeline.set(Signal.WFCK_HALF_NS, HALF_7K3_NS).frames(LEAD_IN, QUICK_LEAD_IN_FRAMES)
    return timeline.mark(Phase.PROGRAM).frames(PROGRAM, QUICK_PLAY_FRAMES)


def _supply_hold() -> Timeline:
    timeline = carrier().set(Signal.VCC_MV, SUPPLY_GOOD_MV).idle(BOOT_NS)
    timeline.mark(Phase.LEAD_IN).frames(LEAD_IN, 13)
    timeline.set(Signal.VCC_MV, SUPPLY_LOW_MV).frames(LEAD_IN, 100)
    timeline.set(Signal.VCC_MV, SUPPLY_GOOD_MV).frames(LEAD_IN, 30)
    timeline.idle(DRIVE_STOP_NS)
    return disc(timeline, False)


def _trim(scale: float, factory: int | None = None) -> Timeline:
    start = carrier()
    if factory is not None:
        start.set(Signal.OSCCAL_FACTORY, factory)
    timeline = disc(start.idle(BOOT_NS), True, 120)
    timeline.frame_ns = int(FRAME_NS * scale)
    timeline.mark(Phase.REREAD).frames(LEAD_IN, 200)
    return timeline.frames(SILENCE, 260)


def _trim_replay() -> Timeline:
    timeline = _seed(carrier(), Record(BOARD_CARRIER, 8, 10, 0, -3)).idle(BOOT_NS)
    timeline = disc(timeline, True)
    timeline.set(Signal.OSCCAL_FACTORY, CAL7_FACTORY)
    _seed(timeline, Record(BOARD_CARRIER, 8, 10, 0, 3))
    return disc(_power_cycle(timeline), False)


def _carrier_starts_late() -> Timeline:
    """WFCK sits low for 4 s after power-on before the carrier starts, as on
    a board whose CD DSP starts after the chip does: long enough that a chip
    which waits 2 s before looking at WFCK still finds it low, so it takes the
    board for a gate. The carrier then stalls for 1 s in the first string, a
    watchdog reset on a board recorded as a gate, before the disc is read."""
    timeline = Timeline(strobes_per_frame=STROBES_PER_FRAME).set(Signal.WFCK, 0)
    timeline.idle(LATE_CARRIER_NS).set(Signal.WFCK_HALF_NS, HALF_7K3_NS)
    timeline.idle(BOOT_NS).mark(Phase.LEAD_IN).frames(LEAD_IN, 12)
    timeline.set(Signal.WFCK_HALF_NS, 0).set(Signal.WFCK, 1).frames(LEAD_IN, 75)
    timeline.set(Signal.WFCK_HALF_NS, HALF_7K3_NS).frames(LEAD_IN, QUICK_LEAD_IN_FRAMES)
    return timeline.mark(Phase.PROGRAM).frames(PROGRAM, QUICK_PLAY_FRAMES)


def _no_disc_long() -> Timeline:
    return carrier().idle(BOOT_NS).mark(Phase.LEAD_IN).idle(QUIET_NS)


def _frame_kinds() -> Timeline:
    point = (0x41, 0x00, 0x05, 0x50, 0, 0, 0, 0, 0, 0, 0, 0)
    early = (0x41, 0x00, 0x01, 0x50, 0, 0, 0, 0, 0, 0, 0, 0)
    late = (0x41, 0x00, 0x01, 0x98, 0, 0, 0, 0, 0, 0, 0, 0)
    bad_track = (0x41, 0x1A, 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0)
    audio_play = (0x01, 0x01, 0x01, 0, 0x02, 0, 0, 0, 0x02, 0, 0, 0)
    other_control = (0x02, 0x01, 0x01, 0, 0x02, 0, 0, 0, 0x02, 0, 0, 0)
    zero_lead = (0x41, 0x00, 0xA0, 0, 0, 0, 0x05, 0, 0, 0, 0, 0)
    zero_play = (0x41, 0x01, 0x01, 0, 0x02, 0, 0x05, 0, 0x02, 0, 0, 0)
    timeline = carrier().idle(BOOT_NS).mark(Phase.LEAD_IN).frames(LEAD_IN, 3)
    for _ in range(60):
        timeline.frames(AUDIO_LEAD_IN, 1).frames(LEAD_IN, 1)
    for frame in (
        point,
        early,
        bad_track,
        audio_play,
        other_control,
        zero_lead,
        zero_play,
    ):
        timeline.frames(frame, 20)
    timeline.frames(late, QUICK_LEAD_IN_FRAMES)
    return timeline.mark(Phase.PROGRAM).frames(PROGRAM, QUICK_PLAY_FRAMES)


def _stored_records() -> Timeline:
    """Seven boots onto damaged records, the last of which runs a disc on the
    defaults it falls back to, so the judged disc is one every chip meets on
    equal terms; then boots onto a probe at its upper limit, a frozen probe,
    and a probe moved past the default that misses its window."""
    damaged = [
        Record(BOARD_CARRIER, 8, 10, 0, 0, check=0),
        Record(BOARD_CARRIER, 8, 10, 0, 0, magic=0x11),
        Record(5, 8, 10, 0, 0),
        Record(BOARD_CARRIER, 30, 10, 0, 0),
        Record(BOARD_CARRIER, 8, 11, 0, 0),
        Record(BOARD_CARRIER, 8, 10, 2, 0),
        Record(BOARD_CARRIER, 8, 10, 0, 40),
    ]
    timeline = carrier()
    for stored in damaged[:-1]:
        _seed(timeline, stored).set(Signal.POWER_CYCLE, 1).idle(SECOND_NS)
    _seed(timeline, damaged[-1]).set(Signal.POWER_CYCLE, 1)
    timeline = disc(timeline.idle(BOOT_NS), True)
    _seed(timeline, Record(BOARD_CARRIER, 8, 30, 0, 0))
    timeline = disc(_power_cycle(timeline), False)
    _seed(timeline, Record(BOARD_CARRIER, 8, 14, 1, 0)).set(Signal.POWER_CYCLE, 1)
    timeline.idle(BOOT_NS).frames(LEAD_IN, QUICK_LEAD_IN_FRAMES)
    timeline.frames(PROGRAM, QUICK_PLAY_FRAMES)
    _seed(timeline, Record(BOARD_CARRIER, 8, 14, 0, 0)).set(Signal.POWER_CYCLE, 1)
    timeline.idle(BOOT_NS).frames(LEAD_IN, 12).frames(LEAD_OUT, 40)
    return timeline.frames(PROGRAM, QUICK_PLAY_FRAMES)


def _disc_gone_pending() -> Timeline:
    timeline = carrier().idle(BOOT_NS).mark(Phase.LEAD_IN).frames(LEAD_IN, 13)
    timeline.frames(LEAD_OUT, 26).idle(DRIVE_STOP_NS)
    timeline.frames(LEAD_IN, 30).idle(DRIVE_STOP_NS)
    return disc(timeline, False)


def _sqck_stuck() -> Timeline:
    timeline = carrier().idle(BOOT_NS).mark(Phase.LEAD_IN).frames(LEAD_IN, 3)
    start = timeline.now_ns
    timeline.frame(LEAD_IN[:STUCK_AFTER_BYTES])
    low = start + STUCK_AFTER_BYTES * 8 * 2 * EDGE_NS + 2 * EDGE_NS
    timeline.events.append(Event(low, Signal.SQCK, 0))
    timeline.events.append(Event(low + STUCK_NS, Signal.SQCK, 1))
    timeline.idle(SECOND_NS // 10).frames(LEAD_IN, QUICK_LEAD_IN_FRAMES)
    return timeline.mark(Phase.PROGRAM).frames(PROGRAM, QUICK_PLAY_FRAMES)


def edge(
    name: str,
    description: str,
    build: Callable[[], Timeline],
    boards: frozenset[Board] = CARRIER_BOARDS,
) -> Scenario:
    """A scenario whose phases and length come from a dry build of its
    timeline, one that keeps the marks and the length but no events; the
    runner builds it for real when it plays it. It reaches code and is
    never judged against the envelope (see Scenario)."""
    with dry_run():
        timeline = build()
    phases = {Phase(mark): at for mark, at in timeline.marks.items()}
    length = timeline.now_ns + TAIL_NS
    return Scenario(name, description, length, phases, build, boards, judged=False)


EDGE_SCENARIOS = (
    edge(
        "carrier-quick-accept",
        "a console that reads the program area right after it accepts, then plays",
        _quick_accept,
    ),
    edge(
        "carrier-power-cycle",
        "a disc accepted after a full burst, a power cycle, and a quick accept",
        _power_cycle_twice,
    ),
    edge(
        "board-change",
        "the chip moves from a carrier board to a gate board and idles 22 s",
        _board_change,
        frozenset(Board),
    ),
    edge(
        "carrier-watchdog",
        "the WFCK carrier stalls for 1 s in the middle of the first string",
        _watchdog,
    ),
    edge(
        "carrier-supply-hold",
        "the supply sags below the guard mid-burst, recovers, and the disc leaves",
        _supply_hold,
    ),
    edge(
        "carrier-trim-fast",
        "a console 5 percent fast on a part calibrated at the CAL7 edge, 0x7F",
        lambda: _trim(FAST_CONSOLE, CAL7_FACTORY),
    ),
    edge(
        "carrier-trim-slow",
        "after acceptance a console 5 percent slow rereads the lead-in, then stops",
        lambda: _trim(SLOW_CONSOLE),
    ),
    edge(
        "trim-replay",
        "boots replaying a stored trim, then one that would cross CAL7",
        _trim_replay,
    ),
    edge(
        "carrier-starts-late",
        "WFCK low for 4 s after power-on, then a carrier that stalls 1 s",
        _carrier_starts_late,
    ),
    edge(
        "no-disc-long",
        "the console stays on 26 s with no disc, past every no-disc LED code",
        _no_disc_long,
    ),
    edge(
        "subq-frame-kinds",
        "every SUBQ frame kind the decoder sorts, then a lead-in and play",
        _frame_kinds,
    ),
    edge(
        "stored-records",
        "boots onto damaged records and onto records at their limits",
        _stored_records,
    ),
    edge(
        "disc-gone-pending",
        "the disc leaves while the console has not yet resolved its check",
        _disc_gone_pending,
    ),
    edge(
        "sqck-stuck",
        "SQCK stops low for 100 ms in the middle of a lead-in frame",
        _sqck_stuck,
    ),
)
