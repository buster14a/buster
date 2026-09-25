# Merge-parent preservation guard (#1080)

## Required gate and branch freshness

The `CI complete` job in `.github/workflows/ci.yml` runs the guard after a full-history checkout. The live `main` ruleset already requires `CI complete`; the guard therefore uses that existing repository-owned gate. No branch rule, required-check list, merge-queue setting, or strict-freshness setting is changed.

The workflow runs on pull requests, merge groups and pushes to `main`. It checks only merge commits newly introduced between the event base and the exact candidate SHA. Pull-request and merge-group runs verify that the candidate merge commit's first parent is the event base. A stale but conflict-free feature branch remains eligible: GitHub supplies the current base as the first parent of its candidate merge, and the branch itself is never checked for ancestry from current `main`. The merge queue validates the combined tree without requiring a manual branch update.

The regression suite is `python3 -B tools/merge_parent_preservation_test.py -v`. With the full checkout used by CI, it also inspects the exact historical #1041 merge `718ee0064bdc3513499784e7522ebad5e39bc29c`.

## Detection rule

For each two-parent merge in the candidate-only range, the guard focuses on the failure signature from #1041: the resulting tree is byte-for-byte the first parent's tree.

It then finds the unique merge base and checks whether the second parent has any net tree change from that base. A no-op second parent passes. For changed trees, `git cherry` enumerates ordinary commits unique to the second-parent side; the guard checks both patch-equivalent and non-equivalent rows against the final trees. A commit is ignored if its patch was later reverted on the second-parent side. If reverse-applying the patch to the merge result succeeds, its exact change is present. If the patch cleanly applies forward to the result, it is missing and the check fails. If Git can show neither, the merge is reported as ambiguous and fails for review.

Ordinary merges whose resulting tree differs from the first parent are outside this specific failure signature and pass. Patch-equivalent changes and no-op merges pass. Findings identify the merge, both parents, merge base, and the second-parent commit whose change is missing or ambiguous.

## Coverage and limits

This is a candidate-range check, not a periodic audit of all historical merges. Once a merge is common to the event base, it is not re-audited; the full-history regression keeps the known #1041 example covered.

The check classifies first-parent-equal two-parent merges using non-merge commit patches. Merge-resolution-only changes, criss-cross histories with multiple best merge bases, and changes whose file content has been transformed so Git can neither reverse-apply nor cleanly apply the original patch are not fully classified. Git's patch identity and apply checks are content tests; they do not prove semantic equivalence. Ambiguous patches in a checked merge block the candidate instead of being silently accepted. Octopus merges are outside scope.

The check does not change the live ruleset: its existing eight required status contexts and `strict_required_status_checks_policy: false` remain as read from GitHub. The repository ruleset fixture is covered by a regression assertion so a later workflow edit cannot silently make `CI complete` optional or require branch freshness.
