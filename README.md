# psone-cdr-unlock

An open-source region-unlock modchip for the original Sony PlayStation and PSone, targeting an ATtiny, built to the engineering standard of a professional embedded project rather than a hobby firmware.

Status: discovery, 2026-10-04. No firmware exists yet. No claim here has been verified on hardware. This repository currently holds the project specification and the research behind it. Four architectural decisions await the owner before implementation begins.

## What it will do

A PlayStation checks a physical fingerprint pressed into the disc lead-in, the wobble groove, which decodes to a four-character region string (SCEI for Japan, SCEA for the Americas, SCEE for Europe). The console boots only discs carrying the string it expects. This modchip injects the string the console wants, so out-of-region and backup discs boot. For Japanese consoles and the PAL PSone, which run a second check inside the boot ROM, a later version adds a boot-ROM patch.

This is not an optical-drive emulator. It does not replace the drive, does not support PS2 or Saturn, and cannot defeat data-layer protections such as LibCrypt.

## Design principles

- Portable, layered C: a host-testable logic layer, a thin platform layer, and register access isolated to assembly, so the protocol logic is tested without hardware.
- Timing with stated origins: every timing constant traces to a measured bit cell, a datasheet figure, or a simulation, never copied from another chip without derivation.
- Verified before claimed: host tests at full coverage, simulation across the oscillator tolerance band, mutation testing, and reproducible builds from a pinned toolchain, with a documented set of claims that only real hardware can settle.
- Derive timing from the console where the hardware allows it, for precision, as the Mayumi design did.
- Inexpensive, through-hole-preferred, genuinely open source, with no proprietary ROM content in the tree.

## Documentation

- `docs/specification.md`: the full project specification, goals through open questions.
- `docs/hardware.md`: the board-family model, injection points, clock sources, and the compatibility matrix.
- `docs/research/`: the evidence base, one file per domain, every fact confidence-tagged.
- `docs/decisions/`: the architectural decision records, currently proposed and awaiting approval.

## Prior art and credit

The protocol and compatibility knowledge here is synthesized from decades of community work: Old Crow's open-source chip, the Mayumi and MM3 lineage, OneChip, and above all PsNee, the modern open-source synthesis whose source is the primary protocol reference. This project's contribution is engineering quality, not protocol novelty. Sources are cited per claim in the research documents.

## Licence

Proposed MIT for firmware and documentation, matching the reference project. Third-party source is studied, not copied; research informs architecture without taking implementation from any licence that forbids it.
