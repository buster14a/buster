#!/bin/bash
# Claude Code hook: gives cloud sessions the owner's agent memory, davidgmbb/llm-memory (docs/commands/llm-memory.md).
#
#   llm-memory.sh start   SessionStart: put the clone at ~/llm-memory and link this checkout's memory folder into it
#   llm-memory.sh stop    Stop: run `llm-memory sync` in the background, so each turn's memories reach GitHub
#
# On the owner's own computers the clone, the link and the sync timer already exist, so the hook does nothing outside
# Claude Code's cloud (CLAUDE_CODE_REMOTE=true). A cloud session's git proxy serves only the repositories attached to
# the session, so where the clone can't be made the hook says so in the session's context, and the agent attaches the
# repository with add_repo and runs `.claude/hooks/llm-memory.sh start` itself. add_repo clones beside this checkout;
# the hook links ~/llm-memory to that clone rather than make a second one.
#
# Auto memory is off in cloud sessions unless the environment sets CLAUDE_CODE_DISABLE_AUTO_MEMORY=0; without it the
# link is still made, so agents can read the memory and save to it by hand.
#
# BUSTER_LLM_MEMORY_HOME stands for $HOME, for trying the hook without touching the real one.

home=${BUSTER_LLM_MEMORY_HOME:-$HOME}
memory=$home/llm-memory
project=${CLAUDE_PROJECT_DIR:-$(cd "$(dirname "$0")/../.." && pwd)}
sibling=$(dirname "$project")/llm-memory
state=$home/.local/state/llm-memory
name=buster-fast

is_clone()
{
    git -C "$1" rev-parse -q --verify HEAD > /dev/null 2>&1 && [ -x "$1/bin/llm-memory" ]
}

start()
{
    if ! is_clone "$memory"; then
        if [ ! -e "$memory" ] && is_clone "$sibling"; then
            ln -s "$sibling" "$memory"
        elif [ ! -e "$memory" ]; then
            timeout 300 git clone -q --depth 1 https://github.com/davidgmbb/llm-memory "$memory" > /dev/null 2>&1 || rm -rf "$memory"
        fi
    fi
    if is_clone "$memory"; then
        HOME=$home "$memory/bin/llm-memory" link "$project" "$name" > /dev/null
        echo "Agent memory: ~/llm-memory/projects/$name is this project's memory folder; the Stop hook syncs it to GitHub."
        if [ "${CLAUDE_CODE_DISABLE_AUTO_MEMORY:-}" != 0 ]; then
            echo "Auto memory is off in this cloud session (the environment doesn't set CLAUDE_CODE_DISABLE_AUTO_MEMORY=0): read ~/llm-memory/projects/$name/MEMORY.md and save there by hand, as AGENTS.md says for other agents."
        fi
    else
        echo "Agent memory: davidgmbb/llm-memory isn't attached to this session. Attach it with the add_repo tool (access: push), let add_repo's clone finish, then run: bash .claude/hooks/llm-memory.sh start"
    fi
}

stop()
{
    if is_clone "$memory"; then
        mkdir -p "$state"
        HOME=$home setsid -f timeout 120 "$memory/bin/llm-memory" sync >> "$state/cloud-sync.log" 2>&1 < /dev/null
    fi
}

if [ "${CLAUDE_CODE_REMOTE:-}" = true ]; then
    case ${1:-} in
    start)
        start
        ;;
    stop)
        # The hook's input, which this doesn't need, is read so that Claude Code never writes to a closed pipe.
        cat > /dev/null
        stop
        ;;
    esac
fi
exit 0
