# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Every claim in the three READMEs links to its source (AGENTS rule 15).

A claim is a table row past the header, a list item, a paragraph line
outside code and headings, or a line of text inside an HTML block other than
a heading; each needs a Markdown link or an href.
A link into the tree must name a file that exists, and a link to a file on
GitHub must be a permalink pinned to a 40-digit commit.
"""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
READMES = ("README.md", "README.ja.md", "README.zh.md")
LINK = re.compile(r"\]\([^)]+\)|href=\"[^\"]+\"")
SEPARATOR = re.compile(r"^\|(\s*:?-+:?\s*\|)+$")
ALERT = re.compile(r"^> \[!\w+\]$")
# Block tags that open and close an HTML region; a line may hold both.
OPEN_BLOCK = re.compile(r"<(div|p|table)[\s>]")
CLOSE_BLOCK = re.compile(r"</(div|p|table)>")
TAG = re.compile(r"<[^>]*>")
HEADING_TAG = re.compile(r"^<h[1-6][\s>]")
TARGET = re.compile(r"\]\(([^)\s]+)[^)]*\)|href=\"([^\"]+)\"")
# GitHub file links must pin a commit, so the cited line cannot move under them.
GITHUB_FILE = re.compile(r"https://github\.com/[^/]+/[^/]+/(blob|tree)/([^/]+)/")
PINNED = re.compile(r"[0-9a-f]{40}")


def claims(text: str) -> list[tuple[int, str]]:
    """The lines of a README that state something, with their line numbers."""
    found: list[tuple[int, str]] = []
    in_code = False
    depth = 0
    header_next = True
    for number, raw in enumerate(text.splitlines(), start=1):
        line = raw.strip()
        if line.startswith("```"):
            in_code = not in_code
            continue
        opened = len(OPEN_BLOCK.findall(line)) if not in_code else 0
        closed = len(CLOSE_BLOCK.findall(line)) if not in_code else 0
        inside = depth > 0 or opened > 0
        depth = max(depth + opened - closed, 0)
        if inside and (HEADING_TAG.match(line) or not TAG.sub("", line).strip()):
            continue
        if (
            in_code
            or not line
            or line.startswith(("#", "<!--"))
            or (line.startswith("<") and not TAG.sub("", line).strip())
            or ALERT.match(line)
        ):
            header_next = True
            continue
        if line.startswith("|"):
            if SEPARATOR.match(line):
                continue
            if header_next:
                header_next = False
                continue
        else:
            header_next = True
        found.append((number, line))
    return found


def unsourced(text: str) -> list[int]:
    """Line numbers of claims that carry no link."""
    return [number for number, line in claims(text) if not LINK.search(line)]


def bad_targets(text: str, base: Path) -> list[str]:
    """Link targets that cannot be a source: a path into the tree that does
    not exist, or a GitHub file link that does not pin a commit. Routes into
    the GitHub UI, such as ../../releases, and anchors are not files."""
    bad: list[str] = []
    for match in TARGET.finditer(text):
        target = match.group(1) or match.group(2)
        github = GITHUB_FILE.match(target)
        if github is not None:
            if not PINNED.fullmatch(github.group(2)):
                bad.append(target)
            continue
        local = target.split("#")[0]
        if re.match(r"[a-z]+:", target) or not local or local.startswith("../../"):
            continue
        if not (base / local).exists():
            bad.append(target)
    return bad


class ClaimsTest(unittest.TestCase):
    def test_a_table_header_and_its_separator_are_not_claims(self) -> None:
        # Arrange
        text = "| a | b |\n|:--|:--|\n| row | [x](y) |\n"

        # Act
        found = claims(text)

        # Assert
        self.assertEqual(found, [(3, "| row | [x](y) |")])

    def test_code_bare_tags_and_headings_are_not_claims(self) -> None:
        # Arrange
        text = "# Title\n```\nplain\n```\n<div>\n<br>\n</div>\nfact\n"

        # Act
        found = claims(text)

        # Assert
        self.assertEqual(found, [(8, "fact")])

    def test_a_claim_without_a_link_is_reported(self) -> None:
        # Arrange
        text = "first [a](b)\n\nsecond with none\n"

        # Act
        missing = unsourced(text)

        # Assert
        self.assertEqual(missing, [3])

    def test_an_empty_header_row_is_a_header_not_a_separator(self) -> None:
        # Arrange
        text = "| | |\n|:--|:--|\n| cell | [x](y) |\n"

        # Act
        found = claims(text)

        # Assert
        self.assertEqual(found, [(3, "| cell | [x](y) |")])

    def test_an_alert_marker_is_not_a_claim(self) -> None:
        # Arrange
        text = "> [!IMPORTANT]\n> fact [a](b)\n"

        # Act
        found = claims(text)

        # Assert
        self.assertEqual(found, [(2, "> fact [a](b)")])

    def test_a_one_line_html_block_does_not_hide_the_lines_after_it(self) -> None:
        # Arrange
        text = '<p align="center"><img src="a.png"></p>\nunsourced claim\n'

        # Act
        missing = unsourced(text)

        # Assert
        self.assertEqual(missing, [2])

    def test_text_inside_html_blocks_is_a_claim_but_a_heading_is_not(self) -> None:
        # Arrange
        text = (
            "<div>\n<h1>Name</h1>\n<p>\n<strong>tagline</strong>\n</p>\n</div>\nfact\n"
        )

        # Act
        found = claims(text)

        # Assert
        self.assertEqual(found, [(4, "<strong>tagline</strong>"), (7, "fact")])

    def test_inline_html_with_text_is_a_claim(self) -> None:
        # Arrange
        text = "<b>unsourced</b>\n<br>\n"

        # Act
        missing = unsourced(text)

        # Assert
        self.assertEqual(missing, [1])


class TargetsTest(unittest.TestCase):
    def test_a_missing_file_is_a_bad_target(self) -> None:
        # Arrange
        text = "[a](src/region.c) [b](src/absent.c) [c](../../releases) [d](#top)"

        # Act
        bad = bad_targets(text, ROOT)

        # Assert
        self.assertEqual(bad, ["src/absent.c"])

    def test_a_github_file_link_must_pin_a_commit(self) -> None:
        # Arrange
        sha = "6d91e5d1859700000000000000000000000000ab"
        text = (
            f"[a](https://github.com/o/r/blob/{sha}/f.c#L1) "
            "[b](https://github.com/o/r/blob/main/f.c)"
        )

        # Act
        bad = bad_targets(text, ROOT)

        # Assert
        self.assertEqual(bad, ["https://github.com/o/r/blob/main/f.c"])


class ReadmeTest(unittest.TestCase):
    def test_every_claim_in_every_readme_links_to_its_source(self) -> None:
        # Act
        missing = {name: unsourced((ROOT / name).read_text()) for name in READMES}

        # Assert
        self.assertEqual({k: v for k, v in missing.items() if v}, {})

    def test_every_readme_link_resolves_or_pins_a_commit(self) -> None:
        # Act
        bad = {name: bad_targets((ROOT / name).read_text(), ROOT) for name in READMES}

        # Assert
        self.assertEqual({k: v for k, v in bad.items() if v}, {})
