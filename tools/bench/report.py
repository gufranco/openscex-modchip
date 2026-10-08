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

from collections.abc import Iterable
from dataclasses import dataclass

from tools.bench.envelope import Finding, Verdict
from tools.bench.run import Run

NANOSECONDS_PER_MS = 1_000_000


@dataclass(frozen=True, slots=True)
class Coverage:
    """How many of a chip's instructions ran, and which ranges never did."""

    chip: str
    executed: int
    total: int
    uncovered: tuple[tuple[int, int], ...]

    @property
    def percent(self) -> float:
        """Executed share in percent; an empty image counts as zero."""
        return 100.0 * self.executed / self.total if self.total else 0.0


def coverage(
    chip: str, addresses: frozenset[int], executed: Iterable[frozenset[int]]
) -> Coverage:
    """Merge every scenario's executed set and measure it against the image.

    Execution outside the image, such as the PIC reset vector's calibration
    word at the top of memory when the image leaves it to the factory, is not
    an instruction of the image and is not counted. Uncovered instructions
    that follow each other in the image merge into one range.
    """
    ran = frozenset().union(*executed) & addresses
    ranges: list[tuple[int, int]] = []
    previous_ran = True
    for address in sorted(addresses):
        if address in ran:
            previous_ran = True
            continue
        if previous_ran or not ranges:
            ranges = [*ranges, (address, address)]
        else:
            ranges = [*ranges[:-1], (ranges[-1][0], address)]
        previous_ran = False
    return Coverage(chip, len(ran), len(addresses), tuple(ranges))


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


def _coverage(results: list[Coverage]) -> list[str]:
    lines = [
        "## Instruction coverage",
        "",
        "| chip | executed | instructions | percent |",
        "|---|---|---|---|",
    ]
    lines.extend(
        f"| {c.chip} | {c.executed} | {c.total} | {c.percent:.1f} |" for c in results
    )
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
