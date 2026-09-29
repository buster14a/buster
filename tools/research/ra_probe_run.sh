#!/usr/bin/env bash
# Isolated diagnostic runner; no production test registration or admission.
set -euo pipefail
out="${1:-/tmp/buster-ra-probe}"
mkdir -p "$out"
common=(-std=c11 -Wall -Wextra -Wpedantic -Werror -fwrapv -fno-strict-aliasing -funsigned-char)
for compiler in clang gcc; do
    "$compiler" "${common[@]}" -O2 tools/research/ra_checker.c tools/research/ra_checker_test.c -o "$out/test-$compiler"
    "$out/test-$compiler" > "$out/test-$compiler.txt"
    cat "$out/test-$compiler.txt"
done
clang "${common[@]}" -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer tools/research/ra_checker.c tools/research/ra_checker_test.c -o "$out/test-sanitize"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$out/test-sanitize" > "$out/test-sanitize.txt"
cat "$out/test-sanitize.txt"
clang -O2 -ffunction-sections -fdata-sections -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-variable \
    -DBUSTER_INCLUDE_TESTS=0 -DBUSTER_LINK_LIBC=1 -DBUSTER_OPTIMIZE=1 -DBUSTER_SINGLE_THREADED=1 \
    -Isrc -fwrapv -fno-strict-aliasing -funsigned-char \
    tools/research/ra_buster_probe.c tools/research/ra_checker.c \
    src/buster/lib/compiler/codegen/machine.c src/buster/lib/arena.c src/buster/lib/os.c \
    src/buster/lib/integer.c src/buster/lib/string.c src/buster/lib/float.c src/buster/lib/time.c \
    -Wl,--gc-sections -lpthread -ldl -lm -o "$out/buster-probe"
"$out/buster-probe" > "$out/buster-probe.txt"
cat "$out/buster-probe.txt"
# Timing is opt-in and requested only on the hosted diagnostic run.
if [[ "${RA_PROBE_HOSTED_BENCH:-0}" == 1 ]]; then
    "$out/test-clang" --bench > "$out/checker-cost.txt"
    cat "$out/checker-cost.txt"
fi
git rev-parse HEAD > "$out/revision.txt"
sha256sum tools/research/ra_checker.h tools/research/ra_checker.c tools/research/ra_checker_test.c \
    tools/research/ra_buster_probe.c "$out/test-clang" "$out/test-gcc" "$out/buster-probe" > "$out/identities.txt"
clang --version > "$out/clang-version.txt"
gcc --version > "$out/gcc-version.txt"
lscpu > "$out/cpu.txt"
