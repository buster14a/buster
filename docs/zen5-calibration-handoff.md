# #426 calibration handoff for the fixed retirement campaign

This packet prepares the existing #884 immutable-binary A/A schedule and #915
same-source rebuild controls for a protected service job. It does **not** enable
the blocked `native-retirement-performance-v1` recipe. The ordinary throughput
guard, #511 budgets, #619 statistical family, and #1022 A/B sample plan stay
unchanged. Read [dedicated-host procedure](../tools/throughput/DEDICATED.md),
[host qualification](zen5-host-qualification.md), and the
[retirement contract](native-retirement-performance-contract.md) first.

## Before the exclusive host interval

The admitted service, not a candidate request, selects one reviewed source,
toolchain, fixed workload/output oracle, host profile, selected core, and
environment. Build trusted Clang subjects serially. Freeze one copy for the
immutable A/A and two same-source binaries for each of the same-root-rebuild
and cross-root controls. Record full unnormalized build/link argv, commands,
environment, logs, source/tree/toolchain/binary digests and `.text` placement.
The #915 example shows why same-binary A/A alone does not test build roots.

The service supplies `spec.json` with exactly these fields:

| Field | Authority |
|---|---|
| `repository` | Clean exact revision/tree of the trusted immutable source |
| `source_identity_sha256`, `expected_output_sha256` | Trusted source inventory and independently established oracle |
| `environment_fingerprint_sha256`, `host_qualification_sha256` | Admitted host/profile qualification, collected before timing |
| `immutable_binary_sha256` | Hash of the frozen baseline copied to two execution paths |
| `minimum_interblock_gap_ns` | Positive, predeclared separation for all three captures |
| `controls` | Keys `same-root-rebuild` and `cross-root`, each with `roots` and `binaries` maps for A and B; the former shares one configured root and the latter uses distinct roots |

There is no caller supplied pair count, statistical threshold, stopping rule,
schedule or A/B authorization flag. Generate the canonical immutable plan:

```sh
python3 -B tools/zen5_calibration_handoff.py freeze spec.json --output plan.json
```

Before the **first timed observation**, the protected service must persist and
authenticate the SHA-256 printed by this command, together with the exact
job/attempt, plan bytes and approved host/lease/profile identity. Merely writing
`plan.json` beside later results does not prove predeclaration. Any source,
binary, root, host/profile or output-oracle change requires a new plan and job.

## Exclusive collection and independent replay

The operator explicitly authorizes the host interval and verifies the approved
service owns the whole machine/lease; no unrelated build, test, profiling or
job overlaps. The service executes each existing 120-slot schedule in its own
separated blocks, retaining invalid, cancelled and superseded attempts. These
are **three distinct captures**, totaling 360 planned pairs (720 child
observations), rather than a full-corpus A/B experiment. No missing slot or
failed capture is repaired by rerunning until green. Same-root builds are
serialized in one configured path, with A frozen before B; cross-root builds
have normalized command equality. PMU/IBS qualification is a separate
diagnostic phase outside ordinary timing. Record unsupported events as null/NA.

The service publishes `immutable.json`, `same-root-rebuild.json` and
`cross-root.json` in the existing #884/#915 schemas. All three carry the
pre-sample `predeclared_family_sha256`; both controls retain actual first/second
binary digests per slot. The authenticated control channel separately provides
the exact plan digest and a JSON object of SHA-256 digests keyed `immutable`,
`same-root-rebuild`, `cross-root`. Save that independent object as
`trusted-captures.json`; it must **not** be copied from the result archive.

```sh
python3 -B tools/zen5_calibration_handoff.py replay plan.json \
  --trusted-plan-sha256 "$PRE_SAMPLE_SERVICE_DIGEST" \
  --trusted-captures trusted-captures.json \
  --immutable immutable.json \
  --same-root-rebuild same-root-rebuild.json \
  --cross-root cross-root.json --output qualification-inputs.json
```

This executable consumer calls the existing A/A and build-control validators
and analyzers, joins host/source/output identities, checks both build-root
relationships and actual frozen binaries, and recomputes capture and analysis
digests. The fixture that runs this producer-to-consumer path is:

```sh
python3 -B tools/zen5_calibration_handoff_test.py
```

