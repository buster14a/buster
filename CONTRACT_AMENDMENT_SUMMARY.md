# Contract amendment A1: batched native-host sampling (draft for #36 approval)

Scratch-branch companion to the edit of
`docs/native-retirement-performance-contract.md`. Not for `docs/`; nothing
here is implemented.

## (a) Summary for approval

**What A1 changes in `native-retirement-performance-v1`**

1. **Timed population.** Only the 16 native-host configurations on
   `x86_64-unknown-linux-gnu` are timed (4 allocators × 2 frontend lowerings ×
   2 PIC), covering every compiler-eligible fixture row on that target. The
   correctness census still covers all 78,912 rows on all 12 targets.
   Cross-target rows are correctness-only. The #508 row artifact is unchanged;
   the timed projection (eligible ∧ native target) is derived from it.
2. **Batched samples.** One fresh compiler process per (configuration,
   recipe/flag group) compiles every fixture of that group as its own TU
   through multi-input `-c`: one worker, frozen input order,
   continue-on-failure. `-fmetrics-out` gives each fixture a wall interval and
   an arena high-water figure, and those are that fixture's paired sample. Batch
   process wall time and peak RSS are recorded per batch as separate,
   non-gating metrics, with startup reported explicitly. Link and self-host
   rows remain singleton processes.
3. **Code bytes** are parsed once per (variant, row). Every batch must
   reproduce each object byte-for-byte, which is the determinism check.
4. **Sampling and statistics are unchanged:** at least 60 even pairs × 2
   rounds, 2 warmups, blocked AB/BA, A/A then A/B, the same #619 estimator,
   family rule, Bonferroni bounds, thresholds and outcomes. The schedule
   shuffles batch groups instead of rows, and each batch pair gives one pair
   to every fixture in it.
5. **Runtime** is unchanged, over native runtime-eligible rows.

**Changes the unit switch forces:**
- Per-cell memory becomes per-input arena high-water instead of OS RSS
  (proposed token `compiler_peak_memory`), and per-cell wall becomes the
  per-input interval.
- The execution plan moves to v3 with group contracts.
- The transcript records batch invocations plus per-input metrics artifacts.
- Result records drop `generated_code_bytes`; a per-row code record set
  replaces it.
- The invocation count becomes `(G+U)·2·(2+2P)`.
- The family is derived over timed rows. The target slice has one value, and
  there are about 16 bootstrap members per metric instead of up to 25.
- A reviewed, measured-batch budget replaces the one-hour worker budget.

**Estimate.** The 16 principal groups need 488 processes each: 7,808 batch
processes, about 2.4 h at P = 60. Today's declaration also has four small
recipe groups per configuration (c23, c23-dialect, avx512, cx16), adding
about 31k short processes (≈ 0.35 h). Runtime is extra.

**Unchanged:** the decision ID, limits, verdict rules, the 39,518,208-record
ceiling, the 254-pair maximum, and the lease, supervisor and replay rules.

## (b) Code and test changes required, by file

**Pins (required follow-up; pin values were deliberately not changed here)**
- `tools/bench_service/queue.c` (`bq_native_retirement_blocked_profile`) and
  `tools/bench_service/profiles/native-retirement-performance-v1.blocked`:
  update `contract-sha256`. The old pin is `67fff9a8…a431b0`; the value
  follows the final approved bytes. `tools/bench_service/tests.c` enforces
  the pin and fails until it is updated. The `binding-validator-sha256` pin
  changes again when the validator lands; `statistics-sha256` does not if
  `retirement_stats.h` stays untouched.

**Binding validator: `tools/native_retirement_performance_binding.py`**
- Add a pinned `NATIVE_TIMED_TARGET` and a `_timed_rows` projection. This is
  the population definition.
- `_derive_statistical_family` / `_family_member_counts` / `_population`:
  derive over timed rows. The family must exclude cross-target cells.
- `_check_execution_evidence`: require the profile, qualification and A/A
  `native_target` to equal `NATIVE_TIMED_TARGET`. This ties the host to the
  pinned target.
- `METRICS` / thresholds / `_rules.aggregation`: rename `peak_rss` →
  `peak_memory` (the per-cell metric semantics change). Add rules tokens for
  the sampling unit (`native-host-batch-group`) and once-measured code bytes,
  so that A1 policy is bound and unknown fields still fail.
- `_execution_schedule`: derive batch groups (config + recipe + CPU; singleton
  stage rows). Cells become groups; the runtime campaign is unchanged over
  timed runtime rows. This is the schedule-unit change.
