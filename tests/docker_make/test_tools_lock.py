# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""docker/tools.in and the hash lock compiled from it name the same versions.

Dependabot bumps the lock; a bump that leaves tools.in behind would make the
next recompile silently downgrade the tool, so the two are checked together.
"""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "docker" / "tools.in"
LOCK = ROOT / "docker" / "requirements-tools.txt"
PIN = re.compile(r"^([A-Za-z0-9][A-Za-z0-9._-]*)==([^\s;\\]+)", re.MULTILINE)


def pins(text: str) -> dict[str, str]:
    """Each `name==version` line, keyed by the normalised project name."""
    return {
        re.sub(r"[-_.]+", "-", name).lower(): version
        for name, version in PIN.findall(text)
    }


class PinsTest(unittest.TestCase):
    def test_names_are_normalised_and_markers_dropped(self) -> None:
        # Arrange
        text = (
            "Boolean.Py==5.0 \\\n"
            "librt==0.16.0 ; platform_python_implementation != 'PyPy'\n"
            "# x==1\n"
        )

        # Act
        found = pins(text)

        # Assert
        self.assertEqual(found, {"boolean-py": "5.0", "librt": "0.16.0"})


class ToolsLockTest(unittest.TestCase):
    def test_every_top_level_pin_is_the_locked_version(self) -> None:
        # Arrange
        locked = pins(LOCK.read_text())

        # Act
        wanted = pins(SOURCE.read_text())

        # Assert
        self.assertEqual(
            {name: locked.get(name) for name in wanted},
            wanted,
        )
