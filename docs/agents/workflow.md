# Forge, issues, and pull requests

[Agent instructions](../../AGENTS.md) · Paths and commands below are relative to the repository root.

## Forge, issues, and pull requests

Use the repository and host named by the user; a GitHub URL names the GitHub
repository, not a Forgejo task. Otherwise inspect the current git remote.
Forgejo at `https://code.buster14a.com/buster/buster` retains the workflows
under `.forgejo/`; `.github/workflows/ci.yml` runs the migration matrix described
in `docs/ci-github-actions.md`. Check the live checks on the submitted commit.
The separate private workflow-only GitHub broker may supply opt-in hosted
desktop runners as documented in `docs/ci-github-hosted-runners.md`; that
broker is never a source mirror. When working on Forgejo, use **`fj`**, the Forgejo CLI. It reads
the repository from the git remote, so run it from inside a checkout (or pass
`-C <path>` / `-r buster/buster`).

The issues open on 2026-08-31 were copied to `buster14a/buster` on GitHub as
part of the migration. Numbers did not survive the copy, so historical
references in the topic guides, code comments, and commit messages may name a
**Forgejo** number: resolve those through `docs/issue-migration-map.md`, or
through `docs/forgejo-issue-archive.md` for issues already closed by then.
Do not remap an issue identified by a current GitHub URL.

`fj` needs a token once per machine. The password half of the
`code.buster14a.com` line in `~/.git-credentials` is a valid API token, so
authentication is a pipe, not a browser round trip:

```sh
grep code.buster14a.com ~/.git-credentials \
  | sed 's|https://[^:]*:||; s|@code.buster14a.com.*||' \
  | fj auth add-token -H code.buster14a.com
fj whoami          # verify: <account>@code.buster14a.com
```

`fj issue create "<title>" --body-file <path> --no-template`,
`fj issue search [-s open|closed|all]`, `fj issue view <n>`,
`fj issue comment <n>`, and `fj pr create --base main --head <branch>
--body-file <path>` are the whole working set; `fj pr search`, `fj pr status`
and `fj pr view` read the other side. Two things to know before scripting it:
the subcommand for listing issues is `search`, not `list`, and **omitting both
`--body` and `--body-file` opens `$EDITOR`**, which hangs a non-interactive
session — always pass a body file. Write the body as a file rather than a
shell string: backticks inside `$(cat <<EOF)` get command-substituted by zsh,
which has mangled a commit message before.

`fj` supersedes the older workarounds. `tea`'s login for this host has no
token, and the raw-`curl` recipe that went with it needed a browser
`User-Agent` to get past Cloudflare's `403 error code: 1010`; `fj` is not
subject to either problem.

**Issues are the task queue.** Work that is real but not being done right now
becomes an issue, not a paragraph in an audit that nobody will find — a chip
filed against a memory is invisible to the next agent, while an issue is
something a fresh session can pick up cold. Write the body as a **prompt**: what
is wrong and how it was diagnosed, the file and symbol names to start from,
the constraints and do-not-retries that earlier work already paid for, how to
validate the fix (which oracle, which harness, which counters), and a
definition of done. State what was measured and when, so a stale claim is
recognisable as stale; the tree moves fast enough that a count quoted without
a date is a trap. Issues #537-#549 are examples of the form.

**Push a rebase before you re-verify it.** A rebase onto a moved `main` is
followed by a full local pass — `test_all`, `test_self_host`, whichever compat
harness the change touches — and that pass takes longer than CI takes to start.
Push the rebased branch first, as long as the runners are not already saturated
with other work, so the matrix runs while the local pass runs; the two agree
almost always, and when they disagree you have both answers sooner. Force-push
with `--force-with-lease`, never a bare `--force`, so a branch someone else
advanced is not overwritten. This is a rebase rule, not a general one: a branch
whose content is still changing waits for the local pass, because a red CI run
on a commit you already know is incomplete tells nobody anything.
