#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# Shell/API boundaries for feedback.c and the existing parallel runner. No qualification or cancellation policy here.
set -euo pipefail
shopt -s inherit_errexit

smoke()
{
    local work compiler=${BUSTER_COMPILER:?BUSTER_COMPILER is required}
    unset CPATH C_INCLUDE_PATH
    work=$(mktemp -d "${BUSTER_FEEDBACK_WORK:-${TMPDIR:-/tmp}}/feedback-corpus.XXXXXX")
    # Keep failed inputs and outputs. The record wrapper's artifact owns this directory when CI runs it.
    local status=0
    set +e
    (
        set -e
        printf 'feedback-case: preprocessing (fixed corpus; no random seed)\n'
        "$compiler" -E -P tests/ci/feedback/driver.c -o "$work/driver.i"
        test -s "$work/driver.i"
        printf 'feedback-case: parsing\n'
        "$compiler" parse -fsyntax-only tests/ci/feedback/driver.c
        printf 'feedback-case: checking\n'
        "$compiler" -fsyntax-only -Wall -Werror tests/ci/feedback/driver.c
        printf 'feedback-case: object generation\n'
        "$compiler" --lanes 1 --verify -c -march=x86-64 tests/ci/feedback/driver.c -o "$work/driver.o"
        "$compiler" --lanes 1 --verify -c -march=x86-64 tests/ci/feedback/helper.c -o "$work/helper.o"
        printf 'feedback-case: linking and execution\n'
        clang -no-pie "$work/driver.o" "$work/helper.o" -o "$work/program"
        "$work/program"
        printf 'feedback-case: diagnostic regression\n'
        if "$compiler" -fsyntax-only tests/ci/feedback/invalid.c > "$work/diagnostic.txt" 2>&1; then
            echo 'feedback: invalid.c was incorrectly accepted' >&2
            exit 1
        fi
        cat "$work/diagnostic.txt"
        grep -q 'error:.*buster_feedback_undeclared' "$work/diagnostic.txt"
        grep -q 'invalid.c:4:' "$work/diagnostic.txt"
    )
    status=$?
    set -e
    if [ "$status" = 0 ]; then rm -rf -- "$work"; else echo "feedback: retained failing case in $work" >&2; fi
    return "$status"
}

consume_environment()
{
    local line
    while IFS= read -r line; do
        [[ "$line" =~ ^[A-Za-z_][A-Za-z_0-9]*= ]] || { echo 'feedback: invalid environment handoff' >&2; return 1; }
        export "$line"
    done < "$GITHUB_ENV"
    : > "$GITHUB_ENV"
    while IFS= read -r line; do export PATH="$line:$PATH"; done < "$GITHUB_PATH"
    : > "$GITHUB_PATH"
}

# The vector draws' probe (tools/ci/vector-probe.c; #2924): whether this CPU and its OS enable every feature the
# compiler's AVX-512 host needs. A drawn item's probe ran before its checkout (BUSTER_VECTOR_PROBE, which the draw
# builds); without one it is built here with the explicit vector target's flags. Writes its FEATURE<TAB>yes|no lines and
# its status to FILE, and returns that status: 0 when the CPU qualifies.
probe()
{
    local file=$1 executable=${BUSTER_VECTOR_PROBE:-} built='' status=0 flags=()
    if [ -z "$executable" ]; then
        mapfile -d '' -t flags < <(BUSTER_HOST_ARCH=vector ./build.sh print-host-flags)
        [ "${#flags[@]}" -gt 0 ]
        built=$(mktemp "${TMPDIR:-/tmp}/feedback-probe.XXXXXX")
        executable=$built
        cc -std=c17 -O2 -Wall -Wextra -Werror "${flags[@]}" tools/ci/vector-probe.c -o "$executable"
    fi
    "$executable" > "$file" 2>&1 || status=$?
    printf 'status\t%s\n' "$status" >> "$file"
    if [ -n "$built" ]; then rm -f -- "$built"; fi
    return "$status"
}

