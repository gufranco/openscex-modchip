# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

FROM debian:trixie@sha256:913f6706df59a68922d1dd08f78c2476560a8d367897200a6005b00e5f67c2d5

ARG CPPCHECK_VERSION=2.22.0
ARG CPPCHECK_SHA256=d74945deb2d50393430e07596b766f8a779512c7f60dac2a30ea64e059ece57b
# actionlint's release binaries, one digest per architecture, from the
# release's own checksums file.
ARG ACTIONLINT_VERSION=1.7.12
ARG ACTIONLINT_SHA256_AMD64=8aca8db96f1b94770f1b0d72b6dddcb1ebb8123cb3712530b08cc387b349a3d8
ARG ACTIONLINT_SHA256_ARM64=325e971b6ba9bfa504672e29be93c24981eeb1c07576d730e9f7c8805afff0c6

# All packages come from one dated Debian snapshot, so unpinned ones resolve
# the same everywhere. Snapshots have expired Valid-Until dates; signatures are
# still checked. Plain http works before ca-certificates is installed.
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

# gpsim and avra are pinned to their source release with any rebuild suffix,
# which differs by architecture. avra assembles the Liss AT90S1200 source.
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      gcc-avr=1:14.2.0-2 \
      avr-libc=1:2.2.1-1 \
      binutils-avr \
      libsimavr-dev=1.6+dfsg-3+b3 \
      'gpsim=0.32.1-2+b*' \
      'gpsim-dev=0.32.1-2+b*' \
      gputils=1.5.2-1 \
      'avra=1.4.2+dfsg-1*' \
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
      shellcheck \
 && rm -rf /var/lib/apt/lists/*

# cppcheck from its release tarball. USE_BOOST=Off avoids the warning Auto
# prints when Boost is missing; it is optional.
RUN curl -fsSL -o /tmp/cppcheck.tar.gz "https://github.com/danmar/cppcheck/archive/refs/tags/${CPPCHECK_VERSION}.tar.gz" \
 && echo "${CPPCHECK_SHA256}  /tmp/cppcheck.tar.gz" | sha256sum -c - \
 && tar -xzf /tmp/cppcheck.tar.gz -C /tmp \
 && cmake -S "/tmp/cppcheck-${CPPCHECK_VERSION}" -B /tmp/cppcheck-build -DCMAKE_BUILD_TYPE=Release -DUSE_MATCHCOMPILER=ON -DHAVE_RULES=OFF -DUSE_BOOST=Off \
 && cmake --build /tmp/cppcheck-build --parallel 2 \
 && cmake --install /tmp/cppcheck-build \
 && rm -rf /tmp/cppcheck.tar.gz "/tmp/cppcheck-${CPPCHECK_VERSION}" /tmp/cppcheck-build

# actionlint checks the workflows, and runs shellcheck on their run steps.
RUN arch="$(dpkg --print-architecture)" \
 && case "$arch" in \
      amd64) sum="${ACTIONLINT_SHA256_AMD64}" ;; \
      arm64) sum="${ACTIONLINT_SHA256_ARM64}" ;; \
      *) echo "no actionlint digest for $arch" >&2; exit 1 ;; \
    esac \
 && curl -fsSL -o /tmp/actionlint.tar.gz "https://github.com/rhysd/actionlint/releases/download/v${ACTIONLINT_VERSION}/actionlint_${ACTIONLINT_VERSION}_linux_${arch}.tar.gz" \
 && echo "${sum}  /tmp/actionlint.tar.gz" | sha256sum -c - \
 && tar -xzf /tmp/actionlint.tar.gz -C /usr/local/bin actionlint \
 && rm -f /tmp/actionlint.tar.gz

# Hash-locked Python tools in a venv, kept apart from Debian's packages. The
# venv is first on PATH, so the Makefile's python3 sees them.
COPY docker/requirements-tools.txt /tmp/requirements-tools.txt
RUN python3 -m venv /opt/tools \
 && /opt/tools/bin/pip install --no-cache-dir --require-hashes -r /tmp/requirements-tools.txt \
 && rm -f /tmp/requirements-tools.txt

ENV PATH=/opt/tools/bin:$PATH \
    PSCU_TOOLCHAIN=1 \
    PYTHONDONTWRITEBYTECODE=1 \
    RUFF_CACHE_DIR=/tmp/ruff-cache
WORKDIR /src
