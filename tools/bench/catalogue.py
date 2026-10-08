# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Every scenario the bench runs, in one place: the plain console situations,
the edge cases that reach what those leave dark, and the sense-line walks."""

from tools.bench.edge_scenarios import EDGE_SCENARIOS
from tools.bench.scenarios import SCENARIOS, Scenario
from tools.bench.sense_scenarios import SENSE_SCENARIOS

CATALOGUE = SCENARIOS + EDGE_SCENARIOS + SENSE_SCENARIOS


def find(name: str) -> Scenario:
    """Find any scenario by name; an unknown one raises KeyError."""
    for scenario in CATALOGUE:
        if scenario.name == name:
            return scenario
    raise KeyError(name)
