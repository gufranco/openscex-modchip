# openscex-modchip

An open-source region-unlock modchip for the original Sony PlayStation and PSone, one firmware that builds for the ATtiny85 (8-pin, four signal wires) or the ATtiny84 (14-pin), built to the engineering standard of a professional embedded project rather than a hobby firmware.

Status (as of 2026-10-04): the firmware builds for both chips (ATtiny85 572 bytes, ATtiny84 574, ATtiny84 with the BIOS patch 814) and passes the full in-container gate set (MISRA zero deviations, host tests at 100 percent coverage, simavr console-model scenarios across the oscillator band including the BIOS override, mutation testing, reproducible builds). Nothing has been verified on real hardware yet; the BIOS timing values stay unconfirmed until a console tests them.

## What it will do

A PlayStation checks a physical fingerprint pressed into the disc lead-in, the wobble groove, which decodes to a four-character region string (SCEI for Japan, SCEA for the Americas, SCEE for Europe). The console boots only discs carrying the string it expects. This modchip injects the string the console wants, only in the window the console asks for, and re-arms when it detects a disc change, so out-of-region and backup discs boot.

It is fully stealth: the build is region-specific and emits only that one configured region string, only during the short region-check window, and leaves the data line high-impedance and silent for the rest of the session, so it is electrically invisible during play. Pick the region at build time with `make REGION=jp|us|eu` (default America).

Pick the chip by what the console needs. The ATtiny85 is the minimal, lowest-cost build: four signal wires and power, one optional status LED, no modes or reset or lid wiring. Japanese fat consoles and the PAL PSone run a second region check inside the boot ROM; for those, the ATtiny84 build adds the boot-ROM patch on its extra pins (`make MCU=attiny84 BIOS=<model>`).

This is not an optical-drive emulator. It does not replace the drive, does not support PS2 or Saturn, and cannot defeat data-layer protections such as LibCrypt.

## Design principles

- Portable, layered C: a host-testable logic layer, a thin platform layer, and register access isolated to assembly, so the protocol logic is tested without hardware.
- Timing with stated origins: every timing constant traces to a measured bit cell, a datasheet figure, or a simulation, never copied from another chip without derivation.
- Verified before claimed: host tests at full coverage, simulation across the oscillator tolerance band, mutation testing, and reproducible builds from a pinned toolchain, with a documented set of claims that only real hardware can settle.
- Derive timing from the console where the hardware allows it, for precision, as the Mayumi design did.
- Inexpensive, through-hole-preferred, genuinely open source, with no proprietary ROM content in the tree.

## Documentation

- [`INSTALL.md`](INSTALL.md): the technical install guide, with the pin map, the per-board-family tap-point matrix, and the programming and fuse steps.
- [`AGENTS.md`](AGENTS.md): the contract and hard rules for anyone working in this repository.

The full specification, hardware model, research corpus, and decision records are development material kept outside the published tree.

## Prior art and credit

The protocol and compatibility knowledge here is synthesized from decades of community work: Old Crow's open-source chip, the Mayumi and MM3 lineage, OneChip, and above all PsNee, the modern open-source synthesis whose source is the primary protocol reference. This project's contribution is engineering quality, not protocol novelty. Sources are cited per claim in the research documents.

## Licence

Proposed MIT for firmware and documentation, matching the reference project. Third-party source is studied, not copied; research informs architecture without taking implementation from any licence that forbids it.
