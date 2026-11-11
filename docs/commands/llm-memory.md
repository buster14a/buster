# Agent memory in cloud sessions

The owner's agents keep what they learn about this project in the private
`davidgmbb/llm-memory` repository, under `projects/buster-fast/` (AGENTS.md,
"Agent memory"). On the owner's computers a clone at `~/llm-memory`, a symlink
from Claude Code's memory folder and a sync timer keep it current. Claude Code's
and Codex's (ChatGPT's) cloud containers start empty and are discarded, so
`.claude/hooks/llm-memory.sh`, run by `.claude/settings.json` and
`.codex/hooks.json`, sets the same thing up in each cloud session. It acts only
in Claude Code's cloud (`CLAUDE_CODE_REMOTE=true`) or Codex's, whose
environment already sets `BUSTER_TAILNET_DISPOSABLE=1` to join the tailnet
([benchpress](benchpress.md)), and does nothing elsewhere.

- **SessionStart (`start`):** makes `~/llm-memory` a clone, then, in Claude
  Code, runs `llm-memory link` so that `~/.claude/projects/<checkout>/memory`
  points at `~/llm-memory/projects/buster-fast/`. In Claude Code, a clone that
  `add_repo` made beside the checkout (`/home/user/llm-memory`) is linked to
  `~/llm-memory`; otherwise the hook clones the repository itself, which works
  only where the session already has it attached (Claude Code) or the
  container's credentials reach it (Codex). `add_repo` checks its clone out on
  the session's branch, and sync pushes the branch it is on, so the hook moves
  the clone onto `main` when the branch holds nothing `main` lacks, and says so
  when it can't.
- **Stop (`stop`):** runs `llm-memory sync` in the background after every turn,
  logging to `~/.local/state/llm-memory/cloud-sync.log`. Sync commits, rebases
  onto GitHub and pushes; its lock makes overlapping runs no-ops, and a failed
  push waits for the next turn's. A Claude Code commit names the session.
  Neither cloud copies `~/.codex/memories` into `codex/`: a container's host
  name is new every time, so each session would add a folder of its own.

## Sessions that start beside the checkout

A Claude Code cloud session with several repositories (this one and
`davidgmbb/llm-memory`) starts in the folder holding the checkouts,
`/home/user`, not in `/home/user/buster`. Claude Code reads
`.claude/settings.json` only from the folder it starts in, so there none of
this checkout's hooks run: the tailnet, sign-off and memory are all left
unset, and Claude Code's memory folder is the one keyed by `/home/user`.

The environment's setup script therefore writes user settings,
`~/.claude/settings.json`, whose SessionStart and Stop hooks run
`.claude/hooks/parent-folder.sh start` and `stop`. That script acts only when
`$CLAUDE_PROJECT_DIR` is the checkout's parent, so a session that starts in
the checkout doesn't run the hooks twice. It runs the hooks that
`.claude/settings.json` lists, and `llm-memory.sh start` links the parent's
memory folder as well as the checkout's. In an environment without that setup,
or a container built before it, run it by hand from the parent:

```bash
CLAUDE_PROJECT_DIR=$PWD buster/.claude/hooks/parent-folder.sh start
```

The Stop hook doesn't run there either, so sync after saving a memory, as
AGENTS.md asks of other agents: `~/llm-memory/bin/llm-memory sync`.

## When the repository isn't attached

A cloud session's git proxy serves only the repositories attached to that
session, and the Default environment attaches none besides this one. The hook
then says so in the session's context. The agent attaches it with the `add_repo`
tool (`davidgmbb/llm-memory`, `access: push`), lets `add_repo`'s clone finish,
and runs:

```bash
bash .claude/hooks/llm-memory.sh start
```

Memories Claude saved before then are in a plain folder; `llm-memory link`
moves it aside and merges it into the clone.

## Auto memory

Claude Code turns auto memory off in cloud sessions unless the environment
asks for it. The owner's cloud environment sets the non-secret variable
`CLAUDE_CODE_DISABLE_AUTO_MEMORY=0`, which turns it on with the usual folder,
the one the hook links. Without it the hook says so, and agents read
`MEMORY.md` and save memories by hand, as AGENTS.md asks of other agents. The
variable is read when Claude Code starts, so a change applies from the next
session.

## Codex (ChatGPT) cloud

The hook adds no variable of its own. It clones with the credentials Codex's
container already has: Git's own, else the environment's `GH_TOKEN` or
`GITHUB_TOKEN`. A token is read from the environment by the clone's credential
helper whenever Git asks, so it is never written to disk. Whichever credential
it is must be able to read and push `davidgmbb/llm-memory`, and the agent's
internet access must allow `github.com`. Codex removes secrets after the setup
script, so a token the Stop hook pushes with has to be an environment variable.

Codex has no Claude-style auto memory, so its agents read
`~/llm-memory/projects/buster-fast/MEMORY.md` and save there by hand, as
AGENTS.md says; the hook's Stop run commits and pushes what they save, as
`Codex <noreply@openai.com>` where the container has no Git identity. If the
clone fails, the hook says so in the session's context.

To try the hook without touching `$HOME`, set `BUSTER_LLM_MEMORY_HOME` to a
scratch directory and `CLAUDE_CODE_REMOTE=true` or `BUSTER_TAILNET_DISPOSABLE=1`.
