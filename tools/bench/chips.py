# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""The chips the bench runs, and what each needs from the console.

A chip is a firmware image, the simulator and CPU it runs on, its clock, and
the pins it uses, named the way its runner expects: package pin numbers for
PIC (pin 2 GP5 through pin 7 GP0 on the 8-pin parts), port bits for AVR. Every
pin map is Read from the chip's own source or binary, cited per entry. All
images are the USA build, so every chip sends the same SCEA string and the
comparison is like for like.

field_proven marks the chips real consoles have accepted for years. Their
behaviour is the envelope ours is checked against; ours is not in it. boards
lists the mainboards each chip is made for, from its own guide, so a chip is
never held up as evidence for a board it does not support.
"""

from dataclasses import dataclass
from enum import StrEnum

from tools.bench.scenarios import Board


class Simulator(StrEnum):
    """Which runner plays a chip: simavr for AVR, gpsim for PIC."""

    AVR = "avr"
    PIC = "pic"


@dataclass(frozen=True, slots=True)
class Chip:
    """One firmware under test; firmware is a path under the repository."""

    name: str
    simulator: Simulator
    cpu: str
    clock_hz: int
    pins: str
    firmware: str
    artifact: str | None
    field_proven: bool
    origin: str
    boards: frozenset[Board]


_MAYUMI_PINS = "wfck=5,lid=7,reset=4,sense=3,data=6,gate=5"

# Every board from the PU-7 to the PM-41(2): this firmware's targets (README),
# PsNee's (its README), and MM3's, whose internal oscillator bought PU-7 and
# PU-8 support, imperfect there, per the consolemods.org PS1 modchip wiki and
# the psdevwiki modchip page, which relay forum reports (Concluded). Mayumi V4
# starts at the PU-18: the PU-7, PU-8 and PU-16 are "not compatible with
# Mayumi v4" (quade.co Mayumi V4 guide, Read).
_EVERY_BOARD = frozenset(Board)
_PU18_ON = frozenset(Board) - {Board.PU_7, Board.PU_8}

CHIPS = (
    Chip(
        name="ours",
        simulator=Simulator.AVR,
        cpu="attiny85",
        clock_hz=8_000_000,
        pins="sqck=B0,subq=B1,wfck=B4,data=B2,gate=B4,led=B3",
        firmware="build/attiny85/release/openscex-modchip-attiny85.elf",
        artifact=None,
        field_proven=False,
        origin="this repository, REGION=us; pins from include/port/registers.h",
        boards=_EVERY_BOARD,
    ),
    Chip(
        name="psnee-attiny85",
        simulator=Simulator.AVR,
        cpu="attiny85",
        clock_hz=8_000_000,
        pins="sqck=B0,subq=B1,wfck=B4,data=B2,gate=B4,led=B3",
        firmware="build/bench/psnee-attiny85.elf",
        artifact="psnee-v9",
        field_proven=True,
        origin="PsNee V9.0 SCPH_xxx1 built from source; pins and the 8 MHz "
        "clock from PSNee/MCU.h:453-530",
        boards=_EVERY_BOARD,
    ),
    Chip(
        name="psnee-atmega328p",
        simulator=Simulator.AVR,
        cpu="atmega328p",
        clock_hz=16_000_000,
        pins="sqck=D6,subq=D7,wfck=B1,data=B0,gate=B1,led=B5",
        firmware="build/bench/psnee-atmega328p.elf",
        artifact="psnee-v9",
        field_proven=True,
        origin="PsNee V9.0 SCPH_xxx1 built from source; pins from "
        "PSNee/MCU.h:160-231, 16 MHz Arduino Nano clock",
        boards=_EVERY_BOARD,
    ),
    Chip(
        name="mayumi-v4",
        simulator=Simulator.PIC,
        cpu="p12c508",
        clock_hz=4_233_600,
        pins=_MAYUMI_PINS,
        firmware="docs/research/sources/mayumi-mm3/mayumi_v4_12c508a_usa.hex",
        artifact="mayumi-v4-12c508a-usa",
        field_proven=True,
        origin="Mayumi V4 USA image; pins from its disassembly, named after the "
        "commented MM3 12F629 port, and the quade.co install diagrams; clocked "
        "by the console (configuration word 0xFE3, ExtRC)",
        boards=_PU18_ON,
    ),
    Chip(
        name="mm3-12c508a",
        simulator=Simulator.PIC,
        cpu="p12c508",
        clock_hz=4_000_000,
        pins=_MAYUMI_PINS,
        firmware="docs/research/sources/mayumi-mm3/mm3_v3_12c508a_usa.hex",
        artifact="mm3-12c508a-usa",
        field_proven=True,
        origin="MultiMode 3 USA image; same pins as Mayumi V4 (aligned "
        "disassembly); internal 4 MHz RC (configuration word 0xFEA)",
        boards=_EVERY_BOARD,
    ),
    Chip(
        name="mm3-12f629",
        simulator=Simulator.PIC,
        cpu="p12f629",
        clock_hz=4_000_000,
        pins=_MAYUMI_PINS,
        firmware="docs/research/sources/mayumi-mm3/mm3_v3_12f629_usa.hex",
        artifact="mm3-12f629-usa",
        field_proven=True,
        origin="MultiMode 3 12F629 port; pins from its source header "
        "(gpio0 door, gpio1 data, gpio2 gate, gpio3 reset, gpio4 memline); "
        "internal 4 MHz RC",
        boards=_EVERY_BOARD,
    ),
)


def by_name(name: str) -> Chip:
    """Find a chip by name; an unknown name raises KeyError."""
    for chip in CHIPS:
        if chip.name == name:
            return chip
    raise KeyError(name)
