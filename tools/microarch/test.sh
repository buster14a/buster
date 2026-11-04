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
#   reducer fails leaves no .done marker and no table, and a rerun records it.
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
# fails when behaviour/perf says "script"; `report` prints one row.
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
UNCORE=0
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

echo "microarch test.sh: $checks checks, $failures failed"
[[ $failures -eq 0 ]]
