# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Console situations the plain scenarios never reach.

Program area right after acceptance, a second boot onto a stored record, a
chip moved to another board, a stalled carrier, a sagging supply, an off-rate
clock, damaged or limit records, and every SUBQ frame kind. Each is played to
every chip. Records follow include/pscu/calib.h; other chips ignore EEPROM.
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
    with_crc,
)

RECORD_MAGIC = 0xC8
RECORD_SEED = 0x5A
BOARD_CARRIER = 1
QUICK_LEAD_IN_FRAMES = 2 * 75
FULL_BURST_LEAD_IN_FRAMES = 270
QUICK_PLAY_FRAMES = 4 * 75
# Frames the chip waits for the program area before resolving a session as
# refused (include/pscu/run.h, PSCU_CONFIRM_FRAMES).
REFUSED_LEAD_OUT_FRAMES = 90
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
# At 70 percent of the period a frame spans about 4.6 Timer1 ticks, under the
# trim's 5-tick floor (run.c PSCU_TRIM_LOW), so OSCCAL must not move.
OFF_WINDOW_CONSOLE = 0.7
# The served re-read burst spans about 300 frames; 700 leave the trim the 64
# consecutive samples a verdict needs.
TRIM_REREAD_FRAMES = 700
# The debug tick shows only once the result has played, 9 s in the debug
# profile (include/pscu/indicator.h): 10 s at nominal rate, then 4 s slow.
RESULT_SHOWN_FRAMES = 10 * 75
SLOW_PLAY_FRAMES = 4 * 75
STUCK_AFTER_BYTES = 6
STUCK_NS = 100 * 1_000_000
MS = 1_000_000
LATE_CARRIER_NS = 4_000 * MS


@dataclass(frozen=True, slots=True)
class Record:
    """A calibration record; check None means the correct check byte."""

    board: int
    trigger: int
    frozen: int
    trim: int
    magic: int = RECORD_MAGIC
    check: int | None = None

    def raw(self) -> list[int]:
        """The record's six EEPROM bytes."""
        body = [self.magic, self.board, self.trigger, self.frozen]
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
    """A disc accepted at once: a short lead-in, then the program area."""
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
    """WFCK stalls for 1 s as the first string starts; SUBQ frames go on."""
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


# Lead-in frames with a bad CRC: both CRC bytes zeroed, as UberNee V1.42
# rejects, and only the low byte wrong.
BAD_CRC_ZEROED = (*LEAD_IN[:10], 0, 0)
BAD_CRC_LOW = (*LEAD_IN[:11], LEAD_IN[11] ^ 1)


# An audio lead-in frame with the digital-copy bit, control 0x21: framed but
# neither plain audio nor data, so it must not hold the counter up
# (src/subq.c, pscu_subq_tracking_hit).
AUDIO_COPY_LEAD_IN = with_crc((0x21, 0x00, 0xA0, 0, 0, 0, 0, 0, 0, 0))


def _supply_low_lead_in() -> Timeline:
    """The supply stays below the guard through the climb to the start point,
    then recovers while the lead-in goes on."""
    timeline = carrier().set(Signal.VCC_MV, SUPPLY_GOOD_MV).idle(BOOT_NS)
    timeline.set(Signal.VCC_MV, SUPPLY_LOW_MV).mark(Phase.LEAD_IN).frames(LEAD_IN, 40)
    timeline.set(Signal.VCC_MV, SUPPLY_GOOD_MV).frames(LEAD_IN, 30)
    timeline.idle(DRIVE_STOP_NS)
    return disc(timeline, False)


def _bad_crc() -> Timeline:
    """A lead-in of corrupted frames, which must never arm a string, then the
    same lead-in intact and a quick accept."""
    timeline = carrier().idle(BOOT_NS).mark(Phase.LEAD_IN)
    timeline.frames(BAD_CRC_ZEROED, 30).frames(BAD_CRC_LOW, 30)
    timeline.frames(LEAD_IN, QUICK_LEAD_IN_FRAMES)
    return timeline.mark(Phase.PROGRAM).frames(PROGRAM, QUICK_PLAY_FRAMES)


