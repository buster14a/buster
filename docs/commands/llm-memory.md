# Agent memory in cloud sessions

The owner's agents keep what they learn about this project in the private
`davidgmbb/llm-memory` repository, under `projects/buster-fast/` (AGENTS.md,
"Agent memory"). On the owner's computers a clone at `~/llm-memory`, a symlink
from Claude Code's memory folder and a sync timer keep it current. Claude Code's
cloud containers start empty and are discarded, so `.claude/hooks/llm-memory.sh`
sets the same thing up in each cloud session and does nothing elsewhere
(`CLAUDE_CODE_REMOTE` isn't `true`).

- **SessionStart (`start`):** makes `~/llm-memory` a clone, then runs
  `llm-memory link` so that `~/.claude/projects/<checkout>/memory` points at
  `~/llm-memory/projects/buster-fast/`. A clone that `add_repo` made beside the
  checkout (`/home/user/llm-memory`) is linked to `~/llm-memory`; otherwise the
  hook clones the repository itself, which works only where the session already
  has it attached.
- **Stop (`stop`):** runs `llm-memory sync` in the background after every turn,
  logging to `~/.local/state/llm-memory/cloud-sync.log`. Sync commits, rebases
  onto GitHub and pushes; its lock makes overlapping runs no-ops, and a failed
  push waits for the next turn's. The commit names the session.

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

To try the hook without touching `$HOME`, set `BUSTER_LLM_MEMORY_HOME` to a
scratch directory and `CLAUDE_CODE_REMOTE=true`.
