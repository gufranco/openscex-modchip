English | [日本語](README.ja.md) | [中文](README.zh.md)

<div align="center">

<h1>openscex-modchip</h1>

<strong>Stealth SCEx region unlock for the PlayStation and PSone, on an ATtiny85 that trims its own clock against the console.</strong>

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

**7654** bytes of flash · **8** board families, PU-7 to PM-41(2) · **3** regions · **0** MISRA deviations · **100%** host line and branch coverage · **178/178** mutants killed

```bash
gh release download --repo gufranco/openscex-modchip --pattern 'openscex-modchip-attiny85.hex' --pattern SHA256SUMS
sha256sum -c SHA256SUMS --ignore-missing
avrdude -c <programmer> -p attiny85 -U flash:w:openscex-modchip-attiny85.hex:i
```

> [!IMPORTANT]
> The current firmware passes the full simulation gate and has not yet run on a console. Measure the tap voltages before wiring.

Region-unlock firmware for the original Sony PlayStation (fat) and PSone, on an ATtiny85 running from its internal oscillator, which it trims against the console's own SUBQ frame rate. It emits the configured region string inside the SUBQ region-check window, stops the moment the console accepts it, and leaves the data line high-impedance during play. It needs no lid wire: it knows a disc was swapped when SUBQ goes quiet while the drive is stopped. It does not patch the boot ROM, so Japanese fat consoles and the PAL PSone keep their second region check; install a patched BIOS if you need that bypassed. It is not an optical-drive emulator, does not support PS2 or Saturn, and does not defeat LibCrypt.

| | |
|:--|:--|
| **Silent after acceptance**<br>The first program-area frame stops injection, mid-string included, and the chip stays silent through every reread until the disc leaves. | **Disc swaps without a lid wire**<br>The chip sees a swap as the drive going quiet for 1.5 s and re-arms for every disc of a multi-disc game. |
| **Self-trimmed timing**<br>The chip times the console's 75 Hz SUBQ frames and trims its internal oscillator to within about 1%, with no clock wire. | **One region, one window**<br>Only the configured region string, only inside the SUBQ region-check window, at most 16 strings per arming. |
| **Status LED codes**<br>One LED shows each boot stage, every disc's result and any live wiring fault as counted flashes. | **Proven in code**<br>MISRA C:2012 clean, 100% host coverage, a simavr console model, mutation testing, byte-identical rebuilds. |

## How it works

```mermaid
graph LR
    subgraph Console
        CD[CD subsystem]
        WF[WFCK]
        MECH[Mechacon]
    end
    subgraph ATtiny85
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
    WF -->|gate or carrier| INJ
    INJ -->|DATA| MECH
```

## Against other chips

| Capability | openscex | PsNee V9 | Mayumi V4 | MM3 |
|:-----------|:---------|:---------|:----------|:----|
| Stealth trigger | SUBQ check window plus acceptance latch | SUBQ decode | sense line plus lid line | same program as Mayumi V4 |
| Disc-swap detection | SUBQ silence while the drive is stopped | SUBQ counter decay | lid line | lid line |
| Clock | internal, trimmed against SUBQ | internal | console | internal RC |
| Boot-ROM BIOS patch | no, use a patched BIOS | yes, ATmega builds | no | no |
| Boards | PU-7 to PM-41(2) | PU-7 to PM-41(2) | PU-18 and later | PU-7 and later |
| Diagnostics | LED stage and result codes | serial debug | none | none |
| Per-console learning | string cap and start point | none | none | none |
| Tests and static analysis | host, simavr, mutation, MISRA | none | none | none |
| Field record | 2 boards, earlier firmware | years | decades | decades |

## Overview

| Property | Value |
|:---------|:------|
| Console targets | PlayStation fat PU-7 through PU-23, PSone PM-41 and PM-41(2) |
| MCU | ATtiny85 (8-pin DIP) |
| Method | SCEx injection |
| Region | one per build, `REGION=jp|us|eu` (default us) |
| Clock | the internal 8 MHz RC oscillator, trimmed against the console's SUBQ frame rate |
| Wires | 4 signals (SQCK, SUBQ, DATA, WFCK) plus power; optional LED |
| Toolchain | C17, MISRA C:2012 zero deviations, pinned Docker image |

## Verified on hardware

