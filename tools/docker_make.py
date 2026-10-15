# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Run a make target inside the pinned toolchain container."""

import hashlib
import os
import subprocess
import sys
from pathlib import Path
from typing import Protocol

ROOT = Path(__file__).resolve().parent.parent
IMAGE_INPUTS = ("Dockerfile", "docker/requirements-tools.txt")
IMAGE_NAME = "openscex-modchip-toolchain"
TAG_LENGTH = 12

# Backstops for the two docker calls that run before make. An inspect answers
# at once; a cold build installs the toolchain, and the CI gates job, build
# included, has not passed 259 s, so a build is given half an hour. make itself
# runs unbounded here: its targets take from seconds to over an hour, and CI
# bounds each job.
INSPECT_TIMEOUT_S = 60
BUILD_TIMEOUT_S = 1800


class Runner(Protocol):
    """How the wrapper starts a process: subprocess.run, or a stand-in in
    tests. No call captures text, so the output, when captured, is bytes."""

    def __call__(
        self,
        args: list[str],
        *,
        check: bool,
        capture_output: bool = False,
        timeout: float | None = None,
    ) -> subprocess.CompletedProcess[bytes]: ...


def image_tag(root: Path) -> str:
    """Name the image by a hash of the Dockerfile and pinned requirements, so a
    toolchain change always gets a new tag."""
    digest = hashlib.sha256()
    for name in IMAGE_INPUTS:
        digest.update((root / name).read_bytes())
    return f"{IMAGE_NAME}:{digest.hexdigest()[:TAG_LENGTH]}"


def toolchain_command(
    root: Path, tag: str, user: tuple[int, int], targets: list[str]
) -> list[str]:
    """Build the `docker run` argv that runs `make <targets>` in the image.

    Runs as the host uid/gid so artifacts stay the user's; HOME is /tmp, and the
    safe.directory env vars let git accept the bind-mounted /src.
    """
    uid, gid = user
    return [
        "docker",
        "run",
        "--rm",
        "--user",
        f"{uid}:{gid}",
        "-e",
        "HOME=/tmp",
        "-e",
        "GIT_CONFIG_COUNT=1",
        "-e",
        "GIT_CONFIG_KEY_0=safe.directory",
        "-e",
        "GIT_CONFIG_VALUE_0=/src",
        "-v",
        f"{root}:/src",
        "-w",
        "/src",
        tag,
        "make",
        *targets,
    ]


def ensure_image(root: Path, tag: str, runner: Runner = subprocess.run) -> int:
    """Build the image only when that exact tag is not already present. The
    build's exit code, or 0 when the image was already there."""
    present = runner(
        ["docker", "image", "inspect", tag],
        check=False,
        capture_output=True,
        timeout=INSPECT_TIMEOUT_S,
    )
    if present.returncode == 0:
        return 0
    built = runner(
        ["docker", "build", "-t", tag, str(root)],
        check=False,
        timeout=BUILD_TIMEOUT_S,
    )
    if built.returncode != 0:
        print(f"docker build of {tag} failed: exit {built.returncode}", file=sys.stderr)
    return built.returncode


def main(
    arguments: list[str], root: Path = ROOT, runner: Runner = subprocess.run
) -> int:
    tag = image_tag(root)
    try:
        built = ensure_image(root, tag, runner)
    except subprocess.TimeoutExpired as expired:
        print(f"docker gave no answer in {expired.timeout} s", file=sys.stderr)
        return 1
    if built != 0:
        return built
    command = toolchain_command(root, tag, (os.getuid(), os.getgid()), arguments[1:])
    return runner(command, check=False).returncode


if __name__ == "__main__":
    sys.exit(main(sys.argv))
