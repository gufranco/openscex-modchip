# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Mutation testing for the logic layer: every mutant must make a host test
fail."""

import re
import subprocess
import sys
import tempfile
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LOGIC_FILES = (
    "src/region.c",
    "src/subq.c",
    "src/board_mode.c",
    "src/inject.c",
    "src/indicator.c",
    "src/calib.c",
    "src/trim.c",
    "src/supply.c",
    "src/loop.c",
    "src/trace.c",
)
TEST_FILES = (
    "tests/host/host_assert.c",
    "tests/host/host_test.c",
    "tests/host/calib_test.c",
    "tests/host/trim_test.c",
    "tests/host/disc_test.c",
    "tests/host/indicator_test.c",
    "tests/host/loop_test.c",
    "tests/host/trace_test.c",
)
CC = "gcc"
# A backstop per compile and per run: the unmutated host suite builds and runs
# in under 2 s, so a mutant still running after a minute is stuck in a loop it
# made, which the suite has caught.
STEP_TIMEOUT_S = 60
CFLAGS = (
    "-std=c17",
    "-pedantic-errors",
    "-Iinclude",
    "-O0",
    "-DPSCU_DEBUG",
)


def _bump(match: re.Match[str]) -> str:
    """An unsigned integer literal one higher, in the same base and suffix."""
    digits = match.group(1)
    value = int(digits, 0) + 1
    return (f"0x{value:X}" if digits.lower().startswith("0x") else str(value)) + "U"


Replacement = str | Callable[[re.Match[str]], str]

# Each mutator flips one operator to a near neighbour. The single < and > forms
# use look-around so they never match <<, >>, ->, <=, >= or ==. The arithmetic,
# bitwise and shift forms match only between spaces, as clang-format writes a
# binary operator, so a unary minus or an arrow is never touched. Every unsigned
# literal is also tried one higher, which catches an off-by-one in a bound.
MUTATORS: tuple[tuple[str, re.Pattern[str], Replacement], ...] = (
    ("eq->ne", re.compile(r"=="), "!="),
    ("ne->eq", re.compile(r"!="), "=="),
    ("ge->gt", re.compile(r">="), ">"),
    ("le->lt", re.compile(r"<="), "<"),
    ("and->or", re.compile(r"&&"), "||"),
    ("or->and", re.compile(r"\|\|"), "&&"),
    ("lt->le", re.compile(r"(?<![<>=-])<(?![<=])"), "<="),
    ("gt->ge", re.compile(r"(?<![<>=-])>(?![>=])"), ">="),
    ("add->sub", re.compile(r"(?<= )\+(?= )"), "-"),
    ("sub->add", re.compile(r"(?<= )-(?= )"), "+"),
    ("band->bor", re.compile(r"(?<= )&(?= )"), "|"),
    ("bor->band", re.compile(r"(?<= )\|(?= )"), "&"),
    ("xor->bor", re.compile(r"(?<= )\^(?= )"), "|"),
    ("shl->shr", re.compile(r"(?<= )<<(?= )"), ">>"),
    ("shr->shl", re.compile(r"(?<= )>>(?= )"), "<<"),
    ("true->false", re.compile(r"\btrue\b"), "false"),
    ("false->true", re.compile(r"\bfalse\b"), "true"),
    ("literal+1", re.compile(r"\b(0[xX][0-9A-Fa-f]+|\d+)U\b"), _bump),
)


@dataclass(frozen=True, slots=True)
class Equivalent:
    """A mutant that cannot change behaviour, with the reason. It is matched by
    file, a fragment of its line, the mutator, and which of that mutator's
    matches on the line it is (-1 for all of them). lines is how many lines of
    the file hold the fragment, so a new line that happens to contain it is
    caught by a test instead of silently excused."""

    path: str
    fragment: str
    mutator: str
    occurrence: int
    reason: str
    lines: int = 1


_UNREAD = "code and live are read only in the CODE and DARK stages, live only in CODE"
_CARRIER = "carrier is only ever compared with zero"
_TOLD_ZERO = "told bit 0 names no code; codes start at 1"
_ASSIGNED_FIRST = "pscu_loop_read and pscu_loop_burst assign every field before use"
_SHORT_TAGS = (
    "every tag ends in a zero byte by its seventh letter, so the bound is never met"
)

EQUIVALENTS: tuple[Equivalent, ...] = (
    Equivalent(
        "src/board_mode.c", "(needed == 0U) ? 1U : 0U", "literal+1", 2, _CARRIER
    ),
    Equivalent("src/board_mode.c", "? 1U : state.carrier", "literal+1", 0, _CARRIER),
    Equivalent(
        "src/indicator.c", "PSCU_INDICATOR_BOARD, 0U, false,", "literal+1", 0, _UNREAD
    ),
    Equivalent(
        "src/indicator.c", "PSCU_INDICATOR_BOARD, 0U, false,", "false->true", 0, _UNREAD
    ),
    Equivalent("src/indicator.c", "0U, profile, 0U };", "literal+1", 1, _TOLD_ZERO),
    Equivalent(
        "src/indicator.c",
        "return (uint8_t)(1U << code);",
        "literal+1",
        0,
        "told bits stay distinct for codes 1 to 6; the supply code is never told",
    ),
    Equivalent(
        "src/indicator.c",
        "PSCU_INDICATOR_INJECT, 0U, false);",
        "literal+1",
        -1,
        _UNREAD,
    ),
    Equivalent(
        "src/indicator.c",
        "PSCU_INDICATOR_INJECT, 0U, false);",
        "false->true",
        -1,
        _UNREAD,
    ),
    Equivalent(
        "src/indicator.c",
        "(state, PSCU_INDICATOR_WAIT, 0U, false);",
        "literal+1",
        -1,
        _UNREAD,
        3,
    ),
    Equivalent(
        "src/indicator.c",
        "(state, PSCU_INDICATOR_WAIT, 0U, false);",
        "false->true",
        -1,
        _UNREAD,
        3,
    ),
    Equivalent(
        "src/indicator.c",
        "(&next, PSCU_INDICATOR_WAIT, 0U, false);",
        "literal+1",
        -1,
        _UNREAD,
    ),
    Equivalent(
        "src/indicator.c",
        "(&next, PSCU_INDICATOR_WAIT, 0U, false);",
        "false->true",
        -1,
        _UNREAD,
    ),
    # The board stage's exit picks its next stage from a test of replay, whose
    # literal is live; only the code and live arguments after it go unread.
    Equivalent(
        "src/indicator.c",
        "PSCU_INDICATOR_REPLAY : PSCU_INDICATOR_WAIT, 0U, false);",
        "literal+1",
        1,
        _UNREAD,
    ),
    Equivalent(
        "src/indicator.c",
        "PSCU_INDICATOR_REPLAY : PSCU_INDICATOR_WAIT, 0U, false);",
        "false->true",
        0,
        _UNREAD,
    ),
    Equivalent(
        "src/indicator.c",
        "PSCU_INDICATOR_DARK, 0U, false);",
        "false->true",
        -1,
        _UNREAD,
    ),
    Equivalent(
        "src/indicator.c",
        "PSCU_INDICATOR_DARK, 1U, false);",
        "literal+1",
        -1,
        "the DARK stage reads only whether code is nonzero",
    ),
    Equivalent(
        "src/indicator.c",
        "PSCU_INDICATOR_DARK, 1U, false);",
        "false->true",
        -1,
        _UNREAD,
    ),
    Equivalent(
        "src/indicator.c",
        "pause_ms = 0U;",
        "literal+1",
        -1,
        "the board stage ends at its cycle without a pause, before any pause",
    ),
    Equivalent("src/indicator.c", "next.told = 0U;", "literal+1", -1, _TOLD_ZERO),
    Equivalent(
        "src/calib.c",
        "(trigger - PSCU_CALIB_TRIGGER_MIN) %",
        "sub->add",
        -1,
        "the start bound 10 is even, so t + 10 and t - 10 share a remainder mod 2",
    ),
    Equivalent(
        "src/calib.c",
        "pscu_calib_record_t record = { {",
        "literal+1",
        -1,
        "every byte is assigned before the record is returned",
    ),
    Equivalent(
        "src/loop.c",
        "out.missed = false;",
        "false->true",
        -1,
        "pscu_loop_learn assigns missed before any read",
    ),
    Equivalent(
        "src/loop.c", "pscu_loop_pass_t pass = {", "literal+1", -1, _ASSIGNED_FIRST
    ),
    Equivalent(
        "src/loop.c", "pscu_loop_pass_t pass = {", "false->true", -1, _ASSIGNED_FIRST
    ),
    Equivalent(
        "src/subq.c",
        "(tno <= 0x99U)",
        "literal+1",
        1,
        "a track number 0x9A already fails the units-digit check",
    ),
    Equivalent(
        "src/trace.c",
        "trace->head = 0U;",
        "literal+1",
        0,
        "an empty ring behaves the same from any starting slot",
    ),
    Equivalent(
        "src/trace.c",
        "if (whole >= (uint32_t)room) {",
        "ge->gt",
        0,
        "at whole == room both branches set the seconds to 65535",
    ),
    Equivalent(
        "src/trace.c",
        "for (uint8_t n = 0U; (n < 8U) && going; n++) {",
        "and->or",
        0,
        _SHORT_TAGS,
    ),
    Equivalent(
        "src/trace.c",
        "for (uint8_t n = 0U; (n < 8U) && going; n++) {",
        "lt->le",
        0,
        _SHORT_TAGS,
    ),
    Equivalent(
        "src/trace.c",
        "for (uint8_t n = 0U; (n < 8U) && going; n++) {",
        "literal+1",
        1,
        _SHORT_TAGS,
    ),
    Equivalent(
        "src/run.c",
        "osc.hit = false;",
        "false->true",
        0,
        "the first pass is never timed: the loop's settle time starts at zero",
    ),
    Equivalent(
        "src/run.c",
        "(n < (uint8_t)PSCU_TRIM_MAX_STEP) && (next.osccal != target)",
        "lt->le",
        0,
        "the target is at most PSCU_TRIM_MAX_STEP notches away, so it ends the walk",
    ),
    Equivalent(
        "src/run.c",
        "(target > next.osccal) ?",
        "gt->ge",
        0,
        "the walk runs only while OSCCAL differs from the target",
    ),
    Equivalent(
        "src/run.c",
        "pscu_clock_t clock = { pscu_port_ticks(), 0U };",
        "literal+1",
        0,
        "at 8 MHz a millisecond is 8000 clocks and every tick count is even, "
        "so one clock of carry never reaches the next millisecond",
    ),
    Equivalent(
        "src/engine.c",
        "(1000UL * PSCU_SQCK_IDLE_POLL_CYCLES) - 1UL) /",
        "sub->add",
        0,
        "8 MHz leaves 2000 over a multiple of 43000, so both ceilings give 187",
    ),
    Equivalent(
        "src/engine.c",
        "bool idle = false;",
        "false->true",
        0,
        "the wait budget is never zero, so the loop assigns idle before it returns",
    ),
    Equivalent(
        "src/engine.c",
        "int8_t step = (trim < 0) ?",
        "lt->le",
        0,
        "a zero trim takes no steps, so its direction is never used",
    ),
    Equivalent(
        "src/engine.c",
        "uint8_t steps = (uint8_t)((trim < 0) ? -trim : trim);",
        "lt->le",
        0,
        "minus zero is zero",
    ),
)


def _equivalent(path: str, line: str, mutator: str, occurrence: int) -> bool:
    return any(
        e.path == path
        and e.fragment in line
        and e.mutator == mutator
        and e.occurrence in (-1, occurrence)
        for e in EQUIVALENTS
    )


@dataclass(frozen=True)
class Mutant:
    label: str
    source: str


@dataclass(frozen=True)
class Suite:
    """What to mutate and how to build the tests that must catch each mutant.
    The firmware's own suite is SUITE."""

    logic: tuple[str, ...]
    tests: tuple[str, ...]
    cc: str
    cflags: tuple[str, ...]
    timeout_s: int = STEP_TIMEOUT_S


