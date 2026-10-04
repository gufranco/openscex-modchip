# AGENTS.md

Contract for anyone, human or agent, working in this repository.

## What this project is

Firmware for an ATtiny that defeats the Sony PlayStation region lockout by injecting the SCEx magic string the CD subsystem expects, and, on Japanese fat consoles and the PAL PSone, by patching the boot ROM during boot. It targets the original PlayStation (fat) and PSone across board families PU-7 through PM-41(2). The protocol and compatibility knowledge comes from decades of community work, above all the open-source PsNee; this project's contribution is engineering quality, not protocol novelty. The commercial and technical description lives in `README.md`; development material lives in `docs/` and the local workspace.

## Hard rules

1. **ATtiny only.** PIC is a research source, never a build target. Decided 2026-10-04.
2. **C17 with MISRA C:2012 at zero deviations, in a pinned Docker toolchain.** MISRA covers only through C18, so C23 is out, matching the reference project's own superseding decision. See `docs/decisions/0001`. Every build, check and test runs in the pinned image; only programming with `avrdude` runs on the host.
3. **Console clock primary, internal fallback.** The modchip derives its clock from the console as the primary path, as Mayumi does, on the boards whose clock that approach can use (PU-18 and later). For old boards whose console clock differs (PU-7, PU-8), it falls back to the MCU internal oscillator or another console clock source. The external-clock path is gated on measuring the mechacon frequency and voltage first; until then the buildable default is the internal 8 MHz build, with the external build's F_CPU set to the measured frequency. Do not feed any console signal to a clock pin before the voltage is measured. See `docs/decisions/0002`.
4. **Classic-ATtiny split.** ATtiny84A 14-pin PDIP is the full build; ATtiny85 8-pin PDIP is the minimal variant. Classic parts are the only ones meeting external clock, DIP, and simavr together. See `docs/decisions/0003`.
5. **Full scope in version one, including the BIOS patch.** The boot-ROM patch for Japanese fat and PAL-PSone ships in v1 on the 14-pin build. The accepted risk, recorded in `docs/decisions/0004`, is that the hardest path is built before hardware exists; it is verified in the simavr console model first and every BIOS-patch claim stays Unknown until a real console confirms it.
6. **One simple optional LED, single colour.** Never RGB or bicolor. The firmware is correct with no LED fitted, and the LED never drives the package choice.
7. **No proprietary ROM content in the tree.** BIOS identities and behaviour only. Local dumps used for analysis stay outside the tree and are referenced by SHA-256 in a manifest.
8. **No comments in source files except the two SPDX header lines.** Names carry meaning; rationale lives here, in the READMEs, or in commit messages.
9. **Registers are touched only in assembly**, behind C prototypes. No C file includes an `avr/` or `util/` header, because avr-libc reaches registers through casts MISRA forbids.
10. **Every timing constant carries its origin**: a measured bit cell, a datasheet figure, or a simulation. None is copied from another chip without derivation.
11. **Every fact is tagged Read, Concluded, Verified, or Unknown.** Never present inference as measured fact.
12. **Modes are selected by reset-hold on fat consoles and by lid open/close on the PSone**, stored in EEPROM, and cover the Mayumi, MM3, OneChip and PsNee modes: default/strongest, alternate timing, old-modchip, disabled, with a Universal region cycle. See `docs/decisions/0005` and `docs/modes.md`.
13. **Injection uses the canonical 4ms-bit mirror model** agreed by MM3, Mayumi and the classic PsNee, not the kalymos V9 fixed-edge variant. A newer source never overrides an older, widely-deployed one without a stated reason, and every binary source is recorded with its SHA-256. See `docs/decisions/0006`.

## Layers

| Layer | Files | May include |
|---|---|---|
| Logic | `src/region.c`, `src/subq.c`, `src/board_mode.c`, `src/inject.c`, `include/pscu/*.h` | `<stdint.h>`, `<stdbool.h>`, `<stddef.h>`, `pscu/` |
| Platform C | `src/main.c` (not yet written) | the above plus `port/` |
| Hardware | `src/port.S`, the injection handler, `include/port/*.h` (not yet written) | the above plus `avr/`, `util/` |

The logic layer compiles and runs on the host, which is how it reaches full coverage. A layer check will fail the build when a logic file reaches the platform or a C file includes a hardware header.

## Proposed pin maps (inferred, not hardware-verified)

Signals, from the PsNee pin usage. These are Concluded from PsNee and the board research, not Observed on hardware.

Minimal build, ATtiny85 8-pin, no BIOS patch:

