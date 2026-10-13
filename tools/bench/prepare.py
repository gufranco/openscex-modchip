# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Third-party firmware the bench builds from what its authors published.

Each input is pinned by the manifest and turned into the file its runner reads,
under build/bench:

- a PIC assembly source is assembled with gpasm; Old Crow's _INT_OSC fuse name
  is passed as a define, so the source stays unedited;
- an AVR Intel HEX image is laid out flat, gaps 0xFF, and wrapped in an ELF
  for simavr;
- an Atmel-syntax AVR source is assembled with avra; Liss's .device line is
  commented out in a copy, since avra's 1200def.inc already declares the part;
- UberNee and the Arduino AVR core are fetched at pinned commits and built for
  an ATmega328P at 16 MHz, with the two edits in UBERNEE_EDITS: region USA and
  the LGT8F328P switch off.
"""

import re
import subprocess
from pathlib import Path

from tools.bench.chips import Chip
from tools.bench.intelhex import memory
from tools.bench.manifest import Artifact

# A backstop for each git, assembler, compiler and binutils call, so a stalled
# clone or tool cannot hold the bench until the job limit. No single step was
# measured; the whole bench job took 22 min 44 s for 27 scenarios.
TOOL_TIMEOUT_S = 600
ERASED = 0xFF

# Where Debian's avra package installs the Atmel part definition files.
AVRA_INCLUDES = "/usr/share/avra"
# A source's own .device directive. Only spaces and tabs may lead it: \s would
# cross the form feed before the directive in Liss's file.
DEVICE_LINE = re.compile(r"^([ \t]*\.device\b)", re.IGNORECASE | re.MULTILINE)
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
    filled = memory(text)
    top = max(filled, default=-1) + 1
    return bytes(filled.get(address, ERASED) for address in range(top))


def edit_sketch(text: str) -> str:
    """Apply UBERNEE_EDITS; refuse a sketch that lacks one of the lines."""
    edited = text
    for old, new in UBERNEE_EDITS:
        if old not in edited:
            raise ValueError(f"ubernee: the sketch has no line {old!r}")
        edited = edited.replace(old, new)
    return edited


def assemble(
    root: Path, artifact: Artifact, chip: Chip, source: bytes, workdir: Path
) -> str | None:
    """Assemble a PIC source's verified bytes into the chip's image; None, or
    gpasm's error. Its include files are gputils' own, so a copy assembles."""
    defines = [arg for define in artifact.defines or () for arg in ("-D", define)]
    copy = workdir / f"{chip.name}.asm"
    copy.write_bytes(source)
    result = subprocess.run(
        [
            "gpasm",
            "-p",
            chip.cpu.removeprefix("p"),
            *defines,
            "-o",
            str(root / chip.firmware),
            str(copy),
        ],
        capture_output=True,
        timeout=TOOL_TIMEOUT_S,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        return f"{chip.name}: gpasm failed: {result.stdout.strip()}"
    return None


def avr_elf(root: Path, chip: Chip, image: bytes, workdir: Path) -> None:
    """Wrap an AVR Intel HEX image's verified bytes in the ELF simavr loads."""
    _wrap_hex(image.decode("ascii"), root, chip, workdir)


def assemble_avr(root: Path, chip: Chip, text: bytes, workdir: Path) -> str | None:
    """Assemble an Atmel-syntax AVR source's verified bytes with avra; None, or
    avra's error."""
    source = workdir / f"{chip.name}.asm"
    decoded = text.decode("latin-1")
    source.write_text(DEVICE_LINE.sub(r";\1", decoded), encoding="latin-1")
    result = subprocess.run(
        ["avra", "-I", AVRA_INCLUDES, source.name],
        cwd=workdir,
        capture_output=True,
        timeout=TOOL_TIMEOUT_S,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        return f"{chip.name}: avra failed: {(result.stdout + result.stderr).strip()}"
    _wrap_hex(source.with_suffix(".hex").read_text(), root, chip, workdir)
    return None


def _wrap_hex(text: str, root: Path, chip: Chip, workdir: Path) -> None:
    flat = workdir / f"{chip.name}.bin"
    flat.write_bytes(hex_image(text))
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
        timeout=TOOL_TIMEOUT_S,
    )


def _git(args: list[str], cwd: Path) -> str:
    return subprocess.run(
        ["git", *args],
        cwd=cwd,
        check=True,
        capture_output=True,
        timeout=TOOL_TIMEOUT_S,
        text=True,
    ).stdout.strip()


def checkout(artifact: Artifact, target: Path) -> str | None:
    """Clone once, check out the pinned commit, and confirm its tree."""
    if not target.is_dir():
        subprocess.run(
            ["git", "clone", "--quiet", str(artifact.url), str(target)],
            check=True,
            capture_output=True,
            timeout=TOOL_TIMEOUT_S,
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
        timeout=TOOL_TIMEOUT_S,
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
        timeout=TOOL_TIMEOUT_S,
    )
    return None
