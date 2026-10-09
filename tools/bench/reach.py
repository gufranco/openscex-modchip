# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Which of a chip's unexecuted instructions no console input can reach.

Instruction coverage counts what the scenarios executed. What they did not
splits two ways: code a scenario could still reach, which is the bench's
to-do list, and code nothing on the console's wires can reach, which needs a
reason instead of a scenario. This module does that split.

A reason comes from one of three places. In this firmware it is a marker
comment, MARKER followed by the reason, on the line above the statement; the
bench maps each instruction to its source line through the image's debug
information, so the reason sits next to the code it excuses and moves with it.
Instructions with no source line in this repository are avr-libc's start-up
and exit code, excused once with RUNTIME_REASON. A third-party image cannot
carry markers, so its reasons live in a committed table, each entry a source
line of its own source or an address range of the image.

An exclusion only ever excuses an instruction no scenario executed: once a
scenario reaches it, it counts as executed, so a stale reason can hide nothing.
"""

from collections.abc import Iterable
from dataclasses import dataclass
from pathlib import Path

MARKER = "coverage: unreachable:"
BLOCK_MARKER = "coverage: unreachable block:"
RUNTIME_REASON = (
    "avr-libc and libgcc code built without line information: the interrupt "
    "vectors a firmware never enables, the path after main returns, which it "
    "never does, and helper paths the program's arguments never take"
)
COMMENT_STARTS = ("//", ";", "#", "*", "/*")

Line = tuple[str, int]


@dataclass(frozen=True, slots=True)
class Entry:
    """One table exclusion: a source line `file:line`, an address range from
    start to end inclusive, or a whole source file by name, and why nothing
    reaches it. A file entry is for framework code a third-party build links
    in, such as the Arduino core's serial receive paths, never for a chip's
    own program."""

    line: str | None
    start: int | None
    end: int | None
    reason: str
    file: str | None = None


@dataclass(frozen=True, slots=True)
class Reach:
    """A chip's instructions split three ways."""

    total: int
    executed: int
    excluded: dict[int, str]
    remaining: tuple[int, ...]

    @property
    def percent(self) -> float:
        """Executed or excused, as a share of all; an empty image is whole."""
        if self.total == 0:
            return 100.0
        return 100.0 * (self.executed + len(self.excluded)) / self.total


def _is_code(text: str) -> bool:
    stripped = text.strip()
    return bool(stripped) and not stripped.startswith(COMMENT_STARTS)


def _comment_text(line: str) -> str:
    """A comment line's words, without its comment leader; only ever called
    on a line _is_code has already found to be a comment."""
    stripped = line.strip()
    leader = next(start for start in COMMENT_STARTS if stripped.startswith(start))
    return stripped[len(leader) :].strip()


def _marked_in(path: str, text: str) -> dict[Line, str]:
    found: dict[Line, str] = {}
    reason: str | None = None
    block = False
    for number, content in enumerate(text.splitlines(), start=1):
        if BLOCK_MARKER in content or MARKER in content:
            block = BLOCK_MARKER in content
            reason = content.split(BLOCK_MARKER if block else MARKER, 1)[1].strip()
        elif reason is not None and block and not content.strip():
            reason = None
        elif reason is not None and not _is_code(content) and content.strip():
            reason = f"{reason} {_comment_text(content)}"
        elif reason is not None and _is_code(content):
            found[(path, number)] = reason
            reason = reason if block else None
    return found


def markers(sources: dict[str, str]) -> dict[Line, str]:
    """Each marked code line, as (file, line number), with its reason.

    MARKER names the first line after it that holds code; comment lines in
    between continue the reason, so a reason can run over several lines.
    BLOCK_MARKER names every code line after it up to the next blank line, for
    a stretch of assembly no input reaches, such as a timeout's return path.
    """
    found: dict[Line, str] = {}
    for path, text in sources.items():
        found = {**found, **_marked_in(path, text)}
    return found


def by_line(
    uncovered: Iterable[int],
    lines: dict[int, Line | None],
    marked: dict[Line, str],
) -> dict[int, str]:
    """Excuse uncovered instructions on marked lines, and those with no source."""
    excluded: dict[int, str] = {}
    for address in uncovered:
        line = lines.get(address)
        if line is None:
            excluded[address] = RUNTIME_REASON
        elif line in marked:
            excluded[address] = marked[line]
    return excluded


def _entry_covers(entry: Entry, address: int, line: Line | None) -> bool:
    if entry.file is not None:
        return line is not None and Path(line[0]).name == entry.file
    if entry.line is not None:
        return line is not None and f"{Path(line[0]).name}:{line[1]}" == entry.line
    if entry.start is None or entry.end is None:
        return False
    return entry.start <= address <= entry.end


def by_table(
    uncovered: Iterable[int],
    lines: dict[int, Line | None],
    table: list[Entry],
) -> dict[int, str]:
    """Excuse uncovered instructions a committed table names, by line or range."""
    excluded: dict[int, str] = {}
    for address in uncovered:
        line = lines.get(address)
        for entry in table:
            if _entry_covers(entry, address, line):
                excluded[address] = entry.reason
                break
    return excluded


def account(
    addresses: frozenset[int], executed: frozenset[int], excused: dict[int, str]
) -> Reach:
    """Split an image's instructions into executed, excused and remaining."""
    ran = executed & addresses
    unexecuted = addresses - ran
    excluded = {a: r for a, r in excused.items() if a in unexecuted}
    remaining = tuple(sorted(unexecuted - excluded.keys()))
    return Reach(len(addresses), len(ran), excluded, remaining)
