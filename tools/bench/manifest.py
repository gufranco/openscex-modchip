# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Third-party firmware the bench may run, identified by the bytes alone.

`artifacts.manifest.json` gives each image or source its location or commit,
its SHA-256, a source also its git tree id, and its licence. A missing or
different file is skipped with a message saying how to obtain it.
"""

import hashlib
import json
import re
from dataclasses import dataclass
from enum import StrEnum
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
MANIFEST = "artifacts.manifest.json"
REQUIRED_TEXT = ("id", "kind", "license", "obtain")
OPTIONAL_TEXT = ("path", "sha256", "url", "commit", "tree", "sketch")
FIELDS = frozenset((*REQUIRED_TEXT, *OPTIONAL_TEXT, "redistributable", "defines"))
# What each kind must carry: a file is found by path and identified by its
# SHA-256; a fetched source by its commit, the git tree id checked after it,
# and the SHA-256 of every file it holds, which decides (prepare.tree_sha256).
FILE_KINDS = frozenset({"pic-image", "pic-source", "avr-image", "avr-source"})
SOURCE_KIND = "git-source"
NEEDS = {
    **dict.fromkeys(FILE_KINDS, ("path", "sha256")),
    SOURCE_KIND: ("url", "commit", "tree", "sha256"),
}
SHA256 = re.compile(r"[0-9a-f]{64}")


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
    defines: tuple[str, ...] | None = None
    sketch: str | None = None


@dataclass(frozen=True, slots=True)
class Check:
    """The verdict on one artifact and what to tell the user. data holds the
    exact bytes that were hashed, so nothing reads the file a second time and
    uses bytes that were never checked."""

    artifact: Artifact
    status: Status
    message: str
    data: bytes = b""


def _text(entry: dict[str, object], key: str, where: str) -> str | None:
    value = entry.get(key)
    if value is not None and not isinstance(value, str):
        raise ValueError(f"{where}: {key} must be a string")
    return value


def _defines(entry: dict[str, object], where: str) -> tuple[str, ...] | None:
    value = entry.get("defines")
    if value is None:
        return None
    if not isinstance(value, list) or not all(isinstance(d, str) for d in value):
        raise ValueError(f"{where}: defines must be a list of strings")
    return tuple(value)


def _fields(entry: dict[str, object], where: str) -> dict[str, str | None]:
    unknown = sorted(set(entry) - FIELDS)
    if unknown:
        raise ValueError(f"{where}: unknown fields {unknown}")
    text = {key: _text(entry, key, where) for key in (*REQUIRED_TEXT, *OPTIONAL_TEXT)}
    absent = [key for key in REQUIRED_TEXT if text[key] is None]
    kind = text["kind"]
    if kind not in NEEDS:
        raise ValueError(f"{where}: unknown kind {kind!r}")
    absent += [key for key in NEEDS[kind] if text[key] is None]
    if absent:
        raise ValueError(f"{where}: {kind} needs {absent}")
    if not SHA256.fullmatch(str(text["sha256"])):
        raise ValueError(f"{where}: sha256 must be 64 lowercase hex digits")
    return text


def parse_entry(entry: object, where: str) -> Artifact:
    """One manifest entry checked field by field, so a typo fails at load with
    its place named rather than as a wrong path or hash later."""
    if not isinstance(entry, dict):
        raise ValueError(f"{where}: an entry must be an object")
    text = _fields(entry, where)
    if not isinstance(entry.get("redistributable"), bool):
        raise ValueError(f"{where}: redistributable must be true or false")
    return Artifact(
        id=str(text["id"]),
        kind=str(text["kind"]),
        license=str(text["license"]),
        redistributable=bool(entry["redistributable"]),
        obtain=str(text["obtain"]),
        path=text["path"],
        sha256=text["sha256"],
        url=text["url"],
        commit=text["commit"],
        tree=text["tree"],
        defines=_defines(entry, where),
        sketch=text["sketch"],
    )


def load(root: Path) -> dict[str, Artifact]:
    """Read the manifest under root, keyed by artifact id; refuse a malformed
    entry or a repeated id."""
    data = json.loads((root / MANIFEST).read_text())
    entries = data.get("artifacts") if isinstance(data, dict) else None
    if not isinstance(entries, list):
        raise ValueError(f"{MANIFEST}: needs an artifacts list")
    artifacts = [
        parse_entry(entry, f"{MANIFEST} entry {number}")
        for number, entry in enumerate(entries, start=1)
    ]
    keyed = {artifact.id: artifact for artifact in artifacts}
    if len(keyed) != len(artifacts):
        raise ValueError(f"{MANIFEST}: an artifact id is repeated")
    return keyed


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
    data = target.read_bytes()
    actual = hashlib.sha256(data).hexdigest()
    if actual != artifact.sha256:
        return Check(
            artifact,
            Status.MISMATCH,
            f"{artifact.id}: {artifact.path} has SHA-256 {actual}, but the "
            f"manifest expects {artifact.sha256}. It is a different build or "
            f"an edited copy; replace it with the original: {artifact.obtain}.",
        )
    return Check(artifact, Status.READY, f"{artifact.id}: verified", data)
