# Sourced by commit-msg, enable-sign-off.sh and tools/dco/dco.c's status: the
# one resolver of David's sign-off opt-in and of which commit-msg hook runs
# (CONTRIBUTING.md, "Automatic mechanics and revocation"). Run it from inside
# a working tree. Inspecting changes nothing.
#
# The opt-in, in this order:
# 1. A present BUSTER_DCO_SIGN_OFF decides alone: davidgmbb enables, any other
#    value, empty included, disables.
# 2. Any buster.dcoSignOff value other than davidgmbb, in any Git
#    configuration scope, disables: Git configuration can opt out, never in.
# 3. This worktree's marker, the file `git rev-parse --git-path
#    buster-dco-sign-off` names, enables when its first line is davidgmbb and
#    disables otherwise. Each linked worktree has its own (in
#    .git/worktrees/NAME/), the main worktree's is .git/buster-dco-sign-off,
#    and a new worktree starts without one.
# 4. Otherwise it is off. #1156's clone-wide buster.dcoSignOff=davidgmbb is
#    inert: dco_legacy reports it so that setup can say so.

dco_consent()
{
    dco_marker=$(git rev-parse --path-format=absolute --git-path buster-dco-sign-off)
    dco_marker_state=absent
    if [ -f "$dco_marker" ]; then
        if [ "$(sed -n 1p "$dco_marker")" = davidgmbb ]; then
            dco_marker_state=consent
        else
            dco_marker_state=opt-out
        fi
    fi
    dco_opt_out_scope=$(git config --show-scope --get-all buster.dcoSignOff | awk -F '\t' '$2 != "davidgmbb" { print $1; exit }')
    dco_legacy=absent
    if git config --local --get-all buster.dcoSignOff | grep -q -x davidgmbb; then
        dco_legacy=present
    fi
    if [ -n "${BUSTER_DCO_SIGN_OFF+set}" ]; then
        dco_source=environment
        dco_on=$([ "$BUSTER_DCO_SIGN_OFF" = davidgmbb ] && echo yes || echo no)
    elif [ -n "$dco_opt_out_scope" ]; then
        dco_source=config-opt-out
        dco_on=no
    elif [ "$dco_marker_state" != absent ]; then
        dco_source=worktree
        dco_on=$([ "$dco_marker_state" = consent ] && echo yes || echo no)
    else
        dco_source=none
        dco_on=no
    fi
}

# A rebase or cherry-pick copies commits that keep their own certifications.
dco_copying()
{
    [ -e "$(git rev-parse --git-path rebase-merge)" ] || [ -e "$(git rev-parse --git-path rebase-apply)" ] || [ -e "$(git rev-parse --git-path CHERRY_PICK_HEAD)" ]
}

# core.commentChar or core.commentString set to auto: commit-msg refuses.
dco_automatic()
{
    keys='^core\.comment(char|string)$'
    [ "$(echo x | git -c core.commentString=// stripspace --comment-lines)" = "// x" ] || keys='^core\.commentchar$'
    git config --get-regexp "$keys" | tail -n 1 | cut -d ' ' -f 2- | grep -q -i -x auto
}

# Which commit-msg runs. dco_hooks_dir is Git's hooks directory (core.hooksPath
# or the common .git/hooks), dco_commit_msg one of: direct (this worktree's
# .githooks), worktree-copy (another worktree's .githooks, as the desktop
# app's worktrees configure), adapter or dispatcher (tools/dco/dco.c compose),
# foreign (a hook this setup doesn't own), absent (Git's directory holds hooks
# but no commit-msg), none (no hooks at all).
dco_hook_state()
{
    dco_hooks_path=$(git config --type=path --get core.hooksPath)
    dco_hooks_scope=$(git config --show-scope --get-all core.hooksPath | tail -n 1 | cut -f 1)
    dco_hooks_dir=$(git rev-parse --path-format=absolute --git-path hooks)
    real=$(cd "$dco_hooks_dir" 2>/dev/null && pwd -P)
    own=$(cd "$(git rev-parse --show-toplevel)/.githooks" 2>/dev/null && pwd -P)
    dco_commit_msg=none
    if [ -e "$dco_hooks_dir/commit-msg" ] || [ -L "$dco_hooks_dir/commit-msg" ]; then
        dco_commit_msg=foreign
        if grep -q '^# buster-dco-adapter ' "$dco_hooks_dir/commit-msg" 2>/dev/null; then
            dco_commit_msg=adapter
        elif grep -q '^# buster-dco-dispatch ' "$dco_hooks_dir/commit-msg" 2>/dev/null; then
            dco_commit_msg=dispatcher
        elif [ -n "$real" ] && [ "$real" = "$own" ]; then
            dco_commit_msg=direct
        elif [ -n "$real" ] && git worktree list --porcelain | sed -n 's/^worktree //p' | { while IFS= read -r worktree; do [ "$(cd "$worktree/.githooks" 2>/dev/null && pwd -P)" != "$real" ] || exit 0; done; exit 1; }; then
            dco_commit_msg=worktree-copy
        fi
    elif [ -n "$(find -H "$dco_hooks_dir" -mindepth 1 -maxdepth 1 ! -name '*.sample' 2>/dev/null)" ]; then
        dco_commit_msg=absent
    fi
}

# Everything above as name=value lines, for tools/dco/dco.c's status.
dco_report()
{
    dco_consent
    dco_hook_state
    echo "on=$dco_on"
    echo "source=$dco_source"
    echo "marker=$dco_marker"
    echo "marker_state=$dco_marker_state"
    echo "opt_out_scope=$dco_opt_out_scope"
    echo "legacy=$dco_legacy"
    echo "environment=$([ -z "${BUSTER_DCO_SIGN_OFF+set}" ] && echo absent || { [ "$BUSTER_DCO_SIGN_OFF" = davidgmbb ] && echo enabling || echo disabling; })"
    echo "copying=$(dco_copying && echo yes || echo no)"
    echo "automatic=$(dco_automatic && echo yes || echo no)"
    echo "hooks_path=$dco_hooks_path"
    echo "hooks_scope=$dco_hooks_scope"
    echo "hooks_dir=$dco_hooks_dir"
    echo "commit_msg=$dco_commit_msg"
    echo "worktree=$(git rev-parse --show-toplevel)"
    echo "git_dir=$(git rev-parse --path-format=absolute --git-dir)"
    echo "common_dir=$(git rev-parse --path-format=absolute --git-common-dir)"
}
