English | [日本語](README.ja.md) | [中文](README.zh.md)

<div align="center">

<h1>openscex-modchip</h1>

<strong>Stealth SCEx region unlock for the PlayStation and PSone, on an ATtiny84 clocked by the console itself.</strong>

<br><br>

[![CI](https://github.com/gufranco/openscex-modchip/actions/workflows/ci.yml/badge.svg)](https://github.com/gufranco/openscex-modchip/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/gufranco/openscex-modchip)](https://github.com/gufranco/openscex-modchip/releases)
[![MISRA C:2012](https://img.shields.io/badge/MISRA%20C%3A2012-0%20deviations-brightgreen)](AGENTS.md)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

</div>

<p align="center">
  <a href="#quick-start">Quick start</a> &nbsp;|&nbsp;
  <a href="#console-tap-points">Wiring</a> &nbsp;|&nbsp;
  <a href="#status-led">Status LED</a> &nbsp;|&nbsp;
  <a href="../../issues/new?template=compatibility.yml">Report your console</a>
</p>

**3514** bytes of flash · **6** board families, PU-18 to PM-41(2) · **3** regions · **0** MISRA deviations · **100%** host line and branch coverage · **122/122** mutants killed

```bash
gh release download --repo gufranco/openscex-modchip --pattern 'openscex-modchip-attiny84.hex' --pattern SHA256SUMS
sha256sum -c SHA256SUMS --ignore-missing
avrdude -c <programmer> -p attiny84 -U flash:w:openscex-modchip-attiny84.hex:i
```

> [!IMPORTANT]
> The ATtiny84 redesign (v0.3.0) passes the full simulation gate and has not yet run on a console. Measure the clock and lid points before wiring.

Region-unlock firmware for the original Sony PlayStation (fat) and PSone, on an ATtiny84 clocked by the console itself. It emits the configured region string inside the SUBQ region-check window, stops the moment the console accepts it, and leaves the data line high-impedance during play. A wire to the lid switch tells it exactly when a disc is swapped. It does not patch the boot ROM, so Japanese fat consoles and the PAL PSone keep their second region check; install a patched BIOS if you need that bypassed. It is not an optical-drive emulator, does not support PS2 or Saturn, and does not defeat LibCrypt.

| | |
|:--|:--|
| **Silent after acceptance**<br>The first program-area frame stops injection, mid-string included, and the chip stays silent until the lid opens. | **Exact disc swaps**<br>A lid wire stops a string within 4 ms of opening and re-arms the chip on close, for every disc of a multi-disc game. |
| **Console-locked timing**<br>The chip runs from the console's 4.2336 MHz clock, never its own oscillator. | **One region, one window**<br>Only the configured region string, only inside the SUBQ region-check window, at most 16 strings per arming. |
| **Status LED codes**<br>One LED shows each boot stage, every disc's result and any live wiring fault as counted flashes. | **Proven in code**<br>MISRA C:2012 clean, 100% host coverage, a simavr console model, mutation testing, byte-identical rebuilds. |

## How it works

```mermaid
graph LR
    subgraph Console
        CD[CD subsystem]
        CLK[4.2336 MHz clock]
        LID[Lid switch]
        WF[WFCK]
        MECH[Mechacon]
    end
    subgraph ATtiny84
        CAP[SUBQ capture]
        DET[Region-check detector]
        ST[Stealth state machine]
        INJ[SCEx injector]
        LED[Status LED]
        CAL[Per-console calibration]
    end
    CD -->|SQCK, SUBQ| CAP
    CAP --> DET --> ST --> INJ
    ST --> LED
    ST <-->|cap, start| CAL
    LID -->|lid line| ST
    WF -->|gate or carrier| INJ
    CLK -->|CLKI| ATtiny84
    INJ -->|DATA| MECH
```

## Against other chips

| Capability | openscex | PsNee V9 | Mayumi V4 | MM3 |
|:-----------|:---------|:---------|:----------|:----|
| Stealth trigger | SUBQ check window plus acceptance latch | SUBQ decode | sense line plus lid line | same program as Mayumi V4 |
| Disc-swap detection | lid line | SUBQ counter decay | lid line | lid line |
| Clock | console | internal | console | internal RC |
| Boot-ROM BIOS patch | no, use a patched BIOS | yes, ATmega builds | no | no |
| Boards | PU-18 to PM-41(2) | PU-7 to PM-41(2) | PU-18 and later | PU-7 and later |
| Diagnostics | LED stage and result codes | serial debug | none | none |
| Per-console learning | string cap and start point | none | none | none |
| Tests and static analysis | host, simavr, mutation, MISRA | none | none | none |
| Field record | 2 boards, earlier firmware | years | decades | decades |

## Overview

| Property | Value |
|:---------|:------|
| Console targets | PlayStation fat PU-18 through PU-23, PSone PM-41 and PM-41(2) |
| MCU | ATtiny84 (14-pin DIP) |
| Method | SCEx injection |
| Region | one per build, `REGION=jp|us|eu` (default us) |
| Clock | the console's 4.2336 MHz clock on CLKI, never the internal oscillator |
| Wires | 6 signals (SQCK, SUBQ, DATA, WFCK, clock, lid) plus power; optional LED |
| Toolchain | C17, MISRA C:2012 zero deviations, pinned Docker image |

## Verified on hardware

Combinations that booted out of region on a real console (SCEx region unlock), with the firmware of 2026-10-05.

| Board family | Console | Clock | Injection | Status |
|:-------------|:--------|:------|:----------|:-------|
| PU-18 | fat, NTSC-U/C | console | SCEx | Verified 2026-10-05 |
| PM-41 | PSone | console | SCEx | Verified 2026-10-05 |

These results predate the ATtiny84-only redesign: the mandatory lid line, the console clock as the only clock, the acceptance latch, the bounded SUBQ waits, and every 2026-10-06 change. The redesign passes the full simulation gate and has not yet run on a console. Which chip carried the 2026-10-05 console-clock builds, the exact SCPH of each unit, and the frequency they were built for are not recorded. Not confirmed on hardware: PU-20, PU-22, PU-23, PM-41(2), and PAL or NTSC-J on any family.

Per-console validation is community-driven. Tested it on your console? Open a [compatibility report](../../issues/new?template=compatibility.yml) and this table grows from confirmed installs.

## What's included

| Feature | Detail |
|:--------|:-------|
| SCEx injection | 44-bit LSB-first region string, the PU-18 and PU-20 static-gate method, with the WFCK gate held low for each string as PsNee and Mayumi V4 do, and the PU-22-and-later WFCK-carrier method |
| Board auto-detect | WFCK behaviour at boot selects gate or carrier mode; one build fits every family |
| Console clock | the chip runs from the console's own 4.2336 MHz clock, so every delay is locked to the console crystal, as on Mayumi V4 |
| Lid line | a mandatory wire to the lid switch: an open lid stops a string within one 4 ms bit cell, and a close re-arms the chip for the next disc, so every swap in a multi-disc game is seen, never inferred |
| Stealth | injects only inside the SUBQ region-check window, capped per arming, with 5 frames, 67 ms, between strings as PsNee and Mayumi V4 space them, then DATA high-Z and LED off; stops the moment the console reads the program area and stays silent through every later lead-in read until the lid opens |
| Single configured region | emits only `REGION`, never all three |
| Adaptive timing | on WFCK-carrier boards the injection bit is timed by counting WFCK periods; `TIMING=fixed` uses the console-clocked delay instead |
| Self-recovery | each SUBQ capture realigns on the gap between frames and gives up after 30 ms; a WFCK carrier that stalls mid-injection lets the watchdog release DATA; a missing lid wire reads as open, so the chip stays silent instead of injecting blind |
| Status LED | an optional LED on its own pin shows the boot stages, each disc's result and live faults as counted flashes; the firmware never waits on it and is correct with no LED fitted |
| In-field diagnostics | no programmer needed: the LED codes name the failing stage, from a missing lid wire to a SUBQ line that never shows a region check; nothing is read back with a programmer |
| Per-console calibration | learns how many strings this console needs and how late it can start, kept in a six-byte EEPROM record that falls back to the defaults when missing or damaged |
| Closed-loop confirmation | after injecting, the chip watches SUBQ for a program-area frame (a real track number), which the mechacon only allows once it accepts the region string, and shows whether the region check passed |
| Verification | host tests 100% line and branch coverage, simavr console model at the console clock, mutation testing, reproducible builds |

## Confidence tags

| Tag | Meaning |
|:----|:--------|
| Read | taken from a primary source (PsNee source, the Mayumi V4 binary, quade.co, consolemods, psdevwiki, the ATtiny24A/44A/84A datasheet) |
| Concluded | inferred from sources, not stated by any one of them |
| Verified | confirmed on hardware or in the simavr model by this project |
| Unknown | no source gives it; measure on the console |

| Fact | Tag |
|:-----|:----|
| SCEx pin order | Read (PsNee `MCU.h`), owner-confirmed as field-proven |
| Clock and lid tap points | Read, the Mayumi V4 points 2 and 7 on quade.co, whose PM-41(2) page names them "Clock: Pin 2" and "CD Door: Pin 7" |
| Lid polarity, high while open | Read, the Mayumi V4 binary waits on its door input while it reads high |
| Console clock 4.2336 MHz | Concluded, 16.9344 MHz divided by four, consistent with the Mayumi V4 delay loops (182 passes where MM3 on a 4 MHz RC uses 170) |
| SQCK and SUBQ tap points | Read, labeled on PsNee's board photo for every supported board; the mechacon pin numbers behind them stay Unknown |
| Per-pad voltages | Concluded from the PsNee and Mayumi installs (fat around 5 V, PSone lower and noise-sensitive); measure to confirm |
| SCEx unlock on PU-18 and PSone | Verified 2026-10-05 with the earlier firmware (see table above) |

## Supported build per console

| Console | Board | `REGION` | Second region check |
|:--------|:------|:---------|:--------------------|
| Fat US/Canada, SCPH-550x1/700x1/900x1 | PU-18 to PU-23 | `us` | none |
| Fat PAL, SCPH-550x2/900x2 | PU-18 to PU-22 | `eu` | none |
| PSone US/Canada, SCPH-101 | PM-41 / PM-41(2) | `us` | none |
| PSone PAL, SCPH-102 | PM-41 / PM-41(2) | `eu` | in the boot ROM; needs a patched BIOS |
| PSone Japan, SCPH-100 | PM-41 | `jp` | in the boot ROM; needs a patched BIOS |
| Fat Japan, SCPH-5000/5500/7000/7500/9000 | PU-18 to PU-23 | `jp` | in the boot ROM; needs a patched BIOS |
| Asia, SCPH-xxx3 | not recorded | `jp` | none |
| Asia Video CD, SCPH-5903 | not recorded | `jp` + `VCD_FILTER=on` | none |

Boards older than the PU-18 (PU-7 and PU-8, SCPH-1000 to SCPH-500x) are not supported. The boot-ROM check is not handled by this chip: on the consoles marked above, imports may still be refused until a patched BIOS is installed. The Asian models need no patch: PsNee V9.0 targets SCPH-xxx3 and SCPH-5903 with the NTSC-J string alone. The SCPH-5903 also plays Video CDs, so its build adds `VCD_FILTER=on`, which arms injection only on a game's lead-in and never on a Video CD's. Neither Asian row has been confirmed on a console here. Dev boards (DTL-H120x, PU-9) read burned discs natively and need no chip.

## Configuration

One source builds every variant; the knobs are passed to `make`.

| Knob | Values | Default | Selects |
|:-----|:-------|:--------|:--------|
| `REGION` | `jp`, `us`, `eu` | `us` | the one region string the chip emits |
| `TIMING` | `adaptive`, `fixed` | `adaptive` | adaptive times the WFCK-carrier injection bit by counting WFCK periods; fixed uses the compile-time delay, which the console clock also locks |
| `VCD_FILTER` | `off`, `on` | `off` | on for the SCPH-5903 only: injection arms on a game's lead-in TOC and never on a Video CD's, following PsNee V9.0's SCPH-5903 filter |

A non-default region or filter tags the artifact name, for example `openscex-modchip-attiny84-jp.hex` or `openscex-modchip-attiny84-jp-vcd.hex`.

## MCU pinout

The SCEx signals keep PsNee's tested order, read from PsNee `MCU.h`; the clock and lid sit on PORTB. The physical pin numbers are the standard PDIP pinout; verify against the datasheet for SOIC or QFN.

| DIP pin | Port | Signal | Direction | Connect to |
|:-------:|:-----|:-------|:----------|:-----------|
| 1 | VCC | VCC | - | console supply, measure first |
| 2 | PB0 | CLKI | in | console clock, Mayumi V4 point 2; keep this wire the shortest |
| 3 | PB1 | LID | in, pull-up | lid switch, Mayumi V4 point 7 |
| 4 | PB3 | RESET | - | leave as reset |
| 5 | PB2 | - | - | unused |
| 6 | PA7 | - | - | unused |
| 7 | PA6 | - | - | unused |
| 8 | PA5 | - | - | unused |
| 9 | PA4 | LED | out, optional | status LED through a 1 kΩ resistor, or leave off |
| 10 | PA3 | WFCK | in and out | static gate, or PU-22+ live carrier |
| 11 | PA2 | DATA | out, drive-low or high-Z | SCEx injection into the mechacon |
| 12 | PA1 | SUBQ | in | SUBQ serial data |
| 13 | PA0 | SQCK | in | SUBQ serial clock |
| 14 | GND | GND | - | console ground |

## Per-console calibration

The chip learns how the console it is installed in reads the region string and keeps the result in a six-byte EEPROM record, so later discs spend less time with the data line driven. Every learned value only ever falls back toward the fixed defaults, so a lost, damaged or foreign record costs stealth, never a disc.

| Value | Learned from | Effect |
|:------|:-------------|:-------|
| String cap | the strings an accepted disc needed, plus 4 | later discs get at most that many strings instead of 16; a refused disc restores 16 from the next disc on |
| Start point | each accepted disc moves the start 2 lead-in frames, 27 ms, later, up to 20 frames; a `jp` build keeps the default, since PsNee warns against a later trigger on Japanese consoles | strings start closer to the region check; a refusal, or a lead-in read that ends before the start, steps back 2 frames and stops the probe |
| Board | the board detected at boot | a different board shows code 7 once and restarts the other two values |

The chip writes only a byte whose value changed, only at boot or after a disc's check has resolved, never while a string is being sent, so a console that has settled writes nothing. The cell endurance is 100,000 writes (Read: ATtiny24A/44A/84A datasheet DS40002269A). A check byte catches a record cut short by a power-off, which then reads as the defaults. Reflashing erases the record too, because both fuse sets above leave EESAVE unprogrammed (Read: the same datasheet, Table 19-4, high fuse bit 3). The margin of 4 strings, the 2-frame step and the 20-frame bound are design choices, not yet tuned on a console.

## Console tap points

DATA carries the SCEx bitstream and WFCK is the gate or carrier; these are the PsNee and Mayumi tap points. The clock and lid wires go where a Mayumi V4 chip puts its pins 2 and 7, shown per board on the quade.co diagrams: [PU-18](https://quade.co/ps1-modchip-guide/mayumi-v4/pu-18/), [PU-20](https://quade.co/ps1-modchip-guide/mayumi-v4/pu-20/), [PU-22](https://quade.co/ps1-modchip-guide/mayumi-v4/pu-22/), [PU-23](https://quade.co/ps1-modchip-guide/mayumi-v4/pu-23/), [PM-41](https://quade.co/ps1-modchip-guide/mayumi-v4/pm-41/), [PM-41(2)](https://quade.co/ps1-modchip-guide/mayumi-v4/pm-41-2/).

On these diagrams, point 2 is the console clock and point 7 is the lid line; those are the only two points this chip takes from them. Points 1 and 8 are power and ground, 5 and 6 are the WFCK and DATA points this chip also uses, and points 3 and 4 belong to Mayumi's own stealth and reset wiring, which this chip does not use. SQCK and SUBQ are not on these diagrams; the PsNee photos further down mark them.

<table>
<tr><td align="center" width="33%"><a href="https://quade.co/ps1-modchip-guide/mayumi-v4/pu-18/"><img src="https://quade.co/wp-content/uploads/2018/02/PU18L.jpg" alt="PU-18 Mayumi V4 installation diagram by William Quade" width="240"></a><br><sub><b>PU-18</b>. Diagram by William Quade, <a href="https://quade.co/ps1-modchip-guide/mayumi-v4/pu-18/">quade.co</a></sub></td><td align="center" width="33%"><a href="https://quade.co/ps1-modchip-guide/mayumi-v4/pu-20/"><img src="https://quade.co/wp-content/uploads/2018/02/PU20L.jpg" alt="PU-20 Mayumi V4 installation diagram by William Quade" width="240"></a><br><sub><b>PU-20</b>. Diagram by William Quade, <a href="https://quade.co/ps1-modchip-guide/mayumi-v4/pu-20/">quade.co</a></sub></td><td align="center" width="33%"><a href="https://quade.co/ps1-modchip-guide/mayumi-v4/pu-22/"><img src="https://quade.co/wp-content/uploads/2018/02/PU22L.jpg" alt="PU-22 Mayumi V4 installation diagram by William Quade" width="240"></a><br><sub><b>PU-22</b>. Diagram by William Quade, <a href="https://quade.co/ps1-modchip-guide/mayumi-v4/pu-22/">quade.co</a></sub></td></tr>
<tr><td align="center" width="33%"><a href="https://quade.co/ps1-modchip-guide/mayumi-v4/pu-23/"><img src="https://quade.co/wp-content/uploads/2018/01/pu23l.jpg" alt="PU-23 Mayumi V4 installation diagram by William Quade" width="240"></a><br><sub><b>PU-23</b>. Diagram by William Quade, <a href="https://quade.co/ps1-modchip-guide/mayumi-v4/pu-23/">quade.co</a></sub></td><td align="center" width="33%"><a href="https://quade.co/ps1-modchip-guide/mayumi-v4/pm-41/"><img src="https://quade.co/wp-content/uploads/2018/11/pm-41-mayumiv4.jpg" alt="PM-41 Mayumi V4 installation diagram by William Quade" width="240"></a><br><sub><b>PM-41</b>. Diagram by William Quade, <a href="https://quade.co/ps1-modchip-guide/mayumi-v4/pm-41/">quade.co</a></sub></td><td align="center" width="33%"><a href="https://quade.co/ps1-modchip-guide/mayumi-v4/pm-41-2/"><img src="https://quade.co/wp-content/uploads/2020/05/pm-412-m4.jpg" alt="PM-41(2) Mayumi V4 installation diagram by William Quade" width="240"></a><br><sub><b>PM-41(2)</b>. Diagram by William Quade, <a href="https://quade.co/ps1-modchip-guide/mayumi-v4/pm-41-2/">quade.co</a></sub></td></tr>
</table>

The six diagrams are William Quade's and are shown from quade.co with credit; they are not part of this repository or its MIT license. Click a diagram for the full page and its comments.

Every other point has a labeled photo below: SQCK, SUBQ, DATA, WFCK, VCC and GND are marked by name on each board. Solder the clock and lid where the diagrams above put points 2 and 7, and everything else where these photos mark it. The PU-18 photo shows the underside of the board. AX, DX and RESET are labeled too but belong to PsNee's boot-ROM patch, which this chip does not have; leave them unconnected.

<table>
<tr><td align="center" width="33%"><a href="assets/psnee/pu-18.jpg"><img src="assets/psnee/pu-18.jpg" alt="PU-18 board with the SQCK, SUBQ, DATA, WFCK, VCC and GND points labeled, from PsNee" width="240"></a><br><sub><b>PU-18</b>. Photo from PsNee</sub></td><td align="center" width="33%"><a href="assets/psnee/pu-20.jpg"><img src="assets/psnee/pu-20.jpg" alt="PU-20 board with the SQCK, SUBQ, DATA, WFCK, VCC and GND points labeled, from PsNee" width="240"></a><br><sub><b>PU-20</b>. Photo from PsNee</sub></td><td align="center" width="33%"><a href="assets/psnee/pu-22.jpg"><img src="assets/psnee/pu-22.jpg" alt="PU-22 board with the SQCK, SUBQ, DATA, WFCK, VCC and GND points labeled, from PsNee" width="240"></a><br><sub><b>PU-22</b>. Photo from PsNee</sub></td></tr>
<tr><td align="center" width="33%"><a href="assets/psnee/pu-23.jpg"><img src="assets/psnee/pu-23.jpg" alt="PU-23 board with the SQCK, SUBQ, DATA, WFCK, VCC and GND points labeled, from PsNee" width="240"></a><br><sub><b>PU-23</b>. Photo from PsNee</sub></td><td align="center" width="33%"><a href="assets/psnee/pm-41.jpg"><img src="assets/psnee/pm-41.jpg" alt="PM-41 board with the SQCK, SUBQ, DATA, WFCK, VCC and GND points labeled, from PsNee" width="240"></a><br><sub><b>PM-41</b>. Photo from PsNee</sub></td><td align="center" width="33%"><a href="assets/psnee/pm-41-2.jpg"><img src="assets/psnee/pm-41-2.jpg" alt="PM-41(2) board with the SQCK, SUBQ, DATA, WFCK, VCC and GND points labeled, from PsNee" width="240"></a><br><sub><b>PM-41(2)</b>. Photo from PsNee</sub></td></tr>
</table>

These six photos come from [PsNee](https://github.com/kalymos/PsNee) V9.0 by kalymos and its contributors, released into the public domain under the [Unlicense](LICENSES/Unlicense.txt), and are copied here resized. With them and the diagrams above, every wire of the chip has a picture of where it goes.

| Board family | SCPH era | DATA injection point | WFCK role | Confidence |
|:-------------|:---------|:---------------------|:----------|:-----------|
| PU-18, PU-20 | 550x-750x | digital NRZ output of the wobble ASIC into the mechacon | static gate | Read |
| PU-22, PU-23 | 7500-900x | CD-processor tracking line, WFCK as a fake carrier, three-wire-plus-link | live clock, sync required | Read |
| PM-41, PM-41(2) | PSone 100-103 | same tracking-line carrier method; float the chip I/O when idle on PM-41(2) | live clock | Read |

The clock wire carries the console's 4.2336 MHz clock into the chip, so its length matters: quade.co traced Mayumi V4 failures to that wire picking up noise, and recommends making it the shortest of all. Mount the chip close to the clock point. The datasheet warns that a clock varying more than 2% from one cycle to the next can make the chip behave unpredictably.

The photos above mark SQCK and SUBQ on every supported board, so follow them. The mechacon pin numbers a forum relay gives for PU-22 and later, SUBQ on pin 24 and SQCK on pin 26, stay Unknown, since psxdev.net is offline since October 2025.

## Quick start

Prebuilt per-console `.hex` images are attached to each [release](../../releases), so you can skip the toolchain and go straight to flashing with the `avrdude` steps below. Each release carries the three ATtiny84 images (`us`, `eu`, `jp`), the SCPH-5903 image (`jp-vcd`), a `SHA256SUMS` file, the license, and a build-provenance attestation. Releases up to v0.2.0 carried ATtiny85 images for the earlier four-wire design. Check a download before flashing it:

```bash
sha256sum -c SHA256SUMS --ignore-missing
gh attestation verify openscex-modchip-attiny84.hex --repo gufranco/openscex-modchip
```

Releases are versioned automatically from the commit history, and only from a commit whose CI run passed; the `0.x` line marks the firmware as pre-hardware-validation. To build from source instead: every build, check and test runs in the pinned Docker toolchain through `make`, and only `avrdude` runs on the host.

| Tool | Purpose |
|:-----|:--------|
| Docker | runs the pinned toolchain |
| Git | clones the repository |
| avrdude | flashes the image to the chip |

```bash
git clone https://github.com/gufranco/openscex-modchip.git
cd openscex-modchip
make REGION=us                                        # America
make REGION=jp VCD_FILTER=on                          # SCPH-5903
avrdude -c <programmer> -p attiny84 -U flash:w:openscex-modchip-attiny84.hex:i
avrdude -c <programmer> -p attiny84 -U lfuse:w:0xE0:m -U hfuse:w:0xDF:m -U efuse:w:0xFF:m
```

Any ISP works, including an Arduino as ISP. Write the flash first and the fuses last. The low fuse `0xE0` selects an external clock on CLKI with the slow-rising-power start-up delay and no clock divider (Read: ATtiny24A/44A/84A datasheet DS40002269A, Table 19-5 and Table 6-3, CKSEL 0000, SUT 10). From then on the chip has no clock of its own: to read or reprogram it, program it in place with the console powered, or feed a clock to pin 2 from the programmer. The firmware also clears the clock prescaler at boot, so the CKDIV8 fuse cannot slow it.

| Build | Fuses (low / high / extended) |
|:------|:------------------------------|
| Console clock | `0xE0` / `0xDF` / `0xFF` |
| Console clock with brown-out detection at 2.7 V | `0xE0` / `0xDD` / `0xFF` |

Brown-out detection holds the chip in reset while the supply is below 2.7 V, so the chip never runs on a supply that is collapsing at power-off; it is optional and not yet exercised on a console. The ATtiny84 runs at 4.2336 MHz from 1.8 V upward by its speed grade (Read: the same datasheet, 0 to 4 MHz at 1.8 V, 0 to 10 MHz at 2.7 V), which covers the PSone's lower supply.

Verify:

```bash
make test        # host tests, simavr console model, static analysis, MISRA
make repro       # two fresh builds, byte-identical
make mutate      # mutation testing on the logic layer
```

The same gates run in CI on every push and pull request, defined in [`.github/workflows/ci.yml`](.github/workflows/ci.yml). Contributors can run `make hooks` once to enable the commit-message and formatting checks locally.

## Status LED

The LED is optional and is the chip's only diagnostic channel: it shows which stage the chip is in, whether each disc passed its region check, and which wire to look at when something is wrong. It never delays or gates a feature and keeps no history, so a code is about now, except the two boot codes.

| Part | Choice |
|:-----|:-------|
| LED | a 3 mm or 5 mm red, orange, yellow or green LED, forward voltage about 2 V; not blue or white, whose 3 V forward voltage leaves almost nothing across the resistor on the PSone's lower supply |
| Resistor | 1 kΩ, any wattage: about 3 mA at 5 V and 1.5 mA at 3.5 V, bright enough indoors and far below the pin's 40 mA absolute maximum (Read: ATtiny24A/44A/84A datasheet DS40002269A) |
| Wiring | pin 9 (PA4) to the resistor, the resistor to the LED anode (long leg), the cathode (flat side) to ground |

| Stage | What the LED does |
|:------|:------------------|
| Board detection | lit for about 0.4 s after power-on |
| Board found | one 300 ms blink for a static-gate board (PU-18, PU-20), two for a WFCK-carrier board (PU-22 and later) |
| Waiting for a disc | a 40 ms blip every 2 s |
| Injecting | a 90 to 181 ms flash per region string |
| Result | a code shown three times, then dark for play |

Codes are long 700 ms flashes, 300 ms apart, with a 2 s pause before the code repeats.

| Code | Meaning | Check |
|:----:|:--------|:------|
| 1 | the console accepted the region string | nothing, the disc plays |
| 2 | strings were sent and the console never reached the program area | DATA and WFCK wiring, and that the build's region matches the disc |
| 3 | the lid is open, or the lid wire is missing; repeats while it holds | the lid wire and its point |
| 4 | no SUBQ frame for 5 s with the lid closed; repeats while it holds | the clock wire, SQCK, power and ground |
| 5 | frames arrive but no region check for 20 s; repeats while it holds | SUBQ; also normal with no disc or an audio CD |
| 6 | the watchdog reset the chip, shown once at the next boot | WFCK, which stalled mid-injection |
| 7 | the board differs from the one the calibration stored, shown once at boot; code 6 takes priority | the WFCK wire, which is intermittent, unless the chip moved to another console |

## Safety

- Measure the logic voltage at every tap point before wiring, the clock and lid points included. The values are taken from the established PsNee and Mayumi installs (fat boards around 5 V, the PSone PM-41(2) lower and noise-sensitive), but assumption is not measurement.
- Never feed a console signal into the clock pin before measuring its voltage and frequency.
- Opening a console and soldering to the CD subsystem can destroy it. Build at your own risk.

## Versioning

Releases follow [Semantic Versioning](https://semver.org/) on the `0.x` line, which means a minor release can break compatibility: v0.3.0 replaced the ATtiny85 four-wire design with the ATtiny84. Every release is tagged and built from a commit whose CI passed; see [releases](../../releases) for the notes.

## Support

| Need | Where |
|:-----|:------|
| Bug report | [bug template](../../issues/new?template=bug.yml) |
| Result on your console | [compatibility report](../../issues/new?template=compatibility.yml) |
| Security report | [security policy](SECURITY.md), reported privately |
| Contributing | [contribution guide](CONTRIBUTING.md) |

## License

[MIT](LICENSE) for firmware and documentation.
