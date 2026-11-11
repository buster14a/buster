#!/bin/bash
# Claude Code hook for sessions that start in the folder holding this checkout rather than in it: a cloud session with
# several repositories (docs/commands/llm-memory.md). Claude Code reads .claude/settings.json only from the folder it
# starts in, so there this checkout's hooks never run; the environment's setup script names this script in the user
# settings instead, and it runs the hooks .claude/settings.json lists.
#
#   parent-folder.sh start   SessionStart: join the tailnet, enable sign-off, set up agent memory
#   parent-folder.sh stop    Stop: sync agent memory
#
# Where the session starts in the checkout, .claude/settings.json runs the hooks itself, and this does nothing.

checkout=$(cd "$(dirname "$0")/../.." && pwd -P)
parent=$(dirname "$checkout")
project=$(cd "${CLAUDE_PROJECT_DIR:-$checkout}" 2> /dev/null && pwd -P)

if [ "$project" = "$parent" ]; then
    case ${1:-} in
    start)
        "$checkout/.claude/hooks/join-tailnet.sh" < /dev/null
        CLAUDE_PROJECT_DIR=$checkout "$checkout/.githooks/enable-sign-off.sh" < /dev/null
        # llm-memory.sh links both this checkout's memory folder and the one Claude Code keys by $CLAUDE_PROJECT_DIR.
        "$checkout/.claude/hooks/llm-memory.sh" start < /dev/null
        ;;
    stop)
        "$checkout/.claude/hooks/llm-memory.sh" stop
        ;;
    esac
fi
exit 0
