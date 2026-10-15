# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""The showcase: named properties every chip is judged on, side by side.

A property holds for a chip only if it holds in every scenario standing for a
board the chip is built for; a chip built for none is not applicable. Ours must
hold every property. Results go to a JSON file, from which the README tables
are generated between markers. The README wording per language lives in TEXT.
"""

import argparse
import json
import sys
from collections.abc import Callable, Mapping, Sequence
from dataclasses import dataclass
from enum import StrEnum
from functools import cache
from pathlib import Path

from tools.bench.chips import CHIPS
from tools.bench.metrics import Metrics
from tools.bench.run import Run
from tools.bench.shapes import mapping, texts

START = "<!-- showcase:start -->"
END = "<!-- showcase:end -->"
OURS = "ours"
# The README names this firmware by the project's name.
OURS_SHOWN = "openscex"
RESULTS = "bench/showcase-results.json"
TEXT = Path(__file__).resolve().parents[2] / "bench" / "showcase-text.json"
READMES = {"en": "README.md", "ja": "README.ja.md", "zh": "README.zh.md"}


class Outcome(StrEnum):
    """One chip's result on one property."""

    HELD = "pass"
    FAILED = "fail"
    NA = "n/a"


@dataclass(frozen=True, slots=True)
class Property:
    """A behaviour judged over named scenarios, with its sources as (label,
    link) pairs; its text per language is in TEXT under its key."""

    key: str
    sources: tuple[tuple[str, str], ...]
    scenarios: tuple[str, ...]
    holds: Callable[[Metrics], bool]


@dataclass(frozen=True, slots=True)
class Words:
    """TEXT as read: per language, the table's words and each property's title
    and mechanism."""

    heading: Mapping[str, str]
    source: Mapping[str, str]
    stop: Mapping[str, str]
    colon: Mapping[str, str]
    outcome: Mapping[str, Mapping[str, str]]
    properties: Mapping[str, Mapping[str, Mapping[str, str]]]

    def said(self, key: str, field: str, lang: str) -> str:
        """A property's title or mechanism in one language."""
        return self.properties[key][field][lang]


_SPX = (
    "https://github.com/psx-spx/psx-spx.github.io/blob/"
    "6d7d1bc106a7e0b616b0330fe58401ab1ba57f0f/docs/"
)
_TONYHAX = (
    "https://github.com/socram8888/tonyhax/blob/"
    "6c9d18ccbdfd3ffc3dc5f0eb373a50600199e208/docs/ap_v2.c#L225-L285"
)
_APRIP = (
    "https://github.com/alex-free/aprip/blob/"
    "767fa1ded63076e2380822986120170272420443/readme.md#apv2"
)
_PROBE = ("psx-spx cdromformat.md, anti-modchip", _SPX + "cdromformat.md#L1624-L1646")
_COUNTERS = ("psx-spx cdromdrive.md, 19h,04h", _SPX + "cdromdrive.md#L1211-L1230")
_APV2 = ("tonyhax docs/ap_v2.c", _TONYHAX)
_READTOC = ("aprip readme, APv2", _APRIP)
_FLOAT = ("quade.co PsNee guide", "https://quade.co/ps1-modchip-guide/psnee/")
_PULLS = ("bench runners", "bench")
_EDGE = ("tools/bench/edge_scenarios.py", "tools/bench/edge_scenarios.py")
_CORE_FILE = ("tools/bench/scenarios.py", "tools/bench/scenarios.py")

_ANTIMOD_V1 = ("antimod-v1-carrier", "antimod-v1-gate-early", "antimod-v1-gate")
_ANTIMOD_V2 = ("antimod-v2-carrier", "antimod-v2-gate-early", "antimod-v2-gate")
_CORE = ("carrier-accept", "carrier-double-speed", "gate-accept-early", "gate-accept")


def _pins_float(m: Metrics) -> bool:
    return (
        m.data_driven_outside_ns == 0
        and m.gate_driven_outside_ns == 0
        and m.pulled_ns == 0
    )


