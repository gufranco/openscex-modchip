# openscex-modchip

English | [日本語](README.ja.md) | [中文](README.zh.md)

[![CI](https://github.com/gufranco/openscex-modchip/actions/workflows/ci.yml/badge.svg)](https://github.com/gufranco/openscex-modchip/actions/workflows/ci.yml)
[![MISRA C:2012](https://img.shields.io/badge/MISRA%20C%3A2012-0%20deviations-brightgreen)](AGENTS.md)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

Region-unlock firmware for the original Sony PlayStation (fat) and PSone. One source builds for the ATtiny85 (SCEx injection) and the ATtiny84 (SCEx plus a boot-ROM BIOS patch). It emits the configured region string inside the SUBQ region-check window and leaves the data line high-impedance during play. It is not an optical-drive emulator, does not support PS2 or Saturn, and does not defeat LibCrypt.

## Overview

| Property | Value |
|:---------|:------|
| Console targets | PlayStation fat PU-7 through PU-23, PSone PM-41 and PM-41(2) |
| MCU | ATtiny85 (8-pin DIP), ATtiny84 (14-pin DIP) |
| Method | SCEx injection on both chips; the ATtiny84 also patches the boot ROM |
| Region | one per build, `REGION=jp|us|eu` (default us) |
| Clock | internal 8 MHz RC, or console external (hardware-gated) |
| Signal wires | 4 (SQCK, SUBQ, DATA, WFCK) plus power; optional LED |
| Toolchain | C17, MISRA C:2012 zero deviations, pinned Docker image |

## Verified on hardware

Combinations that booted out of region on a real console (SCEx region unlock). Every other combination builds and passes the in-container gate set but is not confirmed on hardware.

| Board family | Console | Clock | Injection | Status |
|:-------------|:--------|:------|:----------|:-------|
| PU-8 | fat, NTSC-U/C | internal | SCEx | Verified 2026-10-05 |
| PU-18 | fat, NTSC-U/C | internal | SCEx | Verified 2026-10-05 |
| PU-18 | fat, NTSC-U/C | external | SCEx | Verified 2026-10-05 |
| PM-41 | PSone | internal | SCEx | Verified 2026-10-05 |
| PM-41 | PSone | external | SCEx | Verified 2026-10-05 |

Not confirmed on hardware (built and simulated only): PU-7, PU-20, PU-22, PU-23, the boot-ROM BIOS patch on every model, and PAL or NTSC-J on any family. The exact SCPH of the PU-8 and PU-18 units, the region and SCPH of the PSone unit, and which chip carried the external-clock builds, are not recorded.

## Features

| Feature | Detail |
|:--------|:-------|
| SCEx injection | 44-bit LSB-first region string, legacy static-gate method and the PU-22-and-later WFCK-carrier method |
| Board auto-detect | WFCK behaviour at boot selects legacy or carrier mode; one build fits every family |
| Single configured region | emits only `REGION`, never all three |
| Stealth | injects only inside the SUBQ region-check window, capped per arming, then DATA high-Z and LED off |
| Disc-swap re-arm | leaving the window re-arms the one-shot |
| Boot-ROM BIOS patch | ATtiny84 only, Japanese fat and PAL PSone, every model including two-phase SCPH-1000 and SCPH-3000 |
| Optional LED | status output; firmware is correct with no LED fitted |
| In-field diagnostics | a four-byte flight recorder written to EEPROM after the chip goes idle (detected board, session count, injection count), read back with avrdude; no existing PS1 modchip reports what it saw |
| Verification | host tests 100% line and branch coverage, simavr console model across the oscillator band, mutation testing, reproducible builds |

## Confidence tags

| Tag | Meaning |
|:----|:--------|
| Read | taken from a primary source (PsNee source, consolemods, psdevwiki) |
| Concluded | inferred from sources, not stated by any one of them |
| Verified | confirmed on hardware or in the simavr model by this project |
| Unknown | no source gives it; measure on the console |

| Fact | Tag |
|:-----|:----|
| MCU pin assignment | Read (PsNee `MCU.h`), owner-confirmed as field-proven |
| Console tap points, signal level | Read, field-proven in PsNee and Mayumi installs |
| Mechacon pin numbers | Unknown, forum relay, re-confirm against a board diagram |
| Per-pad voltages | Unknown, must be measured |
| SCEx unlock on PU-8, PU-18, PSone | Verified 2026-10-05 (see table above) |
| External-clock build on PU-18 and PSone | Verified 2026-10-05; gated on every other board |
| BIOS-patch timing | Read from PsNee and exercised in simavr, not Verified on hardware |

## Supported build per console

The BIOS model follows the console's actual BIOS version, which matters more than the SCPH number.

| Console | Board | `MCU` | `REGION` | `BIOS` | Wiring |
|:--------|:------|:------|:---------|:-------|:-------|
| Fat US/Canada, SCPH-100x1/550x1/700x1/900x1 | PU-7 to PU-23 | `attiny85` | `us` | `none` | SCEx, 4 wires |
| Fat PAL, SCPH-100x2/550x2/900x2 | PU-8 to PU-22 | `attiny85` | `eu` | `none` | SCEx, 4 wires |
| PSone US/Canada, SCPH-101 | PM-41 / PM-41(2) | `attiny85` | `us` | `none` | SCEx, 4 wires |
| PSone PAL, SCPH-102 | PM-41 / PM-41(2) | `attiny84` | `eu` | `scph_102` | SCEx + BIOS patch |
| PSone Japan, SCPH-100 | PM-41 | `attiny84` | `jp` | `scph_100` | SCEx + BIOS patch |
| Fat Japan, SCPH-5000/5500/3500 | PU-18 / PU-8 | `attiny84` | `jp` | `scph_3500_5500` | SCEx + BIOS patch |
| Fat Japan, SCPH-7000/7500/9000 | PU-20 to PU-23 | `attiny84` | `jp` | `scph_7000_9000` | SCEx + BIOS patch |
| Fat Japan, SCPH-3000 | PU-8 | `attiny84` | `jp` | `scph_3000` | SCEx + two-phase BIOS patch |
| Fat Japan, SCPH-1000 | PU-7 | `attiny84` | `jp` | `scph_1000` | SCEx + two-phase BIOS patch |

Not covered by a BIOS model: Asian models (SCPH-xxx3, for example SCPH-5003/5903) carry an English ROM with no second check but an NTSC-J CD controller with no backdoor, so SCEx alone is insufficient; dev boards (DTL-H120x, PU-9) read burned discs natively and need no chip.

## Configuration

One source builds every variant; the knobs are passed to `make`.

| Knob | Values | Default | Selects |
|:-----|:-------|:--------|:--------|
| `MCU` | `attiny85`, `attiny84` | `attiny85` | 8-pin (SCEx only) or 14-pin (adds the BIOS patch) |
| `REGION` | `jp`, `us`, `eu` | `us` | the one region string the chip emits |
| `CLOCK` | `internal`, `external` | `internal` | internal 8 MHz RC, or the console clock (`CLOCK=external EXT_F_CPU=<hz>`), gated on measuring that clock and the pin voltage first |
| `BIOS` | `none`, `scph_102`, `scph_100`, `scph_7000_9000`, `scph_3500_5500`, `scph_1000`, `scph_3000` | `none` | ATtiny84 only; the boot-ROM patch tuned to that BIOS version |

A non-default region tags the artifact name, for example `openscex-modchip-attiny85-jp.hex`.

## MCU pinout

PsNee's tested ATtiny85 (`ATTINY_X5`) assignment, read from PsNee `MCU.h`. The physical pin numbers are the standard PDIP pinouts; verify against the datasheet for SOIC or QFN.

### ATtiny85, 8-pin DIP (SCEx only)

| DIP pin | Port | Signal | Direction | Connect to |
|:-------:|:-----|:-------|:----------|:-----------|
| 1 | PB5 | RESET | - | leave as reset |
| 2 | PB3 | LED | out, optional | status LED through a resistor, or leave off |
| 3 | PB4 | WFCK | in and out | legacy gate, or PU-22+ live carrier |
| 4 | GND | GND | - | console ground |
| 5 | PB0 | SQCK | in | SUBQ serial clock |
| 6 | PB1 | SUBQ | in | SUBQ serial data |
| 7 | PB2 | DATA | out, drive-low or high-Z | SCEx injection into the mechacon |
| 8 | VCC | VCC | - | console 5 V, measure first |

### ATtiny84, 14-pin DIP (SCEx plus BIOS patch)

The SCEx signals sit on PORTA in the same order as the 85. AX, AY and DX are wired only on Japanese fat and PAL PSone consoles.

| DIP pin | Port | Signal | Direction | Connect to |
|:-------:|:-----|:-------|:----------|:-----------|
| 1 | VCC | VCC | - | console 5 V, measure first |
| 2 | PB0 | - | - | unused, XTAL1 if external clock is enabled |
| 3 | PB1 | - | - | unused, XTAL2 |
| 4 | PB3 | RESET | - | leave as reset |
| 5 | PB2 | AX | in | BIOS patch, first address line, pulses counted |
| 6 | PA7 | - | - | unused |
| 7 | PA6 | AY | in | BIOS patch, second address line, two-phase models only |
| 8 | PA5 | DX | out, drive-low or high-Z | BIOS patch, data-bus override |
| 9 | PA4 | LED | out, optional | status LED through a resistor, or leave off |
| 10 | PA3 | WFCK | in and out | legacy gate, or PU-22+ live carrier |
| 11 | PA2 | DATA | out, drive-low or high-Z | SCEx injection into the mechacon |
| 12 | PA1 | SUBQ | in | SUBQ serial data |
| 13 | PA0 | SQCK | in | SUBQ serial clock |
| 14 | GND | GND | - | console ground |

On an SCEx-only console the ATtiny84 uses the same four signals as the 85; AX, AY and DX stay unconnected.

## Console tap points

The DATA line carries the SCEx bitstream; WFCK is the gate or carrier. These are the PsNee and Mayumi tap points. The board family is auto-detected, so the chip is the same and only the tap points differ.

| Board family | SCPH era | DATA injection point | WFCK role | Confidence |
|:-------------|:---------|:---------------------|:----------|:-----------|
| PU-7, PU-8 | 1000-5003 | demodulated wobble NRZ serial into the mechacon, historically "point 6" | static high | Read; point from a forum snippet, confirm on your board |
| PU-18, PU-20 | 550x-750x | digital NRZ output of the wobble ASIC into the mechacon | static gate | Read |
| PU-22, PU-23 | 7500-900x | CD-processor tracking line, WFCK as a fake carrier, three-wire-plus-link | live clock, sync required | Read |
| PM-41, PM-41(2) | PSone 100-103 | same tracking-line carrier method; float the chip I/O when idle on PM-41(2) | live clock | Read |

Mechacon SUBQ and SQCK pins, forum relay, tagged Unknown because psxdev.net is offline since October 2025: PU-22 and later, SUBQ on pin 24 and SQCK on pin 26; PU-7 and early PU-8, SUBQ on pin 39 and SQCK on pin 41. Confirm against a consolemods board diagram before cutting.

BIOS-patch pads (ATtiny84, Japanese fat and PAL PSone): the MCU-side pins are fixed in the table above, driven by [`src/bios.c`](src/bios.c) and [`src/port.S`](src/port.S). The console-side pads for AX, AY and DX are per board and per BIOS revision, and no source in this project gives a verified per-pad mapping. Identify them against PsNee's notes for your model and a board diagram; treat every BIOS-patch install as unconfirmed until it boots.

## Build and program

Every build, check and test runs in the pinned Docker toolchain through `make`. Only `avrdude` runs on the host.

| Tool | Purpose |
|:-----|:--------|
| Docker | runs the pinned toolchain |
| Git | clones the repository |
| avrdude | flashes the image to the chip |

```bash
git clone https://github.com/gufranco/openscex-modchip.git
cd openscex-modchip
make REGION=us                                        # ATtiny85, America, internal clock
make MCU=attiny84 REGION=eu BIOS=scph_102             # PAL PSone, with the BIOS patch
avrdude -c <programmer> -p attiny85 -U flash:w:openscex-modchip-attiny85.hex:i
```

Any ISP works, including an Arduino as ISP. The firmware resets the clock prescaler to divide-by-one at boot, so a factory CKDIV8 fuse does not change the timing.

| Build | Fuses (low / high / extended) |
|:------|:------------------------------|
| Internal 8 MHz RC | `0xE2` / `0xDF` / `0xFF` |
| External clock | depends on the measured frequency; read from the chip datasheet |

The external-clock build is Verified on hardware on PU-18 and PSone (2026-10-05) and gated on every other board. Build it with the measured mechacon frequency, `make CLOCK=external EXT_F_CPU=<hz>UL`; the output name carries `-extclk`. The candidate frequency is 4.2336 MHz (16.9344 MHz divided by four), Concluded from a snippet, not measured here. Do not program external-clock fuses before measuring the clock pin voltage.

Verify:

```bash
make test        # host tests, simavr console model, static analysis, MISRA
make repro       # two fresh builds, byte-identical
make mutate      # mutation testing on the logic layer
```

The same gates run in CI on every push and pull request, defined in [`.github/workflows/ci.yml`](.github/workflows/ci.yml).

## Diagnostics

The chip writes a four-byte flight recorder to EEPROM once it has finished injecting and gone idle, so an install can be diagnosed rather than guessed. The write never happens at boot or inside the region-check window, so it does not affect injection timing, and it is one write per power cycle so EEPROM endurance is not a concern. Read it back with the programmer:

```bash
avrdude -c <programmer> -p attiny85 -U eeprom:r:diag.bin:r
```

| Byte | Meaning |
|:-----|:--------|
| 0 | magic `0x50`; any other value means no record was written yet |
| 1 | detected board: `0` legacy gate, `1` WFCK carrier |
| 2 | sessions that reached idle, wraps at 255 |
| 3 | region strings emitted in the last session |

## Safety

- Measure the logic voltage at every tap point before wiring. No source gives a per-pad voltage, so it is Unknown. Fat boards are assumed around 5 V and the PSone PM-41(2) lower and noise-sensitive; assumption is not measurement.
- Never feed a console signal into a clock pin before measuring it.
- Opening a console and soldering to the CD subsystem can destroy it. Build at your own risk.

## License

[MIT](LICENSE) for firmware and documentation.
