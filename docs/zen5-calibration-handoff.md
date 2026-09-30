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

## Service producer

The held `zen5-calibration-v1` recipe (`tools/bench_service/zen5_recipe.c`,
described in `tools/bench_service/README.md`) is the intended producer. It
builds the five trusted subjects serially, writes this plan in the canonical
`freeze` form before any timed child, publishes a durable plan manifest, and
emits `immutable.json`, `same-root-rebuild.json` and `cross-root.json` under
`zen5/captures/`. Its final manifest lists the plan and capture digests, but
those are bundle contents: the trusted digests for `replay` must still come
from the authenticated service channel. The recipe never authorizes A/B.

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

## A/A policy evaluator (#426 plan step 4)

`tools/zen5_aa_evaluator.py` turns retained `zen5-calibration-v1` attempt
bundles into the canonical A/A policy document that a later
`aa-policy-sha256=` recipe-profile pin binds. It implements the family
proposed by #1188 with the inputs decided on
[#36](https://github.com/buster14a/buster/issues/36#issuecomment-5919407135):
q = 0.90, a 42-member family with 0.05 Bonferroni allocation (so a finite bound
needs at least 64 confirmatory attempts), and #881 current-job P = 60 with
runtime rows (U = R). It changes no threshold. It carries no approved limit,
and no policy digest is pinned anywhere.

There are two inputs and two digests:

- **Protocol** (`buster-zen5-aa-protocol-v1`). The reviewer writes it from
  exploratory pilots, then freezes and publishes it before the first
  confirmatory attempt. `template` writes the unapproved skeleton. The decided
  inputs are fixed, and each reviewer choice starts as `null`: the 42 practical
  limits, the stationarity alpha, permutation count and seed, the applicability
  identities, the pilot list, the current-job A/A equivalence band and the
  approval. The evaluator records the protocol's SHA-256. Nothing offline can
  prove that the protocol was published before window 2.
- **Ledger** (`buster-zen5-aa-attempt-ledger-v1`). It lists every retained
  attempt in execution order, so job/attempt tokens must strictly increase,
  and pilots come first. Each entry has its role, its bundle directory and the
  plan and capture digests that the authenticated service channel supplied.
  Those digests are never copied from the bundle.
- **Policy document** (`buster-zen5-aa-policy-v1`). The canonical JSON bytes
  are hashed, and that SHA-256 is the future `aa-policy-sha256=` value. The
  document embeds the protocol, the method, every attempt with its validity
  reasons, the family result, and the evaluator source digests and Python
  minor version.

```sh
python3 -B tools/zen5_aa_evaluator.py template --output protocol.json
python3 -B tools/zen5_aa_evaluator.py evaluate --protocol protocol.json \
  --ledger ledger.json --policy-output aa-policy.json --report-output aa-policy.md
python3 -B tools/zen5_aa_evaluator.py verify --protocol protocol.json \
  --ledger ledger.json --policy aa-policy.json
```

Each attempt is replayed from its exported result root:

- the final manifest must show a succeeded, complete, `pmu-qualified`,
  oracle-consistent attempt with `ab-authorized=false`;
- the `BQ-BUNDLE-V1` index must list every file, with matching sizes and
  digests, and no unlisted file;
- `zen5_calibration_handoff.replay` runs against the authenticated digests;
- every capture must have `ab_authorized` false and match the ledger's
  job/attempt;
- the PMU record must replay, and its digest must match the plan and the
  manifest.

A failed, incomplete or tampered attempt is kept as `invalid` with its reasons,
and it contributes no values. Each valid attempt contributes the 42 members:
7 checks for each metric and control, computed by the existing analyzers. An
undefined value, such as the lag-one correlation of a constant series, stays
unavailable and is never zero.

The status follows #1188's precedence:

- **`invalid`**: an invalid confirmatory attempt, more attempts than the fixed
  count, or ledger pilots that differ from the protocol's list.
- **`unavailable`**: an unset reviewer choice, or an attempt outside the
  declared applicability.
- **`inconclusive`**: insufficient evidence (fewer attempts than the
  predeclared count), an unavailable or unbounded member, a bound above its
  limit, or across-attempt dependence.
- **`eligible`**: none of the above.

Confirmatory statistics are withheld unless a complete, valid, fixed-count set
is evaluated under a complete protocol, so they cannot be used to choose
limits. Pilot attempts get descriptive summaries only and never count toward
the confirmatory set.

For a complete set, each member's bound is the exact order statistic `T_(k)`,
where k is the smallest value with `Pr[Binomial(n, 0.9) <= k-1] >= 1 - 0.05/42`.
Rational arithmetic is used, and ties are kept. The proposed stationarity
mechanism, which the reviewer approves separately, runs two seeded two-sided
rank permutation tests on every member in execution order: a Spearman trend
test and a lag-one serial test. Both use splitmix64 Fisher-Yates orders and
Bonferroni over all 84 checks. There is no optional stopping and no outlier
deletion.

The exit status is 0 for `eligible`, 1 for `unavailable` or `inconclusive`,
and 2 for `invalid` or a refused input. Every output says
`ab_authorized=false`. The result is not the #881 current-job A/A itself: that
check applies the band recorded here with #619's intervals.

The synthetic suite, `python3 -B tools/zen5_aa_evaluator.py --self-test`, runs
in the benchmark-service policy workflow. It covers invalid, tampered,
incomplete and relabeled bundles, too few and too many attempts, drift within a
capture and across attempts, serial dependence, unavailable members, pilot and
applicability mismatches, and refused protocols. It also covers byte replay
through the CLI. None of its bundles is host evidence.

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
