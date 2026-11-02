#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# The build's regressions (.github/workflows/build.yml; docs/commands/build.md): from the repository root, with clang,
# cmake, ctest and ninja on PATH, it removes this settings' tree and checks that
#   - a fresh ./build.sh builds and publishes the Release executable, and its configuration's compile commands;
#   - an unchanged build configures, compiles, links and bakes nothing, and publishes nothing again;
#   - an edited unit, an included .c and a header rebuild their dependents alone (Ninja's plan against its depfiles),
#     and an edited unit relinks and rebakes;
#   - Debug and Release share the tree, and switching back builds nothing and publishes that configuration's file;
#   - BUSTER_BAKE reruns the post-link step alone, both ways, and BUSTER_LINKER the link;
#   - a failed post-link step, a failed compile and an interrupt publish nothing, and the next build recovers;
#   - the presets configure, build and test the same tree, and the print commands print NUL-delimited arguments;
#   - shifts link the padded executables, and lexer-variant builds a tree of its own.
# It leaves the tree built. Each check prints what it checked; the first that fails stops it.
set -euo pipefail
cd "$(dirname "$0")/../.."

fail()
{
    printf 'build-regression: FAILED: %s\n' "$*" >&2
    exit 1
}
pass()
{
    printf 'build-regression: ok: %s\n' "$*"
}

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

./build.sh clean
tree="$(dirname "$(dirname "$(./build.sh print-output)")")"
release="$(./build.sh print-output --config Release)"
debug="$(./build.sh print-output --config Debug)"
log="$tree/.ninja_log"

# The build's own lines, and the outputs whose commands ran since the mark, by Ninja's log: each output's last entry
# (its run's times and the output's time), which Ninja keeps when it compacts the log.
build()
{
    ./build.sh "$@" > "$work/build.log" 2>&1 || { cat "$work/build.log" >&2; return 1; }
}
entries()
{
    awk -F '\t' 'NR > 1 { last[$4] = $1 "\t" $2 "\t" $3 } END { for (output in last) print output "\t" last[output] }' "$log" | sort
}
mark()
{
    entries > "$work/mark"
}
since()
{
    entries | comm -13 "$work/mark" - | cut -f 1
}
# The objects, links and post-link steps Ninja would run now for the configuration $1, by its plan.
plan()
{
    ninja -C "$tree" -f "build-$1.ninja" -n compiler | sed -n 's/^\[[0-9]*\/[0-9]*\] //p'
}

# A fresh build.
build
test -x "$release" || fail "the fresh build left no $release"
[ build/compiler -ef "$release" ] || fail "build/compiler isn't the Release executable"
python3 -I -c 'import json, sys; d = json.load(open(sys.argv[1])); assert d and all("/Release/" in e["output"] for e in d), "configurations"' \
    build/compile_commands.json || fail "build/compile_commands.json isn't the Release commands alone"
[ -z "$(plan Release)" ] || fail "a fresh build left work to do"
pass "a fresh build publishes the Release executable and compile commands"

# An unchanged build.
mark
inode="$(stat -c %i build/compiler)"
cache="$(stat -c %Y "$tree/CMakeCache.txt")"
commands="$(stat -c %Y build/compile_commands.json)"
build
grep -q '^ninja: no work to do\.$' "$work/build.log" || fail "an unchanged build had work to do"
[ -z "$(since)" ] || fail "an unchanged build ran $(since | tr '\n' ' ')"
[ "$(stat -c %i build/compiler)" = "$inode" ] || fail "an unchanged build published build/compiler again"
[ "$(stat -c %Y "$tree/CMakeCache.txt")" = "$cache" ] || fail "an unchanged build configured the tree"
[ "$(stat -c %Y build/compile_commands.json)" = "$commands" ] || fail "an unchanged build rewrote build/compile_commands.json"
pass "an unchanged build compiles, links, bakes and publishes nothing"

