#!/bin/sh
# SessionStart hook of Claude Code (.claude/settings.json) and Codex
# (.codex/hooks.json), which other agents run by hand (AGENTS.md, "David's
# sign-off"). Where David's opt-in BUSTER_DCO_SIGN_OFF=davidgmbb is set, it
# points Git at .githooks, whose commit-msg hook adds his sign-off. Anywhere
# else it does nothing. It leaves a hooks path that is already set, and the
# hooks in .git/hooks that the setting would turn off (any entry there but a
# sample, symbolic links and a linked directory included), and then says so.

if [ "$BUSTER_DCO_SIGN_OFF" = davidgmbb ] && cd "${CLAUDE_PROJECT_DIR:-.}" && git rev-parse --git-dir > /dev/null 2>&1; then
    path="$(git config core.hooksPath)"
    own="$(find -H "$(git rev-parse --git-common-dir)/hooks" -mindepth 1 -maxdepth 1 ! -name '*.sample' 2> /dev/null)"
    if [ -z "$path" ] && [ -z "$own" ]; then
        git config core.hooksPath .githooks
    elif [ "$path" != .githooks ]; then
        echo "David's sign-off hook is off (core.hooksPath is '$path'; .git/hooks has '$own'), so write his trailer yourself, last (AGENTS.md, \"David's sign-off\")."
    fi
fi
exit 0