def _trim(scale: float, factory: int | None = None) -> Timeline:
    start = carrier()
    if factory is not None:
        start.set(Signal.OSCCAL_FACTORY, factory)
    timeline = disc(start.idle(BOOT_NS), True, 120)
    timeline.frame_ns = int(FRAME_NS * scale)
    timeline.mark(Phase.REREAD).frames(LEAD_IN, TRIM_REREAD_FRAMES)
    return timeline.frames(SILENCE, 260)


def _trim_in_play() -> Timeline:
    timeline = disc(carrier().idle(BOOT_NS), True, RESULT_SHOWN_FRAMES)
    timeline.frame_ns = int(FRAME_NS * SLOW_CONSOLE)
    return timeline.frames(PROGRAM, SLOW_PLAY_FRAMES)


def _trim_replay() -> Timeline:
    timeline = _seed(carrier(), Record(BOARD_CARRIER, 10, 0, -3)).idle(BOOT_NS)
    timeline = disc(timeline, True)
    timeline.set(Signal.OSCCAL_FACTORY, CAL7_FACTORY)
    _seed(timeline, Record(BOARD_CARRIER, 10, 0, 3))
    return disc(_power_cycle(timeline), False)


def _carrier_starts_late() -> Timeline:
    """WFCK stays low for 4 s after power-on, so the chip takes the board for
    a gate; the carrier then stalls for 1 s in the first string."""
    timeline = Timeline(strobes_per_frame=STROBES_PER_FRAME).set(Signal.WFCK, 0)
    timeline.idle(LATE_CARRIER_NS).set(Signal.WFCK_HALF_NS, HALF_7K3_NS)
    timeline.idle(BOOT_NS).mark(Phase.LEAD_IN).frames(LEAD_IN, 12)
    timeline.set(Signal.WFCK_HALF_NS, 0).set(Signal.WFCK, 1).frames(LEAD_IN, 75)
    timeline.set(Signal.WFCK_HALF_NS, HALF_7K3_NS).frames(LEAD_IN, QUICK_LEAD_IN_FRAMES)
    return timeline.mark(Phase.PROGRAM).frames(PROGRAM, QUICK_PLAY_FRAMES)


def _no_disc_long() -> Timeline:
    return carrier().idle(BOOT_NS).mark(Phase.LEAD_IN).idle(QUIET_NS)


def _frame_kinds() -> Timeline:
    point = with_crc((0x41, 0x00, 0x05, 0x50, 0, 0, 0, 0, 0, 0))
    early = with_crc((0x41, 0x00, 0x01, 0x50, 0, 0, 0, 0, 0, 0))
    late = with_crc((0x41, 0x00, 0x01, 0x98, 0, 0, 0, 0, 0, 0))
    bad_track = with_crc((0x41, 0x1A, 0x01, 0, 0, 0, 0, 0, 0, 0))
    audio_play = with_crc((0x01, 0x01, 0x01, 0, 0x02, 0, 0, 0, 0x02, 0))
    other_control = with_crc((0x02, 0x01, 0x01, 0, 0x02, 0, 0, 0, 0x02, 0))
    zero_lead = with_crc((0x41, 0x00, 0xA0, 0, 0, 0, 0x05, 0, 0, 0))
    zero_play = with_crc((0x41, 0x01, 0x01, 0, 0x02, 0, 0x05, 0, 0x02, 0))
    # Control 0x61 is a data track with the digital-copy bit; an audio track's
    # POINT 01 entry is the third lead-in form UberNee checks (_hysteresis).
    copy_lead = with_crc((0x61, 0x00, 0xA0, 0, 0, 0, 0, 0, 0, 0))
    audio_point = with_crc((0x01, 0x00, 0x01, 0x50, 0, 0, 0, 0, 0, 0))
    timeline = carrier().idle(BOOT_NS).mark(Phase.LEAD_IN).frames(early, 2)
    timeline.frames(LEAD_IN, 3)
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
        copy_lead,
        AUDIO_COPY_LEAD_IN,
        audio_point,
    ):
        timeline.frames(frame, 20)
    timeline.frames(late, QUICK_LEAD_IN_FRAMES)
    return timeline.mark(Phase.PROGRAM).frames(PROGRAM, QUICK_PLAY_FRAMES)