- `_check_execution_plan`: plan schema v3. Validate group contracts (members,
  controls, batch command digest per variant, expected exit), and null fields
  for untimed rows. Plans now freeze batches.
- `_check_execution_transcript`: group-keyed invocations; `expected_count =
  (G+U)·2·(W+R·P)`; stream the per-input metrics artifacts (1 MiB per record,
  64 MiB per artifact); check the frozen input order, statuses and diagnostic
  digests, nested non-overlapping intervals, phase sum ≤ interval, and each
  object SHA-256 = frozen artifact; join per-input values to samples. The
  process metrics become batch-level. This is how batch evidence is
  authenticated.
- `_result_input_plan` / `_consume_result_record`: timed sample population,
  no `generated_code_bytes` in pair records, and a new plan schema/population
  token. Code bytes are measured once.
- `_code_bytes_summary`: read per-row code records. The per-pair constancy
  check moves to the transcript determinism check.
- `_check_workflow_evidence_open` / `_sealed_closure_files`: bind the
  code-record set and the metrics artifacts in the sealed closure and replay,
  so that the new evidence is sealed.
- `_check_adapter_series`: follow the metric-token rename. Statistics input is
  otherwise unchanged.

**Tests**
- `tools/native_retirement_performance_binding_test.py`: rebuild fixtures for
  plan v3, group transcripts and pair records without code bytes. Add
  rejection tests for cross-target timing, determinism mismatch, interval
  overlap, control status mismatch and metrics caps. Add a native-population
  capacity test next to the kept ceiling tests. This covers every new rule.
- `tools/native_retirement_performance_identity_test.py`: port the schedule
  and transcript tests (`test_schedule_*`, `test_complete_warmups_*`,
  `test_workflow_checks_actual_invocations_*`) to batch groups, because the
  invocation shape changed.
- `tools/native_retirement_performance_eligibility_test.py`: derive the timed
  projection and batch schedule from the replayed census (`#929`
  regression), because the schedule is now batch-based.
- `tools/native_retirement_result_input.py` and its test: no change needed.
  They are integrity-only and use a generic `record_id`.
- `tools/native_retirement_performance_schema.py`: no change. The census
  report schema is untouched.

**#619 schedule/cursor**
- `tools/throughput/retirement_execution.h`: the cursor walks G groups (member
  lists copied at init); `TpRetirementInvocation` gains a group; `expected`
  uses G; the record encoder emits the group, the metrics descriptor and the
  per-input output digest; recompute `..._TRANSCRIPT_RECORD_BYTES_MAX`. This
  is the producer side of the new transcript.
- `tools/throughput/retirement_stats.h`: no change. The block schedule and
  estimator are unchanged; keep `TP_RETIREMENT_PEAK_RSS` at index 1 so the pin
  holds.
- `tools/throughput/retirement_execution_test.py`: replay the C batch fixtures
  through the new validator.

**Sample shards / measurement**
- `tools/throughput/retirement_samples.h`: fan one batch append out to each
  member's spool slot, take memory from the per-input record, drop the code
  slots from the spool and export, add a per-row code-record export, and
  recompute the record size and maximum line width.
- `tools/throughput/retirement_measurement.h`: launch a batch (all
  inputs/outputs plus `-fmetrics-out` on a private descriptor), hash every
  object against its frozen artifact, and parse and bound the metrics file.
  Stop parsing code on each sample; `retirement_artifact.h` is then used once,
  in preparation.
- New bounded per-input metrics reader (e.g.
  `tools/throughput/retirement_metrics.h` + test). The `-fmetrics-out` format
  must be parsed without trusting the compiler.
- `tools/throughput/retirement_command.h`: `TP_RETIREMENT_COMMAND_ARGUMENTS`
  (256) and `_BYTES` (64 KiB) cannot hold a ~411-input batch. Raise them, or
  bind a digest-checked input-list file (see Q10).
- `tools/throughput/retirement_samples_test.h`,
  `retirement_measurement_test.h`: replace the one-row, 488-process fixture
  with a multi-fixture batch fixture, including failure controls for
  per-input nondeterminism, status mismatch and bad metrics.

**Campaign capacity**
- `tools/throughput/retirement_campaign.h`: `capacity` counts G, U,
  per-input observations and metrics artifacts in the store plan. Remove
  `TP_RETIREMENT_CAMPAIGN_WHOLE_JOB_BUDGET_NS` (one hour) in favor of a
  recipe-bound reviewed budget argument. `freeze` takes per-group command
  contracts. This implements the new capacity rules and the superseded budget.