# Dependents: each file's dependents by the depfiles, against Ninja's plan once the file is newer, its time restored.
dependents()
{
    ninja -C "$tree" -f build-Release.ninja -t deps | awk -v file="$PWD/$1" '
        / #deps / { object = $1; sub(/:$/, "", object); next }
        $1 == file { print object }' | sed 's|.*/\([^/]*\)\.o$|\1|' | sort
}
for file in src/buster/compiler/tier1_model_measure.c src/buster/compiler/c_lexer.c src/buster/compiler/host_isa.h; do
    expected="$(dependents "$file")"
    [ -n "$expected" ] || fail "no unit depends on $file"
    time="$(stat -c %y "$file")"
    touch "$file"
    planned="$(plan Release | sed -n 's|^Building C object .*/\([^/]*\)\.o$|\1|p' | sort)"
    touch -d "$time" "$file"
    [ "$planned" = "$expected" ] || fail "$file: Ninja would compile $(echo $planned), not $(echo $expected)"
    [ -z "$(plan Release)" ] || fail "$file's time restored, Ninja still has work"
    pass "$file rebuilds its $(echo "$expected" | wc -l) dependents alone"
done
mark
touch src/buster/compiler/tier1_model_measure.c
build
ran="$(since | sed 's|.*/||' | sort -u | tr '\n' ' ')"
[ "$ran" = "compiler compiler.link tier1_model_measure.c.o " ] || fail "an edited unit ran $ran"
[ build/compiler -ef "$release" ] || fail "the rebuilt executable isn't published"
pass "an edited unit is compiled, linked, baked and published alone"

# Debug beside Release.
release_time="$(stat -c %Y "$release")"
build --config Debug
test -x "$debug" || fail "no Debug executable"
[ build/compiler -ef "$debug" ] || fail "build/compiler isn't the Debug executable"
python3 -I -c 'import json, sys; d = json.load(open(sys.argv[1])); assert d and all("/Debug/" in e["output"] for e in d)' \
    build/compile_commands.json || fail "build/compile_commands.json isn't the Debug commands alone"
[ "$(stat -c %Y "$release")" = "$release_time" ] || fail "the Debug build changed the Release executable"
mark
build
[ -z "$(since)" ] || fail "switching back to Release ran $(since | tr '\n' ' ')"
[ build/compiler -ef "$release" ] || fail "switching back didn't publish the Release executable"
build --config Debug
[ -z "$(since)" ] || fail "switching back to Debug ran $(since | tr '\n' ' ')"
[ build/compiler -ef "$debug" ] || fail "switching back didn't publish the Debug executable"
build
pass "Debug and Release share the tree and switch without building"

# Settings that change one step: the bake, both ways, and the linker.
for bake in 0 1; do
    mark
    BUSTER_BAKE="$bake" build
    ran="$(since | sed 's|.*/||' | sort -u | tr '\n' ' ')"
    [ "$ran" = "compiler " ] || fail "BUSTER_BAKE=$bake ran $ran"
done
pass "BUSTER_BAKE reruns the post-link step alone, both ways"
if command -v ld.lld > /dev/null; then
    mark
    BUSTER_LINKER=lld build
    ran="$(since | sed 's|.*/||' | sort -u | tr '\n' ' ')"
    [ "$ran" = "compiler compiler.link " ] || fail "BUSTER_LINKER=lld ran $ran"
    build
    pass "BUSTER_LINKER relinks alone"
fi

