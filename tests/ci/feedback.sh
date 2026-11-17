#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# Offline selection, failure classification and reproduction fixtures. Optional --compiler runs real smoke mutants.
set -euo pipefail
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
checks=0
fail() { echo "feedback fixture: $*" >&2; exit 1; }
pass() { checks=$((checks + 1)); }
cc -std=c17 -O2 -Wall -Wextra -Werror -fno-strict-aliasing tools/ci/feedback.c -o "$work/feedback"
cc -std=c17 -O1 -g -Wall -Wextra -Werror -fno-strict-aliasing -fsanitize=address,undefined -fno-sanitize-recover=all tools/ci/feedback.c -o "$work/feedback-sanitized"
export ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=1:abort_on_error=1} UBSAN_OPTIONS=halt_on_error=1
inventory=tools/ci/correctness.tests
plan()
{
    "$work/feedback" --profile "$1" --files "$work/files" --inventory "$inventory" --report "$work/plan.md" > "$work/selected.tests"
}
plan full
sed -n '/^== /p' "$inventory" > "$work/all-headings"
sed -n '/^== /p' "$work/selected.tests" > "$work/headings"
cmp "$work/all-headings" "$work/headings" || fail 'full selection lost a heading'
pass
plan smoke
test "$(grep -c '^== ' "$work/selected.tests")" = 4 || fail 'smoke must have four always-run headings'
grep -q '^bash tools/ci/feedback.sh smoke$' "$work/selected.tests" || fail 'no driver corpus'
pass
for path in src/buster/base.h src/buster/compiler/unknown_new_module.c build.sh CMakeLists.txt cmake/new.cmake .github/workflows/9700x.yml tools/ci/feedback.c tools/ci/correctness.tests AGENTS.md docs/commands/ci.md docs/hosted-toolchain.md tests/checker/shared.h; do
    printf '%s\n' "$path" > "$work/files"
    plan affected
    sed -n '/^== /p' "$work/selected.tests" > "$work/headings"
    cmp "$work/all-headings" "$work/headings" || fail "$path failed to expand shared/unknown coverage"
    pass
done
: > "$work/files"
plan affected
sed -n '/^== /p' "$work/selected.tests" > "$work/headings"
cmp "$work/all-headings" "$work/headings" || fail 'empty paths became a green no-op'
pass
printf 'docs/commands/lexer.md\nREADME.md\n' > "$work/files"
plan affected
grep -q 'documentation exemption' "$work/plan.md" || fail 'documentation has no exemption reason'
test "$(grep -c '^== ' "$work/selected.tests")" -ge 4 || fail 'documentation dropped always-run cases'
grep -q '| skip | no selected dependency group; full qualification retained |' "$work/plan.md" || fail 'skip has no reason'
pass
printf 'src/buster/compiler/object_writer.c\n' > "$work/files"
plan affected
grep -Fq '== Test the object writer' "$work/selected.tests" || fail 'object consumer lost'
grep -Fq "== Test the direct path's executables [8 CPUs]" "$work/selected.tests" || fail 'execution consumer lost'
pass
printf 'docs/new.md\nnew/shared.h\n' > "$work/files"
plan affected
sed -n '/^== /p' "$work/selected.tests" > "$work/headings"
cmp "$work/all-headings" "$work/headings" || fail 'documentation exemption overrode shared dependency'
pass
for profile in frontend checker object link diagnostics; do
    plan "$profile"
    grep -Fq '== Test the early driver feedback corpus' "$work/selected.tests" || fail "$profile lacks smoke"
    pass
done
for bad in ../escape /absolute $'new\tfile' $'new\rfile'; do
    printf '%s\n' "$bad" > "$work/files"
    if plan affected 2>/dev/null; then fail 'invalid path accepted'; fi
    test ! -s "$work/selected.tests" || fail 'failed plan published partial commands'
    pass
