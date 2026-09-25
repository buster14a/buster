#!/usr/bin/env bash
set -euo pipefail
source_dir=$(cd -- "$(dirname -- "$0")" && pwd)
ide=$(realpath "$1")
out=$(realpath -m "$2")
mkdir -p "$out"
cp "$source_dir"/{cases.def,producer.c,observer.c,run.sh} "$out/"
cd "$out"
exec > >(tee -a harness.log) 2>&1
clang --version > clang-version.txt
gcc --version > gcc-version.txt
uname -a > environment.txt
lscpu >> environment.txt
sha256sum "$ide" > compiler.sha256
clang -std=c17 -O0 -Wall -Wextra -Werror -pedantic-errors -c observer.c -o observer.o
printf 'profile\tcompile_status\tlink_status\trun_status\n' > results.tsv
bad=0
run_profile() {
    local profile=$1; shift
    mkdir -p "$profile"
    printf '%q ' "$@" > "$profile/command.txt"
    printf '\n' >> "$profile/command.txt"
    local c=0 l=NA r=NA
    timeout 180 "$@" > "$profile/compile.stdout" 2> "$profile/compile.stderr" || c=$?
    if [[ $c == 0 ]]; then
        l=0
        clang observer.o "$profile/producer.o" -o "$profile/observe" > "$profile/link.stdout" 2> "$profile/link.stderr" || l=$?
        objdump -s "$profile/producer.o" > "$profile/object-dump.txt" 2>&1 || true
        if [[ $l == 0 ]]; then
            r=0
            timeout 60 "$profile/observe" > "$profile/values.tsv" 2> "$profile/summary.txt" || r=$?
            cat "$profile/summary.txt"
        fi
    fi
    printf '%s\t%s\t%s\t%s\n' "$profile" "$c" "$l" "$r" | tee -a results.tsv
    if [[ $c != 0 || $l != 0 || $r != 0 ]]; then bad=1; fi
}
for cc in clang gcc; do
    for opt in 0 2; do
        p="$cc-O$opt"
        run_profile "$p" "$cc" -std=c17 -O"$opt" -g0 -Wall -Wextra -Werror -pedantic-errors -c producer.c -o "$p/producer.o"
    done
done
for allocator in none mir-stack fast quality; do
    for ssa in yes no; do
        flag=-ffrontend-ssa
        [[ $ssa == yes ]] || flag=-fno-frontend-ssa
        p="buster-$allocator-$ssa-O0"
        fallback=()
        [[ $allocator == none ]] || fallback=(-fno-machine-fallback)
        run_profile "$p" "$ide" cc -std=c17 -target x86_64-linux -O0 -g0 -fverify-codegen -fregister-allocator="$allocator" "$flag" "${fallback[@]}" -c producer.c -o "$p/producer.o"
    done
done
for ssa in yes no; do
    flag=-ffrontend-ssa
    [[ $ssa == yes ]] || flag=-fno-frontend-ssa
    p="buster-fast-$ssa-O2"
    run_profile "$p" "$ide" cc -std=c17 -target x86_64-linux -O2 -g0 -fverify-codegen -fregister-allocator=fast "$flag" -fno-machine-fallback -c producer.c -o "$p/producer.o"
done
cat > minimal.c <<'C'
static long double value = -(1u);
int main(void) { return value != 4294967295.0L; }
C
printf 'profile\tcompile_status\trun_status\n' > minimal-results.tsv
for cc in clang gcc buster; do
    c=0; r=NA
    if [[ $cc == buster ]]; then
        cmd=("$ide" cc -std=c17 -target x86_64-linux -O0 -g0 -fverify-codegen -fregister-allocator=fast -ffrontend-ssa -fno-machine-fallback minimal.c -o minimal-buster)
    else
        cmd=("$cc" -std=c17 -O0 -Wall -Wextra -Werror -pedantic-errors minimal.c -o "minimal-$cc")
    fi
    printf '%q ' "${cmd[@]}" > "minimal-$cc.command"
    printf '\n' >> "minimal-$cc.command"
    timeout 180 "${cmd[@]}" > "minimal-$cc.compile.stdout" 2> "minimal-$cc.compile.stderr" || c=$?
    if [[ $c == 0 ]]; then
        r=0
        timeout 30 "./minimal-$cc" > "minimal-$cc.stdout" 2> "minimal-$cc.stderr" || r=$?
    fi
    printf '%s\t%s\t%s\n' "$cc" "$c" "$r" | tee -a minimal-results.tsv
    if [[ $c != 0 || $r != 0 ]]; then bad=1; fi
done
# Sanitized reference executions; failures remain distinct from Buster verdicts.
printf 'profile\tcompile_status\trun_status\n' > sanitized-results.tsv
for cc in clang gcc; do
    c=0; r=NA
    cmd=("$cc" -std=c17 -O1 -g -Wall -Wextra -Werror -pedantic-errors -fsanitize=address,undefined -fno-sanitize-recover=all producer.c observer.c -o "sanitized-$cc")
    printf '%q ' "${cmd[@]}" > "sanitized-$cc.command"
    printf '\n' >> "sanitized-$cc.command"
    timeout 180 "${cmd[@]}" > "sanitized-$cc.compile.stdout" 2> "sanitized-$cc.compile.stderr" || c=$?
    if [[ $c == 0 ]]; then
        r=0
        ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 timeout 60 "./sanitized-$cc" > "sanitized-$cc.values.tsv" 2> "sanitized-$cc.stderr" || r=$?
    fi
    printf '%s\t%s\t%s\n' "$cc" "$c" "$r" | tee -a sanitized-results.tsv
    if [[ $c != 0 || $r != 0 ]]; then bad=1; fi
done
# A separate backend consumes the same canonical globals. This is diagnostic,
# not timing evidence, and a tool failure is not a semantic mismatch.
cat > canonical.c <<'C'
long double direct = -1u;
long double parenthesized = -(1u);
long double signed_one = -(1);
long double unsigned_long = -(1ul);
C
cmd=("$ide" cc -std=c17 -target x86_64-linux -O0 -g0 -emit-llvm -c canonical.c -o canonical.buster.bc)
printf '%q ' "${cmd[@]}" > canonical-buster.command
printf '\n' >> canonical-buster.command
c=0
"${cmd[@]}" > canonical-buster.stdout 2> canonical-buster.stderr || c=$?
printf 'buster-bitcode-status=%s\n' "$c" > canonical-status.txt
dis=$(command -v llvm-dis || command -v llvm-dis-21 || true)
if [[ $c == 0 && -n $dis ]]; then
    c=0
    "$dis" canonical.buster.bc -o canonical.buster.ll > canonical-dis.stdout 2> canonical-dis.stderr || c=$?
    printf 'disassembler=%s status=%s\n' "$dis" "$c" >> canonical-status.txt
fi
c=0
clang -std=c17 -O0 -S -emit-llvm canonical.c -o canonical.clang.ll > canonical-clang.stdout 2> canonical-clang.stderr || c=$?
printf 'clang-ir-status=%s\n' "$c" >> canonical-status.txt
find . -type f ! -name SHA256SUMS ! -name harness.log -print0 | sort -z | xargs -0 sha256sum > SHA256SUMS
exit "$bad"