`descriptive-complete` means all three records replay under one frozen plan;
`invalid` retains reasons. In **both** cases `ab_authorized` is false and
`physical_admission` and `candidate_decision` are `not-evaluated`. The
authenticated service receipt and a reviewed, versioned empirical A/A
eligibility calculation remain necessary before #1022 can authorize A/B. An
accepted A/B result from this same job is never an input to its preceding A/A
decision. The existing #619/#511 consumer then assesses complete candidate
samples only after the independent post-A/A decision and service phase receipt.

## Private phase receipt contract for #1021 and #1022

`verify_service_phase(plan, report, presample, completion, trusted)` in the
handoff module is the fail-closed **descriptive** consumer. The `trusted` object
is supplied only by #1021's authenticated, private service control channel;
it is never read from the candidate request, result bundle or either receipt.
There is deliberately no public CLI option for asserting these facts. This
module checks integrity against that authority; it does not authenticate the
channel or issue an A/B launch token.

Both phase receipts have schema `buster-zen5-calibration-phase-v1`, version 1,
and the exact `job_id`, positive `attempt`, `host_id`, `boot_id`, `lease_id`,
`profile_sha256`, `host_qualification_sha256` and `plan_sha256`. The service
persists the `pre-sample`/`committed` receipt **before** observing any timed
child and independently publishes its SHA-256 and the ordered persistence fact.
The `aa-complete`/`complete` receipt adds the previous receipt digest, the
three raw capture digests and the independent replay report digest. The service
publishes that receipt's digest outside the result bundle as well. No extra
policy, approval or caller-controlled execution fields are accepted in either
receipt. All digests cover canonical JSON bytes; the service must persist the
exact bytes it authenticates.

The private authority supplies the two independently obtained receipt digests,
live job/attempt/host/boot/lease/profile and qualification identities, a
`pmu-qualified` state, `exclusive-live` lease state and a true pre-sample
persistence fact. #1021 must compare these against its installed current
host/boot/lease and persist denied, interrupted or missing phases. #1022 must
consume only the exact same attempt after #1021 acknowledges it; no candidate
may supply, refresh or override the private facts. Path normalization in the
offline plan is lexical: the service must resolve actual configured roots and
reject symlink/bind-mount aliases before freezing, as well as after a host or
lease transition. Service restart, reboot, lease expiry or host/profile drift
invalidates the phase; a new attempt needs a new pre-sample receipt and full
captures. Retain all failed and superseded records.

The function returns `verified-descriptive` or `denied`, and always returns
`ab_authorized=false` and `aa_decision=not-evaluated`. A functional fixture
checks missing authority, denied phase, injected authorization fields, stale
host/boot/lease, invalid qualification, changed binary/root identities and
replay mismatches. Its synthetic `pmu-qualified` fact is a test input, never a
physical host result. The production A/B gate in #1022 remains closed even
after `verified-descriptive`; #1021 and #923 must test a real private-channel
receipt to campaign transition on their integrated heads before admission.

**Reviewed policy decision still required from the #426 owner:** approve a
versioned empirical A/A eligibility rule before any protected physical A/B
attempt. It must state the permitted wall/RSS noise, block drift, serial,
label/path/order and same-root/cross-root build effects; exact statistic,
observational unit, uncertainty/family allocation, thresholds, invalid and
inconclusive outcomes, and the specific host/profile/build scope. Review
independently captured full raw pilots on the qualified exclusive 9700X,
including invalid and superseded attempts, output and binary hashes, PMU event
availability and actual build provenance, before fixing the rule. Persist its
reviewed policy identity in the service's post-A/A decision and authenticate
that decision before #1022 opens A/B. The current descriptive model and #619
candidate statistics cannot choose or infer these thresholds. No empirical
rule has been approved by this PR.

The real producer is an explicitly authorized, separately admitted #880 service
qualification phase. Its A/A work must be allowed before any A/B result exists;
the still-blocked #881 candidate recipe cannot use its own future A/B verdict
to authorize that phase. #1022 consumes the authenticated pre-sample plan and
post-A/A decision through #1021's phase channel. This offline report is an
independently replayable **input** for that
decision, not a forged service receipt or a substitute for #1022's production
integration test. The campaign owner must wire those authenticated identities
at the production boundary and exercise an actual service-produced A/A →
decision → A/B fixture before recipe admission. #882/#883 own physical
candidate measurements. #426 remains open for the actual 9700X noise model,
qualified PMU capture and representative candidate verdict.
