# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""The console's side of the wires, as a timeline of signal changes.

A runner reads one change per line, `<time_ns> <signal> <value>`, and applies
it to the chip's input pins at that simulated time. SUBQ frames follow the
simulator harness exactly (tests/sim/harness.c, clock_frame): twelve bytes,
least significant bit first, SUBQ set and then SQCK pulsed low and high with
EDGE_NS per half, and a new frame every 1/75 s, the single-speed subcode rate.
The WFCK carrier is not listed edge by edge; a `wfck_half_ns` change tells the
runner to toggle WFCK at that half period from then on (0 stops it), which
keeps a 10 s scenario to a few hundred thousand lines.
"""

from dataclasses import dataclass, field
from enum import StrEnum

EDGE_NS = 14_170
FRAME_NS = 1_000_000_000 // 75
FRAME_BYTES = 12

LEAD_IN = (0x41, 0x00, 0xA0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
PROGRAM = (0x41, 0x01, 0x01, 0x00, 0x02, 0, 0, 0, 0x02, 0, 0, 0)
LEAD_OUT = (0x41, 0xAA, 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0)
AUDIO_LEAD_IN = (0x01, 0x00, 0xA0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
VCD_LEAD_IN = (0x41, 0x00, 0xA0, 0x02, 0, 0, 0, 0, 0, 0, 0, 0)


class Signal(StrEnum):
    """A console wire, or the WFCK carrier control, as runners name them."""

    SQCK = "sqck"
    SUBQ = "subq"
    WFCK = "wfck"
    WFCK_HALF_NS = "wfck_half_ns"
    LID = "lid"
    RESET = "reset"
    XLAT = "xlat"
    VCC_MV = "vcc_mv"


@dataclass(frozen=True, slots=True)
class Event:
    """One signal change at one simulated time."""

    time_ns: int
    signal: Signal
    value: int


@dataclass(slots=True)
class Timeline:
    """Builds a scenario's signal changes in time order.

    xlat_per_frame models the mechacon's command strobe, the line Mayumi and
    MM3 watch on GP4: that many low pulses of xlat_width_ns spread over each
    frame the drive reads. Its real cadence is Unknown, so scenarios set it;
    0 leaves XLAT idle high.
    """

    xlat_per_frame: int = 0
    xlat_width_ns: int = 4_000
    now_ns: int = 0
    events: list[Event] = field(default_factory=list)

    def set(self, signal: Signal, value: int) -> "Timeline":
        """Change one signal at the cursor."""
        self.events.append(Event(self.now_ns, signal, value))
        return self

    def idle(self, duration_ns: int) -> "Timeline":
        """Move the cursor without changing anything."""
        self.now_ns += duration_ns
        return self

    def frame(self, data: tuple[int, ...]) -> "Timeline":
        """Clock one SUBQ frame and wait out the rest of its 1/75 s."""
        start = self.now_ns
        at = start
        for byte in data:
            for bit in range(8):
                self.events.append(Event(at, Signal.SUBQ, (byte >> bit) & 1))
                self.events.append(Event(at, Signal.SQCK, 0))
                self.events.append(Event(at + EDGE_NS, Signal.SQCK, 1))
                at += 2 * EDGE_NS
        for pulse in range(self.xlat_per_frame):
            low = start + pulse * (FRAME_NS // self.xlat_per_frame)
            self.events.append(Event(low, Signal.XLAT, 0))
            self.events.append(Event(low + self.xlat_width_ns, Signal.XLAT, 1))
        self.now_ns = start + FRAME_NS
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
