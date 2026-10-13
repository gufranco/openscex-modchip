# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Intel HEX, the one reader every bench tool uses.

Each record is `:` then hex: byte count, 16-bit offset, record type, data and a
checksum that makes the record's bytes sum to zero. Data records land at the
offset plus the base the last extended-address record set; an end record stops
the file. A record that does not add up is refused, so a truncated or edited
image is never laid out as flash.
"""

DATA = 0
END = 1
EXTENDED_SEGMENT = 2
EXTENDED_LINEAR = 4
HEADER_BYTES = 4
SEGMENT_SHIFT = 4
LINEAR_SHIFT = 16
BYTE_MASK = 0xFF


def _record(line: str, number: int) -> bytes:
    """One record's bytes, checked for its start, length and checksum."""
    if not line.startswith(":"):
        raise ValueError(f"line {number}: a record starts with ':'")
    try:
        record = bytes.fromhex(line[1:])
    except ValueError as error:
        raise ValueError(f"line {number}: not hexadecimal") from error
    if len(record) < HEADER_BYTES + 1 or len(record) != HEADER_BYTES + record[0] + 1:
        raise ValueError(f"line {number}: length does not match its byte count")
    if sum(record) & BYTE_MASK:
        raise ValueError(f"line {number}: checksum does not add up")
    return record


def memory(text: str) -> dict[int, int]:
    """Every byte an Intel HEX file fills, by address, up to its end record."""
    filled: dict[int, int] = {}
    base = 0
    for number, line in enumerate(text.split(), start=1):
        record = _record(line, number)
        kind = record[3]
        offset = (record[1] << 8) | record[2]
        payload = record[HEADER_BYTES:-1]
        if kind == END:
            return filled
        if kind == DATA:
            filled.update(
                (base + offset + index, byte) for index, byte in enumerate(payload)
            )
        elif kind == EXTENDED_SEGMENT:
            base = int.from_bytes(payload, "big") << SEGMENT_SHIFT
        elif kind == EXTENDED_LINEAR:
            base = int.from_bytes(payload, "big") << LINEAR_SHIFT
    raise ValueError("no end record: the file is truncated")
