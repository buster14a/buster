#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#
# The complete short work-contract set (#2041), built as standalone correctness programs. Use each hosted row's
# real host and sanitizer flags. Deliberate regressions are temporary source copies, never compiler build options.
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
directory=$(mktemp -d "${TMPDIR:-/tmp}/buster-work-contracts.XXXXXX")
trap 'rm -rf -- "$directory"' EXIT
compiler=${CC:-clang}
./build.sh print-host-flags > "$directory/host.flags"
./build.sh print-sanitizer-flags --config Release > "$directory/sanitizer.flags"
mapfile -d '' -t host_flags < "$directory/host.flags"
mapfile -d '' -t sanitizer_flags < "$directory/sanitizer.flags"
test "${#host_flags[@]}" -gt 0
flags=(-std=c17 -O2 -Isrc -Isrc/buster/compiler -Wall -Wextra -Werror
       -Wno-unused-function -Wno-unused-parameter -Wno-psabi
       -fwrapv -funsigned-char -fno-strict-aliasing -pthread
       "${host_flags[@]}" "${sanitizer_flags[@]}")

fail()
{
    printf 'work-contracts: %s\n' "$*" >&2
    exit 1
}

build()
{
    local name=$1
    shift
    "$compiler" "${flags[@]}" "tools/ci/work-contracts-$name.c" "$@" -o "$directory/$name"
}

# Require the declared records, not merely a successful process. The self-test owns numeric comparisons and report
# I/O; these exact field checks ensure no fixture/size disappears and a work mutant fails with correct semantics.
record()
{
    local log=$1 fixture=$2 size=$3 metric=$4 semantic=$5 result=$6
    local pattern="^work-contract fixture=$fixture size=$size metric=$metric actual=[0-9]+ bound=[0-9]+ semantic=$semantic result=$result$"
    local count
    count=$(grep -Ec "$pattern" "$log" || true)
    if test "$count" != 1; then
        cat "$log"
        fail "missing or repeated $fixture/$size/$metric ($semantic/$result)"
    fi
}

dependency_records()
{
    local log=$1 semantic=$2 comparisons=$3
    test "$(grep -c '^work-contract ' "$log")" = 15 || fail "dependency contract inventory changed"
    for size in 256 512 1024; do
        record "$log" dependency-distinct "$size" filesystem-probes "$semantic" "$semantic"
        record "$log" dependency-distinct "$size" identity-comparisons "$semantic" "$comparisons"
        record "$log" dependency-distinct "$size" arena-bytes "$semantic" "$semantic"
    done
    for size in 512 1024; do
        record "$log" dependency-doubling "$size" filesystem-probes "$semantic" "$semantic"
        record "$log" dependency-doubling "$size" identity-comparisons "$semantic" "$comparisons"
        record "$log" dependency-doubling "$size" arena-bytes "$semantic" "$semantic"
    done
}

ssa_records()
{
    local log=$1 construction=$2
    test "$(grep -c '^work-contract ' "$log")" = 6 || fail "SSA contract inventory changed"
    for size in 256 512 1024; do
        record "$log" ssa-scoped-loops "$size" construction-requested-bytes pass "$construction"
        record "$log" ssa-scoped-loops "$size" verification-requested-bytes pass pass
    done
}

run_contract()
{
    local program=$1 log=$2 expected=$3 status=0
    "$program" > "$log" 2> "$log.stderr" || status=$?
    # A sanitizer can fail at process teardown after all records, with the same exit 1 as an intended work failure.
    # Expected dependency diagnostics are captured by their fixture; any stderr here is an independent failure.
    if test "$status" != "$expected" || test -s "$log.stderr"; then
        cat "$log"
        cat "$log.stderr" >&2
        fail "contract process must exit $expected with no unexpected diagnostics, observed $status"
    fi
}

build report
"$directory/report"
build dependency
run_contract "$directory/dependency" "$directory/dependency.log" 0
dependency_records "$directory/dependency.log" pass pass
cat "$directory/dependency.log"
build ssa
run_contract "$directory/ssa" "$directory/ssa.log" 0
ssa_records "$directory/ssa.log" pass
cat "$directory/ssa.log"

# Repeated executions must have identical deterministic records. There is no elapsed-time or RSS acceptance value.
run_contract "$directory/dependency" "$directory/dependency-repeat.log" 0
run_contract "$directory/ssa" "$directory/ssa-repeat.log" 0
cmp "$directory/dependency.log" "$directory/dependency-repeat.log"
cmp "$directory/ssa.log" "$directory/ssa-repeat.log"

