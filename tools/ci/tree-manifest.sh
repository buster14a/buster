#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# A staged tree's content manifest: `write DIRECTORY` records the SHA-256 of every regular file and the target of every
# symbolic link (DIRECTORY/.manifest), and `check DIRECTORY` fails unless the tree still has exactly those files, with
# those contents, and those links; other kinds of file are refused. The 9700X's staged official Clang
# (setup-benchpress.sh) is checked on every use with it, so that no tree changed in place is used. Hashing runs in 8
# processes: the staged release is a few hundred megabytes. Nothing is written into the tree but the manifest.
set -euo pipefail
command=${1:?usage: tree-manifest.sh write|check DIRECTORY}
directory=${2:?usage: tree-manifest.sh write|check DIRECTORY}
manifest="$directory/.manifest"
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
# The tree's current entries, in a stable order: `file PATH` and `link PATH TARGET` lines, but the manifest and the
# markers written after it.
entries()
{
    (
        cd "$directory"
        if [ -n "$(find . ! -type f ! -type l ! -type d -print -quit)" ]; then
            echo 'tree-manifest.sh: the tree has entries that are neither files nor links' >&2
            exit 1
        fi
        find . -type f ! -path ./.manifest ! -path ./.complete ! -path ./.correctness-complete ! -path ./.manifest-complete -printf 'file\t%p\n'
        find . -type l -printf 'link\t%p\t%l\n'
    ) | LC_ALL=C sort
}
case "$command" in
    write)
        entries > "$scratch/entries"
        awk -F '\t' '$1 == "file" { printf "%s%c", $2, 0 }' "$scratch/entries" > "$scratch/files"
        (cd "$directory" && xargs -0 -r -n 64 -P 8 sha256sum < "$scratch/files") | LC_ALL=C sort -k 2 > "$scratch/hashes"
        cat "$scratch/entries" "$scratch/hashes" > "$manifest"
        ;;
    check)
        test -f "$manifest"
        status=0
        entries > "$scratch/entries"
        if ! grep -E $'^(file|link)\t' "$manifest" | cmp -s - "$scratch/entries"; then
            echo "tree-manifest.sh: $directory's files or links differ from its manifest" >&2
            status=1
        fi
        if [ "$status" -eq 0 ]; then
            grep -Ev $'^(file|link)\t' "$manifest" > "$scratch/hashes"
            test "$(wc -l < "$scratch/hashes")" -eq "$(grep -c $'^file\t' "$manifest" || true)"
            split -n l/8 "$scratch/hashes" "$scratch/part."
            for part in "$scratch"/part.*; do
                if [ -s "$part" ]; then
                    (cd "$directory" && sha256sum --check --status --strict "$part") &
                fi
            done
            for job in $(jobs -p); do
                wait "$job" || status=1
            done
            if [ "$status" -ne 0 ]; then
                echo "tree-manifest.sh: $directory's contents differ from its manifest" >&2
            fi
        fi
        exit "$status"
        ;;
    *)
        echo "usage: tree-manifest.sh write|check DIRECTORY" >&2
        exit 2
        ;;
esac
