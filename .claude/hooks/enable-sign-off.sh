#!/bin/sh
# SessionStart hook, which other agents run by hand (AGENTS.md, "David's
# sign-off"). Where David's opt-in BUSTER_DCO_SIGN_OFF=davidgmbb is set, it
# points Git at .githooks, whose commit-msg hook adds his sign-off. Anywhere
# else it does nothing. It leaves a hooks path that is already set, and the
# hooks in .git/hooks that the setting would turn off, and then says so.

if [ "$BUSTER_DCO_SIGN_OFF" = davidgmbb ] && cd "${CLAUDE_PROJECT_DIR:-.}" && git rev-parse --git-dir > /dev/null 2>&1; then
    path="$(git config core.hooksPath)"
    own="$(find "$(git rev-parse --git-common-dir)/hooks" -type f ! -name '*.sample' 2> /dev/null)"
    if [ -z "$path" ] && [ -z "$own" ]; then
        git config core.hooksPath .githooks
    elif [ "$path" != .githooks ]; then
        echo "David's sign-off hook is off (core.hooksPath is '$path'; .git/hooks has '$own'), so write his trailer yourself, last (AGENTS.md, \"David's sign-off\")."
    fi
fi
exit 0
