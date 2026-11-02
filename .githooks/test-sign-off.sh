#!/bin/sh
# Isolated Git integration fixtures for the shell hooks and tools/dco/dco.c,
# which it builds with cc; no compiler build.
set -eu
source=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' 0
trap 'exit 1' HUP INT TERM
# Do not inherit the runner's repository, identity, consent or Git settings.
for variable in $(git rev-parse --local-env-vars); do
    unset "$variable"
done
unset BUSTER_DCO_SIGN_OFF CLAUDE_PROJECT_DIR GIT_CONFIG_PARAMETERS GIT_CONFIG_COUNT GIT_AUTHOR_NAME GIT_AUTHOR_EMAIL GIT_COMMITTER_NAME GIT_COMMITTER_EMAIL
HOME="$temporary/home"
XDG_CONFIG_HOME="$HOME/.config"
GIT_CONFIG_NOSYSTEM=1
GIT_CONFIG_GLOBAL=/dev/null
export HOME XDG_CONFIG_HOME GIT_CONFIG_NOSYSTEM GIT_CONFIG_GLOBAL
mkdir -p "$HOME"
trailer='Signed-off-by: David Gonzalez Martin <davidgmbb@gmail.com>'
dco="$temporary/dco"
cc -std=c17 -O2 -Wall -Wextra -Werror -o "$dco" "$source/tools/dco/dco.c"
number=0

fail()
{
    echo "FAIL: $*" >&2
    exit 1
}

new_repository()
{
    number=$((number + 1))
    mkdir "$temporary/repo-$number"
    cd "$temporary/repo-$number"
    git init -q --template= -b main
    mkdir .git/hooks
    git config user.name 'DCO test agent'
    git config user.email agent@example.invalid
    git config core.commentChar '#'
    cp -R "$source/.githooks" .
}

new_empty_repository()
{
    number=$((number + 1))
    mkdir "$temporary/repo-$number"
    cd "$temporary/repo-$number"
    git init -q --template= -b main
    mkdir .git/hooks
    git config user.name 'DCO test agent'
    git config user.email agent@example.invalid
    git config core.commentChar '#'
}

signed()
{
    git log -1 --format=%B > actual-message
    [ "$(grep -Fxc "$trailer" actual-message || :)" = 1 ] || fail 'expected exactly one maintainer trailer'
    [ "$(sed '/^$/d' actual-message | tail -n 1)" = "$trailer" ] || fail 'maintainer trailer must be last'
    [ "$(git log -1 --format=%ae)" = agent@example.invalid ] || fail 'author identity changed'
}

marker()
{
    cat "$(git rev-parse --git-path buster-dco-sign-off)" 2>/dev/null || :
}

unsigned()
{
    if git log -1 --format=%B | grep -Fqx "$trailer"; then
        fail 'unexpected maintainer certification'
    fi
}

new_repository
sh .githooks/enable-sign-off.sh
[ -z "$(marker)" ] || fail 'startup persisted consent'
[ -z "$(git config --local --get core.hooksPath || :)" ] || fail 'startup enabled unrequested hooks'
git -c core.hooksPath=.githooks commit --allow-empty -qm 'No opt-in'
unsigned
echo 'PASS: unconfigured startup and installed hook do not sign'

git config --file "$temporary/global" buster.dcoSignOff davidgmbb
GIT_CONFIG_GLOBAL="$temporary/global" sh .githooks/enable-sign-off.sh
[ -z "$(git config --local --get core.hooksPath || :)" ] || fail 'global consent installed hooks'
GIT_CONFIG_GLOBAL="$temporary/global" git -c core.hooksPath=.githooks commit --allow-empty -qm 'Global setting is not consent'
unsigned
echo 'PASS: global configuration cannot opt in another checkout'

new_empty_repository
sh "$source/.githooks/enable-sign-off.sh" > setup-output
[ -z "$(marker)" ] || fail 'startup without .githooks persisted consent'
[ -z "$(git config --local --get core.hooksPath || :)" ] || fail 'startup without .githooks installed hooks'
if ! CLAUDE_PROJECT_DIR="$temporary/does-not-exist" sh "$source/.githooks/enable-sign-off.sh"; then
    fail 'no-argument startup failed on a bad project directory'
fi
echo 'PASS: no-argument startup without .githooks or a valid project directory is a no-op'

new_empty_repository
if sh "$source/.githooks/enable-sign-off.sh" --maintainer-session > setup-output 2>&1; then
    fail 'maintainer setup succeeded without .githooks'
fi
[ -z "$(marker)" ] || fail 'failed setup persisted consent'
[ -z "$(git config --local --get core.hooksPath || :)" ] || fail 'failed setup installed hooks'
echo 'PASS: maintainer setup without .githooks fails without recording consent'

new_repository
mkdir nested
(cd nested && sh ../.githooks/enable-sign-off.sh --maintainer-session > ../setup-output 2>&1)
[ "$(marker)" = davidgmbb ] || fail 'consent not persisted'
[ -z "$(git config --get buster.dcoSignOff || :)" ] || fail 'consent went into shared configuration'
[ "$(git config --local --get core.hooksPath)" = .githooks ] || fail 'hook not installed'
grep -q 'every commit made here, by any author, and no other worktree' setup-output || fail 'missing worktree-scope warning'
git commit --allow-empty -qm 'Persistent opt-in' -m 'Co-Authored-By: Test Agent <agent@example.invalid>'
signed
sh .githooks/enable-sign-off.sh
git commit --allow-empty -qm 'Already signed' -m "$trailer"
signed
git checkout -q -b merge-side
git commit --allow-empty -qm 'Merge side'
git checkout -q main
git commit --allow-empty -qm 'Merge main'
git merge --no-ff -q merge-side -m 'Merge side into main'
signed
echo 'PASS: persistent opt-in, nested setup, merge, unchanged author and idempotent trailers'

