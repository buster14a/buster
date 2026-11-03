#!/bin/sh
# SessionStart hook for Claude Code and Codex. With no arguments, install the
# hook only when an existing opt-in permits it. A maintainer-directed session
# may use --maintainer-session under CONTRIBUTING.md to remember that opt-in
# in this worktree alone; --revoke opts this worktree out. sign-off-lib.sh
# resolves the opt-in. Never infer authorization from a remote or Git
# identity. Existing hooks and explicit opt-outs are left alone.

mode=${1-}
if [ "$#" -gt 1 ] || { [ -n "$mode" ] && [ "$mode" != --maintainer-session ] && [ "$mode" != --revoke ]; }; then
    echo "Usage: sh .githooks/enable-sign-off.sh [--maintainer-session | --revoke]" >&2
    exit 2
fi

fail()
{
    if [ -n "$mode" ]; then
        echo "David's sign-off setup needs a Buster working tree." >&2
        exit 1
    fi
    exit 0
}

cd "${CLAUDE_PROJECT_DIR:-.}" 2>/dev/null || fail
root=$(git rev-parse --show-toplevel 2>/dev/null) || fail
cd "$root" || exit 1
[ -f .githooks/sign-off-lib.sh ] || fail
. .githooks/sign-off-lib.sh
dco_consent

if [ "$mode" = --revoke ]; then
    echo off > "$dco_marker" || exit 1
    echo "David's sign-off is disabled for this worktree ($root); other worktrees keep their own state, and startup won't re-enable it. A present BUSTER_DCO_SIGN_OFF=davidgmbb still enables a single command."
    exit 0
fi

# Only the explicit maintainer-session mode persists consent, and only into
# this worktree's marker. Normal startup never turns a temporary environment
# opt-in into a permanent one, and no mode overwrites an opt-out.
if [ "$mode" = --maintainer-session ]; then
    if [ "${BUSTER_DCO_SIGN_OFF-davidgmbb}" != davidgmbb ]; then
        echo "David's sign-off is disabled by BUSTER_DCO_SIGN_OFF; leaving it disabled."
        exit 0
    elif [ -n "$dco_opt_out_scope" ]; then
        echo "David's sign-off is disabled by buster.dcoSignOff in $dco_opt_out_scope Git configuration; leaving it disabled."
        exit 0
    elif [ "$dco_marker_state" = opt-out ]; then
        echo "David's sign-off is disabled for this worktree by $dco_marker; leaving it disabled. David may remove that file to allow setup again."
        exit 0
    fi
    if [ ! -f .githooks/commit-msg ]; then
        echo "David's sign-off setup cannot find .githooks/commit-msg." >&2
        exit 1
    fi
    if [ "$dco_marker_state" = absent ]; then
        echo davidgmbb > "$dco_marker" || exit 1
    fi
    if [ "$dco_legacy" = present ]; then
        git config --local --unset-all buster.dcoSignOff '^davidgmbb$' || exit 1
        echo "Removed the clone-wide buster.dcoSignOff=davidgmbb that #1156's setup wrote; it no longer enables any worktree, so each needs its own setup."
    fi
    dco_on=yes
    echo "David's sign-off is enabled for this worktree only ($root): it covers every commit made here, by any author, and no other worktree. Run 'sh .githooks/enable-sign-off.sh --revoke' before another contributor uses this worktree."
elif [ "$dco_legacy" = present ]; then
    echo "buster.dcoSignOff=davidgmbb in this clone's shared Git configuration no longer enables David's sign-off; a session he directs runs 'sh .githooks/enable-sign-off.sh --maintainer-session' in each worktree that should sign."
fi

if [ "$dco_on" = yes ]; then
    dco_hook_state
    case $dco_commit_msg in
        direct | worktree-copy | adapter | dispatcher)
            ;;
        *)
            if [ "$dco_commit_msg" = none ] && [ -z "$dco_hooks_path" ]; then
                git config --local core.hooksPath .githooks || exit 1
            else
                echo "David's sign-off hook is off (Git runs hooks from '$dco_hooks_dir', whose commit-msg is $dco_commit_msg). Chain them once with 'cc -O2 -o build/dco tools/dco/dco.c && build/dco compose' (CONTRIBUTING.md), or write his authorized trailer yourself, last (AGENTS.md, \"David's sign-off\")."
            fi
            ;;
    esac
fi
exit 0