# Every production compiler/benchmark unit receives BUSTER_EXECUTABLE_UNIT from BusterSettings.cmake. Each actual
# test wrapper must refuse that mode, including the znver5 benchmark target. BUSTER_UNIT also blocks accidental
# textual inclusion in main.c. These checks preprocess only; they do not run native code on a different host.
BUSTER_HOST_ARCH=znver5 BUSTER_SANITIZER= ./build.sh print-compile-flags --config Release > "$directory/production.flags"
mapfile -d '' -t production_flags < "$directory/production.flags"
production_marker=false
for flag in "${production_flags[@]}"; do
    if test "$flag" = -DBUSTER_EXECUTABLE_UNIT; then
        production_marker=true
    fi
done
test "$production_marker" = true || fail "the real znver5 Release flags lost the production-unit marker"
./build.sh print-sources > "$directory/production.sources"
mapfile -d '' -t production_sources < "$directory/production.sources"
test "${#production_sources[@]}" -gt 0
for source in "${production_sources[@]}"; do
    case "$source" in
        *work-contract*|*work_contract*) fail "standalone instrumentation entered the compiler source inventory: $source" ;;
    esac
done
for name in report dependency ssa; do
    for marker in BUSTER_EXECUTABLE_UNIT BUSTER_UNIT; do
        if "$compiler" "${production_flags[@]}" "-D$marker" -E "tools/ci/work-contracts-$name.c" \
            -o /dev/null 2> "$directory/production-guard.log"; then
            fail "$name instrumentation was accepted in a production compiler unit ($marker)"
        fi
        grep -Eq 'work contracts.*compiler executable|work contracts.*standalone|work contracts belong to standalone' \
            "$directory/production-guard.log" || fail "production refusal did not reach the instrumentation guard"
    done
done

build mutate
"$directory/mutate" dependency-linear src/buster/compiler/c_dependency_driver.c "$directory/dependency-linear.c"
"$compiler" "${flags[@]}" "-DBUSTER_WORK_DEPENDENCY_SOURCE=\"$directory/dependency-linear.c\"" \
    tools/ci/work-contracts-dependency.c -o "$directory/dependency-linear"
run_contract "$directory/dependency-linear" "$directory/dependency-linear.log" 1
dependency_records "$directory/dependency-linear.log" pass fail
printf 'work-contracts: restored linear searches fail the work bound with correct semantics\n'
grep 'fixture=dependency-distinct .*metric=identity-comparisons ' "$directory/dependency-linear.log"

"$directory/mutate" dependency-omit src/buster/compiler/c_dependency_driver.c "$directory/dependency-omit.c"
"$compiler" "${flags[@]}" "-DBUSTER_WORK_DEPENDENCY_SOURCE=\"$directory/dependency-omit.c\"" \
    tools/ci/work-contracts-dependency.c -o "$directory/dependency-omit"
run_contract "$directory/dependency-omit" "$directory/dependency-omit.log" 1
dependency_records "$directory/dependency-omit.log" fail fail
printf 'work-contracts: omitted overlap checks fail semantic verification\n'
grep 'fixture=dependency-distinct .*metric=identity-comparisons ' "$directory/dependency-omit.log"

"$directory/mutate" ssa-dense src/buster/compiler/ssa.c "$directory/ssa-dense.c"
"$compiler" "${flags[@]}" "-DBUSTER_WORK_SSA_SOURCE=\"$directory/ssa-dense.c\"" \
    tools/ci/work-contracts-ssa.c -o "$directory/ssa-dense"
run_contract "$directory/ssa-dense" "$directory/ssa-dense.log" 1
ssa_records "$directory/ssa-dense.log" fail
printf 'work-contracts: restored dense SSA allocation fails the byte bound with correct semantics\n'
grep 'metric=construction-requested-bytes ' "$directory/ssa-dense.log"

# A stale mutation must never silently compile the unmodified source and masquerade as a negative control.
if "$directory/mutate" dependency-linear "$directory/dependency-linear.c" "$directory/invalid.c" \
    > "$directory/mutation.log" 2>&1; then
    fail "a mutation with no matching source fragment was accepted"
fi
test ! -e "$directory/invalid.c" || fail "failed mutation published an output"
printf 'work-contracts: complete set, deterministic repeats, production isolation and real mutations passed\n'
