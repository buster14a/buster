# Protected GitHub admission for the Ryzen 7 9700X

The benchmark runner is not a general Actions executor. Repository content must
never be checked out or executed on it. The only Actions path is the literal,
operator-installed gateway in `9700x-service-dispatch.yml`.

The workflow is fail-closed while the repository variable
`BENCH_SERVICE_DISPATCH_ENABLED` is absent or not exactly `true`. Keep it false
until every item below is complete.

## 1. Install the repository controls

Use an administration token with repository Administration write permission.
Choose a user or team other than the dispatcher that can approve benchmark
dispatches and obtain its numeric GitHub ID. From a trusted checkout of
protected `main`, run:

```sh
bash tools/bench_service/deploy/configure_github_admission.sh \
  buster14a/buster User REVIEWER_ID
```

Use `Team TEAM_ID` instead when a team owns admission. The command is
idempotent. It creates or updates:

- the active `Benchmark dispatch main protection` ruleset from
  `.github/rulesets/benchmark-main.json`;
- the protected `benchmark-9700x` environment with required review,
  self-review disabled and one exact `main` deployment branch policy; and
- the repository variable `BENCH_SERVICE_DISPATCH_ENABLED=false`.

The installer first disables dispatch and compares repository ruleset
`22537199` (`main`) with `.github/main-merge-queue.ruleset.json`. A mismatch
stops installation with dispatch disabled. It does not replace that ruleset or
its eight required checks, one-build/one-merge queue, `ALLGREEN` policy, and
no-bypass boundary. The additional benchmark ruleset has no bypass actors.
It blocks deletion and force-push, requires a reviewed pull request with stale
approvals dismissed and the last push approved by another reviewer. It
separately requires `CI complete` and `Benchmark service workflow policy`.
Its non-strict status checks preserve the queue's combined-tree validation policy.

Read back both active rulesets after installation. Verify the benchmark
environment has an independent required reviewer, self-review disabled, and
only the exact `main` deployment branch policy. The policy selects the branch
already protected by the active repository merge-queue ruleset; it does not
rely on GitHub's separate legacy branch-protection setting. If no distinct
reviewer is available, leave dispatch disabled and do not submit a job.

## 2. Verify the host boundary before activation

Do not activate manual dispatch until all of the following are true:

1. The privileged installer/verifier has installed the reviewed service,
   service user/group, stable lease inode, system-bus or sudo authorization,
   cgroup limits and immutable recipe/profile identities.
2. The installed verifier succeeds after a fresh boot and records the expected
   host identity, boot ID, CPU topology, scheduler settings and cgroup ancestry.
3. No queued or running Actions job other than
   `9700X benchmark service dispatch` targets either benchmark label.
4. `python3 -B tools/bench_service/workflow_policy_test.py` passes on protected
   `main`.
5. `zen5-audit.yml` is absent and the runner has no repository checkout from a
   prior job.
6. The service queue has no unfinished job or unresolved reconciliation.
7. A reviewer other than the dispatcher can approve the protected environment.

## 3. Activate and dispatch

After recording the verification evidence, enable the workflow explicitly:

```sh
gh variable set BENCH_SERVICE_DISPATCH_ENABLED \
  --body true --repo buster14a/buster
```

Dispatch only full lowercase commit IDs and a stable idempotency key. The fixed
gateway performs an atomic idle-only submission: an exact retry returns the
existing job, a changed request with the same key conflicts, and a new request
waits until all previous work and reconciliation are complete.

Disable admission immediately after qualification or on any drift:

```sh
gh variable set BENCH_SERVICE_DISPATCH_ENABLED \
  --body false --repo buster14a/buster
```

Changing the workflow, ruleset payload, installer, gateway binary, recipe or
profile requires a new reviewed installation and verification before
re-enabling dispatch.
