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
# The original 778-header bundle predates this main.c's linux/memfd.h use.
# The separately frozen supplement adds one header, after all pinned paths.
supplement="$task_root/main-headers-v1"
(cd "$supplement" && sha256sum --check files.sha256)
main_options=("${system[@]}" -isystem "$supplement/usr/include"
    -I "$frozen/src" -fwrapv -funsigned-char)
case "${1:-}" in
    implementation-repair)
        corpus implementation-headers-v1 system-pinned.args "$task_root/117-implementation-memfd-v1.txt" "$task_root/candidate-9b70ee7" \
            -isystem "$supplement/usr/include" -I "$task_root/candidate-9b70ee7/src" -fwrapv -funsigned-char
        ;;
    macros-main)
        run macros-main-headers-v1 "$candidate" test-preprocessor --clang --fuzz 0 \
            "${main_options[@]}" "$frozen/src/buster/compiler/main.c"
        ;;
    measure-main)
        run candidate-main-headers-v1 taskset -c 2 "$candidate" benchmark-preprocessor --clang \
            "${main_options[@]}" "$frozen/src/buster/compiler/main.c"
        run baseline-main-headers-v1 taskset -c 2 "$baseline" benchmark-preprocessor --clang \
            "${main_options[@]}" "$frozen/src/buster/compiler/main.c"
        run guards-main-headers-v1 taskset -c 2 "$candidate" benchmark-preprocessor --guard-cost \
            "${main_options[@]}" "$frozen/src/buster/compiler/main.c"
        ;;
    guards-sqlite)
        run guards-sqlite taskset -c 2 "$candidate" benchmark-preprocessor --guard-cost \
            "${system[@]}" "$bundle/sqlite/sqlite3.c"
        ;;
    *)
        printf 'usage: %s implementation-repair|macros-main|measure-main|guards-sqlite\n' "$0" >&2
        exit 2
        ;;
esac