Combinations that booted out of region on a real console (SCEx region unlock), with the firmware of 2026-10-05.

| Board family | Console | Clock | Injection | Status |
|:-------------|:--------|:------|:----------|:-------|
| PU-18 | fat, NTSC-U/C | console | SCEx | Verified 2026-10-05 |
| PM-41 | PSone | console | SCEx | Verified 2026-10-05 |

These results predate the current design: disc swaps seen from SUBQ with no lid wire, the internal oscillator with its trim, the acceptance latch, the bounded SUBQ waits, and every change since 2026-10-06. The current firmware passes the full simulation gate and has not yet run on a console. Which chip carried the 2026-10-05 console-clock builds, the exact SCPH of each unit, and the frequency they were built for are not recorded. Not confirmed on hardware: PU-7, PU-8, PU-20, PU-22, PU-23, PM-41(2), and PAL or NTSC-J on any family.

Per-console validation is community-driven. Tested it on your console? Open a [compatibility report](../../issues/new?template=compatibility.yml) and this table grows from confirmed installs.

## What's included

| Feature | Detail |
|:--------|:-------|
| SCEx injection | 44-bit LSB-first region string, the PU-7 to PU-20 static-gate method, with the WFCK gate held low for each string as PsNee and Mayumi V4 do, and the PU-22-and-later WFCK-carrier method |
| Board auto-detect | WFCK behaviour at boot selects gate or carrier mode; one build fits every family. Before each string on a board taken for a gate, WFCK is watched again for 9.4 ms, so a carrier that starts after boot is never held low |
| Supply guard | before every string the chip measures its own supply against its 1.1 V bandgap; below about 2.75 V, or on a reading no real supply gives, it sends nothing and shows code 7; a brief dip pauses a burst without restarting its count, and a window it kept shut teaches the calibration nothing |
| Oscillator trim | the chip runs from its internal 8 MHz oscillator and times the console's 75 Hz SUBQ frames, stepping OSCCAL one notch at a time until it is within 1% and never more than 16 notches from the factory value; the trim is kept in EEPROM and applied at boot |
| Disc swaps | no lid wire: 1.5 s with no valid SUBQ frame, which a stopped drive gives, ends the acceptance latch and re-arms the chip for the next disc; a seek or a reread on a spinning disc keeps producing frames, so it never reads as a swap |
| Stealth | injects only inside the SUBQ region-check window, capped per arming, with 5 frames, 67 ms, between strings as PsNee and Mayumi V4 space them, then DATA high-Z and LED off; stops the moment the console reads the program area and stays silent through every later lead-in read until the disc leaves |
| Single configured region | emits only `REGION`, never all three |
| Adaptive timing | on WFCK-carrier boards the injection bit is timed by counting WFCK periods; `TIMING=fixed` uses the trimmed-oscillator delay instead |
| Self-recovery | each SUBQ capture realigns on the gap between frames and gives up after 30 ms; a WFCK carrier that stalls mid-injection lets the watchdog release DATA; every unused pin has its pull-up on, so none floats |
| Status LED | an optional LED on its own pin shows the boot stages, each disc's result and live faults as counted flashes; the firmware never waits on it and is correct with no LED fitted |
| In-field diagnostics | no programmer needed: the LED codes name the failing stage, from a SUBQ line that never clocks to one that never shows a region check; nothing is read back with a programmer |
| Per-console calibration | learns how many strings this console needs, how late it can start and how fast its own oscillator runs, kept in a seven-byte EEPROM record that falls back to the defaults when missing or damaged |
| Closed-loop confirmation | after injecting, the chip watches SUBQ for a program-area frame (a real track number), which the mechacon only allows once it accepts the region string, and shows whether the region check passed |
| Verification | host tests 100% line and branch coverage, simavr console model, including a fast and a slow oscillator, mutation testing, reproducible builds |

## Confidence tags

| Tag | Meaning |
|:----|:--------|
| Read | taken from a primary source (PsNee source, the Mayumi V4 binary, quade.co, consolemods, psdevwiki, the ATtiny25/45/85 datasheet) |
| Concluded | inferred from sources, not stated by any one of them |
| Verified | confirmed on hardware or in the simavr model by this project |
| Unknown | no source gives it; measure on the console |

