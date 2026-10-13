# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Every claim in the three READMEs links to its source (AGENTS rule 15).

A claim is a table row past the header, a list item, or a paragraph line
outside code, HTML and headings; each needs a Markdown link or an href.
"""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
READMES = ("README.md", "README.ja.md", "README.zh.md")
LINK = re.compile(r"\]\([^)]+\)|href=\"[^\"]+\"")
SEPARATOR = re.compile(r"^\|(\s*:?-+:?\s*\|)+$")
ALERT = re.compile(r"^> \[!\w+\]$")


def claims(text: str) -> list[tuple[int, str]]:
    """The lines of a README that state something, with their line numbers."""
    found: list[tuple[int, str]] = []
    in_code = False
    in_html = False
    header_next = True
    for number, raw in enumerate(text.splitlines(), start=1):
        line = raw.strip()
        if line.startswith("```"):
            in_code = not in_code
            continue
        if line.startswith(("<div", "<p ", "<table")):
            in_html = True
        if in_html:
            in_html = not line.startswith(("</div>", "</p>", "</table>"))
            continue
        if (
            in_code
            or not line
            or line.startswith(("#", "<!--", "<"))
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


class ClaimsTest(unittest.TestCase):
    def test_a_table_header_and_its_separator_are_not_claims(self) -> None:
        # Arrange
        text = "| a | b |\n|:--|:--|\n| row | [x](y) |\n"

        # Act
        found = claims(text)

        # Assert
        self.assertEqual(found, [(3, "| row | [x](y) |")])

    def test_code_html_and_headings_are_not_claims(self) -> None:
        # Arrange
        text = "# Title\n```\nplain\n```\n<div>\nplain\n</div>\nfact\n"

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


class ReadmeTest(unittest.TestCase):
    def test_every_claim_in_every_readme_links_to_its_source(self) -> None:
        # Act
        missing = {name: unsourced((ROOT / name).read_text()) for name in READMES}

        # Assert
        self.assertEqual({k: v for k, v in missing.items() if v}, {})