replay()
{
    local record=$1 current expected
    record=$(realpath "$record")
    current=$(git rev-parse HEAD)
    expected=$(cat "$record/revision")
    [ "$current" = "$expected" ] || { echo "feedback: checkout $expected first (current $current)" >&2; return 1; }
    git diff --quiet HEAD -- || { echo 'feedback: replay requires the recorded revision without tracked edits' >&2; return 1; }
    if grep -xq $'kind\ttests' "$record/context.tsv" && { [ ! -s "$record/command.sh" ] || [ ! -s "$record/selected.tests" ]; }; then
        echo 'feedback: incomplete test record; no passing reproduction can be claimed' >&2
        return 1
    fi
    unset BUSTER_PRODUCT BUSTER_COMPILER BUSTER_HOST_RUNTIME BUSTER_CLANG_PRAGMA_ORACLE BUSTER_CLANG_SERVER BUSTER_CI_CACHE BUSTER_CI_OFFLINE
    # This is a generated, quoted script of configuration values, never contents from a test log.
    source "$record/environment.sh"
    RUNNER_TEMP=$(mktemp -d)
    export RUNNER_TEMP GITHUB_ENV="$RUNNER_TEMP/environment" GITHUB_PATH="$RUNNER_TEMP/path" GITHUB_OUTPUT="$RUNNER_TEMP/output"
    : > "$GITHUB_ENV"
    : > "$GITHUB_PATH"
    # Every hosted job that builds or runs the compiler drew a CPU whose probe qualified (#2924): so must this one. A
    # native build is its CPU's alone, so its replay needs the recorded CPU model too; the vector target runs on any
    # qualifying CPU.
    probe "$RUNNER_TEMP/probe.txt" || { cat "$RUNNER_TEMP/probe.txt" >&2; echo 'feedback: this CPU lacks a feature the compiler'"'"'s AVX-512 host needs' >&2; return 1; }
    if [ "${BUSTER_HOST_ARCH:-native}" = native ]; then
        LC_ALL=C lscpu | grep '^Model name:' > "$RUNNER_TEMP/cpu-model"
        grep '^Model name:' "$record/cpu.txt" > "$RUNNER_TEMP/recorded-cpu-model"
        cmp "$RUNNER_TEMP/cpu-model" "$RUNNER_TEMP/recorded-cpu-model" || { echo 'feedback: native replay needs the recorded CPU model' >&2; return 1; }
    fi
    export BUSTER_HOSTED_RELEASES="$RUNNER_TEMP/releases" BUSTER_HOSTED_OFFLINE="$RUNNER_TEMP/offline"
    bash tools/ci/hosted-release.sh prepare "$BUSTER_HOSTED_RELEASES"
    # Prefer the original apt closure and frozen-input mirrors to resolving packages again. Its name is the
    # successful preparation attempt's, not necessarily this failed consumer's attempt. Verify every byte below.
    local restored=false repository run handoff
    if [ -s "$record/hosted-inputs.tsv" ] && command -v gh > /dev/null; then
        IFS=$'\t' read -r repository run handoff < "$record/hosted-inputs.tsv"
        [[ "$repository" =~ ^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$ && "$run" =~ ^[1-9][0-9]*$ && "$handoff" =~ ^hosted-inputs-[0-9]+-[0-9]+$ ]] ||
            { echo 'feedback: invalid prepared-input identity' >&2; return 1; }
        if gh run download "$run" --repo "$repository" --name "$handoff-offline" --dir "$BUSTER_HOSTED_OFFLINE"; then
            restored=true
        else
            rm -rf -- "$BUSTER_HOSTED_OFFLINE"
            echo 'feedback: original handoff unavailable; reacquiring and comparing pinned inputs' >&2
        fi
    fi
    if [ "$restored" != true ]; then bash tools/ci/hosted-offline.sh prepare "$BUSTER_HOSTED_OFFLINE"; fi
    bash tools/ci/hosted-offline.sh verify "$BUSTER_HOSTED_OFFLINE"
    if [ -f "$record/apt-manifest" ]; then
        # Image labels may differ, package bytes may not. Never silently call a changed toolchain an exact replay.
        cmp <(sed '/^runner /d' "$record/apt-manifest") <(sed '/^runner /d' "$BUSTER_HOSTED_OFFLINE/apt/MANIFEST") ||
            { echo 'feedback: apt inputs differ; use the recorded runner image and apt manifest' >&2; return 1; }
    fi
    export BUSTER_CI_CACHE="$BUSTER_HOSTED_OFFLINE/ci" BUSTER_CI_OFFLINE=1
    # The drawn item's steps, in its order (.github/workflows/hosted-correctness.yml).
    bash tools/ci/setup-hosted.sh
    consume_environment
    if [ "$BUSTER_SANITIZER" != none ]; then
        bash tools/ci/sanitizers.sh configure "$RUNNER_TEMP/sanitizer-reports"
        consume_environment
        bash tools/ci/sanitizers.sh probe "$RUNNER_TEMP/sanitizer-probes"
    fi
    bash tools/ci/hosted-product.sh build
    consume_environment
    if [ -f "$record/command.sh" ]; then
        bash tools/ci/fetch-corpus.sh "$RUNNER_TEMP/corpus"
        export BUSTER_CORPUS="$RUNNER_TEMP/corpus"
        local flags=()
        mapfile -d '' -t flags < <(./build.sh print-host-flags)
        [ "${#flags[@]}" -gt 0 ]
        mkdir -p build
        clang -dM -E "${flags[@]}" -fwrapv -funsigned-char -x c /dev/null > build/predefines.h
        bash tools/ci/fetch-c-testsuite.sh build/c-testsuite
        bash tools/ci/fetch-sqlite.sh build/sqlite
        consume_environment
        export BUSTER_FEEDBACK_WORK="$RUNNER_TEMP"
        # command.sh resolves the freshly built compiler and its recorded inventory/runner options.
        bash "$record/command.sh" "$record"
    fi
    if [ "$BUSTER_SANITIZER" != none ]; then bash tools/ci/sanitizers.sh audit "$RUNNER_TEMP/sanitizer-reports"; fi
}

