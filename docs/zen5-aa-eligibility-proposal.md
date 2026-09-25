# Proposed Zen 5 A/A eligibility policy for #426

**Status: proposal for independent review; not approved or executable.** The
proposed identifier is `buster-zen5-aa-eligibility-v1`. No physical pilot,
empirical limit, approval receipt, or A/B launch authority is supplied by this
document. A reviewer must approve an exact version and digest before an admitted
service can use it. Until then the eligibility result is `unavailable`.

This policy would decide whether the dedicated Ryzen 7 9700X environment and
the current *baseline A/A* are precise and stable enough to inspect a candidate
A/B under the already approved [native-retirement contract](native-retirement-performance-contract.md).
It does not change #511's budgets, #619's candidate statistics, the ordinary
throughput guard, or the fixed #884/#1016/#1053 calibration readers. In
particular, a complete calibration report is an input to this decision, not a
decision. The 120-pair control captures are not #511's full-population A/A.

## Established contracts and the remaining choice

| Established on `main` | Still requires independent #426 decision |
| --- | --- |
| #884: one immutable-binary A/A capture has two rounds, four separated 15-pair blocks per round, all label/path assignments, exact slot retention, wall/RSS raw rows, and descriptive summaries. | How much wall/RSS variation, drift and serial dependence is tolerable for this host, workload and profile. |
| #1016: same-source same-root rebuild and cross-root controls use the same schedule and retain build identities, commands, binary hashes and `.text` placement. | How large either rebuild effect may be and which build-root arrangements remain applicable. |
| #1053: one frozen family and independently supplied plan/capture digests join and replay all three controls; its report always has `ab_authorized=false`. | Qualified physical pilot population, empirical limits, reviewer approval and a current service phase decision. |
| #422/#884: host, lease and Zen 5 PMU facts have replay formats. | A current admitted physical capture with valid required event runtime/running fractions and service-owned lease facts. |
| #511/#619: exact candidate population, practical limits, two rounds, paired block statistics and simultaneous candidate bounds. | A separate pre-A/B A/A eligibility calculation. Candidate A/B data cannot choose or validate its earlier A/A rule. |