done
rm "$work/files"
if plan affected 2>/dev/null; then fail 'missing change file accepted'; fi
test ! -s "$work/selected.tests" || fail 'unreadable plan published commands'
pass
if plan nonexistent 2>/dev/null; then fail 'unknown profile accepted'; fi
pass
# A renamed/deleted mandatory heading must fail before publishing any commands, under the detectors too.
sed 's/^== Test the early driver feedback corpus$/== Unreviewed replacement/' "$inventory" > "$work/broken.tests"
for executable in "$work/feedback" "$work/feedback-sanitized"; do
    if "$executable" --profile full --inventory "$work/broken.tests" --report "$work/plan.md" > "$work/selected.tests" 2>/dev/null; then fail 'missing mandatory corpus accepted'; fi
    test ! -s "$work/selected.tests" || fail 'broken inventory published executable partial plan'
    "$executable" --profile full --inventory "$inventory" --report "$work/plan.md" > "$work/selected.tests"
    pass
done
cat "$inventory" > "$work/duplicate.tests"
printf '\n== Duplicate shared heading\ntrue\n== Duplicate shared heading\ntrue\n' >> "$work/duplicate.tests"
if "$work/feedback" --profile full --inventory "$work/duplicate.tests" --report "$work/plan.md" > "$work/selected.tests" 2>/dev/null; then fail 'duplicate inventory title accepted'; fi
test ! -s "$work/selected.tests" || fail 'duplicate title published a partial plan'
pass
cat > "$work/compiler" <<'COMPILER'
#!/usr/bin/env bash
set -euo pipefail
[[ "$1" == parallel ]]
cat > /dev/null
case "$FIXTURE_RESULT" in
    passed) echo 'parallel: 4 commands in 0.1 s, 0.1 s of CPU, at most 1 CPUs at once: 4 passed, 0 failed, 0 skipped, 0 not started' ;;
    product) echo 'feedback-case: diagnostic, seed 133'; echo 'parallel: 4 commands in 0.1 s, 0.1 s of CPU, at most 1 CPUs at once: 0 passed, 1 failed, 0 skipped, 3 not started, stopped at the first failure (0 running commands stopped)'; exit 1 ;;
    incomplete) echo 'parallel: 4 commands in 0.1 s, 0.1 s of CPU, at most 1 CPUs at once: 1 passed, 0 failed, 0 skipped, 3 not started'; exit 130 ;;
    skip) echo 'parallel: 4 commands in 0.1 s, 0.1 s of CPU, at most 1 CPUs at once: 0 passed, 0 failed, 4 skipped, 0 not started' ;;
    no-summary) echo 'interrupted before a summary'; exit 1 ;;
    timeout) echo '::error title=driver corpus::failed (timed out)'; echo 'parallel: 4 commands in 1.0 s, 0.1 s of CPU, at most 1 CPUs at once: 0 passed, 1 failed, 0 skipped, 3 not started'; exit 1 ;;
    start-error) echo "::error title=driver corpus::failed (can't start bash: unavailable)"; exit 1 ;;
esac
COMPILER
chmod +x "$work/compiler"
for state in passed product incomplete skip no-summary timeout start-error; do
    status=0
    FIXTURE_RESULT=$state BUSTER_HOSTED_OFFLINE= BUSTER_FEEDBACK_HOSTED_TOOLS= BUSTER_PRODUCT= \
        BUSTER_HOST_ARCH=x86-64 BUSTER_SANITIZER=none BUSTER_SANITIZER_OPTIMIZATION=3 \
        BUSTER_COMPILER="$work/compiler" bash tools/ci/feedback.sh run "$work/$state" smoke --jobs 1 --timeout 120 > "$work/record.log" 2>&1 || status=$?
    case "$state" in
        passed) test "$status" = 0; expected=passed ;;
        product) test "$status" != 0; expected=product-failure ;;
        start-error) test "$status" != 0; expected=infrastructure-failure ;;
        *) test "$status" != 0; expected=incomplete ;;
    esac
    grep -Fq $'result\t'"$expected" "$work/$state/result.tsv" || { cat "$work/record.log"; fail "$state misclassified"; }
    bash -n "$work/$state/reproduce.sh" "$work/$state/command.sh" "$work/$state/environment.sh"
    grep -Fq -- '--timeout 120' "$work/$state/command.sh" || fail 'timeout missing from exact command'
    test -s "$work/$state/cpu.txt" && test -s "$work/$state/host-path" && test -s "$work/$state/output.log"
    pass
