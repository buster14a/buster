## #125 — consolidated selector experiment and independent validation

**Evidence record only. No new allocator implementation, merge recommendation,
whole-compiler speedup, retirement prerequisite, or parent-issue closure.**

Two completed investigation packets are recorded together below. Their reports
retain their original execution scope and source identities rather than claiming
that a newer repository head was tested.

### What is established

- The first standalone selector experiment passed 12,057 deterministic populations
  and 3,973,748 instrumented/sanitized assertions. Its final uninstrumented screen
  retained 1,488 samples across 31 shapes. Full heaps built on request two lost on
  several dense two-query cases; first-request tile indexing lost on first-only
  and empty cases. These are selector-only findings on a shared execution host,
  not compiler performance evidence or a rejection of selective sparse traffic.
- The later independent review reconciled the owner's hosted capture
  [36000528004, attempt 1](https://github.com/buster14a/buster/actions/runs/36000528004).
  Frozen direct-SSA unity had 947,201 dense cells, 99,703 nonzero cells, 43,263
  selector calls and 603,265 cell visits. The memory-form capture had 947,597,
  99,300, 42,190 and 600,854 respectively. The pressure fixture had zero region
  selection calls; its name does not establish repeated-selection coverage.
- 51 of 4,083 direct-SSA tables account for 67.34% of dense cells and 74.16% of
  scanner-cell visits. They satisfy the conservative sparse storage gate even
  with total edits E as an upper bound for U. Another 588 tables require U for
  exact dispatch classification. The gate `16U + 8C + 20 < 8CL` proves a storage
  bound, not a time crossover; the two full edit-stream traversals still cost E.
- Independent extracted-helper tests passed 5,258,803 checks each under Clang,
  GCC and fail-fast ASan/UBSan with leak detection. They cover 177,293 captured
  rows, all 47,985 recorded region selections and 800 generated rows, plus dirty
  heap restarts, ties, wide traffic values and counted growth cases.

### Policy boundary

Preserve descending exact-u64 traffic and ascending original region ID, the
candidate heap's separate strict-greater tie policy, pre-heap candidate slots,
all original eligibility/rejection/capacity checks and ordered split edits.
Ranking traffic is immutable in the inspected source; eligibility is not cached.
Only attempt 0 splits at that baseline. A mutated ranking requires rebuilding;
negative controls demonstrate stale-index disagreement. The hybrid's dense or
unordered fallback retains the overall O(CL²) worst case.

### Snapshot and ownership

Inspected baseline: `5d1c314a8ee3c5f9fb713f6afb85ab1e795f44a0` (tree
`b1f7f60e49d79b1452349493c3f6cbc2a4acd93d`); earlier experiment base:
`2e942e80a87666409cf29d3e24a68d322b9e71fd`. Baseline QUALITY blob:
`2ce4bfdfb73d07cd9a127812700108403b770de8`.
The [existing owner](https://github.com/buster14a/buster/issues/125#issuecomment-5813893082)
published implementation `6644f7a9dd9c9b360091c5c9d7a6eda75183f293` while the
independent review ran. This record does not claim or update that branch, and
extracted-helper validation is not a build/test of the complete implementation.
The landed #381 census, #462 arithmetic fix and #496 touched-scratch work remain
prior work. #586 is research; the old closure microbenchmark is not reused as
accepted evidence.

### Acceptance ledger for these two packets

| Gate | Recorded status |
| --- | --- |
| Standalone selector exactness, generated work and sanitizers | PASS, at the packet's exact sources |
| Hosted capture hash, census joins and recorded selection prefixes | PASS, independently checked |
| Extracted owner ordering helpers, dirty restarts and generated tests | PASS, three builds |
| Unconditional activation hypotheses from standalone timing | FAIL in the stated generated cases, not all representations |
| Actual sparse constructor / Buster arena regression by this reviewer | NOT RUN |
| Complete decisions, placements, ordered edits and emitted artifacts | NOT RUN |
| Full candidate modes/platforms/self-host/required CI by these sessions | NOT RUN |
| Matched complete allocator/compiler setup, cleanup, timing and RSS | NOT RUN |
| Authorized benchpress 9700X / PMU / accepted timing crossover | NOT RUN |

The second packet resolves the first packet's unavailable-population limitation
for its frozen captured inputs only. It does not fill the full-cost performance
or complete-placement gates. No new allocator test or compiler benchmark ran during archival.

### Original reports and evidence