# Failures publish nothing: a post-link step that fails (an nm that does), a compile that fails, an interrupt.
inode="$(stat -c %i build/compiler)"
mkdir "$work/nm"
printf '#!/bin/sh\nexit 3\n' > "$work/nm/nm"
chmod +x "$work/nm/nm"
if PATH="$work/nm:$PATH" ./build.sh > "$work/build.log" 2>&1; then fail "a failed post-link step succeeded"; fi
[ "$(stat -c %i build/compiler)" = "$inode" ] || fail "a failed post-link step published build/compiler"
[ ! -e "$release" ] || fail "a failed post-link step left $release"
build
[ build/compiler -ef "$release" ] || fail "the build after a failed post-link step didn't publish"
inode="$(stat -c %i build/compiler)"
unit=src/buster/compiler/tier1_model_measure.c
cp "$unit" "$work/unit.c"
printf 'int buster_build_regression(void) { return }\n' >> "$unit"
status=0
./build.sh > "$work/build.log" 2>&1 || status=$?
cp "$work/unit.c" "$unit"
[ "$status" -ne 0 ] || fail "a failed compile succeeded"
[ "$(stat -c %i build/compiler)" = "$inode" ] || fail "a failed compile published build/compiler"
touch src/buster/compiler/main_unit_types.c
setsid bash -c 'echo "$$" > "$1/group"; exec ./build.sh > "$1/interrupted.log" 2>&1' _ "$work" &
interrupted=$!
# main_unit_types.c takes 20 s on the 9700X and longer elsewhere: the interrupt comes while it compiles.
sleep 3
kill -INT -- "-$(cat "$work/group")"
status=0
wait "$interrupted" || status=$?
[ "$status" = 130 ] || fail "an interrupted build exited with $status, not 130"
[ "$(stat -c %i build/compiler)" = "$inode" ] || fail "an interrupted build published build/compiler"
build
[ build/compiler -ef "$release" ] || fail "the build after an interrupt didn't publish"
pass "a failed post-link step, a failed compile and an interrupt publish nothing"

# The presets, on the same tree, when it is the native one.
if [ "$tree" = "$PWD/build/cmake/native" ]; then
    cmake --preset native > "$work/preset.log"
    mark
    cmake --build --preset release > "$work/preset.log"
    [ -z "$(since)" ] || fail "the release preset rebuilt $(since | tr '\n' ' ')"
    cmake --build --preset debug > "$work/preset.log"
    ctest --preset release-build
    ctest --preset debug-build
    pass "the presets configure, build and test the build.sh tree"
fi

# The print commands' NUL-delimited arguments.
for command in print-sources print-compile-flags; do
    ./build.sh "$command" > "$work/printed"
    [ -s "$work/printed" ] && [ "$(tail -c 1 "$work/printed" | od -An -tx1 | tr -d ' ')" = 00 ] || fail "$command didn't print NUL-terminated arguments"
done
mapfile -d '' -t sources < <(./build.sh print-sources)
[ "${sources[0]}" = src/buster/compiler/main.c ] || fail "print-sources doesn't start with main.c"
pass "the print commands print NUL-delimited arguments"

# The shifted links, and their padding.
./build.sh shifts "$work/shifts" lane_benchmark.c 0 64 > "$work/build.log" 2>&1 || { cat "$work/build.log" >&2; fail "shifts failed"; }
# nm's whole output first: grep -q would leave it writing to a closed pipe, which pipefail counts as a failure.
for bytes in 0 64; do
    nm "$work/shifts/compiler-$bytes" > "$work/symbols"
    grep -q ' buster_shift_padding$' "$work/symbols" || fail "compiler-$bytes has no padding"
done
! cmp -s "$work/shifts/compiler-64" build/compiler || fail "compiler-64 is build/compiler"
"$work/shifts/compiler-64" test-baked-tables > /dev/null || fail "compiler-64 doesn't run"
build
pass "shifts link padded executables"

# A lexer variant, in a tree of its own, and the published executable back.
BUSTER_DEBUG_INFO=0 ./build.sh lexer-variant regression=src/buster/compiler/c_lexer.c > "$work/build.log" 2>&1 || { cat "$work/build.log" >&2; fail "lexer-variant failed"; }
nm build/compiler > "$work/symbols"
grep -q ' T c_lexer_variant_regression$' "$work/symbols" || fail "the lexer variant isn't in build/compiler"
build
[ build/compiler -ef "$release" ] || fail "the build after lexer-variant didn't publish"
pass "lexer-variant builds a tree of its own"
