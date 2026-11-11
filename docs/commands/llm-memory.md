# Agent memory in cloud sessions

The owner's agents keep what they learn about this project in the private
`davidgmbb/llm-memory` repository, under `projects/buster-fast/` (AGENTS.md,
"Agent memory"). On the owner's computers a clone at `~/llm-memory`, a symlink
from Claude Code's memory folder and a sync timer keep it current. Claude Code's
and Codex's (ChatGPT's) cloud containers start empty and are discarded, so
`.claude/hooks/llm-memory.sh`, run by `.claude/settings.json` and
`.codex/hooks.json`, sets the same thing up in each cloud session. It acts only
in Claude Code's cloud (`CLAUDE_CODE_REMOTE=true`) or where
`BUSTER_LLM_MEMORY_TOKEN` is set (Codex's cloud), and does nothing elsewhere.

- **SessionStart (`start`):** makes `~/llm-memory` a clone, then, in Claude
  Code, runs `llm-memory link` so that `~/.claude/projects/<checkout>/memory`
  points at `~/llm-memory/projects/buster-fast/`. In Claude Code, a clone that
  `add_repo` made beside the checkout (`/home/user/llm-memory`) is linked to
  `~/llm-memory`; otherwise the hook clones the repository itself, which works
  only where the session already has it attached (Claude Code) or with the
  token (Codex).
- **Stop (`stop`):** runs `llm-memory sync` in the background after every turn,
  logging to `~/.local/state/llm-memory/cloud-sync.log`. Sync commits, rebases
  onto GitHub and pushes; its lock makes overlapping runs no-ops, and a failed
  push waits for the next turn's. A Claude Code commit names the session.
  Neither cloud copies `~/.codex/memories` into `codex/`: a container's host
  name is new every time, so each session would add a folder of its own.

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

Codex's container reaches GitHub with no credentials of the owner's, so its
environment (chatgpt.com/codex, Environments, this repository's environment)
needs:

- **Environment variable `BUSTER_LLM_MEMORY_TOKEN`:** a fine-grained GitHub
  token for `davidgmbb/llm-memory` alone, with Contents read and write. It has
  to be an environment variable, not a secret: Codex removes secrets after the
  setup script, and the Stop hook pushes during the task. The clone's
  credential helper reads it from the environment when Git asks, so it is never
  written to disk.
- **Agent internet access** that allows `github.com`.

Codex has no Claude-style auto memory, so its agents read
`~/llm-memory/projects/buster-fast/MEMORY.md` and save there by hand, as
AGENTS.md says; the hook's Stop run commits and pushes what they save, as
`Codex <noreply@openai.com>` where the container has no Git identity. If the
clone fails, the hook says so in the session's context.

To try the hook without touching `$HOME`, set `BUSTER_LLM_MEMORY_HOME` to a
scratch directory and `CLAUDE_CODE_REMOTE=true` or `BUSTER_LLM_MEMORY_TOKEN`.
