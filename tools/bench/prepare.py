# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Third-party firmware the bench builds from what its authors published.

Some chips exist only as source, some only as an image in a form a runner
cannot load, and one needs a framework to build. Each is turned into the
exact file its runner reads, under build/bench, from inputs whose bytes the
manifest pins:

- a PIC assembly source is assembled with gpasm. Old Crow's 1997 source was
  written for MPASM and names its oscillator fuse _INT_OSC, which gputils'
  p12c508.inc calls _IntRC_OSC; the manifest gives that name as a define, so
  the published file is assembled unedited;
- an AVR Intel HEX image becomes an ELF, the only form simavr loads. modavr's
  image starts with an extended segment record that avr-objcopy refuses, so
  the records are laid out here into a flat image first, gaps erased to 0xFF
  as an unprogrammed flash reads, and that image is wrapped;
- UberNee is an Arduino sketch. Both it and the Arduino AVR core are fetched
  at pinned commits, their git tree ids checked, and built as the Arduino IDE
  would for an ATmega328P at 16 MHz. Two configuration lines are changed in a
  copy, as its author asks a builder to: the region, to the USA string every
  chip here sends, and the LGT8F328P switch, off, because simavr models the
  ATmega328P the sketch also supports, not the LGT8F328P. UBERNEE_EDITS lists
  both, and a sketch missing either line is refused rather than built.
"""

import subprocess
from pathlib import Path

from tools.bench.chips import Chip
from tools.bench.manifest import Artifact

HEX_DATA = 0
HEX_EXTENDED_SEGMENT = 2
HEX_EXTENDED_LINEAR = 4
ERASED = 0xFF
SEGMENT_SHIFT = 4
LINEAR_SHIFT = 16

ARDUINO_CORE = "arduino-core-avr"
UBERNEE_EDITS = (
    ("#define SELECT_MAGICKEY SCEE", "#define SELECT_MAGICKEY SCEA"),
    ("const bool LGT8F328P = yes", "const bool LGT8F328P = no"),
)
ARDUINO_FLAGS = (
    "-mmcu=atmega328p",
    "-DF_CPU=16000000L",
    "-DARDUINO=10819",
    "-DARDUINO_AVR_UNO",
    "-DARDUINO_ARCH_AVR",
    "-Os",
    "-g",
    "-ffunction-sections",
    "-fdata-sections",
)
CPP_FLAGS = (
    "-std=gnu++17",
    "-fpermissive",
    "-fno-exceptions",
    "-fno-threadsafe-statics",
)


def hex_image(text: str) -> bytes:
    """Lay an Intel HEX file out as flash, gaps erased."""
    memory: dict[int, int] = {}
    base = 0
    for line in text.split():
        record = bytes.fromhex(line[1:])
        count, offset, kind = record[0], (record[1] << 8) | record[2], record[3]
        payload = record[4 : 4 + count]
        if kind == HEX_DATA:
            memory.update(
                (base + offset + index, byte) for index, byte in enumerate(payload)
            )
        elif kind == HEX_EXTENDED_SEGMENT:
            base = int.from_bytes(payload, "big") << SEGMENT_SHIFT
        elif kind == HEX_EXTENDED_LINEAR:
            base = int.from_bytes(payload, "big") << LINEAR_SHIFT
    top = max(memory, default=-1) + 1
    return bytes(memory.get(address, ERASED) for address in range(top))


def edit_sketch(text: str) -> str:
    """Apply UBERNEE_EDITS; refuse a sketch that lacks one of the lines."""
    edited = text
    for old, new in UBERNEE_EDITS:
        if old not in edited:
            raise ValueError(f"ubernee: the sketch has no line {old!r}")
        edited = edited.replace(old, new)
    return edited


def assemble(root: Path, artifact: Artifact, chip: Chip) -> str | None:
    """Assemble a PIC source into the chip's image; None, or gpasm's error."""
    defines = [arg for define in artifact.defines or () for arg in ("-D", define)]
    result = subprocess.run(
        [
            "gpasm",
            "-p",
            chip.cpu.removeprefix("p"),
            *defines,
            "-o",
            str(root / chip.firmware),
            str(root / str(artifact.path)),
        ],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        return f"{chip.name}: gpasm failed: {result.stdout.strip()}"
    return None


def avr_elf(root: Path, artifact: Artifact, chip: Chip, workdir: Path) -> None:
    """Wrap an AVR Intel HEX image in the ELF simavr loads."""
    flat = workdir / f"{chip.name}.bin"
    flat.write_bytes(hex_image((root / str(artifact.path)).read_text()))
    subprocess.run(
        [
            "avr-objcopy",
            "-I",
            "binary",
            "-O",
            "elf32-avr",
            "-B",
            "avr",
            "--rename-section",
            ".data=.text,contents,alloc,load,readonly,code",
            flat.name,
            str(root / chip.firmware),
        ],
        cwd=workdir,
        check=True,
        capture_output=True,
    )


def _git(args: list[str], cwd: Path) -> str:
    return subprocess.run(
        ["git", *args], cwd=cwd, check=True, capture_output=True, text=True
    ).stdout.strip()


def checkout(artifact: Artifact, target: Path) -> str | None:
    """Clone once, check out the pinned commit, and confirm its tree."""
    if not target.is_dir():
        subprocess.run(
            ["git", "clone", "--quiet", str(artifact.url), str(target)],
            check=True,
            capture_output=True,
        )
    _git(["checkout", "--quiet", "--force", str(artifact.commit)], target)
    tree = _git(["rev-parse", "HEAD^{tree}"], target)
    if tree != artifact.tree:
        return (
            f"{artifact.id}: tree {tree} at {artifact.commit}, expected {artifact.tree}"
        )
    return None


def _compile(source: Path, output: Path, includes: list[str]) -> None:
    cpp = source.suffix == ".cpp"
    subprocess.run(
        [
            "avr-g++" if cpp else "avr-gcc",
            *ARDUINO_FLAGS,
            *(CPP_FLAGS if cpp else ("-std=gnu11",)),
            *includes,
            "-c",
            str(source),
            "-o",
            str(output),
        ],
        check=True,
        capture_output=True,
    )


def build_ubernee(
    root: Path, sketch: Artifact, core: Artifact, chip: Chip, workdir: Path
) -> str | None:
    """Build the UberNee sketch against the pinned Arduino AVR core."""
    for artifact, folder in ((core, "arduino-core"), (sketch, "ubernee-src")):
        refused = checkout(artifact, workdir / folder)
        if refused is not None:
            return refused
    core_dir = workdir / "arduino-core"
    out = workdir / "ubernee-build"
    out.mkdir(exist_ok=True)
    copy = out / "sketch.ino"
    copy.write_text(
        edit_sketch((workdir / "ubernee-src" / str(sketch.sketch)).read_text())
    )
    wrapper = out / "sketch.cpp"
    wrapper.write_text(f'#include "Arduino.h"\n#include "{copy.resolve()}"\n')
    core_sources = core_dir / "cores" / "arduino"
    includes = [f"-I{core_sources}", f"-I{core_dir / 'variants' / 'standard'}"]
    sources = [*sorted(core_sources.glob("*.c*")), wrapper]
    objects = [out / f"{source.stem}{source.suffix}.o" for source in sources]
    for source, obj in zip(sources, objects, strict=True):
        _compile(source, obj, includes)
    subprocess.run(
        [
            "avr-gcc",
            "-mmcu=atmega328p",
            "-Os",
            "-g",
            "-Wl,--gc-sections",
            *map(str, objects),
            "-o",
            str(root / chip.firmware),
        ],
        check=True,
        capture_output=True,
    )
    return None
