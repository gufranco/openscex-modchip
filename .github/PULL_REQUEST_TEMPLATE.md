## What

<!-- What changes, and why. Link the issue it closes, if any. -->

## Evidence

<!-- What failed before and passes now, or the output that changed. A bug fix
names the test that fails without it. Hardware results: console model, board,
region and what booted. -->

## Checklist

- [ ] `make all size analyse test repro mutate` passes, with zero warnings
- [ ] `make mutate_platform` passes, if `src/run.c` or `src/engine.c` changed
- [ ] Every commit subject is a Conventional Commit of at most 50 characters
- [ ] `README.md`, `README.ja.md` and `README.zh.md` say the same, and every new claim links to its source
- [ ] No rule in `AGENTS.md` is broken
