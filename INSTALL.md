<!--
SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
SPDX-License-Identifier: MIT
-->

# Installation

TL;DR: flash `openscex-modchip-attiny85.hex` to an ATtiny85, wire four signals plus power to the CD subsystem, and the console boots out-of-region and backup discs. The pin assignment is PsNee's tested ATtiny85 map and the tap points are the established PsNee and Mayumi points, which are field-proven; this project has not independently run it on a console. Measure the signal voltage at each pad before connecting the chip.

## Safety first

- You must measure the logic voltage at every tap point before wiring. No source gives a per-pad voltage, so it is treated as Unknown. Fat boards are assumed around 5 V and the PSone PM-41(2) lower and noise-sensitive, but assumption is not measurement.
- Never feed a console signal into a clock pin before measuring it.
- Opening a console and soldering to the CD subsystem can destroy it. This is a build-at-your-own-risk project.

## The chip

A single ATtiny85 in an 8-pin DIP or SOIC. One optional status LED. No reset button, lid switch, or mode selector is used.

| ATtiny85 pin | Signal | Direction | Purpose |
|---|---|---|---|
| PB0 | SQCK | input | SUBQ serial clock from the CD decoder |
| PB1 | SUBQ | input | SUBQ serial data from the CD decoder |
| PB2 | DATA | output, drive-low or high-Z | SCEx injection into the mechacon |
| PB3 | LED | output, optional | status, safe to leave unconnected |
| PB4 | WFCK | input and output | board auto-detect, static gate on legacy boards, live carrier on PU-22 and later |
| PB5 | RESET | - | leave as reset, unused |
| VCC, GND | power | - | from the console's own 5 V and ground |

The pin assignment is PsNee's tested ATtiny85 (`ATTINY_X5`) map, so an existing PsNee or Mayumi install guide for your board matches this chip wire for wire. The firmware auto-detects the board family at boot from WFCK behaviour, so one build fits every family. There are no per-board firmware variants.

## Tap points per board family

These are the established PsNee and Mayumi tap points, documented on consolemods.org and psdevwiki.com and proven in the field. The DATA line carries the SCEx bitstream and the gate line enables or clocks it.

| Family | SCPH era | DATA injection point | Gate or carrier | Source |
|---|---|---|---|---|
| PU-7, PU-8 | 1000-5003 | demodulated wobble NRZ serial into the mechacon, historically "point 6" | WFCK held static high | PsNee/Mayumi; exact point from a forum snippet, confirm on your board |
| PU-18, PU-20 | 550x-750x | digital NRZ output of the custom wobble ASIC into the mechacon | WFCK static gate | PsNee/Mayumi |
| PU-22, PU-23 | 7500-900x | CD-processor tracking line via WFCK as a fake carrier, the three-wire-plus-link method | WFCK live clock, sync required | PsNee/Mayumi |
| PM-41, PM-41(2) | PSone 100-103 | same tracking-line carrier method; float the chip's I/O when idle on PM-41(2) for pickup noise | WFCK live clock | PsNee/Mayumi |

SUBQ and SQCK mechacon pins, from a forum snippet because psxdev.net has been offline since October 2025: on PU-22 and later, SUBQ on mechacon pin 24 and SQCK on pin 26; on PU-7 and early PU-8, SUBQ on pin 39 and SQCK on pin 41. Confirm against a consolemods board diagram before cutting.

## What this chip does not do

Japanese fat consoles and the PAL PSone run a second region check inside the boot ROM. This chip injects the SCEx string only and does not patch the boot ROM, so those specific models may still refuse some imports. Region unlock through SCEx alone covers the rest of the range.

## Programming

Program with `avrdude` on the host. Any ISP works, including an Arduino as ISP. The firmware resets the clock prescaler to divide-by-one at boot, so a factory CKDIV8 fuse does not change the timing.

Pick the region first. The build emits only the console's own region string, so
build for the console you are modding: `make REGION=jp`, `make REGION=us`
(default) or `make REGION=eu`. A non-default region tags the artifact name, for
example `openscex-modchip-attiny85-jp.hex`. The chip is stealth: it drives the
region string only during the boot region-check window and stays high-impedance
and silent during play, re-arming when you swap discs.

Default internal-oscillator build, the only build verified in simulation:

- Flash: `avrdude -c <programmer> -p attiny85 -U flash:w:openscex-modchip-attiny85.hex:i`
- Fuses, internal 8 MHz RC: low `0xE2`, high `0xDF`, extended `0xFF`.

External-clock build, unverified and hardware-gated:

- Build it with the measured mechacon frequency: `make CLOCK=external EXT_F_CPU=<hz>UL`. The output is `openscex-modchip-attiny85-extclk.hex`.
- The candidate frequency is 4.2336 MHz, equal to 16.9344 MHz divided by four, but it is Concluded from a snippet, not measured. Confirm it against the CD-DSP datasheet and a scope before use.
- The external-clock fuse bits depend on the measured frequency and must be read from the ATtiny85 datasheet. Do not program external-clock fuses before measuring the clock pin voltage.

## Status

The firmware builds and passes the full in-container gate set, including a simavr console model across the oscillator tolerance band. The pin assignment and tap points are the field-proven PsNee and Mayumi design; this project has not independently run the firmware on a real console, so treat per-board install success as confirmed only once you have done it. See [`README.md`](README.md) for scope and build instructions.
