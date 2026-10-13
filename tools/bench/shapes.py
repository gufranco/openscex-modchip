# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Checks for the shapes of the bench's committed JSON files.

json.loads returns whatever the file holds; each reader narrows it with these
before use, so a hand edit that breaks a file fails where it is read, naming
the place, instead of as a KeyError or a wrong table later.
"""


def mapping(value: object, where: str) -> dict[str, object]:
    """An object with string keys."""
    if not isinstance(value, dict) or not all(isinstance(k, str) for k in value):
        raise ValueError(f"{where}: expected an object")
    return value


def texts(value: object, where: str) -> dict[str, str]:
    """An object whose values are all strings."""
    checked = mapping(value, where)
    wrong = sorted(k for k, v in checked.items() if not isinstance(v, str))
    if wrong:
        raise ValueError(f"{where}: {wrong} must be strings")
    return {k: str(v) for k, v in checked.items()}


def items(value: object, where: str) -> list[dict[str, object]]:
    """A list of objects."""
    if not isinstance(value, list):
        raise ValueError(f"{where}: expected a list")
    return [mapping(item, f"{where}[{index}]") for index, item in enumerate(value)]
