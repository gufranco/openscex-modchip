# AGENTS.md

Rules for anyone, human or agent, working in this repository. What the project is and how to build, wire and flash it: [`README.md`](README.md).

## Rules

1. **ATtiny85 and ATtiny84 only.** PIC is a research source, never a build target. One source builds both; pick the 14-pin part only when the console needs the BIOS patch.
2. **C17, MISRA C:2012 at zero deviations, pinned toolchain.** Every build, check and test runs in the pinned Docker image through `make`; only `avrdude` runs on the host.
3. **Registers only in assembly.** Hardware access lives in `src/port.S` behind C prototypes; no C file includes an `avr/` or `util/` header.
4. **Layers.** The logic layer (`region`, `subq`, `board_mode`, `inject`, `diag`) includes only the standard headers and `pscu/` and is host-tested; platform C may add `port/`; only `port.S` touches hardware.
5. **One mode, fully stealth.** SCEx is injected only inside the SUBQ region-check window, at most `PSCU_STEALTH_STRINGS` strings per arming, using the canonical 4 ms-bit mirror model; outside the window DATA is high-Z. Leaving the window re-arms for the next disc. No mode system, no reset or lid wiring.
6. **One region per build.** `REGION=jp|us|eu`; the chip emits only that region's string, never all three.
7. **No hardware beyond the chip.** Only the ATtiny, four signal wires and power. The optional single-colour LED never gates or delays a feature, never sits on a timing path, and gives up its pin to any feature that needs it. Diagnostics go to EEPROM; EEPROM never holds a mode or a setting.
8. **Console clock primary, internal fallback.** The internal oscillator is the default until the console clock's frequency and voltage are measured; an external clock is a bare wire, never through an added part.
9. **The BIOS patch never blocks SCEx.** It is experimental, every wait in it is bounded, a failure falls back to SCEx, and its images are not released until its constants are derived for this chip and confirmed on a console.
10. **Every timing constant states its origin:** a source and location, a datasheet figure, or a measurement. None is copied from another chip without derivation. When sources disagree, a long-deployed release wins over a newer one unless a reason is stated.
11. **Every fact is tagged** Read, Concluded, Verified, or Unknown. Nothing is Verified without a hardware or simulation result.
12. **No proprietary ROM content** in the tree; BIOS identities and behaviour only.
13. **Didactic comments** in every source file, tests and tools included, a deliberate override of the global no-comments rule: explain the hardware context, the reason for a constant or a design choice, and the invariant a block upholds; never restate the code. Keep the two SPDX header lines.
14. **Gates before every push:** `make all size analyse test repro mutate` green, 100 percent host line and branch coverage, every mutant killed, zero warnings. A bug fix adds a test that fails without it.
15. **Conventional commit subjects** of at most 50 characters; `make hooks` enforces them, and releases are versioned from them.
