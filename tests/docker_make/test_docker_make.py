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
    """Records every docker command and its options, and answers like the CLI
    would: `present` is whether the image exists, `build_code` what `docker
    build` returns, `exit_code` what `docker run` returns, and `hang` makes
    the inspect run past its limit."""

    def __init__(
        self, present: bool, exit_code: int = 0, build_code: int = 0, hang: bool = False
    ) -> None:
        self.present = present
        self.exit_code = exit_code
        self.build_code = build_code
        self.hang = hang
        self.calls: list[list[str]] = []
        self.options: list[tuple[bool, bool, float | None]] = []

    def __call__(
        self,
        args: list[str],
        *,
        check: bool,
        capture_output: bool = False,
        timeout: float | None = None,
    ) -> subprocess.CompletedProcess[bytes]:
        self.calls.append(args)
        self.options.append((check, capture_output, timeout))
        if args[:3] == ["docker", "image", "inspect"]:
            if self.hang:
                raise subprocess.TimeoutExpired(args, timeout or 0)
            return subprocess.CompletedProcess(args, 0 if self.present else 1)
        if args[:2] == ["docker", "build"]:
            return subprocess.CompletedProcess(args, self.build_code)
        return subprocess.CompletedProcess(args, self.exit_code)


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
        # Arrange
        root = self.inputs()

        # Act
        tag = docker_make.image_tag(root)

        # Assert
        name, _, digest = tag.partition(":")
        self.assertEqual(name, docker_make.IMAGE_NAME)
        self.assertEqual(len(digest), docker_make.TAG_LENGTH)
        self.assertTrue(all(character in "0123456789abcdef" for character in digest))

    def test_tag_changes_when_an_input_changes(self) -> None:
        # Arrange
        root = self.inputs()
        before = docker_make.image_tag(root)
        dockerfile = root / "Dockerfile"
        dockerfile.write_bytes(dockerfile.read_bytes() + b"x")

        # Act
        after = docker_make.image_tag(root)

        # Assert
        self.assertNotEqual(after, before)


class ToolchainCommandTest(unittest.TestCase):
    def test_command_mounts_root_and_runs_make_targets(self) -> None:
        # Act
        command = docker_make.toolchain_command(
            Path("/repo"), "img:abc", (501, 20), ["analyse", "hosttest"]
        )

        # Assert
        self.assertEqual(command[:3], ["docker", "run", "--rm"])
        self.assertIn("501:20", command)
        self.assertIn("/repo:/src", command)
        self.assertEqual(command[-3:], ["make", "analyse", "hosttest"])
        self.assertEqual(
            [command[i + 1] for i, word in enumerate(command) if word in ("-e", "-w")],
            [
                "HOME=/tmp",
                "GIT_CONFIG_COUNT=1",
                "GIT_CONFIG_KEY_0=safe.directory",
                "GIT_CONFIG_VALUE_0=/src",
                "/src",
            ],
        )


class EnsureImageTest(unittest.TestCase):
    def test_builds_when_image_absent(self) -> None:
        # Arrange
        docker = FakeDocker(present=False)

        # Act
        docker_make.ensure_image(Path("/repo"), "img:abc", docker)

        # Assert
        self.assertEqual(
            [call[:3] for call in docker.calls],
            [["docker", "image", "inspect"], ["docker", "build", "-t"]],
        )
        self.assertEqual(
            docker.options,
            [
                (False, True, docker_make.INSPECT_TIMEOUT_S),
                (False, False, docker_make.BUILD_TIMEOUT_S),
            ],
        )

    def test_skips_build_when_image_present(self) -> None:
        # Arrange
        docker = FakeDocker(present=True)

        # Act
        docker_make.ensure_image(Path("/repo"), "img:abc", docker)

        # Assert
        self.assertEqual(
            [call[:3] for call in docker.calls], [["docker", "image", "inspect"]]
        )


class MainTest(ImageInputsTestCase):
    def test_main_builds_the_image_then_returns_the_make_exit_code(self) -> None:
        # Arrange
        root = self.inputs()
        docker = FakeDocker(present=False, exit_code=7)

        # Act
        code = docker_make.main(["docker_make.py", "hosttest"], root, docker)

        # Assert
        self.assertEqual(code, 7)
        self.assertEqual(
            [call[:2] for call in docker.calls],
            [["docker", "image"], ["docker", "build"], ["docker", "run"]],
        )
        self.assertEqual(docker.calls[-1][-2:], ["make", "hosttest"])

    def test_a_failed_build_stops_before_make_and_returns_its_code(self) -> None:
        # Arrange
        root = self.inputs()
        docker = FakeDocker(present=False, build_code=3)

        # Act
        code = docker_make.main(["docker_make.py", "hosttest"], root, docker)

        # Assert
        self.assertEqual(
            (code, [call[:2] for call in docker.calls]),
            (3, [["docker", "image"], ["docker", "build"]]),
        )

    def test_docker_that_never_answers_fails_without_a_traceback(self) -> None:
        # Arrange
        root = self.inputs()
        docker = FakeDocker(present=False, hang=True)

        # Act
        code = docker_make.main(["docker_make.py", "hosttest"], root, docker)

        # Assert
        self.assertEqual((code, len(docker.calls)), (1, 1))


if __name__ == "__main__":
    unittest.main()
