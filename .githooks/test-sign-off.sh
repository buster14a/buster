#!/bin/sh
# Isolated Git integration fixtures for the shell hooks; no compiler build.
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

unsigned()
{
    if git log -1 --format=%B | grep -Fqx "$trailer"; then
        fail 'unexpected maintainer certification'
    fi
}

new_repository
sh .githooks/enable-sign-off.sh
[ -z "$(git config --local --get buster.dcoSignOff || :)" ] || fail 'startup persisted consent'
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
[ -z "$(git config --local --get buster.dcoSignOff || :)" ] || fail 'startup without .githooks persisted consent'
[ -z "$(git config --local --get core.hooksPath || :)" ] || fail 'startup without .githooks installed hooks'
if ! CLAUDE_PROJECT_DIR="$temporary/does-not-exist" sh "$source/.githooks/enable-sign-off.sh"; then
    fail 'no-argument startup failed on a bad project directory'
fi
echo 'PASS: no-argument startup without .githooks or a valid project directory is a no-op'

new_empty_repository
if sh "$source/.githooks/enable-sign-off.sh" --maintainer-session > setup-output 2>&1; then
    fail 'maintainer setup succeeded without .githooks'
fi
[ -z "$(git config --local --get buster.dcoSignOff || :)" ] || fail 'failed setup persisted consent'
[ -z "$(git config --local --get core.hooksPath || :)" ] || fail 'failed setup installed hooks'
echo 'PASS: maintainer setup without .githooks fails without recording consent'

new_repository
mkdir nested
(cd nested && sh ../.githooks/enable-sign-off.sh --maintainer-session > ../setup-output 2>&1)
[ "$(git config --local --get buster.dcoSignOff)" = davidgmbb ] || fail 'consent not persisted'
[ "$(git config --local --get core.hooksPath)" = .githooks ] || fail 'hook not installed'
grep -q 'every commit made here' setup-output || fail 'missing checkout-wide warning'
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
    [ -z "$(git config --local --get buster.dcoSignOff || :)" ] || fail "disabled session persisted consent ($disabled)"
    [ -z "$(git config --local --get core.hooksPath || :)" ] || fail "disabled session installed hooks ($disabled)"
    grep -q 'leaving it disabled' setup-output || fail "missing disabled diagnostic ($disabled)"
done
echo 'PASS: explicit opt-out blocks initial setup'

new_repository
BUSTER_DCO_SIGN_OFF=davidgmbb sh .githooks/enable-sign-off.sh
[ -z "$(git config --local --get buster.dcoSignOff || :)" ] || fail 'legacy environment consent became persistent'
BUSTER_DCO_SIGN_OFF=davidgmbb git commit --allow-empty -qm 'Legacy opt-in'
signed
git commit --allow-empty -qm 'Legacy opt-in removed'
unsigned
echo 'PASS: legacy environment-only setup retains its revocation behavior'

new_repository
sh .githooks/enable-sign-off.sh --maintainer-session
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
sh .githooks/enable-sign-off.sh --maintainer-session
git add .githooks
git commit -qm 'Track hooks for linked worktree'
git worktree add -q -b dco-worktree "$temporary/worktree"
cd "$temporary/worktree"
sh .githooks/enable-sign-off.sh
git commit --allow-empty -qm 'Linked worktree'
signed
echo 'PASS: persisted consent and tracked hook work in linked worktrees'

new_repository
if sh .githooks/enable-sign-off.sh --unknown; then
    fail 'unknown option accepted'
fi
[ -z "$(git config --local --get buster.dcoSignOff || :)" ] || fail 'invalid invocation persisted consent'
echo 'PASS: unknown options fail without recording consent'
