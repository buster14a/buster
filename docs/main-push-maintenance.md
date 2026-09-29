# Main-push maintenance and supersession

Two required workflows have default-branch side effects in addition to their
candidate checks:

- `API migration policy` invalidates trusted native-retirement integrations
  after `main` advances.
- `Merge conflict preflight` refreshes every open pull request against one exact
  default-branch snapshot.

Their pull-request and merge-group jobs are admission checks. They remain
fail-closed and may cancel an obsolete run for the same candidate. A `push` to
`main` has different semantics: a later merge supersedes the maintenance input,
but it must not turn an otherwise green historical commit into a cancelled run.

## Concurrency contract

For `push`, each workflow uses a concurrency group containing the exact
`github.sha` and sets `cancel-in-progress` to false. Three rapid pushes therefore
create three distinct groups; GitHub cannot cancel the running invocation or
replace an older pending invocation in a shared branch group.

For every other event, the group remains candidate- or event-specific and
`cancel-in-progress` stays enabled. This preserves exact-head admission and does
not convert real failures or arbitrary cancellations to success.

## Reconciliation contract

`tools/main_push_maintenance.py` owns the push-only arbitration:

1. It reads the live `refs/heads/main` identity before doing shared work.
2. An event SHA that is already older is a successful `superseded` no-op. It
   performs no status or check-run publication.
3. A current invocation performs the fixed maintenance action.
4. It reads live `main` again after the action. If the branch moved, it repeats
   the same action against the newer exact SHA. The final promoted evidence is
   only the stable attempt.
5. Reconciliation is bounded to four revisions. Failure to converge is red;
   it is not silently classified as superseded.

A merge-conflict refresh is retryable only when its retained report says the
sole active failure is the existing default-branch-moved recheck. API errors,
publication ambiguity, incomplete inventory, exhausted budget and script/test
failures remain red. Native-retirement invalidation exceptions also remain red.

The helper retains `main-push-maintenance.json`. A superseded push records the
event and current SHAs with zero attempts. A converged run records every
requested, acted-on and subsequently observed SHA. Merge-conflict attempts are
retained in per-attempt directories; only the stable attempt is promoted to the
artifact root and job summary.

Manual `workflow_dispatch` refreshes are not commit-supersession sentinels and
continue to run the ordinary snapshot tool directly.

## Validation

The focused regression is:

```sh
python3 -B tools/main_push_maintenance_test.py -v
```

It covers three rapid main revisions, initial supersession, movement during
both side effects, stable-attempt evidence promotion, generic failure
preservation, and the event-specific concurrency text in both workflows.
