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
behaviour is the envelope ours is checked against; ours is not in it. The
other chips are recorded only: their results are reported and judged by the
showcase, never used to widen the envelope. Old Crow sends strings for as
long as it is powered, so as evidence it would let ours send anything. boards
lists the mainboards each chip is made for, from its own guide, so a chip is
never held up as evidence for a board it does not support. region is the
string the image sends; OneChip exists only as a PAL image, so it alone is
measured against the European string.
"""

from dataclasses import dataclass
from enum import StrEnum

from tools.bench.scenarios import GATE_BOARDS, Board
from tools.bench.scex import Region


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
    region: Region = Region.AMERICA


_MAYUMI_PINS = "wfck=5,lid=7,reset=4,sense=3,data=6,gate=5"

# Every board from the PU-7 to the PM-41(2): this firmware's targets (README),
# PsNee's (its README), and MM3's, whose internal oscillator bought PU-7 and
# PU-8 support, imperfect there, per the consolemods.org PS1 modchip wiki and
# the psdevwiki modchip page, which relay forum reports (Concluded). Mayumi V4
# starts at the PU-18: the PU-7, PU-8 and PU-16 are "not compatible with
# Mayumi v4" (quade.co Mayumi V4 guide, Read).
_EVERY_BOARD = frozenset(Board)
_PU18_ON = frozenset(Board) - {Board.PU_7, Board.PU_8}
# Old Crow and modavr hold the gate low from 850 ms after power-on to the end:
# a static-gate install, which the PU-22 and later boards replaced with a
# carrier the chip must not hold (Concluded from their sources, which never
# release the gate pin). OneChip is made for the PAL PSone, PM-41 and
# PM-41(2) only (quade.co ONEchip guide, Read).
_PSONE = frozenset({Board.PM_41, Board.PM_41_2})

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
    Chip(
        name="old-crow-12c508",
        simulator=Simulator.PIC,
        cpu="p12c508",
        clock_hz=4_000_000,
        pins="data=6,gate=5",
        firmware="build/bench/old-crow-12c508.hex",
        artifact="old-crow-12c508",
        field_proven=False,
        origin="Old Crow v5.3 source assembled with gpasm; GP1, pin 6, sends "
        "by port direction and GP2, pin 5, holds the gate low (its source, "
        "steps 1 to 4); internal 4 MHz RC; sends all three region strings",
        boards=GATE_BOARDS,
    ),
    Chip(
        name="old-crow-12f629",
        simulator=Simulator.PIC,
        cpu="p12f629",
        clock_hz=4_000_000,
        pins="data=6,gate=5",
        firmware="build/bench/old-crow-12f629.hex",
        artifact="old-crow-12f629",
        field_proven=False,
        origin="GaryOPA's 4-wire PIC12F629 port of Old Crow, assembled with "
        "gpasm; same pins as the 12C508 source; internal 4 MHz RC",
        boards=GATE_BOARDS,
    ),
    Chip(
        name="old-crow-12c508-v54f",
        simulator=Simulator.PIC,
        cpu="p12c508",
        clock_hz=4_000_000,
        pins="data=6,gate=5",
        firmware="docs/research/sources/old-crow/jstic/m508v54f/m508v54f.hex",
        artifact="old-crow-12c508-v54f",
        field_proven=False,
        origin="Old Crow v5.4F, his last 4-wire 12C508 code (14-JUN-98), "
        "published image; GP1 DATA, GP2 gate as in v5.3; internal 4 MHz RC",
        boards=GATE_BOARDS,
    ),
    Chip(
        name="old-crow-16c84",
        simulator=Simulator.PIC,
        cpu="p16c84",
        clock_hz=4_000_000,
        pins="data=7,gate=8",
        firmware="docs/research/sources/old-crow/jstic/modc84/MODC84.HEX",
        artifact="old-crow-16c84-v101",
        field_proven=False,
        origin="Old Crow v1.01 for the PIC16C84 (19-JAN-97), the first public "
        "port of his Z8 chip, published image; 6-wire: RB1, pin 7, DATA, RB2, "
        "pin 8, the gate, MCLR wired to the console and held released here, "
        "since gpsim drives no stimulus on MCLR; XT oscillator fed on OSC1, "
        "taken as the 4 MHz of the early boards' mechacon clock (Concluded)",
        boards=GATE_BOARDS,
    ),
    Chip(
        name="old-crow-16c54",
        simulator=Simulator.PIC,
        cpu="p16c54",
        clock_hz=4_000_000,
        pins="data=7,gate=8",
        firmware="docs/research/sources/old-crow/jstic/modc54/MODC54.HEX",
        artifact="old-crow-16c54-v101",
        field_proven=False,
        origin="Old Crow v1.01 for the PIC16C54, published image; the same "
        "6-wire pins and external clock as the 16C84 version",
        boards=GATE_BOARDS,
    ),
    Chip(
        name="modavr-attiny13",
        simulator=Simulator.AVR,
        cpu="attiny13",
        clock_hz=1_200_000,
        pins="data=B1,gate=B0",
        firmware="build/bench/modavr-attiny13.elf",
        artifact="modavr-attiny13",
        field_proven=False,
        origin="modavr, an ATtiny13 rewrite of Old Crow; PB0 gate, PB1 DATA, "
        "internal 9.6 MHz RC divided by 8 (its source header)",
        boards=GATE_BOARDS,
    ),
    Chip(
        name="ubernee-atmega328p",
        simulator=Simulator.AVR,
        cpu="atmega328p",
        clock_hz=16_000_000,
        pins="sqck=B1,subq=B0,wfck=B2,data=D4,gate=B2",
        firmware="build/bench/ubernee-atmega328p.elf",
        artifact="ubernee-v142",
        field_proven=False,
        origin="UberNee V1.42 built against Arduino AVR core 1.8.8 for an "
        "ATmega328P at 16 MHz, region SCEA, LGT8F328P off; SUBQ D8, SQCK D9, "
        "DATA D4, the WFCK carrier out of D10 (its sketch, lines 18-48)",
        boards=_EVERY_BOARD,
    ),
    Chip(
        name="onechip-12c508a",
        simulator=Simulator.PIC,
        cpu="p12c508",
        clock_hz=4_000_000,
        pins="lid=7,wfck=5,data=6,reset=4",
        firmware="docs/research/sources/onechip/onechip_v1_12c508a_pal.hex",
        artifact="onechip-12c508a-pal",
        field_proven=False,
        origin="ONEchip V1.00 PAL image, no source; pins Concluded from its "
        "disassembly: GP1 DATA, GP2 WFCK copied onto DATA, GP0 starts the "
        "strings when low, GP3 waited high at boot (the BIOS side, held high "
        "here); internal 4 MHz RC (configuration word 0xFEA)",
        boards=_PSONE,
        region=Region.EUROPE,
    ),
)


def by_name(name: str) -> Chip:
    """Find a chip by name; an unknown name raises KeyError."""
    for chip in CHIPS:
        if chip.name == name:
            return chip
    raise KeyError(name)
