#!/usr/bin/env bash
# Invocation glue only: every build/test/measurement takes its own normal lock.
set -euo pipefail
export PATH=/usr/bin:/bin
task_root=/home/david/buster-fast-bench/codex-117-01a16812
bundle=/home/david/buster-fast-bench/preprocessor-corpora-astra-01a158dd
candidate="$task_root/candidate-9b70ee7/build/compiler"
baseline="$task_root/baseline-21d632c/build/compiler"
frozen="$task_root/base-635434f2"
evidence="$task_root/evidence-9b70ee7"
export SOURCE_DATE_EPOCH=1792454400
export EXCLUSIVE_OWNER=codex-117-acceptance
export EXCLUSIVE_LINK=https://github.com/buster14a/buster/pull/323
unset CPATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH
system=(-target x86_64-linux-gnu -march=znver5 -std=gnu17 -nostdinc
    -isystem "$bundle/headers/usr/lib/clang/23/include"
    -isystem "$bundle/headers/usr/local/include"
    -isystem "$bundle/headers/usr/include")
run()
{
    local label="$1"
    shift
    test ! -e "$evidence/$label.log"
    export EXCLUSIVE_TITLE="Issue 117 $label"
    {
        date -u +'%Y-%m-%dT%H:%M:%SZ queued'
        printf 'command:'
        printf ' %q' "$@"
        printf '\n'
        "$HOME/buster-fast-bench/exclusive.sh" bash "$task_root/window-run.sh" "$@"
        date -u +'%Y-%m-%dT%H:%M:%SZ passed'
    } > "$evidence/$label.log" 2>&1
    printf '%s passed\n' "$label"
}
corpus()
{
    local label="$1" config="$2" inventory="$3" root="$4"
    shift 4
    local options=()
    mapfile -t options < "$bundle/config/$config"
    run "$label" "$candidate" test-preprocessor --compare-clang --clang /usr/bin/clang \
        "${options[@]}" "$@" --files-from "$inventory" --root "$root" --report "$evidence/$label.tsv"
}
case "${1:-}" in
    focused)
        run fixtures "$candidate" test-preprocessor --clang
        ;;
    tables)
        run tables-oracle "$candidate" test-preprocessor --tables "$task_root/candidate-9b70ee7" "$bundle/libc"
        run tables-benchmark taskset -c 2 "$candidate" benchmark-preprocessor --tables "$bundle/libc"
        run defines-benchmark taskset -c 2 "$candidate" benchmark-preprocessor --defines "$bundle/libc"
        ;;
    corpus)
        corpus sqlite system-pinned.args "$bundle/config/sqlite-c.txt" "$bundle/sqlite"
        corpus musl-ordinary musl.args "$bundle/config/musl-ordinary.txt" "$bundle/libc/musl"
        corpus musl-crt musl-crt.args "$bundle/config/musl-crt.txt" "$bundle/libc/musl"
        corpus musl-crt-pic musl-crt-pic.args "$bundle/config/musl-crt-pic.txt" "$bundle/libc/musl"
        corpus musl-ldso musl-ldso.args "$bundle/config/musl-ldso.txt" "$bundle/libc/musl"
        corpus libc system-pinned.args "$bundle/config/libc-ch.txt" "$bundle/libc"
        corpus repository system-pinned.args "$bundle/config/repository-c.txt" "$bundle/repository" -I "$bundle/repository/src"
        corpus implementation system-pinned.args "$task_root/117-implementation-9b70ee7-c.txt" "$task_root/candidate-9b70ee7" \
            -I "$task_root/candidate-9b70ee7/src" -fwrapv -funsigned-char
        ;;
    macros)
        run macros-sqlite "$candidate" test-preprocessor --clang --fuzz 0 "${system[@]}" "$bundle/sqlite/sqlite3.c"
        run macros-main "$candidate" test-preprocessor --clang --fuzz 0 "${system[@]}" \
            -I "$frozen/src" -fwrapv -funsigned-char "$frozen/src/buster/compiler/main.c"
        ;;
    measure)
        run baseline-sqlite taskset -c 2 "$baseline" benchmark-preprocessor --clang "${system[@]}" "$bundle/sqlite/sqlite3.c"
        run candidate-sqlite taskset -c 2 "$candidate" benchmark-preprocessor --clang "${system[@]}" "$bundle/sqlite/sqlite3.c"
        run candidate-main taskset -c 2 "$candidate" benchmark-preprocessor --clang "${system[@]}" \
            -I "$frozen/src" -fwrapv -funsigned-char "$frozen/src/buster/compiler/main.c"
        run baseline-main taskset -c 2 "$baseline" benchmark-preprocessor --clang "${system[@]}" \
            -I "$frozen/src" -fwrapv -funsigned-char "$frozen/src/buster/compiler/main.c"
        run guards-sqlite taskset -c 2 "$candidate" benchmark-preprocessor --guard-cost "${system[@]}" "$bundle/sqlite/sqlite3.c"
        run guards-main taskset -c 2 "$candidate" benchmark-preprocessor --guard-cost "${system[@]}" \
            -I "$frozen/src" -fwrapv -funsigned-char "$frozen/src/buster/compiler/main.c"
        ;;
    *)
        printf 'usage: %s focused|tables|corpus|macros|measure\n' "$0" >&2
        exit 2
        ;;
esac
