# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""A runner's trace, read back into per-pin changes.

Both runners write `<time_ns> <output> <driven> <level>` whenever an output
changes, and one final `<time_ns> end 0 0` line at the end of the run. DATA
and the gate (the WFCK pin when the chip drives it) are what the console sees;
any other output, such as the LED on ours, is ignored here. A `pull-<line>`
line says whether that console line is held by the chip's internal pull-up,
`<time_ns> pull-<line> <pulled> <pulled>`, and is kept per line.
"""

from dataclasses import dataclass, field

from tools.bench.decode import Change

PULL_PREFIX = "pull-"


@dataclass(frozen=True, slots=True)
class Trace:
    """One run's DATA and gate changes and when the run ended."""

    data: list[Change]
    gate: list[Change]
    end_ns: int
    pulls: dict[str, list[Change]] = field(default_factory=dict)


def parse(lines: list[str]) -> Trace:
    """Read the trace lines a runner wrote."""
    pins: dict[str, list[Change]] = {"data": [], "gate": []}
    pulls: dict[str, list[Change]] = {}
    end_ns = 0
    for line in lines:
        time_ns, name, driven, level = line.split()
        change = Change(int(time_ns), driven == "1", level == "1")
        if name == "end":
            end_ns = int(time_ns)
        elif name in pins:
            pins[name].append(change)
        elif name.startswith(PULL_PREFIX):
            pulls.setdefault(name.removeprefix(PULL_PREFIX), []).append(change)
    return Trace(pins["data"], pins["gate"], end_ns, pulls)


def pulled_ns(trace: Trace) -> int:
    """Total time any console line was held by the chip's pull-up."""
    return sum(driven_ns(changes, trace.end_ns, []) for changes in trace.pulls.values())


def driven_ns(
    changes: list[Change], end_ns: int, excluded: list[tuple[int, int]]
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
