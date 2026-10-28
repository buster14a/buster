#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# Frozen test inputs, independent of the physical benchmark host.
set -euo pipefail
root=${1:?usage: fetch-corpus.sh DESTINATION}
archive=$(mktemp)
trap 'rm -f "$archive"' EXIT
curl --fail --location --retry 3 \
    https://github.com/buster14a/buster-performance/releases/download/ci-corpora-preprocessor-391e729af658/preprocessor-391e729af658-ci.tar.zst \
    --output "$archive"
printf '%s  %s\n' ceb39bf938cd7a7469bbb53646c3318d0cbfdac4d37b6d05b183c4fd0b9898c2 "$archive" | sha256sum --check --status
mkdir -p "$root"
tar --zstd --extract --file "$archive" --directory "$root" --no-same-owner
printf '%s  %s\n' \
    391e729af6587355050881752558e790299cfe638c7dc7803bd028a8535ebcbd "$root/MANIFESTS.sha256" \
    8e6e646951cbd08c07bbc76ff46d6796a83a58e9b1c4959278b60193718cd10c "$root/manifests/headers.sha256" \
    e307b836448ef20e603a617a4b5023831c72ff5f307f614c50f4f8833088717c "$root/manifests/headers-bundle.sha256" \
    9179b7ad7db7366d12a214b562e478ac703e7ec0376f38f4516756bcf7ee0047 "$root/manifests/sqlite.sha256" \
    bf593a94954f3ed9431dd12e60233ec0229f7226a1fc270a701614a20f86baeb "$root/manifests/libc.sha256" \
    89dbe35f0324cf08d0cb9cf31ecf15092fc20783a9e470e80e5f41b1d48251d3 "$root/manifests/musl-generated.sha256" | sha256sum --check --status
(cd "$root/headers/usr" && sha256sum --check --status "$root/manifests/headers.sha256")
for entry in headers:headers-bundle libc:libc sqlite:sqlite musl-generated:musl-generated; do
    directory="$root/${entry%:*}"
    manifest="$root/manifests/${entry#*:}.sha256"
    (cd "$directory" && sha256sum --check --status "$manifest")
    test "$(find "$directory" -type f -printf '.\n' | wc -l)" -eq "$(wc -l < "$manifest")"
    test -z "$(find "$directory" ! -type d ! -type f -print -quit)"
done
