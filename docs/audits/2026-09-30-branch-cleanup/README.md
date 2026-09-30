# Branch cleanup, 2026-09-30

## Scope

Every remote branch present on 2026-09-30 (686 refs, full history, not a
shallow clone) was classified against main `37a2902af`. The result is
[inventory.tsv](inventory.tsv), with one row per branch: name, tip SHA,
`keep`/`delete`, reason, tip commit date, the PRs whose head equals the tip,
and the tip commit's subject.

| Action | Count | Reason |
|---|---:|---|
| keep | 60 | head or base of an open PR |
| keep | 68 | committed on or after 2026-09-28; may belong to an active session |
| keep | 5 | useful unlanded work (see below) |
| keep | 4 | `gh-readonly-queue/*`, `native-retirement-staging/*` (automation-owned) |
| keep | 3 | named by a workflow on main |
| keep | 2 | `main` and this cleanup's branch |
| delete | 138 | tip is an ancestor of main |
| delete | 20 | `git merge-tree` into main yields main's tree unchanged |
| delete | 2 | tip is the head of a merged PR |
| delete | 94 | triaged SUPERSEDED: the change is already on main |
| delete | 38 | triaged OBSOLETE: probe, evidence, transport, rejected approach or older snapshot of an open PR |
| delete | 252 | no production diff against main (`src/`, `build.c`, `CMakeLists.txt`); CI transport, evidence and research scratch |

Every branch whose merge into main would still change production code, and
which fell outside the recent-activity window, went to per-branch triage (137
branches). [triage.tsv](triage.tsv) records the verdict and reason for each
one. The triage compared each diff with current main by reading source. No
branch code was built or run.

## Preservation

The branch `archive/branch-snapshot-20260930` (`a2b56afa8`) is a chain of
commits whose parents are all 529 distinct tip SHAs of the `delete` rows.
Every commit on those branches therefore stays reachable after the refs are
removed. Its tree holds only a copy of the inventory, and it must never be
merged. To restore a branch:

```sh
git fetch origin archive/branch-snapshot-20260930
git push origin <sha-from-inventory>:refs/heads/<branch>
```

Branches whose tips are PR heads also remain under GitHub's `refs/pull/*/head`.

## Useful work kept and reported

| Branch | Commit | Record |
|---|---|---|
| `claude/brave-feynman-1g8df5` | `7d70ae1dd` | [#1598](https://github.com/buster14a/buster/issues/1598): single tree-check registration table |
| `claude/hopeful-bardeen-y44j61` | `7a2f2a77f` | [#1602](https://github.com/buster14a/buster/issues/1602): per-function FAST decline |
| `codex/75-preserve-variadic-al` | `01589059b` | [#2013](https://github.com/buster14a/buster/issues/2013): indirect variadic call clobbers AL |
| `codex/124-gpr-prewarm-candidate-20260924-a` | `e1cd8979d` | [#2017](https://github.com/buster14a/buster/issues/2017) (part of #124): distinct-key GPR prewarm, not measured |
| `audit/parameter-matrix-238-14ebc9aa` | `14ebc9aab` | [#2018](https://github.com/buster14a/buster/issues/2018) (follows #238): allocator × optimization parameter-alignment regression |

## Deleting

The session that made this inventory could create branches but could not
delete refs. A maintainer runs the deletion from a full clone:

```sh
git fetch origin
tools/delete_archived_branches.py docs/audits/2026-09-30-branch-cleanup/inventory.tsv            # dry run
tools/delete_archived_branches.py docs/audits/2026-09-30-branch-cleanup/inventory.tsv --execute
```

The tool deletes a `delete` row only when three conditions hold: the live
branch still points at the recorded SHA, that SHA is reachable from the
archive branch, and the push lease on that SHA succeeds. It holds and reports
every other row. A dry run on 2026-09-30 reported 544 deletable and 0 held.
`--self-test` exercises the checks against throwaway repositories.

A branch that moved after capture is held rather than deleted. Re-inventory
it before making a new decision. `keep` rows in the recent-activity window
are for a later pass, once their sessions have finished.