for disabled in off '' someone-else; do
    BUSTER_DCO_SIGN_OFF="$disabled" git commit --allow-empty -qm 'Explicit opt-out'
    unsigned
done
echo 'PASS: present environment values override persistent consent'

git config --local buster.dcoSignOff off
sh .githooks/enable-sign-off.sh --maintainer-session > setup-output 2>&1
[ "$(git config --local --get buster.dcoSignOff)" = off ] || fail 'session overrode revocation'
grep -q 'leaving it disabled' setup-output || fail 'missing revocation diagnostic'
git commit --allow-empty -qm 'Persistent opt-out'
unsigned
BUSTER_DCO_SIGN_OFF=davidgmbb git commit --allow-empty -qm 'Explicit temporary opt-in'
signed
[ "$(git config --local --get buster.dcoSignOff)" = off ] || fail 'temporary opt-in changed persistent setting'
git config --local buster.dcoSignOff someone-else
sh .githooks/enable-sign-off.sh --maintainer-session > setup-output 2>&1
[ "$(git config --local --get buster.dcoSignOff)" = someone-else ] || fail 'session overrode a local non-consent'
grep -q 'leaving it disabled' setup-output || fail 'missing local non-consent diagnostic'
git commit --allow-empty -qm 'Local non-consent'
unsigned
echo 'PASS: persistent revocation and local non-consent survive startup; environment override is temporary'

for disabled in off '' someone-else; do
    new_repository
    BUSTER_DCO_SIGN_OFF="$disabled" sh .githooks/enable-sign-off.sh --maintainer-session > setup-output 2>&1
    [ -z "$(marker)" ] || fail "disabled session persisted consent ($disabled)"
    [ -z "$(git config --local --get core.hooksPath || :)" ] || fail "disabled session installed hooks ($disabled)"
    grep -q 'leaving it disabled' setup-output || fail "missing disabled diagnostic ($disabled)"
done
echo 'PASS: explicit opt-out blocks initial setup'

new_repository
BUSTER_DCO_SIGN_OFF=davidgmbb sh .githooks/enable-sign-off.sh
[ -z "$(marker)" ] || fail 'legacy environment consent became persistent'
BUSTER_DCO_SIGN_OFF=davidgmbb git commit --allow-empty -qm 'Legacy opt-in'
signed
git commit --allow-empty -qm 'Legacy opt-in removed'
unsigned
echo 'PASS: legacy environment-only setup retains its revocation behavior'

new_repository
sh .githooks/enable-sign-off.sh --maintainer-session > /dev/null
for state in rebase-merge rebase-apply CHERRY_PICK_HEAD; do
    state_path=$(git rev-parse --git-path "$state")
    if [ "$state" = CHERRY_PICK_HEAD ]; then
        touch "$state_path"
    else
        mkdir "$state_path"
    fi
    printf 'Copied historical commit\n' > message
    cp message expected
    sh .githooks/commit-msg message
    cmp message expected || fail "signed while $state exists"
    rm -rf "$state_path"
done
git config trailer.signed-off-by.key Wrong-Trailer
git commit --allow-empty -qm 'Trailer configuration is isolated'
signed
before=$(git rev-parse HEAD)
if git -c core.commentChar=auto commit --allow-empty -qm 'Ambiguous comments' 2> auto-error; then
    fail 'automatic comment character was accepted'
fi
grep -q 'BUSTER_DCO_SIGN_OFF=off' auto-error || fail 'automatic-comment error omits the suppression command'
[ "$(git rev-parse HEAD)" = "$before" ] || fail 'failed commit changed HEAD'
echo 'PASS: copy-state guards, trailer isolation and automatic-comment refusal'

new_repository
git config core.hooksPath .custom-hooks
mkdir .custom-hooks
sh .githooks/enable-sign-off.sh --maintainer-session > setup-output
[ "$(git config core.hooksPath)" = .custom-hooks ] || fail 'replaced custom hooks path'
grep -q 'hook is off' setup-output || fail 'missing custom-hook diagnostic'
echo 'PASS: custom hooks path is preserved and reported'

new_repository
printf '#!/bin/sh\nexit 0\n' > .git/hooks/pre-commit
chmod +x .git/hooks/pre-commit
sh .githooks/enable-sign-off.sh --maintainer-session > setup-output
[ -z "$(git config --local --get core.hooksPath || :)" ] || fail 'disabled existing Git hooks'
grep -q 'hook is off' setup-output || fail 'missing existing-hook diagnostic'
echo 'PASS: existing .git/hooks are preserved'

new_repository
ln -s ../missing .git/hooks/pre-commit
sh .githooks/enable-sign-off.sh --maintainer-session > setup-output
[ -z "$(git config --local --get core.hooksPath || :)" ] || fail 'ignored existing hook symlink'
echo 'PASS: hook symlinks are preserved'

