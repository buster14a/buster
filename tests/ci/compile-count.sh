#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# The compile's count (#2850): tools/ci/compile-count.c's self-test and its verdicts on files, and
# tools/ci/compile-count.sh's check on fake checkouts, with a fake clang whose "executables" write the raw profiles
# their sources' work adds up to. No network and no compiler of ours: the real build of a change and its base runs in
# the hosted row (hosted-correctness.yml's "Count the compile against its base's").
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
checks=0
fail() { printf 'compile-count fixture: %s\n' "$*" >&2; exit 1; }
pass() { checks=$((checks + 1)); }

tool="$work/compile-count"
cc -std=c17 -O2 -Wall -Wextra -Werror -o "$tool" "$root/tools/ci/compile-count.c"
"$tool" --self-test > "$work/self-test.log" || { cat "$work/self-test.log" >&2; fail "the self-test failed"; }
pass

# The repository's own statements are ones the check can read.
for file in "$root"/tools/ci/compile-count/*.txt; do
    if [ -e "$file" ]; then
        "$tool" statement "$file" > "$work/statement.log" || fail "${file#"$root"/} has no well-formed statement"
        pass
    fi
done

# --- The verdicts, on files ---
# expect STATUS TEXT ARGUMENTS...: compile-count's exit status and a text its output holds.
expect()
{
    local expected_status=$1 expected_text=$2 status=0
    shift 2
    "$tool" "$@" > "$work/log" 2>&1 || status=$?
    if [ "$status" != "$expected_status" ] || ! grep -Fq -- "$expected_text" "$work/log"; then
        cat "$work/log" >&2
        fail "expected exit $expected_status and '$expected_text', got exit $status for: $*"
    fi
    pass
}
profile() { "$tool" --fixture "$work/$1.profraw" "$1" || fail "no fixture profile of $1"; }
for count in 1000000 1000001 1000400 1005000 1005001 994999 1070000 1040000 970000; do
    profile "$count"
done
printf '# A comment.\n+0.00 the check itself\n' > "$work/none.txt"
printf '+7.00 a feature\n' > "$work/rise.txt"
printf -- '-3.00 a cut\n' > "$work/cut.txt"
printf '+1000 the most\n' > "$work/most.txt"
printf '+0.001 a thousandth\n' > "$work/thousandth.txt"
printf 'the count rose by seven percent\n' > "$work/prose.txt"
printf '# Only a comment.\n' > "$work/empty.txt"
verdict() { expect "$1" "$2" check "${@:3}"; }
p() { printf '%s\n' "$work/$1.profraw"; }

expect 0 "$(printf '1000000\t')" sum "$(p 1000000)"
verdict 0 'passed: the change states nothing and moves the count by +0.000%' --base "$(p 1000000)" --head "$(p 1000000)"
verdict 0 'moves the count by +0.500%, within half a percent' --base "$(p 1000000)" --head "$(p 1005000)"
verdict 1 'FAILED: the change moves the count by +0.500% and doesn'"'"'t state it' --base "$(p 1000000)" --head "$(p 1005001)"
verdict 1 '"+0.50 what moved it"' --base "$(p 1000000)" --head "$(p 1005001)"
verdict 1 'tools/ci/compile-count/NAME.txt' --base "$(p 1000000)" --head "$(p 1005001)"
verdict 1 'FAILED: the change moves the count by -0.500% and doesn'"'"'t state it' --base "$(p 1000000)" --head "$(p 994999)"
verdict 1 'FAILED: the change moves the count by +7.000% and doesn'"'"'t state it' --base "$(p 1000000)" --head "$(p 1070000)"
verdict 0 'passed: the change states +7.000% and moves the count by +7.000%' --statement "$work/rise.txt" --base "$(p 1000000)" --head "$(p 1070000)"
verdict 0 'the change'"'"'s statement rise.txt: +7.000% ("+7.00 a feature")' --statement "$work/rise.txt" --base "$(p 1000000)" --head "$(p 1070000)"
verdict 0 'passed: the change states -3.000%' --statement "$work/cut.txt" --base "$(p 1000000)" --head "$(p 970000)"
verdict 1 'FAILED: the change states +7.000% and moves the count by +0.000%' --statement "$work/rise.txt" --base "$(p 1000000)" --head "$(p 1000000)"
verdict 1 '"-3.00 what moved it"' --statement "$work/rise.txt" --base "$(p 1000000)" --head "$(p 970000)"
# A statement of nothing is as none.
verdict 0 'passed: the change states +0.000%' --statement "$work/none.txt" --base "$(p 1000000)" --head "$(p 1000000)"
verdict 1 'FAILED: the change states +0.000%' --statement "$work/none.txt" --base "$(p 1000000)" --head "$(p 1070000)"
# A change's statements add up, wherever they stand among its sides' profiles.
verdict 0 'passed: the change states +4.000% and moves the count by +4.000%' --statement "$work/rise.txt" --base "$(p 1000000)" --statement "$work/cut.txt" --head "$(p 1040000)"
verdict 0 'the change'"'"'s statement cut.txt: -3.000% ("-3.00 a cut")' --statement "$work/rise.txt" --statement "$work/cut.txt" --base "$(p 1000000)" --head "$(p 1040000)"
verdict 1 'FAILED: the change states +7.000%' --statement "$work/rise.txt" --statement "$work/none.txt" --base "$(p 1000000)" --head "$(p 1040000)"
verdict 2 'past the most one change states (1000%)' --statement "$work/most.txt" --statement "$work/thousandth.txt" --base "$(p 1000000)" --head "$(p 1000000)"
many=()
for statement in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16; do many+=(--statement "$work/none.txt"); done
verdict 0 'passed: the change states +0.000%' "${many[@]}" --base "$(p 1000000)" --head "$(p 1000000)"
verdict 2 'more than 16 of them' "${many[@]}" --statement "$work/none.txt" --base "$(p 1000000)" --head "$(p 1000000)"
verdict 2 'a --statement without its file' --base "$(p 1000000)" --head "$(p 1000000)" --statement
# The median of a side's runs, and runs too far apart.
verdict 0 'the change'"'"'s compile: 1000001 (3 runs, 1000000 to 1000400)' --base "$(p 1000000)" --head "$(p 1000400)" "$(p 1000000)" "$(p 1000001)"
verdict 2 'UNUSABLE: runs of one executable are more than 0.05% apart' --base "$(p 1000000)" --head "$(p 1000000)" "$(p 1005000)"
# What can't be judged.
verdict 2 "can't open" --statement "$work/missing.txt" --base "$(p 1000000)" --head "$(p 1000000)"
verdict 2 'must start with a percentage' --statement "$work/prose.txt" --base "$(p 1000000)" --head "$(p 1000000)"
verdict 2 'has no statement line' --statement "$work/empty.txt" --base "$(p 1000000)" --head "$(p 1000000)"
verdict 2 'not a 64-bit little-endian raw profile' --base "$root/tools/ci/compile-count.c" --head "$(p 1000000)"
verdict 2 'shorter than a raw profile'"'"'s header' --base "$(p 1000000)" --head "$work/none.txt"
verdict 2 'usage: compile-count check' --base "$(p 1000000)"
verdict 2 'unexpected argument' "$(p 1000000)"
many=()
for run in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16; do many+=("$(p 1000000)"); done
verdict 0 '(16 runs, 1000000 to 1000000)' --base "$(p 1000000)" --head "${many[@]}"
verdict 2 'unexpected argument' --base "$(p 1000000)" --head "${many[@]}" "$(p 1000000)"
expect 2 'usage: compile-count' frobnicate
expect 2 'has no statement line' statement "$work/empty.txt"
expect 0 "$(printf -- '-3.000%%\t-3.00 a cut')" statement "$work/cut.txt"

# --- compile-count.sh's check, on fake checkouts ---
# The fake clang. A compile (-c) requires the instrumented -O0 after the checkout's own flags and the checkout as its
# directory, and writes its source's "work N" as the object; a source that says "broken" doesn't compile. A link writes
# an "executable" that adds its objects' work up and writes a raw profile of that count (1,000 for the one-function
# unit) where LLVM_PROFILE_FILE says, as an instrumented compiler does when it exits.
export FIXTURE_TOOL="$tool"
cat > "$work/clang" <<'CLANG'
#!/usr/bin/env bash
set -euo pipefail
case " $* " in
    *' -c '*)
        [ "$1" = -c ] && [ "$3" = -o ] || { echo "fixture clang: unexpected compile: $*" >&2; exit 1; }
        source=$2 object=$4
        shift 4
        [ "$*" = "$(cat flags) -O0 -g0 -fprofile-instr-generate" ] || { echo "fixture clang: unexpected flags: $*" >&2; exit 1; }
        [ -f "$source" ] && [ -x ./build.sh ] || { echo "fixture clang: $source isn't in the checkout the compile runs in" >&2; exit 1; }
        if grep -q broken "$source"; then echo "$source:1:1: error: fixture" >&2; exit 1; fi
        grep -o 'work [0-9]*' "$source" > "$object"
        ;;
    *)
        output= objects=() mode=
        while [ "$#" -gt 0 ]; do
            case "$1" in
                -o) output=$2; shift ;;
                -no-pie|-static|-Wl,--wrap=*) mode="$mode $1" ;;
                -fprofile-instr-generate|-Wl,--push-state,--as-needed|-Wl,--pop-state|-lm|-ldl) ;;
                *.o) objects+=("$1") ;;
                *) echo "fixture clang: unexpected link argument: $1" >&2; exit 1 ;;
            esac
            shift
        done
        printf '%s\n' "$mode" > "$output.mode"
        work=$(cat "${objects[@]}" | awk '{ sum += $2 } END { print sum }')
        cat > "$output" <<EXECUTABLE
#!/usr/bin/env bash
set -euo pipefail
count=$work
output= unit= lanes=
while [ "\$#" -gt 0 ]; do
    case "\$1" in
        -o) output=\$2; shift ;;
        --lanes) lanes=\$2; shift ;;
        --predefines|-isystem) [ -e "\$2" ] || { echo "fixture compiler: no \$2" >&2; exit 1; }; shift ;;
        *.c) unit=\$1 ;;
    esac
    shift
done
[ "\$lanes" = 1 ] || { echo "fixture compiler: not on one lane" >&2; exit 1; }
case "\$unit" in
    */one.c) count=1000 ;;
    */sqlite3.c) if grep -q crash "\$unit"; then echo "fixture compiler: crashed" >&2; exit 1; fi ;;
    *) echo "fixture compiler: no unit" >&2; exit 1 ;;
