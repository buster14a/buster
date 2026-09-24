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