def _stored_records() -> Timeline:
    """Boots onto seven damaged records, then onto a probe at its upper limit,
    a frozen probe, and a probe past the default that misses its window."""
    damaged = [
        Record(BOARD_CARRIER, 10, 0, 0, check=0),
        Record(BOARD_CARRIER, 10, 0, 0, magic=0x11),
        Record(5, 10, 0, 0),
        Record(BOARD_CARRIER, 8, 0, 0),
        Record(BOARD_CARRIER, 11, 0, 0),
        Record(BOARD_CARRIER, 10, 2, 0),
        Record(BOARD_CARRIER, 10, 0, 40),
    ]
    timeline = carrier()
    for stored in damaged[:-1]:
        _seed(timeline, stored).set(Signal.POWER_CYCLE, 1).idle(SECOND_NS)
    _seed(timeline, damaged[-1]).set(Signal.POWER_CYCLE, 1)
    timeline = disc(timeline.idle(BOOT_NS), True)
    _seed(timeline, Record(BOARD_CARRIER, 30, 0, 0))
    timeline = disc(_power_cycle(timeline), False)
    _seed(timeline, Record(BOARD_CARRIER, 14, 1, 0)).set(Signal.POWER_CYCLE, 1)
    timeline.idle(BOOT_NS).frames(LEAD_IN, QUICK_LEAD_IN_FRAMES)
    timeline.frames(PROGRAM, QUICK_PLAY_FRAMES)
    _seed(timeline, Record(BOARD_CARRIER, 14, 0, 0)).set(Signal.POWER_CYCLE, 1)
    timeline.idle(BOOT_NS).frames(LEAD_IN, 12).frames(LEAD_OUT, 40)
    return timeline.frames(PROGRAM, QUICK_PLAY_FRAMES)


def _disc_gone_pending() -> Timeline:
    timeline = carrier().idle(BOOT_NS).mark(Phase.LEAD_IN).frames(LEAD_IN, 13)
    timeline.frames(LEAD_OUT, 26).idle(DRIVE_STOP_NS)
    timeline.frames(LEAD_IN, 30).idle(DRIVE_STOP_NS)
    return disc(timeline, False)


def _refused() -> Timeline:
    timeline = carrier().idle(BOOT_NS).mark(Phase.LEAD_IN).frames(LEAD_IN, 13)
    return timeline.frames(LEAD_OUT, REFUSED_LEAD_OUT_FRAMES).idle(SECOND_NS)


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
    drives: str | None = None,
) -> Scenario:
    """A scenario whose phases and length come from a dry build of its
    timeline; it reaches code and is never judged against the envelope."""
    with dry_run():
        timeline = build()
    phases = {Phase(mark): at for mark, at in timeline.marks.items()}
    length = timeline.now_ns + TAIL_NS
    return Scenario(
        name, description, length, phases, build, boards, judged=False, drives=drives
    )


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
        "carrier-supply-low-lead-in",
        "the supply sits below the guard through the climb to the start, then recovers",
        _supply_low_lead_in,
    ),
    edge(
        "carrier-bad-crc",
        "60 lead-in frames failing their CRC, then the intact lead-in and an accept",
        _bad_crc,
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
        "carrier-trim-cal7",
        "a console 5 percent slow on a part calibrated at 0x7F: the step up "
        "would cross CAL7 and is refused",
        lambda: _trim(SLOW_CONSOLE, CAL7_FACTORY),
    ),
    edge(
        "carrier-trim-in-play",
        "long after acceptance, with the result already shown, the console "
        "runs 5 percent slow during play",
        _trim_in_play,
    ),
    edge(
        "carrier-trim-off-window",
        "a console whose frames come at 70 percent of the period: every trim "
        "sample is refused",
        lambda: _trim(OFF_WINDOW_CONSOLE),
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
        "carrier-refused",
        "a disc whose check never passes: one string on the default start, "
        "then lead-out frames until the chip resolves a refusal",
        _refused,
    ),
    edge(
        "sqck-stuck",
        "SQCK stops low for 100 ms in the middle of a lead-in frame",
        _sqck_stuck,
    ),
)
