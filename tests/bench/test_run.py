# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Integration tests for running chips through scenarios.

They run the real runners on ours and on a small PIC fixture, so no
third-party firmware is needed. PsNee's build path runs on a local git
repository, offline.
"""

import hashlib
import json
import os
import shutil
import subprocess
import tempfile
import unittest
from dataclasses import asdict, replace
from pathlib import Path

from tests.bench.test_prepare import CORE_FILES, SKETCH, repository
from tests.bench.test_prepare import source as fixture_source
from tools.bench.chips import Chip, Simulator, by_name
from tools.bench.manifest import ROOT, Artifact, load
from tools.bench.prepare import ARDUINO_CORE
from tools.bench.run import (
    RUN_TIMEOUT_PER_SIM_S,
    RUN_TIMEOUT_S,
    RUNNERS,
    _hex_bytes,
    _instruction_address,
    build_psnee,
    check_events,
    events_read,
    excuse,
    load_exclusions,
    parse_lines,
    prepare,
    program_addresses,
    run,
    run_timeout,
    tree_stamp,
)
from tools.bench.scenarios import by_name as scenario

# A 12F629 program that toggles GP1, DATA, forever: activity, never a string.
FIXTURE_ASM = """
    list p=12f629
    include "p12f629.inc"
    __config _INTRC_OSC_NOCLKOUT & _WDT_OFF & _MCLRE_OFF
    org 0
    bsf STATUS, RP0
    movlw b'11111101'
    movwf TRISIO
    bcf STATUS, RP0
loop
    bcf GPIO, 1
    bsf GPIO, 1
    goto loop
    end
