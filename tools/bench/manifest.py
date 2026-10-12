# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Third-party firmware the bench may run, identified by the bytes alone.

`artifacts.manifest.json` gives each image or source its location or commit,
its SHA-256 or git tree id, and its licence. A missing or different file is
skipped with a message saying how to obtain it.
"""

import hashlib
import json
from dataclasses import dataclass
from enum import StrEnum
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
MANIFEST = "artifacts.manifest.json"


class Status(StrEnum):
    """Whether an artifact can be used as listed."""

    READY = "ready"
    MISSING = "missing"
    MISMATCH = "mismatch"


@dataclass(frozen=True, slots=True)
class Artifact:
    """One manifest entry: file entries carry path and sha256, git sources url,
    commit and tree; optional assembler defines and sketch path."""

    id: str
    kind: str
    license: str
    redistributable: bool
    obtain: str
    path: str | None = None
    sha256: str | None = None
    url: str | None = None
    commit: str | None = None
    tree: str | None = None
    defines: list[str] | None = None
    sketch: str | None = None


@dataclass(frozen=True, slots=True)
class Check:
    """The verdict on one artifact and what to tell the user."""

    artifact: Artifact
    status: Status
    message: str


def load(root: Path) -> dict[str, Artifact]:
    """Read the manifest under root, keyed by artifact id."""
    data = json.loads((root / MANIFEST).read_text())
    return {entry["id"]: Artifact(**entry) for entry in data["artifacts"]}


def check(root: Path, artifact: Artifact) -> Check:
    """Verify a file artifact's bytes; a source entry is checked when fetched."""
    if artifact.path is None:
        return Check(artifact, Status.READY, f"{artifact.id}: fetched at build")
    target = root / artifact.path
    if not target.is_file():
        return Check(
            artifact,
            Status.MISSING,
            f"{artifact.id}: {artifact.path} is absent. To run it: {artifact.obtain}.",
        )
    actual = hashlib.sha256(target.read_bytes()).hexdigest()
    if actual != artifact.sha256:
        return Check(
            artifact,
            Status.MISMATCH,
            f"{artifact.id}: {artifact.path} has SHA-256 {actual}, but the "
            f"manifest expects {artifact.sha256}. It is a different build or "
            f"an edited copy; replace it with the original: {artifact.obtain}.",
        )
    return Check(artifact, Status.READY, f"{artifact.id}: verified")
