# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Command line for the console bench.

    python3 -m tools.bench.cli [--chips a,b] [--scenarios x,y] [--out FILE]
                               [--workdir DIR]

Every selected chip is prepared, a chip that cannot be is skipped with its
reason, and each ready chip plays every selected scenario. Ours is then held
against the field-proven chips that ran the same scenario, and the report is
written as Markdown. The exit status is 1 when any envelope rule fails, 2 on
a bad argument, else 0; a run where nothing could be compared still exits 0,
and the report says every rule is unchecked rather than passed.
"""

import argparse
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

from tools.bench.chips import CHIPS
from tools.bench.chips import by_name as chip_by_name
from tools.bench.envelope import Finding, Verdict, check_ours
from tools.bench.manifest import ROOT
from tools.bench.metrics import Metrics
from tools.bench.report import Coverage, coverage, render
from tools.bench.run import Run, prepare, program_addresses, run
from tools.bench.scenarios import SCENARIOS, Scenario
from tools.bench.scenarios import by_name as scenario_by_name

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


def _envelope(runs: list[Run]) -> list[Finding]:
    findings: list[Finding] = []
    for ours in (r for r in runs if r.chip.name == OURS):
        proven = evidence(runs, ours.scenario)
        findings.extend(check_ours(ours.scenario.name, ours.metrics, proven))
    return findings


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
        mine = [run(root, chip, scenario_by_name(s), workdir) for s in scenarios]
        runs.extend(mine)
        addresses = program_addresses(root, chip)
        coverages.append(coverage(chip.name, addresses, (r.executed for r in mine)))
    return Bench(runs, skips, _envelope(runs), coverages)


def verdict(findings: list[Finding]) -> int:
    """Print every broken rule and return the exit status they call for."""
    broken = [f for f in findings if f.verdict is Verdict.BROKEN]
    for finding in broken:
        print(f"BROKEN {finding.scenario} {finding.metric}: {finding.ours}")
    return 1 if broken else 0


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
        scenarios = select(args.scenarios, [s.name for s in SCENARIOS])
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
    print(f"bench report: {args.out}")
    return verdict(result.findings)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