PROPERTIES = (
    Property(
        "antimod-v1",
        (_PROBE,),
        _ANTIMOD_V1,
        lambda m: m.probe_strings == 0,
    ),
    Property(
        "antimod-v2-reauth",
        (_APV2, _READTOC),
        _ANTIMOD_V2,
        lambda m: m.reread_valid >= 1,
    ),
    Property(
        "antimod-v2",
        (_APV2, _PROBE),
        _ANTIMOD_V2,
        lambda m: m.probe_strings == 0,
    ),
    Property(
        "silent-in-play",
        (_COUNTERS,),
        _CORE,
        lambda m: m.during_play == 0,
    ),
    Property(
        "pins-float",
        (_FLOAT, _PULLS),
        _CORE + _ANTIMOD_V1 + _ANTIMOD_V2,
        _pins_float,
    ),
    Property(
        "stuck-sqck",
        (_EDGE,),
        ("sqck-stuck",),
        lambda m: m.would_accept,
    ),
    Property(
        "watchdog",
        (_EDGE,),
        ("carrier-watchdog",),
        lambda m: m.would_accept and m.gate_driven_ns == 0,
    ),
    Property(
        "swap",
        (_CORE_FILE,),
        ("carrier-swap",),
        lambda m: m.second_window_valid >= 1,
    ),
)

Results = dict[str, dict[str, Outcome]]


def _per_language(value: object, where: str) -> dict[str, str]:
    said = texts(value, where)
    missing = sorted(set(READMES) - set(said))
    if missing:
        raise ValueError(f"{where}: no text for {missing}")
    return said


def parse_words(raw: object) -> Words:
    """TEXT's content checked: every phrase, outcome and property title and
    mechanism in every README language."""
    top = mapping(raw, "showcase text")
    outcome = mapping(top.get("outcome"), "outcome")
    properties = mapping(top.get("properties"), "properties")
    phrases = {
        key: _per_language(top.get(key), key)
        for key in ("heading", "source", "stop", "colon")
    }
    return Words(
        heading=phrases["heading"],
        source=phrases["source"],
        stop=phrases["stop"],
        colon=phrases["colon"],
        outcome={
            lang: _outcomes(outcome.get(lang), f"outcome.{lang}") for lang in READMES
        },
        properties={p.key: _property(properties.get(p.key), p.key) for p in PROPERTIES},
    )


def _property(value: object, key: str) -> dict[str, dict[str, str]]:
    said = mapping(value, key)
    return {
        field: _per_language(said.get(field), f"{key}.{field}")
        for field in ("title", "mechanism")
    }


def _outcomes(value: object, where: str) -> dict[str, str]:
    said = texts(value, where)
    missing = sorted({str(o) for o in Outcome} - set(said))
    if missing:
        raise ValueError(f"{where}: no text for {missing}")
    return said


@cache
def words() -> Words:
    """The README-facing words, read and checked once on first use."""
    return parse_words(json.loads(TEXT.read_text()))


def _judge(prop: Property, runs: Sequence[Run]) -> Outcome:
    relevant = [
        r
        for r in runs
        if r.scenario.name in prop.scenarios and r.scenario.boards & r.chip.boards
    ]
    if not relevant:
        return Outcome.NA
    held = all(prop.holds(r.metrics) for r in relevant)
    return Outcome.HELD if held else Outcome.FAILED


def evaluate(runs: Sequence[Run]) -> Results:
    """Every chip that ran, judged on every property, in catalogue order."""
    names = [c.name for c in CHIPS if any(r.chip.name == c.name for r in runs)]
    return {
        name: {
            p.key: _judge(p, [r for r in runs if r.chip.name == name])
            for p in PROPERTIES
        }
        for name in names
    }


def failures(results: Results) -> list[str]:
    """The properties ours does not hold, failed or never judged; an empty
    list is the only passing answer."""
    row = results.get(OURS, {})
    return [p.key for p in PROPERTIES if row.get(p.key, Outcome.NA) is not Outcome.HELD]