new_repository
if sh .githooks/enable-sign-off.sh --unknown; then
    fail 'unknown option accepted'
fi
[ -z "$(marker)" ] || fail 'invalid invocation persisted consent'
echo 'PASS: unknown options fail without recording consent'

# Worktree scope (#1162): consent is each worktree's marker, and Git
# configuration can only opt out.
new_repository
git add .githooks
git commit -qm 'Track hooks for linked worktrees'
main_tree=$PWD
git worktree add -q -b tree-a "$temporary/tree a"
git worktree add -q -b tree-b "$temporary/tree b"
cd "$temporary/tree a"
sh .githooks/enable-sign-off.sh --maintainer-session > "$temporary/setup-output" 2>&1
grep -q 'enabled for this worktree only' "$temporary/setup-output" || fail 'setup omits its worktree scope'
git commit --allow-empty -qm 'Worktree A'
signed
cd "$temporary/tree b"
sh .githooks/enable-sign-off.sh
git commit --allow-empty -qm 'Worktree B'
unsigned
cd "$main_tree"
git commit --allow-empty -qm 'Main worktree'
unsigned
git worktree add -q -b tree-c "$temporary/tree c"
cd "$temporary/tree c"
sh .githooks/enable-sign-off.sh
git commit --allow-empty -qm 'Worktree C, created later'
unsigned
echo 'PASS: enabling A signs A alone; B, the main worktree and a later C stay unsigned'

cd "$temporary/tree b"
sh .githooks/enable-sign-off.sh --maintainer-session > /dev/null
git commit --allow-empty -qm 'B, separately enabled'
signed
cd "$temporary/tree a"
sh .githooks/enable-sign-off.sh --revoke > "$temporary/setup-output"
[ "$(marker)" = off ] || fail 'revocation not recorded'
grep -q 'disabled for this worktree' "$temporary/setup-output" || fail 'revocation omits its scope'
sh .githooks/enable-sign-off.sh
sh .githooks/enable-sign-off.sh --maintainer-session > "$temporary/setup-output"
grep -q 'leaving it disabled' "$temporary/setup-output" || fail 'setup re-enabled a revoked worktree'
[ "$(marker)" = off ] || fail 'startup restored a revoked opt-in'
git commit --allow-empty -qm 'A, revoked'
unsigned
cd "$temporary/tree b"
[ "$(marker)" = davidgmbb ] || fail "revoking A changed B's state"
git commit --allow-empty -qm 'B after A revoked'
signed
echo 'PASS: revoking A leaves B alone, and neither startup nor setup restores A'

cd "$temporary/tree a"
BUSTER_DCO_SIGN_OFF=davidgmbb git commit --allow-empty -qm 'A, environment opt-in'
signed
[ "$(marker)" = off ] || fail 'environment opt-in changed the marker'
cd "$temporary/tree b"
BUSTER_DCO_SIGN_OFF=off git commit --allow-empty -qm 'B, environment opt-out'
unsigned
cd "$main_tree"
git worktree move "$temporary/tree b" "$temporary/moved b"
cd "$temporary/moved b"
git commit --allow-empty -qm 'B, moved'
signed
echo 'PASS: the environment overrides a marker either way, and a moved worktree keeps its own'

cd "$main_tree"
sh .githooks/enable-sign-off.sh --maintainer-session > /dev/null
git commit --allow-empty -qm 'Main, enabled'
signed
[ "$(cat .git/buster-dco-sign-off)" = davidgmbb ] || fail "the main worktree's marker isn't in .git"
cd "$temporary/tree c"
git commit --allow-empty -qm 'C after main enabled'
unsigned
git config --file "$temporary/global-consent" buster.dcoSignOff davidgmbb
GIT_CONFIG_GLOBAL="$temporary/global-consent" git commit --allow-empty -qm 'C, global consent'
unsigned
git config --file "$temporary/global-opt-out" buster.dcoSignOff off
cd "$temporary/moved b"
GIT_CONFIG_GLOBAL="$temporary/global-opt-out" git commit --allow-empty -qm 'B, global opt-out'
unsigned
echo 'PASS: the main worktree has its own marker; global configuration opts out, never in'

cd "$main_tree"
bare=$(git config --get core.bare)
git config extensions.worktreeConfig true
cd "$temporary/tree c"
git config --worktree buster.dcoSignOff davidgmbb
git commit --allow-empty -qm 'C, worktree configuration consent'
unsigned
git config --worktree --unset buster.dcoSignOff
cd "$temporary/moved b"
git config --worktree buster.dcoSignOff off
git commit --allow-empty -qm 'B, worktree configuration opt-out'
unsigned
sh .githooks/enable-sign-off.sh --maintainer-session > "$temporary/setup-output"
grep -q 'worktree Git configuration; leaving it disabled' "$temporary/setup-output" || fail 'setup overrode a worktree configuration opt-out'
"$dco" status > "$temporary/status"
grep -q 'opt-in: off, from buster.dcoSignOff in worktree' "$temporary/status" || fail 'status misses a worktree configuration opt-out'
git config --worktree --unset buster.dcoSignOff
git commit --allow-empty -qm 'B, worktree opt-out removed'
signed
cd "$main_tree"
[ "$(git config --get core.bare)" = "$bare" ] || fail 'core.bare changed'
git commit --allow-empty -qm 'Main with worktree configuration'
signed
echo 'PASS: existing worktree configuration can opt out, never in, and is left as it was'

