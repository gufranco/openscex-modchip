#!/usr/bin/env sh
# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

# Conventional-commit subject check, shared by CI and the commit-msg hook.
#   check-commit-messages.sh <base-sha> <head-sha>   every commit in base..head
#   check-commit-messages.sh --message-file <file>   one message, as git hooks pass it
# Merge commits are skipped (their subject is generated), and so are commits
# written by bots whose subject format this project does not choose
# (Dependabot, and the semantic-release changelog commit, which carries a
# version number and is pushed with [skip ci]).

set -eu

types='feat|fix|docs|style|refactor|perf|test|build|ci|chore|revert'
subject_pattern="^(${types})(\([a-z0-9-]+\))?: [a-z][^.]*$"
max_length=50
zero_sha='0000000000000000000000000000000000000000'

usage() {
	printf 'usage: %s <base-sha> <head-sha>\n       %s --message-file <file>\n' "$0" "$0" >&2
	exit 2
}

check_subject() {
	subject=$1
	where=$2
	length=$(printf '%s' "${subject}" | wc -c | tr -d ' ')
	if ! printf '%s' "${subject}" | grep -Eq "${subject_pattern}"; then
		printf 'invalid subject format: %s  %s\n' "${where}" "${subject}" >&2
		return 1
	fi
	if [ "${length}" -gt "${max_length}" ]; then
		printf 'subject over %s chars (%s): %s  %s\n' "${max_length}" "${length}" "${where}" "${subject}" >&2
		return 1
	fi
	return 0
}

if [ "$#" -ne 2 ]; then
	usage
fi

if [ "$1" = "--message-file" ]; then
	subject=$(grep -v '^#' "$2" | sed -n '1p')
	check_subject "${subject}" "commit message"
	exit $?
fi

base=$1
head=$2
if [ "${base}" = "${zero_sha}" ]; then
	range=$(git rev-list --no-merges --max-count=1 "${head}")
else
	range=$(git rev-list --no-merges "${base}..${head}")
fi

bad=0
for sha in ${range}; do
	author=$(git show -s --format=%ae "${sha}")
	case "${author}" in
	*'dependabot[bot]'* | *semantic-release-bot*) continue ;;
	esac
	if ! check_subject "$(git show -s --format=%s "${sha}")" "${sha}"; then
		bad=1
	fi
done

exit "${bad}"
