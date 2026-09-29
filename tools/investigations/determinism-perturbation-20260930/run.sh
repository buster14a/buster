#!/usr/bin/env bash
# Isolated correctness experiment, not a benchmark or an admission workflow.
set -euo pipefail
root=$(pwd)
probe=tools/investigations/determinism-perturbation-20260930
base=8f67df736f13d4edc055110a7a6d619a00a22eaf
mkdir -p det-evidence
{
    git rev-parse HEAD
    git rev-parse "$base^{tree}"
    uname -a
    lscpu
    clang --version
} > det-evidence/environment.txt
# Refuse production source changes before applying the diagnostic overlay.
git diff --exit-code "$base" HEAD -- src build.c CMakeLists.txt cmake
clang -std=c11 -O2 -Wall -Wextra -Werror "$probe/apply.c" -o det-evidence/apply
clang -Isrc -Wall -Werror -Wno-unused-function -Wno-unused-variable -g build.c -o det-build
./det-build generate --cc clang --ci --linker DEFAULT -DBUSTER_UNITY_BUILD=ON -DBUSTER_INCLUDE_TESTS=OFF
./det-build build --config Release -t ide
cp build/Release/ide det-evidence/ide-original
./det-evidence/apply
git diff --check
git diff -- src > det-evidence/overlay.diff
./det-build build --config Release -t ide
cp build/Release/ide det-evidence/ide-probe
sha256sum det-evidence/ide-* > det-evidence/compiler-sha256.txt
cp build/CMakeCache.txt det-evidence/ 2>/dev/null || true
mkdir det-evidence/work
cd det-evidence/work
cat > common.h <<'C'
#ifndef DET_COMMON_H
#define DET_COMMON_H
#define CAT_I(a,b) a##b
#define CAT(a,b) CAT_I(a,b)
typedef struct Pair { int a; int b; } Pair;
#endif
C
cat > main.c <<'C'
#include "common.h"
int f0(int); int f1(int); int f2(int); int f3(int);
int f4(int); int f5(int); int f6(int); int f7(int);
int main(void)
{
    int sum = f0(7)+f1(7)+f2(7)+f3(7)+f4(7)+f5(7)+f6(7)+f7(7);
    return sum == 116 ? 0 : 25;
}
C
for i in {0..7}; do
    cat > "u$i.c" <<C
