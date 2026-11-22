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
# The early step runs in every plain drawn item: the driver corpus alone, whose regressions run in the item's own deal.
test "$(grep -c '^== ' "$work/selected.tests")" = 1 || fail 'the early profile must be the driver corpus alone'
grep -q '^bash tools/ci/feedback.sh smoke$' "$work/selected.tests" || fail 'no driver corpus'
grep -Fq '| Test the C lexer | skip | outside the early driver corpus; full qualification retained |' "$work/plan.md" || fail 'early skip has no reason'
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
    grep -Fq '== Test the C lexer' "$work/selected.tests" || fail "$profile lacks an always-run regression"
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
# The draw's probe (BUSTER_VECTOR_PROBE): a qualifying CPU's answers, or a refusing one's.
printf '#!/usr/bin/env bash\nprintf "x86-64-v4\\tyes\\navx512f\\tyes\\n"\n' > "$work/probe"
printf '#!/usr/bin/env bash\nprintf "x86-64-v4\\tyes\\navx512f\\tno\\n"\nexit 1\n' > "$work/probe-refused"
chmod +x "$work/probe" "$work/probe-refused"
export BUSTER_VECTOR_PROBE="$work/probe"
for state in passed product incomplete skip no-summary timeout start-error; do
    status=0
    FIXTURE_RESULT=$state BUSTER_HOSTED_OFFLINE= BUSTER_FEEDBACK_HOSTED_TOOLS= BUSTER_PRODUCT= \
        BUSTER_HOST_ARCH=vector BUSTER_SANITIZER=none BUSTER_SANITIZER_OPTIMIZATION=3 \
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
    test -s "$work/$state/cpu.txt" && test -s "$work/$state/output.log"
    grep -qx $'status\t0' "$work/$state/probe.txt" || fail "the record lacks the probe's answers"
    pass
done
status=0
BUSTER_HOSTED_OFFLINE= BUSTER_FEEDBACK_HOSTED_TOOLS= BUSTER_PRODUCT= BUSTER_COMPILER=/missing/compiler BUSTER_HOST_ARCH=vector \
    bash tools/ci/feedback.sh run "$work/infra" smoke --jobs 1 > /dev/null 2>&1 || status=$?
test "$status" != 0 && grep -Fq $'result\tinfrastructure-failure' "$work/infra/result.tsv" || fail 'missing executable not infrastructure'
pass
# Workflow boundaries: each drawn item's suite is the full inventory, and the early result comes before it, in every plain
# item; the affected plan is advisory. The qualification/cancellation policies have their own mutation fixtures.
row=.github/workflows/hosted-correctness.yml
grep -Fq '          bash tools/ci/feedback.sh run "$RUNNER_TEMP/feedback-suite" full --jobs "$(nproc)" --timeout "$timeout" "${fail_fast[@]}" \' "$row" || fail 'the suite is not the full inventory'
early=$(grep -n '^      - name: Early hosted feedback (not qualification)$' "$row" | cut -d: -f1)
suite=$(grep -n '^      - name: Run correctness tests within the hosted CPU budget$' "$row" | cut -d: -f1)
[ -n "$early" ] && [ -n "$suite" ] && [ "$early" -lt "$suite" ] || fail 'no early result before the suite'
sed -n "$((early + 1))p" "$row" | grep -Fq "if: env.BUSTER_SHARD != '' && env.BUSTER_SANITIZER == 'none' && " || fail "the early result is not every plain item's"
if grep -Eq 'feedback-profile|reproduce\.yml' .github/workflows/*.yml; then fail 'a partial profile reaches a hosted row'; fi
grep -A1 -F '      - name: Publish conservative affected-test groups (advisory)' .github/workflows/9700x.yml | grep -Fq 'continue-on-error: true' || fail 'the advisory plan can fail the selector'
pass
# Execute the emitted replay command from a clean checkout with offline stand-ins at the acquisition boundary.
# This checks environment handoffs, rebuilding, pinned fixture setup, exact arguments and the CPU's verification.
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
for script in hosted-release hosted-offline setup-hosted fetch-corpus fetch-c-testsuite fetch-sqlite; do
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
    test -s build/predefines.h || fail 'replay wrote no predefines'
    if BUSTER_VECTOR_PROBE="$work/probe-refused" bash "$work/passed/reproduce.sh" > "$work/replay-refused.log" 2>&1; then fail 'replay accepted a CPU the probe refuses'; fi
    grep -Fq 'lacks a feature' "$work/replay-refused.log"
    cp "$work/passed/environment.sh" "$work/environment.sh"
    sed -i 's/^export BUSTER_HOST_ARCH=vector$/export BUSTER_HOST_ARCH=native/' "$work/passed/environment.sh"
    sed -i 's/^Model name:.*/Model name: Some other CPU/' "$work/passed/cpu.txt"
    if bash "$work/passed/reproduce.sh" > "$work/replay-native.log" 2>&1; then fail 'native replay accepted another CPU model'; fi
    grep -Fq 'recorded CPU model' "$work/replay-native.log"
    cp "$work/environment.sh" "$work/passed/environment.sh"
    echo 0000000000000000000000000000000000000000 > "$work/passed/revision"
    if bash "$work/passed/reproduce.sh" > /dev/null 2>&1; then fail 'replay accepted a different revision'; fi
)
pass
if [ "${1:-}" = --compiler ]; then
    real=$(realpath "${2:?real compiler}")
    # A partial reproduction keeps the full row's memory/cost table. Unselected costs are ignored, no test is lost.
    printf '== Test the link representation [8 CPUs]\ntrue\n== Test deterministic diagnostic ordering\ntrue\n' > "$work/partial.tests"
    "$real" parallel --jobs 1 --shard 1/1 --costs tools/ci/costs/vector-memory-O3.costs < "$work/partial.tests" > "$work/partial.log"
    grep -q '2 passed, 0 failed, 0 skipped, 0 not started' "$work/partial.log" || fail 'partial profile lost a heading under full-row costs'
    pass
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
