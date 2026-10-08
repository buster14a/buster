# Forge, issues, and pull requests

[Agent instructions](../../AGENTS.md) · [Research lifecycle](research.md) · Paths and commands below are relative to the repository root.

## Forge, issues, and pull requests

Use the repository and host named by the user; a GitHub URL names the GitHub
repository, not a Forgejo task. Otherwise inspect the current git remote.
Active CI and merge admission are defined under `.github/workflows/`; check
the live checks on the submitted commit. The `.forgejo/` implementation and
source-free GitHub runner broker were removed in commit
`02c0400a34d04be9e984f29a59291750b3998d3f`. Their
[retirement record](../ci-github-hosted-runners.md) links historical material,
not current setup or validation commands.

The issues open on 2026-08-31 were copied to `buster14a/buster` on GitHub as
part of the migration. Numbers did not survive the copy, so historical
references in the topic guides, code comments, and commit messages may name a
**Forgejo** number: resolve those through `docs/issue-migration-map.md`, or
through `docs/forgejo-issue-archive.md` for issues already closed by then.
Do not remap an issue identified by a current GitHub URL.

**Issues are the task queue.** Work that is real but not being done right now
becomes an issue, not a paragraph in an audit that nobody will find — a chip
filed against a memory is invisible to the next agent, while an issue is
something a fresh session can pick up cold. An agent that encounters a separate
actionable problem reports it during the task, before session end or handoff:
search open and closed issues/PRs for the root cause; comment with fresh evidence
on the matching record, or file a new issue if none exists. A finding fixed in
the active PR belongs in that PR's description and regression evidence; link an
existing issue if one tracks it. Group symptoms with the same root cause in one
record and separate independent problems. Report a blocker on its owning issue
or PR as soon as it changes the next action. Do not open a new issue for every
flaky retry or known duplicate. When access prevents publication, preserve a
ready-to-post body and explicitly identify the unposted report in the handoff.

Write the body as a **prompt**: what
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

## Parallel sessions on one machine

Each session that builds, tests or measures takes its own source worktree.
One writer per branch also means one owner for that worktree's generated build
and result paths: different build directories in a shared checkout do not
protect a compiler reading source while another session edits it. For a
committed subject revision, start from the repository that owns the worktrees:

```sh
subject_revision=$(git rev-parse HEAD)
session_root=$(mktemp -d "${TMPDIR:-/tmp}/buster-session.XXXXXX")
session_root=$(cd "$session_root" && pwd)
git worktree add --detach "$session_root/src" "$subject_revision"
cd "$session_root/src"
```

