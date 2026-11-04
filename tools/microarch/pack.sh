#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#
# Copies one host's results into a buster-performance record
# (docs/measurements/TOPIC-DATE/HOST):
#   tools/microarch/pack.sh build/microarch/results/HOST RECORD/HOST
# The summaries (every *.tsv, *.txt and env file) are copied as they are; the
# per-run raw outputs (perf stat CSVs, status files and logs) go into
# raw.tar.zst beside them, whole. Kernel command lines lose their device UUIDs.
set -eu
export LC_ALL=C
src=$1
dst=$2
mkdir -p "$dst"
# Summaries: everything but the per-run directories.
(cd "$src" && find . -type f ! -path '*/stat/*' ! -path '*/sweep/*' ! -path '*/metrics/*' ! -path '*/logs/*' -print0) |
    (cd "$src" && xargs -0 tar -cf -) | tar -xf - -C "$dst"
# The per-run raw files, whole.
(cd "$src" && find . -type f \( -path '*/stat/*' -o -path '*/sweep/*' -o -path '*/metrics/*' -o -path '*/logs/*' \) -print0 | sort -z |
    tar --null -T - --owner=0 --group=0 --numeric-owner --mtime=2026-11-04 -cf -) | zstd -19 -q -o "$dst/raw.tar.zst"
sed -i 's/rd\.luks[^ ]*//g; s/initrd=[^ ]*//g' "$dst/env/settings.txt"
# What the record must not carry: other sessions' lock messages and links.
if grep -r -l -E 'exclusive\.sh: waiting|claude\.ai/|^  (title|link):' "$dst" --exclude=raw.tar.zst; then
    echo "pack.sh: foreign lock lines in $dst" >&2
    exit 1
fi
if zstd -dc "$dst/raw.tar.zst" | tar -xOf - | grep -q -E 'exclusive\.sh: waiting|claude\.ai/'; then
    echo "pack.sh: foreign lock lines in $dst/raw.tar.zst" >&2
    exit 1
fi
du -sh "$dst" "$dst/raw.tar.zst"