esac
if [ -n "\${FIXTURE_JITTER:-}" ] && [ "\$unit" != "\${unit%sqlite3.c}" ]; then
    runs=\$(cat "\$FIXTURE_JITTER" 2>/dev/null || echo 0)
    echo \$((runs + 1)) > "\$FIXTURE_JITTER"
    count=\$((count + runs * 1000))
fi
: > "\$output"
if [ -z "\${FIXTURE_NO_PROFILE:-}" ]; then
    "\$FIXTURE_TOOL" --fixture "\$LLVM_PROFILE_FILE" "\$count"
fi
EXECUTABLE
        chmod +x "$output"
        ;;
esac
CLANG
chmod +x "$work/clang"
export BUSTER_COMPILE_COUNT_CLANG="$work/clang" BUSTER_COMPILE_COUNT_JOBS=2

# checkout NAME WORK_A WORK_B [FILE=STATEMENT]...: a checkout of three units, with the real compile-count.c and, for
# each FILE=STATEMENT, the statement file tools/ci/compile-count/FILE. Its flags are its file's, a dynamic executable's
# until a test says otherwise.
checkout()
{
    local tree="$work/$1" statement
    rm -rf -- "$tree"
    mkdir -p "$tree/src" "$tree/tools/ci"
    printf '%s\n' '-Isrc -DFIXTURE_FLAG=1 -O3 -g' > "$tree/flags"
    printf '// work %s\n' "$2" > "$tree/src/a.c"
    printf '// work %s\n' "$3" > "$tree/src/b.c"
    printf '// work 0\n' > "$tree/src/c.c"
    cat > "$tree/build.sh" <<'BUILD'
#!/usr/bin/env bash
case "$*" in
    print-sources) printf '%s\0' src/a.c src/b.c src/c.c ;;
    'print-compile-flags --config Release') tr ' \n' '\0\0' < flags ;;
    *) echo "fixture build.sh: $*" >&2; exit 1 ;;
