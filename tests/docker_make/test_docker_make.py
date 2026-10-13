# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Tests for the container build wrapper. FakeDocker stands in for the docker
CLI, which is unavailable inside the toolchain container."""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools import docker_make


class FakeDocker:
    """Records every docker command and answers like the CLI would: `present`
    is whether the image exists, `exit_code` what `docker run` returns."""

    def __init__(self, present: bool, exit_code: int = 0) -> None:
        self.present = present
        self.exit_code = exit_code
        self.calls: list[list[str]] = []

    def __call__(
        self, args: list[str], **_kwargs: object
    ) -> subprocess.CompletedProcess[bytes]:
        self.calls.append(args)
        if args[:3] == ["docker", "image", "inspect"]:
            return subprocess.CompletedProcess(args, 0 if self.present else 1)
        if args[:2] == ["docker", "run"]:
            return subprocess.CompletedProcess(args, self.exit_code)
        return subprocess.CompletedProcess(args, 0)


class ImageInputsTestCase(unittest.TestCase):
    def inputs(self) -> Path:
        """A temp root holding copies of the real image inputs."""
        root = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, root)
        for name in docker_make.IMAGE_INPUTS:
            (root / name).parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(docker_make.ROOT / name, root / name)
        return root


class ImageTagTest(ImageInputsTestCase):
    def test_tag_is_name_and_twelve_hex_of_input_digest(self) -> None:
        root = self.inputs()

        tag = docker_make.image_tag(root)

        name, _, digest = tag.partition(":")
        self.assertEqual(name, docker_make.IMAGE_NAME)
        self.assertEqual(len(digest), docker_make.TAG_LENGTH)
        self.assertTrue(all(character in "0123456789abcdef" for character in digest))

    def test_tag_changes_when_an_input_changes(self) -> None:
        root = self.inputs()
        before = docker_make.image_tag(root)
        dockerfile = root / "Dockerfile"
        dockerfile.write_bytes(dockerfile.read_bytes() + b"x")

        after = docker_make.image_tag(root)

        self.assertNotEqual(after, before)


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
        docker = FakeDocker(present=False)

        docker_make.ensure_image(Path("/repo"), "img:abc", docker)

        self.assertEqual(
            [call[:3] for call in docker.calls],
            [["docker", "image", "inspect"], ["docker", "build", "-t"]],
        )

    def test_skips_build_when_image_present(self) -> None:
        docker = FakeDocker(present=True)

        docker_make.ensure_image(Path("/repo"), "img:abc", docker)

        self.assertEqual(
            [call[:3] for call in docker.calls], [["docker", "image", "inspect"]]
        )


class MainTest(ImageInputsTestCase):
    def test_main_builds_the_image_then_returns_the_make_exit_code(self) -> None:
        root = self.inputs()
        docker = FakeDocker(present=False, exit_code=7)

        code = docker_make.main(["docker_make.py", "hosttest"], root, docker)

        self.assertEqual(code, 7)
        self.assertEqual(
            [call[:2] for call in docker.calls],
            [["docker", "image"], ["docker", "build"], ["docker", "run"]],
        )
        self.assertEqual(docker.calls[-1][-2:], ["make", "hosttest"])


if __name__ == "__main__":
    unittest.main()