| Fact | Tag |
|:-----|:----|
| SCEx pin order | Read (PsNee `MCU.h`), owner-confirmed as field-proven |
| Disc swap seen from SUBQ silence | Concluded: opening the lid stops the drive, so valid mode 1 frames stop; the 1.5 s bound is a design choice, Unknown until hardware |
| Internal oscillator accuracy | Read: factory calibration +-10%, user calibration +-1% (ATtiny25/45/85 datasheet, Table 21-2). That the trim reaches it on a console is Unknown |
| SQCK and SUBQ tap points | Read, labeled on PsNee's board photo for every supported board; the mechacon pin numbers behind them stay Unknown |
| Per-pad voltages | Concluded from the PsNee and Mayumi installs (fat around 5 V, PSone lower and noise-sensitive); measure to confirm |
| SCEx unlock on PU-18 and PSone | Verified 2026-10-05 with the earlier firmware (see table above) |

## Supported build per console

| Console | Board | `REGION` | Second region check |
|:--------|:------|:---------|:--------------------|
| Fat US/Canada, SCPH-1001 | PU-8 | `us` | none |
| Fat US/Canada, SCPH-550x1/700x1/900x1 | PU-18 to PU-23 | `us` | none |
| Fat PAL, SCPH-1002 | PU-8 | `eu` | none |
| Fat PAL, SCPH-550x2/900x2 | PU-18 to PU-22 | `eu` | none |
| PSone US/Canada, SCPH-101 | PM-41 / PM-41(2) | `us` | none |
| PSone PAL, SCPH-102 | PM-41 / PM-41(2) | `eu` | in the boot ROM; needs a patched BIOS |
| PSone Japan, SCPH-100 | PM-41 | `jp` | in the boot ROM; needs a patched BIOS |
| Fat Japan, SCPH-1000/3000/3500/5000/5500/7000/7500/9000 | PU-7 to PU-23 | `jp` | in the boot ROM; needs a patched BIOS |
| Asia, SCPH-xxx3 | not recorded | `jp` | none |
| Asia Video CD, SCPH-5903 | not recorded | `jp` + `VCD_FILTER=on` | none |

The PU-7 and PU-8 use the same static-gate method as the PU-18 and PU-20, as PsNee V9.0 does on them; PsNee also lists the SCPH-1000 and SCPH-3000 among the consoles whose second check needs a BIOS patch (Read: PSNee.ino:33-38). The boot-ROM check is not handled by this chip: on the consoles marked above, imports may still be refused until a patched BIOS is installed. The Asian models need no patch: PsNee V9.0 targets SCPH-xxx3 and SCPH-5903 with the NTSC-J string alone. The SCPH-5903 also plays Video CDs, so its build adds `VCD_FILTER=on`, which arms injection only on a game's lead-in and never on a Video CD's. Neither Asian row has been confirmed on a console here. Dev boards (DTL-H120x, PU-9) read burned discs natively and need no chip.

## Configuration

One source builds every variant; the knobs are passed to `make`.

| Knob | Values | Default | Selects |
|:-----|:-------|:--------|:--------|
| `REGION` | `jp`, `us`, `eu` | `us` | the one region string the chip emits |
| `TIMING` | `adaptive`, `fixed` | `adaptive` | adaptive times the WFCK-carrier injection bit by counting WFCK periods; fixed uses the compile-time delay, which the trimmed internal oscillator times |
| `VCD_FILTER` | `off`, `on` | `off` | on for the SCPH-5903 only: injection arms on a game's lead-in TOC and never on a Video CD's, following PsNee V9.0's SCPH-5903 filter |

A non-default region or filter tags the artifact name, for example `openscex-modchip-attiny85-jp.hex` or `openscex-modchip-attiny85-jp-vcd.hex`.

## MCU pinout

The four SCEx signals and the LED keep PsNee's tested ATtiny85 assignment, read from PsNee `MCU.h`, so PsNee wiring guides match pin for pin; PB5 stays RESET, so the chip remains ISP-programmable. The physical pin numbers are the standard 8-pin PDIP pinout; verify against the datasheet for SOIC.

