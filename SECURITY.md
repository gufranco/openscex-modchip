# Security policy

## Supported versions

Only the latest release receives fixes. The project is on the `0.x` line and has not yet been validated on a console, so upgrade to the newest release before reporting.

## What counts as a security issue here

The firmware runs on an ATtiny85 inside a console, with no network, no user data, and no secrets. A report is in scope when it shows that the firmware or a published image can:

- drive a console line outside the documented behaviour in a way that can damage hardware, such as holding DATA driven while the lid is open;
- be replaced or altered between the release and the user, for example a release image whose SHA-256 or build-provenance attestation does not verify;
- run build or release automation in this repository with more privilege than its workflow declares.

Ordinary bugs, such as a console that does not boot an import, belong in a [bug report](../../issues/new?template=bug.yml) or a [compatibility report](../../issues/new?template=compatibility.yml).

## How to report

Report privately through GitHub's [private vulnerability reporting](../../security/advisories/new). Do not open a public issue for a security problem.

Include the release or commit, the console model and board, what you observed, and how to reproduce it. A confirmed issue is fixed in a new release, and the advisory credits you unless you ask otherwise.
