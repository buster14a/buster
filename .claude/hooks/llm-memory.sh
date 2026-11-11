#!/bin/bash
# Claude Code and Codex hook: gives cloud sessions the owner's agent memory, davidgmbb/llm-memory
# (docs/commands/llm-memory.md).
#
#   llm-memory.sh start   SessionStart: put the clone at ~/llm-memory; in Claude Code, link this checkout's memory
#                         folder into it
#   llm-memory.sh stop    Stop: run `llm-memory sync` in the background, so each turn's memories reach GitHub
#
# On the owner's own computers the clone, the link and the sync timer already exist, so the hook acts only in a
# cloud session: Claude Code's (CLAUDE_CODE_REMOTE=true), or Codex's, whose environment already sets
# BUSTER_TAILNET_DISPOSABLE=1 to join the tailnet (docs/commands/benchpress.md).
#
# Claude Code: the session's git proxy serves only the repositories attached to the session, so where the clone
# can't be made the hook says so in the session's context, and the agent attaches the repository with add_repo and
# runs `.claude/hooks/llm-memory.sh start` itself. add_repo clones beside this checkout; the hook links ~/llm-memory
# to that clone rather than make a second one. Auto memory is off in Claude Code's cloud unless the environment sets
# CLAUDE_CODE_DISABLE_AUTO_MEMORY=0; without it, agents save by hand.
#
# Codex: the clone uses the credentials the container already has: git's own, else the environment's GH_TOKEN or
# GITHUB_TOKEN, which the clone's credential helper reads from the environment when git asks, so the token is never
# written to disk. Whichever it is must be able to read and push davidgmbb/llm-memory. Codex saves its memories by
# hand, as AGENTS.md says.
#
# Neither cloud copies ~/.codex/memories: a container's host name is new each time, so each session would add a
# codex/<host>/ folder of its own.
#
# BUSTER_LLM_MEMORY_HOME stands for $HOME, for trying the hook without touching the real one.

home=${BUSTER_LLM_MEMORY_HOME:-$HOME}
memory=$home/llm-memory
project=${CLAUDE_PROJECT_DIR:-$(cd "$(dirname "$0")/../.." && pwd)}
sibling=$(dirname "$project")/llm-memory
state=$home/.local/state/llm-memory
name=buster-fast
helper='!f() { test "$1" = get && echo username=x-access-token && echo "password=${GH_TOKEN:-$GITHUB_TOKEN}"; }; f'
url=https://github.com/davidgmbb/llm-memory

claude()
{
    [ "${CLAUDE_CODE_REMOTE:-}" = true ]
}

codex()
{
    ! claude && [ "${BUSTER_TAILNET_DISPOSABLE:-}" = 1 ]
}

is_clone()
{
    git -C "$1" rev-parse -q --verify HEAD > /dev/null 2>&1 && [ -x "$1/bin/llm-memory" ]
}

start()
{
    if ! is_clone "$memory"; then
        if claude && [ ! -e "$memory" ] && is_clone "$sibling"; then
            ln -s "$sibling" "$memory"
        elif [ ! -e "$memory" ]; then
            GIT_TERMINAL_PROMPT=0 timeout 300 git clone -q --depth 1 "$url" "$memory" > /dev/null 2>&1 || rm -rf "$memory"
            if [ ! -e "$memory" ] && codex && [ -n "${GH_TOKEN:-$GITHUB_TOKEN}" ]; then
                GIT_TERMINAL_PROMPT=0 timeout 300 git -c credential.helper= -c credential.helper="$helper" clone -q --depth 1 "$url" "$memory" > /dev/null 2>&1 || rm -rf "$memory"
                [ ! -e "$memory" ] || git -C "$memory" config credential.helper "$helper"
            fi
        fi
    fi
    if is_clone "$memory" && codex; then
        # Codex's container may have no Git identity; sync commits need one.
        git -C "$memory" config user.email > /dev/null || git -C "$memory" config user.email noreply@openai.com
        git -C "$memory" config user.name > /dev/null || git -C "$memory" config user.name Codex
    fi
    if is_clone "$memory"; then
        if claude; then
            HOME=$home "$memory/bin/llm-memory" link "$project" "$name" > /dev/null
            echo "Agent memory: ~/llm-memory/projects/$name is this project's memory folder; the Stop hook syncs it to GitHub."
            if [ "${CLAUDE_CODE_DISABLE_AUTO_MEMORY:-}" != 0 ]; then
                echo "Auto memory is off in this cloud session (the environment doesn't set CLAUDE_CODE_DISABLE_AUTO_MEMORY=0): read ~/llm-memory/projects/$name/MEMORY.md and save there by hand, as AGENTS.md says for other agents."
            fi
        else
            echo "Agent memory: read ~/llm-memory/projects/$name/MEMORY.md and save there as AGENTS.md says; the Stop hook syncs it to GitHub."
        fi
    elif claude; then
        echo "Agent memory: davidgmbb/llm-memory isn't attached to this session. Attach it with the add_repo tool (access: push), let add_repo's clone finish, then run: bash .claude/hooks/llm-memory.sh start"
    else
        echo "Agent memory: couldn't clone davidgmbb/llm-memory with this container's Git credentials or GH_TOKEN/GITHUB_TOKEN; check that the agent's internet access allows github.com and that a token can read the repository."
    fi
}

stop()
{
    if is_clone "$memory"; then
        mkdir -p "$state"
        HOME=$home CODEX_HOME=$state/no-codex setsid -f timeout 120 "$memory/bin/llm-memory" sync >> "$state/cloud-sync.log" 2>&1 < /dev/null
    fi
}

if claude || codex; then
    case ${1:-} in
    start)
        start
        ;;
    stop)
        # The hook's input, which this doesn't need, is read so that the agent never writes to a closed pipe.
        cat > /dev/null
        stop
        ;;
    esac
fi
exit 0
