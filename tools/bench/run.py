# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Run chips through scenarios: prepare each firmware, play, measure.

A chip whose firmware cannot be prepared is skipped with the reason. Every
subprocess has a time limit: a runner's grows with the simulated time, every
other tool has TOOL_TIMEOUT_S.
"""

import json
import subprocess
from dataclasses import dataclass
from pathlib import Path

from tools.bench.chips import Chip, Simulator
from tools.bench.manifest import Artifact, Status, check, load
from tools.bench.metrics import Metrics, measure
from tools.bench.prepare import (
    ARDUINO_CORE,
    TOOL_TIMEOUT_S,
    assemble,
    assemble_avr,
    avr_elf,
    build_ubernee,
)
from tools.bench.reach import Entry, Line, by_line, by_table, markers
from tools.bench.scenarios import Scenario
from tools.bench.trace import parse

RUNNERS = {
    Simulator.AVR: "build/bench/avr_runner",
    Simulator.PIC: "build/bench/pic_runner",
}
PROGRAM_WORDS = {"p12c508": 0x200, "p12f629": 0x400, "p16c84": 0x400, "p16c54": 0x200}
PSNEE_TARGETS = {"attiny85": "8000000UL", "atmega328p": "16000000UL"}
PSNEE_PRELUDE = (
    "#include <avr/io.h>\n#include <avr/interrupt.h>\n"
    "#include <util/delay.h>\n#include <stdint.h>\n"
)
# A run's backstop: a fixed allowance plus a share per simulated second. The
# slowest chip, UberNee at 16 MHz with serial debug on, takes 5.1 s per
# simulated second alone on one core; the share doubles that.
RUN_TIMEOUT_S = 600
RUN_TIMEOUT_PER_SIM_S = 10
NANOSECONDS_PER_SECOND = 1_000_000_000
EXCLUSIONS = "bench/coverage-exclusions.json"
SOURCE_GLOBS = ("src/*.c", "src/*.S", "include/**/*.h")
HEX_DATA = 0
HEX_EXTENDED_LINEAR_ADDRESS = 4


@dataclass(frozen=True, slots=True)
class Run:
    """One chip's measured run of one scenario and the addresses it executed."""

    chip: Chip
    scenario: Scenario
    metrics: Metrics
    executed: frozenset[int]


def _git(args: list[str], cwd: Path) -> str:
    return subprocess.run(
        ["git", *args],
        cwd=cwd,
        check=True,
        capture_output=True,
        timeout=TOOL_TIMEOUT_S,
        text=True,
    ).stdout.strip()


def build_psnee(root: Path, artifact: Artifact, workdir: Path) -> str | None:
    """Fetch PsNee at its pinned commit and build it for every target.

    Returns None on success, else the reason. The tree id is checked after
    checkout. Built with -g so instructions map to sketch lines.
    """
    source = workdir / "psnee-src"
    if not source.is_dir():
        subprocess.run(
            ["git", "clone", "--quiet", str(artifact.url), str(source)],
            check=True,
            capture_output=True,
            timeout=TOOL_TIMEOUT_S,
        )
    _git(["checkout", "--quiet", "--force", str(artifact.commit)], source)
    tree = _git(["rev-parse", "HEAD^{tree}"], source)
    if tree != artifact.tree:
        return f"psnee: tree {tree} at {artifact.commit}, expected {artifact.tree}"
    wrapper = workdir / "psnee-main.cpp"
    sketch = (source / "PSNee" / "PSNee.ino").resolve()
    wrapper.write_text(PSNEE_PRELUDE + f'#include "{sketch}"\n')
    for mcu, clock in PSNEE_TARGETS.items():
        out = root / "build" / "bench" / f"psnee-{mcu}.elf"
        subprocess.run(
            [
                "avr-g++",
                f"-mmcu={mcu}",
                f"-DF_CPU={clock}",
                "-DSCPH_xxx1",
                "-Os",
                "-g",
                "-std=gnu++17",
                "-x",
                "c++",
                str(wrapper),
                "-o",
                str(out),
            ],
            check=True,
            capture_output=True,
            timeout=TOOL_TIMEOUT_S,
        )
    return None