| Signal | Direction | Purpose |
|---|---|---|
| DATA | out, drive-low or high-Z | SCEx injection |
| WFCK | in and out | board detect, gate on legacy, carrier sync on PU-22+ |
| SQCK | in | SUBQ serial clock |
| SUBQ | in | SUBQ serial data |
| LED | out, optional | status |

Full build, ATtiny84A 14-pin, adds for the BIOS patch on Japanese fat and PAL-PSone: an address-bus line AX, a second address line AY for the two-phase patch, a data-bus line DX, and RESET. The exact ATtiny pin assignment is set once the package and the external-clock option are fixed and checked against the datasheet.

## Timing budget (origins and unknowns)

| Item | Value | Origin |
|---|---|---|
| SCEx frame | 44 bits, LSB first | Read, PsNee source |
| Legacy bit cell | 4 ms per bit, about 250 baud | Read, PsNee BIT_DELAY and the PsNee wiki "nominal region bit 4 ms" |
| WFCK-sync bit | modulated across 30 WFCK edges | Read, PsNee PerformInjectionSequence |
| WFCK frequency | about 7.3 kHz init, 14.6 kHz read | Read as PsNee comment, not measured here |
| Mechacon clock, external-clock option | likely 4.2336 MHz, equal to 16.9344 MHz divided by 4 | Concluded, snippet-sourced, needs datasheet confirmation |
| Signal voltages | Unknown | no source, must be measured |

Do not implement from a guess. A hardware fact enters the code only with a source beside it here or in the research docs.

## Test tiers

| Tier | What it runs | Gate |
|---|---|---|
| Host | `tests/host/host_test.c` against the logic layer with asserts on | full line and branch coverage, gcovr in the pinned image, assert false-paths excluded by pattern |
| Simulation | the release image in simavr with a console model driving SQCK, SUBQ, WFCK and the BIOS-patch address bus | every firmware instruction, scenarios mapped to the spec, across the oscillator tolerance band on each chip image |
| Tools | Python build and check tooling | full coverage |

Plus mutation testing and reproducible builds. No compatibility claim is Verified without a hardware or simulation result.

## Prior art (sourced)

- PsNee, kalymos/PsNee V9.0, MIT, is the modern open synthesis and the primary protocol reference. SCEx strings, 44-bit LSB-first injection, legacy and WFCK-sync methods, WFCK board detection, SUBQ-decode stealth, and the BIOS patch are read from its source.
- Mayumi V4 derived timing from the console mechacon clock, which is why it cannot run on PU-7/PU-8. MM3 switched to the internal oscillator to gain those boards. Read, consolemods.
- Old Crow released the canonical open 4-wire non-stealth chip. OneChip added the PAL-PSone boot-ROM patch and dropped reset-button mode selection.

## Decided against

| Idea | Why |
|---|---|
| C23 | MISRA C:2012 covers only through C18; the quality bar is the point |
| Console-derived clock as default | loses PU-7/PU-8; kept only as an optional path |
| Modern tinyAVR 0/1/2-series | no DIP and not modeled by simavr; migration target only |
| Becoming an ODE, or PS2/Saturn support | out of scope; mined for transferable ideas only |
| Two-phase scope | the owner chose full scope in v1 |

## State (as of 2026-10-04)

- Discovery complete. The specification, hardware model, research, and the accepted decision records are in `docs/`.
- Pinned Docker toolchain built and working (`openscex-modchip-toolchain`); every build, check and test runs in it through `tools/docker_make.py`.
- Firmware builds for both chips: ATtiny85 minimal (`src/run_basic.c`) at 624 bytes and ATtiny84 full-mode (`src/run_modes.c`) at 976 bytes, sharing `src/engine.c` and `src/port.S`. The mode system (reset-hold / lid open-close selection, EEPROM persistence, the four modes, Universal cycle) is implemented on the 84.
- Gates green in-container: `make all size`, `make analyse` (clang-format, ruff, reuse lint, MISRA C:2012 zero deviations on both chip configs across debug and release, tool coverage 100 percent), `make hosttest` (37 checks, 0 failures, gcovr lines/functions/branches 100 percent, assert false-paths excluded), `make simtest` (the simavr console model drives SQCK/SUBQ/WFCK and watches DATA/LED; 7 checks at each of 7.2/8.0/8.8 MHz, decoding injected SCEI bit-exact on legacy and modern boards, a non-TOC no-inject negative, and 84 EEPROM mode restore for disabled and old-modchip).
- Not yet done: the BIOS-patch handler, the reset/lid gesture sim scenario, the external-clock build, mutation and reproducible-build checks, CI, the revision-to-tap-point READMEs, and any hardware result. Nothing has run on a console.
