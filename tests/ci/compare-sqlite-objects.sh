#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
set -euo pipefail
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
compare=tools/ci/compare-sqlite-objects.sh

# Pad two different valid ELF objects so the size boundary is exact to the byte.
for value in 1 2; do
    printf 'int value = %s;\n' "$value" | cc -x c -c - -o "$work/$value.o"
    test "$(stat -c %s "$work/$value.o")" -lt 8999
    truncate -s 10000 "$work/$value.o"
done
check()
{
    local expected_status=$1 expected_text=$2 status=0
    shift 2
    bash "$compare" "$@" > "$work/log" 2>&1 || status=$?
    if [[ $status != "$expected_status" ]] || ! grep -Fq -- "$expected_text" "$work/log"; then
        cat "$work/log" >&2
        echo "expected exit $expected_status and '$expected_text', got exit $status for $*" >&2
        exit 1
    fi
}

check 0 'byte-identical' "$work/1.o" "$work/1.o" true
check 1 '::error::' "$work/1.o" "$work/2.o" true
check 0 '::warning::' "$work/1.o" "$work/2.o" false
for bytes in 9000 11000; do
    truncate -s "$bytes" "$work/2.o"
    check 0 "::warning::sqlite3.c objects differ (base 10000 bytes, PR $bytes bytes)" "$work/1.o" "$work/2.o" false
done
for bytes in 8999 11001; do
    truncate -s "$bytes" "$work/2.o"
    check 1 "::error::sqlite3.c objects differ (base 10000 bytes, PR $bytes bytes)" "$work/1.o" "$work/2.o" false
done
truncate -s 0 "$work/2.o"
check 1 'Empty sqlite3.c object' "$work/2.o" "$work/2.o" false
check 1 'missing.o' "$work/1.o" "$work/missing.o" false
echo 'SQLite object comparison fixtures passed'
