# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""The console's side of the wires, as a timeline of signal changes.

A runner reads one `<time_ns> <signal> <value>` change per line. SUBQ frames
match tests/sim/harness.c: twelve bytes LSB first, one frame per 1/75 s. WFCK
is not listed edge by edge; a `wfck_half_ns` change sets its half period, 0
stops it.
"""

import binascii
from collections.abc import Iterator
from contextlib import contextmanager
from contextvars import ContextVar
from dataclasses import dataclass, field
from enum import StrEnum

EDGE_NS = 14_170
# Whether timelines built now keep their events. A dry build only records
# marks and length, so only the timeline being played is held in memory.
_RECORDING: ContextVar[bool] = ContextVar("recording", default=True)
FRAME_NS = 1_000_000_000 // 75
FRAME_BYTES = 12


def with_crc(data: tuple[int, ...]) -> tuple[int, ...]:
    """Append the SUBQ CRC: CRC-16-CCITT from zero over the ten bytes, inverted,
    high byte first (psx-spx cdromformat.md, Subchannel Q)."""
    crc = binascii.crc_hqx(bytes(data), 0) ^ 0xFFFF
    return (*data, crc >> 8, crc & 0xFF)


# Bytes 0 to 9 are the fields the chips read, addresses left zero; bytes 10 and
# 11 are the CRC.
LEAD_IN = with_crc((0x41, 0x00, 0xA0, 0, 0, 0, 0, 0, 0, 0))
PROGRAM = with_crc((0x41, 0x01, 0x01, 0x00, 0x02, 0, 0, 0, 0x02, 0))
LEAD_OUT = with_crc((0x41, 0xAA, 0x01, 0, 0, 0, 0, 0, 0, 0))
AUDIO_LEAD_IN = with_crc((0x01, 0x00, 0xA0, 0, 0, 0, 0, 0, 0, 0))
VCD_LEAD_IN = with_crc((0x41, 0x00, 0xA0, 0x02, 0, 0, 0, 0, 0, 0))


class Signal(StrEnum):
    """A console wire, or the WFCK carrier control, as runners name them."""

    SQCK = "sqck"
    SUBQ = "subq"
    WFCK = "wfck"
    WFCK_HALF_NS = "wfck_half_ns"
    LID = "lid"
    RESET = "reset"
    SENSE = "sense"
    VCC_MV = "vcc_mv"
    POWER_CYCLE = "power_cycle"
    EEPROM_BYTE = "eeprom_byte"
    OSCCAL_FACTORY = "osccal_factory"


@dataclass(frozen=True, slots=True)
class Event:
    """One signal change at one simulated time."""

    time_ns: int
    signal: Signal
    value: int


@dataclass(slots=True)
class Timeline:
    """Builds a scenario's signal changes in time order.

    strobes_per_frame models the sense line Mayumi and MM3 watch as XLAT: that
    many low pulses of strobe_width_ns per frame; 0 sends none. frame_ns is the
    frame period, 1/75 s on time, off-nominal to test the oscillator trim.
    """

    strobes_per_frame: int = 0
    frame_ns: int = FRAME_NS
    strobe_width_ns: int = 4_000
    now_ns: int = 0
    events: list[Event] = field(default_factory=list)
    marks: dict[str, int] = field(default_factory=dict)
    record: bool = field(default_factory=_RECORDING.get)

    def set(self, signal: Signal, value: int) -> "Timeline":
        """Change one signal at the cursor."""
        if self.record:
            self.events.append(Event(self.now_ns, signal, value))
        return self

    def mark(self, name: str) -> "Timeline":
        """Name the cursor's time, a console phase the metrics judge by."""
        self.marks[name] = self.now_ns
        return self

    def idle(self, duration_ns: int) -> "Timeline":
        """Move the cursor without changing anything."""
        self.now_ns += duration_ns
        return self

    def frame(self, data: tuple[int, ...]) -> "Timeline":
        """Clock one SUBQ frame and wait out the rest of its 1/75 s."""
        start = self.now_ns
        if not self.record:
            self.now_ns = start + self.frame_ns
            return self
        at = start
        for byte in data:
            for bit in range(8):
                self.events.append(Event(at, Signal.SUBQ, (byte >> bit) & 1))
                self.events.append(Event(at, Signal.SQCK, 0))
                self.events.append(Event(at + EDGE_NS, Signal.SQCK, 1))
                at += 2 * EDGE_NS
        for pulse in range(self.strobes_per_frame):
            low = start + pulse * (self.frame_ns // self.strobes_per_frame)
            self.events.append(Event(low, Signal.SENSE, 0))
            self.events.append(Event(low + self.strobe_width_ns, Signal.SENSE, 1))
        self.now_ns = start + self.frame_ns
        return self

    def frames(self, data: tuple[int, ...], count: int) -> "Timeline":
        """Clock the same frame count times."""
        for _ in range(count):
            self.frame(data)
        return self

    def lines(self) -> list[str]:
        """The timeline as the runners read it, sorted by time."""
        ordered = sorted(self.events, key=lambda event: event.time_ns)
        return [f"{e.time_ns} {e.signal} {e.value}" for e in ordered]


@contextmanager
def dry_run() -> Iterator[None]:
    """Build timelines inside the block without keeping their events."""
    token = _RECORDING.set(False)
    try:
        yield
    finally:
        _RECORDING.reset(token)