Keep this session's frozen binaries and captures under `"$session_root"`, and
record the exact revisions and paths in its issue claim or shared ledger. The
[A/B benchmark recipe](benchmarking.md#benchmarking-a-compiler-change-ab) builds
both revisions serially in one owned path and freezes the workload before
sampling; it also documents the controls needed when build roots differ. The
[work-ledger recipe](../work-ledger.md#build-and-read) uses a separately named
diagnostic tree. A session changing source uses its claimed branch in its own
worktree; building or measuring a committed revision can stay detached.

`generate` still deletes its selected build directory without a live-use lock.
Regenerate or remove only paths owned by this session, after
all builds, tests, self-host stages, compilers and profilers using them finish.
A session name alone is not evidence that a process has stopped. If ownership
or live use is uncertain, preserve the path and resolve it with its owner.
Retain captures and provenance before cleanup; remove the worktree with
`git worktree remove` after its users finish instead of recursively deleting
shared build directories or fixed paths under `/tmp`. See the
[build guidance](build.md) for destructive-generation behavior.

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
required `Native retirement merge admission` check is the merge-admission
authority. `API migration policy` remains a separate compatibility check.

An ordinary PR that changes admitted sources needs no writer step. It queues
like any other PR, and queue admission requires the ephemeral reconstruction
of its exact group tree. Afterwards an automatic catch-up PR
(`native-retirement/catch-up`) publishes the regenerated pair for `main`.
Leave that PR to the automation: do not edit it, push to it or merge it by hand.

`bootstrap` and `policy` transitions still need the writer first. For them,
admission accepts only a current-main, two-parent integration head with a
successful exact-head `Native retirement trusted integration` status from
`github-actions[bot]`. When `main` advances that status is invalidated; rerun
the protected writer instead of hand-editing generated state or requiring a
manual rebase.

Changes to rebinder/materializer/validator/workflow implementation are a
`bootstrap` transition. Changes to reviewed policy, generated schema, or
consumers are a `policy` transition. Never combine those two classes in one
candidate: land a backwards-compatible bootstrap first. Its new authority
must accept the exact state produced by the old trusted authority before it
can become trusted; then dispatch the separate policy transition. Manual
generated-file edits are rejected for every class. See
[native-retirement rebinding](../native-retirement-rebinding.md).

## Merge-conflict preflight

The `merge-conflict-preflight` status is a cheap read-only answer for one exact
triple: current `main`, candidate head and merge base. Its description embeds
the full main and head SHAs, plus a trailing `q=queued` when the trusted job
read that exact head as queued; the retained JSON also records their trees,
every merge-base SHA/tree and the exact combined tree when clean. A result for
`m=<old-main> h=<head>` is not authoritative after `main` moves, even when the
same head still shows a green status. The default-branch refresh rewrites the
status for open PRs, and the later merge-group admission path must validate its
own exact combined head rather than reuse a historical PR-head result.

The default-branch sweep retries selected transient GET failures at most three
times within a 240-second refresh budget, using bounded backoff and server
rate-limit timing when safe. Status POSTs are sent once: a timeout after a write
may leave publication uncertain. A per-PR lookup failure retains an error JSON
and allows later independent PRs to be checked; a systemic rate limit or spent
budget leaves the rest unattempted. The retained `refresh.json` and job summary
count completed, failed, and unattempted work. Incomplete coverage exits 2;
ordinary blocking PR conflicts do not fail the main-push sweep. A missing
lookup never authorizes a status, and a green result for an older main remains
stale until that exact PR is successfully refreshed.

The preflight never checks out, rebases, merges or updates a PR branch. It uses
`git merge-tree` plumbing, reports every unmerged path and Git's conflict kind,
and does not choose `ours`, `theirs`, a union driver or a semantic resolution.
Respond to its numbered classification exactly as follows:

1. **Generated/integration-owned workflow violation.** Remove
   `docs/native-retirement-repository-sources-v1.json` and/or
   `tools/native_retirement_dependency_binding.generated.h` from the PR. Do not
   hand-resolve hashes or refresh generated state on the feature branch; rerun
   ephemeral validation and let the automatic catch-up (or, for a trust
   transition, the trusted writer) publish the integrated result.
2. **Genuine source overlap.** Stop the expensive matrix and inspect the exact
   named paths. Choose an intentional order, rebase or explicit stack; preserve
   both changes where required, run the affected focused tests, then let the
   preflight evaluate the new immutable head. Never auto-resolve merely because
   textual hunks appear disjoint.
3. **Clean but stale.** Do not rebase solely to make the branch pointer current.
   Keep the clean branch directly mergeable under the loose-check policy, but
   require validation of the reported exact combined tree (or the later
   merge-group head). Rerun that validation whenever either main or the head
   SHA changes.
4. **Policy/schema/trust-boundary overlap.** Use the reviewed native-retirement
   bootstrap/policy transition procedure. Do not let candidate-modified trust
   code approve its own output, and do not combine a trust implementation
   bootstrap with its dependent policy/schema transition. Resolve ordering and
   the applicable exact-head authorization before running expensive acceptance
   again: independent review by default, or the explicitly configured admin
   dispatch in the documented solo-maintainer policy.

**Dequeue a conflicted queued PR before pushing its fix.** GitHub keeps a
queued PR that starts conflicting with `main` in the queue, and it refuses
every push to that PR's branch with `GH006` ("Branches that are queued for
merging cannot be updated"). Any of these signals means the PR is in that
state: a conflicted status that ends in `q=queued`, a preflight summary that
names the PR as queued, or that push error. Dequeue the PR first, with
**Remove from queue** or the GraphQL `dequeuePullRequest` mutation. Then push
the resolution and re-enqueue the PR after its checks pass. Do not retry the
push or use a queue bypass. If you cannot dequeue the PR, say so on the PR.
Nothing dequeues it automatically; see
[queued PRs that start conflicting](../merge-queue-admission.md#queued-prs-that-start-conflicting).

For a local diagnosis with already-fetched immutable commits, run:

```sh
python3 -B tools/merge_conflict_preflight.py analyze \
  --repo . --main <main-sha> --head <head-sha> \
  --summary /tmp/merge-conflict-preflight.md --fail-on-blocking
```

This command may write ordinary Git merge-tree objects to the local object
database, but it does not change refs, the index, the worktree or either input
branch. The hosted trusted job records the same machine-readable report without
executing candidate code.

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

## Serialized main integration rollout

`Main integration admission` distinguishes PR readiness from exact merge-group
admission. The group checker executes from the immutable trusted base, binds
all five required core workflows to the group SHA and latest attempt, and rejects
stale main/group identities. It delegates retirement admission to the existing
trusted gate; it is neither another writer nor a replacement queue.
Sensitive groups must have the full tree of a successfully published attested
integration head. The gate rechecks the live PR, latest status and writer attempt.
After main advances, a fresh authorized dispatch recovers the original source
from verified writer output and regenerates against current main; enqueue the
replacement head after its checks pass. No manual generated-file repair is needed.

The main merge queue was enabled and read back on 2026-09-22 after #945 landed.
All seven documented admission checks are required from GitHub Actions. The
dedicated Self-host fixed point audit runs after each exact main push and is
outside PR/queue admission (#3045); ordinary self-host matrix coverage remains
required. The repository
contract permits up to 6 speculative combined-head builds (raised from 4 by
#2012 after #1805 lowered it from 20) while allowing only one validated candidate
to merge at a time.
`ALLGREEN` requires every queued group's checks, and `MERGE` retains merge
commits. The initial activation had
one build slot and no bypass; the administrator must read back the current live
settings before relying on them. On 2026-09-24 the administrator added two
standing `always` bypass actors to the live main ruleset: Repository admin
(role 5) and `davidgmbb` (user 39247043). The reviewed contract now expects
exactly these actors; a bypass action is not passing queue evidence. Live
acceptance is tracked in #867 and remains
distinct from activation. Keep
`strict_required_status_checks_policy: false`: a conflict-free branch does not
need a manual update just because main advanced. Use the queue to validate the
new combined tree; never reuse the original PR-head green as that evidence. The required-check audit,
fork and cancellation policy, activation/read-back steps, remaining live
acceptance tests, and emergency restrictions are in
[serialized main integration](../merge-queue-admission.md). Land the repository
build-limit policy through the old trusted-base queue contract before raising
the live build limit; the exact rollout and read-back are documented there.
No manual success status or temporary bypass is authorized by that guide.