esac
BUILD
    chmod +x "$tree/build.sh"
    cp "$root/tools/ci/compile-count.c" "$tree/tools/ci/compile-count.c"
    for statement in "${@:4}"; do
        mkdir -p "$tree/tools/ci/compile-count"
        printf '# The statement.\n%s\n' "${statement#*=}" > "$tree/tools/ci/compile-count/${statement%%=*}"
    done
}
corpus="$work/corpus"
mkdir -p "$corpus/sqlite" "$corpus/provenance" "$corpus/headers/usr/lib/clang/23/include" "$corpus/headers/usr/local/include" "$corpus/headers/usr/include"
printf 'int sqlite3;\n' > "$corpus/sqlite/sqlite3.c"
: > "$corpus/provenance/gnu17-pinned-predefines.txt"

# run STATUS TEXT BASE HEAD: compile-count.sh check's exit status and a text its output holds.
run()
{
    local expected_status=$1 expected_text=$2 status=0
    rm -rf -- "$work/count"
    GITHUB_STEP_SUMMARY="$work/summary" bash "$root/tools/ci/compile-count.sh" check "$work/$3" "$work/$4" "$corpus" "$work/count" > "$work/log" 2>&1 || status=$?
    if [ "$status" != "$expected_status" ] || ! grep -Fq -- "$expected_text" "$work/log"; then
        cat "$work/log" >&2
        fail "expected exit $expected_status and '$expected_text', got exit $status for $3 against $4"
    fi
    pass
}

