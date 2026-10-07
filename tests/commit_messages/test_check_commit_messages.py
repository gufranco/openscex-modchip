# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Tests for the conventional-commit subject check CI runs on every push.

Each case builds a throwaway repository with real commits and runs the shell
script against it, because the defects worth catching live in how the script
asks git for the range, not in the subject pattern alone. A push event hands
the script the branch's old tip as the base; after a force push that tip is not
an ancestor of the new head and is usually not even fetched, which once made
git exit 128 and turned CI red until the next push.
"""

import subprocess
import tempfile
import unittest
from pathlib import Path

SCRIPT = (
    Path(__file__).resolve().parents[2]
    / ".github"
    / "scripts"
    / "check-commit-messages.sh"
)
ZERO_SHA = "0" * 40
GOOD = "fix: keep the burst through a dip"
BAD = "Fixed stuff."


class CommitMessageCheckTest(unittest.TestCase):
    def setUp(self) -> None:
        self._dir = tempfile.TemporaryDirectory()
        self.repo = Path(self._dir.name)
        self.git("init", "-q", "-b", "main")

    def tearDown(self) -> None:
        self._dir.cleanup()

    def git(self, *args: str) -> str:
        return subprocess.run(
            [
                "git",
                "-c",
                "user.name=Test",
                "-c",
                "user.email=test@example.invalid",
                "-c",
                "commit.gpgsign=false",
                *args,
            ],
            cwd=self.repo,
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip()

    def commit(self, subject: str) -> str:
        self.git("commit", "-q", "--allow-empty", "-m", subject)
        return self.git("rev-parse", "HEAD")

    def check(self, base: str, head: str) -> int:
        return subprocess.run(
            ["sh", str(SCRIPT), base, head],
            cwd=self.repo,
            check=False,
            capture_output=True,
            text=True,
        ).returncode

    def test_a_range_of_good_subjects_passes(self) -> None:
        base = self.commit(GOOD)
        head = self.commit(GOOD)

        code = self.check(base, head)

        self.assertEqual(code, 0)

    def test_a_bad_subject_in_the_range_fails(self) -> None:
        base = self.commit(GOOD)
        self.commit(BAD)
        head = self.commit(GOOD)

        code = self.check(base, head)

        self.assertEqual(code, 1)

    def test_a_new_branch_checks_only_its_head(self) -> None:
        self.commit(BAD)
        head = self.commit(GOOD)

        code = self.check(ZERO_SHA, head)

        self.assertEqual(code, 0)

    def test_a_force_push_checks_only_the_new_head(self) -> None:
        old_tip = self.commit(GOOD)
        self.git("checkout", "-q", "--orphan", "rewritten")
        self.commit(BAD)
        head = self.commit(GOOD)

        code = self.check(old_tip, head)

        self.assertEqual(code, 0)

    def test_an_unfetched_base_checks_only_the_head(self) -> None:
        self.commit(BAD)
        head = self.commit(GOOD)

        code = self.check("1" * 40, head)

        self.assertEqual(code, 0)

    def test_a_force_push_still_rejects_a_bad_head(self) -> None:
        old_tip = self.commit(GOOD)
        self.git("checkout", "-q", "--orphan", "rewritten")
        head = self.commit(BAD)

        code = self.check(old_tip, head)

        self.assertEqual(code, 1)


if __name__ == "__main__":
    unittest.main()