SUITE = Suite(LOGIC_FILES, TEST_FILES, CC, CFLAGS)


@dataclass(frozen=True, slots=True)
class CommandSuite:
    """Files the host suite cannot reach, and the command that must fail for
    each of their mutants. PLATFORM is the platform C, killed by the simulator."""

    logic: tuple[str, ...]
    command: tuple[str, ...]
    timeout_s: int


# The platform C drives the port and the run loop, which only the simulator
# exercises. One simulator run builds the six images it needs and plays every
# scenario in about a minute; a mutant still running after 15 minutes is stuck.
PLATFORM = CommandSuite(
    ("src/run.c", "src/engine.c"),
    ("make", "--no-print-directory", "simtest"),
    900,
)


@dataclass(frozen=True)
class Result:
    total: int
    killed: int
    survivors: tuple[str, ...]


def _is_skipped(line: str) -> bool:
    """Lines not worth mutating: preprocessor and comment lines, PSCU_ASSERT
    guards (excluded from coverage too), and _Static_assert, which emits no
    code."""
    stripped = line.lstrip()
    if stripped.startswith("#") or stripped.startswith("//"):
        return True
    return ("PSCU_ASSERT" in line) or ("_Static_assert" in line)


def generate_mutants(source: str, path: str = "") -> list[Mutant]:
    """Every mutant of a file at path, less the listed equivalent ones."""
    lines = source.split("\n")
    mutants: list[Mutant] = []
    for index, line in enumerate(lines):
        if _is_skipped(line):
            continue
        for name, pattern, replacement in MUTATORS:
            for occurrence, match in enumerate(pattern.finditer(line)):
                if _equivalent(path, line, name, occurrence):
                    continue
                text = (
                    replacement if isinstance(replacement, str) else replacement(match)
                )
                new_line = line[: match.start()] + text + line[match.end() :]
                mutated = lines.copy()
                mutated[index] = new_line
                label = f"L{index + 1} {name} col{match.start() + 1}"
                mutants.append(Mutant(label, "\n".join(mutated)))
    return mutants