"""

# A sketch with its own main, standing in for PsNee in the build test.
FIXTURE_SKETCH = "int main(void) {\n  for (;;) {\n  }\n}\n"


def git(cwd: Path, *args: str) -> str:
    return subprocess.run(
        [
            "git",
            "-c",
            "user.name=Test",
            "-c",
            "user.email=test@example.invalid",
            "-c",
            "commit.gpgsign=false",
            *args,
        ],
        cwd=cwd,
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()


class OursTest(unittest.TestCase):
    def test_ours_is_ready_once_built(self) -> None:
        reason = prepare(ROOT, by_name("ours"), Path(tempfile.gettempdir()))

        self.assertIsNone(reason)

    def test_ours_accepts_the_carrier_lead_in_and_stays_silent_in_play(self) -> None:
        with tempfile.TemporaryDirectory() as work:
            result = run(ROOT, by_name("ours"), scenario("carrier-accept"), Path(work))

        self.assertTrue(result.metrics.would_accept)
        self.assertEqual(result.metrics.during_play, 0)
        self.assertTrue(result.metrics.carrier)
        self.assertGreater(len(result.executed), 100)

    def test_ours_stays_silent_once_play_resumes_after_a_swap(self) -> None:
        # Arrange
        chip = by_name("ours")

        # Act
        with tempfile.TemporaryDirectory() as work:
            result = run(ROOT, chip, scenario("carrier-swap"), Path(work))

        # Assert
        self.assertGreaterEqual(result.metrics.second_window, 1)
        self.assertEqual(result.metrics.second_play, 0)
        self.assertEqual(result.metrics.during_swap, 0)

    def test_ours_stays_silent_once_play_resumes_after_a_reread(self) -> None:
        # Arrange
        chip = by_name("ours")

        # Act
        with tempfile.TemporaryDirectory() as work:
            result = run(ROOT, chip, scenario("carrier-reread"), Path(work))

        # Assert
        self.assertGreaterEqual(result.metrics.during_reread, 1)
        self.assertEqual(result.metrics.second_play, 0)

    def test_ours_program_addresses_hold_every_instruction_start(self) -> None:
        addresses = program_addresses(ROOT, by_name("ours"))

        self.assertGreater(len(addresses), 1000)
        self.assertIn(0, addresses)
        self.assertTrue(all(address % 2 == 0 for address in addresses))


class EventCountTest(unittest.TestCase):
    def test_the_runner_count_line_is_read(self) -> None:
        # Arrange
        stdout = "gpsim banner\nevents 4890138\n"

        # Act
        count = events_read(stdout)

        # Assert
        self.assertEqual(count, 4_890_138)

    def test_a_runner_that_prints_no_count_reads_as_none(self) -> None:
        # Arrange
        stdout = "cpu stopped\n"

        # Act
        count = events_read(stdout)

        # Assert
        self.assertIsNone(count)

    def test_a_short_read_is_refused(self) -> None:
        # Arrange
        label = "ours on gate-sense-paths"

        # Act
        def act() -> None:
            check_events(label, 4_000_000, 4_890_138)

        # Assert
        self.assertRaises(RuntimeError, act)

    def test_a_missing_count_is_refused(self) -> None:
        # Arrange
        label = "fixture on no-disc"

        # Act
        def act() -> None:
            check_events(label, None, 12)

        # Assert
        self.assertRaises(RuntimeError, act)

    def test_the_avr_runner_reads_a_timeline_past_four_million_events(self) -> None:
        # Arrange
        ours = by_name("ours")
        events = 4_200_000
        with tempfile.TemporaryDirectory() as work:
            folder = Path(work)
            timeline = folder / "long.timeline"
            with timeline.open("w") as out:
                out.writelines(f"{i} sense {i % 2}\n" for i in range(events))

            # Act
            done = subprocess.run(
                [
                    str(ROOT / RUNNERS[Simulator.AVR]),
                    str(ROOT / ours.firmware),
                    ours.cpu,
                    str(ours.clock_hz),
                    str(timeline),
                    str(folder / "long.trace"),
                    str(folder / "long.cov"),
                    "1000000",
                    ours.pins,
                ],
                check=True,
                capture_output=True,
                text=True,
                timeout=120,
            )

        # Assert
        self.assertEqual(events_read(done.stdout), events)


# An AVR image that calls past the end of flash, which simavr reports as a crash.
CRASH_C = "int main(void) {\n  ((void (*)(void))0x3FFF)();\n  return 0;\n}\n"


class AvrCrashTest(unittest.TestCase):
    def test_a_crashing_image_fails_the_run(self) -> None:
        # Arrange
        ours = by_name("ours")
        with tempfile.TemporaryDirectory() as work:
            folder = Path(work)
            (folder / "crash.c").write_text(CRASH_C)
            subprocess.run(
                ["avr-gcc", "-mmcu=attiny85", "-Os", "-o", "crash.elf", "crash.c"],
                cwd=folder,
                check=True,
                capture_output=True,
            )
            chip = replace(ours, name="crash", firmware=str(folder / "crash.elf"))

            # Act
            def act() -> None:
                run(ROOT, chip, scenario("no-disc"), folder)

            # Assert
            self.assertRaises(subprocess.CalledProcessError, act)


class PicRunnerTest(unittest.TestCase):
    def test_a_pic_program_drives_data_and_is_measured(self) -> None:
        with tempfile.TemporaryDirectory() as work:
            folder = Path(work)
            (folder / "fixture.asm").write_text(FIXTURE_ASM)
            subprocess.run(
                ["gpasm", "-o", "fixture.hex", "fixture.asm"],
                cwd=folder,
                check=True,
                capture_output=True,
            )
            chip = Chip(
                name="fixture",
                simulator=Simulator.PIC,
                cpu="p12f629",
                clock_hz=4_000_000,
                pins="data=6",
                firmware=str(folder / "fixture.hex"),
                artifact=None,
                field_proven=False,
                origin="test fixture",
                boards=frozenset(),
            )

            result = run(ROOT, chip, scenario("no-disc"), folder)
            addresses = program_addresses(ROOT, chip)

        self.assertEqual(result.metrics.strings, 0)
        self.assertGreater(result.metrics.data_driven_outside_ns, 0)
        self.assertEqual(addresses, frozenset(range(7)))
        self.assertLessEqual(result.executed, addresses)


class TimeoutTest(unittest.TestCase):
    def test_the_backstop_grows_with_the_simulated_time(self) -> None:
        short = run_timeout(1_000_000_000)
        long = run_timeout(300_000_000_000)

        self.assertEqual(long - short, 299 * RUN_TIMEOUT_PER_SIM_S)
        self.assertGreaterEqual(short, RUN_TIMEOUT_S)


class PrepareTest(unittest.TestCase):
    def test_missing_ours_says_to_build_first(self) -> None:
        with tempfile.TemporaryDirectory() as work:
            reason = prepare(Path(work), by_name("ours"), Path(work))

        self.assertIn("make all", str(reason))

    def test_a_missing_image_is_skipped_with_how_to_get_it(self) -> None:
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            shutil.copy(ROOT / "artifacts.manifest.json", root)

            reason = prepare(root, by_name("mayumi-v4"), root)

        self.assertIn("quade.co", str(reason))

    def test_a_present_image_with_the_right_bytes_is_ready(self) -> None:
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            target = root / "image.hex"
            target.write_bytes(b"bytes")
            entry = {
                "id": "img",
                "kind": "pic-image",
                "path": "image.hex",
                "sha256": hashlib.sha256(b"bytes").hexdigest(),
                "license": "test",
                "redistributable": True,
                "obtain": "test",
            }
            (root / "artifacts.manifest.json").write_text(
                json.dumps({"artifacts": [entry]})
            )
            chip = replace(by_name("mayumi-v4"), artifact="img", firmware="image.hex")

            reason = prepare(root, chip, root)

        self.assertIsNone(reason)

    def manifest(self, root: Path, entry: dict[str, object]) -> None:
        (root / "artifacts.manifest.json").write_text(
            json.dumps({"artifacts": [entry]})
        )

    def file_entry(
        self, root: Path, name: str, text: str, kind: str
    ) -> dict[str, object]:
        (root / name).write_text(text)
        return {
            "id": "x",
            "kind": kind,
            "path": name,
            "sha256": hashlib.sha256(text.encode()).hexdigest(),
            "license": "test",
            "redistributable": False,
            "obtain": "test",
        }

    def test_a_pic_source_is_assembled_into_the_chip_image(self) -> None:
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            (root / "build" / "bench").mkdir(parents=True)
            self.manifest(
                root, self.file_entry(root, "a.asm", FIXTURE_ASM, "pic-source")
            )
            chip = replace(by_name("old-crow-12c508"), artifact="x")

            reason = prepare(root, chip, root)
            built = (root / chip.firmware).is_file()

        self.assertIsNone(reason)
        self.assertTrue(built)

    def test_an_avr_image_is_wrapped_into_the_chip_elf(self) -> None:
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            (root / "build" / "bench").mkdir(parents=True)
            image = ":0400000000C0FFCF6E\n:00000001FF\n"
            self.manifest(root, self.file_entry(root, "m.hex", image, "avr-image"))
            chip = replace(by_name("modavr-attiny13"), artifact="x")

            reason = prepare(root, chip, root)
            built = (root / chip.firmware).is_file()

        self.assertIsNone(reason)
        self.assertTrue(built)

    def test_an_avr_source_is_assembled_into_the_chip_elf(self) -> None:
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            (root / "build" / "bench").mkdir(parents=True)
            source = '.device AT90S1200\n.include "1200def.inc"\nstart:\trjmp start\n'
            self.manifest(root, self.file_entry(root, "l.asm", source, "avr-source"))
            chip = replace(by_name("liss-at90s1200"), artifact="x")

            reason = prepare(root, chip, root)
            built = (root / chip.firmware).is_file()

        self.assertIsNone(reason)
        self.assertTrue(built)

    def test_a_source_with_the_wrong_bytes_is_not_built(self) -> None:
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            entry = self.file_entry(root, "a.asm", FIXTURE_ASM, "pic-source")
            self.manifest(root, {**entry, "sha256": "0" * 64})
            chip = replace(by_name("old-crow-12c508"), artifact="x")

            reason = prepare(root, chip, root)

        self.assertIn("SHA-256", str(reason))

    def test_an_image_from_another_tree_is_built_again(self) -> None:
        with tempfile.TemporaryDirectory() as work:
            # Arrange
            root = Path(work)
            (root / "build" / "bench").mkdir(parents=True)
            core = fixture_source(
                repository(root / "core", CORE_FILES), ARDUINO_CORE, None
            )
            sketch = fixture_source(
                repository(root / "ub", {"U.ino": SKETCH}), "ub", "U.ino"
            )
            (root / "artifacts.manifest.json").write_text(
                json.dumps({"artifacts": [asdict(core), asdict(sketch)]})
            )
            chip = replace(by_name("ubernee-atmega328p"), artifact="ub")
            (root / chip.firmware).write_bytes(b"old build")
            tree_stamp(root, chip).write_text("an older tree")

            # Act
            reason = prepare(root, chip, root)

            # Assert
            self.assertIsNone(reason)
            self.assertEqual(tree_stamp(root, chip).read_text(), sketch.tree)
            self.assertNotEqual((root / chip.firmware).read_bytes(), b"old build")

    def test_a_source_that_fails_its_tree_check_leaves_no_stamp(self) -> None:
        with tempfile.TemporaryDirectory() as work:
            # Arrange
            root = Path(work)
            (root / "build" / "bench").mkdir(parents=True)
            core = fixture_source(
                repository(root / "core", CORE_FILES), ARDUINO_CORE, None
            )
            sketch = fixture_source(
                repository(root / "ub", {"U.ino": SKETCH}), "ub", "U.ino"
            )
            wrong = replace(sketch, tree="0" * 40)
            (root / "artifacts.manifest.json").write_text(
                json.dumps({"artifacts": [asdict(core), asdict(wrong)]})
            )
            chip = replace(by_name("ubernee-atmega328p"), artifact="ub")

            # Act
            reason = prepare(root, chip, root)

            # Assert
            self.assertIsNotNone(reason)
            self.assertFalse(tree_stamp(root, chip).exists())

    def test_a_sketch_source_is_built_against_the_arduino_core(self) -> None:
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            (root / "build" / "bench").mkdir(parents=True)
            core = fixture_source(
                repository(root / "core", CORE_FILES), ARDUINO_CORE, None
            )
            sketch = fixture_source(
                repository(root / "ub", {"U.ino": SKETCH}), "ub", "U.ino"
            )
            entries = [asdict(core), asdict(sketch)]
            (root / "artifacts.manifest.json").write_text(
                json.dumps({"artifacts": entries})
            )
            chip = replace(by_name("ubernee-atmega328p"), artifact="ub")

            reason = prepare(root, chip, root)
            built = (root / chip.firmware).is_file()

        self.assertIsNone(reason)
        self.assertTrue(built)

    def test_a_built_sketch_source_is_not_built_again(self) -> None:
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            chip = replace(by_name("ubernee-atmega328p"), firmware="built.elf")
            (root / "built.elf").write_bytes(b"elf")
            shutil.copy(ROOT / "artifacts.manifest.json", root)
            pinned = load(root)[str(chip.artifact)].tree
            tree_stamp(root, chip).write_text(str(pinned))

            reason = prepare(root, chip, root)

        self.assertIsNone(reason)


class PsneeBuildTest(unittest.TestCase):
    def setUp(self) -> None:
        self._dir = tempfile.TemporaryDirectory()
        self.base = Path(self._dir.name)
        self.repo = self.base / "upstream"
        (self.repo / "PSNee").mkdir(parents=True)
        (self.repo / "PSNee" / "PSNee.ino").write_text(FIXTURE_SKETCH)
        git(self.repo, "init", "--quiet")
        git(self.repo, "add", ".")
        git(self.repo, "commit", "--quiet", "-m", "sketch")
        self.root = self.base / "root"
        (self.root / "build" / "bench").mkdir(parents=True)
        self.work = self.base / "work"
        self.work.mkdir()

    def tearDown(self) -> None:
        self._dir.cleanup()

    def artifact(self, tree: str) -> Artifact:
        return Artifact(
            id="psnee-v9",
            kind="git-source",
            license="Unlicense",
            redistributable=True,
            obtain="test",
            url=str(self.repo),
            commit=git(self.repo, "rev-parse", "HEAD"),
            tree=tree,
        )

    def test_the_pinned_tree_builds_both_targets(self) -> None:
        tree = git(self.repo, "rev-parse", "HEAD^{tree}")

        reason = build_psnee(self.root, self.artifact(tree), self.work)

        self.assertIsNone(reason)
        self.assertTrue((self.root / "build/bench/psnee-attiny85.elf").is_file())
        self.assertTrue((self.root / "build/bench/psnee-atmega328p.elf").is_file())

    def test_a_relative_workdir_still_finds_the_sketch(self) -> None:
        tree = git(self.repo, "rev-parse", "HEAD^{tree}")
        relative = Path(os.path.relpath(self.work))

        reason = build_psnee(self.root, self.artifact(tree), relative)

        self.assertIsNone(reason)

    def test_a_second_build_reuses_the_clone(self) -> None:
        tree = git(self.repo, "rev-parse", "HEAD^{tree}")
        build_psnee(self.root, self.artifact(tree), self.work)

        reason = build_psnee(self.root, self.artifact(tree), self.work)

        self.assertIsNone(reason)
        self.assertTrue((self.work / "psnee-src" / ".git").is_dir())

    def test_a_different_tree_is_refused(self) -> None:
        reason = build_psnee(self.root, self.artifact("0" * 40), self.work)

        self.assertIn("expected", str(reason))

    def test_prepare_builds_a_missing_psnee_image(self) -> None:
        tree = git(self.repo, "rev-parse", "HEAD^{tree}")
        manifest = {"artifacts": [asdict(self.artifact(tree))]}
        (self.root / "artifacts.manifest.json").write_text(json.dumps(manifest))

        first = prepare(self.root, by_name("psnee-attiny85"), self.work)
        second = prepare(self.root, by_name("psnee-attiny85"), self.work)

        self.assertIsNone(first)
        self.assertIsNone(second)


class ListingTest(unittest.TestCase):
    def test_an_instruction_line_gives_its_address(self) -> None:
        address = _instruction_address("  1a4:\t0f 92       \tpush\tr0")

        self.assertEqual(address, 0x1A4)

    def test_labels_headers_and_data_lines_give_none(self) -> None:
        lines = [
            "00000000 <__vectors>:",
            "",
            "Disassembly of section .text:",
            " zz:\tx",
        ]

        addresses = [_instruction_address(line) for line in lines]

        self.assertEqual(addresses, [None, None, None, None])


class LinesTest(unittest.TestCase):
    def test_lines_in_and_out_of_the_tree_and_code_without_lines(self) -> None:
        root = Path("/src")
        text = [
            "/src/src/inject.c:124 (discriminator 1)",
            "??:?",
            "/usr/lib/avr/lib/crt.o:?",
            "/tmp/clone/PSNee/PSNee.ino:540",
            "/usr/lib/avr/include/util/delay.h:174",
        ]

        lines = parse_lines([2, 4, 6, 8, 10], text, root)

        self.assertEqual(
            lines,
            {
                2: ("src/inject.c", 124),
                4: None,
                6: None,
                8: ("PSNee.ino", 540),
                10: ("delay.h", 174),
            },
        )

    def test_a_line_number_lost_inside_the_tree_is_kept_as_zero(self) -> None:
        lines = parse_lines([2], ["/src/src/loop.c:?"], Path("/src"))

        self.assertEqual(lines, {2: ("src/loop.c", 0)})


class ExclusionsTest(unittest.TestCase):
    def test_the_committed_table_loads_entries_by_artifact(self) -> None:
        table = load_exclusions(ROOT)

        self.assertTrue(all(isinstance(k, str) for k in table))
        self.assertTrue(any(e.file for e in table["ubernee-v142"]))

    def test_nothing_uncovered_needs_no_excuse(self) -> None:
        excused = excuse(ROOT, by_name("ours"), frozenset())

        self.assertEqual(excused, {})

    def test_a_pic_image_is_excused_by_address_from_the_table(self) -> None:
        excused = excuse(ROOT, by_name("mayumi-v4"), frozenset({0x008, 0x050}))

        self.assertEqual(list(excused), [0x008])
        self.assertIn("Mayumi04", excused[0x008])

    def test_ours_excuses_only_marked_and_runtime_instructions(self) -> None:
        chip = by_name("ours")
        addresses = program_addresses(ROOT, chip)

        excused = excuse(ROOT, chip, addresses)

        self.assertGreater(len(excused), 0)
        self.assertLess(len(excused), len(addresses) // 10)


class HexTest(unittest.TestCase):
    def test_an_extended_address_record_moves_the_base(self) -> None:
        with tempfile.TemporaryDirectory() as work:
            path = Path(work) / "x.hex"
            path.write_text(":020000040001F9\n:0100000000FF\n:00000001FF\n")

            addresses = _hex_bytes(path)

        self.assertEqual(addresses, {0x10000})
