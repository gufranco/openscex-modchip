# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Tests for the conventional-commit subject check CI runs on every push.

Each case runs the script on a throwaway repository with real commits, since
the risk is in the git range, such as a force-pushed base that is no ancestor
of the new head.
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

    def check_message(self, subject: str) -> int:
        message = self.repo / "MSG"
        message.write_text(f"{subject}\n\n# a comment git strips\n")
        return subprocess.run(
            ["sh", str(SCRIPT), "--message-file", str(message)],
            cwd=self.repo,
            check=False,
            capture_output=True,
            text=True,
        ).returncode

    def commit_as(self, email: str, subject: str) -> str:
        self.git(
            "-c",
            f"user.email={email}",
            "commit",
            "-q",
            "--allow-empty",
            "-m",
            subject,
        )
        return self.git("rev-parse", "HEAD")

    def test_a_fifty_character_subject_passes(self) -> None:
        # Arrange
        subject = "fix: " + "a" * 45

        # Act
        code = self.check_message(subject)

        # Assert
        self.assertEqual((len(subject), code), (50, 0))

    def test_a_fifty_one_character_subject_fails(self) -> None:
        # Arrange
        subject = "fix: " + "a" * 46

        # Act
        code = self.check_message(subject)

        # Assert
        self.assertEqual((len(subject), code), (51, 1))

    def test_a_subject_starting_with_a_capital_fails(self) -> None:
        # Arrange
        subject = "fix: Keep the burst through a dip"

        # Act
        code = self.check_message(subject)

        # Assert
        self.assertEqual(code, 1)

    def test_a_type_outside_the_list_fails(self) -> None:
        # Arrange
        subject = "feature: keep the burst through a dip"

        # Act
        code = self.check_message(subject)

        # Assert
        self.assertEqual(code, 1)

    def test_a_lowercase_scope_passes_and_a_capital_one_fails(self) -> None:
        # Arrange
        good, bad = "fix(sim-test): keep a burst", "fix(Sim): keep a burst"

        # Act
        codes = (self.check_message(good), self.check_message(bad))

        # Assert
        self.assertEqual(codes, (0, 1))

    def test_bot_commits_in_the_range_are_skipped(self) -> None:
        # Arrange
        base = self.commit(GOOD)
        self.commit_as("49699333+dependabot[bot]@users.noreply.github.com", BAD)
        head = self.commit_as("semantic-release-bot@martynus.net", BAD)

        # Act
        code = self.check(base, head)

        # Assert
        self.assertEqual(code, 0)

    def test_a_merge_commit_in_the_range_is_skipped(self) -> None:
        # Arrange
        base = self.commit(GOOD)
        self.git("checkout", "-q", "-b", "side")
        self.commit(GOOD)
        self.git("checkout", "-q", "main")
        self.commit(GOOD)
        self.git("merge", "-q", "--no-ff", "-m", BAD, "side")
        head = self.git("rev-parse", "HEAD")

        # Act
        code = self.check(base, head)

        # Assert
        self.assertEqual(code, 0)

    def test_a_wrong_argument_count_exits_with_usage(self) -> None:
        # Act
        code = subprocess.run(
            ["sh", str(SCRIPT), "only-one"],
            cwd=self.repo,
            check=False,
            capture_output=True,
            text=True,
        ).returncode

        # Assert
        self.assertEqual(code, 2)

    def test_a_subject_naming_a_version_passes(self) -> None:
        # Arrange
        subject = "build: bump ruff to 0.17.0"

        # Act
        code = self.check_message(subject)

        # Assert
        self.assertEqual(code, 0)

    def test_a_subject_ending_in_a_period_fails(self) -> None:
        # Arrange
        subject = "fix: keep the burst through a dip."

        # Act
        code = self.check_message(subject)

        # Assert
        self.assertEqual(code, 1)

    def test_a_one_letter_subject_passes(self) -> None:
        # Arrange
        subject = "docs: a"

        # Act
        code = self.check_message(subject)

        # Assert
        self.assertEqual(code, 0)

    def test_a_range_of_good_subjects_passes(self) -> None:
        # Arrange
        base = self.commit(GOOD)
        head = self.commit(GOOD)

        # Act
        code = self.check(base, head)

        # Assert
        self.assertEqual(code, 0)

    def test_a_bad_subject_in_the_range_fails(self) -> None:
        # Arrange
        base = self.commit(GOOD)
        self.commit(BAD)
        head = self.commit(GOOD)

        # Act
        code = self.check(base, head)

        # Assert
        self.assertEqual(code, 1)

    def test_a_new_branch_checks_only_its_head(self) -> None:
        # Arrange
        self.commit(BAD)
        head = self.commit(GOOD)

        # Act
        code = self.check(ZERO_SHA, head)

        # Assert
        self.assertEqual(code, 0)

    def test_a_force_push_checks_only_the_new_head(self) -> None:
        # Arrange
        old_tip = self.commit(GOOD)
        self.git("checkout", "-q", "--orphan", "rewritten")
        self.commit(BAD)
        head = self.commit(GOOD)

        # Act
        code = self.check(old_tip, head)

        # Assert
        self.assertEqual(code, 0)

    def test_an_unfetched_base_checks_only_the_head(self) -> None:
        # Arrange
        self.commit(BAD)
        head = self.commit(GOOD)

        # Act
        code = self.check("1" * 40, head)

        # Assert
        self.assertEqual(code, 0)

    def test_a_force_push_still_rejects_a_bad_head(self) -> None:
        # Arrange
        old_tip = self.commit(GOOD)
        self.git("checkout", "-q", "--orphan", "rewritten")
        head = self.commit(BAD)

        # Act
        code = self.check(old_tip, head)

        # Assert
        self.assertEqual(code, 1)


if __name__ == "__main__":
    unittest.main()
