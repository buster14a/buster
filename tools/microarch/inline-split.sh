#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#
# Splits IBS op samples (or any perf.data samples) of the functions matching
# PATTERN by the function inlined directly into OUTER (or by source line with
# --lines), through llvm-symbolizer's inline chains:
#   tools/microarch/inline-split.sh BINARY PERF.DATA 'PATTERN' OUTER [--lines]
# e.g. the common-warnings pass's sub-passes:
#   tools/microarch/inline-split.sh bin/compiler-znver4 scratch/zen4-7940hs/profile-znver4-ibs.data \
#       '^check_(driver_common_warnings|common_)' check_driver_common_warnings
set -eu
export LC_ALL=C
binary=$1 data=$2 pattern=$3 outer=$4 mode=${5:-}
perf script -i "$data" -F ip,sym 2>/dev/null | awk -v p="$pattern" '$2 ~ p { print "0x" $1 }' | sort | uniq -c > "${TMPDIR:-/tmp}/inline-split.$$"
trap 'rm -f "${TMPDIR:-/tmp}/inline-split.$$"' EXIT
awk '{ print $2 }' "${TMPDIR:-/tmp}/inline-split.$$" |
    llvm-symbolizer --obj="$binary" --inlining=true --output-style=LLVM |
    awk -v counts="${TMPDIR:-/tmp}/inline-split.$$" -v outer="$outer" -v mode="$mode" '
        BEGIN { while ((getline line < counts) > 0) { split(line, f, " "); n[++k] = f[1] } }
        # Each address prints function/location line pairs, innermost first, then a blank line.
        NF == 0 { if (frames > 0) { flush() } next }
        { if (odd == 0) { name[++frames] = $0; odd = 1 } else { place[frames] = $0; odd = 0 } }
        function flush(    key, i) {
            address += 1
            if (mode == "--lines") { key = place[1]; sub(/:[0-9]+$/, "", key); sub(/.*\//, "", key) }
            else {
                key = name[frames]
                for (i = 1; i <= frames; i++) if (name[i] == outer) { key = i > 1 ? name[i - 1] : outer " (own code)"; break }
            }
            total += n[address]; share[key] += n[address]; frames = 0
        }
        END { if (frames > 0) flush(); for (key in share) printf "%d\t%.1f%%\t%s\n", share[key], 100 * share[key] / total, key; printf "%d\t100.0%%\ttotal\n", total }' |
    sort -t$'\t' -k1,1nr