record()
{
    local kind=$1 directory=$2 profile=$3
    shift 3
    mkdir -p "$directory/cases"
    directory=$(realpath "$directory")
    feedback_phase=infrastructure
    feedback_result=infrastructure-failure
    feedback_directory=$directory
    feedback_profile=$profile
    feedback_kind=$kind
    finish()
    {
        local exit_status=$?
        printf 'result\t%s\nexit\t%s\nphase\t%s\nfinished\t%s\n' "$feedback_result" "$exit_status" "$feedback_phase" "$(date -u +%FT%TZ)" > "$feedback_directory/result.tsv"
        if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
            local note=''
            if [ "$feedback_profile" != full ]; then note='; not qualification'; fi
            printf '\n### Hosted feedback: %s (%s%s)\n\nRevision: `%s`. Profile: `%s`. Result: **%s**.\n\nDownload this job\x27s `feedback-*` artifact and run `bash reproduce.sh` from a clean checkout of that revision on Ubuntu 26.04. The artifact contains the exact inventory, runner arguments, CPU and its probe, fixed cases/seeds, tool pins and logs.\n' \
                "$feedback_kind" "$feedback_phase" "$note" "$(cat "$feedback_directory/revision" 2>/dev/null || echo unknown)" "$feedback_profile" "$feedback_result" >> "$GITHUB_STEP_SUMMARY"
        fi
        # A passing build or full suite needs no annotation: its step passed. An early or partial pass says what it isn't.
        if [ "$feedback_result" = passed ] && [ "$feedback_profile" != full ]; then
            printf '::notice title=Early hosted feedback (not qualification)::%s %s passed; full required coverage remains independent.\n' "$feedback_kind" "$feedback_profile"
        elif [ "$feedback_result" != passed ]; then
            printf '::error title=Hosted feedback %s::%s: %s (exit %s). Exact reproduction and logs: feedback artifact.\n' "$feedback_result" "$feedback_kind" "$feedback_profile" "$exit_status"
        fi
    }
    trap finish EXIT
    trap 'feedback_result=incomplete; exit 130' INT
    trap 'feedback_result=incomplete; exit 143' TERM
    printf 'started\t%s\nprofile\t%s\nkind\t%s\n' "$(date -u +%FT%TZ)" "$profile" "$kind" > "$directory/context.tsv"
    git rev-parse HEAD > "$directory/revision"
    git status --porcelain > "$directory/working-tree.txt"
    cp tools/ci/hosted-tools.lock "$directory/hosted-tools.lock"
    if [ -n "${BUSTER_FEEDBACK_HOSTED_TOOLS:-}" ]; then
        printf '%s\t%s\t%s\n' "${GITHUB_REPOSITORY:?recorded repository}" "${GITHUB_RUN_ID:?recorded run}" "$BUSTER_FEEDBACK_HOSTED_TOOLS" > "$directory/hosted-inputs.tsv"
    fi
    cp /etc/os-release "$directory/os-release"
    if [ -f "${BUSTER_HOSTED_OFFLINE:-}/apt/MANIFEST" ]; then
        cp "$BUSTER_HOSTED_OFFLINE/apt/MANIFEST" "$directory/apt-manifest"
    fi
    uname -a > "$directory/kernel.txt"
    LC_ALL=C lscpu > "$directory/cpu.txt"
    clang --version > "$directory/clang.txt"
    local flags=() name
    mapfile -d '' -t flags < <(./build.sh print-host-flags)
    [ "${#flags[@]}" -gt 0 ]
    printf '%q ' "${flags[@]}" > "$directory/host-flags.txt"
    # The CPU's AVX-512 features as the draw's probe saw them; its replay requires a CPU that qualifies the same way.
    probe "$directory/probe.txt" || true
    : > "$directory/environment.sh"
    for name in BUSTER_HOST_ARCH BUSTER_SANITIZER BUSTER_SANITIZER_OPTIMIZATION BUSTER_LINKER BUSTER_DEBUG_INFO BUSTER_ORACLE_CACHE BUSTER_DRIVER_DIFFERENTIAL ImageOS ImageVersion; do
        if [[ -v $name ]]; then
            printf 'export %s=%q\n' "$name" "${!name}" >> "$directory/environment.sh"
        else
            printf 'unset %s\n' "$name" >> "$directory/environment.sh"
        fi
    done
    cat > "$directory/reproduce.sh" <<'REPRODUCE'
#!/usr/bin/env bash
set -euo pipefail
record=$(cd -- "$(dirname -- "$0")" && pwd)
exec bash tools/ci/feedback.sh replay "$record"
REPRODUCE
    printf 'Run from the recorded clean checkout on Ubuntu 26.04: bash /path/to/artifact/reproduce.sh\nThe replay acquires the revision\x27s pinned tools and fixtures and rebuilds once. CPU model is in cpu.txt; the replay\x27s CPU must pass the vector probe (probe.txt), and a native replay needs the same model.\nCase/seed: smoke uses tests/ci/feedback (fixed corpus, no random seed). Other headings retain their literal seed options/defaults in selected.tests; the failing case and any generated seed are in output.log.\n' > "$directory/README.txt"
    local status=0
    if [ "$kind" = build ]; then
        feedback_phase=build
        set +e
        bash tools/ci/hosted-product.sh build 2>&1 | tee "$directory/output.log"
        local pipeline=("${PIPESTATUS[@]}")
        set -e
        status=${pipeline[0]}
        if [ "${pipeline[1]}" != 0 ]; then feedback_result=infrastructure-failure; status=${pipeline[1]};
        elif [ "$status" = 0 ]; then feedback_result=passed; else feedback_result=product-failure; fi
    else
        test -x "${BUSTER_COMPILER:?BUSTER_COMPILER is required}"
        cc -std=c17 -O2 -Wall -Wextra -Werror -fno-strict-aliasing tools/ci/feedback.c -o "$directory/feedback"
        local plan=(--profile "$profile" --inventory tools/ci/correctness.tests --report "$directory/plan.md")
        if [ "$profile" = affected ]; then plan+=(--files "${BUSTER_FEEDBACK_FILES:?affected requires BUSTER_FEEDBACK_FILES}"); fi
        "$directory/feedback" "${plan[@]}" > "$directory/selected.tests"
        {
            printf '#!/usr/bin/env bash\nset -euo pipefail\nrecord=${1:?artifact directory}\n'
            printf '"${BUSTER_COMPILER:?built compiler}" parallel --github '
            printf '%q ' "$@"
            printf '< "$record/selected.tests"\n'
        } > "$directory/command.sh"
        if [ -n "${BUSTER_PRODUCT:-}" ]; then cp -r "$BUSTER_PRODUCT/provenance" "$directory/product-provenance"; fi
        export BUSTER_FEEDBACK_WORK="$directory/cases"
        feedback_phase=tests
        feedback_result=incomplete
        set +e
        "$BUSTER_COMPILER" parallel --github "$@" < "$directory/selected.tests" 2>&1 | tee "$directory/output.log"
        local pipeline=("${PIPESTATUS[@]}")
        set -e
        status=${pipeline[0]}
        if [ "${pipeline[1]}" != 0 ]; then feedback_result=infrastructure-failure; status=${pipeline[1]};
        elif grep -Eq '^::error title=.*::failed \(can.t start ' "$directory/output.log"; then feedback_result=infrastructure-failure; [ "$status" != 0 ] || status=1;
        elif grep -Eq '^::error title=.*::failed \(timed out\)' "$directory/output.log"; then feedback_result=incomplete; [ "$status" != 0 ] || status=1;
        elif [ "$status" = 0 ] && grep -Eq '^parallel: .* [1-9][0-9]* passed, 0 failed, [0-9]+ skipped, 0 not started$' "$directory/output.log" &&
            { [ "$profile" != smoke ] || grep -Eq ' passed, 0 failed, 0 skipped, 0 not started$' "$directory/output.log"; }; then feedback_result=passed;
        elif grep -Eq '^parallel: .* [1-9][0-9]* failed,' "$directory/output.log"; then feedback_result=product-failure;
        else feedback_result=incomplete; [ "$status" != 0 ] || status=1; fi
    fi
    exit "$status"
}

case "${1:-}" in
    smoke) smoke ;;
    replay) replay "${2:?artifact directory}" ;;
    build) record build "${2:?record directory}" full ;;
    run) shift; record tests "$@" ;;
    *) echo 'usage: feedback.sh smoke | build DIRECTORY | run DIRECTORY PROFILE [parallel options] | replay DIRECTORY' >&2; exit 2 ;;
esac
