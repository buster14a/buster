# Forge, issues, and pull requests

[Agent instructions](../../AGENTS.md) · [Research lifecycle](research.md) · Paths and commands below are relative to the repository root.

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

Research, performance, experiment, and architecture issues also carry at most
one primary lifecycle label for their immediate evidence gate. Keep area,
architecture, kind, and priority labels orthogonal; preserve historical bodies
and update the live state in issue metadata plus a compact comment. See the
[research lifecycle](research.md) for the vocabulary, transition rules, and
required evidence/disposition fields.

## Cross-cutting internal API migrations

Default to **add -> migrate -> remove** when a new internal API can coexist
safely with its predecessor. A large logical change is not, by itself, a reason
to rewrite every caller in one branch. The purpose of staging is to keep
`main` correct while making each caller group independently reviewable,
mergeable and revertible.

### Add the new contract

Land the new contract, implementation and focused behavior tests first. When
unmigrated callers still need the previous call shape, retain one narrow
compatibility entry point that preserves their existing semantics without
weakening the new invariant. The Add PR must:

- state the old and new ownership, error, environment, inheritance and lookup
  policies that matter at the boundary;
- test both entry points in the same partially migrated tree;
- keep the compatibility symbol explicit and searchable rather than hiding the
  old behavior behind defaults or an overloaded parameter;
- file a distinct removal issue and state the objective condition that makes
  removal safe; and
- register the active compatibility path in `.github/api-migrations.json`.

Each registry entry gives the compatibility symbol, its owning and removal
issues, its removal condition, the source globs to audit, and exact token counts
for the narrow implementation and every admitted old-style caller. For example:

```json
{
  "id": "issue-123-explicit-widget-open",
  "compatibility_symbol": "widget_open_legacy",
  "owner_issue": 123,
  "removal_issue": 124,
  "removal_condition": "Remove after every caller supplies WidgetOpenOptions.",
  "scan_globs": ["src/**/*.c", "src/**/*.h", "tools/**/*.c", "build.c"],
  "compatibility_owners": {
    "src/buster/lib/widget.c": 1,
    "src/buster/lib/widget.h": 1
  },
  "allowed_callers": {
    "src/buster/apps/ide/ide.c": 1
  }
}
```

`python3 tools/api_migration_audit.py` fails on unregistered uses, changed token
counts, unsafe or unmatched paths, missing issue ownership, and a compatibility
entry with no remaining callers. The last caller migration must therefore be
coordinated with the Remove PR instead of leaving an unused shim on `main`.
`python3 tools/api_migration_audit_test.py -v` exercises the policy scanner and
a valid partially migrated tree. The dedicated policy workflow runs both on
pull requests, merge groups and `main`.

### Migrate caller groups

Start every independent migration PR from current `main`, not from the Add PR
or another sibling feature branch after Add has landed. Group callers by a
real subsystem boundary and include only that caller set plus its focused
tests. State the selected policy explicitly instead of preserving accidental
ambient behavior. Update the registry's exact admitted caller set in the same
PR. Unrelated cleanup waits.

Deliberate stacking is allowed when two changes edit the same code and cannot
be reviewed or tested independently. Name the dependency, keep one writer per
branch, and rebase the dependent branch after its parent lands. Convenience or
avoidance of a current-main rebase is not a stacking reason.

### Remove compatibility

Once the old caller set is empty, delete the compatibility entry point and its
registry entry in a final small PR. Retain or add the cheapest source-level
regression that prevents the obsolete spelling or behavior from returning.
Close the removal issue only after the final integrated tree passes the old and
new contract's applicable tests. A zero-caller compatibility entry is a failed
audit, not a supported steady state.

### Atomic exceptions

Keep a migration atomic only when no compatibility window can preserve
correctness. At least one concrete condition must hold:

- exposing the old behavior for any caller would retain a security, resource
  ownership, memory-safety or hermeticity defect;
- old and new participants share one representation or protocol boundary and
  mixed versions cannot interoperate;
- an adapter cannot distinguish the old intent without ambiguity or cannot
  reproduce it without weakening the new invariant; or
- every possible intermediate repository state is broken and no narrow adapter
  can make one correct.

The atomic PR description must identify the exact seam, show why a bounded
adapter is unsafe or impossible, and separate that seam from additive API work
and caller groups that can still migrate independently. “One logical change”,
“easier to test together”, or “fewer PRs” are not atomicity arguments. Never
retain unsafe process, descriptor, handle, environment, path, privilege or
validation behavior merely to create a compatibility window.

## Native-retirement generated-state ownership

Ordinary feature branches do not own
`docs/native-retirement-repository-sources-v1.json` or
`tools/native_retirement_dependency_binding.generated.h`. Do not run
`native_retirement_rebind.py refresh` and commit its output merely because
an admitted source changed. The read-only rebinding workflow reconstructs the
exact candidate state in a disposable checkout. The repository ruleset's
required `API migration policy` status is the merge-admission authority. For
retirement-sensitive changes it accepts only a current-main, two-parent
integration head with a successful exact-head
`Native retirement trusted integration` status from `github-actions[bot]`.
When `main` advances that status is invalidated; rerun the protected writer
instead of hand-editing generated state or requiring a manual rebase.

Changes to rebinder/materializer/validator/workflow implementation are a
`bootstrap` transition. Changes to reviewed policy, generated schema, or
consumers are a `policy` transition. Never combine those two classes in one
candidate: land a backwards-compatible bootstrap first. Its new authority
must accept the exact state produced by the old trusted authority before it
can become trusted; then dispatch the separate policy transition. Manual
generated-file edits are rejected for every class. See
[native-retirement rebinding](../native-retirement-rebinding.md).

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
