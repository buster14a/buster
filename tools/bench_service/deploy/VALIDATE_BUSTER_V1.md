# First authenticated smoke deployment gate

This checklist is for the first real `validate-buster-v1` execution on
`buster-zen5-9700x`. It is not an installer or a deployment approval. Stop before
submission if an identity, authorization, host fact or cleanup proof is missing.
Keep #437, #880, #512 and #36 open. Keep `native-retirement-performance-v1` blocked.
One-pair smoke observations are not A/A qualification or a performance verdict.

Read [the service contract](../README.md), [deployment references](README.md),
[benchmarking guidance](../../../docs/agents/benchmarking.md) and issues
[#437](https://github.com/buster14a/buster/issues/437),
[#880](https://github.com/buster14a/buster/issues/880),
[#422](https://github.com/buster14a/buster/issues/422),
[#426](https://github.com/buster14a/buster/issues/426) and
[#512](https://github.com/buster14a/buster/issues/512). Resolve current main's
full commit and tree and check active service PRs before changing the inventory.

## Repository prerequisites that provisioning cannot substitute for

- The typed `client` and fixed `gateway` reach the authenticated socket, but
  do not grant the Actions runner permission to use the service identity.
  A reviewed operator authorization boundary is still required. The installed
  `gateway` fixes the socket, principal and smoke recipe; it accepts only
  bounded keys, full source identities, numeric job IDs and log cursors. The
  service checks the immutable installed inventory under the host lease.
  The runner must not become `buster-bench`, gain
  queue/lease write access, or receive a shell or unrestricted `sudo` under that
  identity. Authorize only the fixed installed `gateway` subcommand, with no
  environment or executable override. Never expose direct `submit`, `protocol`, `rpc`,
  `materialize`, `worker-run` or `workspace-reconcile` to Actions.
- The bounded authenticated exporter from #878 is available as `gateway
  export JOB ATTEMPT FULL_SHA`. It selects only the authenticated finalized
  job/attempt and expected full-result digest, never a caller-supplied path,
  and returns a digest-bound receipt plus the complete immutable result archive.
  Capture stdout as binary, check the exit status before publishing it, and
  retain the `export-receipt-sha256` printed on stderr. `gateway result` and
  journal logs are receipts, not downloaded result bytes. See
  [the export contract](../EXPORT.md).
- Reconstruct an exported archive in a new private destination with
  `bench_service unpack-export ARCHIVE DESTINATION RECEIPT_SHA`. Use a new
  absolute destination whose parent is private and trusted. That command
  reruns the exhaustive manifest, bundle and full-result validators without
  opening the original host path or executing downloaded content. Retain the
  sealed service export, the exact downloaded copy, the exporting/replay tool
  identities and every separately bound lifecycle control. Durable publication
  under #510 remains operator work; an Actions artifact alone is not sufficient.
- `.github/workflows/9700x-service-dispatch.yml` is the reviewed smoke
  submission path. It is manual, main-only, protected-environment gated and
  fail-closed unless `BENCH_SERVICE_DISPATCH_ENABLED` is exactly `true`. It
  performs no checkout and invokes only literal installed `gateway
  capabilities`, `gateway submit` and `gateway result` commands. Keep it
  disabled until the repository controls and host verifier in
  `GITHUB_ADMISSION.md` are complete. It does not invoke `gateway export` or
  publish result bytes; use only a separately reviewed fixed operator path for
  that transfer rather than granting the runner broader service or host access.

The remaining authorization, durable-publication and physical-host evidence
are qualification work, not permissions that an operator should compensate for
by broadening account or socket access.

## One-time operator provisioning

Perform this work in a maintenance window with admission stopped and all prior
outer/stage units and recursive cgroups reconciled. Do not install binaries,
change permissions, copy a live queue, or replace a lease while work is active.
The tmpfiles reference describes a **new installation**. An existing deployment
requires a separately reviewed layout transition that preserves ownership and
the recorded stable lease inode; this document authorizes no automatic migration.

### Identities and paths

Record numeric UID/GID values and supplementary groups, not only account names.
`buster-bench` is both the trusted service and trusted baseline-build identity;
there is no separate `benchmark` account in the compiled recipe. Provision
`buster-bench-candidate` as a different non-login user and primary group. The
trusted account needs membership in the candidate group for staging access.
The candidate and runner must not belong to the service group or obtain system
manager authority. Apply that membership to the accounts used by transient
units as well as the long-lived unit.

| Object | Required new-installation contract |
| --- | --- |
| `/usr/local/libexec/buster-bench-service` | Reviewed service binary; operator-owned, executable, not writable by any service/candidate/runner identity |
| `/usr/local/libexec/buster-bench-build` | Reviewed Clang-built native `build.c` driver with a nonexecutable `GNU_STACK` program header (`RW`, no `E`); same executable ownership rule |
| `/usr/local/libexec/buster-bench-throughput` | Reviewed prebuilt native harness; same executable ownership rule |
| `/opt/buster-bench/installed` | Root/service-owned, no write bits, service-readable, no symlink components |
| `installed/recipes/validate-buster-v1.recipe` | Exact repository recipe bytes, read-only regular single-link file |
| `installed/sources/REVISION/source.manifest` | Exact `BQ-SOURCE-V1` manifest and reviewed source closure; read-only |
| `/var/lib/buster-bench` | `buster-bench:buster-bench-candidate`, `0710`; candidate traversal only |
| `/var/lib/buster-bench/queue` | `buster-bench:buster-bench`, `0710`; service-group traversal only, local durable filesystem |
| `queue/worker-JOB`, `queue/worker-instance-JOB` | `buster-bench:buster-bench`, `0440`; broker-readable single-link records |
| `/var/lib/buster-bench/lease` | `buster-bench:buster-bench`, `0710`; service-group traversal only |
| `/var/lib/buster-bench/lease/host.lock` | Pre-provision once, `0640`, `buster-bench:buster-bench`, regular, one link; record device/inode; never truncate, unlink, replace or age it |
| `/var/lib/buster-bench/workspaces` | `buster-bench:buster-bench-candidate`, `02710`; SGID and candidate traversal, no group write |
| `workspaces/job-JOB-attempt-TOKEN` | Materializer-owned; disjoint base/candidate source/build trees, candidate staging only |
| `workspaces/results` | `buster-bench:buster-bench`, `0710`; service-group traversal only |
| `workspaces/results/job-JOB-attempt-TOKEN` | `buster-bench:buster-bench`, `0700`; service-private durable evidence outside removable attempt tree |
| `/run/buster-bench/control.sock` | Service-created Unix seqpacket endpoint under systemd-owned runtime directory, `0700`, service UID/GID |

The service-group-only lease subdirectory is deliberate: candidate traversal
of the state parent must not expose the lease. A `0700`
workspace without SGID fails `bq_workspace_root_directory`; a `0700` state
parent blocks candidate traversal even if the workspace itself is correct.
Do not grant candidate write permission to either parent to fix an access error.
With `RestrictSUIDSGID=yes`, verify that the materializer inherits SGID from
the `02710` workspace parent when it creates an attempt, both `02710` subject
directories, `02750` source directories and `02700` build directories. The
trusted driver must likewise inherit `02770` for candidate staging and
throughput output. Exact owner, candidate-group identity and mode checks must
pass without requesting SGID in `mkdir` or `chmod`; a mismatch stops admission.

The broker template uses root UID, service primary GID, candidate
supplementary GID and empty capability sets. Read back those exact groups and
the queue, result and lease modes before starting the socket. A TCC bootstrap
driver with no `GNU_STACK` header cannot run inside the transient unit's
`MemoryDenyWriteExecute=yes` sandbox: glibc's stage `posix_spawn` requests a
writable and executable stack and fails with `EACCES`. The installed recipe
driver must be independently built and reviewed from the selected source with
Clang; verify its `GNU_STACK` header is `RW` without `E` before installation.

The long-lived reference uses `Type=exec` and `serve`, with `Restart=no`.
`RuntimeDirectory` manages only the socket directory. It does not reconcile
queue state or clean sibling transient units. No `.socket` activation unit is
supplied for the service control endpoint: `serve` binds its own endpoint and
rejects a pre-existing socket. The separate root systemd broker has a
socket-activated template service.
Do not blindly unlink a stale endpoint; establish daemon/instance absence and
preserve the journal before an operator-authorized restart.

### Immutable inventory and source closure

For each executable record SHA-256, source commit/tree, build invocation,
bootstrap/toolchain and dependency identities. Resolve and record executable
dependencies selected by the fixed build driver, including Clang, CMake and
Ninja, and the effective environment. A root-owned executable pathname alone
does not establish the identity of the program installed there.

Record both profile SHA-256 values and confirm that the retirement descriptor
is still a blocked descriptor, never an executable `.recipe`. Independently
bind each allowed subject's commit to its Git tree and source manifest digest.
The materializer checks source hashes and a revision string; it does not prove
that a human-labelled revision is that Git commit's tree. The installation
receipt must supply that relation. Include the complete fixed build's source
closure, generated inputs and dependencies; do not omit files to meet bounds.

`BQ-SOURCE-V1` allows at most 64 KiB of manifest, 4,096 sorted unique entries,
64 MiB per file, 512 MiB per snapshot and 480 copied directories. Hash and
validate the real closure before publication. A size violation blocks this
deployment; do not increase limits or substitute a partial source tree merely
to make a smoke run pass. No fetch or branch resolution belongs to the gateway.

### System manager and host policy

The reference slice, outer worker and nested build-driver policy must agree:

| Property | Current compiled/reference requirement |
| --- | --- |
| Slice | `buster-bench.slice` |
| CPU | Logical CPU `2`; also `BENCH_SERVICE_RECIPE_CPU` in `build.c` |
| Memory | `8589934592` bytes |
| Swap | `0` bytes |
| Tasks | `256` |
| Per-unit runtime | `3600000000` microseconds |
| Stop policy | `KillMode=control-group`, `SendSIGKILL=yes`, `TimeoutStopSec=10s` |
| Outer identity | Explicit `buster-bench` UID and primary GID |
| Nested identities | Baseline: `buster-bench`; candidate/throughput: `buster-bench-candidate` |

CPU 2 is a reference requirement, not an assertion about this physical host.
Reject the deployment if topology or effective ancestor restrictions do not
support it. A different CPU requires reviewed, matching driver/unit/slice
identities; changing only `serve`'s CPU argument is insufficient. Preserve the
existing sandbox properties and exact checks in `bq_worker_observed` and
`bench_service_recipe_sandbox_process_add`.

An operator must review and install the constrained broker and exact socket,
template service and lease-identity receipt described in
[SYSTEMD_BROKER.md](SYSTEMD_BROKER.md). Merely matching a unit-name prefix or
granting the entire `manage-units` action does not constrain arbitrary
transient service properties or executable selection. Do not install such a
broad polkit rule. Record which identity may start, observe, signal and collect
each fixed unit and prove that direct manager writes remain denied to the
service, candidate and runner. If that cannot be demonstrated, leave the
service stopped. Candidates and the Actions runner must not gain broker access.

Record kernel, boot ID, systemd version, cgroup-v2 mount identity, CPU model,
logical/physical topology and SMT siblings, microcode, RAM, governor/driver,
boost/EPP availability, CPU affinity, effective ancestor CPU/memory/swap/task
limits, quota/throttling state, and relevant services/timers/power policy.
Distinguish unsupported optional sensors from missing required facts. Verify
filesystem durability and free space. None of these facts can be inferred from
the `ryzen-9700x` runner label or from this repository's example values.

### GitHub and competing execution

Restrict the runner to the reviewed service workflow and a protected manual
main dispatch. Provision and verify the environment's actual review/branch
rules; an `environment:` name in YAML can create an unprotected environment.
See [GitHub's environment documentation](https://docs.github.com/en/actions/how-tos/deploy/configure-and-manage-deployments/manage-environments).
Require immutable action SHAs for any action use, minimal permissions, and
`persist-credentials: false` for any checkout. The fixed dispatcher has no
checkout, sets `permissions: {}`, and selects both the restricted group and
labels exactly:

```yaml
runs-on:
  group: buster-9700x-service-dispatch
  labels: [self-hosted, Linux, X64, buster-zen5, ryzen-9700x]
```

Do not expose that group or those labels to a `pull_request` job.

The reviewed workflow must not execute checked-out candidate code. All service,
driver, harness, source and policy identities come from the installed inventory.
Validate typed input values before transport; pass no request expressions into
shell source. A workflow concurrency group is not whole-host ownership.

Before activation, verify `.github/workflows/zen5-audit.yml` is absent from
protected `main` and no queued or running job from an older ref targets either
benchmark label. Drain any previously admitted execution and verify
runner-group/workflow restrictions outside YAML; changing only `main` does
not cancel older branch workflow runs. Identify and stop competing
Forgejo/manual/agent execution through the operator's maintenance procedure.
The retired audit workflow must not be restored or used as a host probe or
substitute service dispatcher.


## First execution and controlled recovery

Use an approved run plan with a unique retained key per intended job. No
retry-until-green loop. Same-key transport retries may recover an uncertain
acknowledgment; never change the key to hide a failed or unknown attempt.

1. Through the fixed gateway, submit one `validate-buster-v1` request with two
   allowlisted full source IDs. Retain the canonical request, durable job ID,
   attempt token and authenticated receipt. Record lease acquisition before
   reservation/materialization and separate identity-bound workspaces.
2. Retain observed outer and all five nested units (`base-generate`,
   `base-build`, `candidate-generate`, `candidate-build`, `throughput`), their
   UID/GID, invocation IDs, boot identity, properties and descriptor-bound
   cgroup identities. Verify the candidate has only its staging write surface.
   Its inability to write queue/lease/inventory/final candidate/results must be
   demonstrated through the reviewed containment procedure, not an uploaded
   candidate script or a root-run probe.
3. The harness invocation remains exactly the recipe's `--profile smoke
   --mode all --pairs 1 --warmups 1 --no-guard --service-output` selection.
   The final option makes completed candidate-owned output readable and its
   directories removable through the trusted service's candidate-group
   membership before private result copying and workspace cleanup.
   This recipe builds Release `ide` with tests disabled; it does not run the
   compiler correctness suite or self-host fixed point. Record those as
   checks of the exact revision,
   never infer them from smoke success.
4. Retain success or failure evidence. Require exhaustive `BQ-BUNDLE-V1`
   replay: 4,096 entries, 512 MiB total, 64 MiB/file, 256 levels, 192-byte
   relative paths, 8 MiB index. Include separately bound manifest/outcome
   controls and reject missing/unlisted/unsafe files. Obtain every finalized
   attempt through `gateway export JOB ATTEMPT FULL_SHA`, retain the reported
   export-receipt digest, publish the exact bytes durably, download them into a
   clean private workspace and run `bench_service unpack-export` with that
   receipt. Replaying native throughput uses the prebuilt
   `buster-bench-throughput compare --output RETRIEVED_THROUGHPUT_DIRECTORY`
   on the reconstructed copy, after outer and bundle validation and away from
   an active measurement.
5. Predeclare and retain separate real-systemd attempts for #880: normal
   completion with a materialized source manifest larger than 4 KiB;
   interruption after `validate-buster-v1.prepare.manifest` exists;
   interruption after a completed stage manifest exists; worker/coordinator
   restart after bundle publication but before the final manifest; and a
   connected lease-handoff peer that never acknowledges. Use reviewed binaries
   and the fixed real recipe, not stub executables or the injected manager test
   backend. Record the exact observed boundary, signal/restart action and
   journal identity for each attempt. Never relabel an interrupted or partial
   result as success.
6. Preserve the service journal, terminal/partial evidence, export outcome and
   relevant system journal before each controlled restart. Reopen
   deterministically and reconcile exact boot/unit/invocation/cgroup identities.
   Prove `.lease-handoff`, the outer and all five stage units, recursive cgroups
   and every descendant process absent before admission or lease release. The
   lease must remain continuously owned through that proof. Ambiguity means
   quarantine, not force-unlock.
7. Admit a later benign request only after durable reconciliation and absence
   proof, then retain its actual reservation and terminal evidence. This is a
   new job, not a retry of an interrupted attempt. Repeat export, durable
   publication, clean download and independent replay for every finalized
   attempt; preserve unavailable or failed exports as failures.

Keep `execution_outcome`, `measurement_validity` and `statistical_decision`
separate. For this slice validity/decision remain `not_evaluated`. Service
success, a replayed bundle, or successful next-job admission cannot establish
#512 acceptance, A/A calibration or a compiler performance conclusion.

## Evidence required before reporting completion

Update #437, #880 and #512 with main commit/tree; implementation head/tree and merge
identity; independent final-head review; exact-head warning/native/ASan/UBSan/
throughput/recipe/workflow checks; installed executable/profile/source digests;
workflow run/job IDs; service job/attempt/request identities; host/boot facts;
lifecycle trace; durable result location and bundle/full-result digests;
download/replay result; recursive absence proof; interruption boundary/outcome;
and later admission proof. Preserve original failed attempts and diagnosis.

Mark every unavailable gate explicitly. An Actions artifact's finite retention
is not the entire durable publication policy. Keep the service-owned original,
exact downloaded copy and their digests attributable to the same attempt.

## Implementation admission before live qualification

For a service implementation PR, finish its exact-head checks and independent
source review while draft. Before marking a sandbox repair ready, obtain the
required **isolated real-systemd** slice for the exact tested source, binary
hashes, manager/kernel and effective sandbox properties. Exercise actual
materialization, staging and output creation, visibility verification and
complete build-tree freezing under the relevant trusted/candidate identities.
Retain failures and cleanup observations. Repository fixtures and a standalone
syscall-filter probe do not replace this slice. Do not install an unmerged PR
on Benchpress to satisfy a pre-merge gate.

After those pre-merge gates pass, the owner marks the draft ready and submits
it to the normal main merge queue. The queue must validate the actual combined
tree; refresh preflight and admission if main or the PR head changes. A clean
branch need not be rebased solely because it is behind main. Once the admitted
PR is merged, record the resulting protected-main commit/tree. Only then
install the reviewed artifact under fresh administrator and host receipts and
start a new, separately keyed rehearsal through the protected fixed gateway.

The full real smoke/recovery campaign described above is **post-merge** #880
qualification, not a prerequisite for marking its implementation PR ready.
It still requires the five distinct scenarios, durable export/download/replay,
recursive absence and subsequent reservation before #880 can close. An
unavailable pre-merge slice or ambiguous live-host state remains a failed gate;
neither the merge queue nor the operator controls may be bypassed.
