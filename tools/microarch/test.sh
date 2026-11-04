#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#
# The driver's checks (docs/commands/microarch.md), on fake compilers, a fake perf
# and a fake tcc, without a lock or a measurement:
#   tools/microarch/test.sh
# - a failed step fails run.sh and stops it (KEEP_GOING=1 runs the rest);
# - validate passes identical outputs, fails a variant whose object or -E output
#   differs, fails a variant whose command fails without hashing its stale output,
#   and fails differing diagnostics unless VALIDATE_DIAGNOSTICS=warn;
# - a profile whose perf record fails (leaving a partial perf.data) or whose
#   reducer fails leaves no .done marker and no table, and a rerun records it;
# - a sweep or metrics chunk with a recorded failure fails every rerun, and
#   RETRY_FAILED=1 runs the failed entries again;
# - a finished uncore measurement isn't measured again, and an interrupted one
#   runs its fanout batches again inside the new measurement, one row per batch;
# - build-commit refuses a name that holds another commit, and setup refuses a
#   build of another commit or another dirty state than the checkout's;
# - setup refuses a self-main.i made from another checkout or other predefines,
#   and makes it again from the checkout once removed;
# - env keeps a catalog that numbers results when perf's list changes (a shifted
#   list of the same length included) unless no result uses it, and sweep refuses
#   results numbered by another catalog;
# - summarize passes a campaign without some steps and fails when a reducer fails;
# - env fails on a perf list that fails or lists nothing and writes no catalog
#   from it, and sweep and metrics fail without a catalog;
# - a step whose CPU can't be pinned fails;
# - results/HOST is bound to its inputs: after setup rebuilds the checkout, a
#   step that measures is refused until the results are moved away, a
#   build-commit build is added, and results without identity.txt are refused.
# uarch.c's own checks are `uarch test`.
set -u
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/microarch-test.XXXXXX") || exit 2
trap 'rm -rf -- "${work:?}"' EXIT
checks=0
failures=0

expect()
{
    checks=$((checks + 1))
    if ! eval "$1"; then
        echo "FAIL: $2" >&2
        failures=$((failures + 1))
    fi
}

mkdir -p "$work/fake" "$work/root/bin" "$work/behaviour"
export FAKE_BEHAVIOUR=$work/behaviour

# A compiler that writes its -o output; behaviour/VARIANT says "fail", "differ"
# (another object) or "warn" (a diagnostic).
cat > "$work/fake/compiler" <<'EOF'
#!/usr/bin/env bash
out=""
mode=c
while [[ $# -gt 0 ]]; do
    case $1 in
        -o) out=$2; shift ;;
        -E) mode=E ;;
        -fsyntax-only) mode=syntax ;;
    esac
    shift
done
variant=$(basename "$0")
variant=${variant#compiler-}
echo "$variant" >> "$FAKE_BEHAVIOUR/../calls"
behaviour=$(cat "$FAKE_BEHAVIOUR/$variant" 2>/dev/null)
[[ $behaviour == fail ]] && exit 1
case $mode in
    c) printf 'object %s\n' "$([[ $behaviour == differ ]] && echo "$variant")" > "$out" ;;
    E) printf 'preprocessed\n' > "$out" ;;
    syntax) [[ $behaviour == warn ]] && echo "sqlite3.c:1:1: warning: fake" >&2 ;;
esac
exit 0
EOF
chmod +x "$work/fake/compiler"
ln -s ../../fake/compiler "$work/root/bin/compiler-a"
ln -s ../../fake/compiler "$work/root/bin/compiler-b"

