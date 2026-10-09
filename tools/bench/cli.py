# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Command line for the console bench.

    python3 -m tools.bench.cli [--chips a,b] [--scenarios x,y] [--out FILE]
                               [--workdir DIR]

Every selected chip is prepared, a chip that cannot be is skipped with its
reason, and each ready chip plays every selected scenario. Ours is then held
against the field-proven chips that ran the same scenario, and the report is
written as Markdown. The exit status is 1 when any envelope rule fails or
when ours has an instruction no scenario executed and no reason excuses, or
fails a showcase property, 2 on a bad argument, else 0; a run where nothing
could be compared still exits 0, and the report says every rule is unchecked
rather than passed. Coverage and the showcase are judged only when every
scenario runs: a run of a chosen few is expected to leave code uncovered and
properties unexercised. The showcase results are written as showcase.json
next to the report.
"""

import argparse
import os
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path

from tools.bench import showcase
from tools.bench.catalogue import CATALOGUE
from tools.bench.catalogue import find as scenario_by_name
from tools.bench.chips import CHIPS, Chip
from tools.bench.chips import by_name as chip_by_name
from tools.bench.envelope import Finding, Verdict, check_ours
from tools.bench.manifest import ROOT
from tools.bench.metrics import Metrics
from tools.bench.report import Coverage, coverage, render
from tools.bench.run import Run, excuse, prepare, program_addresses, run
from tools.bench.scenarios import Scenario

DEFAULT_REPORT = ROOT / "build" / "bench" / "report.md"
OURS = "ours"


@dataclass(frozen=True, slots=True)
class Bench:
    """Everything one bench invocation produced."""

    runs: list[Run]
    skips: dict[str, str]
    findings: list[Finding]
    coverages: list[Coverage]


def select(given: str | None, known: list[str]) -> list[str]:
    """The names a comma-separated option picks, or all of them."""
    if given is None:
        return known
    chosen = [name.strip() for name in given.split(",") if name.strip()]
    unknown = [name for name in chosen if name not in known]
    if unknown:
        raise ValueError(f"unknown {', '.join(unknown)}; known: {', '.join(known)}")
    return chosen


def evidence(runs: list[Run], scenario: Scenario) -> list[Metrics]:
    """The field-proven runs of one scenario that may bound ours.

    A chip counts only when it is made for every board the scenario stands
    for: a chip that was never meant for a PU-7 says nothing about what a
    PU-7 accepts, whatever it did in the simulation.
    """
    return [
        r.metrics
        for r in runs
        if r.chip.field_proven
        and r.scenario.name == scenario.name
        and scenario.boards <= r.chip.boards
    ]


def judge(runs: list[Run]) -> list[Finding]:
    """Hold ours to the envelope in every judged scenario it ran."""
    findings: list[Finding] = []
    for ours in (r for r in runs if r.chip.name == OURS and r.scenario.judged):
        proven = evidence(runs, ours.scenario)
        findings.extend(check_ours(ours.scenario.name, ours.metrics, proven))
    return findings


def applies(chip: Chip, scenario: Scenario) -> bool:
    """Whether a chip runs a scenario. A scenario that exists to drive one
    line, such as the sense-line walks of Mayumi V4 and MM3, is skipped by a
    third-party chip that has no such line, where it would only burn time;
    ours runs every scenario, since every instruction of it must be reached."""
    return (
        chip.name == OURS
        or scenario.drives is None
        or f"{scenario.drives}=" in chip.pins
    )


def _run_all(root: Path, chip: Chip, scenarios: list[str], workdir: Path) -> list[Run]:
    """One chip through every scenario, the runs side by side.

    Each run is its own simulator process writing its own files, so they share
    nothing and can run at once; the pool is bounded by the machine's cores.
    The results keep the scenarios' order.
    """
    jobs = os.cpu_count() or 1
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        chosen = [
            scenario_by_name(name)
            for name in scenarios
            if applies(chip, scenario_by_name(name))
        ]
        return list(pool.map(lambda s: run(root, chip, s, workdir), chosen))


def bench(root: Path, chips: list[str], scenarios: list[str], workdir: Path) -> Bench:
    """Prepare, run and measure every selected chip in every scenario."""
    runs: list[Run] = []
    skips: dict[str, str] = {}
    coverages: list[Coverage] = []
    for chip in map(chip_by_name, chips):
        reason = prepare(root, chip, workdir)
        if reason is not None:
            skips = {**skips, chip.name: reason}
            continue
        mine = _run_all(root, chip, scenarios, workdir)
        runs.extend(mine)
        addresses = program_addresses(root, chip)
        ran = frozenset().union(*(r.executed for r in mine))
        excused = excuse(root, chip, addresses - ran)
        coverages.append(coverage(chip.name, addresses, [ran], excused))
    return Bench(runs, skips, judge(runs), coverages)


def verdict(findings: list[Finding]) -> int:
    """Print every broken rule and return the exit status they call for."""
    broken = [f for f in findings if f.verdict is Verdict.BROKEN]
    for finding in broken:
        print(f"BROKEN {finding.scenario} {finding.metric}: {finding.ours}")
    return 1 if broken else 0


def reached(coverages: list[Coverage]) -> int:
    """Fail the run when this firmware has an instruction no scenario reaches
    and no reason excuses.

    Only ours is held to it: a third-party image's gaps are reported, since
    its code is not this project's to change, but they never fail the run.
    """
    left = [c for c in coverages if c.chip == OURS and c.uncovered]
    for result in left:
        spans = ", ".join(f"{low:#x}-{high:#x}" for low, high in result.uncovered)
        print(f"UNCOVERED {result.chip} {spans}")
    return 1 if left else 0


def showcased(results: showcase.Results) -> int:
    """Fail the run when ours fails a showcase property; the other chips'
    outcomes are reported only."""
    failed = showcase.failures(results)
    for key in failed:
        print(f"SHOWCASE {OURS} fails {key}")
    return 1 if failed else 0


def _parse(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="python3 -m tools.bench.cli")
    parser.add_argument("--chips", help="comma-separated chip names")
    parser.add_argument("--scenarios", help="comma-separated scenario names")
    parser.add_argument("--out", type=Path, default=DEFAULT_REPORT)
    parser.add_argument("--workdir", type=Path)
    return parser.parse_args(argv[1:])


def main(argv: list[str]) -> int:
    """Run the bench as asked; see the module docstring for exit codes."""
    args = _parse(argv)
    try:
        chips = select(args.chips, [c.name for c in CHIPS])
        scenarios = select(args.scenarios, [s.name for s in CATALOGUE])
    except ValueError as error:
        print(f"bench: {error}", file=sys.stderr)
        return 2
    with tempfile.TemporaryDirectory() as scratch:
        workdir = args.workdir or Path(scratch)
        workdir.mkdir(parents=True, exist_ok=True)
        result = bench(ROOT, chips, scenarios, workdir)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(
        render(result.runs, result.skips, result.findings, result.coverages)
    )
    results = showcase.evaluate(result.runs)
    args.out.with_name("showcase.json").write_text(showcase.to_json(results))
    print(f"bench report: {args.out}")
    return exit_status(result, results, whole=args.scenarios is None)


def exit_status(result: Bench, results: showcase.Results, *, whole: bool) -> int:
    """The run's exit status: broken rules always count; uncovered code and
    failed showcase properties only on a run of every scenario."""
    if not whole:
        return verdict(result.findings)
    return max(verdict(result.findings), reached(result.coverages), showcased(results))


if __name__ == "__main__":
    sys.exit(main(sys.argv))
