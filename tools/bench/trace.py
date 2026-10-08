# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""A runner's trace, read back into per-pin changes.

Both runners write `<time_ns> <output> <driven> <level>` whenever an output
changes, and one final `<time_ns> end 0 0` line at the end of the run. DATA
and the gate (the WFCK pin when the chip drives it) are what the console sees;
any other output, such as the LED on ours, is ignored here.
"""

from dataclasses import dataclass

from tools.bench.decode import Change


@dataclass(frozen=True, slots=True)
class Trace:
    """One run's DATA and gate changes and when the run ended."""

    data: list[Change]
    gate: list[Change]
    end_ns: int


def parse(lines: list[str]) -> Trace:
    """Read the trace lines a runner wrote."""
    pins: dict[str, list[Change]] = {"data": [], "gate": []}
    end_ns = 0
    for line in lines:
        time_ns, name, driven, level = line.split()
        if name == "end":
            end_ns = int(time_ns)
        elif name in pins:
            pins[name].append(Change(int(time_ns), driven == "1", level == "1"))
    return Trace(pins["data"], pins["gate"], end_ns)


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