def _compile_and_run(root: Path, binary: Path, suite: Suite = SUITE) -> bool:
    """Build and run the host suite; True only if it compiles and passes.

    A mutant that fails to compile, or makes a step outlast STEP_TIMEOUT_S,
    counts as killed. The suite runs in the temp directory so a core dump never
    lands in the repo.
    """
    sources = [str(root / name) for name in (*suite.logic, *suite.tests)]
    try:
        compiled = subprocess.run(
            [suite.cc, *suite.cflags, "-o", str(binary), *sources],
            cwd=root,
            capture_output=True,
            check=False,
            timeout=suite.timeout_s,
        )
        if compiled.returncode != 0:
            return False
        executed = subprocess.run(
            [str(binary)],
            cwd=binary.parent,
            capture_output=True,
            check=False,
            timeout=suite.timeout_s,
        )
    except subprocess.TimeoutExpired:
        return False
    return executed.returncode == 0


def _mutant_killed(
    root: Path, path: Path, mutant: Mutant, binary: Path, suite: Suite = SUITE
) -> bool:
    """Apply one mutant in place, test it, and always restore the file."""
    original = path.read_text()
    try:
        path.write_text(mutant.source)
        return not _compile_and_run(root, binary, suite)
    finally:
        path.write_text(original)


def _command_passes(root: Path, suite: CommandSuite) -> bool:
    """Run the suite's command; True only if it exits 0 within its limit."""
    try:
        done = subprocess.run(
            list(suite.command),
            cwd=root,
            capture_output=True,
            check=False,
            timeout=suite.timeout_s,
        )
    except subprocess.TimeoutExpired:
        return False
    return done.returncode == 0


