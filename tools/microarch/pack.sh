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
# Every producer's failure fails the pack (pipefail), and the copied summaries and
# the archive are checked against the lists of files they must hold, so a file
# that can't be read or that disappears never leaves an incomplete record.
set -euo pipefail
export LC_ALL=C
src=$1
dst=$2
mkdir -p "$dst"
lists=$(mktemp -d "${TMPDIR:-/tmp}/pack.XXXXXX")
trap 'rm -rf -- "${lists:?}"' EXIT
# Summaries: everything but the per-run directories.
(cd "$src" && find . -type f ! -path '*/stat/*' ! -path '*/sweep/*' ! -path '*/metrics/*' ! -path '*/logs/*' -print0) | sort -z > "$lists/summaries"
(cd "$src" && tar --null -T "$lists/summaries" -cf -) | tar -xf - -C "$dst"
while IFS= read -r -d '' file; do
    if ! cmp -s -- "$src/$file" "$dst/$file"; then
        echo "pack.sh: $file wasn't copied whole" >&2
        exit 1
    fi
done < "$lists/summaries"
# The per-run raw files, whole.
(cd "$src" && find . -type f \( -path '*/stat/*' -o -path '*/sweep/*' -o -path '*/metrics/*' -o -path '*/logs/*' \) -print0) | sort -z > "$lists/raw"
(cd "$src" && tar --null -T "$lists/raw" --owner=0 --group=0 --numeric-owner --mtime=2026-11-04 -cf -) | zstd -19 -q -f -o "$dst/raw.tar.zst"
tr '\0' '\n' < "$lists/raw" > "$lists/raw.lines"
zstd -dc "$dst/raw.tar.zst" | tar -tf - > "$lists/archived"
if ! cmp -s "$lists/raw.lines" "$lists/archived"; then
    echo "pack.sh: raw.tar.zst doesn't hold every raw file" >&2
    exit 1
fi
sed -i 's/rd\.luks[^ ]*//g; s/initrd=[^ ]*//g' "$dst/env/settings.txt"
# What the record must not carry: other sessions' lock messages and links.
if grep -r -l -E 'exclusive\.sh: waiting|claude\.ai/|^  (title|link):' "$dst" --exclude=raw.tar.zst; then
    echo "pack.sh: foreign lock lines in $dst" >&2
    exit 1
fi
# grep reads the whole stream (no -q): pipefail would take tar's SIGPIPE for no match.
if zstd -dc "$dst/raw.tar.zst" | tar -xOf - | grep -E 'exclusive\.sh: waiting|claude\.ai/' > /dev/null; then
    echo "pack.sh: foreign lock lines in $dst/raw.tar.zst" >&2
    exit 1
fi
du -sh "$dst" "$dst/raw.tar.zst"
