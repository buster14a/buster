#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# Fixed fatal policy, detector canaries and report audit for hosted CI. A report
# also fails the audit when an expected-failure fixture swallowed its exit code.
set -euo pipefail

sanitizer_options()
{
    local reports=$1 symbolizer
    [[ "$reports" = /* && "$reports" != *$'\n'* && "$reports" != *:* ]] || { echo 'sanitizers: report path must be absolute, without newlines or colons' >&2; return 1; }
    mkdir -p "$reports"
    symbolizer=$(command -v llvm-symbolizer)
    export ASAN_SYMBOLIZER_PATH="$symbolizer"
    export MSAN_SYMBOLIZER_PATH="$symbolizer"
    export ASAN_OPTIONS="halt_on_error=1:abort_on_error=0:exitcode=86:detect_leaks=1:detect_stack_use_after_return=1:strict_string_checks=1:check_initialization_order=1:symbolize=1:log_path=$reports/asan"
    # Integrated runtimes share common flags. Give them one exit code rather
    # than depending on the runtime's option-parser initialization order.
    export LSAN_OPTIONS="exitcode=86:print_suppressions=1:log_path=$reports/lsan"
    export UBSAN_OPTIONS="halt_on_error=1:exitcode=86:print_stacktrace=1:symbolize=1:external_symbolizer_path=$symbolizer:log_path=$reports/ubsan"
    export TSAN_OPTIONS="halt_on_error=1:exitcode=86:second_deadlock_stack=1:external_symbolizer_path=$symbolizer:log_path=$reports/tsan"
    export MSAN_OPTIONS="halt_on_error=1:exitcode=86:symbolize=1:external_symbolizer_path=$symbolizer:log_path=$reports/msan"
}

case "${1:-}" in
    configure)
        sanitizer_options "$2"
        {
            printf 'BUSTER_SANITIZER_REPORTS=%s\n' "$2"
            for name in ASAN_SYMBOLIZER_PATH MSAN_SYMBOLIZER_PATH ASAN_OPTIONS LSAN_OPTIONS UBSAN_OPTIONS TSAN_OPTIONS MSAN_OPTIONS; do
                printf '%s=%s\n' "$name" "${!name}"
            done
        } >> "$GITHUB_ENV"
        ;;
    probe)
        directory=$2
        mkdir -p "$directory"
        mapfile -d '' -t flags < <(./build.sh print-sanitizer-flags)
        [ "${#flags[@]}" -gt 0 ] || { echo 'sanitizers: probe requires BUSTER_SANITIZER' >&2; exit 1; }
        clang -std=c17 -Wall -Wextra -Werror -pthread "${flags[@]}" tests/sanitizers/probe.c -o "$directory/probe"
        sanitizer_options "$directory/clean"
        "$directory/probe" clean
        tests=()
        case "$BUSTER_SANITIZER" in
            address) tests=('heap-overflow|86|AddressSanitizer: heap-buffer-overflow' 'use-after-free|86|AddressSanitizer: heap-use-after-free' 'use-after-return|86|AddressSanitizer: stack-use-after-return' 'leak|86|LeakSanitizer: detected memory leaks' 'undefined-shift|86|runtime error:' 'bounds|86|runtime error:') ;;
            undefined) tests=('undefined-shift|86|runtime error:' 'bounds|86|runtime error:') ;;
            thread) tests=('race|86|ThreadSanitizer: data race') ;;
            memory) tests=('uninitialized|86|MemorySanitizer: use-of-uninitialized-value') ;;
            *) echo 'sanitizers: unknown probe mode' >&2; exit 1 ;;
        esac
        for test in "${tests[@]}"; do
            IFS='|' read -r name expected diagnostic <<< "$test"
            reports="$directory/$name"
            sanitizer_options "$reports"
            status=0
            "$directory/probe" "$name" > "$reports/stdout" 2> "$reports/stderr" || status=$?
            if [ "$status" -ne "$expected" ] || ! grep -r -F -q "$diagnostic" "$reports"; then
                printf '::error::%s probe: expected exit %s and %s; got exit %s\n' "$name" "$expected" "$diagnostic" "$status"
                find "$reports" -type f -exec cat {} +
                exit 1
            fi
            # A working symbolizer must name this original source in its report.
            grep -r -F -q 'tests/sanitizers/probe.c:' "$reports"
            printf 'Detected %s (exit %s, symbolized report)\n' "$name" "$status"
        done
        ;;
    audit)
        reports=$2
        test -d "$reports"
        find "$reports" -type f -size +0c -print > "$reports/../sanitizer-report-files.txt"
        if [ -s "$reports/../sanitizer-report-files.txt" ]; then
            printf '%s\n' '::error::Sanitizer diagnostics were emitted, including by child processes. See the uploaded reports.'
            while IFS= read -r path; do
                printf '\n%s\n' "$path"
                cat "$path"
            done < "$reports/../sanitizer-report-files.txt"
            exit 1
        fi
        echo 'No sanitizer reports from the compiler, test runner or child processes.'
        ;;
    *) echo 'usage: sanitizers.sh configure|probe|audit ABSOLUTE_DIRECTORY' >&2; exit 1 ;;
esac