#include "common.h"
_Alignas(64) static const int bias = $((i+1));
static const int* pointer = &bias;
static const char date_text[] = __DATE__ " " __TIME__;
static const char file_text[] = __FILE__;
int CAT(f,$i)(int x)
{
    Pair p = {x, *pointer};
    for (int j = 0; j < 3; ++j) p.a += j;
    return p.a + p.b + (date_text[0] == 0) + (file_text[0] == 0);
}
C
done
printf '#warning stable-warning, punctuation stays!\nint warned(void){return 2;}\n' > warn.c
printf '#line 71 "virtual-error.c"\nint broken(void){return absent_symbol;}\n' > bad.c
printf '#error later-error-must-not-win\n' > later.c
printf 'int collision(int x){return x+99;}\n' > warm.c
printf 'int collision(int); int main(void){return collision(1)==100?0:1;}\n' > warm-main.c
printf '#error independent-prior-error\n' > warm-bad.c
printf '%s\n' '-nostdinc -g0 -fcompile-jobs=4 -target x86_64-unknown-linux warm-main.c warm.c -o warm-out' > warm-x86.rsp
printf '%s\n' '-nostdinc -g0 -target aarch64-unknown-linux -c warm.c -o warm-arm.o' > warm-arm.rsp
printf '%s\n' '-nostdinc -g0 -target x86_64-unknown-linux -c warm-bad.c -o warm-bad.o' > warm-bad.rsp
sha256sum -- *.c *.h *.rsp > ../inputs-before.sha256
# A distinct semantic oracle. Byte identity is never required against Clang.
clang -std=gnu17 -O0 -fsanitize=address,undefined main.c u{0..7}.c warn.c -o oracle 2> ../oracle-build.stderr
./oracle > ../oracle.stdout 2> ../oracle.stderr
printf '0\n' > ../oracle.status
rows=0
comparisons=0
differences=0
failures=0
run_row() {
    local dest=$1 binary=$2 enabled=$3 jobs=$4 schedule=$5 placement=$6 history=$7 tracing=$8
    mkdir -p "$dest"
    rm -rf det-stage
    mkdir det-stage
    rm -f det-reach.tsv det-diag.bin
    printf 'PREEXISTING-OUTPUT-MUST-SURVIVE-FAILURE\n' > out
    local command=("$binary" cc -std=gnu17 -nostdinc -target "$target" "$debug" -fregister-allocator=fast -fno-machine-fallback -fverify-codegen -v "-fcompile-jobs=$jobs" "${inputs[@]}" -o out)
    printf '%q ' "${command[@]}" > "$dest/command.txt"
    printf '\n' >> "$dest/command.txt"
    set +e
    DET_ENABLE=$enabled DET_SCHEDULE=$schedule DET_PLACEMENT=$placement DET_HISTORY=$history DET_TRACE=$tracing timeout 60 "${command[@]}" > "$dest/stdout" 2> "$dest/stderr"
    local status=$?
    set -e
    printf '%s\n' "$status" > "$dest/status"
    cp out "$dest/out"
    if [[ $negative == 0 && $status == 0 && $target == x86_64-unknown-linux ]]; then
        set +e
        timeout 5 ./out > "$dest/runtime.stdout" 2> "$dest/runtime.stderr"
        local runtime=$?
        set -e
        printf '%s\n' "$runtime" > "$dest/runtime.status"
        if [[ $runtime != 0 ]]; then failures=$((failures+1)); fi
    fi
    if [[ $negative == 0 && $status != 0 ]] || [[ $negative == 1 && $status == 0 ]] || [[ $status -ge 90 ]]; then
        failures=$((failures+1))
        echo "ROW_FAILURE $dest status=$status"
        cat "$dest/stderr"
    fi
    if [[ $negative == 1 ]] && ! cmp -s out <(printf 'PREEXISTING-OUTPUT-MUST-SURVIVE-FAILURE\n'); then
        failures=$((failures+1)); echo "PUBLICATION_FAILURE $dest"
    fi
    if [[ $enabled == 1 ]]; then
        if [[ -f det-reach.tsv && -f det-diag.bin ]]; then
            cp det-reach.tsv det-diag.bin "$dest/"
        else
            failures=$((failures+1)); echo "MISSING_REACH $dest"
        fi
    fi
    if [[ $tracing == 1 ]]; then
        cp -r det-stage "$dest/stages"
        local count
        count=$(find det-stage -type f | wc -l)
        if [[ $count != 30 ]]; then failures=$((failures+1)); echo "TRACE_COUNT $dest expected=30 got=$count"; fi
        for stage in det-stage/*; do
            if [[ ! -f $stage || $(tail -c 8 "$stage") != BSTREND1 ]]; then
                failures=$((failures+1)); echo "INCOMPLETE_TRACE $dest $stage"
            fi
        done
    fi
    (cd "$dest"; sha256sum out stdout stderr status > sha256.txt)
    rows=$((rows+1))
}
compare_files() {
    local a=$1 b=$2 name=$3
    comparisons=$((comparisons+1))
    if ! cmp -s "$a/$name" "$b/$name"; then
        differences=$((differences+1))
        echo "DIFFERENCE file=$name reference=$a candidate=$b"
        cmp "$a/$name" "$b/$name" || true
    fi
}
compare_public() {
    for name in out stdout stderr status; do compare_files "$1" "$2" "$name"; done
    if [[ $negative == 0 && $target == x86_64-unknown-linux ]]; then
        for name in runtime.stdout runtime.stderr runtime.status; do compare_files "$1" "$2" "$name"; done
    fi
}
for case in elf-g0 elf-g arm-g0 invalid; do
    target=x86_64-unknown-linux; debug=-g0; negative=0
    inputs=(main.c u{0..7}.c warn.c)
    if [[ $case == elf-g ]]; then debug=-g; fi
    if [[ $case == arm-g0 ]]; then target=aarch64-unknown-linux; fi
    if [[ $case == invalid ]]; then inputs=(warn.c bad.c later.c u{0..6}.c); negative=1; fi
    mkdir -p "../$case"
    run_row "../$case/original" "$root/det-evidence/ide-original" 0 1 0 0 0 0
    run_row "../$case/inactive" "$root/det-evidence/ide-probe" 0 1 0 0 0 0
    compare_public "../$case/original" "../$case/inactive"
    for jobs in 1 2 3 4; do
        for profile in identity forward reverse placement history-ab history-ba combined; do
            schedule=0; placement=0; history=0
            case $profile in
                forward) schedule=1;; reverse) schedule=2;; placement) placement=1;;
                history-ab) history=1;; history-ba) history=2;; combined) schedule=2; placement=2; history=1;;
            esac
            row="../$case/j$jobs-$profile"
            run_row "$row" "$root/det-evidence/ide-probe" 1 "$jobs" "$schedule" "$placement" "$history" 0
            compare_public "../$case/original" "$row"
            compare_files "../$case/j1-identity" "$row" det-diag.bin
        done
    done
    if [[ $negative == 0 ]]; then
        for trace_profile in identity combined; do
            jobs=1; schedule=0; placement=0; history=0
            if [[ $trace_profile == combined ]]; then jobs=4; schedule=2; placement=2; history=1; fi
            row="../$case/trace-$trace_profile"
            run_row "$row" "$root/det-evidence/ide-probe" 1 "$jobs" "$schedule" "$placement" "$history" 1
            compare_public "../$case/original" "$row"
            compare_files "../$case/j1-identity" "$row" det-diag.bin
        done
        # Trace comparisons use original byte streams, not hashes/projections invented by this harness.
        for phase in tokens ir mir; do
            for unit in {0..9}; do
                compare_files "../$case/trace-identity/stages" "../$case/trace-combined/stages" "tu-$unit.$phase"
            done
        done
    fi
done
sha256sum --check ../inputs-before.sha256 > ../inputs-after.check
# Comparator positive control does not alter any retained measured artifact.
cp ../elf-g0/original/out comparator-control
printf X | dd of=comparator-control bs=1 seek=0 conv=notrunc status=none
if cmp -s comparator-control ../elf-g0/original/out; then failures=$((failures+1)); fi
rm comparator-control
{
    echo "SUMMARY rows=$rows comparisons=$comparisons differences=$differences failures=$failures"
    echo 'REACH_COUNTS'
    find .. -name det-reach.tsv -exec head -n 1 {} \; | sort | uniq -c
    echo 'COMBINED_X86_REACH'
    cat ../elf-g0/j4-combined/det-reach.tsv
    echo 'ORDER_FORWARD_X86'
    cat ../elf-g0/j4-forward/det-reach.tsv
    echo 'ORDER_REVERSE_X86'
    cat ../elf-g0/j4-reverse/det-reach.tsv
} | tee ../summary.txt
[[ $differences == 0 && $failures == 0 ]]