done
status=0
BUSTER_HOSTED_OFFLINE= BUSTER_FEEDBACK_HOSTED_TOOLS= BUSTER_PRODUCT= BUSTER_COMPILER=/missing/compiler BUSTER_HOST_ARCH=x86-64 \
    bash tools/ci/feedback.sh run "$work/infra" smoke --jobs 1 > /dev/null 2>&1 || status=$?
test "$status" != 0 && grep -Fq $'result\tinfrastructure-failure' "$work/infra/result.tsv" || fail 'missing executable not infrastructure'
pass
# Workflow boundaries: full remains the qualifying caller's explicit profile, and partial feedback has its own name,
# hosted preparation and run-id group. The qualification/cancellation policies have their own mutation fixtures.
grep -Fq '      feedback-profile: full' .github/workflows/9700x.yml || fail 'qualification not full'
grep -Fq 'if [ "$GITHUB_EVENT_NAME" != workflow_dispatch ] && [ "$FEEDBACK_PROFILE" != full ]; then' .github/workflows/hosted-correctness.yml || fail 'partial profile allowed on qualification event'
grep -Fq "        default: full" .github/workflows/hosted-correctness.yml || fail 'row default weakened'
grep -Fq '      - name: Early hosted feedback (not qualification)' .github/workflows/hosted-correctness.yml || fail 'no early result'
grep -Fq '  group: hosted-reproduction-${{ github.run_id }}' .github/workflows/reproduce.yml || fail 'reproductions cancel each other'
pass
# Execute the emitted replay command from a clean checkout with offline stand-ins at the acquisition boundary.
# This checks environment handoffs, rebuilding, pinned fixture setup, exact arguments and host-path verification.
checkout="$work/replay-checkout"
mkdir -p "$checkout/tools/ci" "$checkout/src/buster/compiler"
cp tools/ci/feedback.sh "$checkout/tools/ci/"
cp "$work/compiler" "$checkout/compiler"
cat > "$checkout/build.sh" <<'BUILD'
#!/usr/bin/env bash
set -euo pipefail
[[ "$1" == print-host-flags ]]
printf -- '-march=x86-64\0'
BUILD
chmod +x "$checkout/build.sh"
echo '#define BUSTER_HOST_VECTOR 0' > "$checkout/src/buster/compiler/host_isa.h"
for script in hosted-release hosted-offline setup-hosted setup-baseline-runtime fetch-corpus fetch-c-testsuite fetch-sqlite; do
    printf '#!/usr/bin/env bash\nset -euo pipefail\nprintf "%%s\\n" "%s $*" >> "$FEEDBACK_REPLAY_TRACE"\n' "$script" > "$checkout/tools/ci/$script.sh"
