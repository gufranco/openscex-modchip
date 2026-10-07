# Contributing

## The most useful contribution

A result from a real console. The firmware passes its full simulation gate but has not yet run on most boards, so a [compatibility report](../../issues/new?template=compatibility.yml) with the console model, board, region and what booted moves the project further than any code change.

## Before changing code

Read [`AGENTS.md`](AGENTS.md). Its rules are hard constraints: ATtiny85 only, the trimmed internal oscillator, four signal wires with no lid wire, registers touched only in assembly, MISRA C:2012 with zero deviations, and every timing constant stating its origin.

## Working on the code

Every build, check and test runs in the pinned Docker toolchain through `make`; you need Docker, Git and, to flash a chip, avrdude.

```bash
make hooks        # once: enables the commit-message and formatting hooks
make test         # host tests, simavr console model, static analysis, MISRA
make repro        # two fresh builds must be byte-identical
make mutate       # mutation testing on the logic layer
```

A change is ready when `make all size analyse test repro mutate` passes with 100 percent host line and branch coverage, every mutant killed, and zero warnings. A bug fix adds a test that fails without it.

## Commits and pull requests

- Commit subjects follow [Conventional Commits](https://www.conventionalcommits.org/) and stay within 50 characters; the hook and CI enforce both, and releases are versioned from them.
- Keep one concern per commit, and update [`README.md`](README.md), its Japanese and Chinese versions, and the behaviour spec when behaviour changes.
- Every fact you add is tagged Read, Concluded, Verified or Unknown, as the README explains, and nothing is Verified without a hardware or simulation result.

Security problems go through the [security policy](SECURITY.md), not a public issue.