def _from_source(
    root: Path, artifact: Artifact, chip: Chip, workdir: Path
) -> str | None:
    """Build a fetched source once; a sketch builds against the Arduino core."""
    if (root / chip.firmware).is_file():
        return None
    if artifact.sketch is not None:
        core = load(root)[ARDUINO_CORE]
        return build_ubernee(root, artifact, core, chip, workdir)
    return build_psnee(root, artifact, workdir)


def _from_file(root: Path, artifact: Artifact, chip: Chip, workdir: Path) -> str | None:
    """Check a file's bytes, then assemble or convert it if its kind asks."""
    result = check(root, artifact)
    if result.status is not Status.READY:
        return result.message
    if artifact.kind == "pic-source":
        return assemble(root, artifact, chip)
    if artifact.kind == "avr-source":
        return assemble_avr(root, artifact, chip, workdir)
    if artifact.kind == "avr-image":
        avr_elf(root, artifact, chip, workdir)
    return None


def prepare(root: Path, chip: Chip, workdir: Path) -> str | None:
    """Make a chip's firmware ready; return None, or why it is skipped."""
    if chip.artifact is None:
        if not (root / chip.firmware).is_file():
            return f"{chip.name}: {chip.firmware} is missing; run make all first"
        return None
    artifact = load(root)[chip.artifact]
    if artifact.kind == "git-source":
        return _from_source(root, artifact, chip, workdir)
    return _from_file(root, artifact, chip, workdir)


def run_timeout(duration_ns: int) -> int:
    """Seconds a run of this simulated length may take before it is stopped."""
    return RUN_TIMEOUT_S + RUN_TIMEOUT_PER_SIM_S * (
        duration_ns // NANOSECONDS_PER_SECOND
    )


def run(root: Path, chip: Chip, scenario: Scenario, workdir: Path) -> Run:
    """Play one scenario into one chip and measure what it did."""
    stem = workdir / f"{chip.name}--{scenario.name}"
    timeline = stem.with_suffix(".timeline")
    trace = stem.with_suffix(".trace")
    coverage = stem.with_suffix(".cov")
    lines = scenario.build().lines()
    timeline.write_text("\n".join(lines) + "\n")
    done = subprocess.run(
        [
            str(root / RUNNERS[chip.simulator]),
            str(root / chip.firmware),
            chip.cpu,
            str(chip.clock_hz),
            str(timeline),
            str(trace),
            str(coverage),
            str(scenario.duration_ns),
            chip.pins,
        ],
        check=True,
        capture_output=True,
        text=True,
        timeout=run_timeout(scenario.duration_ns),
    )
    check_events(
        f"{chip.name} on {scenario.name}", events_read(done.stdout), len(lines)
    )
    metrics = measure(
        parse(trace.read_text().splitlines()), scenario.phases, chip.region
    )
    executed = frozenset(int(line, 16) for line in coverage.read_text().split())
    return Run(chip, scenario, metrics, executed)


def events_read(stdout: str) -> int | None:
    """How many timeline events a runner says it read, from its "events N"
    line; None when it printed none."""
    for line in stdout.splitlines():
        word, _, count = line.partition(" ")
        if word == "events" and count.isdigit():
            return int(count)
    return None


def check_events(label: str, read: int | None, written: int) -> None:
    """Refuse a run whose runner read fewer or more events than were written,
    or reported none: the measurement would be of a different scenario."""
    if read != written:
        raise RuntimeError(
            f"{label}: the runner read {read} of {written} timeline events"
        )