| DIP pin | Port | Signal | Direction | Connect to |
|:-------:|:-----|:-------|:----------|:-----------|
| 1 | PB5 | RESET | - | leave as reset |
| 2 | PB3 | LED | out, optional | status LED through a 1 kΩ resistor, or leave off |
| 3 | PB4 | WFCK | in and out | static gate, or PU-22+ live carrier |
| 4 | GND | GND | - | console ground |
| 5 | PB0 | SQCK | in | SUBQ serial clock |
| 6 | PB1 | SUBQ | in | SUBQ serial data |
| 7 | PB2 | DATA | out, drive-low or high-Z | SCEx injection into the mechacon |
| 8 | VCC | VCC | - | console supply, measure first |

## Per-console calibration

The chip learns how the console it is installed in reads the region string and keeps the result in a seven-byte EEPROM record, so later discs spend less time with the data line driven. Every learned value only ever falls back toward the fixed defaults, so a lost, damaged or foreign record costs stealth, never a disc.

| Value | Learned from | Effect |
|:------|:-------------|:-------|
| String cap | the strings an accepted disc needed, plus 4 | later discs get at most that many strings instead of 16; a refused disc restores 16 from the next disc on |
| Start point | each accepted disc moves the start 2 lead-in frames, 27 ms, later, up to 20 frames; a `jp` build keeps the default, since PsNee warns against a later trigger on Japanese consoles | strings start closer to the region check; a refusal, or a lead-in read that ends before the start, steps back 2 frames and stops the probe |
| Board | the board detected at boot | a different board shows code 6 once and restarts the string cap and start point |
| Oscillator | the time between lead-in frames, which the console's crystal spaces at 75 Hz | OSCCAL steps one notch per 64 frames until the chip is within 1%; the trim belongs to the chip, so a board change keeps it |

The chip writes only a byte whose value changed, only at boot or after a disc's check has resolved, never while a string is being sent, so a console that has settled writes nothing. The cell endurance is 100,000 writes (Read: ATtiny25/45/85 datasheet 2586Q). A check byte catches a record cut short by a power-off, which then reads as the defaults. Reflashing erases the record too, because both fuse sets above leave EESAVE unprogrammed (Read: the same datasheet, Table 20-4, high fuse bit 3). The margin of 4 strings, the 2-frame step and the 20-frame bound are design choices, not yet tuned on a console.

## Console tap points

Every point has a labeled photo below: SQCK, SUBQ, DATA, WFCK, VCC and GND are marked by name on each board; solder each wire where these photos mark it. The PU-18 photo shows the underside of the board. AX, DX and RESET are labeled too but belong to PsNee's boot-ROM patch, which this chip does not have; leave them unconnected.

<table>
<tr><td align="center" width="33%"><a href="assets/psnee/pu-7.jpg"><img src="assets/psnee/pu-7.jpg" alt="PU-7 board with the SQCK, SUBQ, DATA, WFCK, VCC and GND points labeled, from PsNee" width="240"></a><br><sub><b>PU-7</b>. Photo from PsNee</sub></td><td align="center" width="33%"><a href="assets/psnee/pu-8a.jpg"><img src="assets/psnee/pu-8a.jpg" alt="PU-8 board, later revision, with the SQCK, SUBQ, DATA, WFCK, VCC and GND points labeled, from PsNee" width="240"></a><br><sub><b>PU-8</b>, 1-658-467-22. Photo from PsNee</sub></td><td align="center" width="33%"><a href="assets/psnee/pu-8b.jpg"><img src="assets/psnee/pu-8b.jpg" alt="PU-8 board, earlier revision, with the SQCK, SUBQ, DATA, WFCK, VCC and GND points labeled, from PsNee" width="240"></a><br><sub><b>PU-8</b>, 1-658-467-12. Photo from PsNee</sub></td></tr>
<tr><td align="center" width="33%"><a href="assets/psnee/pu-18.jpg"><img src="assets/psnee/pu-18.jpg" alt="PU-18 board with the SQCK, SUBQ, DATA, WFCK, VCC and GND points labeled, from PsNee" width="240"></a><br><sub><b>PU-18</b>. Photo from PsNee</sub></td><td align="center" width="33%"><a href="assets/psnee/pu-20.jpg"><img src="assets/psnee/pu-20.jpg" alt="PU-20 board with the SQCK, SUBQ, DATA, WFCK, VCC and GND points labeled, from PsNee" width="240"></a><br><sub><b>PU-20</b>. Photo from PsNee</sub></td><td align="center" width="33%"><a href="assets/psnee/pu-22.jpg"><img src="assets/psnee/pu-22.jpg" alt="PU-22 board with the SQCK, SUBQ, DATA, WFCK, VCC and GND points labeled, from PsNee" width="240"></a><br><sub><b>PU-22</b>. Photo from PsNee</sub></td></tr>
<tr><td align="center" width="33%"><a href="assets/psnee/pu-23.jpg"><img src="assets/psnee/pu-23.jpg" alt="PU-23 board with the SQCK, SUBQ, DATA, WFCK, VCC and GND points labeled, from PsNee" width="240"></a><br><sub><b>PU-23</b>. Photo from PsNee</sub></td><td align="center" width="33%"><a href="assets/psnee/pm-41.jpg"><img src="assets/psnee/pm-41.jpg" alt="PM-41 board with the SQCK, SUBQ, DATA, WFCK, VCC and GND points labeled, from PsNee" width="240"></a><br><sub><b>PM-41</b>. Photo from PsNee</sub></td><td align="center" width="33%"><a href="assets/psnee/pm-41-2.jpg"><img src="assets/psnee/pm-41-2.jpg" alt="PM-41(2) board with the SQCK, SUBQ, DATA, WFCK, VCC and GND points labeled, from PsNee" width="240"></a><br><sub><b>PM-41(2)</b>. Photo from PsNee</sub></td></tr>
</table>

