# AGENTS.md

Contract for anyone, human or agent, working in this repository.

## What this project is

Firmware for an ATtiny85 that defeats the Sony PlayStation region lockout by injecting the SCEx magic string the CD subsystem expects, in the SUBQ-gated window the console asks for, with disc swaps re-arming injection through the SUBQ counter. It targets the original PlayStation (fat) and PSone across board families PU-7 through PM-41(2). It does not patch the boot ROM, so Japanese fat consoles and the PAL PSone keep their second region check; those models may still refuse some imports. The protocol and compatibility knowledge comes from decades of community work, above all the open-source PsNee; this project's contribution is engineering quality, not protocol novelty. The commercial and technical description lives in `README.md`; development material lives in `docs/` and the local workspace.

## Hard rules

1. **ATtiny only.** PIC is a research source, never a build target. Decided 2026-10-04.
2. **C17 with MISRA C:2012 at zero deviations, in a pinned Docker toolchain.** MISRA covers only through C18, so C23 is out, matching the reference project's own superseding decision. See `docs/decisions/0001`. Every build, check and test runs in the pinned image; only programming with `avrdude` runs on the host.
3. **Console clock primary, internal fallback.** The modchip derives its clock from the console as the primary path, as Mayumi does, on the boards whose clock that approach can use (PU-18 and later). For old boards whose console clock differs (PU-7, PU-8), it falls back to the MCU internal oscillator or another console clock source. The external-clock path is gated on measuring the mechacon frequency and voltage first; until then the buildable default is the internal 8 MHz build, with the external build's F_CPU set to the measured frequency. Do not feed any console signal to a clock pin before the voltage is measured. See `docs/decisions/0002`.
4. **One chip, one mode.** A single ATtiny85 8-pin PDIP runs one precise mode: SCEx is injected only in the SUBQ-gated window the console asks for, and disc swaps re-arm injection through the SUBQ counter. There is no mode system, no mode selection, no EEPROM state, and no reset or lid wiring. Decided 2026-10-04, superseding `docs/decisions/0003` and `docs/decisions/0005`. The accepted trade-off: dropping the legacy modes drops the runtime fallback, so a board or game that will not boot on this mode needs a firmware change, not an on-device switch.
5. **SCEx injection only; no BIOS patch.** The boot-ROM patch for Japanese fat and PAL-PSone is out of scope, superseding `docs/decisions/0004`. Those models keep their second region check unpatched, so some imports may still refuse to boot on them; the four-wire SCEx chip unlocks the rest.
6. **One simple optional LED, single colour.** Never RGB or bicolor. The firmware is correct with no LED fitted, and the LED never drives the package choice; it is a status output on PB3 within the 85's pin budget.
7. **No proprietary ROM content in the tree.** BIOS identities and behaviour only. Local dumps used for analysis stay outside the tree and are referenced by SHA-256 in a manifest.
8. **Didactic comments throughout every source file.** Owner directive, 2026-10-04, a deliberate project-level override of the global no-comments rule. Every source file, tests and tools included, carries teaching comments that explain the non-obvious: the PlayStation hardware and protocol context, why a timing constant has its value, the reason behind a design choice, and the invariant a block upholds. Never restate what the code plainly says; a comment that only renames the next line is noise and must be removed. Keep the two SPDX header lines. Tool directives (clang-format, cppcheck, shellcheck) stay as needed.
9. **Registers are touched only in assembly**, behind C prototypes. No C file includes an `avr/` or `util/` header, because avr-libc reaches registers through casts MISRA forbids.
10. **Every timing constant carries its origin**: a measured bit cell, a datasheet figure, or a simulation. None is copied from another chip without derivation.
11. **Every fact is tagged Read, Concluded, Verified, or Unknown.** Never present inference as measured fact.
12. **Injection uses the canonical 4ms-bit mirror model** agreed by MM3, Mayumi and the classic PsNee, not the kalymos V9 fixed-edge variant. A newer source never overrides an older, widely-deployed one without a stated reason, and every binary source is recorded with its SHA-256. See `docs/decisions/0006`.
13. **Fully stealth, one configured region, only when needed.** Owner directive, 2026-10-04. The build is region-specific (`REGION=jp|us|eu`, default America) and emits only that one region string, never all three. It injects only inside the SUBQ region-check window, capped at `PSCU_STEALTH_STRINGS` strings per arming, then leaves DATA high-Z and the LED off for the rest of the session, so the chip is electrically silent during play. Leaving the window re-arms the one-shot, which is how disc swaps re-trigger. The stealth state machine is pure and host-tested in `src/inject.c`.