new_repository
git config --local buster.dcoSignOff davidgmbb
git config --local core.hooksPath .githooks
git config --local custom.setting kept
git commit --allow-empty -qm 'Legacy shared consent'
unsigned
sh .githooks/enable-sign-off.sh > setup-output
grep -q 'no longer enables' setup-output || fail 'startup hides inert legacy consent'
sh .githooks/enable-sign-off.sh --maintainer-session > setup-output
grep -q 'Removed the clone-wide' setup-output || fail 'setup did not report the migration'
[ -z "$(git config --get buster.dcoSignOff || :)" ] || fail 'legacy consent left behind'
[ "$(marker)" = davidgmbb ] || fail 'migration did not enable this worktree'
git config --local --list > config-before
sh .githooks/enable-sign-off.sh --maintainer-session > /dev/null
sh .githooks/enable-sign-off.sh
git config --local --list > config-after
cmp config-before config-after || fail 'setup is not idempotent'
[ "$(git config --get custom.setting)" = kept ] || fail 'setup changed unrelated configuration'
git commit --allow-empty -qm 'Migrated'
signed
echo 'PASS: legacy shared consent is inert until a directed setup migrates it, idempotently'

# Setup status (#1163): read-only, and the same resolver as the hook.
new_repository
git config core.hooksPath .githooks
git config --local --list > config-before
"$dco" status > status
grep -q 'opt-in: off, from nothing' status || fail 'status: no opt-in'
BUSTER_DCO_SIGN_OFF=davidgmbb "$dco" status > status
grep -q 'opt-in: on, from BUSTER_DCO_SIGN_OFF' status || fail 'status: environment opt-in'
grep -q "commit-msg: this worktree's .githooks/commit-msg" status || fail 'status: direct hook'
git config buster.dcoSignOff off
BUSTER_DCO_SIGN_OFF=davidgmbb "$dco" status > status
grep -q 'opt-in: on, from BUSTER_DCO_SIGN_OFF' status || fail 'status: environment over configuration'
"$dco" status > status
grep -q 'opt-in: off, from buster.dcoSignOff in local' status || fail 'status: local opt-out'
git config --unset buster.dcoSignOff
git config --local --list > config-after
cmp config-before config-after || fail 'status changed configuration'
[ -z "$(marker)" ] || fail 'status recorded consent'
chmod -x .githooks/commit-msg
BUSTER_DCO_SIGN_OFF=davidgmbb "$dco" status > status
grep -q 'NOT executable' status || fail 'status: non-executable hook'
chmod +x .githooks/commit-msg
mkdir "$(git rev-parse --git-path rebase-merge)"
"$dco" status > status
grep -q 'rebase or cherry-pick is in progress' status || fail 'status: copy state'
rmdir "$(git rev-parse --git-path rebase-merge)"
git config core.commentChar auto
"$dco" status > status
grep -q 'comment prefix' status || fail 'status: automatic comment prefix'
git config core.commentChar '#'
git config core.hooksPath .custom-hooks
mkdir .custom-hooks
printf '#!/bin/sh\nexit 0\n' > .custom-hooks/commit-msg
chmod +x .custom-hooks/commit-msg
sh .githooks/enable-sign-off.sh --maintainer-session > /dev/null
"$dco" status > status
grep -q 'a hook of its own' status || fail 'status: custom hook'
grep -q "the hook isn't active" status || fail 'status: inactive hook'
echo 'PASS: status reports precedence, opt-outs, hooks, copy state and comment prefixes, and changes nothing'

# Commit-range audit (#1163).
new_empty_repository
git commit --allow-empty -qm 'Before the base' -m 'Unsigned history the range excludes.'
git commit --allow-empty -qm 'Base'
base=$(git rev-parse HEAD)
git checkout -q -b topic
git commit --allow-empty -qm 'Signed' -m "$trailer"
git commit --allow-empty -qm 'Other signer' -m 'Signed-off-by: Other Person <other@example.invalid>'
GIT_AUTHOR_NAME=Agent GIT_AUTHOR_EMAIL=noreply@example.invalid git commit --allow-empty -qm 'Agent author' -m "$trailer"
git checkout -q main
git commit --allow-empty -qm 'Main, unsigned'
git checkout -q topic
git merge -q --no-ff main -m 'Merge main' -m "$trailer"
"$dco" audit --base main --head topic > audit || fail 'audit: a fully signed range fails'
grep -q '^4 commits from' audit || fail 'audit: wrong range'
if grep -q 'Before the base\|Main, unsigned' audit; then
    fail 'audit: commits the base reaches'
