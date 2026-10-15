# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""A runner's trace, read back into per-pin changes.

Each line is `<time_ns> <output> <driven> <level>`; the last is
`<time_ns> end 0 0`. Only DATA, the gate and `pull-<line>` states are kept. A
trace without its end line comes from a runner that stopped early, and is
refused rather than measured as if the run were shorter.
"""

from collections.abc import Mapping, Sequence
from dataclasses import dataclass, field

from tools.bench.decode import Change

PULL_PREFIX = "pull-"
FIELDS = 4


@dataclass(frozen=True, slots=True)
class Trace:
    """One run's DATA and gate changes and when the run ended."""

    data: tuple[Change, ...]
    gate: tuple[Change, ...]
    end_ns: int
    pulls: Mapping[str, tuple[Change, ...]] = field(default_factory=dict)


def parse(lines: list[str]) -> Trace:
    """Read the trace lines a runner wrote; refuse a malformed line or a trace
    with no end line."""
    pins: dict[str, list[Change]] = {"data": [], "gate": []}
    pulls: dict[str, list[Change]] = {}
    end_ns: int | None = None
    for number, line in enumerate(lines, start=1):
        words = line.split()
        if (
            len(words) != FIELDS
            or not words[0].isdigit()
            or not {words[2], words[3]} <= {"0", "1"}
        ):
            raise ValueError(
                f"trace line {number}: expected time, output, driven, level"
            )
        time_ns, name, driven, level = words
        change = Change(int(time_ns), driven == "1", level == "1")
        if name == "end":
            end_ns = int(time_ns)
        elif name in pins:
            pins[name].append(change)
        elif name.startswith(PULL_PREFIX):
            pulls.setdefault(name.removeprefix(PULL_PREFIX), []).append(change)
    if end_ns is None:
        raise ValueError("trace has no end line: the runner stopped early")
    return Trace(
        tuple(pins["data"]),
        tuple(pins["gate"]),
        end_ns,
        {line: tuple(changes) for line, changes in pulls.items()},
    )


def pulled_ns(trace: Trace) -> int:
    """Total time any console line was held by the chip's pull-up."""
    return sum(driven_ns(changes, trace.end_ns, []) for changes in trace.pulls.values())


def driven_ns(
    changes: Sequence[Change], end_ns: int, excluded: Sequence[tuple[int, int]]
) -> int:
    """Total time the pin was driven, outside the excluded spans."""
    stops = [c.time_ns for c in changes[1:]] + ([end_ns] if changes else [])
    spans = [
        (change.time_ns, stop)
        for change, stop in zip(changes, stops, strict=True)
        if change.driven
    ]
    total = 0
    for start, stop in spans:
        covered = sum(max(0, min(stop, hi) - max(start, lo)) for lo, hi in excluded)
        total += (stop - start) - covered
    return total