## Layers

| Layer | Files | May include |
|---|---|---|
| Logic | `src/region.c`, `src/subq.c`, `src/board_mode.c`, `src/inject.c`, `include/pscu/*.h` | `<stdint.h>`, `<stdbool.h>`, `<stddef.h>`, `pscu/` |
| Platform C | `src/engine.c`, `src/run.c`, `src/main.c` | the above plus `port/` |
| Hardware | `src/port.S`, `include/port/*.h` | the above plus `avr/`, `util/` |

The logic layer compiles and runs on the host, which is how it reaches full coverage. A layer check will fail the build when a logic file reaches the platform or a C file includes a hardware header.

## Pin map

This is the tested PsNee ATtiny85 (`ATTINY_X5`) pin assignment, read from the PsNee `MCU.h`: SQCK PB0, SUBQ PB1, DATA PB2, LED PB3, WFCK PB4. Our `include/port/registers.h` matches it exactly. The owner confirms these pins and the Mayumi/PsNee tap points work, so the pin assignment is proven, not inferred; only the per-pad voltage stays Unknown and must be measured before wiring.

ATtiny85 8-pin, four signal wires plus power and one optional LED:

| Signal | Pin | Direction | Purpose |
|---|---|---|---|
| SQCK | PB0 | in | SUBQ serial clock |
| SUBQ | PB1 | in | SUBQ serial data |
| DATA | PB2 | out, drive-low or high-Z | SCEx injection |
| LED | PB3 | out, optional | status |
| WFCK | PB4 | in and out | board detect, gate on legacy, carrier sync on PU-22+ |

PB5 stays RESET. No reset-sense, lid, address, or data-bus pins: the single-mode SCEx design needs none.

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
| Simulation | the release image in simavr with a console model driving SQCK, SUBQ and WFCK | every firmware instruction, scenarios mapped to the spec, across the oscillator tolerance band |
| Tools | Python build and check tooling | full coverage |

Plus `make mutate` mutation testing at a 100 percent kill rate on the logic layer, and `make repro` reproducible builds. No compatibility claim is Verified without a hardware or simulation result.

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
| A mode system (default/alt/old/disabled, Universal cycle) | one precise SUBQ-gated mode covers the range; dropped 2026-10-04 with its LED-necessity, reset and lid wiring |
| The ATtiny84 14-pin full build | nothing left needs the extra pins once modes and the BIOS patch are gone |
| The boot-ROM BIOS patch (Japanese fat, PAL-PSone) | out of scope; needed the 14-pin and a second region check the SCEx string alone does not satisfy |

## State (as of 2026-10-04)

- Discovery complete. The specification, hardware model, and research are in `docs/`.
- Pinned Docker toolchain built and working (`openscex-modchip-toolchain`); every build, check and test runs in it through `tools/docker_make.py`.
- Firmware builds for the single ATtiny85 at 572 bytes: logic layer (`region`, `subq`, `board_mode`, `inject`), shared engine (`src/engine.c`), the one run loop (`src/run.c`), `src/main.c`, and register access in `src/port.S`. One precise SUBQ-gated SCEx mode with board auto-detect; no modes, EEPROM, reset, or lid. Fully stealth: emits only the compile-time region (`REGION=jp|us|eu`, default us, via `include/pscu/config.h`), only inside the check window, capped by the stealth state machine, then silent. Build knobs: `REGION` and `CLOCK` (internal/external), each tagged into its own build directory so variants never share objects.
- Gates green in-container: `make all size`, `make analyse` (clang-format, ruff, reuse lint, MISRA C:2012 zero deviations across debug and release, tool coverage 100 percent), `make hosttest` (28 checks, 0 failures, gcovr lines/functions/branches 100 percent, assert false-paths excluded), `make simtest` (the simavr console model drives SQCK/SUBQ/WFCK and watches DATA/LED; 5 checks at each of 7.2/8.0/8.8 MHz, decoding injected SCEI bit-exact on legacy and modern boards and a non-TOC no-inject negative), `make mutate` (25/25 mutants killed on the logic layer; `PSCU_ASSERT` guard lines excluded), `make repro` (two fresh builds byte-identical).
- CI runs the gates on push and pull request: `.github/workflows/ci.yml` builds the pinned toolchain image on an `ubuntu-26.04` runner and runs `make all size analyse test`, then `make repro` and `make mutate`, plus a pull-request-only conventional-commit-subject check (`.github/scripts/check-commit-messages.sh`). Green with zero annotations.
- Not yet done: the external-clock build, the revision-to-tap-point READMEs, and any hardware result. Nothing has run on a console.
