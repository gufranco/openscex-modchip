# Contributing

## The most useful contribution

A result from a real console. The firmware passes its full simulation gate but has not yet run on most boards, so a [compatibility report](../../issues/new?template=compatibility.yml) with the console model, board, region and what booted moves the project further than any code change.

## Before changing code

Read [`AGENTS.md`](AGENTS.md). Its rules are hard constraints: ATtiny85 only, the trimmed internal oscillator, four signal wires with no lid wire, registers touched only in assembly, MISRA C:2012 with zero deviations, and every timing constant stating its origin.

## Working on the code

Every build, check and test runs in the pinned Docker toolchain through `make`; you need Docker, Git and, to flash a chip, avrdude.

```bash
make hooks        # once: enables the commit-message and formatting hooks
make size         # image size; fails when less than 256 bytes of flash stay free
make test         # host tests, simavr console model and its stack check, static analysis, MISRA
make repro        # two fresh builds must be byte-identical
make mutate       # mutation testing on the logic layer
make bench_ci     # console bench: this firmware against PsNee in the same scenarios
make bench        # the same with every chip whose image is on this machine
make showcase     # after a full bench: regenerate the README comparison tables
```

The console bench plays one simulated console into several modchip firmwares and checks that this one stays inside what the field-proven chips do: it accepts wherever they accept, keeps its bit cell in their range, starts no later and sends no more outside the region check. PsNee and UberNee are fetched and built at pinned commits, UberNee against the Arduino AVR core 1.8.8 for an ATmega328P. Old Crow, its PIC12F629 port, modavr and OneChip are recorded alongside but never bound this firmware: Old Crow sends strings for as long as it is powered. The Mayumi V4, MM3, Old Crow, modavr and OneChip files carry no licence, so they are never committed; [`artifacts.manifest.json`](artifacts.manifest.json) names each by SHA-256 and says where to obtain it, and the bench skips any image that is missing or differs. The report lands in `build/bench/report.md`.

Only the core scenarios are judged that way; the edge, fault and sense-line scenarios exist to reach code, so their counts are reported and never compared with another chip's.

The showcase judges every chip on named properties instead: silence in the two anti-mod probes games run, a fresh string in the anti-mod v2 re-read, no string in play, console pins floating between strings, and recovery from a stuck SQCK, a WFCK stall and a disc swap. This firmware must hold every one, or a full bench fails; the other chips' outcomes are reported. `make showcase` copies the results into [`bench/showcase-results.json`](bench/showcase-results.json) and regenerates the README comparison tables from them, and a host test fails when a table differs from that file. The README words for each property live in [`bench/showcase-text.json`](bench/showcase-text.json).

The bench also measures instruction coverage, and this firmware must reach all of it: every instruction is executed by some scenario or excused with a reason. A line no console input can reach carries a comment `coverage: unreachable: <reason>` above it, or `coverage: unreachable block: <reason>` above a stretch of assembly that runs to the next blank line; the bench maps instructions to lines through the image's debug information. A full `make bench_ci` fails on any instruction that is neither. Reasons for third-party images live in [`bench/coverage-exclusions.json`](bench/coverage-exclusions.json).

A change is ready when `make all size analyse test repro mutate` passes with 100 percent host line and branch coverage, every mutant killed, zero warnings, at least 256 bytes of flash free and at least 96 bytes of RAM left between the stack and the static data in every simulated scenario. A bug fix adds a test that fails without it.

## Commits and pull requests

- Commit subjects follow [Conventional Commits](https://www.conventionalcommits.org/) and stay within 50 characters; the hook and CI enforce both, and releases are versioned from them.
- Keep one concern per commit, and update [`README.md`](README.md) and its Japanese and Chinese versions when behaviour changes.
- Every fact you add is tagged Read, Concluded, Verified or Unknown, as the README explains, and nothing is Verified without a hardware or simulation result.

Security problems go through the [security policy](SECURITY.md), not a public issue.
