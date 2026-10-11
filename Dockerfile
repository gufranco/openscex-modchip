# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

FROM debian:trixie@sha256:9cc080028c43b27d2074d63a5f9caf7166d731494965616c1a6d2827a004585c

ARG CPPCHECK_VERSION=2.22.0
ARG CPPCHECK_SHA256=d74945deb2d50393430e07596b766f8a779512c7f60dac2a30ea64e059ece57b

# Every Debian package comes from one dated snapshot of the archive, so the
# packages installed without an explicit version (binutils-avr, gcc, make,
# cmake and the rest) resolve to the same build on every machine and every
# rebuild, not to whatever the mirror holds that day. The snapshot of
# 2026-10-08 holds each pinned version below (Read: its trixie arm64 Packages
# index on 2026-10-11). Snapshot Release files carry a Valid-Until date long
# past, hence Check-Valid-Until: no; the archive signature is still checked
# against the image's Debian keyring. Plain http keeps apt working before
# ca-certificates is installed; the signature, not the transport, is the trust.
ARG DEBIAN_SNAPSHOT=20261008T000000Z
RUN rm -f /etc/apt/sources.list.d/debian.sources \
 && printf '%s\n' \
      'Types: deb' \
      "URIs: http://snapshot.debian.org/archive/debian/${DEBIAN_SNAPSHOT}" \
      'Suites: trixie trixie-updates' \
      'Components: main' \
      'Check-Valid-Until: no' \
      'Signed-By: /usr/share/keyrings/debian-archive-keyring.gpg' \
      '' \
      'Types: deb' \
      "URIs: http://snapshot.debian.org/archive/debian-security/${DEBIAN_SNAPSHOT}" \
      'Suites: trixie-security' \
      'Components: main' \
      'Check-Valid-Until: no' \
      'Signed-By: /usr/share/keyrings/debian-archive-keyring.gpg' \
      > /etc/apt/sources.list.d/snapshot.sources

# gpsim is pinned to its source release, 0.32.1-2, with any rebuild suffix: the
# binary rebuild number differs by architecture (+b1 on amd64, +b2 on arm64 in
# trixie, read with apt-cache madison on 2026-10-08), so an exact pin built on
# one machine failed to install on the other. The quotes keep the shell from
# treating the pattern as a file glob; apt matches it against versions.
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      gcc-avr=1:14.2.0-2 \
      avr-libc=1:2.2.1-1 \
      binutils-avr \
      libsimavr-dev=1.6+dfsg-3+b3 \
      'gpsim=0.32.1-2+b*' \
      'gpsim-dev=0.32.1-2+b*' \
      gputils=1.5.2-1 \
      libglib2.0-dev \
      libelf-dev \
      pkg-config \
      gcc \
      libc6-dev \
      make \
      cmake \
      g++ \
      clang-format=1:19.0-63 \
      gcovr \
      python3 \
      python3-venv \
      curl \
      ca-certificates \
      git \
      libmagic1 \
 && rm -rf /var/lib/apt/lists/*

# cppcheck is built from its release tarball. USE_BOOST=Off: Boost is only an
# optional speed-up of one container type (Read: cppcheck 2.22.0
# cmake/options.cmake:34 and :111), the image never had it, and the default
# Auto mode searched for it anyway and printed a warning when it was missing.
RUN curl -fsSL -o /tmp/cppcheck.tar.gz "https://github.com/danmar/cppcheck/archive/refs/tags/${CPPCHECK_VERSION}.tar.gz" \
 && echo "${CPPCHECK_SHA256}  /tmp/cppcheck.tar.gz" | sha256sum -c - \
 && tar -xzf /tmp/cppcheck.tar.gz -C /tmp \
 && cmake -S "/tmp/cppcheck-${CPPCHECK_VERSION}" -B /tmp/cppcheck-build -DCMAKE_BUILD_TYPE=Release -DUSE_MATCHCOMPILER=ON -DHAVE_RULES=OFF -DUSE_BOOST=Off \
 && cmake --build /tmp/cppcheck-build --parallel 2 \
 && cmake --install /tmp/cppcheck-build \
 && rm -rf /tmp/cppcheck.tar.gz "/tmp/cppcheck-${CPPCHECK_VERSION}" /tmp/cppcheck-build

# The hash-locked Python tools (ruff, reuse, coverage) live in their own virtual
# environment rather than in the system Python: pip then never writes over
# packages Debian manages, needs no --break-system-packages, and has no reason
# to warn about running as root. The venv comes first on PATH, so python3 in
# the Makefile is the venv's interpreter and sees the tools.
COPY docker/requirements-tools.txt /tmp/requirements-tools.txt
RUN python3 -m venv /opt/tools \
 && /opt/tools/bin/pip install --no-cache-dir --require-hashes -r /tmp/requirements-tools.txt \
 && rm -f /tmp/requirements-tools.txt

ENV PATH=/opt/tools/bin:$PATH \
    PSCU_TOOLCHAIN=1 \
    PYTHONDONTWRITEBYTECODE=1 \
    RUFF_CACHE_DIR=/tmp/ruff-cache
WORKDIR /src
