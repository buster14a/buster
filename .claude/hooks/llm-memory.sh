#!/bin/bash
# Claude Code and Codex hook: gives cloud sessions the owner's agent memory, davidgmbb/llm-memory
# (docs/commands/llm-memory.md).
#
#   llm-memory.sh start   SessionStart: put the clone at ~/llm-memory; in Claude Code, link this checkout's memory
#                         folder into it
#   llm-memory.sh stop    Stop: run `llm-memory sync` in the background, so each turn's memories reach GitHub (in Claude
#                         Code, this session's branch and its one pull request)
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
checkout=$(cd "$(dirname "$0")/../.." && pwd)
# The folder Claude Code runs in: the checkout, or, in a session with several repositories, the folder that holds
# them. Claude Code keys its memory folder by it, so that is the one to link.
project=${CLAUDE_PROJECT_DIR:-$checkout}
sibling=$(dirname "$checkout")/llm-memory
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

# A Claude Code cloud session may push only to its own branch, the one this checkout is on. Put the clone on that branch
# (GitHub's copy of it if a container before this one pushed it, else main) and set llm-memory.branch, so that sync
# commits there, rebases it on GitHub's main, pushes it and keeps one pull request open for it. Nothing commits to local
# main, so it can't diverge from GitHub's. A shallow clone is deepened, since the index orders notes by when Git first
# saw them.
on_session_branch()
{
    local branch
    local current
    local fetch=()
    branch=$(git -C "$checkout" symbolic-ref -q --short HEAD) || branch=
    case $branch in
    claude/*)
        ;;
    *)
        echo "Agent memory: this checkout is on ${branch:-a detached HEAD}, not a claude/ session branch, so sync can't push memories; save them and open a pull request in davidgmbb/llm-memory by hand."
        return
        ;;
    esac
    if [ "$(git -C "$memory" rev-parse --is-shallow-repository 2> /dev/null)" = true ]; then
        fetch=(--depth=1000)
    fi
    GIT_TERMINAL_PROMPT=0 timeout 120 git -C "$memory" fetch -q "${fetch[@]}" origin +refs/heads/main:refs/remotes/origin/main > /dev/null 2>&1 || true
    GIT_TERMINAL_PROMPT=0 timeout 60 git -C "$memory" fetch -q origin "+refs/heads/$branch:refs/remotes/origin/$branch" > /dev/null 2>&1 || true
    current=$(git -C "$memory" symbolic-ref -q --short HEAD) || current=
    if [ "$current" != "$branch" ]; then
        if git -C "$memory" rev-parse -q --verify "refs/heads/$branch" > /dev/null; then
            git -C "$memory" checkout -q "$branch" > /dev/null 2>&1 || true
        elif git -C "$memory" merge-base --is-ancestor HEAD refs/remotes/origin/main 2> /dev/null; then
            if git -C "$memory" rev-parse -q --verify "refs/remotes/origin/$branch" > /dev/null; then
                git -C "$memory" checkout -q --no-track -b "$branch" "refs/remotes/origin/$branch" > /dev/null 2>&1 || true
            else
                git -C "$memory" checkout -q --no-track -b "$branch" refs/remotes/origin/main > /dev/null 2>&1 || true
            fi
        else
            # HEAD holds memories main lacks: they go on the session branch, and sync rebases them onto main.
            git -C "$memory" checkout -q -b "$branch" > /dev/null 2>&1 || true
        fi
    fi
    if [ "$(git -C "$memory" symbolic-ref -q --short HEAD)" = "$branch" ]; then
        git -C "$memory" config llm-memory.branch "$branch"
    else
        echo "Agent memory: ~/llm-memory is on ${current:-a detached HEAD} and couldn't move to $branch; sync would push nowhere. Check it out by hand, then run: git -C ~/llm-memory config llm-memory.branch $branch"
    fi
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
    if is_clone "$memory" && claude; then
        on_session_branch
    fi
    if is_clone "$memory"; then
        if claude; then
            HOME=$home "$memory/bin/llm-memory" link "$checkout" "$name" > /dev/null
            if [ "$(cd "$project" && pwd -P)" = "$(cd "$checkout/.." && pwd -P)" ]; then
                HOME=$home "$memory/bin/llm-memory" link "$project" "$name" > /dev/null
            fi
            echo "Agent memory: ~/llm-memory/projects/$name is this project's memory folder; the Stop hook syncs it to GitHub, on this session's branch and pull request. MEMORY.md is generated from the notes: save each memory as a note with name/description frontmatter."
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
