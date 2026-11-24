#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
set -euo pipefail
base=${1:?expected the base sqlite3.c object}
head=${2:?expected the PR sqlite3.c object}
stable=${3:-false}

base_bytes=$(stat -c %s -- "$base")
head_bytes=$(stat -c %s -- "$head")
if (( base_bytes == 0 || head_bytes == 0 )); then
    echo "::error::Empty sqlite3.c object (base $base_bytes bytes, PR $head_bytes bytes)"
    exit 1
fi
if cmp -s -- "$base" "$head"; then
    echo "sqlite3.c objects are byte-identical ($base_bytes bytes)"
    exit 0
else
    status=$?
    if (( status != 1 )); then
        echo "::error::Could not compare sqlite3.c objects (cmp exited $status)"
        exit "$status"
    fi
fi

size -- "$base" "$head"
change="sqlite3.c objects differ (base $base_bytes bytes, PR $head_bytes bytes)"
if [[ $stable == true ]]; then
    echo "::error::$change after a diagnostic-only change that must preserve every byte"
    exit 1
fi

delta=$(( head_bytes - base_bytes ))
limit=$(( base_bytes / 10 ))
if (( delta < -limit || delta > limit )); then
    echo "::error::$change, outside the 10% size limit relative to the base"
    exit 1
fi
echo "::warning::$change within the 10% size limit"
