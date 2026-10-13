# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""What one chip did in one scenario, as numbers the console would care about.

Strings belong to the phase they start in. A string is accepted only if it is
the expected region and ends before the program area. Drive time outside
strings is what a detector could see. Play resumes three ways: after a swap's
second lead-in, after a re-read and after the anti-mod probe; strings there are
second_play, and strings while the drive is stopped for a swap are during_swap.
reread_valid and second_window_valid count only strings of the expected region,
since a garbled string re-authenticates nothing.
"""

from collections.abc import Mapping
from dataclasses import dataclass

from tools.bench.decode import decode
from tools.bench.scenarios import Phase
from tools.bench.scex import Region
from tools.bench.trace import Trace, driven_ns, pulled_ns


@dataclass(frozen=True, slots=True)
class Metrics:
    """One chip's run of one scenario."""

    strings: int
    valid: int
    garbled: int
    before_lead_in: int
    before_program: int
    during_play: int
    during_reread: int
    second_window: int
    would_accept: bool
    first_latency_ns: int | None
    cell_min_ns: int | None
    cell_max_ns: int | None
    spread_max_ns: int | None
    carrier: bool | None
    data_driven_outside_ns: int
    gate_driven_ns: int
    probe_strings: int = 0
    reread_valid: int = 0
    gate_driven_outside_ns: int = 0
    pulled_ns: int = 0
    second_play: int = 0
    during_swap: int = 0
    second_window_valid: int = 0


def _segment(start_ns: int, phases: Mapping[Phase, int]) -> Phase | None:
    """The phase a time falls in, or None before the first mark."""
    current = None
    for phase, mark in sorted(phases.items(), key=lambda item: item[1]):
        if start_ns >= mark:
            current = phase
    return current


# Gate drive is judged against each string widened by one 4 ms bit cell on each
# side (PsNee V9.0 PSNee.ino:587 and :603), which covers the gate's set-up and
# release.
GATE_MARGIN_NS = 4_000_000


def framed(spans: list[tuple[int, int]]) -> list[tuple[int, int]]:
    """Each string's span widened by the gate margin, overlaps merged."""
    merged: list[tuple[int, int]] = []
    for start, end in sorted(spans):
        low, high = start - GATE_MARGIN_NS, end + GATE_MARGIN_NS
        if merged and low <= merged[-1][1]:
            merged = [*merged[:-1], (merged[-1][0], max(merged[-1][1], high))]
        else:
            merged = [*merged, (low, high)]
    return merged


def measure(trace: Trace, phases: Mapping[Phase, int], region: Region) -> Metrics:
    """Measure one run against its scenario's phase marks."""
    strings = decode(trace.data, trace.end_ns)
    valid = [s for s in strings if s.region is region]
    counts = dict.fromkeys(Phase, 0)
    before_lead_in = 0
    for string in strings:
        phase = _segment(string.start_ns, phases)
        if phase is None:
            before_lead_in += 1
        else:
            counts[phase] += 1
    lead_in = phases[Phase.LEAD_IN]
    program = phases.get(Phase.PROGRAM)
    accepting = [
        s
        for s in valid
        if s.start_ns >= lead_in and program is not None and s.end_ns <= program
    ]
    cells = [s.cell_ns for s in strings]
    spans = [(s.start_ns, s.end_ns) for s in strings]
    return Metrics(
        strings=len(strings),
        valid=len(valid),
        garbled=sum(1 for s in strings if s.region is None),
        before_lead_in=before_lead_in,
        before_program=counts[Phase.LEAD_IN],
        during_play=counts[Phase.PROGRAM],
        during_reread=counts[Phase.REREAD],
        second_window=counts[Phase.SECOND_LEAD_IN],
        would_accept=bool(accepting),
        first_latency_ns=accepting[0].start_ns - lead_in if accepting else None,
        cell_min_ns=min(cells) if cells else None,
        cell_max_ns=max(cells) if cells else None,
        spread_max_ns=max(s.cell_spread_ns for s in strings) if strings else None,
        carrier=strings[0].carrier if strings else None,
        data_driven_outside_ns=driven_ns(trace.data, trace.end_ns, spans),
        gate_driven_ns=driven_ns(trace.gate, trace.end_ns, []),
        probe_strings=counts[Phase.PROBE],
        gate_driven_outside_ns=driven_ns(trace.gate, trace.end_ns, framed(spans)),
        pulled_ns=pulled_ns(trace),
        reread_valid=sum(
            1 for s in valid if _segment(s.start_ns, phases) is Phase.REREAD
        ),
        second_window_valid=sum(
            1 for s in valid if _segment(s.start_ns, phases) is Phase.SECOND_LEAD_IN
        ),
        second_play=counts[Phase.SECOND_PROGRAM] + counts[Phase.AFTER_PROBE],
        during_swap=counts[Phase.SWAP],
    )