fi
grep -q 'signed: Other Person <other@example.invalid>$' audit || fail 'audit: another signer'
grep -q 'note: the author, Agent <noreply@example.invalid>, is not a signer' audit || fail 'audit: agent author'
grep -q '(merge) Merge main' audit || fail 'audit: merge'
grep -q '^result: every commit' audit || fail 'audit: result'
signed_head=$(git rev-parse HEAD)
git commit --allow-empty -qm 'Unsigned'
git commit --allow-empty -qm 'Co-author only' -m 'Co-Authored-By: Agent <agent@example.invalid>'
git commit --allow-empty -qm 'Quoted' -m "> $trailer" -m "$trailer" -m 'A closing paragraph of prose.'
git commit --allow-empty -qm 'Duplicate' -m "$trailer
$trailer"
git commit --allow-empty -qm 'Malformed' -m 'Signed-off-by:David Gonzalez Martin <davidgmbb@gmail.com>
signed-off-by: David Gonzalez Martin <davidgmbb@gmail.com>
Signed-off-by: David Gonzalez Martin davidgmbb@gmail.com'
status=0
"$dco" audit --base main --head HEAD > audit || status=$?
[ "$status" = 1 ] || fail "audit: gaps exit $status"
[ "$(grep -c '^  MISSING' audit)" = 4 ] || fail 'audit: wrong missing count'
grep -q 'a Co-authored-by trailer certifies nothing' audit || fail 'audit: co-author only'
[ "$(grep -c OUTSIDE audit)" = 2 ] || fail 'audit: sign-offs outside the trailer block'
grep -q 'DUPLICATE: 2 copies' audit || fail 'audit: duplicate'
[ "$(grep -c MALFORMED audit)" = 3 ] || fail 'audit: malformed'
grep -q '^summary: 5 signed, 4 missing' audit || fail 'audit: summary'
echo 'PASS: audit reports signers, gaps, duplicates, malformed and outside trailers, and excludes the base'

new_empty_repository
git commit --allow-empty -qm 'Base'
base=$(git rev-parse HEAD)
git commit --allow-empty -qm 'Unsigned first'
first=$(git rev-parse HEAD)
"$dco" audit --base "$base" --head HEAD --certification "$first=https://example.invalid/review/1" > audit || fail 'audit: a current head record'
grep -q 'covered by a supplied record' audit || fail 'audit: coverage'
grep -q 'not verified here' audit || fail 'audit: unverified record label'
git commit --allow-empty -qm 'Unsigned second'
status=0
"$dco" audit --base "$base" --head HEAD --certification "$first=https://example.invalid/review/1" > audit || status=$?
[ "$status" = 1 ] || fail 'audit: a stale record covers a new head'
grep -q 'STALE' audit || fail 'audit: stale record not shown'
[ "$(grep -c '^  MISSING.*covered by a supplied record' audit)" = 1 ] || fail 'audit: stale record covers later commits'
status=0
"$dco" audit --base 0000000000000000000000000000000000000001 --head HEAD > audit || status=$?
[ "$status" = 2 ] && grep -q INCOMPLETE audit || fail 'audit: missing base'
repository=$PWD
git clone -q --depth 1 "file://$repository" "$temporary/shallow"
cd "$temporary/shallow"
status=0
"$dco" audit --base HEAD --head HEAD > audit || status=$?
[ "$status" = 2 ] && grep -q 'shallow' audit || fail 'audit: shallow history'
if "$dco" audit --base HEAD > audit 2>&1; then
    fail 'audit: missing --head accepted'
fi
echo 'PASS: head records cover their head alone, stale ones show, and missing history is incomplete'

new_repository
git config core.hooksPath .githooks
git commit --allow-empty -qm 'Base'
BUSTER_DCO_SIGN_OFF=davidgmbb git commit --allow-empty -qm 'Divider' -m 'Body' -m '---
Signed-off-by: A U Thor <author@example.invalid>'
signed
BUSTER_DCO_SIGN_OFF=davidgmbb git commit --allow-empty -qm 'Comment after the trailers' -m 'Signed-off-by: A U Thor <author@example.invalid>' -m '# note'
git log -1 --format=%B | grep -Fqx "$trailer" || fail 'the hook did not sign before a comment line'
mkdir relative-temporary
TMPDIR=relative-temporary "$dco" audit --base HEAD~2 --head HEAD > audit || fail 'audit: what the hook signed is not a sign-off'
grep -q '^summary: 2 signed, 0 missing' audit || fail 'audit: divider and comment shapes'
echo 'PASS: audit parses trailers as the hook does (a --- line, a trailing comment) and takes a relative TMPDIR'

cd "$temporary"
one=1111111111111111111111111111111111111111
two=2222222222222222222222222222222222222222
three=3333333333333333333333333333333333333333
base=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
api_commit()
{
    printf '{"sha":"%s","parents":[{"sha":"%s"}],"commit":{"author":{"name":"DCO test agent","email":"agent@example.invalid"},"message":"%s"}}' "$1" "$2" "$3"
}
printf '{"head":{"sha":"%s"},"base":{"sha":"%s"},"commits":3}' "$three" "$base" > pull.json
printf '[%s,%s]' "$(api_commit "$one" "$base" "One\\n\\n$trailer")" "$(api_commit "$two" "$one" "Two\\n\\nSigned-off-by: Other Person <other@example.invalid>")" > page-1.json
printf '[%s]' "$(api_commit "$three" "$two" "Three\\n\\nCo-Authored-By: Agent <agent@example.invalid>")" > page-2.json
printf '[%s,%s]' "$(cat page-1.json)" "$(cat page-2.json)" > slurped.json
status=0
"$dco" audit --api-pull pull.json --api-commits page-1.json --api-commits page-2.json > audit || status=$?
[ "$status" = 1 ] || fail "API audit exit $status"
grep -q '^3 commits from GitHub' audit && grep -q "^- 333333333333 Three" audit && [ "$(grep -c '^  MISSING' audit)" = 1 ] || fail 'API audit: pages'
"$dco" audit --api-pull pull.json --api-commits slurped.json --head "$three" > audit-slurped || :
cmp audit audit-slurped || fail 'API audit: slurped pages differ'
for incomplete in short stale moved; do
    case $incomplete in
        short) arguments="--api-pull pull.json --api-commits page-1.json" ;;
        stale) arguments="--api-pull pull.json --api-commits slurped.json --head $two" ;;
        moved) printf '{"head":{"sha":"%s"},"base":{"sha":"%s"},"commits":3}' "$base" "$base" > moved.json
               arguments="--api-pull moved.json --api-commits slurped.json" ;;
    esac
    status=0
    "$dco" audit $arguments > audit || status=$?
    [ "$status" = 2 ] && grep -q INCOMPLETE audit || fail "API audit: $incomplete listing passed"
