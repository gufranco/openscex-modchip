# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Ours against the envelope of the chips real consoles have accepted.

Per scenario, the field-proven chips' metrics bound ours:

- acceptance: ours accepts wherever any of them does;
- bit cell: within their range, give or take one 14.6 kHz WFCK period, 68.5 us;
- first string: no later than the slowest, give or take two SUBQ frames,
  26.7 ms, for sampling phase and a frame ours can miss between captures;
- strings sent, in every phase including play after a swap, a re-read or the
  anti-mod probe, and drive outside strings: at most their maximum.

A rule with nothing to compare against is unchecked, never passed. Ours sending
no string at all has no bit cell, which meets the cell rules; a garbled string
still has one, and is judged.
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
    ("second_play", _at_most),
    ("during_swap", _at_most),
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
        if metric in CELL_RULES and ours.strings == 0 and verdict is Verdict.BROKEN:
            verdict = Verdict.MET
        findings.append(
            Finding(scenario, metric, verdict, getattr(ours, metric), low, high)
        )
    return findings
