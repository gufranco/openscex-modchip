# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Run a make target inside the pinned toolchain container.

Every build, check and test runs in the pinned Docker image so results are
identical on any machine and in CI; only flashing with avrdude runs on the
host. This wrapper builds the image on demand and forwards the make target.
"""

import hashlib
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
IMAGE_INPUTS = ("Dockerfile", "docker/requirements-tools.txt")
IMAGE_NAME = "openscex-modchip-toolchain"
TAG_LENGTH = 12


def image_tag(root: Path) -> str:
    """Name the image by a hash of its inputs.

    Tagging with the digest of the Dockerfile and the pinned requirements means
    any change to the toolchain yields a new tag, so a stale image is never
    reused and the rebuild is automatic.
    """
    digest = hashlib.sha256()
    for name in IMAGE_INPUTS:
        digest.update((root / name).read_bytes())
    return f"{IMAGE_NAME}:{digest.hexdigest()[:TAG_LENGTH]}"


def toolchain_command(
    root: Path, tag: str, user: tuple[int, int], targets: list[str]
) -> list[str]:
    """Build the `docker run` argv that runs `make <targets>` in the image.

    The container runs as the host uid/gid so build artifacts stay owned by the
    user, HOME is /tmp because the container user has no home, and the git
    safe.directory env vars stop git refusing the bind-mounted /src as
    dubiously owned when a check shells out to git.
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


def ensure_image(root: Path, tag: str) -> None:
    """Build the image only when that exact tag is not already present."""
    present = subprocess.run(
        ["docker", "image", "inspect", tag], capture_output=True, check=False
    )
    if present.returncode != 0:
        subprocess.run(["docker", "build", "-t", tag, str(root)], check=True)


def main(arguments: list[str]) -> int:
    tag = image_tag(ROOT)
    ensure_image(ROOT, tag)
    command = toolchain_command(ROOT, tag, (os.getuid(), os.getgid()), arguments[1:])
    return subprocess.run(command, check=False).returncode


if __name__ == "__main__":
    sys.exit(main(sys.argv))