def to_json(results: Results) -> str:
    """The results as stable JSON: chips and properties in their own order."""
    return json.dumps(results, indent=2, ensure_ascii=False) + "\n"


def from_json(raw_text: str) -> Results:
    """Read results written by to_json; refuse an unknown property or
    outcome."""
    keys = {p.key for p in PROPERTIES}
    results: Results = {}
    for chip, row in mapping(json.loads(raw_text), "results").items():
        said = texts(row, chip)
        unknown = sorted(set(said) - keys)
        if unknown:
            raise ValueError(f"{chip}: unknown properties {unknown}")
        results[chip] = {k: Outcome(v) for k, v in said.items()}
    return results


def complete(results: Results) -> Results:
    """The results, when they hold every chip in the catalogue and every
    property for each; a partial run would drop README columns or rows."""
    names = [c.name for c in CHIPS]
    missing = [n for n in names if n not in results]
    unknown = sorted(set(results) - set(names))
    if missing or unknown:
        raise ValueError(f"chips missing {missing}, unknown {unknown}")
    for chip, row in results.items():
        gaps = [p.key for p in PROPERTIES if p.key not in row]
        if gaps:
            raise ValueError(f"{chip}: no outcome for {gaps}")
    return results


def table(results: Results, lang: str) -> list[str]:
    """A Markdown table: a row per property, a column per chip."""
    said = words()
    chips = list(results)
    outcome = said.outcome[lang]
    lines = [
        f"| {said.heading[lang]} | "
        + " | ".join(OURS_SHOWN if c == OURS else c for c in chips)
        + f" | {said.source[lang]} |",
        "|---|" + "---|" * len(chips) + "---|",
    ]
    lines.extend(
        f"| {said.said(p.key, 'title', lang)} | "
        + " | ".join(outcome[results[c].get(p.key, Outcome.NA)] for c in chips)
        + f" | {_links(p)} |"
        for p in PROPERTIES
    )
    return lines


def _links(prop: Property) -> str:
    return ", ".join(f"[{label}]({url})" for label, url in prop.sources)


def legend(lang: str) -> list[str]:
    """One line per property: its title, the mechanism it models, the source."""
    said = words()
    colon, stop = said.colon[lang], said.stop[lang]
    return [
        f"- **{said.said(p.key, 'title', lang)}**{colon}"
        f"{said.said(p.key, 'mechanism', lang)}{stop}"
        f"{said.source[lang]}{colon}{_links(p)}{stop.rstrip()}"
        for p in PROPERTIES
    ]


def rewrite(readme: str, lines: list[str]) -> str:
    """Replace what sits between the markers; refuse a text without them."""
    head, start, rest = readme.partition(START + "\n")
    _, end, tail = rest.partition(END)
    if not start or not end:
        raise ValueError(f"no {START} ... {END} block")
    return head + start + "\n".join(lines) + "\n" + end + tail


def block(results: Results, lang: str) -> list[str]:
    """What goes between the markers in one README: the table and its legend."""
    return [*table(results, lang), "", *legend(lang)]


def write_readmes(root: Path, results: Results) -> None:
    """Regenerate the table in every README from the results."""
    for lang, name in READMES.items():
        path = root / name
        path.write_text(rewrite(path.read_text(), block(results, lang)))


def main(argv: list[str]) -> int:
    """Copy a bench run's results into the tree and regenerate the tables."""
    parser = argparse.ArgumentParser(prog="python3 -m tools.bench.showcase")
    parser.add_argument("results", type=Path, help="showcase JSON from a bench run")
    parser.add_argument(
        "--root", type=Path, default=Path(__file__).resolve().parents[2]
    )
    args = parser.parse_args(argv[1:])
    results = complete(from_json(args.results.read_text()))
    (args.root / RESULTS).write_text(to_json(results))
    write_readmes(args.root, results)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
