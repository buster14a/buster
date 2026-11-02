#!/bin/sh
# SessionStart hook for Claude Code and Codex. With no arguments, install the
# hook only when an existing opt-in permits it. A maintainer-directed session
# may use --maintainer-session under CONTRIBUTING.md to remember that opt-in
# in this checkout. Never infer authorization from a remote or Git identity.
# Existing hooks and explicit opt-outs are left alone.

mode=${1-}
if [ "$#" -gt 1 ] || { [ -n "$mode" ] && [ "$mode" != --maintainer-session ]; }; then
    echo "Usage: sh .githooks/enable-sign-off.sh [--maintainer-session]" >&2
    exit 2
fi

cd "${CLAUDE_PROJECT_DIR:-.}" 2>/dev/null || {
    if [ "$mode" = --maintainer-session ]; then
        echo "David's sign-off setup needs a Buster working tree." >&2
        exit 1
    fi
    exit 0
}
root=$(git rev-parse --show-toplevel 2>/dev/null) || {
    if [ "$mode" = --maintainer-session ]; then
        echo "David's sign-off setup needs a Buster working tree." >&2
        exit 1
    fi
    exit 0
}
cd "$root" || exit 1

# Only the explicit maintainer-session mode persists consent. Normal session
# startup never turns a temporary environment opt-in into a permanent one.
# A present environment value (including empty) wins over local configuration.
# Do not overwrite a persisted opt-out when another agent starts a session.
persist=0
if [ "$mode" = --maintainer-session ] && [ "${BUSTER_DCO_SIGN_OFF-davidgmbb}" = davidgmbb ] && ! git config --local --get buster.dcoSignOff >/dev/null 2>&1; then
    persist=1
fi
sign_off=${BUSTER_DCO_SIGN_OFF-$(git config --local --get buster.dcoSignOff)}
if [ "$persist" = 1 ]; then
    sign_off=davidgmbb
fi
if [ "$sign_off" = davidgmbb ] && [ ! -f .githooks/commit-msg ]; then
    echo "David's sign-off setup cannot find .githooks/commit-msg." >&2
    exit 1
fi
if [ "$persist" = 1 ]; then
    git config --local buster.dcoSignOff davidgmbb || exit 1
    echo "David's sign-off is enabled for this checkout: the opt-in covers every commit made here, by any author. Disable it with 'git config --local buster.dcoSignOff off' before another contributor uses this checkout." >&2
fi
if [ "$sign_off" = davidgmbb ]; then
    path=$(git config core.hooksPath)
    own=$(find -H "$(git rev-parse --git-common-dir)/hooks" -mindepth 1 -maxdepth 1 ! -name '*.sample' 2>/dev/null)
    if [ -z "$path" ] && [ -z "$own" ]; then
        git config --local core.hooksPath .githooks || exit 1
    elif [ "$path" != .githooks ]; then
        echo "David's sign-off hook is off (core.hooksPath is '$path'; .git/hooks has '$own'), so write his authorized trailer yourself, last (AGENTS.md, \"David's sign-off\")."
    fi
elif [ "$mode" = --maintainer-session ]; then
    echo "David's sign-off is disabled by an explicit environment or checkout setting; leaving it disabled."
fi
exit 0
