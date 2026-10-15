#!/usr/bin/env sh
# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

# Compare the tools the Dockerfile pins by ARG, which Dependabot does not track,
# with each project's latest GitHub release. Exits 1 when a pin is behind, so a
# scheduled run turns red and names the tool; the digests beside each pin are
# updated with the version, by hand.
#   check-toolchain-pins.sh [Dockerfile]
# Needs gh with a token in GH_TOKEN.

set -eu

dockerfile=${1:-Dockerfile}
behind=0

check() {
	name=$1
	repository=$2
	prefix=$3
	pinned=$(sed -n "s/^ARG ${name}=//p" "${dockerfile}")
	if [ -z "${pinned}" ]; then
		printf '::error::%s has no ARG %s\n' "${dockerfile}" "${name}" >&2
		behind=1
		return
	fi
	latest=$(gh api "repos/${repository}/releases/latest" --jq .tag_name)
	latest=${latest#"${prefix}"}
	if [ "${pinned}" != "${latest}" ]; then
		printf '::error::%s is pinned at %s; the latest %s release is %s\n' \
			"${name}" "${pinned}" "${repository}" "${latest}" >&2
		behind=1
	else
		printf '%s %s is the latest release\n' "${name}" "${pinned}"
	fi
}

check CPPCHECK_VERSION danmar/cppcheck ''
check ACTIONLINT_VERSION rhysd/actionlint v

exit "${behind}"