earlier='earlier.txt=-1.37 an earlier change'
checkout base 600000 400000 "$earlier"
checkout same 600000 400000 "$earlier"
checkout rise 610000 400000 "$earlier"
checkout stated 610000 400000 "$earlier" 'feature.txt=+1.00 a feature'
checkout replaced 610000 400000 'feature.txt=+1.00 a feature'
checkout restated 610000 400000 'earlier.txt=+1.00 restated'
checkout noted 600000 400000 "$earlier" 'notes.md=+9.00 no statement'
checkout cut 600000 380000 "$earlier"
checkout after 600000 400000 "$earlier" 'feature.txt=+1.00 a feature'
checkout older 600000 400000
checkout broken 600000 400000 "$earlier"
printf '// work 1\n#error broken\n' > "$work/broken/src/b.c"

: > "$work/summary"
run 0 'passed: the change states nothing and moves the count by +0.000%' base same
grep -Fq 'a one-function unit counts 1000 at the base and 1000 at the change' "$work/log" || fail "the one-function unit's counts aren't reported"
grep -Fq 'compile-count: passed: the change states nothing' "$work/summary" || fail "the verdict isn't in the step's summary"
pass
run 1 '::error title=The compile'"'"'s count::the change moves the count by +1.000% and doesn'"'"'t state it' base rise
grep -Fq 'tools/ci/compile-count/NAME.txt, NAME its branch'"'"'s last part: "+1.00 what moved it"' "$work/log" || fail "a failure doesn't propose its file and line"
pass
# A statement the change adds is its own; the base's, which it holds the same, are earlier changes'.
run 0 'passed: the change states +1.000% and moves the count by +1.000%' base stated
grep -Fq 'the change'"'"'s statement feature.txt: +1.000% ("+1.00 a feature")' "$work/log" || fail "the change's statement isn't named"
! grep -Fq 'statement earlier.txt' "$work/log" || fail "the base's statement is read as the change's"
pass
# The link is the build's own mode: static, with the build's wrapped symbols, where the flags say the units are a static
# executable's, and dynamic otherwise.
[ "$(cat "$work/count/base/compiler.mode")" = ' -no-pie' ] || fail "a dynamic build's executable isn't linked so: $(cat "$work/count/base/compiler.mode")"
printf '%s\n' '-Isrc -DBUSTER_HOST_STATIC=1 -O3' > "$work/stated/flags"
run 0 'passed: the change states +1.000%' base stated
[ "$(cat "$work/count/head/compiler.mode")" = ' -no-pie -static -Wl,--wrap=__register_frame_info,--wrap=__deregister_frame_info' ] || fail "a static build's executable isn't linked so: $(cat "$work/count/head/compiler.mode")"
pass
# A statement the change deletes states nothing, one it changes is its own, and a file that isn't *.txt is none.
run 0 'passed: the change states +1.000%' base replaced
run 0 'the change'"'"'s statement earlier.txt: +1.000% ("+1.00 restated")' base restated
run 0 'passed: the change states nothing' base noted
run 0 'passed: the change states nothing' base older
run 1 '::error title=The compile'"'"'s count::the change moves the count by -2.000% and doesn'"'"'t state it' base cut
# The base's statements don't excuse the change after it: a statement is one change's.
run 1 'the change moves the count by -0.990% and doesn'"'"'t state it' stated after
# A base without the directory: every statement the change holds is its own.
run 1 'FAILED: the change states -1.370% and moves the count by +0.000%' older base
run 2 "a unit of $work/broken didn't compile" base broken
grep -Fq 'error: fixture' "$work/log" || fail "a unit's diagnostics aren't shown"
pass
run 2 "a unit of $work/broken didn't compile" broken base

