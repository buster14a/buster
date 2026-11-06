#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# Network-free checks of the narrowly pinned broken-base compatibility repair.
set -euo pipefail
root=$(pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail() { printf 'tests/ci/checker-baseline.sh: %s\n' "$1" >&2; exit 1; }
broken=8b1e14b089f462d09e5cfb9a0c44a9806f86422f
real_git=$(command -v git)
mkdir -p "$work/bin" "$work/base/src/buster/compiler"
cat > "$work/base/src/buster/compiler/c_check_input.c" <<'SOURCE'
    bool weak = false;
    for (uint32_t i = 0; !weak && i < count; i += 1)
    {
        weak = pragmas->records[i].replacement == 0 && pragmas->records[i].name == name && pragmas->records[i].token <= use;
    }
    return weak;
}
SOURCE
"$real_git" -C "$work/base" init -q
"$real_git" -C "$work/base" add .
"$real_git" -C "$work/base" -c user.name=test -c user.email=test@example.invalid commit -qm fixture
actual=$("$real_git" -C "$work/base" rev-parse --verify HEAD)
# The tiny fixture cannot have the historical commit's SHA. Stub only the identity read to exercise the guarded
# branch; all cleanliness, patch-application and diff checks still run against a real temporary git repository.
cat > "$work/bin/git" <<'SHIM'
#!/usr/bin/env bash
if [[ ${CHECKER_TEST_FAKE_SHA:-} != '' && $# == 5 && $1 == -C && $3 == rev-parse && $4 == --verify && $5 == HEAD ]]; then
    printf '%s\n' "$CHECKER_TEST_FAKE_SHA"
else
    exec "$CHECKER_TEST_REAL_GIT" "$@"
fi
SHIM
chmod +x "$work/bin/git"
export CHECKER_TEST_REAL_GIT="$real_git"
export PATH="$work/bin:$PATH"
script="$root/tools/ci/checker-baseline.sh"
export CHECKER_TEST_FAKE_SHA="$broken"
bash "$script" "$work/base" "$broken" > "$work/log" || fail 'the exact pinned fixture was not repaired'
grep -q 'comparison uses this modified base' "$work/log" || fail 'the repaired comparison was not disclosed'
grep -q '^+        const CheckInputSymbolPragma\* record' "$work/log" || fail 'the applied diff was not shown'
"$real_git" -C "$work/base" apply --reverse --check "$root/tools/ci/checker-baseline-8b1e14b.patch" || fail 'the patch was not applied exactly'
if bash "$script" "$work/base" "$broken" > /dev/null 2>&1; then fail 'a dirty or already repaired tree was accepted'; fi
"$real_git" -C "$work/base" reset --hard -q HEAD
printf 'unrelated change\n' >> "$work/base/src/buster/compiler/c_check_input.c"
if bash "$script" "$work/base" "$broken" > /dev/null 2>&1; then fail 'a dirty preimage was accepted'; fi
"$real_git" -C "$work/base" reset --hard -q HEAD
# The pinned patch itself is verified before any mutation.
mkdir -p "$work/copy/tools/ci"
cp "$script" "$work/copy/tools/ci/checker-baseline.sh"
cp "$root/tools/ci/checker-baseline-8b1e14b.patch" "$work/copy/tools/ci/"
printf '# tampered\n' >> "$work/copy/tools/ci/checker-baseline-8b1e14b.patch"
if bash "$work/copy/tools/ci/checker-baseline.sh" "$work/base" "$broken" > /dev/null 2>&1; then fail 'a modified patch was accepted'; fi
"$real_git" -C "$work/base" diff --quiet HEAD -- || fail 'a modified patch changed the tree'
# A committed, clean but different preimage must fail its context check and remain unchanged.
sed -i 's/replacement == 0/replacement != 0/' "$work/base/src/buster/compiler/c_check_input.c"
"$real_git" -C "$work/base" add .
"$real_git" -C "$work/base" -c user.name=test -c user.email=test@example.invalid commit -qm changed-context
if bash "$script" "$work/base" "$broken" > /dev/null 2>&1; then fail 'a changed patch context was accepted'; fi
"$real_git" -C "$work/base" diff --quiet HEAD -- || fail 'a failed context check changed the tree'
unset CHECKER_TEST_FAKE_SHA
actual=$("$real_git" -C "$work/base" rev-parse --verify HEAD)
bash "$script" "$work/base" "$actual" > "$work/log" || fail 'an ordinary base was rejected'
grep -q 'unmodified; build and comparison remain mandatory' "$work/log" || fail 'ordinary baseline status was not shown'
"$real_git" -C "$work/base" diff --quiet HEAD -- || fail 'an unrelated SHA was modified'
if bash "$script" "$work/base" "$broken" > /dev/null 2>&1; then fail 'a SHA/worktree mismatch was accepted'; fi
# A build failure at an unrecognized base must propagate through the workflow's `set -e`, rather than be skipped.
printf '#!/usr/bin/env bash\nexit 47\n' > "$work/base/build.sh"
"$real_git" -C "$work/base" add build.sh
"$real_git" -C "$work/base" -c user.name=test -c user.email=test@example.invalid commit -qm broken-build
actual=$("$real_git" -C "$work/base" rev-parse --verify HEAD)
set +e
bash -c 'set -e; bash "$1" "$2" "$3" > /dev/null; bash "$2/build.sh"' _ "$script" "$work/base" "$actual"
status=$?
set -e
[ "$status" = 47 ] || fail 'a later build failure was suppressed'
printf 'checker baseline: pinned repair, clean/context guards, unrelated SHA and failure propagation checked\n'
