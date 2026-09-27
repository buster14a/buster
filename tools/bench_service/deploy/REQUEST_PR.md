# Resolving an ordinary PR benchmark request (#1192)

This procedure runs on a trusted workstation or hosted control job, away from
the 9700X runner. It records a proposed immutable request; it does not admit
source, approve the protected environment, or submit a service job. The
ordinary recipe in #1190 is currently blocked in production. Keep
`BENCH_SERVICE_DISPATCH_ENABLED=false` until that recipe and the host have been
qualified under [GitHub admission](GITHUB_ADMISSION.md).

For a same-repository PR targeting protected `main`, resolve the PR head and
the current PR base snapshot with:

```sh
python3 -B tools/bench_service/resolve_pr_request.py \
  --repository buster14a/buster --pr PR_NUMBER \
  --idempotency-key UNIQUE_KEY > request.json
```

`--baseline-commit FULL_SHA` selects another explicit immutable baseline.
The reader queries GitHub for the PR, both exact commit objects and their
trees, then rereads the PR. It rejects a push or base advance during that
window, a fork or deleted head repository, a closed PR, an unexpected base
repository/branch, malformed IDs, and substituted commit/tree responses.
Retry a changing PR with a new key. The record freezes PR/base/head commit
and tree IDs, recipe, workload policy, key and a canonical request digest.
It always says `source_admission=required` and `dispatch_ready=false`.

The source-inventory producer must separately prove the complete immutable
Git and build/dependency closure and install only reviewed bytes through the
operator-controlled path. The current resolver does not produce or install
that inventory. An exact merge-group candidate also needs a separate record
binding event base/head, queue ref and constituent PR source identities;
do not substitute an exploratory PR-head record or a disappearing synthetic
ref for that evidence.

Once the inventory, ordinary recipe, protected-main workflow and host are
qualified, use the approved GitHub Actions workflow-dispatch UI or `gh
workflow run 9700x-service-dispatch.yml --ref main --field ...` with the
*recorded immutable* base/candidate commits and key. An available connector
may invoke that same approved workflow API; its integration identity must be
on the reviewed requester list. GitHub verifies the actual actor. A maintainer
using their own account cannot approve their own protected-environment run;
administrative bypass is a separate recorded release, not an approval receipt.
Never give the 9700X runner a PR number, mutable branch, checkout, token,
source installer or arbitrary command.

Use the same key only to retry the exact same typed request. The service
returns the same logical job for identical retries and rejects a changed
request under the same key. Use a new key for an intentional new measurement
attempt. Keep the GitHub run ID, service job/attempt receipt and any explicit
rejection or withdrawal together. GitHub `queue: max` retains at most 100
pending runs; overflow cancellation and environment waits are visible in
Actions. It does not promise FIFO service admission. The service has its own
eight-pending, 64-lifetime limits and authoritative host lease. Cancelling a
workflow or losing its client connection does not release an active service
reservation; use the authenticated result or cancellation receipt to determine
what happened.

The fixed workflow currently submits only the smoke recipe. Do not use its
present inputs to claim an ordinary throughput result. The reviewed gateway
enum, inventory handoff, status retention and exact integration-candidate
request path must land before this procedure can dispatch #1190 comparisons.