- `tools/throughput/retirement_campaign_test.h`,
  `tools/throughput/retirement_capacity.py`: G/U arithmetic and budget
  preflight tests.

**Census / eligibility import and service**
- `tools/bench_service/retirement_validator_eligibility.c` (+ `_test.py`),
  `retirement_census_import.md`: emit the timed projection (eligible ∧ native
  target). The C projection must match the validator.
- `tools/bench_service/retirement_correctness*.c/h`,
  `retirement_oracle_authority.c/h`: freeze per-group batch commands, control
  statuses and diagnostic digests, and the once-parsed code facts. These are
  frozen before timing.
- `tools/bench_service/retirement_campaign_binding.h`,
  `retirement_campaign_service.h/.md`: bind group contracts into the
  campaign.
- `tools/bench_service/retirement_unit.c` and the retirement recipe
  entry: build the batch timed-child graph and carry the reviewed budget.
  This is the service recipe.

**Docs**
- `tools/throughput/README.md` ("Native-retirement statistics", "invocation
  evidence", "paired numeric samples"), `tools/bench_service/README.md`
  ("fixed worker budget is one hour" paragraph), `tools/bench_service/EXPORT.md`
  (capacity table) and `docs/agents/benchmarking.md` (one sentence). These
  describe per-row fresh-process timing and the one-hour budget.
- `.github/workflows/native-retirement-contract.yml` is a
  `TRUST_IMPLEMENTATION_PATHS` file. Update its path and copy lists only if new
  files are added (e.g. the metrics reader), and only through the trusted
  transition.

## (c) Open questions (each with a recommended default)

1. **Per-fixture wall inside a batch: TU interval or phase sum?** *Recommend
   the TU interval.* It runs from the start of that input through its object
   write, excludes metrics serialization, and captures inter-phase overhead
   that a phase sum hides. Phase sums remain diagnostic and must be ≤ the
   interval. (This is what the draft encodes.)
2. **Should batch process metrics be gated?** *Recommend retaining them
   without a threshold* (the draft), because gating them changes the family.
   The risk is that startup/teardown and process-global memory regressions
   (e.g. #1295 prewarm) are then ungated. The alternative is a
   `compiler_batch_*` metric pair with the group as cell, under the existing
   1.05/1.02 limits; that needs explicit approval as a family change.
3. **Do batch process metrics also get A/A qualification?** *Recommend yes,
   as diagnostic input to #426* (drift, order and multimodality checks
   reject the host). They should not be a precision criterion, since they are
   not family members.
4. **Is 60 pairs per fixture still the right minimum?** *Recommend keeping
   60.* The contract forbids reducing it; cost scales with 488·G, not with
   fixture count; lower per-input noise yields tighter bounds. Revisit only
   by a new versioned decision after real A/A data.
5. **Memory definition and name.** *Recommend committed high-water of the
   arenas owned by that input,* excluding lazily grown process-global
   tables, which show in batch RSS. Rename the token to
   `compiler_peak_memory` so that an RSS producer fails closed, and keep the
   C metric index so `retirement_stats.h` is untouched.
6. **Group count.** The 2.4 h estimate assumes 16 groups; today's
   declaration gives 80 object groups plus stage singletons. *Recommend
   accepting 80* (≈ +0.35 h) rather than adding per-input flags to the driver.
7. **Link/self-host stage rows** cannot batch. *Recommend singleton
   processes with process wall/RSS* (the v1 definitions). Ratios are
   unitless, and stage slices keep them separable.
8. **Which rejection/diagnostic controls share batches?** #508 rejection
   fixtures are not rows, and the six non-object controls are untimed.
   *Recommend appending the group's frozen controls after the timed members*
   as status-checked, never-timed inputs, with a frozen per-group expected
   exit status.
9. **Cross-target code bytes.** Taken literally, the decision makes them
   correctness-only (the draft), yet they need no timing. *Recommend keeping
   the cross-target code-byte gate from correctness artifacts:* it is free,
   and it is the only size gate for AArch64/Windows/macOS. This needs explicit
   approval.
10. **Batch argv size.** *Recommend a digest-bound input-list file* over
    raising the 256-argument/64 KiB command caps. Object paths stay
    `cwd/basename.o`, and the plan rejects basename collisions.
11. **Input order within a batch.** *Recommend fixed ascending row order*
    for both variants. Identical position effects cancel in ratios, and no
    new random domain is needed.
12. **Determinism scope.** *Recommend hashing every emitted object in every
    batch, warmups included,* and parsing code sections only once.
13. **Decision record.** Add the #36 comment permalink and date to the A1
    header before any binding.
