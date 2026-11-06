#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# Prepare the exact broken #1518 base for a checker comparison without changing the triggering checkout.
set -euo pipefail
if [ "$#" != 2 ]; then
    printf 'usage: %s BASE_WORKTREE BASE_SHA\n' "$0" >&2
    exit 2
fi
base=$1
requested=$2
broken=8b1e14b089f462d09e5cfb9a0c44a9806f86422f
root=$(cd "$(dirname "$0")/../.." && pwd)
actual=$(git -C "$base" rev-parse --verify HEAD)
if [[ ! "$requested" =~ ^[0-9a-f]{40}$ ]] || [ "$actual" != "$requested" ]; then
    printf 'checker baseline: requested SHA does not match the temporary worktree\n' >&2
    exit 1
fi
printf 'checker baseline: requested %s\n' "$requested"
if [ "$requested" != "$broken" ]; then
    printf 'checker baseline: unmodified; build and comparison remain mandatory\n'
    exit 0
fi
if ! git -C "$base" diff --quiet HEAD -- || ! git -C "$base" diff --cached --quiet; then
    printf 'checker baseline: refusing to repair a modified worktree\n' >&2
    exit 1
fi
patch="$root/tools/ci/checker-baseline-8b1e14b.patch"
printf '%s  %s\n' e2ed809e06b472de151864b503009b40239698d90b378d3f8594a0a27f6dd8ac "$patch" | sha256sum --check
git -C "$base" apply --check -- "$patch"
git -C "$base" apply -- "$patch"
if [ "$(git -C "$base" diff --name-only HEAD)" != 'src/buster/compiler/c_check_input.c' ]; then
    printf 'checker baseline: repair changed unexpected paths\n' >&2
    exit 1
fi
git -C "$base" diff --check HEAD
git -C "$base" apply --reverse --check -- "$patch"
printf 'checker baseline: repaired exact SHA %s with the build fix from #1522; comparison uses this modified base\n' "$broken"
git -C "$base" diff --no-ext-diff HEAD -- src/buster/compiler/c_check_input.c