done
printf '{"head":{"sha":"%s"},"base":{"sha":"%s"},"commits":300}' "$three" "$base" > large.json
status=0
"$dco" audit --api-pull large.json --api-commits slurped.json > audit || status=$?
[ "$status" = 2 ] && grep -q 'at most 250' audit || fail 'API audit: the 250-commit cap'
echo 'PASS: API audits read every page, and short, stale or capped listings are incomplete'

# Composition with existing hooks (#1164).
new_repository
printf '#!/bin/sh\necho ran >> "$(git rev-parse --git-dir)/pre-commit-ran"\n' > .git/hooks/pre-commit
chmod +x .git/hooks/pre-commit
sh .githooks/enable-sign-off.sh --maintainer-session > setup-output
grep -q 'dco compose' setup-output || fail 'setup does not offer composition'
"$dco" compose > compose-output
grep -q 'installed' compose-output || fail 'compose: default hooks'
"$dco" compose > compose-output
grep -q 'already composed' compose-output || fail 'compose is not idempotent'
sh .githooks/enable-sign-off.sh > setup-output
if grep -q 'hook is off' setup-output; then
    fail 'startup does not see the adapter'
fi
git commit --allow-empty -qm 'Composed default hooks'
signed
[ -s .git/pre-commit-ran ] || fail 'compose: pre-commit did not run'
printf '#!/bin/sh\nexit 1\n' > .git/hooks/pre-commit
before=$(git rev-parse HEAD)
if git commit --allow-empty -qm 'Rejected by pre-commit' 2> /dev/null; then
    fail 'compose: a rejecting pre-commit passed'
fi
[ "$(git rev-parse HEAD)" = "$before" ] || fail 'compose: rejected commit changed HEAD'
printf '#!/bin/sh\nexit 0\n' > .git/hooks/pre-commit
BUSTER_DCO_SIGN_OFF=off git commit --allow-empty -qm 'Composed, opted out'
unsigned
git checkout -q -b composed-side
git commit --allow-empty -qm 'Side'
git checkout -q main
git commit --allow-empty -qm 'Main'
git merge --no-ff -q composed-side -m 'Merge composed side'
signed
"$dco" uncompose > compose-output
[ ! -e .git/hooks/commit-msg ] || fail 'uncompose left the adapter'
[ -x .git/hooks/pre-commit ] || fail 'uncompose removed pre-commit'
echo 'PASS: an existing pre-commit composes, rejects, opts out, merges and uncomposes'

new_repository
sh .githooks/enable-sign-off.sh --maintainer-session > /dev/null 2>&1
git config --unset core.hooksPath
cat > .git/hooks/commit-msg << 'HOOK'
#!/bin/sh
if grep -q WIP "$1"; then
    exit 3
fi
grep -q '^Ticket: ' "$1" || git interpret-trailers --in-place --trailer 'Ticket: T-1' "$1"
HOOK
chmod +x .git/hooks/commit-msg
cp .git/hooks/commit-msg original-commit-msg
"$dco" compose > compose-output
grep -q 'keeping the existing commit-msg' compose-output || fail 'compose: existing commit-msg'
git commit --allow-empty -qm 'Ticketed'
signed
git log -1 --format=%B | grep -q '^Ticket: T-1$' || fail 'compose: the kept hook did not edit the message'
BUSTER_DCO_SIGN_OFF=off git commit --allow-empty -qm 'Ticketed, opted out'
unsigned
git log -1 --format=%B | grep -q '^Ticket: T-1$' || fail 'compose: an opt-out stopped the kept hook'
before=$(git rev-parse HEAD)
status=0
git commit --allow-empty -qm 'WIP' 2> /dev/null || status=$?
[ "$status" != 0 ] && [ "$(git rev-parse HEAD)" = "$before" ] || fail "compose: rejecting commit-msg exit $status"
printf '#!/bin/sh\nif grep -q "^%s$" "$1"; then\n    exit 1\nfi\n' "$trailer" > .git/hooks/commit-msg.buster-dco-chained
if git commit --allow-empty -qm 'Rejects the signed message' 2> commit-error; then
    fail 'compose: the kept hook did not see the signed message'
fi
grep -q 'rejects the message once' commit-error || fail 'compose: rejection of the signed message unexplained'
printf '#!/bin/sh\necho Checked. >> "$1"\n' > .git/hooks/commit-msg.buster-dco-chained
if git commit --allow-empty -qm 'Unstable hook' 2> commit-error; then
    fail 'compose: an unsettled message passed'
