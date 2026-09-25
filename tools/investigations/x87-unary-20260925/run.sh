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
find . -type f ! -name SHA256SUMS ! -name harness.log -print0 | sort -z | xargs -0 sha256sum > SHA256SUMS
exit "$bad"