These nine photos come from [PsNee](https://github.com/kalymos/PsNee) V9.0 by kalymos and its contributors, released into the public domain under the [Unlicense](LICENSES/Unlicense.txt), and are copied here resized. With them, every wire of the chip has a picture of where it goes.

| Board family | SCPH era | DATA injection point | WFCK role | Confidence |
|:-------------|:---------|:---------------------|:----------|:-----------|
| PU-7, PU-8, PU-18, PU-20 | 1000-750x | digital NRZ output of the wobble ASIC into the mechacon | static gate | Read |
| PU-22, PU-23 | 7500-900x | CD-processor tracking line, WFCK as a fake carrier, three-wire-plus-link | live clock, sync required | Read |
| PM-41, PM-41(2) | PSone 100-103 | same tracking-line carrier method; float the chip I/O when idle on PM-41(2) | live clock | Read |

There is no clock wire and no lid wire: the chip runs from its own oscillator and sees disc swaps in SUBQ, so it can sit anywhere the four signal wires reach. Keep those wires short; quade.co traced Mayumi V4 failures to a long wire picking up noise. An install made for an earlier release can keep its clock and lid wires on pins 2 and 3: the firmware never drives them and holds them with pull-ups, though removing them is tidier.

The photos above mark SQCK and SUBQ on every supported board, so follow them. The mechacon pin numbers a forum relay gives for PU-22 and later, SUBQ on pin 24 and SQCK on pin 26, stay Unknown, since psxdev.net is offline since October 2025.

## Quick start

Prebuilt per-console `.hex` images are attached to each [release](../../releases), so you can skip the toolchain and go straight to flashing with the `avrdude` steps below. Each release carries the three ATtiny85 images (`us`, `eu`, `jp`), the SCPH-5903 image (`jp-vcd`), a `SHA256SUMS` file, the license, and a build-provenance attestation. Releases v0.3.0 to v0.8.0 carried ATtiny84 images, and releases up to v0.2.0 ATtiny85 images of the earlier four-wire design. Check a download before flashing it:

```bash
sha256sum -c SHA256SUMS --ignore-missing
gh attestation verify openscex-modchip-attiny85.hex --repo gufranco/openscex-modchip
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
avrdude -c <programmer> -p attiny85 -U flash:w:openscex-modchip-attiny85.hex:i
avrdude -c <programmer> -p attiny85 -U lfuse:w:0xE2:m -U hfuse:w:0xDD:m -U efuse:w:0xFF:m
```

Any ISP works, including an Arduino as ISP. Write the flash first and the fuses last. The low fuse `0xE2` selects the internal 8 MHz oscillator with the slow-rising-power start-up delay and no clock divider (Read: ATtiny25/45/85 datasheet 2586Q, Table 6-6 and Table 6-7, CKSEL 0010, SUT 10), so the chip can be read and reprogrammed on the bench with nothing but the programmer. The firmware also clears the clock prescaler at boot, so the CKDIV8 fuse cannot slow it. Reflashing erases the calibration record and its oscillator trim; the chip learns them again.

| Build | Fuses (low / high / extended) |
|:------|:------------------------------|
| Internal 8 MHz with brown-out detection at 2.7 V, recommended | `0xE2` / `0xDD` / `0xFF` |
| Internal 8 MHz without brown-out detection | `0xE2` / `0xDF` / `0xFF` |

Brown-out detection holds the chip in reset while the supply is below 2.7 V, so it never runs on a supply that is collapsing at power-off; it is not yet exercised on a console. Keep it on: by its speed grade the ATtiny85 runs 0 to 10 MHz from 2.7 V (Read: the same datasheet; only the ATtiny85V reaches down to 1.8 V), so below 2.7 V the chip is out of its rating. A fuse is easy to forget, so the firmware also measures its supply before every string and holds back below about 2.75 V, shown as code 7. The limit is set for a chip whose bandgap reads high, so a 3.3 V supply 5 percent low still passes.

Verify:

```bash
make size        # image size; fails when less than 256 bytes of flash stay free
make test        # host tests, simavr console model and its stack check, static analysis, MISRA
make repro       # two fresh builds, byte-identical
make mutate      # mutation testing on the logic layer
```

The same gates run in CI on every push and pull request, defined in [`.github/workflows/ci.yml`](.github/workflows/ci.yml). Contributors can run `make hooks` once to enable the commit-message and formatting checks locally.

## Status LED

The LED is optional and is the chip's only diagnostic channel: it shows which stage the chip is in, whether each disc passed its region check, and which wire to look at when something is wrong. It never delays or gates a feature and keeps no history, so a code is about now, except the two boot codes.

| Part | Choice |
|:-----|:-------|
| LED | a 3 mm or 5 mm red, orange, yellow or green LED, forward voltage about 2 V; not blue or white, whose 3 V forward voltage leaves almost nothing across the resistor on the PSone's lower supply |
| Resistor | 1 kΩ, any wattage: about 3 mA at 5 V and 1.5 mA at 3.5 V, bright enough indoors and far below the pin's 40 mA absolute maximum (Read: ATtiny25/45/85 datasheet 2586Q) |
| Wiring | pin 2 (PB3) to the resistor, the resistor to the LED anode (long leg), the cathode (flat side) to ground |

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
| 3 | no SUBQ frame for 5 s after power-on; shown for 15 s, then the heartbeat | SQCK, SUBQ, power and ground; also shown with no disc in |
| 4 | frames arrive but no region check for 20 s; repeats while it holds | SUBQ; also normal with an audio CD |
| 5 | the watchdog reset the chip, shown once at the next boot | WFCK, which stalled mid-injection |
| 6 | the board differs from the one the calibration stored, shown once at boot; code 5 takes priority | the WFCK wire, which is intermittent, unless the chip moved to another console |
| 7 | the supply measured under about 2.75 V, or the reading failed; no string is sent while it holds, and it outranks codes 3 and 4 | VCC and ground at the tap points, which must measure 3.3 V or more |

## Safety

- Measure the logic voltage at every tap point before wiring. The values are taken from the established PsNee and Mayumi installs (fat boards around 5 V, the PSone PM-41(2) lower and noise-sensitive), but assumption is not measurement.
- Opening a console and soldering to the CD subsystem can destroy it. Build at your own risk.

## Versioning

Releases follow [Semantic Versioning](https://semver.org/) on the `0.x` line, which means a minor release can break compatibility: v0.3.0 replaced the ATtiny85 four-wire design with the ATtiny84, and the release after v0.8.0 returned to the ATtiny85 once the design needed no clock and no lid wire. Every release is tagged and built from a commit whose CI passed; see [releases](../../releases) for the notes.

## Support

| Need | Where |
|:-----|:------|
| Bug report | [bug template](../../issues/new?template=bug.yml) |
| Result on your console | [compatibility report](../../issues/new?template=compatibility.yml) |
| Security report | [security policy](SECURITY.md), reported privately |
| Contributing | [contribution guide](CONTRIBUTING.md) |

## License

[MIT](LICENSE) for firmware and documentation.