cat > "$work/fake/tcc" <<'EOF'
#!/usr/bin/env bash
while [[ $# -gt 0 ]]; do
    if [[ $1 == -o ]]; then printf 'tcc object\n' > "$2"; fi
    shift
done
EOF
chmod +x "$work/fake/tcc"

# perf: `record` writes its -o file, then fails when behaviour/perf says "fail" (a
# partial perf.data); `script` prints one sample of each kind the reducers read, or
# fails when behaviour/perf says "script"; `report` prints one row. `stat` runs its
# command and writes one counted line per event or metric; it exits 1, counted,
# when its events name behaviour/stat-fail's word (a failed run, not a refusal).
# `list` lists behaviour/events and behaviour/metrics, then fails when
# behaviour/perf says "list".
cat > "$work/fake/perf" <<'EOF'
#!/usr/bin/env bash
command=$1
shift
behaviour=$(cat "$FAKE_BEHAVIOUR/perf" 2>/dev/null)
case $command in
    record)
        while [[ $# -gt 0 ]]; do
            if [[ $1 == -o ]]; then printf 'data\n' > "$2"; fi
            shift
        done
        if [[ $behaviour == fail ]]; then exit 1; fi ;;
    script)
        if [[ $behaviour == script ]]; then exit 1; fi
        fields=""
        while [[ $# -gt 0 ]]; do [[ $1 == -F ]] && fields=$2; shift; done
        case $fields in
            brstacksym) echo "f+0x10/f+0x20/M/-/-/0//X f+0x30/f+0x40/P/-/-/0//X" ;;
            ip,sym,data_src,weight) echo "1a29081042 |OP LOAD|LVL L1 hit|SNP N/A|TLB L1 hit|LCK N/A|BLK  N/A  0  d822e8 f" ;;
            *) echo "  100003 cycles:u:  d822e8 f" ;;
        esac ;;
    report) echo "    100.00%  compiler  [.] f" ;;
    list)
        if [[ ${1:-} == --raw-dump ]]; then
            cat "$FAKE_BEHAVIOUR/events" 2>/dev/null | tr '\n' ' '
            echo
        elif [[ ${1:-} == -j && ${2:-} == metrics ]]; then
            cat "$FAKE_BEHAVIOUR/metrics" 2>/dev/null | sed 's/.*/    "MetricName": "&",/'
        fi
        if [[ $behaviour == list ]]; then exit 1; fi ;;
    stat)
        out=""
        spec=""
        while [[ $# -gt 0 ]]; do
            case $1 in
                -o) out=$2; shift ;;
                -e | -M) spec+="${spec:+,}$2"; shift ;;
                --) shift; break ;;
            esac
            shift
        done
        "$@" > /dev/null 2>&1
        status=$?
        : > "$out"
        events=${spec//[\{\}]/}
        for event in ${events//,/ }; do echo "100,,$event,1000,100.00,," >> "$out"; done
        failing=$(cat "$FAKE_BEHAVIOUR/stat-fail" 2>/dev/null)
        if [[ -n $failing && $spec == *"$failing"* ]]; then exit 1; fi
        exit $status ;;
esac
exit 0
EOF
chmod +x "$work/fake/perf"
export PATH="$work/fake:$PATH"

cat > "$work/host.sh" <<'EOF'
HOST=test
NATIVE=a
VARIANTS="a b"
SWEEP_VARIANTS=a
SELF_VARIANTS=a
TD_WIDTH=6
TD_RESYNC=resyncs_or_nc_redirects
SCALING_LANES=1
FANOUT_WORKERS=1
SWEEP_CHUNK=100
METRIC_CHUNK=60
PAUSE=0
UNCORE=${UNCORE:-0}
hold() { "$@"; }
hold_multi() { "$@"; }
hold_build() { "$@"; }
receipt() { echo "test hold"; }
EOF

export ROOT=$work/root
results=$ROOT/results/test
run() { "$here/run.sh" "$work/host.sh" "$@" > "$work/run.out" 2>&1; status=$?; }
reset() { rm -rf -- "${results:?}" "${ROOT:?}/scratch"; rm -f -- "$work"/behaviour/*; }

reset
run validate
expect '[[ $status -eq 0 ]]' "validate: identical outputs pass"
expect '[[ $(wc -l < "$results/validate.tsv") -eq 9 ]]' "validate: four hashes per variant and tcc's"

reset
echo differ > "$work/behaviour/b"
run validate time
expect '[[ $status -ne 0 ]]' "validate: a variant's different object fails the run"
expect 'grep -q "b'"'"'s object differs from a'"'"'s" "$results/run.log"' "validate: the difference is logged"
expect '! grep -q "inner validate  done" "$results/run.log"' "validate: a failed step is not logged as done"
expect '[[ ! -d $results/time ]]' "driver: the step after a failed one does not run"

reset
echo differ > "$work/behaviour/b"
KEEP_GOING=1 run validate time
expect '[[ $status -ne 0 && -f $results/time/time.tsv ]]' "driver: KEEP_GOING runs the rest and still fails"

reset
mkdir -p "$ROOT/scratch/test"
printf 'stale object\n' > "$ROOT/scratch/test/b-sq.o"
echo fail > "$work/behaviour/b"
run validate
expect '[[ $status -ne 0 ]]' "validate: a failed command fails the run"
expect '[[ ! -f $ROOT/scratch/test/b-sq.o ]]' "validate: a failed command's stale output is gone, not hashed"
expect '! grep -q "^b" "$results/validate.tsv"' "validate: nothing of the failed variant is recorded"

reset
echo warn > "$work/behaviour/b"
run validate
expect '[[ $status -ne 0 ]]' "validate: differing diagnostics fail by default"
reset
echo warn > "$work/behaviour/b"
VALIDATE_DIAGNOSTICS=warn run validate
expect '[[ $status -eq 0 ]]' "validate: VALIDATE_DIAGNOSTICS=warn lets differing diagnostics pass"

reset
echo fail > "$work/behaviour/perf"
run profiles
expect '[[ $status -ne 0 ]]' "profiles: a failed perf record fails the run"
expect '[[ -z $(ls "$results"/profiles/a/*.done 2>/dev/null) ]]' "profiles: no profile is marked done after a failed record"
rm -f -- "$work/behaviour/perf"
echo script > "$work/behaviour/perf"
run profiles
expect '[[ $status -ne 0 ]]' "profiles: a failed reducer fails the run"
expect '[[ ! -f $results/profiles/a/td1.done && ! -f $results/profiles/a/td1.tsv ]]' "profiles: a failed reducer leaves no table and no .done"
expect '[[ -f $results/profiles/a/cycles.done ]]' "profiles: a profile reduced by perf report alone is done"
rm -f -- "$work/behaviour/perf"
run profiles
expect '[[ $status -eq 0 ]]' "profiles: the rerun records every profile"
expect '[[ $(ls "$results"/profiles/a/*.done | wc -l) -eq 10 ]]' "profiles: ten profiles done"
expect '[[ $(grep -c . "$results/profiles/a/lbr.tsv") -gt 3 ]]' "profiles: the reduced tables have rows"

# Sweep and metrics chunks: a recorded failure isn't skipped as done.
reset
printf 'e1\ne2\ne3\n' > "$work/behaviour/events"
printf 'm1\nm2\n' > "$work/behaviour/metrics"
run env
echo e2 > "$work/behaviour/stat-fail"
SWEEP_WORKLOADS=sq-c run sweep
expect '[[ $status -ne 0 ]]' "sweep: a failed event fails the step"
SWEEP_WORKLOADS=sq-c run sweep
expect '[[ $status -ne 0 ]]' "sweep: the rerun fails again on the recorded failure, not skipped as done"
expect 'grep -q "runs failed before" "$results/run.log"' "sweep: the recorded failure is logged"
rm -f -- "$work/behaviour/stat-fail"
SWEEP_WORKLOADS=sq-c run sweep
expect '[[ $status -ne 0 ]]' "sweep: without RETRY_FAILED the failed entry isn't retried"
RETRY_FAILED=1 SWEEP_WORKLOADS=sq-c run sweep
expect '[[ $status -eq 0 ]]' "sweep: RETRY_FAILED=1 runs the failed entry again"
SWEEP_WORKLOADS=sq-c run sweep
expect '[[ $status -eq 0 ]]' "sweep: a complete, successful chunk is skipped"
echo m2 > "$work/behaviour/stat-fail"
METRIC_WORKLOADS=sq-c run metrics
METRIC_WORKLOADS=sq-c run metrics
expect '[[ $status -ne 0 ]]' "metrics: the rerun fails again on the recorded failure"

# The catalogs that number the sweep's and the metrics' results.
reset
catalog() { paste -sd ' ' "$results/env/$1.txt"; }
printf 'e1\ne2\ne3\n' > "$work/behaviour/events"
printf 'm1\nm2\n' > "$work/behaviour/metrics"
run env
expect '[[ $status -eq 0 && $(catalog core-events) == "e1 e2 e3" && $(catalog metrics) == "m1 m2" ]]' "env: writes perf's lists as the catalogs"
printf 'm0\nm1\nm2\n' > "$work/behaviour/metrics"
run env
expect '[[ $status -eq 0 && $(catalog metrics) == "m0 m1 m2" ]]' "env: a catalog that numbers no result follows perf's list"
SWEEP_WORKLOADS=sq-c run sweep
expect '[[ $status -eq 0 ]]' "sweep: measures the catalog"
# A shifted list of the same length: every entry's number changes.
printf 'e0\ne1\ne2\n' > "$work/behaviour/events"
run env
expect '[[ $status -ne 0 && $(catalog core-events) == "e1 e2 e3" ]]' "env: a changed list doesn't replace a catalog that numbers results"
expect 'grep -q "core-events.txt numbers recorded results" "$results/run.log"' "env: the changed catalog is logged"
KEEP_CATALOG=1 run env
expect '[[ $status -eq 0 && $(catalog core-events) == "e1 e2 e3" ]]' "env: KEEP_CATALOG=1 keeps the frozen catalog"
SWEEP_WORKLOADS=sq-c run sweep
expect '[[ $status -eq 0 ]]' "sweep: resumes on the frozen catalog"
cp "$results/env/core-events.live.txt" "$results/env/core-events.txt"
SWEEP_WORKLOADS=sq-c run sweep
expect '[[ $status -ne 0 ]] && grep -q "recorded entries aren'"'"'t core-events.txt'"'"'s lines" "$results/run.log"' "sweep: results numbered by another catalog are refused"

# A perf list that fails, or lists nothing, is no catalog.
reset
printf 'e1\n' > "$work/behaviour/events"
printf 'm1\n' > "$work/behaviour/metrics"
echo list > "$work/behaviour/perf"
run env
expect '[[ $status -ne 0 && ! -f $results/env/core-events.txt && ! -f $results/env/metrics.txt ]]' "env: a failed perf list writes no catalog"
KEEP_GOING=1 SWEEP_WORKLOADS=sq-c METRIC_WORKLOADS=sq-c run sweep metrics
expect '[[ $status -ne 0 && $(grep -c "is missing or empty" "$results/run.log") -eq 2 ]]' "sweep, metrics: no catalog fails the step instead of measuring nothing"
rm -f -- "$work/behaviour/perf"
: > "$work/behaviour/events"
run env
expect '[[ $status -ne 0 && ! -f $results/env/core-events.txt && $(catalog metrics) == m1 ]]' "env: an empty list is no catalog"
printf 'e1\n' > "$work/behaviour/events"
run env
expect '[[ $status -eq 0 && $(catalog core-events) == e1 ]]' "env: a list with entries is the catalog"
echo list > "$work/behaviour/perf"
run env
expect '[[ $status -ne 0 && $(catalog core-events) == e1 ]]' "env: a failed perf list leaves the frozen catalog"

# A run that can't be pinned to its CPU is no sample.
reset
SINGLE_CPU=99999 run time
expect '[[ $status -ne 0 && ! -f $results/time/time.tsv ]] && grep -q "can'"'"'t pin to CPU" "$results/run.log"' "time: a step that can't pin its CPU fails"

# summarize reduces what a partial campaign has, and fails when a reducer fails.
reset
run time
run summarize
expect '[[ $status -eq 0 && -f $results/time/time-summary.tsv && -f $results/stages-a.tsv ]]' "summarize: a campaign without the self steps passes"
rm -f -- "$results/time/time-summary.tsv"
mkdir "$results/time/time-summary.tsv"
run summarize
expect '[[ $status -ne 0 ]]' "summarize: a failed reducer fails the step"

# uncore: one measurement with its batches.
reset
UNCORE=1 run uncore
expect '[[ $status -eq 0 && -f $results/uncore/fanout8.csv.done ]]' "uncore: the measurement is done"
expect '[[ $(grep -c . "$results/uncore/fanout.tsv") -eq 3 ]]' "uncore: two batches"
calls=$(wc -l < "$work/calls")
csv=$(sha256sum < "$results/uncore/fanout8.csv")
UNCORE=1 run uncore
expect '[[ $status -eq 0 && $(wc -l < "$work/calls") -eq $calls && $(sha256sum < "$results/uncore/fanout8.csv") == "$csv" ]]' "uncore: a finished measurement isn't measured again"
rm -f -- "$results/uncore/fanout8.csv.done"
UNCORE=1 run uncore
expect '[[ $status -eq 0 && $(wc -l < "$work/calls") -gt $calls ]]' "uncore: an unfinished measurement runs its batches again"
expect '[[ $(grep -c . "$results/uncore/fanout.tsv") -eq 3 ]]' "uncore: still one row per batch"

# build-commit and setup on a checkout of two commits.
reset
repository=$work/repository
mkdir -p "$repository"
git -C "$repository" init -q
# Its compiler prints VERSION, or writes "preprocessed VERSION" to its -o file.
cat > "$repository/build.sh" <<'EOF'
#!/usr/bin/env bash
cd "$(dirname "$0")" || exit 1
mkdir -p build
{
    echo '#!/usr/bin/env bash'
    echo "version=$(cat VERSION)"
    echo 'out=""; while [[ $# -gt 0 ]]; do [[ $1 == -o ]] && out=$2; shift; done'
    echo 'if [[ -n $out ]]; then echo "preprocessed $version" > "$out"; else echo "$version"; fi'
} > build/compiler
chmod +x build/compiler
EOF
chmod +x "$repository/build.sh"
echo first > "$repository/VERSION"
git -C "$repository" add -A && git -C "$repository" -c user.name=test -c user.email=test@test commit -q -m first
first=$(git -C "$repository" rev-parse HEAD)
echo second > "$repository/VERSION"
git -C "$repository" -c user.name=test -c user.email=test@test commit -q -a -m second
second=$(git -C "$repository" rev-parse HEAD)
REPOSITORY=$repository run build-commit candidate "${first:0:8}" znver4
expect '[[ $status -eq 0 && $("$ROOT/bin/compiler-candidate-znver4") == first && $(cat "$ROOT/bin/compiler-candidate-znver4.commit") == "$first" ]]' "build-commit: builds the named commit, recorded by its full id"
REPOSITORY=$repository run build-commit candidate "$second" znver4
expect '[[ $status -ne 0 && $("$ROOT/bin/compiler-candidate-znver4") == first ]]' "build-commit: a name that holds another commit is refused"
REPOSITORY=$repository run build-commit candidate "$first" znver4
expect '[[ $status -eq 0 ]]' "build-commit: the same commit under the same name is reused"
# Without its tree, the name's build still holds the first commit.
rm -rf -- "${ROOT:?}/trees/candidate"
REPOSITORY=$repository run build-commit candidate "$second" znver4
expect '[[ $status -ne 0 && $("$ROOT/bin/compiler-candidate-znver4") == first ]]' "build-commit: a build of another commit under the name is refused"
cp "$ROOT/bin/compiler-candidate-znver4" "$ROOT/bin/compiler-znver4"
cp "$ROOT/bin/compiler-candidate-znver4.commit" "$ROOT/bin/compiler-znver4.commit"
echo '#define FAKE 1' > "$ROOT/predefines-znver5.h"
setup() { REPOSITORY=$repository CORPUS=$work BUILD_ARCHES=znver4 run setup; }
setup
expect '[[ $status -ne 0 ]]' "setup: a build of another commit than the checkout's is refused"
rm -f -- "$ROOT/bin/compiler-znver4" "$ROOT/bin/compiler-znver4.commit"
setup
expect '[[ $status -eq 0 && $("$ROOT/bin/compiler-znver4") == second && $(cat "$ROOT/bin/compiler-znver4.commit") == "$second" ]]' "setup: builds the checkout and records its commit"
expect '[[ $(cat "$ROOT/self-main.i") == "preprocessed second" && -s $ROOT/self-main.i.source ]]' "setup: the checkout's build preprocesses self-main.i, keyed"
setup
expect '[[ $status -eq 0 && $(cat "$ROOT/self-main.i") == "preprocessed second" ]]' "setup: the checkout's own self-main.i is reused"

# Another checkout: its build is made once the old one is removed, as setup says,
# and the old self-main.i is refused until it is removed too.
git -C "$repository" checkout -q "$first"
rm -f -- "$ROOT/bin/compiler-znver4" "$ROOT/bin/compiler-znver4.commit"
setup
expect '[[ $status -ne 0 && $("$ROOT/bin/compiler-znver4") == first && $(cat "$ROOT/self-main.i") == "preprocessed second" ]]' "setup: another checkout's self-main.i is refused"
expect 'grep -q "self-main.i was made from" "$results/run.log"' "setup: the refused self-main.i is logged"
rm -f -- "$ROOT/self-main.i"
setup
expect '[[ $status -eq 0 && $(cat "$ROOT/self-main.i") == "preprocessed first" ]]' "setup: self-main.i is made again from the checkout"
echo '#define OTHER 1' > "$ROOT/predefines-znver5.h"
setup
expect '[[ $status -ne 0 ]]' "setup: a self-main.i made from other predefines is refused"
echo '#define FAKE 1' > "$ROOT/predefines-znver5.h"
setup
expect '[[ $status -eq 0 ]]' "setup: the predefines it was made from are accepted again"

# Dirty states of the checkout: each names its diff.
echo third > "$repository/VERSION"
rm -f -- "$ROOT/bin/compiler-znver4" "$ROOT/bin/compiler-znver4.commit" "$ROOT/self-main.i"
setup
expect '[[ $status -eq 0 && $("$ROOT/bin/compiler-znver4") == third && $(cat "$ROOT/bin/compiler-znver4.commit") == "$first"-dirty-* ]]' "setup: a dirty checkout's build records the commit and its diff"
echo fourth > "$repository/VERSION"
setup
expect '[[ $status -ne 0 && $("$ROOT/bin/compiler-znver4") == third ]]' "setup: a build of another dirty state is refused"

# results/HOST is bound to the inputs it was measured with.
echo third > "$repository/VERSION"
run time
expect '[[ $status -eq 0 ]] && grep -q "^compiler-znver4 " "$results/identity.txt" && grep -q "^self-main.i " "$results/identity.txt"' "time: results/HOST records its inputs"
REPOSITORY=$repository run build-commit other "$second" znver4
run time
expect '[[ $status -eq 0 ]] && grep -q "^compiler-other-znver4 " "$results/identity.txt"' "time: a build first seen is added to the inputs"
# Another state of the checkout, rebuilt as setup asks: the old results stay its.
echo fifth > "$repository/VERSION"
rm -f -- "$ROOT/bin/compiler-znver4" "$ROOT/bin/compiler-znver4.commit" "$ROOT/self-main.i"
setup
expect '[[ $status -eq 0 && $("$ROOT/bin/compiler-znver4") == fifth ]]' "setup: builds the checkout's new state"
run time
expect '[[ $status -ne 0 ]] && grep -q "measured with another compiler-znver4" "$results/run.log"' "time: results measured with another build are refused"
rm -rf -- "${results:?}"
run time
expect '[[ $status -eq 0 && -f $results/time/time.tsv ]]' "time: new results measure the new inputs"
rm -f -- "$results/identity.txt"
run time
expect '[[ $status -ne 0 ]] && grep -q "no identity.txt" "$results/run.log"' "time: results without identity.txt are refused"

echo "microarch test.sh: $checks checks, $failures failed"
[[ $failures -eq 0 ]]
