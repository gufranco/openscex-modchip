# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Ours against the envelope of the chips real consoles have accepted.

For each scenario, the field-proven chips' metrics bound what a console has
been seen to accept. Each rule says which way ours may leave that bound and
why, and the rules were set before ours was measured, so they cannot have been
fitted to it:

- acceptance: ours must accept wherever any field-proven chip accepts;
- bit cell: within the range they produce, the timing a mechacon has accepted,
  give or take one period of the fastest WFCK carrier, 14.6 kHz or 68.5 us:
  the mechanism clocks DATA on WFCK, so it cannot tell two cells apart that
  differ by less, and two chips counting the same carrier land a fraction of
  a period apart from where each samples its first edge. A chip that sent no
  string has no cell and meets the rule; whether it should have sent one is
  the acceptance rule's question;
- first string: no later than the slowest of them, or it may miss the check,
  give or take two SUBQ frames, 26.7 ms. One is sampling phase: chips start
  on a count of lead-in frames, so where in a frame each lands differs. The
  other is a frame ours can miss: between two captures with no disc it spends
  about 1.4 ms on bookkeeping and the 1 ms gap check PsNee also uses, and a
  disc's first frame that begins in that slice goes uncounted, so at 3 of 80
  arrival phases ours starts a frame later (traced with the bench,
  2026-10-08). PsNee waits on SQCK without a bound and never misses;
  ours bounds every wait by design;
- everything a detector could see (strings sent, drive time outside strings):
  at most the most they do. Fewer is always allowed; being quieter is the
  point of ours, so the envelope never forbids it.

A rule with no field-proven value to compare, because those chips sent nothing
measurable or were skipped, is unchecked, never passed.
"""

from collections.abc import Callable
from dataclasses import dataclass
from enum import StrEnum

from tools.bench.metrics import Metrics
from tools.bench.timeline import FRAME_NS

CELL_TOLERANCE_NS = 1_000_000_000 // 14_600
LATENCY_TOLERANCE_NS = 2 * FRAME_NS
CELL_RULES = frozenset({"cell_min_ns", "cell_max_ns"})


class Verdict(StrEnum):
    """One rule's outcome for one scenario."""

    MET = "met"
    BROKEN = "broken"
    UNCHECKED = "unchecked"


@dataclass(frozen=True, slots=True)
class Finding:
    """A rule applied to ours in one scenario, with the bound it met or not."""

    scenario: str
    metric: str
    verdict: Verdict
    ours: object
    low: object
    high: object


def _within(ours: int | None, values: list[int]) -> tuple[Verdict, object, object]:
    if not values:
        return Verdict.UNCHECKED, None, None
    low, high = min(values), max(values)
    floor, ceiling = low - CELL_TOLERANCE_NS, high + CELL_TOLERANCE_NS
    ok = ours is not None and floor <= ours <= ceiling
    return (Verdict.MET if ok else Verdict.BROKEN), low, high


def _at_most(ours: int | None, values: list[int]) -> tuple[Verdict, object, object]:
    if not values:
        return Verdict.UNCHECKED, None, None
    high = max(values)
    ok = ours is not None and ours <= high
    return (Verdict.MET if ok else Verdict.BROKEN), None, high


def _no_later(ours: int | None, values: list[int]) -> tuple[Verdict, object, object]:
    if not values:
        return Verdict.UNCHECKED, None, None
    high = max(values)
    ok = ours is not None and ours <= high + LATENCY_TOLERANCE_NS
    return (Verdict.MET if ok else Verdict.BROKEN), None, high


Check = Callable[[int | None, list[int]], tuple[Verdict, object, object]]

RULES: tuple[tuple[str, Check], ...] = (
    ("cell_min_ns", _within),
    ("cell_max_ns", _within),
    ("first_latency_ns", _no_later),
    ("before_lead_in", _at_most),
    ("before_program", _at_most),
    ("during_play", _at_most),
    ("during_reread", _at_most),
    ("second_window", _at_most),
    ("data_driven_outside_ns", _at_most),
    ("gate_driven_ns", _at_most),
)


def check_ours(scenario: str, ours: Metrics, proven: list[Metrics]) -> list[Finding]:
    """Apply every rule to ours in one scenario."""
    findings = []
    if proven:
        accepted = any(m.would_accept for m in proven)
        ok = ours.would_accept or not accepted
        verdict = Verdict.MET if ok else Verdict.BROKEN
    else:
        verdict = Verdict.UNCHECKED
    findings.append(
        Finding(scenario, "would_accept", verdict, ours.would_accept, None, None)
    )
    for metric, rule in RULES:
        values = [getattr(m, metric) for m in proven if getattr(m, metric) is not None]
        verdict, low, high = rule(getattr(ours, metric), values)
        if metric in CELL_RULES and ours.valid == 0 and verdict is Verdict.BROKEN:
            verdict = Verdict.MET
        findings.append(
            Finding(scenario, metric, verdict, getattr(ours, metric), low, high)
        )
    return findings
