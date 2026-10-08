# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

FROM debian:trixie@sha256:9cc080028c43b27d2074d63a5f9caf7166d731494965616c1a6d2827a004585c

ARG CPPCHECK_VERSION=2.22.0
ARG CPPCHECK_SHA256=d74945deb2d50393430e07596b766f8a779512c7f60dac2a30ea64e059ece57b

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      gcc-avr=1:14.2.0-2 \
      avr-libc=1:2.2.1-1 \
      binutils-avr \
      libsimavr-dev=1.6+dfsg-3+b3 \
      gpsim=0.32.1-2+b2 \
      gpsim-dev=0.32.1-2+b2 \
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