def program_addresses(root: Path, chip: Chip) -> frozenset[int]:
    """Where each instruction of the image starts, the coverage denominator.

    A PIC instruction is one word, so every filled program word counts, without
    the configuration word. An AVR image is disassembled and each instruction's
    first byte address kept.
    """
    path = root / chip.firmware
    if chip.simulator is Simulator.PIC:
        limit = PROGRAM_WORDS[chip.cpu]
        words = {byte // 2 for byte in _hex_bytes(path)}
        return frozenset(word for word in words if word < limit)
    listing = subprocess.run(
        ["avr-objdump", "-d", str(path)],
        check=True,
        capture_output=True,
        timeout=TOOL_TIMEOUT_S,
        text=True,
    ).stdout
    return frozenset(
        address
        for address in map(_instruction_address, listing.splitlines())
        if address is not None
    )


def _instruction_address(line: str) -> int | None:
    """The address of an avr-objdump instruction line, else None."""
    address, colon, _ = line.partition(":\t")
    if not colon or not line.startswith(" "):
        return None
    try:
        return int(address.strip(), 16)
    except ValueError:
        return None


def _hex_bytes(path: Path) -> set[int]:
    """Byte addresses an Intel HEX file fills, honouring extended addresses."""
    addresses: set[int] = set()
    base = 0
    for line in path.read_text().split():
        record = bytes.fromhex(line[1:])
        count, offset, kind = record[0], (record[1] << 8) | record[2], record[3]
        if kind == HEX_DATA:
            addresses.update(base + offset + index for index in range(count))
        elif kind == HEX_EXTENDED_LINEAR_ADDRESS:
            base = ((record[4] << 8) | record[5]) << 16
    return addresses


def parse_lines(
    addresses: list[int], text: list[str], root: Path
) -> dict[int, Line | None]:
    """Pair each address with the source line avr-addr2line printed for it.

    Files in this repository keep their path from the root; other files keep
    only their name. Precompiled library code has no line.
    """
    lines: dict[int, Line | None] = {}
    for address, printed in zip(addresses, text, strict=True):
        location = printed.split(" (", 1)[0].strip()
        path, _, number = location.rpartition(":")
        source = Path(path)
        if source.is_relative_to(root):
            line = int(number) if number.isdigit() else 0
            lines[address] = (str(source.relative_to(root)), line)
        elif number.isdigit():
            lines[address] = (source.name, int(number))
        else:
            lines[address] = None
    return lines


def _source_lines(
    root: Path, chip: Chip, addresses: list[int]
) -> dict[int, Line | None]:
    if not addresses:
        return {}
    printed = subprocess.run(
        ["avr-addr2line", "-e", str(root / chip.firmware), *map(hex, addresses)],
        check=True,
        capture_output=True,
        timeout=TOOL_TIMEOUT_S,
        text=True,
    ).stdout.splitlines()
    return parse_lines(addresses, printed, root)


def load_exclusions(root: Path) -> dict[str, list[Entry]]:
    """The committed exclusion table for third-party images, by artifact id."""
    raw = json.loads((root / EXCLUSIONS).read_text())
    return {
        artifact: [
            Entry(
                line=item.get("line"),
                start=int(item["start"], 16) if "start" in item else None,
                end=int(item["end"], 16) if "end" in item else None,
                reason=item["reason"],
                file=item.get("file"),
            )
            for item in entries
        ]
        for artifact, entries in raw.items()
    }


def _markers(root: Path) -> dict[Line, str]:
    files = sorted({path for glob in SOURCE_GLOBS for path in root.glob(glob)})
    return markers({str(path.relative_to(root)): path.read_text() for path in files})


def excuse(root: Path, chip: Chip, uncovered: frozenset[int]) -> dict[int, str]:
    """The uncovered instructions no console input can reach, with reasons.

    Ours carries marker comments in its source; a third-party image uses the
    committed table under its artifact id. A PIC image matches by address only.
    """
    table = load_exclusions(root).get(chip.artifact or chip.name, [])
    ordered = sorted(uncovered)
    if chip.simulator is not Simulator.AVR:
        return by_table(ordered, {}, table)
    lines = _source_lines(root, chip, ordered)
    marked = _markers(root) if chip.artifact is None else {}
    return {**by_line(ordered, lines, marked), **by_table(ordered, lines, table)}
