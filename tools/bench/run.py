# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Run chips through scenarios: prepare each firmware, play, measure.

Preparing a chip means making sure its exact bytes are there: ours is built by
`make all`, a third-party image must match its manifest hash, and PsNee is
fetched at its pinned commit, its git tree id checked, and built here with
avr-g++. A chip whose firmware cannot be prepared is skipped with the reason,
so a machine without the unlicensed images still runs everything else.

Every runner call is bounded by a timeout, and the runners themselves stop
after a fixed number of instruction steps.
"""

import subprocess
from dataclasses import dataclass
from pathlib import Path

from tools.bench.chips import Chip, Simulator
from tools.bench.manifest import Artifact, Status, check, load
from tools.bench.metrics import Metrics, measure
from tools.bench.scenarios import Scenario
from tools.bench.scex import Region
from tools.bench.trace import parse

RUNNERS = {
    Simulator.AVR: "build/bench/avr_runner",
    Simulator.PIC: "build/bench/pic_runner",
}
PROGRAM_WORDS = {"p12c508": 0x200, "p12f629": 0x400}
PSNEE_TARGETS = {"attiny85": "8000000UL", "atmega328p": "16000000UL"}
PSNEE_PRELUDE = (
    "#include <avr/io.h>\n#include <avr/interrupt.h>\n"
    "#include <util/delay.h>\n#include <stdint.h>\n"
)
RUN_TIMEOUT_S = 600
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
        ["git", *args], cwd=cwd, check=True, capture_output=True, text=True
    ).stdout.strip()


def build_psnee(root: Path, artifact: Artifact, workdir: Path) -> str | None:
    """Fetch PsNee at its pinned commit and build it for every target.

    Returns None on success, else why it could not be built. The tree id is
    checked after checkout, so a rewritten history at the same commit id, or a
    local edit, is refused rather than built. The sketch is included by its
    absolute path because the compiler resolves a quoted include against the
    wrapper's own folder, which breaks any relative work directory.
    """
    source = workdir / "psnee-src"
    if not source.is_dir():
        subprocess.run(
            ["git", "clone", "--quiet", str(artifact.url), str(source)],
            check=True,
            capture_output=True,
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
                "-std=gnu++17",
                "-x",
                "c++",
                str(wrapper),
                "-o",
                str(out),
            ],
            check=True,
            capture_output=True,
        )
    return None


def prepare(root: Path, chip: Chip, workdir: Path) -> str | None:
    """Make a chip's firmware ready; return None, or why it is skipped."""
    if chip.artifact is not None:
        artifact = load(root)[chip.artifact]
        if artifact.kind == "git-source":
            if (root / chip.firmware).is_file():
                return None
            return build_psnee(root, artifact, workdir)
        result = check(root, artifact)
        return None if result.status is Status.READY else result.message
    if not (root / chip.firmware).is_file():
        return f"{chip.name}: {chip.firmware} is missing; run make all first"
    return None


def run(root: Path, chip: Chip, scenario: Scenario, workdir: Path) -> Run:
    """Play one scenario into one chip and measure what it did."""
    stem = workdir / f"{chip.name}--{scenario.name}"
    timeline = stem.with_suffix(".timeline")
    trace = stem.with_suffix(".trace")
    coverage = stem.with_suffix(".cov")
    timeline.write_text("\n".join(scenario.build().lines()) + "\n")
    subprocess.run(
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
        timeout=RUN_TIMEOUT_S,
    )
    metrics = measure(
        parse(trace.read_text().splitlines()), scenario.phases, Region.AMERICA
    )
    executed = frozenset(int(line, 16) for line in coverage.read_text().split())
    return Run(chip, scenario, metrics, executed)


def program_addresses(root: Path, chip: Chip) -> frozenset[int]:
    """Where each instruction of the image starts, the denominator of coverage.

    The addresses are in the unit the runner writes coverage in, so the two
    sets compare directly. A PIC instruction is one word, so it is every
    program word the image fills, by word address; the configuration word
    sits above program memory and is left out. An AVR image is disassembled,
    since its instructions take one or two words, and each instruction's
    first byte address is kept.
    """
    path = root / chip.firmware
    if chip.simulator is Simulator.PIC:
        limit = PROGRAM_WORDS[chip.cpu]
        words = {byte // 2 for byte in _hex_bytes(path)}
        return frozenset(word for word in words if word < limit)
    listing = subprocess.run(
        ["avr-objdump", "-d", str(path)], check=True, capture_output=True, text=True
    ).stdout
    return frozenset(
        address
        for address in map(_instruction_address, listing.splitlines())
        if address is not None
    )


def _instruction_address(line: str) -> int | None:
    """The address of an avr-objdump instruction line, else None.

    Instruction lines are an indented hex address, a colon and a tab; labels
    and section headers are not indented, so they never match.
    """
    address, colon, _ = line.partition(":\t")
    if not colon or not line.startswith(" "):
        return None
    try:
        return int(address.strip(), 16)
    except ValueError:
        return None


def _hex_bytes(path: Path) -> set[int]:
    """Byte addresses an Intel HEX file fills, honouring extended addresses."""
    addresses = set()
    base = 0
    for line in path.read_text().split():
        record = bytes.fromhex(line[1:])
        count, offset, kind = record[0], (record[1] << 8) | record[2], record[3]
        if kind == HEX_DATA:
            addresses.update(base + offset + index for index in range(count))
        elif kind == HEX_EXTENDED_LINEAR_ADDRESS:
            base = ((record[4] << 8) | record[5]) << 16
    return addresses
