# Desktop tree phase evidence (#892)

`build.c` owns the optional `buster-desktop-phases-v1` observation. Set
`BUSTER_MATRIX_PHASE_OUTPUT` to a **fresh directory outside `build/`** before
`test_all_combinations_ci`. The twelve existing desktop jobs set it to
`RUNNER_TEMP/buster-ci/matrix-phases`; the ordinary desktop artifact retains
all records, including failed and interrupted attempts. Unset it for compiler
performance acceptance. Ordinary local builds and native/throughput jobs do
not enable it.

The observer prefixes existing commands without changing their graph edges,
launch order, targets, test selection, unity objects, or quotas. Windows retains
its pooled superbuild; Intel macOS retains its direct driver graph. A test
command remains part of its existing `test_all` target. Current main runs the
native test executable there, not a separate CTest payload. CTest worker quota
is consequently `not-applicable`; inherited `BUSTER_TEST_JOBS` is recorded,
and missing/unmeasured values remain `unknown`.
Native observation arguments occupy a separate `Generate.phase_arguments`
field so the canonical Release selector continues to inspect the original
compiler policy. The live CMake cache and graph remain provenance-bound.

## Authority and files

- `plan.json`: exact source commit/tree, build-source/driver hashes, workflow,
  run/attempt/logical job, platform/architecture/shard, detected logical CPUs,
  effective matrix CPU budget, outer quota, tree identities, authoritative
  coverage row IDs, compiler path/hash/version/target, configuration and
  sanitizer/fuzz/LTO/linker/generator policy, and expected phase tasks.
- `<task>.ready.json`: the existing native graph makes a configure/direct
  command eligible. All commands in one graph step share that enqueue time.
- `<task>.<pid>.start.json` and `.end.json`: one native observer per admitted
  command. Retain the exact child argv, inherited test quota, monotonic
  admission/child-start/child-end/publication-start timestamps, portable result,
  native wait status, and timeout/termination authority. CPU time and RSS are
  `unknown`. A missing end record is incomplete, never a zero-duration success.
- `terminal.json`: the original driver graph finished, including its coverage
  publication. It does not substitute for any child record.
- `summary.json` and `summary.md`: validated timeline, per-tree phase durations,
  admission/completion order, wait, overlap, idle gaps, critical tree, and
  explicitly conditional alternative-order predictions.

The existing producer capture/clean bookkeeping, Linux x86-64 census
validation/preparation, and final coverage publication are timed in the native
driver as `evidence` callbacks. Their authority is `driver_callback`, with the
actual callback result; no child exit word is fabricated. The producer clean
command contributes to build time, and the completion census contributes to
post-test time. Final coverage publication is a matrix-wide event.

All timestamps use the host monotonic clock (`CLOCK_MONOTONIC` or QPC) sampled
in the process owning that lifecycle transition. Workers on one host share its
origin; clocks are never compared between machines. Each child is waited for
immediately by its own observer, so the main driver's ordered reaping of
concurrent commands cannot inflate their end times. Publication-start is named
precisely: the subsequent atomic publication must succeed for the worker to
return success. The enclosing worker/driver completion proves publication
finished. Partial staging files are retained and make the consumer fail.

In pooled mode each build becomes eligible when the outer scheduler starts;
validation becomes eligible at its producer's completion. Consecutive validation
commands for a shared multi-config tree retain one pool-edge identity because
Ninja holds that slot across them. The self-host consumer is a separate
competing edge. Its time is retained, not attributed to compiler tests.

For a direct `test_all` command, the nested test observer separates compilation
before the test from the actual test payload. The enclosing command's measured
tail after the test, plus explicit native analyzer work, is `post_test`.
For a pooled test target, any pre-test work is included in build time by the
same rule. Absent tests are compile-only rows, not fabricated successful tests.

The exact configure/build/test argv retain relevant flags. Existing
`configure/` evidence retains CMake caches and configure logs; `zig-cache.json`
retains the verified cache identity. Runner/image metadata remains beside the
phase report and in the existing job result. GitHub's numeric job ID and API
wall timestamps are joined from the run inventory using the exact platform and
shard name; they do not order native phase events.

## Validation and failure behavior

`tools/ci_matrix_phases.py` joins the immutable plan to the existing coverage
manifest. It independently requires every selected row exactly once, every
required tree/phase, matching compiler/source/job identities, one admission and
one terminal record, consistent native exit authority, monotonic timestamps,
valid nesting/dependencies, and bounded pool occupancy. Unknown, duplicated,
missing, malformed, cancelled, timed-out, failed or interrupted observations
cannot yield a complete ranking. The existing `ci_summary.py` result fails when
phase evidence fails, even if the enclosing combination command says success.
The existing required-job aggregate and disjoint/exhaustive row policy remain
unchanged.

Run `python3 tools/ci_matrix_phases_test.py -v`. It compiles the actual native
observer, exercises success/nonzero exit/deadline controls, exports native
plans for pooled/direct release/checks at three- and four-CPU budgets, verifies
exact Git tree identity and canonical Release selection, and validates deterministic concurrent
fixtures including a fifth tree waiting behind four slots. It also emits five
`MATRIX_PHASE_OBSERVER_OVERHEAD` samples per hosted release lane. Each upper
bound includes observer launch/publication, temporary-fixture creation and
Python result-reading; it subtracts only the observer-measured child interval.
Keep every sample. These controls measure observer cost, not compiler throughput
or the total source-change cost of rebuilding the driver.

The fixed-duration prediction preserves pool/inner quotas and dependencies,
uses declaration priority among ready edges, and evaluates at most 5,040 tree
orders. It reports both current-order modeled makespan and observed pool span.
Contention, cache effects and runner variability are held fixed; a favorable
prediction is only grounds for a separately reviewed scheduling candidate.
Direct sequential trees predict no order-only saving under that model.

## Campaign completion

After merge, use a complete current-main Buster CI run. Record every attempt
and exact commit/tree/run/attempt/job identity for all six checks lanes. Join
runner/tool/cache evidence, inspect each critical tree and its largest/terminal
phases, and compare conditional order predictions. #891 already disposed of
the historical anomalous Intel-macOS observation as `timestamp-invalid`; do
not recycle its timings as a matched baseline. Update #709 and choose exactly
one #892 disposition from current evidence: focused Zig configuration work,
focused sanitized-test-tail work, a separate scheduling candidate, or no change.
The instrumentation PR alone is not that measured disposition.