# What can't be judged is no verdict: the compiler's failure, runs too far apart, a checkout that prints nothing.
printf 'int sqlite3; // crash\n' > "$corpus/sqlite/sqlite3.c"
run 2 "didn't compile sqlite3.c" base same
printf 'int sqlite3;\n' > "$corpus/sqlite/sqlite3.c"
FIXTURE_JITTER="$work/jitter" run 2 '::error title=The compile'"'"'s count::It could not be judged (compile-count exited 2)' base same
grep -Fq 'UNUSABLE: runs of one executable are more than 0.05% apart' "$work/log" || fail "runs too far apart aren't named"
pass
# An executable that writes no profile is no count, though an earlier run's profiles are there; and a unit that stops
# compiling is no build, though an earlier build's objects are.
status=0
FIXTURE_NO_PROFILE=1 bash "$root/tools/ci/compile-count.sh" count "$work/count/base/compiler" "$corpus" "$work/count" base > "$work/log" 2>&1 || status=$?
[ "$status" = 2 ] && grep -Fq 'wrote no raw profile' "$work/log" || { cat "$work/log" >&2; fail "an executable that writes no profile isn't refused"; }
pass
bash "$root/tools/ci/compile-count.sh" build "$work/base" "$work/again" > "$work/log" 2>&1 || { cat "$work/log" >&2; fail "a build alone failed"; }
status=0
bash "$root/tools/ci/compile-count.sh" build "$work/broken" "$work/again" > "$work/log" 2>&1 || status=$?
[ "$status" = 2 ] && grep -Fq "didn't compile" "$work/log" || { cat "$work/log" >&2; fail "a unit that stops compiling builds from an earlier build's object"; }
pass
printf '#!/usr/bin/env bash\nexit 1\n' > "$work/same/build.sh"
run 2 "print-sources failed" base same
rm -f "$work/older/build.sh"
run 2 'is no checkout' older base
status=0
bash "$root/tools/ci/compile-count.sh" check "$work/base" > "$work/log" 2>&1 || status=$?
[ "$status" = 2 ] && grep -Fq 'usage: compile-count.sh check BASE HEAD CORPUS WORK' "$work/log" || fail "a check without its arguments isn't refused"
pass
# A count of jobs that is none, and a directory whose name the profile runtime would expand.
for jobs in 0 x 2x ''; do
    status=0
    BUSTER_COMPILE_COUNT_JOBS="$jobs" timeout 20 bash "$root/tools/ci/compile-count.sh" build "$work/base" "$work/jobs" > "$work/log" 2>&1 || status=$?
    if [ -n "$jobs" ]; then
        [ "$status" = 2 ] && grep -Fq 'BUSTER_COMPILE_COUNT_JOBS must be a positive number' "$work/log" || { cat "$work/log" >&2; fail "BUSTER_COMPILE_COUNT_JOBS='$jobs' isn't refused"; }
    else
        # (An empty setting is the default's, the CPUs'.)
        [ "$status" = 0 ] || { cat "$work/log" >&2; fail "an empty BUSTER_COMPILE_COUNT_JOBS isn't the default"; }
    fi
    pass
done
status=0
bash "$root/tools/ci/compile-count.sh" count "$work/again/compiler" "$corpus" "$work/50%h" base > "$work/log" 2>&1 || status=$?
[ "$status" = 2 ] && grep -Fq "holds a '%'" "$work/log" || { cat "$work/log" >&2; fail "a directory with a % isn't refused"; }
pass

printf 'compile-count fixtures passed: %s checks\n' "$checks"
