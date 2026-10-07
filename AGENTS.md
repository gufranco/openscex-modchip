# AGENTS.md

Rules for anyone, human or agent, working in this repository. What the project is and how to build, wire and flash it: [`README.md`](README.md).

## Rules

1. **ATtiny85 only.** PIC and the ATtiny84 are research sources, never build targets. Every board from the PU-7 to the PM-41(2) is a target.
2. **C17, MISRA C:2012 at zero deviations, pinned toolchain.** Every build, check and test runs in the pinned Docker image through `make`; only `avrdude` runs on the host.
3. **Registers only in assembly.** Hardware access lives in `src/port.S` (the console's signals) and `src/port_chip.S` (the chip's housekeeping) behind C prototypes; no C file includes an `avr/` or `util/` header.
4. **Layers.** The logic layer (`region`, `subq`, `board_mode`, `inject`, `led`, `calib`, `trim`, `supply`, `loop`) includes only the standard headers and `pscu/` and is host-tested; platform C (`engine`, `run`, `main`) may add `port/`; only the two `.S` files touch hardware.
5. **One mode, fully stealth.** SCEx is injected only inside the SUBQ region-check window, at most `PSCU_STEALTH_STRINGS` strings per arming, `PSCU_STEALTH_GAP_FRAMES` frames apart, using the canonical 4 ms-bit mirror model; outside the window DATA is high-Z. Once the console reads the program area the chip stays silent until the disc leaves, seen as `PSCU_DISC_GONE_MS` without a valid SUBQ frame. No mode system, no reset or lid wiring.
6. **One region per build.** `REGION=jp|us|eu`; the chip emits only that region's string, never all three.
7. **No hardware beyond the chip.** Only the ATtiny85, four signal wires (SQCK, SUBQ, DATA, WFCK) and power, each a bare wire; PB5 stays RESET so the chip remains ISP-programmable. The optional single-colour LED never gates or delays a feature, never sits on a timing path, and gives up its pin to any feature that needs it. The LED is the only diagnostic channel. EEPROM holds only values measured on this console, never a mode or a user setting, and a missing or damaged record falls back to the fixed defaults.
8. **Internal oscillator, trimmed.** The chip runs from its internal 8 MHz RC oscillator, never from an external clock or crystal. It trims OSCCAL against the console's SUBQ frame rate one step at a time, within `PSCU_TRIM_MAX_OFFSET` of the factory value and never across CAL7. Every cycle-based constant is derived from F_CPU.
9. **No boot-ROM patch.** The chip does SCEx only; consoles with a second region check in the boot ROM need a patched BIOS.
10. **Every timing constant states its origin:** a source and location, a datasheet figure, or a measurement. None is copied from another chip without derivation. When sources disagree, a long-deployed release wins over a newer one unless a reason is stated.
11. **Every fact is tagged** Read, Concluded, Verified, or Unknown. Nothing is Verified without a hardware or simulation result.
12. **No proprietary ROM content** in the tree; BIOS identities and behaviour only.
13. **Didactic comments** in every source file, tests and tools included, a deliberate override of the global no-comments rule: explain the hardware context, the reason for a constant or a design choice, and the invariant a block upholds; never restate the code. Keep the two SPDX header lines.
14. **Gates before every push:** `make all size analyse test repro mutate` green, 100 percent host line and branch coverage, every mutant killed, zero warnings. A bug fix adds a test that fails without it.
15. **Conventional commit subjects** of at most 50 characters; `make hooks` enforces them, and releases are versioned from them.
