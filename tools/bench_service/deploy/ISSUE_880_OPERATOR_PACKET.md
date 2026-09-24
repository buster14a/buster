# #880 protected smoke service: operator action and receipt packet

Use this packet only from reviewed protected `main`. Attach actual output and
immutable digests to #880 and #437. An empty field is a failed gate, not an
instruction to fill it from a fixture. Admission remains disabled until the
administrator and host operator separately sign off the complete preflight.
The smoke result has no #511/#512/#36 performance authority.

## 1. Repository administrator

Record the current main commit and tree, the merged #1017 and #1058 identities,
the authenticated administrator and the installed Actions actor policy. Confirm
it permits only the Repository admin role to dispatch the fixed workflow and
does not grant a bot, LLM connector or team independent dispatch authority.
The `benchmark-9700x` environment does not require a second approval; edits to
the workflow, policy and installed gateway still require review under the
repository trust policy.

In a trusted checkout at the recorded main commit, inspect
`GITHUB_ADMISSION.md`, the main and benchmark ruleset policies, installer and
both verifiers. Confirm the one-runner organization group allows this public
repository and only the fixed workflow on `main`, and that the old repository
runner registration is gone. Apply the reviewed installer only with
administrator authorization:

```sh
bash tools/bench_service/deploy/configure_github_admission.sh buster14a/buster
```

The installer first disables dispatch. Do not run it as a read-only probe
during an enabled window. Keep its log and separately retrieved, timestamped
JSON responses for the live `main` ruleset 22537199, the existing admin-only
Actions policy, organization runner group, `Benchmark dispatch main protection`
ruleset, `benchmark-9700x` environment, deployment branch policies and
`BENCH_SERVICE_DISPATCH_ENABLED`. Its post-install read-back verifier must pass
with `value=false`. The main queue must retain eight Actions-bound checks,
non-strict status checks, one-build/one-merge `ALLGREEN` and no bypass. The
benchmark ruleset must protect the exact `main` branch with no bypass, without
a blanket pull-request review requirement; the environment must have no
required reviewer and must allow only the exact `main` deployment branch.

Record the runner registration, its actual runner group, allowed repository
and workflow restrictions, effective labels, account and idle state from
administrator settings. Prove no other workflow can select this host, including
old refs and queued runs. A 403 or inaccessible settings page is **unverified**,
not evidence that a restriction is absent or present. The environment name
in YAML and an idle runner label are insufficient.

## 2. Privileged 9700X operator, before starting the service

Review `README.md`, `VALIDATE_BUSTER_V1.md`, the unit/slice/tmpfiles
references, the installed binary build records, #878's export contract and
the current systemd/polkit configuration. Do not apply tmpfiles to an
existing queue or replace the lease inode. Record and sign off:

| Receipt | Required observation |
| --- | --- |
| Software | Main commit/tree, exact service, build driver, harness, gateway/export and helper SHA-256; bootstrap and dependency identities; installed recipes/profiles and blocked retirement descriptor |
| Sources | Immutable reviewed commit-to-tree-to-manifest mapping; full closure, sorted inventory, file counts/bytes/hashes; source manifest over 4 KiB for the normal attempt |
| Principals | Numeric service, candidate, runner and operator UID/GID/supplementary groups; runner can invoke only the fixed installed gateway, candidate cannot mutate queue, lease, policy or results |
| State | Canonical queue/workspace/result/source/runtime paths, symlink/hard-link checks and permissions, filesystem identity and space; stable lease device/inode observed before and after each attempt |
| Supervisor | Host/boot/kernel/microcode, systemd version, exact service/unit files, fixed system-bus authority, cgroup v2 ancestry and effective CPU/memory/swap/tasks/runtime; real UID/GID and process absence |
| Exclusivity | Drained GitHub and Forgejo jobs, timers, cron, agents, profilers, backups, indexers and manual work from preparation through cleanup; recorded negative audit and no unresolved queue job |

Reject broad `manage-units`, wildcard executable authorization, service
identity for the runner, candidate-selected source/policy, or arbitrary
shell access. Test the installed verifier after a fresh boot, with the service
stopped and host quiet. Preserve the exact read-only commands and their
output, without secrets. The operator must approve a narrow systemd/polkit
configuration for this host; this repository does not install one.

## 3. Protected execution, once both receipts pass

After the administrator and host operator sign off the actual receipts,
explicitly enable admission. An administrator submits only via
`.github/workflows/9700x-service-dispatch.yml` on protected `main`.
Predeclare distinct idempotency keys and record workflow run/job, request
digest, principal, job/attempt, installed inventory and boot/lease identities.
Do not replace the protected entrypoint with SSH or direct local submit.

Preserve five separate real-systemd attempts: (1) normal completion from
a source manifest over 4 KiB, (2) interruption after the prepare manifest,
(3) interruption after a completed stage manifest, (4) restart after bundle
publication and before final manifest, and (5) connected lease-handoff peer
that does not acknowledge. Use reviewed binaries and the real fixed recipe.
Retain each interruption trigger and journal boundary. For each, record the
outer/stage unit and cgroup identity, descendants, lifetime of the same
whole-host lease, cleanup decision and any quarantine. Never turn a partial
attempt into a completed measurement.

For each finalized attempt, use the authenticated `gateway export JOB ATTEMPT
FULL_SHA` boundary, retain the stderr export receipt and exact stdout archive
bytes, publish them to the approved immutable #510 destination, download into
a clean private workspace and run `bench_service unpack-export ARCHIVE
DESTINATION RECEIPT_SHA`. Verify digests and replay the applicable bundle
validators. A workflow result line or an Actions artifact is not a durable
download/replay receipt.

Before lease release or new admission, prove no `.lease-handoff` remains,
no outer/stage unit is active or ambiguously failed, no owned cgroup is
populated and no descendant survives. Record the continuous lease
device/inode and ownership across this proof. Only then demonstrate a
subsequent *new* real reservation and completion. On any ambiguity, keep the
host quarantined and dispatch disabled. After the window, set
`BENCH_SERVICE_DISPATCH_ENABLED=false` and read it back.

## Evidence disposition

Post the immutable installed identities, five attempt IDs, run/job URLs,
authenticated archive and publication/download/replay receipts, recursive
absence and subsequent-reservation proof to #880 and #437. Explicitly list
failed or unavailable gates and their owners. Do not close #880 for this
repository packet or #1017; do not claim #512 or #36 acceptance.
