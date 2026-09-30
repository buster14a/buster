# #880 protected smoke service: operator action and receipt packet

Use this packet only from reviewed protected `main`. Attach actual output and
immutable digests to #880 and #437. An empty field is a failed gate, not an
instruction to fill it from a fixture. Admission remains disabled until the
administrator and host operator separately sign off the complete preflight.
The smoke result has no #511/#512/#36 performance authority.

## 1. Repository administrator

Record the current main commit and tree, the merged #1017 and #1058 identities,
the authenticated administrator and the installed Actions actor policy. Confirm
it permits exactly Repository admin, `davidgmbb`, ChatGPT Codex Connector,
Claude and Devin.ai Integration to request the fixed workflow through manual
dispatch. Confirm `davidgmbb` has repository admin permission. No one approves
or releases the job: `benchmark-9700x` has no required reviewer or wait timer,
and the dispatch workflow's `authorize` and `submit` jobs admit only a run
whose actor and triggering actor are `davidgmbb` by login and numeric ID
39247043, verified per run attempt, so re-runs by anyone else are skipped.
The other permitted requesters may start a run, but it is skipped before the
self-hosted runner. Edits to the workflow, policy and installed gateway still
require review under the repository trust policy.

In a trusted checkout at the recorded main commit, inspect
`GITHUB_ADMISSION.md`, the main ruleset policy, requester policy and read-only
preflight. Confirm the one-runner organization group allows this public
repository and only the fixed workflow on `main`, and that the old repository
runner registration is gone. Run the reviewed preflight with settings read
access:

```sh
bash tools/bench_service/deploy/configure_github_admission.sh buster14a/buster
```

Apply the settings transition in two separate steps. **Step 1:** once the
workflow gate is on protected `main`, remove the required reviewer from
`benchmark-9700x`, keep its one `main` deployment branch rule, add no wait
timer, and leave `BENCH_SERVICE_DISPATCH_ENABLED=false`; then run the preflight
above with the variable still `false`. **Step 2** follows in section 3, only
after the host operator's readiness post.

The preflight first reads back disabled dispatch and leaves the variable
unchanged. Explicitly set it to `false` before checking if it is absent;
do not run it during an enabled window.
Keep its log and separately retrieved, timestamped
JSON responses for the live `main` ruleset 22537199, the existing requester
Actions policy, organization installation-to-app mapping, organization runner
group, `benchmark-9700x` environment,
deployment branch policies and `BENCH_SERVICE_DISPATCH_ENABLED`. Its read-back
verifier must pass with `value=false`, and no superseded benchmark branch
ruleset may be present. The main queue must retain eight Actions-bound checks,
non-strict status checks, 4-build/one-merge `ALLGREEN` and exactly the two
reviewed standing bypass actors, Repository admin (role 5) and `davidgmbb`
(user 39247043), both in `always` mode. The environment must have no
required reviewer, wait timer or other approval rule and must allow only the
exact `main` deployment branch. The preflight must also read back live
`main`'s dispatch workflow byte-identical to the reviewed checkout and pass
the workflow policy test, which pins the per-attempt actor gate.

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
| Software | Main commit/tree, exact service, build driver, harness, gateway/export, systemd broker, static credential gate, static broker entry gate and helper SHA-256; both gates have no ELF INTERP/DYNAMIC and no executable stack; root-owned `0444` numeric account receipt and its SHA-256; bootstrap and dependency identities; installed recipes/profiles and blocked retirement descriptor |
| Sources | Immutable reviewed commit-to-tree-to-manifest mapping; full closure, sorted inventory, file counts/bytes/hashes; source manifest over 4 KiB for the normal attempt |
| Principals | Authenticated numeric service, candidate, runner and operator UID/GID/supplementary inventory; actual service/runner process real/effective/saved/filesystem UID/GID, Groups, Cap* and NoNewPrivs; account and authorization sources stable for the whole lease; runner can invoke only the fixed installed gateway, candidate cannot mutate queue, lease, policy or results |
| State | Canonical queue/workspace/result/source/runtime paths, symlink/hard-link checks and permissions, filesystem identity and space; stable lease device/inode observed before and after each attempt |
| Supervisor | Host/boot/kernel/microcode, systemd version, exact service/broker/socket/unit files, root-owned stable-lease identity, fixed system-bus authority, cgroup v2 ancestry and effective CPU/memory/swap/tasks/runtime; real UID/GID and process absence |
| Exclusivity | Drained GitHub and Forgejo jobs, timers, cron, agents, profilers, backups, indexers and manual work from preparation through cleanup; recorded negative audit and no unresolved queue job |

Reject broad `manage-units`, wildcard executable authorization, service
identity for the runner, candidate-selected source/policy, or arbitrary
shell access. Test the installed verifier after a fresh boot, with the service
stopped and host quiet. Preserve the exact read-only commands and their
output, without secrets. The operator must approve a narrow systemd/polkit
configuration for this host. The reviewed candidate broker and exact operator
checks are in [SYSTEMD_BROKER.md](SYSTEMD_BROKER.md); these references do not
install or approve it.

## 3. Protected execution, once both receipts pass

**Step 2:** only after the host operator posts readiness matching this exact
installation packet and the administrator has signed off the step 1 preflight
receipts, the administrator sets `BENCH_SERVICE_DISPATCH_ENABLED=true` and
reads it back. `davidgmbb` then starts only
`.github/workflows/9700x-service-dispatch.yml` on protected `main`; the
`authorize` job verifies that run attempt and `submit` runs the fixed gateway
without a manual approval step. Re-run with **Re-run all jobs**; a partial
re-run skips `submit`.
Predeclare distinct idempotency keys and record workflow run/job, the
`authorize` job and its verified attempt, request digest, principal,
job/attempt, installed inventory and boot/lease identities.
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
