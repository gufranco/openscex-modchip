#!/usr/bin/env sh
# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

# Conventional-commit subject check, shared by CI and the commit-msg hook.
#   check-commit-messages.sh <base-sha> <head-sha>   every commit in base..head
#   check-commit-messages.sh --message-file <file>   one message, as git hooks pass it
# Merge commits and Dependabot and semantic-release commits are skipped. A
# message file may also be a merge or a fixup git wrote, which a rebase folds
# away before it is pushed; pushed, CI still refuses a fixup.

set -eu

types='feat|fix|docs|style|refactor|perf|test|build|ci|chore|revert'
# The subject may name a version such as 0.18.0; only a trailing period fails.
subject_pattern="^(${types})(\([a-z0-9-]+\))?: [a-z](.*[^.])?$"
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
        case "${subject}" in
        'Merge '* | 'fixup! '* | 'squash! '* | 'amend! '*) exit 0 ;;
        esac
        check_subject "${subject}" "commit message"
        exit $?
fi

base=$1
head=$2
# A new branch or a force push has no usable base, so every commit since the
# last release tag is checked, or only the head when there is no tag yet.
if [ "${base}" = "${zero_sha}" ] || ! git merge-base --is-ancestor "${base}" "${head}" 2>/dev/null; then
        if tag=$(git describe --tags --abbrev=0 --match 'v*' "${head}" 2>/dev/null); then
                range=$(git rev-list --no-merges "${tag}..${head}")
        else
                range=$(git rev-list --no-merges --max-count=1 "${head}")
        fi
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
