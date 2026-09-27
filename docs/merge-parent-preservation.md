# Merge-parent preservation guard (#1080, #1189)

## Required gate and branch freshness

The `Workflow lint` job in `.github/workflows/ci.yml` runs the guard after a full-history checkout. The required `CI complete` aggregate depends on that job; the guard therefore uses the existing repository-owned gate. No branch rule, required-check list, merge-queue setting, or strict-freshness setting is changed.

The workflow runs on pull requests, merge groups and pushes to `main`. It checks only newly introduced merge commits up to the exact candidate SHA. A PR event's `pull_request.base.sha` can be older than the first parent of the synthetic merge checked out for that run (#1189). Those identities are not interchangeable. Pull-request runs now bind the range as follows:

1. Read the runner-provided `GITHUB_EVENT_PATH` (or explicit `--event-path` for replay). Require full event base/source SHAs, a valid target branch name, and agreement between `--base` and the event base SHA. The payload's potentially stale `merge_commit_sha` is not candidate authority.
2. Require the candidate to equal the checked-out `HEAD`, a full-history checkout, exactly two parents, and a second parent equal to the event's exact source head.
3. Resolve `refs/remotes/origin/<event base.ref>` once. The existing `actions/checkout` with `fetch-depth: 0` fetches all branches from the base repository into those remote-tracking refs. Both the event base and the synthetic first parent must occur on that fetched branch's **first-parent** history, and the event base must be an ancestor of the synthetic first parent. General reachability alone is insufficient: a merged side-branch commit is not thereby a target-branch tip.
4. Use the verified synthetic first parent, not the stale event base or the later fetched tip, as the scan boundary. Log the candidate, candidate tree, exact source head, event base, target branch, fetched tip and actual range base.

Missing/malformed event data, a missing target ref, rewritten or inconsistent base history, a wrong source parent, a different checkout, or a shallow checkout fails closed. There is no fallback to trusting `candidate^1` without the independent binding. The existing workflow command remains unchanged: its `--base` is the event anchor, and the runner already supplies `GITHUB_EVENT_PATH`.

A stale but conflict-free feature branch remains eligible; it need not contain the current base. If main advances again after the synthetic candidate was created, the recorded candidate can still be checked against its own immutable first parent on fetched base history. That result is **not** admission for the newer combined tree. The normal merge queue validates its own exact combined candidate without requiring a manual branch update.

Merge-group runs retain the existing strict equality between the candidate's first parent and `merge_group.base_sha`; the PR reconciliation does not apply to them. Push, tag and manual-dispatch range selection is unchanged. No API lookup, branch mutation, status publication or retry is performed by this guard.

The regression suite is `python3 -B tools/merge_parent_preservation_test.py -v`. It covers stale event/current synthetic bases, later main advancement, exact-source and first-parent binding, malformed/missing identities, dropped changes, and merge-group controls. With the full checkout used by CI, it also inspects the exact historical #1041 merge `718ee0064bdc3513499784e7522ebad5e39bc29c`.

For an offline PR replay, retain the event JSON and the independently fetched target-branch history, check out the recorded synthetic candidate in a disposable worktree, then run:

```sh
python3 -B tools/merge_parent_preservation.py \
  --candidate <synthetic-candidate-sha> --base <event-base-sha> \
  --event-name pull_request --event-path <retained-event.json>
```

## Detection rule

For each two-parent merge in the candidate-only range, the guard focuses on the failure signature from #1041: the resulting tree is byte-for-byte the first parent's tree.

It then finds the unique merge base and checks whether the second parent has any net tree change from that base. A no-op second parent passes. For changed trees, `git cherry` enumerates ordinary commits unique to the second-parent side; the guard checks both patch-equivalent and non-equivalent rows against the final trees. A commit is ignored if its patch was later reverted on the second-parent side. If reverse-applying the patch to the merge result succeeds, its exact change is present. If the patch cleanly applies forward to the result, it is missing and the check fails. If Git can show neither, the merge is reported as ambiguous and fails for review.

Ordinary merges whose resulting tree differs from the first parent are outside this specific failure signature and pass. Patch-equivalent changes and no-op merges pass. Findings identify the merge, both parents, merge base, and the second-parent commit whose change is missing or ambiguous.

## Coverage and limits

This is a candidate-range check, not a periodic audit of all historical merges. Once a merge is common to the verified range base, it is not re-audited; the full-history regression keeps the known #1041 example covered. A faulty merge newly introduced through the PR source head is still scanned even when the event base was stale.

The check classifies first-parent-equal two-parent merges using non-merge commit patches. Merge-resolution-only changes, criss-cross histories with multiple best merge bases, and changes whose file content has been transformed so Git can neither reverse-apply nor cleanly apply the original patch are not fully classified. Git's patch identity and apply checks are content tests; they do not prove semantic equivalence. Ambiguous patches in a checked merge block the candidate instead of being silently accepted. Octopus merges inside the scanned history are outside scope; a PR or merge-group candidate itself must have exactly two parents.

The check does not change the live ruleset: its existing required status contexts and `strict_required_status_checks_policy: false` remain unchanged. The repository ruleset fixture is covered by a regression assertion so a later workflow edit cannot silently make `CI complete` optional or require branch freshness. Repairing #1189 does not satisfy #1162's independent real-systemd prerequisite or authorize readiness, deployment, or closure of #1159/#880.
