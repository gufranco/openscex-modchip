# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Tests for the trace reader: lines as the chip sends them (src/trace.c), the
words each becomes, and the diagnosis a capture gets."""

import io
import re
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest import mock

from tools.bench.timeline import with_crc
from tools.serial_trace import (
    STRING_CAP,
    SUPPLY_GUARD_MV,
    Event,
    Frame,
    Other,
    describe_event,
    describe_frame,
    diagnose,
    main,
    parse,
    timeline,
)


def frame_line(data: tuple[int, ...]) -> str:
    return bytes(with_crc(data)).hex().upper()


LEAD_IN = frame_line((0x41, 0x00, 0xA0, 0, 0, 0, 0, 0x01, 0x20, 0))
TOC_TRACK = frame_line((0x41, 0x00, 0x05, 0, 0, 0, 0, 0x03, 0x10, 0x05))
AUDIO_LEAD_IN = frame_line((0x01, 0x00, 0xA1, 0, 0, 0, 0, 0x02, 0, 0))
PROGRAM = frame_line((0x41, 0x01, 0x01, 0, 0x02, 0x10, 0, 0x00, 0x04, 0x12))
LEAD_OUT = frame_line((0x41, 0xAA, 0x01, 0, 0, 0, 0, 0x50, 0x10, 0))
FAILED = "F" * 24
BAD_CRC = LEAD_IN[:-1] + ("0" if LEAD_IN[-1] != "0" else "1")
BOOT = [
    "@0.000 BOOT US 1A2B",
    "@0.000 RST PWR",
    "@0.000 BRD W D",
    "@0.000 OSC 50>4E",
    "@0.000 REC - 0C P -2",
    "@0.000 SUP OK 3303mV",
]


def lines(*text: str) -> list[Frame | Event | Other]:
    return [parse(line) for line in text]


class ParseTest(unittest.TestCase):
    def test_each_kind_of_line_is_told_apart(self) -> None:
        # Arrange
        text = (LEAD_IN, "@1.005 SCEX 3 W", "garbled", "@0.000 SAVE NO 2601mV")

        # Act
        parsed = lines(*text)

        # Assert
        self.assertEqual(
            parsed,
            [
                Frame(bytes(with_crc((0x41, 0x00, 0xA0, 0, 0, 0, 0, 0x01, 0x20, 0)))),
                Event(1, 5, "SCEX", ("3", "W")),
                Other("garbled"),
                Event(0, 0, "SAVE NO", ("2601mV",)),
            ],
        )


class FrameTest(unittest.TestCase):
    def test_every_place_on_the_disc_is_named(self) -> None:
        # Arrange
        frames = [LEAD_IN, TOC_TRACK, AUDIO_LEAD_IN, PROGRAM, LEAD_OUT, FAILED, BAD_CRC]

        # Act
        said = [describe_frame(Frame(bytes.fromhex(text))) for text in frames]

        # Assert
        self.assertEqual(
            said,
            [
                "lead-in, data TOC entry: first track",
                "lead-in, data TOC entry: track 05",
                "lead-in, audio TOC entry: last track",
                "program area, data track 01, at 00:04:12",
                "lead-out",
                "no frame captured (SQCK silent, or the frame was cut short)",
                "frame with a bad CRC, ignored by the chip",
            ],
        )


class EventTest(unittest.TestCase):
    def test_every_event_reads_as_a_sentence(self) -> None:
        # Arrange
        text = [
            *BOOT,
            "@0.000 BOOT JP C0DE V",
            "@0.000 BOOT XX 0000",
            "@0.000 RST WDT",
            "@0.000 BRD G K",
            "@0.000 BRD W C",
            "@0.000 REC W 0E F 3",
            "@0.000 SUP LOW 2601mV",
            "@1.000 DISC",
            "@2.000 GONE",
            "@1.460 OPEN 0A 0A",
            "@1.374 HOLD 03 0A 2601mV",
            "@1.374 HOLD 09 0A 3303mV",
            "@3.000 SHUT P 00",
            "@3.000 SHUT G 00",
            "@3.000 SHUT D 02",
            "@1.460 SCEX 1 G",
            "@4.000 PASS",
            "@4.000 FAIL",
            "@4.000 MISS 0B 0C",
            "@4.000 SAVE OK",
            "@4.000 SAVE NO 2601mV",
            "@4.000 LOST 2",
            "@4.000 ZAP 1",
            "@4.000 ZAP",
        ]

        # Act
        said = [
            describe_event(event) for event in lines(*text) if isinstance(event, Event)
        ]

        # Assert
        self.assertEqual(
            said,
            [
                "firmware 1A2B for America (NTSC-U/C)",
                "started from power-on",
                "board: a WFCK carrier board, detected at power-on",
                "OSCCAL 50 to 4E (-2 notches)",
                "record: no board, start at counter 12, still probing, trim -2",
                "supply good, 3303mV",
                "firmware C0DE for Japan (NTSC-J), Video CD filter on",
                "firmware 0000 for XX",
                "restarted by the watchdog: the main loop stalled",
                "board: a static-gate board, kept from the record after a watchdog "
                "reset",
                "board: a WFCK carrier board, detected, and not the board recorded: "
                "the "
                "record starts over",
                "record: a WFCK carrier board, start at counter 14, frozen, trim 3",
                "supply low, 2601mV: no string and no record write below 2.75 V",
                "a disc is being read: valid SUBQ frames again",
                "the disc is gone: no valid frame for 1.5 s",
                "window open: counter 10 reached the start 10",
                "window held at counter 3: supply 2601mV, too low to send",
                "window held at counter 9, just below the start 10: the opening keeps "
                "its count",
                "window shut: a program-area frame: the console finished the region "
                "check",
                "window shut: the disc left",
                "window shut: the SUBQ counter fell back below the start",
                "SCEx string 1 of 16 sent on a static-gate board",
                "accepted: the console went on to the program area",
                "refused: no program area in time after the strings",
                "window missed: the lead-in ended at counter 11, before the start 12; "
                "the start moves earlier for the next disc",
                "record written to EEPROM",
                "record not written, supply 2601mV; it is tried again",
                "2 events lost: too many in a row for the queue",
                "unknown event ZAP 1",
                "unknown event ZAP",
            ],
        )


ROOT = Path(__file__).resolve().parents[2]


def header_value(header: str, name: str) -> int:
    """The number a firmware header defines for name."""
    text = (ROOT / "include" / "pscu" / header).read_text()
    found = re.search(rf"#define {name} \(\(\w+\)(\d+)U\)", text)
    if found is None:
        raise AssertionError(f"{name} not found in {header}")
    return int(found.group(1))


class FirmwareConstantsTest(unittest.TestCase):
    def test_the_reader_uses_the_firmware_limits(self) -> None:
        # Arrange
        firmware = (
            header_value("supply.h", "PSCU_SUPPLY_LOW_MV"),
            header_value("config.h", "PSCU_STEALTH_STRINGS"),
            header_value("inject.h", "PSCU_DISC_GONE_MS"),
        )

        # Act
        gone = describe_event(Event(2, 0, "GONE", ()))

        # Assert
        self.assertEqual(
            (SUPPLY_GUARD_MV, STRING_CAP, f"{firmware[2] / 1000:g} s" in gone),
            (firmware[0], firmware[1], True),
        )


class NoisyEventTest(unittest.TestCase):
    def test_a_garbled_event_reads_as_unreadable(self) -> None:
        # Arrange
        garbled = lines(
            "@1.000 BRD X D",
            "@1.000 REC W",
            "@1.000 OSC 50",
            "@1.000 HOLD 03 0A xmV",
            "@1.000 SCEX 1",
        )

        # Act
        said = [describe_event(event) for event in garbled if isinstance(event, Event)]

        # Assert
        self.assertEqual(
            said,
            [
                "unreadable event: BRD X D",
                "unreadable event: REC W",
                "unreadable event: OSC 50",
                "unreadable event: HOLD 03 0A xmV",
                "unreadable event: SCEX 1",
            ],
        )

    def test_garbled_strings_still_get_a_diagnosis(self) -> None:
        # Arrange
        capture = lines(
            *BOOT, LEAD_IN, "@1.400 OPEN 0A 0A", "@1.460 SCEX 1", "@1.500 SCEX 2 X"
        )

        # Act
        findings = diagnose(capture)

        # Assert
        self.assertEqual(
            findings[0],
            "2 string(s) sent on board X but the console never accepted the disc. "
            "Check the DATA wire, the WFCK wire on a carrier board, and that the "
            "build's region matches the console.",
        )


class TimelineTest(unittest.TestCase):
    def test_repeated_frames_fold_and_events_keep_their_time(self) -> None:
        # Arrange
        capture = lines(
            BOOT[1], LEAD_IN, LEAD_IN, LEAD_IN, "@1.460 OPEN 0A 0A", PROGRAM, "noise"
        )

        # Act
        said = timeline(capture)

        # Assert
        self.assertEqual(
            said,
            [
                "    0.000s started from power-on",
                "  lead-in, data TOC entry: first track (x3)",
                "    1.460s window open: counter 10 reached the start 10",
                "  program area, data track 01, at 00:04:12",
                "  unreadable line: noise",
            ],
        )


class DiagnoseTest(unittest.TestCase):
    def test_a_clean_accept_reports_the_later_checks(self) -> None:
        # Arrange
        capture = lines(
            *BOOT,
            LEAD_IN,
            "@1.460 OPEN 0A 0A",
            "@1.460 SCEX 1 W",
            PROGRAM,
            "@4.000 PASS",
        )

        # Act
        findings = diagnose(capture)

        # Assert
        self.assertEqual(
            findings,
            [
                "The console accepted the disc. If the game still stops later, it has "
                "an "
                "anti-mod check or the console checks the region again in its boot "
                "ROM, "
                "which needs a patched BIOS."
            ],
        )

    def test_frames_without_a_lead_in_point_at_the_drive(self) -> None:
        # Arrange
        capture = lines(*BOOT, LEAD_OUT)

        # Act
        findings = diagnose(capture)

        # Assert
        self.assertEqual(
            findings,
            [
                "Frames, but never the lead-in: the drive did not read the disc from "
                "its start. Clean the disc, check the laser, or restart with the lid "
                "closed."
            ],
        )

    def test_nothing_wrong_is_said_when_nothing_is(self) -> None:
        # Arrange
        capture = lines(*BOOT, FAILED, LEAD_IN, "@1.460 OPEN 0A 0A")

        # Act
        findings = diagnose(capture)

        # Assert
        self.assertEqual(findings, ["Nothing wrong: no fault in this capture."])

    def test_wiring_faults_come_first(self) -> None:
        # Arrange
        silent = lines("@0.000 RST PWR")
        cut = lines(*BOOT, FAILED, FAILED)
        noisy = lines(*BOOT, *([BAD_CRC] * 2), *([LEAD_IN] * 10), "@1.000 OPEN 0A 0A")

        # Act
        found = [diagnose(silent), diagnose(cut), diagnose(noisy)[0]]

        # Assert
        self.assertEqual(
            found,
            [
                [
                    "No BOOT line: the capture started after power-on, or this is not "
                    "a "
                    "PROFILE=trace image. Start the capture, then power the console.",
                    "No SUBQ frame at all: the chip never saw SQCK. Check the SQCK and "
                    "SUBQ wires and the ground (indicator code 3).",
                ],
                [
                    "Every capture failed: SQCK pulses arrive but no whole frame. "
                    "Check "
                    "the SUBQ wire and both solder joints."
                ],
                "2 of 12 frames failed their CRC: the SUBQ or SQCK line is noisy. "
                "Shorten "
                "the wires and keep them off the drive's power lines.",
            ],
        )

    def test_power_faults_are_named(self) -> None:
        # Arrange
        capture = lines(
            *BOOT,
            "@0.000 RST WDT",
            "@0.000 RST PWR",
            "@1.000 SAVE NO 2601mV",
            LEAD_IN,
            "@1.000 HOLD 01 0A 2601mV",
        )

        # Act
        findings = diagnose(capture)

        # Assert
        self.assertEqual(
            findings,
            [
                "1 watchdog restart(s): the chip stalled, usually on a WFCK carrier "
                "that stopped mid-string. Check the WFCK wire.",
                "2 power-on starts: the chip lost power during the capture. Check the "
                "supply tap and ground.",
                "The supply fell below 2.75 V: strings and EEPROM writes were held "
                "(indicator code 7). Take power from the console's 3.5 V point.",
            ],
        )

    def test_a_low_supply_alone_is_named(self) -> None:
        # Arrange
        capture = lines(*BOOT, LEAD_IN, "@1.000 OPEN 0A 0A", "@1.000 SUP LOW 2601mV")

        # Act
        findings = diagnose(capture)

        # Assert
        self.assertEqual(
            findings,
            [
                "The supply fell below 2.75 V: strings and EEPROM writes were held "
                "(indicator code 7). Take power from the console's 3.5 V point."
            ],
        )

    def test_session_faults_say_what_to_check(self) -> None:
        # Arrange
        missed = lines(*BOOT, LEAD_IN, "@4.000 MISS 0B 0C")
        refused = lines(
            *BOOT,
            LEAD_IN,
            "@1.400 OPEN 0A 0A",
            "@1.400 SCEX 1 W",
            "@1.600 SCEX 2 G",
            "@4.000 FAIL",
            "@4.000 LOST 1",
        )

        # Act
        found = [diagnose(missed), diagnose(refused)]

        # Assert
        self.assertEqual(
            found,
            [
                [
                    "The lead-in was read but the window never opened (indicator code "
                    "4): see the MISS lines. The start is learned earlier after each "
                    "miss."
                ],
                [
                    "2 string(s) sent on a static-gate board and a WFCK carrier board "
                    "but "
                    "the console never accepted the disc. Check the DATA wire, the "
                    "WFCK "
                    "wire on a carrier board, and that the build's region matches the "
                    "console.",
                    "Some events were lost: the timeline has gaps where they were.",
                ],
            ],
        )


class MainTest(unittest.TestCase):
    def test_a_saved_capture_is_explained(self) -> None:
        # Arrange
        with tempfile.TemporaryDirectory() as work:
            path = Path(work) / "capture.txt"
            path.write_text("\n".join([*BOOT, "", LEAD_IN, "@1.460 OPEN 0A 0A"]) + "\n")
            printed = io.StringIO()

            # Act
            with redirect_stdout(printed):
                code = main(["serial_trace", str(path)])

        # Assert
        self.assertEqual(
            (code, printed.getvalue().splitlines()[-3:]),
            (0, ["", "Diagnosis:", "- Nothing wrong: no fault in this capture."]),
        )

    def test_standard_input_gives_the_summary_alone(self) -> None:
        # Arrange
        printed = io.StringIO()
        typed = io.TextIOWrapper(io.BytesIO(b"@0.000 RST PWR\n"))

        # Act
        with mock.patch("sys.stdin", typed), redirect_stdout(printed):
            code = main(["serial_trace", "--summary"])

        # Assert
        self.assertEqual(
            (code, printed.getvalue().splitlines()[0]),
            (0, "Diagnosis:"),
        )

    def test_bytes_that_are_not_text_are_replaced_not_fatal(self) -> None:
        # Arrange
        printed = io.StringIO()
        typed = io.TextIOWrapper(io.BytesIO(b"\xff\xfe@0.000 RST PWR\n"))

        # Act
        with mock.patch("sys.stdin", typed), redirect_stdout(printed):
            code = main(["serial_trace"])

        # Assert
        self.assertEqual(
            (code, printed.getvalue().splitlines()[0]),
            (0, "  unreadable line: \ufffd\ufffd@0.000 RST PWR"),
        )