def run_command_suite(root: Path, suite: CommandSuite = PLATFORM) -> Result:
    """Mutate each file the command must defend, restoring it after every
    mutant. The unmutated command must pass first."""
    if not _command_passes(root, suite):
        raise RuntimeError("baseline command does not pass; cannot mutate")
    total = 0
    killed = 0
    survivors: list[str] = []
    for name in suite.logic:
        path = root / name
        original = path.read_text()
        for mutant in generate_mutants(original, name):
            total += 1
            try:
                path.write_text(mutant.source)
                caught = not _command_passes(root, suite)
            finally:
                path.write_text(original)
            if caught:
                killed += 1
            else:
                survivors.append(f"{name}: {mutant.label}")
    return Result(total, killed, tuple(survivors))


def run(root: Path, suite: Suite = SUITE) -> Result:
    """Mutate every logic file and tally killed versus survivors. The
    unmutated suite must pass first."""
    with tempfile.TemporaryDirectory() as tmp:
        binary = Path(tmp) / "host_test"
        if not _compile_and_run(root, binary, suite):
            raise RuntimeError("baseline host suite does not pass; cannot mutate")
        total = 0
        killed = 0
        survivors: list[str] = []
        for name in suite.logic:
            path = root / name
            for mutant in generate_mutants(path.read_text(), name):
                total += 1
                if _mutant_killed(root, path, mutant, binary, suite):
                    killed += 1
                else:
                    survivors.append(f"{name}: {mutant.label}")
        return Result(total, killed, tuple(survivors))


def main(
    argv: list[str],
    root: Path = ROOT,
    suite: Suite = SUITE,
    platform: CommandSuite = PLATFORM,
) -> int:
    """Run the host suite, or with --platform the simulator-killed one."""
    result = (
        run_command_suite(root, platform) if "--platform" in argv else run(root, suite)
    )
    for survivor in result.survivors:
        print(f"SURVIVED {survivor}")
    print(f"mutation score {result.killed}/{result.total} killed")
    return 0 if (result.survivors == () and result.total > 0) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
