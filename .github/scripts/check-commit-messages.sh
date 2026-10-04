#!/usr/bin/env sh
# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

set -eu

if [ "$#" -ne 2 ]; then
        printf 'usage: %s <base-sha> <head-sha>\n' "$0" >&2
        exit 2
fi

base=$1
head=$2
types='feat|fix|docs|style|refactor|perf|test|build|ci|chore|revert'
subject_pattern="^(${types})(\([a-z0-9-]+\))?: [a-z][^.]*$"
max_length=50
bad=0

range=$(git rev-list "${base}..${head}")
for sha in ${range}; do
        subject=$(git show -s --format=%s "${sha}")
        length=$(printf '%s' "${subject}" | wc -c | tr -d ' ')
        if ! printf '%s' "${subject}" | grep -Eq "${subject_pattern}"; then
                printf 'invalid subject format: %s  %s\n' "${sha}" "${subject}" >&2
                bad=1
        elif [ "${length}" -gt "${max_length}" ]; then
                printf 'subject over %s chars (%s): %s  %s\n' "${max_length}" "${length}" "${sha}" "${subject}" >&2
                bad=1
        fi
done

exit "${bad}"