fi
grep -q 'changes the message again' commit-error || fail 'compose: an unsettled message unexplained'
[ "$(git rev-parse HEAD)" = "$before" ] || fail 'compose: refused commits changed HEAD'
cp original-commit-msg .git/hooks/commit-msg.buster-dco-chained
echo '# edited by its owner' >> .git/hooks/commit-msg.buster-dco-chained
"$dco" uncompose > compose-output
grep -q 'edited by its owner' .git/hooks/commit-msg || fail 'uncompose lost the owner edit'
[ ! -e .git/hooks/commit-msg.buster-dco-chained ] || fail 'uncompose left the kept hook'
echo 'PASS: an existing commit-msg runs first, may edit or reject, checks the signed message, and comes back edited'

new_repository
sh .githooks/enable-sign-off.sh --maintainer-session > /dev/null 2>&1
git config --unset core.hooksPath
mkdir "$temporary/shared hooks"
printf '#!/bin/sh\nexit 0\n' > "$temporary/shared hooks/target"
chmod +x "$temporary/shared hooks/target"
cp "$temporary/shared hooks/target" target-copy
ln -s "$temporary/shared hooks/target" .git/hooks/commit-msg
"$dco" compose > /dev/null
[ -L .git/hooks/commit-msg.buster-dco-chained ] || fail 'compose replaced a hook symlink'
git commit --allow-empty -qm 'Symlinked hook'
signed
cmp "$temporary/shared hooks/target" target-copy || fail 'compose changed a symlink target'
echo '# local' >> .git/hooks/commit-msg
status=0
"$dco" uncompose > compose-output || status=$?
[ "$status" = 1 ] && grep -q 'changed since compose; left alone' compose-output || fail 'uncompose removed an edited adapter'
sed -i '$d' .git/hooks/commit-msg
"$dco" uncompose > /dev/null
[ -L .git/hooks/commit-msg ] || fail 'uncompose did not restore the symlink'
mv .git/hooks "$temporary/elsewhere hooks"
ln -s "$temporary/elsewhere hooks" .git/hooks
status=0
"$dco" compose > compose-output || status=$?
[ "$status" = 1 ] && grep -q 'unsupported' compose-output || fail 'compose: a linked hooks directory'
[ -L "$temporary/elsewhere hooks/commit-msg" ] && [ ! -e "$temporary/elsewhere hooks/commit-msg.buster-dco-chained" ] || fail 'unsupported compose changed something'
echo 'PASS: symlinked hooks are kept as links, edited adapters stay, and a linked directory is unsupported'

new_repository
mkdir .custom-hooks
printf '#!/bin/sh\necho ran >> "$(git rev-parse --git-dir)/pre-commit-ran"\n' > .custom-hooks/pre-commit
printf '#!/bin/sh\ngrep -q "^Ticket: " "$1" || git interpret-trailers --in-place --trailer "Ticket: T-2" "$1"\n' > .custom-hooks/commit-msg
chmod +x .custom-hooks/pre-commit .custom-hooks/commit-msg
git config core.hooksPath .custom-hooks
sh .githooks/enable-sign-off.sh --maintainer-session > /dev/null
ls -l .custom-hooks > listing-before
"$dco" compose > compose-output
grep -q 'now names' compose-output || fail 'compose: relative hooks directory'
dispatcher="$(git rev-parse --path-format=absolute --git-common-dir)/buster-dco-hooks"
[ "$(git config core.hooksPath)" = "$dispatcher" ] || fail 'compose: core.hooksPath'
git commit --allow-empty -qm 'Relative custom hooks'
signed
git log -1 --format=%B | grep -q '^Ticket: T-2$' || fail 'compose: relative commit-msg'
[ -s .git/pre-commit-ran ] || fail 'compose: relative pre-commit'
printf '#!/bin/sh\necho ran >> "$(git rev-parse --git-dir)/post-commit-ran"\n' > .custom-hooks/post-commit
chmod +x .custom-hooks/post-commit
ls -l .custom-hooks > listing-before
"$dco" compose > compose-output
grep -q 'linked any hooks added since' compose-output || fail 'compose: new hooks'
git commit --allow-empty -qm 'New hook'
[ -s .git/post-commit-ran ] || fail 'compose: a hook added later'
ls -l .custom-hooks > listing-after
cmp listing-before listing-after || fail 'compose changed the custom hooks directory'
"$dco" uncompose > /dev/null
[ "$(git config core.hooksPath)" = .custom-hooks ] && [ ! -e "$dispatcher" ] || fail 'uncompose: relative hooks directory'
echo 'PASS: a relative custom hooks directory composes without touching its files, and uncomposes'

