# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""The bench report: what each chip did, how ours sits in the envelope, and
how much of each firmware the scenarios reached.

Coverage is instruction coverage: the share of the image's instructions that
executed in at least one scenario. The uncovered addresses are listed as
ranges so each can be read against a disassembly and given a reason, such as
a branch only another region's build takes, a path behind a console signal
this bench does not model, or code the image carries but never calls. A gap
without a reason is the bench's to-do list, not a number to round away.

The report is Markdown, written for a person reading it next to the
disassembly; the layout is free to change, the facts it carries are not.
"""

import math
from collections import Counter
from collections.abc import Iterable
from dataclasses import dataclass

from tools.bench.envelope import Finding, Verdict
from tools.bench.reach import account
from tools.bench.run import Run

NANOSECONDS_PER_MS = 1_000_000


@dataclass(frozen=True, slots=True)
class Coverage:
    """How many of a chip's instructions ran, how many no console input can
    reach and why, and which ranges are still owed a scenario."""

    chip: str
    executed: int
    total: int
    uncovered: tuple[tuple[int, int], ...]
    excluded: tuple[tuple[str, int], ...] = ()

    @property
    def percent(self) -> float:
        """Executed or excused, in percent of all; an empty image is whole."""
        excused = sum(count for _, count in self.excluded)
        return 100.0 * (self.executed + excused) / self.total if self.total else 100.0


def _ranges(
    addresses: frozenset[int], left: frozenset[int]
) -> tuple[tuple[int, int], ...]:
    """Merge left-over instructions that follow each other in the image."""
    ranges: list[tuple[int, int]] = []
    previous_left = False
    for address in sorted(addresses):
        if address not in left:
            previous_left = False
            continue
        if previous_left:
            ranges = [*ranges[:-1], (ranges[-1][0], address)]
        else:
            ranges = [*ranges, (address, address)]
        previous_left = True
    return tuple(ranges)


def coverage(
    chip: str,
    addresses: frozenset[int],
    executed: Iterable[frozenset[int]],
    excused: dict[int, str] | None = None,
) -> Coverage:
    """Merge every scenario's executed set and measure it against the image.

    Execution outside the image, such as the PIC reset vector's calibration
    word at the top of memory when the image leaves it to the factory, is not
    an instruction of the image and is not counted. An excused instruction
    that no scenario ran is counted under its reason; one a scenario did run
    is just executed. The rest are the uncovered ranges.
    """
    reach = account(addresses, frozenset().union(*executed), excused or {})
    reasons = Counter(reach.excluded.values())
    return Coverage(
        chip,
        reach.executed,
        reach.total,
        _ranges(addresses, frozenset(reach.remaining)),
        tuple(sorted(reasons.items())),
    )


def _ms(value: int | None) -> str:
    return "-" if value is None else f"{value / NANOSECONDS_PER_MS:.3f}"


def _cell(value: object) -> str:
    return "-" if value is None else str(value)


def _support(run: Run) -> str:
    """Whether the chip is made for all, some or none of the scenario's boards."""
    if run.scenario.boards <= run.chip.boards:
        return "all"
    return "partly" if run.scenario.boards & run.chip.boards else "none"


def _runs(runs: list[Run]) -> list[str]:
    lines = [
        "## Behaviour",
        "",
        "| chip | scenario | accepts | valid | garbled | before program "
        "| in play | in reread | second window | first string ms | cell ms "
        "| driven outside strings ms | boards |",
        "|---|---|---|---|---|---|---|---|---|---|---|---|---|",
    ]
    for run in runs:
        m = run.metrics
        cell = f"{_ms(m.cell_min_ns)}-{_ms(m.cell_max_ns)}"
        lines.append(
            f"| {run.chip.name} | {run.scenario.name} "
            f"| {'yes' if m.would_accept else 'no'} | {m.valid} | {m.garbled} "
            f"| {m.before_program} | {m.during_play} | {m.during_reread} "
            f"| {m.second_window} | {_ms(m.first_latency_ns)} | {cell} "
            f"| {_ms(m.data_driven_outside_ns)} | {_support(run)} |"
        )
    return lines


def _findings(findings: list[Finding]) -> list[str]:
    counts = dict.fromkeys(Verdict, 0)
    for finding in findings:
        counts[finding.verdict] += 1
    lines = [
        "## Ours against the field-proven envelope",
        "",
        f"{counts[Verdict.MET]} met, {counts[Verdict.BROKEN]} broken, "
        f"{counts[Verdict.UNCHECKED]} unchecked.",
        "",
        "| scenario | rule | verdict | ours | low | high |",
        "|---|---|---|---|---|---|",
    ]
    lines.extend(
        f"| {f.scenario} | {f.metric} | {f.verdict} | {_cell(f.ours)} "
        f"| {_cell(f.low)} | {_cell(f.high)} |"
        for f in findings
    )
    return lines


def _floor_tenth(percent: float) -> str:
    """A percent to one decimal, rounded down: 3801 of 3802 instructions is
    99.97, and rounding to nearest would print the 100.0 it has not earned."""
    return f"{math.floor(percent * 10) / 10:.1f}"


def _coverage(results: list[Coverage]) -> list[str]:
    lines = [
        "## Instruction coverage",
        "",
        "| chip | executed | excused | instructions | percent |",
        "|---|---|---|---|---|",
    ]
    lines.extend(
        f"| {c.chip} | {c.executed} | {sum(n for _, n in c.excluded)} | {c.total} "
        f"| {_floor_tenth(c.percent)} |"
        for c in results
    )
    lines.extend(["", "Excused, no console input reaches them:", ""])
    for result in results:
        lines.extend(f"- {result.chip}, {n}: {why}" for why, n in result.excluded)
    lines.extend(["", "Uncovered ranges:", ""])
    for result in results:
        spans = ", ".join(f"{low:#x}-{high:#x}" for low, high in result.uncovered)
        lines.append(f"- {result.chip}: {spans or 'none'}")
    return lines


def render(
    runs: list[Run],
    skips: dict[str, str],
    findings: list[Finding],
    coverages: list[Coverage],
) -> str:
    """The whole report as Markdown."""
    skipped = [f"- {name}: {reason}" for name, reason in skips.items()]
    sections = [
        ["# Console bench report"],
        ["## Skipped chips", "", *(skipped or ["None."])],
        _runs(runs),
        _findings(findings),
        _coverage(coverages),
    ]
    return "\n\n".join("\n".join(section) for section in sections) + "\n"
