# Contributing

## The most useful contribution

A result from a real console: the firmware passes its simulation gate but has not run on most boards. Open a [compatibility report](../../issues/new?template=compatibility.yml) with the console model, board, region and what booted.

## Before changing code

Read [`AGENTS.md`](AGENTS.md); its rules are hard constraints.

## Working on the code

Every build, check and test runs in the pinned Docker toolchain through `make`; you need Docker, Git, Python 3, which runs make's Docker wrapper, and, to flash a chip, avrdude.

```bash
make hooks        # once: enables the commit-message and formatting hooks
make size         # image size; fails when less than 256 bytes of flash stay free
make test         # host tests, simavr console model and its stack check
make analyse      # formatting, lint, types, licences, MISRA and the Python tests
make repro        # every release image, built twice from clean, byte-identical
make mutate       # mutation testing on the logic layer
make mutate_platform  # mutation of run.c and engine.c, killed by the simulator
make bench_ci     # console bench: this firmware against PsNee in the same scenarios
make bench        # the same with every chip whose image is on this machine
make showcase     # after a full bench: regenerate the README comparison tables
```

The console bench plays one simulated console into several modchip firmwares and checks that this one stays within what field-proven chips do: it accepts wherever they accept, keeps its bit cell in their range, starts no later and sends no more outside the region check. PsNee and UberNee are built from pinned commits, UberNee against Arduino AVR core 1.8.8 for an ATmega328P. Old Crow, modavr and OneChip are reported but never bind this firmware, since Old Crow sends strings for as long as it is powered. The Mayumi V4, MM3, Old Crow, modavr and OneChip files carry no licence and are never committed; [`artifacts.manifest.json`](artifacts.manifest.json) names each by SHA-256 and where to obtain it, and the bench skips any image that is missing or differs. The report lands in `build/bench/report.md`.

Only the core scenarios are compared with other chips; the edge, fault and sense-line scenarios exist to reach code and are only reported.

The showcase judges every chip on named properties: silence in the two anti-mod probes, a fresh string in the anti-mod v2 re-read, no string in play, console pins floating between strings, and recovery from a stuck SQCK, a WFCK stall and a disc swap. This firmware must hold every one, or a full bench fails. `make showcase` writes the results to [`bench/showcase-results.json`](bench/showcase-results.json) and regenerates the README tables from it; a host test fails when a table differs from that file. The README wording for each property lives in [`bench/showcase-text.json`](bench/showcase-text.json).

The bench also measures instruction coverage: every instruction of this firmware must be executed by some scenario or excused. A line no console input can reach carries `coverage: unreachable: <reason>` above it, or `coverage: unreachable block: <reason>` above a stretch of assembly running to the next blank line; the bench maps instructions to lines through the image's debug information. A full `make bench_ci` fails on any instruction that is neither. Reasons for third-party images live in [`bench/coverage-exclusions.json`](bench/coverage-exclusions.json).

`make mutate_platform` plays the whole simulator suite once per mutant, so CI runs it in its own workflow, [`.github/workflows/mutation.yml`](.github/workflows/mutation.yml), only when the firmware, the simulator suite, the mutation tool or the pinned toolchain changes; it must also kill every mutant.

A change is ready when `make all size analyse test repro mutate` passes with 100 percent host line and branch coverage, every mutant killed, zero warnings, at least 256 bytes of flash free and at least 96 bytes of RAM left between the stack and the static data in every simulated scenario. A bug fix adds a test that fails without it.

## Commits and pull requests

- Commit subjects follow [Conventional Commits](https://www.conventionalcommits.org/) and stay within 50 characters; the hook and CI enforce both, and releases are versioned from them.
- Keep one concern per commit, and update [`README.md`](README.md) and its Japanese and Chinese versions when behaviour changes.

Security problems go through the [security policy](SECURITY.md), not a public issue. Everyone taking part follows the [code of conduct](CODE_OF_CONDUCT.md).