done
cat > "$checkout/tools/ci/hosted-product.sh" <<'BUILD'
#!/usr/bin/env bash
set -euo pipefail
[[ "$1" == build ]]
echo build >> "$FEEDBACK_REPLAY_TRACE"
printf 'BUSTER_COMPILER=%s/compiler\n' "$PWD" >> "$GITHUB_ENV"
BUILD
mkdir "$checkout/bin"
cat > "$checkout/bin/gh" <<'DOWNLOAD'
#!/usr/bin/env bash
set -euo pipefail
[[ "$1 $2 $3" == 'run download 7' ]]
printf 'download %s\n' "$*" >> "$FEEDBACK_REPLAY_TRACE"
if [ "${FIXTURE_DOWNLOAD:-success}" = fail ]; then exit 1; fi
directory=${*: -1}
mkdir -p "$directory/apt"
printf 'fixture apt bytes\n' > "$directory/apt/MANIFEST"
DOWNLOAD
chmod +x "$checkout/bin/gh"
git -C "$checkout" -c init.defaultBranch=main init -q --template=
git -C "$checkout" add .
git -C "$checkout" -c user.name=Fixture -c user.email=fixture@example.invalid -c core.hooksPath=/dev/null commit -qm fixture
git -C "$checkout" rev-parse HEAD > "$work/passed/revision"
(
    cd "$checkout"
    export FEEDBACK_REPLAY_TRACE="$work/replay-trace" FIXTURE_RESULT=passed
    bash "$work/passed/reproduce.sh" > "$work/replay.log" 2>&1
    grep -Fq 'hosted-offline verify' "$FEEDBACK_REPLAY_TRACE"
    grep -xq build "$FEEDBACK_REPLAY_TRACE"
    export PATH="$checkout/bin:$PATH"
    printf 'buster14a/buster\t7\thosted-inputs-7-1\n' > "$work/passed/hosted-inputs.tsv"
    printf 'fixture apt bytes\n' > "$work/passed/apt-manifest"
    : > "$FEEDBACK_REPLAY_TRACE"
    bash "$work/passed/reproduce.sh" > "$work/replay-download.log" 2>&1
    grep -Fq -- '--name hosted-inputs-7-1-offline' "$FEEDBACK_REPLAY_TRACE"
    if grep -Fq 'hosted-offline prepare' "$FEEDBACK_REPLAY_TRACE"; then fail 'replay resolved packages despite its original handoff'; fi
    if FIXTURE_DOWNLOAD=fail bash "$work/passed/reproduce.sh" > "$work/replay-missing.log" 2>&1; then fail 'replay accepted changed apt inputs after an expired handoff'; fi
    grep -Fq 'apt inputs differ' "$work/replay-missing.log"
    rm "$work/passed/apt-manifest"
    cp "$work/passed/host-path" "$work/host-path"
    echo '#define BUSTER_HOST_VECTOR 1' > "$work/passed/host-path"
    if bash "$work/passed/reproduce.sh" > /dev/null 2>&1; then fail 'replay accepted a different host path'; fi
    cp "$work/host-path" "$work/passed/host-path"
    echo 0000000000000000000000000000000000000000 > "$work/passed/revision"
    if bash "$work/passed/reproduce.sh" > /dev/null 2>&1; then fail 'replay accepted a different revision'; fi
)
pass
if [ "${1:-}" = --compiler ]; then
    real=$(realpath "${2:?real compiler}")
    BUSTER_COMPILER="$real" bash tools/ci/feedback.sh smoke > "$work/real.log" 2>&1
    pass
    cat > "$work/broken-compiler" <<'MUTANT'
#!/usr/bin/env bash
set -euo pipefail
case "$FIXTURE_FAULT:$*" in
    preprocessing:-E*|parsing:parse*|checking:-fsyntax-only*driver.c|objects:*' -c '*) exit 1 ;;
    diagnostics:-fsyntax-only*invalid.c) exit 0 ;;
    execution:*helper.c*|linking:*helper.c*)
        output=
        while [ "$#" -gt 0 ]; do
            if [ "$1" = -o ]; then output=$2; break; fi
            shift
        done
        if [ "$FIXTURE_FAULT" = execution ]; then
            printf '#include "driver.h"\nint feedback_sum(struct FeedbackPair value) { return value.x + value.y + 8; }\n'
        else
            printf 'int missing_feedback_sum(void) { return 0; }\n'
        fi | clang -I tests/ci/feedback -x c -c -o "$output" -
        exit 0 ;;
esac
exec "$FEEDBACK_REAL_COMPILER" "$@"
MUTANT
    chmod +x "$work/broken-compiler"
    for fault in preprocessing parsing checking objects linking execution diagnostics; do
        if FIXTURE_FAULT=$fault FEEDBACK_REAL_COMPILER="$real" BUSTER_COMPILER="$work/broken-compiler" BUSTER_FEEDBACK_WORK="$work" \
            bash tools/ci/feedback.sh smoke > "$work/mutant.log" 2>&1; then fail "$fault regression survived the corpus"; fi
        pass
    done
fi
printf 'feedback fixtures: %s checks passed\n' "$checks"
