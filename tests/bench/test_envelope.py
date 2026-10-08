# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Tests for checking ours against the field-proven chips' envelope."""

import unittest
from dataclasses import replace

from tools.bench.envelope import (
    CELL_TOLERANCE_NS,
    LATENCY_TOLERANCE_NS,
    Verdict,
    check_ours,
)
from tools.bench.metrics import Metrics
from tools.bench.timeline import FRAME_NS

BASE = Metrics(
    strings=4,
    valid=4,
    garbled=0,
    before_lead_in=0,
    before_program=4,
    during_play=0,
    during_reread=0,
    second_window=0,
    would_accept=True,
    first_latency_ns=1_000_000_000,
    cell_min_ns=4_000_000,
    cell_max_ns=4_100_000,
    spread_max_ns=10_000,
    carrier=True,
    data_driven_outside_ns=0,
    gate_driven_ns=0,
)


def verdicts(ours: Metrics, proven: list[Metrics]) -> dict[str, Verdict]:
    return {f.metric: f.verdict for f in check_ours("s", ours, proven)}


class EnvelopeTest(unittest.TestCase):
    def test_identical_behaviour_passes_every_rule(self) -> None:
        result = verdicts(BASE, [BASE])

        self.assertEqual(set(result.values()), {Verdict.MET})

    def test_not_accepting_where_a_proven_chip_accepts_fails(self) -> None:
        ours = replace(BASE, would_accept=False)

        result = verdicts(ours, [BASE])

        self.assertIs(result["would_accept"], Verdict.BROKEN)

    def test_not_accepting_where_none_accepts_passes(self) -> None:
        proven = replace(BASE, would_accept=False)

        result = verdicts(replace(BASE, would_accept=False), [proven])

        self.assertIs(result["would_accept"], Verdict.MET)

    def test_a_cell_outside_the_proven_range_fails(self) -> None:
        ours = replace(BASE, cell_max_ns=4_500_000)

        result = verdicts(ours, [BASE, replace(BASE, cell_max_ns=4_200_000)])

        self.assertIs(result["cell_max_ns"], Verdict.BROKEN)

    def test_fewer_strings_in_play_is_allowed_more_is_not(self) -> None:
        proven = [replace(BASE, during_play=3)]

        fewer = verdicts(replace(BASE, during_play=0), proven)
        more = verdicts(replace(BASE, during_play=5), proven)

        self.assertIs(fewer["during_play"], Verdict.MET)
        self.assertIs(more["during_play"], Verdict.BROKEN)

    def test_a_later_first_string_than_the_slowest_proven_fails(self) -> None:
        ours = replace(BASE, first_latency_ns=3_000_000_000)

        result = verdicts(ours, [BASE])

        self.assertIs(result["first_latency_ns"], Verdict.BROKEN)

    def test_the_latency_tolerance_is_two_subq_frames(self) -> None:
        frames = LATENCY_TOLERANCE_NS / FRAME_NS

        self.assertEqual(frames, 2)

    def test_a_first_string_within_two_frames_of_the_slowest_meets(self) -> None:
        late = BASE.first_latency_ns + LATENCY_TOLERANCE_NS
        ours = replace(BASE, first_latency_ns=late)

        result = verdicts(ours, [BASE])

        self.assertIs(result["first_latency_ns"], Verdict.MET)

    def test_a_first_string_beyond_two_frames_of_the_slowest_breaks(self) -> None:
        late = BASE.first_latency_ns + LATENCY_TOLERANCE_NS + 1
        ours = replace(BASE, first_latency_ns=late)

        result = verdicts(ours, [BASE])

        self.assertIs(result["first_latency_ns"], Verdict.BROKEN)

    def test_the_frame_tolerance_does_not_loosen_the_string_counts(self) -> None:
        ours = replace(BASE, during_play=1)

        result = verdicts(ours, [BASE])

        self.assertIs(result["during_play"], Verdict.BROKEN)

    def test_no_proven_value_leaves_the_rule_unchecked(self) -> None:
        proven = replace(
            BASE, cell_min_ns=None, cell_max_ns=None, first_latency_ns=None
        )

        result = verdicts(BASE, [proven])

        self.assertIs(result["cell_min_ns"], Verdict.UNCHECKED)
        self.assertIs(result["first_latency_ns"], Verdict.UNCHECKED)

    def test_ours_without_a_value_breaks_a_range_rule(self) -> None:
        ours = replace(BASE, cell_min_ns=None)

        result = verdicts(ours, [BASE])

        self.assertIs(result["cell_min_ns"], Verdict.BROKEN)

    def test_sending_nothing_meets_the_cell_rules(self) -> None:
        ours = replace(BASE, valid=0, strings=0, cell_min_ns=None, cell_max_ns=None)

        result = verdicts(ours, [BASE])

        self.assertIs(result["cell_min_ns"], Verdict.MET)
        self.assertIs(result["cell_max_ns"], Verdict.MET)

    def test_a_cell_within_one_carrier_period_of_the_range_meets_it(self) -> None:
        ours = replace(BASE, cell_min_ns=BASE.cell_min_ns - CELL_TOLERANCE_NS)

        result = verdicts(ours, [BASE])

        self.assertIs(result["cell_min_ns"], Verdict.MET)

    def test_a_cell_beyond_one_carrier_period_breaks_the_range(self) -> None:
        ours = replace(BASE, cell_min_ns=BASE.cell_min_ns - CELL_TOLERANCE_NS - 1)

        result = verdicts(ours, [BASE])

        self.assertIs(result["cell_min_ns"], Verdict.BROKEN)

    def test_no_proven_chip_at_all_leaves_everything_unchecked(self) -> None:
        result = verdicts(BASE, [])

        self.assertEqual(set(result.values()), {Verdict.UNCHECKED})