The older [#422 A/A audit](performance-audits/2026-09-12T001148Z.md)
established no confirmed broad-guard regression, not equivalence or a noise
floor. The [#884 diagnostic PMU audit](performance-audits/2026-09-19T222922Z.md)
has no qualified service receipt or resolved running fractions. The
[#915 matched-build audit](performance-audits/2026-09-20T050606Z.md)
demonstrates a real build-root confound, not a transferable correction factor.
None of these is an admitted pilot for this policy.

## Proposed decision and observational units

An eligibility calculation would have four distinct outputs:

| Outcome | Condition | Effect |
| --- | --- | --- |
| `unavailable` | No independently approved policy identity, no admitted physical pilot, missing trusted service phase facts, or unsupported host/profile/build applicability. | Keep A/B blocked; do not synthesize a pass. |
| `invalid` | A required raw slot, identity, oracle, digest, lease, PMU fact, build control, or retained-attempt record fails replay; interrupted or superseded data are hidden. | Retain the attempt and its reasons; a new, fully predeclared attempt is needed. |
| `inconclusive` | Valid complete evidence has uncertain or excessive effects, dependence, quantization, drift, or intervals too wide to adjudicate the predeclared limits. | Keep A/B blocked; do not select a favorable subset or extend sampling after inspection. |
| `eligible` | A reviewed policy, admitted service and qualified current host, complete raw pilots, all required controls, and current full-population A/A satisfy every predeclared check simultaneously. | #1021 may authenticate this exact phase for #1022; this output alone is not an A/B launch token or a candidate verdict. |

One **child execution** is a raw observation. Its adjacent opposite-label
execution makes one A/A pair and a symmetric relative difference, using the
same formula as `zen5_aa_noise.py`. Fifteen consecutive pairs form one
separated calibration block. A complete two-round, 120-pair capture is one
pilot attempt. The three control kinds are separate captures under one frozen
family. Pairs nested in a block are not 15 independent replicates of host
drift; the 120 pairs are not 120 independent host experiments. Independent
complete attempts, if actually separated and shown sufficiently stationary,
would be the across-pilot uncertainty units. #619's adjacent two-pair AB/BA
block remains the **different** unit for the later candidate statistics.

The proposed family has seven checks for each of wall time and peak RSS in
each of the three controls: **42 members**. Each complete capture produces one
value for each member from the existing descriptive analyzer:

| Member | Immutable binary | Either rebuild control |
| --- | --- | --- |
| Pair resolution | `empirical_resolution_fraction` | `observed_absolute_build_difference_p95` |
| Variant effect | Maximum absolute `per_block.median_label_effect` | Maximum absolute `per_block.median_build_effect` |
| Path effect | Maximum absolute `per_block.median_path_effect` | Same |
| Order effect | Maximum absolute `per_block.median_order_effect` | Same |
| Block shift | `pair_center.maximum_absolute_block_median_shift` | Same |
| Serial effect | Absolute `pair_center.lag_one_correlation` | Same |
| Linear drift | Absolute `pair_center.relative_linear_drift_per_pair` | Same |

An undefined serial/drift summary remains unavailable, never zero. Review
both rounds and every block, not only a pooled center. The fixed schedule
rotates AB/BA inside blocks but does not by itself prove independence or
remove time/assignment confounding. A reviewer must reject or narrow scope
when the pilot cannot separate these effects.

**Proposed uncertainty treatment, subject to review before pilots:** keep the
complete capture as the independent unit. For each metric/control/check, first
reduce each capture to the predeclared worst absolute block summary or named
within-capture statistic above. Use a one-sided distribution-free order-statistic
upper bound for a predeclared across-attempt quantile. Allocate a single 5%
family error budget by Bonferroni over the entire fixed member list, including
both metrics and all controls; do not spend separate uncorrected 5% budgets
on attractive slices. If there are `K` members and `n` independent complete
attempts, the proposed upper bound for a quantile `q` is the smallest ordered
attempt value `T_(k)` with
`Pr[Binomial(n,q) <= k-1] >= 1 - 0.05/K`. If no such `k` exists, that bound
is unbounded and eligibility is inconclusive. Ties stay in the order statistic.
Each of the 42 simultaneous upper bounds would have to be at or below its
separately approved practical limit; a valid bound crossing a limit is
inconclusive. The proposed family allocation is therefore `0.05/42` per
one-sided bound, with no unused member removed after seeing pilot results.
No bootstrap of individual children or after-the-fact choice of a smaller
family is allowed. The reviewer must fix `q`, the exact member definitions,
pilot count, stationarity/independence checks, and any finite-sample fallback
**before** the physical pilot. The 5% allocation and order-statistic method
here are proposals, not an approved policy and not #619's A/B family.

The reviewer must also set **empirical practical limits** for each member from
full retained qualified pilots, with justification against the #511 limits and
the requested-work population. No value is supplied here. Wall time uses the
recorded positive integer nanoseconds and reports the observed clock step and
ties. Peak RSS uses actual child high-water bytes and reports measurement
granularity, ties and nonzero steps; equal or page-rounded readings do not
prove sub-granularity equivalence. Both metrics need a predeclared upper
resolution bound and enough precision to distinguish a decision under #511.
Do not infer an empirical threshold from the largest convenient observed
point, subtract a build-control median, or treat a lower point estimate as
precision evidence.

The same-root rebuild control tests the intended serial-build path. Cross-root
results bound where a separate-root result can be applied; a large cross-root
effect may restrict approval to the reviewed same-root build procedure, never
be subtracted from A/B. Source/tree, toolchain, complete build/link flags,
configured root, normalized commands, binary SHA-256 and `.text` placement
must be inspected together. A new workload, binary layout, build arrangement,
profile or host is outside the policy until its applicability is reviewed.

The later current-job A/A must cover every required #511 row and variable
metric under its own frozen seed, two rounds, warmups and even pair count in
the approved 60–254 range. A control capture of one compiler workload cannot
replace it. For that current A/A, the proposed equivalence check reuses
#619's two-pair block statistics and simultaneous lower/upper intervals for
every required member in **each round and pooled**, comparing both bounds
with a separately approved A/A equivalence band around ratio 1. The band is
unset pending the qualified pilots; it is distinct from #511's fixed candidate
budgets. A point estimate near 1, one green round, or no confirmed regression
is insufficient. A valid interval crossing the band remains inconclusive.
Code bytes retain their exact deterministic #511 treatment; generated runtime
retains #511's own required A/A and A/B applicability. This proposed use of
#619's existing intervals does not change its implementation, family allocation
or candidate verdict. The A/A eligibility calculation occurs after current
A/A completes and before the first candidate A/B child. It never consumes
that candidate's later data.

## Operator capture and clean offline replay packet

The future authorized service owner freezes and authenticates these **before
the first timed child**:

1. Exact service recipe and installed binary/profile digests; job, positive
   attempt, host, boot, selected CPU, exclusive lease identity and source/tree.
   The source is a clean immutable checkout with trusted Clang/toolchain and
   identical workload/input/output oracle. Record every build and link argv,
   environment, root, log and binary hash, and section/function placement.
2. The exact #1053 `spec.json` and canonical `plan.json` bytes and SHA-256,
   fixed schedule, interblock gap, immutable same-binary copy, and serialized
   same-root and cross-root build identities. Freeze the pilot count and
   statistical family separately once a reviewer approves them. No request
   field may choose a pair count, threshold or stopping rule.
3. A service-owned pre-sample `buster-zen5-calibration-phase-v1` receipt,
   durably persisted before timing. Its digest and persistence ordering fact
   come through #1021's authenticated private channel, outside the bundle.

During the exclusive interval, retain **all** 120 slots per capture in
`immutable.json`, `same-root-rebuild.json` and `cross-root.json`, including
failures, invalid reasons, monotonic bounds, child output hashes, binary hashes,
wall nanoseconds and peak-RSS bytes. Retain every rejected, cancelled, partial
and superseded attempt, including the unsuccessful preflight and transfer
records. Record warmups and controls separately from timed children. Keep PMU,
IBS, profiler and allocation probes outside ordinary timing. The separate
`zen5-pmu-v1.json` must retain raw perf rows, resolved event encodings,
counts/null availability, event enabled/running time, running fractions, CPU
model/microcode, kernel/perf, topology/SMT, firmware, memory, power/governor,
thermal and environment facts. Missing PMU data is unavailable, not zero.

The service then seals a post-A/A receipt with the pre-sample digest, three
capture byte digests and independently replayed report digest, all for the same
job/attempt/host/boot/lease/profile/qualification. #1021 authenticates these
facts and lease continuity privately. The existing durable export and #510
archive/download verification retain the raw bytes, plans, receipts, failed
attempt ledger and replay command. A local digest calculated from the bundle
cannot impersonate the authenticated service digest.

In a fresh off-host checkout at the recorded repository revision/tree, verify
the service-supplied bytes and run the existing readers:

```sh
python3 -B tools/zen5_host_qualification.py validate zen5-pmu-v1.json
python3 -B tools/zen5_aa_noise.py validate immutable.json
python3 -B tools/zen5_build_control.py validate same-root-rebuild.json
python3 -B tools/zen5_build_control.py validate cross-root.json
python3 -B tools/zen5_calibration_handoff.py replay plan.json \
  --trusted-plan-sha256 "$PRE_SAMPLE_SERVICE_DIGEST" \
  --trusted-captures trusted-captures.json \
  --immutable immutable.json \
  --same-root-rebuild same-root-rebuild.json \
  --cross-root cross-root.json --output qualification-inputs.json
```

`trusted-captures.json` must be independently obtained from the authenticated
service, not copied from the result archive. Repeat the replay from a second
clean extraction and compare all input, report and analysis SHA-256 digests.
The offline fixture is explicitly synthetic:
`python3 -B tools/zen5_calibration_handoff_test.py`.

## Review and ownership handoff

The #426 reviewer must approve or revise the method, member list, pilot count,
empirical limits, applicability, exact policy bytes/digest and requalification
triggers before the service can use the decision. A qualified physical pilot,
not the synthetic fixture, must support every chosen limit. Changes in
microcode, kernel/perf, firmware, memory, topology/SMT, power profile,
environment/toolchain, workload/oracle, source/binary, build root/layout,
service/lease or policy method require a new applicability review and fresh
A/A; #511 further requires complete affected A/B revalidation. Retain the old
record as superseded.

#1021 supplies the service-owned private pre-sample and post-A/A authority,
durable ordering and same-attempt host/boot/lease facts. #1022 consumes the
reviewed eligibility decision **and** that phase authority before launching
any candidate A/B child; it then uses #619/#511 for the candidate verdict.
#923 owns shared production wiring and registered tests. #880/#881 own live
service/recipe admission, and #882/#883 own the authorized physical candidate
runs. This proposal and offline replay do not satisfy any of those gates.
