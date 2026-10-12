# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Mutation testing for the logic layer: every mutant must make a host test
fail."""

import re
import subprocess
import sys
import tempfile
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
)
TEST_FILES = (
    "tests/host/host_assert.c",
    "tests/host/host_test.c",
    "tests/host/calib_test.c",
    "tests/host/trim_test.c",
    "tests/host/disc_test.c",
    "tests/host/indicator_test.c",
    "tests/host/loop_test.c",
)
CC = "gcc"
CFLAGS = ("-std=c17", "-pedantic-errors", "-Iinclude", "-O0", "-DPSCU_DEBUG")

# Each mutator flips one operator to a near neighbour. The single < and > forms
# use look-around so they never match <<, >>, ->, <=, >= or ==.
MUTATORS = (
    ("eq->ne", re.compile(r"=="), "!="),
    ("ne->eq", re.compile(r"!="), "=="),
    ("ge->gt", re.compile(r">="), ">"),
    ("le->lt", re.compile(r"<="), "<"),
    ("and->or", re.compile(r"&&"), "||"),
    ("or->and", re.compile(r"\|\|"), "&&"),
    ("lt->le", re.compile(r"(?<![<>=-])<(?![<=])"), "<="),
    ("gt->ge", re.compile(r"(?<![<>=-])>(?![>=])"), ">="),
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


SUITE = Suite(LOGIC_FILES, TEST_FILES, CC, CFLAGS)


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


def generate_mutants(source: str) -> list[Mutant]:
    lines = source.split("\n")
    mutants: list[Mutant] = []
    for index, line in enumerate(lines):
        if _is_skipped(line):
            continue
        for name, pattern, replacement in MUTATORS:
            for match in pattern.finditer(line):
                new_line = line[: match.start()] + replacement + line[match.end() :]
                mutated = lines.copy()
                mutated[index] = new_line
                label = f"L{index + 1} {name} col{match.start() + 1}"
                mutants.append(Mutant(label, "\n".join(mutated)))
    return mutants


def _compile_and_run(root: Path, binary: Path, suite: Suite = SUITE) -> bool:
    """Build and run the host suite; True only if it compiles and passes.

    A mutant that fails to compile counts as killed. The suite runs in the temp
    directory so a core dump never lands in the repo.
    """
    sources = [str(root / name) for name in (*suite.logic, *suite.tests)]
    compiled = subprocess.run(
        [suite.cc, *suite.cflags, "-o", str(binary), *sources],
        cwd=root,
        capture_output=True,
        check=False,
    )
    if compiled.returncode != 0:
        return False
    executed = subprocess.run(
        [str(binary)], cwd=binary.parent, capture_output=True, check=False
    )
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
            for mutant in generate_mutants(path.read_text()):
                total += 1
                if _mutant_killed(root, path, mutant, binary, suite):
                    killed += 1
                else:
                    survivors.append(f"{name}: {mutant.label}")
        return Result(total, killed, tuple(survivors))


def main(_argv: list[str], root: Path = ROOT, suite: Suite = SUITE) -> int:
    result = run(root, suite)
    for survivor in result.survivors:
        print(f"SURVIVED {survivor}")
    print(f"mutation score {result.killed}/{result.total} killed")
    return 0 if (result.survivors == () and result.total > 0) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