new_repository
mkdir "$temporary/hook dir"
printf '#!/bin/sh\necho ran >> "$(git rev-parse --git-dir)/pre-commit-ran"\n' > "$temporary/hook dir/pre-commit"
chmod +x "$temporary/hook dir/pre-commit"
git config --file "$temporary/global-hooks" core.hooksPath "$temporary/hook dir"
GIT_CONFIG_GLOBAL="$temporary/global-hooks"
export GIT_CONFIG_GLOBAL
sh .githooks/enable-sign-off.sh --maintainer-session > /dev/null
"$dco" compose > /dev/null
git commit --allow-empty -qm 'Absolute global hooks path with spaces'
signed
[ -s .git/pre-commit-ran ] || fail 'compose: absolute pre-commit'
"$dco" uncompose > /dev/null
[ -z "$(git config --local --get core.hooksPath || :)" ] || fail 'uncompose left a local core.hooksPath'
[ "$(git config core.hooksPath)" = "$temporary/hook dir" ] || fail 'uncompose changed global configuration'
"$dco" compose > /dev/null
git config --local core.hooksPath "$temporary/hook dir"
"$dco" uncompose > compose-output
grep -q 'no longer names' compose-output || fail "uncompose: a user's later core.hooksPath"
[ "$(git config --local core.hooksPath)" = "$temporary/hook dir" ] || fail "uncompose overwrote a user's core.hooksPath"
GIT_CONFIG_GLOBAL=/dev/null
echo 'PASS: an absolute, global hooks path with spaces composes locally and uncomposes, keeping later edits'

new_repository
git add .githooks
git commit -qm 'Track hooks'
git config extensions.worktreeConfig true
git worktree add -q -b scoped "$temporary/scoped tree"
cd "$temporary/scoped tree"
git config --worktree core.hooksPath "$temporary/hook dir"
sh .githooks/enable-sign-off.sh --maintainer-session > /dev/null
"$dco" compose > compose-output
grep -q 'core.hooksPath (worktree)' compose-output || fail 'compose: worktree scope'
git commit --allow-empty -qm 'Worktree-scoped hooks'
signed
cd "$temporary/repo-$number"
[ -z "$(git config core.hooksPath || :)" ] || fail 'compose changed another worktree'
cd "$temporary/scoped tree"
"$dco" uncompose > /dev/null
[ "$(git config --worktree core.hooksPath)" = "$temporary/hook dir" ] || fail 'uncompose: worktree scope'
echo 'PASS: a worktree-scoped hooks path composes in that worktree alone'

new_repository
sh .githooks/enable-sign-off.sh --maintainer-session > /dev/null 2>&1
git config --unset core.hooksPath
printf '#!/bin/sh\nexit 0\n' > .git/hooks/commit-msg
chmod +x .git/hooks/commit-msg
cp .git/hooks/commit-msg kept-copy
# Root ignores the directory's mode, so only a non-root run can make the install fail.
if [ "$(id -u)" != 0 ]
then
    chmod a-w .git/hooks
    status=0
    "$dco" compose > compose-output 2>&1 || status=$?
    chmod u+w .git/hooks
    [ "$status" = 1 ] && grep -q 'nothing changed' compose-output || fail 'compose: a failed install'
    cmp .git/hooks/commit-msg kept-copy && [ ! -e .git/hooks/commit-msg.buster-dco-chained ] || fail 'a failed compose moved the existing commit-msg'
fi
echo leftover > .git/hooks/commit-msg.buster-dco-new
"$dco" compose > /dev/null || fail 'compose: a leftover temporary file'
[ ! -e .git/hooks/commit-msg.buster-dco-new ] && cmp .git/hooks/commit-msg.buster-dco-chained kept-copy || fail 'compose: the retry'
echo 'PASS: a failed compose leaves the existing commit-msg in place, and a retry succeeds'

new_repository
mkdir .custom-hooks
git config core.hooksPath .custom-hooks
# A held configuration lock makes the last step, setting core.hooksPath, fail after the dispatcher is written.
: > .git/config.lock
status=0
"$dco" compose > compose-output 2>&1 || status=$?
rm .git/config.lock
dispatcher="$(git rev-parse --path-format=absolute --git-common-dir)/buster-dco-hooks"
[ "$status" = 1 ] && grep -q 'nothing changed' compose-output || fail 'compose: a failed core.hooksPath write'
[ ! -e "$dispatcher" ] && [ "$(git config core.hooksPath)" = .custom-hooks ] || fail 'a failed compose left a dispatcher behind'
"$dco" compose > /dev/null && [ "$(git config core.hooksPath)" = "$dispatcher" ] || fail 'compose: the retry after a failed core.hooksPath write'
echo 'PASS: a failed custom-layout compose removes its dispatcher, and a retry succeeds'

new_repository
mkdir .custom-hooks
git config core.hooksPath .custom-hooks
"$dco" compose > /dev/null
dispatcher="$(git rev-parse --path-format=absolute --git-common-dir)/buster-dco-hooks"
sed -i 's/^--local$/--global/' "$dispatcher/restore"
status=0
"$dco" uncompose > compose-output || status=$?
[ "$status" = 1 ] && grep -q 'changed since compose; left alone' compose-output || fail 'uncompose trusted an edited restore record'
[ "$(git config core.hooksPath)" = "$dispatcher" ] && [ -f "$dispatcher/restore" ] || fail 'uncompose acted on an edited restore record'
sed -i 's/^--global$/--local/' "$dispatcher/restore"
printf '.custom-hooks\nextra\n' > "$dispatcher/original"
status=0
"$dco" uncompose > compose-output || status=$?
[ "$status" = 1 ] && [ -f "$dispatcher/original" ] || fail 'uncompose removed an edited original record'
printf '.custom-hooks\n' > "$dispatcher/original"
"$dco" uncompose > /dev/null
[ "$(git config core.hooksPath)" = .custom-hooks ] && [ ! -e "$dispatcher" ] || fail 'uncompose after the records were restored'
echo 'PASS: uncompose leaves edited dispatcher records alone'
