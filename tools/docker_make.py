# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Run a make target inside the pinned toolchain container."""

import hashlib
import os
import subprocess
import sys
from collections.abc import Callable
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
IMAGE_INPUTS = ("Dockerfile", "docker/requirements-tools.txt")
IMAGE_NAME = "openscex-modchip-toolchain"
TAG_LENGTH = 12

# How the wrapper starts a process: subprocess.run, or a stand-in in tests. No
# call captures text, so the output, when captured, is bytes.
Runner = Callable[..., subprocess.CompletedProcess[bytes]]


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


def ensure_image(root: Path, tag: str, runner: Runner = subprocess.run) -> None:
    """Build the image only when that exact tag is not already present."""
    present = runner(
        ["docker", "image", "inspect", tag], capture_output=True, check=False
    )
    if present.returncode != 0:
        runner(["docker", "build", "-t", tag, str(root)], check=True)


def main(
    arguments: list[str], root: Path = ROOT, runner: Runner = subprocess.run
) -> int:
    tag = image_tag(root)
    ensure_image(root, tag, runner)
    command = toolchain_command(root, tag, (os.getuid(), os.getgid()), arguments[1:])
    return runner(command, check=False).returncode


if __name__ == "__main__":
    sys.exit(main(sys.argv))
