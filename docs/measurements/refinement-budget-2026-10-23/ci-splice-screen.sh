#!/bin/bash
# Temporary draft-only orchestration. The GitHub runner hook already owns the 9700X lock.
set -euo pipefail
test "${GITHUB_ACTIONS:-}" = true
evidence=docs/measurements/refinement-budget-2026-10-23
inputs=/var/lib/buster-9700x/refine116-inputs.Ad1OBe
corpus=/var/lib/buster-9700x/corpora/preprocessor-391e729af658/libc
mkdir -p build/refine116
cat <<CHECKSUMS | sha256sum -c -
b1dd5d74ec7f29055a6684fa06fb3c2f6821c87dd38f9a458dfd2e8a1db28189  $inputs/sqlite-3.53.4.c
0a409f1633283fa31a9126b11fbfd64a1991c5d30defad07e5745d4667f5e23d  $inputs/sqlite-3.53.2.c
d9bcc9e2c979d19eea5ed611e8fa971a2af357494c9526fd27d125751e3d9f95  src/buster/compiler/c_refine.c
CHECKSUMS
sha256sum --check "$evidence/sqlite-system-headers.sha256"
git rev-parse HEAD
date -u
uname -srvmo
cat /sys/devices/system/cpu/cpu2/cpufreq/scaling_governor /sys/devices/system/cpu/cpu10/online
executables=()
for variant in fixed coldpref directspace directcold; do
    overlay="build/refine116/$variant"
    mkdir -p "$overlay/buster/compiler"
    for component in c_lexer c_refine c_preprocessor; do
        cp "src/buster/compiler/$component.c" "$overlay/buster/compiler/$component.c"
        if [ "$variant" != fixed ]; then
            patch --batch --fuzz=0 "$overlay/buster/compiler/$component.c" "$evidence/candidates/$variant-$component.patch"
        fi
    done
    sha256sum "$overlay"/buster/compiler/*.c
    clang -O3 -g -march=native -DBUSTER_REFINE_SOURCE_CHUNK=131072 -I"$overlay" -Isrc -Isrc/buster/compiler \
        -fwrapv -funsigned-char -fno-strict-aliasing -Wall -Werror "$evidence/fifth-main.c" -o "build/refine116/pp-$variant"
    taskset -c 2 "build/refine116/pp-$variant" --test --count 2000 --seed 116 src "$inputs/sqlite-3.53.4.c" "$corpus"
    executables+=("build/refine116/pp-$variant")
    if [ "$variant" = fixed ] || [ "$variant" = directcold ]; then
        clang -O3 -g -march=native -I"$overlay" -Isrc -fwrapv -funsigned-char -fno-strict-aliasing -Wall -Werror \
            "$evidence/refinement-main.c" -o "build/refine116/kernel-$variant"
    fi
done
overlay=build/refine116/directcold
clang -O2 -g -march=native -I"$overlay" -Isrc -Isrc/buster/compiler -fwrapv -funsigned-char -fno-strict-aliasing -Wall -Werror \
    "$evidence/chunk-prepare-boundaries.c" -o build/refine116/chunk-prepare-boundaries
taskset -c 2 build/refine116/chunk-prepare-boundaries "$inputs/sqlite-3.53.4.c" "$inputs/sqlite-3.53.2.c" src/buster/compiler/main.c
clang -O3 -g -march=native -DBUSTER_REFINE_SOURCE_CHUNK=64 -I"$overlay" -Isrc -Isrc/buster/compiler \
    -fwrapv -funsigned-char -fno-strict-aliasing -Wall -Werror "$evidence/files-test-main.c" -o build/refine116/files-chunks
taskset -c 2 build/refine116/files-chunks --seed 116 --id-order 19
taskset -c 2 build/refine116/files-chunks --preprocessor --clang -march=native -Isrc -fwrapv -funsigned-char \
    src/buster/compiler/main.c "$inputs/sqlite-3.53.4.c"
sha256sum build/refine116/pp-* build/refine116/kernel-*
./build/compiler ab --rounds 3 --cpu 2 "${executables[@]}" \
    -- --only vectorized "$inputs/sqlite-3.53.4.c" \
    -- --only vectorized "$inputs/sqlite-3.53.2.c" \
    -- --only vectorized -march=native -Isrc -fwrapv -funsigned-char src/buster/compiler/main.c \
    | tee build/refine116/screen.txt
./build/compiler ab --rounds 3 --cpu 2 build/refine116/pp-{fixed,directspace,directcold} \
    -- --setup "$inputs/sqlite-3.53.4.c" | tee build/refine116/setup.txt
./build/compiler ab --rounds 3 --cpu 2 build/refine116/kernel-{fixed,directcold} \
    -- --only refine_tokens_64 "$inputs/sqlite-3.53.4.c" \
    -- --only refine_tokens_64 --after-scalar 100 "$inputs/sqlite-3.53.4.c" \
    -- --only refine_tokens_64 "$corpus" \
    | tee build/refine116/kernels.txt
sha256sum --check "$evidence/sqlite-system-headers.sha256"
