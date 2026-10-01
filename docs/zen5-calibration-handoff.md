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
  exploratory pilots, then freezes it and merges it on main before the
  window-2 dispatch (decided on
  [#36](https://github.com/buster14a/buster/issues/36#issuecomment-5921955897)).
  `template` writes the unapproved skeleton. The decided inputs are fixed:
  q, K, the family alpha, the count of at least 64 (at most 256) confirmatory
  attempts, and the independence checks (alpha 0.05, Bonferroni over 84, at
  least 100,000 permutations; decided on
  [#36](https://github.com/buster14a/buster/issues/36#issuecomment-5921978144)).
  Each reviewer choice starts as `null`: the 42 practical limits, the
  permutation seed, the confirmatory job range, the applicability identities,
  the pilot list, the current-job A/A equivalence band and the approval. The
  evaluator records the protocol's SHA-256. Nothing offline can prove that the
  protocol was published before window 2.
- **Ledger** (`buster-zen5-aa-attempt-ledger-v1`). It must be the complete
  attempt list the service reports, never a selection of kept bundles. It
  lists every retained attempt in execution order, so job/attempt tokens must
  strictly increase, and pilots come first. `window_jobs` lists every job id
  the service reports inside the protocol's `confirmatory_jobs` range. Each
  entry has its role, its bundle directory and a `trusted` block of digests
  taken from the authenticated service channel, never from the bundle (see
  below).
- **Policy document** (`buster-zen5-aa-policy-v1`). The canonical JSON bytes
  are hashed, and that SHA-256 is the future `aa-policy-sha256=` value. The
  document embeds the protocol, the method, every attempt with its validity
  reasons, the family result, and the evaluator source digests. It does not
  record the interpreter version: the analyzers use `math.fsum` since #2110,
  and the evaluator's own statistics are exact integers or fractions, so the
  bytes replay under any supported Python 3 (checked with 3.11, 3.12 and 3.13).
  The report shows the interpreter for information.

Each ledger `trusted` block has four parts:

- `manifest_sha256`: the SHA-256 of the final `zen5-calibration-v1.manifest`
  bytes. The service binds it with `BQ_RESULT_BIND` (`bq_result_bind`) and
  reports it as `manifest-sha256=` in the `gateway result JOB` receipt. The
  `BQEXP001` export receipt carries it at offset 112. The evaluator requires the
  bundle's manifest bytes to hash to it. Profile, status, stage, oracle, PMU,
  budget and job/attempt lines therefore come from the service, not from the
  bundle.
- `profile_sha256`: the SHA-256 of the compiled recipe profile, at offset 608
  of the `BQEXP001` export receipt. This is the same byte string that
  `zen5_recipe.c` hashes into `profile-sha256=`, and the manifest line must
  equal it.
- `plan_sha256` and `captures`: these must equal the authenticated manifest's
  `plan-sha256=` and `*-capture-sha256=` lines, and the bundle bytes must
  match them.

On current main the `zen5-calibration-v1` recipe is still held
(`bq_recipe_blocked`). The worker does not yet bind a zen5 result, so no zen5
attempt has these receipts yet. The ledger requires them anyway. An attempt
without them, or with digests that differ from them, is `invalid`.

The confirmatory set is fixed in advance by the protocol's
`confirmatory_jobs` range (`first_job_id`..`last_job_id`). The service assigns
job ids from its journal sequence, so ids are increasing but not consecutive.
The reviewer sets the range when the protocol is merged, and window 2
dispatches only its confirmatory attempts. `window_jobs` must be the
service's complete list of jobs in that range: for example, every id in the
range for which `gateway status JOB` returns a job (unassigned ids are
refused). The confirmatory entries must be exactly those jobs, one attempt
each. The set is `invalid` if a job in the range is missing from the ledger,
if a job has a replaced (second) attempt, if a confirmatory attempt or listed
job lies outside the range, or if a pilot lies inside it. The evaluator cannot
query the service itself. Omitting an attempt therefore requires
misreporting the service's list explicitly; it cannot happen by quietly
dropping a bundle.

```sh
python3 -B tools/zen5_aa_evaluator.py template --output protocol.json
python3 -B tools/zen5_aa_evaluator.py evaluate --protocol protocol.json \
  --ledger ledger.json --policy-output aa-policy.json --report-output aa-policy.md
python3 -B tools/zen5_aa_evaluator.py verify --protocol protocol.json \
  --ledger ledger.json --policy aa-policy.json
```

Each attempt is replayed from its exported result root:

- the final manifest bytes must hash to the authenticated digest. The manifest
  must show a succeeded, complete, `pmu-qualified`, oracle-consistent attempt
  with `ab-authorized=false`, the authenticated profile digest, and
  `elapsed-ns` no greater than `budget-seconds`;
- the `BQ-BUNDLE-V1` index must list every file, with matching sizes and
  digests. No file may be unlisted, and no directory may be unreadable
  or change while it is inventoried;
- `zen5_calibration_handoff.replay` runs against the authenticated digests;
- every capture must have `ab_authorized` false and match the ledger's
  job/attempt;
- the PMU record must replay, and its digest must match the plan and the
  manifest.

A failed, incomplete or tampered attempt is kept as `invalid` with its reasons,
and it contributes no values. Each valid attempt contributes the 42 members:
7 checks for each metric and control, computed by the existing analyzers.

Constant series follow the rule decided on
[#36](https://github.com/buster14a/buster/issues/36#issuecomment-5921955897).
A zero-variance pair-center series, such as page-quantized peak RSS with equal
centers, is listed in the attempt's `constant_series`. Its serial effect is
exactly 0, because a constant series cannot be serially dependent. Its linear
drift is already a defined 0. Every other undefined value stays unavailable
and is never zero, for example a lag-one correlation where only one window is
constant.

The status follows #1188's precedence:

- **`invalid`**: an invalid confirmatory attempt, more attempts than the fixed
  count, a confirmatory set that differs from the service's job list for the
  declared range, or ledger pilots that differ from the protocol's list.
- **`unavailable`**: an unset reviewer choice, or a confirmatory attempt outside
  the declared applicability. Applicability is checked on confirmatory
  attempts only, because pilots are exploratory.
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
Rational arithmetic is used, and ties are kept. At n = 64 the bound is the
sample maximum. A limit is compared exactly as a decimal, against the
shortest round-trip form of the bound. Independence across attempts uses the
decided mechanism. Two seeded two-sided rank permutation tests run on every
member in execution order: a Spearman trend test and a lag-one serial test.
Both use splitmix64 Fisher-Yates orders, at least 100,000 permutations, and
family alpha 0.05 with Bonferroni over all 84 checks, so each test rejects at
p <= 1/1680. If either test rejects, the family is inconclusive. Nothing is
deleted or rerun. The statistics are packed into exact integer fields, and a
100,000-permutation run takes about 8 s for 64 attempts.

The exit status is 0 for `eligible`, 1 for `unavailable` or `inconclusive`,
and 2 for `invalid` or a refused input. Every output says
`ab_authorized=false`. The result is not the #881 current-job A/A itself: that
check applies the band recorded here with #619's intervals.

The synthetic suite, `python3 -B tools/zen5_aa_evaluator.py --self-test`, runs
in the benchmark-service policy workflow. It covers invalid, tampered,
incomplete and relabeled bundles, as well as forged manifests, forged profiles
and budget overruns. It covers omitted, replaced and out-of-range attempts,
unreadable or changing bundle directories, too few and too many attempts,
drift within a capture and across attempts, and serial dependence. It also
covers constant and non-constant undefined series through the real analyzers,
pilot and applicability mismatches, refused protocols, and byte replay through
the CLI. None of its bundles is host evidence.

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
