# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Read a PROFILE=trace capture: a timeline in words and a diagnosis.

The chip sends, at 115200 baud 8N1 on pin 2, a boot report, then every SUBQ
frame it captured as 24 hex digits and, between frames, an event line for each
decision it took (src/trace.c). Save what a USB serial adapter receives to a
file and run:

    python3 tools/serial_trace.py capture.txt

or pipe the terminal output in. The timeline names each frame's place on the
disc and checks its CRC; the diagnosis says what went wrong, if anything, and
what to check first.
"""

import argparse
import binascii
import re
import sys
from collections.abc import Callable, Iterable, Sequence
from dataclasses import dataclass
from pathlib import Path

FRAME = re.compile(r"[0-9A-F]{24}")
# Only these tags have a second word: RST PWR, SUP LOW, SAVE OK and the like.
EVENT = re.compile(r"@(\d+)\.(\d{3}) ([A-Z]+(?: (?:PWR|WDT|OK|LOW|NO))?)((?: \S+)*)")
FAILED_CAPTURE = bytes([0xFF] * 12)
LEAD_IN_TRACK = 0x00
LEAD_OUT_TRACK = 0xAA
TOC_POINTS = {0xA0: "first track", 0xA1: "last track", 0xA2: "lead-out start"}
# The guard below which no string goes out (include/pscu/supply.h).
SUPPLY_GUARD_MV = 2750
# Strings per opening (include/pscu/config.h, PSCU_STEALTH_STRINGS).
STRING_CAP = 16
# More than this share of frames with a bad CRC points at the wiring.
NOISY_SHARE = 0.05
REGIONS = {"JP": "Japan (NTSC-J)", "US": "America (NTSC-U/C)", "EU": "Europe (PAL)"}
BOARDS = {"W": "a WFCK carrier board", "G": "a static-gate board", "-": "no board"}
HOW = {
    "D": "detected at power-on",
    "K": "kept from the record after a watchdog reset",
    "C": "detected, and not the board recorded: the record starts over",
}
SHUT = {
    "P": "a program-area frame: the console finished the region check",
    "G": "the disc left",
    "D": "the SUBQ counter fell back below the start",
}


@dataclass(frozen=True, slots=True)
class Frame:
    """One captured SUBQ frame."""

    data: bytes

    @property
    def captured(self) -> bool:
        """False for the all-FF line a failed capture prints."""
        return self.data != FAILED_CAPTURE

    @property
    def crc_ok(self) -> bool:
        """CRC-16-CCITT from zero over bytes 0 to 9, inverted, high byte first
        (psx-spx cdromformat.md, Subchannel Q), as the chip checks it."""
        crc = binascii.crc_hqx(self.data[:10], 0) ^ 0xFFFF
        return self.data[10:] == bytes([crc >> 8, crc & 0xFF])

    @property
    def valid(self) -> bool:
        return self.captured and self.crc_ok


@dataclass(frozen=True, slots=True)
class Event:
    """One event line: when, its tag and its values."""

    seconds: int
    ms: int
    tag: str
    values: tuple[str, ...]

    @property
    def at(self) -> str:
        return f"{self.seconds}.{self.ms:03d}s"


@dataclass(frozen=True, slots=True)
class Other:
    """A line that is neither, such as noise before the chip started."""

    text: str


Line = Frame | Event | Other


def parse(text: str) -> Line:
    """One received line, classified."""
    stripped = text.strip()
    if FRAME.fullmatch(stripped):
        return Frame(bytes.fromhex(stripped))
    found = EVENT.fullmatch(stripped)
    if found:
        seconds, ms, tag, values = found.groups()
        return Event(int(seconds), int(ms), tag, tuple(values.split()))
    return Other(stripped)


def _bcd(value: int) -> str:
    return f"{value >> 4}{value & 0x0F}"


def describe_frame(frame: Frame) -> str:
    """Where a frame says the laser is, or why it says nothing."""
    if not frame.captured:
        return "no frame captured (SQCK silent, or the frame was cut short)"
    if not frame.crc_ok:
        return "frame with a bad CRC, ignored by the chip"
    data = frame.data
    kind = "data" if data[0] & 0x40 else "audio"
    if data[1] == LEAD_IN_TRACK:
        point = TOC_POINTS.get(data[2], f"track {_bcd(data[2])}")
        return f"lead-in, {kind} TOC entry: {point}"
    if data[1] == LEAD_OUT_TRACK:
        return "lead-out"
    at = ":".join(_bcd(part) for part in data[7:10])
    return f"program area, {kind} track {_bcd(data[1])}, at {at}"


def _number(value: str) -> int:
    return int(value, 16)


def _boot(values: Sequence[str]) -> str:
    region, build, *vcd = values
    filter_on = ", Video CD filter on" if vcd else ""
    return f"firmware {build} for {REGIONS.get(region, region)}{filter_on}"


def _record(values: Sequence[str]) -> str:
    board, start, state, trim = values
    probing = "frozen" if state == "F" else "still probing"
    return (
        f"record: {BOARDS[board]}, start at counter {_number(start)}, "
        f"{probing}, trim {trim}"
    )


def _osc(values: Sequence[str]) -> str:
    before, after = (_number(part) for part in values[0].split(">"))
    return f"OSCCAL {before:02X} to {after:02X} ({after - before:+d} notches)"


def _hold(values: Sequence[str]) -> str:
    counter, start, supply = values
    if int(supply.removesuffix("mV")) < SUPPLY_GUARD_MV:
        return (
            f"window held at counter {_number(counter)}: supply {supply}, "
            "too low to send"
        )
    return (
        f"window held at counter {_number(counter)}, just below the start "
        f"{_number(start)}: the opening keeps its count"
    )


def _open(values: Sequence[str]) -> str:
    counter, start = values
    return f"window open: counter {_number(counter)} reached the start {_number(start)}"


def _miss(values: Sequence[str]) -> str:
    return (
        f"window missed: the lead-in ended at counter {_number(values[0])}, before the "
        f"start {_number(values[1])}; the start moves earlier for the next disc"
    )


Describer = Callable[[Sequence[str]], str]

WORDS: dict[str, Describer] = {
    "BOOT": _boot,
    "RST PWR": lambda _: "started from power-on",
    "RST WDT": lambda _: "restarted by the watchdog: the main loop stalled",
    "BRD": lambda v: f"board: {BOARDS[v[0]]}, {HOW[v[1]]}",
    "OSC": _osc,
    "REC": _record,
    "SUP OK": lambda v: f"supply good, {v[0]}",
    "SUP LOW": lambda v: (
        f"supply low, {v[0]}: no string and no record write below 2.75 V"
    ),
    "DISC": lambda _: "a disc is being read: valid SUBQ frames again",
    "GONE": lambda _: "the disc is gone: no valid frame for 1.5 s",
    "OPEN": _open,
    "HOLD": _hold,
    "SHUT": lambda v: f"window shut: {SHUT[v[0]]}",
    "SCEX": lambda v: f"SCEx string {v[0]} of {STRING_CAP} sent on {BOARDS[v[1]]}",
    "PASS": lambda _: "accepted: the console went on to the program area",
    "FAIL": lambda _: "refused: no program area in time after the strings",
    "MISS": _miss,
    "SAVE OK": lambda _: "record written to EEPROM",
    "SAVE NO": lambda v: f"record not written, supply {v[0]}; it is tried again",
    "LOST": lambda v: f"{v[0]} events lost: too many in a row for the queue",
}


def describe_event(event: Event) -> str:
    """An event line in words. A line noise garbled after its tag, with a value
    missing or one no tag uses, is shown as unreadable instead of stopping the
    report: noise on the line is itself something the diagnosis reports."""
    say = WORDS.get(event.tag)
    raw = f"{event.tag} {' '.join(event.values)}".rstrip()
    if say is None:
        return f"unknown event {raw}"
    try:
        return say(event.values)
    except (KeyError, ValueError, IndexError):
        return f"unreadable event: {raw}"


def timeline(lines: Sequence[Line]) -> list[str]:
    """Every line in words; a run of frames that read the same is one line. A
    blank Other closes the last run."""
    out: list[str] = []
    run: list[str] = []
    for line in [*lines, Other("")]:
        text = describe_frame(line) if isinstance(line, Frame) else None
        if run and text != run[0]:
            out.append(f"  {run[0]}" + (f" (x{len(run)})" if len(run) > 1 else ""))
            run = []
        if text is not None:
            run.append(text)
        elif isinstance(line, Event):
            out.append(f"{line.at:>10} {describe_event(line)}")
        elif isinstance(line, Other) and line.text:
            out.append(f"  unreadable line: {line.text}")
    return out


@dataclass(frozen=True, slots=True)
class Tally:
    """What a capture contains, counted once for every rule."""

    frames: int
    failed: int
    bad_crc: int
    lead_in: int
    tags: dict[str, int]
    events: tuple[Event, ...]

    def count(self, tag: str) -> int:
        return self.tags.get(tag, 0)


def tally(lines: Iterable[Line]) -> Tally:
    """Count frames by kind and events by tag."""
    seen = list(lines)
    frames = [line for line in seen if isinstance(line, Frame)]
    events = tuple(line for line in seen if isinstance(line, Event))
    tags: dict[str, int] = {}
    for event in events:
        tags[event.tag] = tags.get(event.tag, 0) + 1
    return Tally(
        frames=len(frames),
        failed=sum(1 for frame in frames if not frame.captured),
        bad_crc=sum(1 for frame in frames if frame.captured and not frame.crc_ok),
        lead_in=sum(
            1 for frame in frames if frame.valid and frame.data[1] == LEAD_IN_TRACK
        ),
        tags=tags,
        events=events,
    )


def _wiring(t: Tally) -> list[str]:
    findings: list[str] = []
    if t.count("BOOT") == 0:
        findings.append(
            "No BOOT line: the capture started after power-on, or this is not a "
            "PROFILE=trace image. Start the capture, then power the console."
        )
    if t.frames == 0:
        findings.append(
            "No SUBQ frame at all: the chip never saw SQCK. Check the SQCK and SUBQ "
            "wires and the ground (indicator code 3)."
        )
    elif t.failed == t.frames:
        findings.append(
            "Every capture failed: SQCK pulses arrive but no whole frame. Check the "
            "SUBQ "
            "wire and both solder joints."
        )
    elif t.bad_crc > t.frames * NOISY_SHARE:
        findings.append(
            f"{t.bad_crc} of {t.frames} frames failed their CRC: the SUBQ or SQCK line "
            "is noisy. Shorten the wires and keep them off the drive's power lines."
        )
    return findings


def _power(t: Tally) -> list[str]:
    findings: list[str] = []
    if t.count("RST WDT"):
        findings.append(
            f"{t.count('RST WDT')} watchdog restart(s): the chip stalled, usually on a "
            "SQCK line stuck or floating. Check the SQCK wire."
        )
    if t.count("RST PWR") > 1:
        findings.append(
            f"{t.count('RST PWR')} power-on starts: the chip lost power during the "
            "capture. Check the supply tap and ground."
        )
    if t.count("SUP LOW") or t.count("SAVE NO"):
        findings.append(
            "The supply fell below 2.75 V: strings and EEPROM writes were held "
            "(indicator code 7). Take power from the console's 3.5 V point."
        )
    return findings


def _session(t: Tally) -> list[str]:
    findings: list[str] = []
    if t.frames - t.failed - t.bad_crc > 0 and not t.lead_in:
        findings.append(
            "Frames, but never the lead-in: the drive did not read the disc from its "
            "start. Clean the disc, check the laser, or restart with the lid closed."
        )
    if t.lead_in and not t.count("OPEN") and not t.count("HOLD"):
        findings.append(
            "The lead-in was read but the window never opened (indicator code 4): see "
            "the MISS lines. The start is learned earlier after each miss."
        )
    if t.count("SCEX") and not t.count("PASS"):
        boards = sorted(
            {
                event.values[1]
                for event in t.events
                if event.tag == "SCEX" and len(event.values) > 1
            }
        )
        findings.append(
            f"{t.count('SCEX')} string(s) sent on "
            + " and ".join(BOARDS.get(board, f"board {board}") for board in boards)
            + " but the console never accepted the disc. Check the DATA wire, the "
            "WFCK wire on a carrier board, and that the build's region matches the "
            "console."
        )
    if t.count("PASS"):
        findings.append(
            "The console accepted the disc. If the game still stops later, it has an "
            "anti-mod check or the console checks the region again in its boot ROM, "
            "which needs a patched BIOS."
        )
    if t.count("LOST"):
        findings.append("Some events were lost: the timeline has gaps where they were.")
    return findings


def diagnose(lines: Sequence[Line]) -> list[str]:
    """What went wrong, most basic first, or that nothing did."""
    t = tally(lines)
    findings = _wiring(t) + _power(t) + _session(t)
    return findings or ["Nothing wrong: no fault in this capture."]


def read(path: str | None) -> list[Line]:
    """The capture's lines, from a file or from standard input. Bytes that are
    not text, as a wrong baud rate gives, are replaced either way."""
    raw = sys.stdin.buffer.read() if path is None else Path(path).read_bytes()
    text = raw.decode(errors="replace")
    return [parse(raw) for raw in text.splitlines() if raw.strip()]


def main(argv: Sequence[str]) -> int:
    parser = argparse.ArgumentParser(
        prog="serial_trace", description="Explain a PROFILE=trace serial capture."
    )
    parser.add_argument(
        "capture", nargs="?", help="saved capture; standard input if absent"
    )
    parser.add_argument(
        "--summary", action="store_true", help="print only the diagnosis"
    )
    args = parser.parse_args(argv[1:])
    lines = read(args.capture)
    report = [] if args.summary else [*timeline(lines), ""]
    report += ["Diagnosis:", *(f"- {finding}" for finding in diagnose(lines))]
    print("\n".join(report))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
