# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""The three SCEx region strings, as the bits a chip clocks onto DATA.

Each string is six bytes sent least significant bit first, of which the first
44 bits are used; only the fifth byte differs by region. Read: kalymos PsNee
V9.0, PSNee.ino:577-581 and :603-605. The firmware's own table lives in C; the
bench derives the bits from the bytes so the two can be checked against each
other rather than copied.
"""

from enum import StrEnum

SCEX_BITS = 44


class Region(StrEnum):
    """The region letter the console expects: America, Europe or Japan."""

    AMERICA = "A"
    EUROPE = "E"
    JAPAN = "I"


_COMMON = (0x59, 0xC9, 0x4B, 0x5D)
_REGION_BYTE = {Region.AMERICA: 0xFA, Region.EUROPE: 0xEA, Region.JAPAN: 0xDA}
_TAIL = 0x02


def region_bits(region: Region) -> str:
    """Return the 44-bit string for a region as a text of '0' and '1'."""
    data = (*_COMMON, _REGION_BYTE[region], _TAIL)
    bits = "".join(
        "1" if (byte >> bit) & 1 else "0" for byte in data for bit in range(8)
    )
    return bits[:SCEX_BITS]


def identify(bits: str) -> Region | None:
    """Name the region a decoded string belongs to, or None if it is no SCEx."""
    for region in Region:
        if bits == region_bits(region):
            return region
    return None
