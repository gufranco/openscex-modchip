# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Tests for the container build wrapper.

Docker is never invoked: subprocess is mocked so the tag hashing, the run argv,
the build-only-when-absent logic, and the exit-code passthrough are checked
without a daemon.
"""

import subprocess
import unittest
from pathlib import Path
from unittest import mock

from tools import docker_make


class ImageTagTest(unittest.TestCase):
    def test_tag_is_name_and_twelve_hex_of_input_digest(self) -> None:
        root = docker_make.ROOT

        tag = docker_make.image_tag(root)

        name, _, digest = tag.partition(":")
        self.assertEqual(name, docker_make.IMAGE_NAME)
        self.assertEqual(len(digest), docker_make.TAG_LENGTH)
        self.assertTrue(all(character in "0123456789abcdef" for character in digest))

    def test_tag_changes_when_an_input_changes(self) -> None:
        root = docker_make.ROOT
        original = (root / "Dockerfile").read_bytes()

        with mock.patch.object(Path, "read_bytes", return_value=original + b"x"):
            changed = docker_make.image_tag(root)

        self.assertNotEqual(changed, docker_make.image_tag(root))


class ToolchainCommandTest(unittest.TestCase):
    def test_command_mounts_root_and_runs_make_targets(self) -> None:
        command = docker_make.toolchain_command(
            Path("/repo"), "img:abc", (501, 20), ["analyse", "hosttest"]
        )

        self.assertEqual(command[:3], ["docker", "run", "--rm"])
        self.assertIn("501:20", command)
        self.assertIn("/repo:/src", command)
        self.assertEqual(command[-3:], ["make", "analyse", "hosttest"])


class EnsureImageTest(unittest.TestCase):
    def test_builds_when_image_absent(self) -> None:
        calls: list[list[str]] = []

        def fake_run(
            args: list[str], **_kwargs: object
        ) -> subprocess.CompletedProcess[str]:
            calls.append(args)
            code = 1 if args[:3] == ["docker", "image", "inspect"] else 0
            return subprocess.CompletedProcess(args, code)

        with mock.patch.object(docker_make.subprocess, "run", side_effect=fake_run):
            docker_make.ensure_image(Path("/repo"), "img:abc")

        self.assertEqual(calls[-1][:3], ["docker", "build", "-t"])

    def test_skips_build_when_image_present(self) -> None:
        calls: list[list[str]] = []

        def fake_run(
            args: list[str], **_kwargs: object
        ) -> subprocess.CompletedProcess[str]:
            calls.append(args)
            return subprocess.CompletedProcess(args, 0)

        with mock.patch.object(docker_make.subprocess, "run", side_effect=fake_run):
            docker_make.ensure_image(Path("/repo"), "img:abc")

        self.assertEqual(len(calls), 1)
        self.assertEqual(calls[0][:3], ["docker", "image", "inspect"])


class MainTest(unittest.TestCase):
    def test_main_ensures_image_then_runs_targets(self) -> None:
        with (
            mock.patch.object(docker_make, "ensure_image") as ensure,
            mock.patch.object(
                docker_make.subprocess,
                "run",
                return_value=subprocess.CompletedProcess([], 7),
            ) as run,
        ):
            code = docker_make.main(["docker_make.py", "hosttest"])

        self.assertEqual(code, 7)
        ensure.assert_called_once()
        run.assert_called_once()


if __name__ == "__main__":
    unittest.main()
