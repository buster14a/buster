# CI latency and throughput observations, 2026-09-30

Reference main: `6eb73e977c32699d8828ca73b05ded20262aec28`. Repository: `buster14a/buster`. Buster's first-party license remains unspecified/unresolved; `LICENSES/README.md` at the reference revision explicitly distinguishes upstream terms from a first-party license decision (#621). This observation does not redistribute external project code or add a dependency.

## Measurement and reproduction

Read-only GitHub connector collection paged the unfiltered repository run endpoint through pages 1–30 (`per_page=100`), selected the latest twelve encountered successful `Buster CI` runs by `created_at`, and fetched each run's jobs with `per_page=100`. Each selected run had exactly 25 jobs, so no second job page was necessary. An additional fourteen latest encountered failed runs were retained. Filtered collection endpoints returned unexpectedly stale September 6/18 results; those were excluded. Collection snapshots are not a stable historical database, and concurrent runs can change status while paging.

The [retained observation data](2026-09-30-ci-throughput-data.json) includes the
26 selected runs' exact source/attempt identities, normalized job timestamps,
derived cohort statistics, and the two exact-main artifacts' phase/module
summaries. Query `GET /repos/buster14a/buster/actions/runs/{run_id}/jobs?per_page=100`
for each listed run to inspect currently available job and step evidence;
historical artifacts can expire and live metadata can change. Queue time is
job `created_at` to `started_at`; dependency/pre-job waiting is workflow creation
to job creation; execution is job start to completion. Summed execution is
observed runner occupancy, not billing or CPU time.

| Cohort | N | Median workflow completion | Range | Median summed runner execution |
| --- | ---: | ---: | ---: | ---: |
| Successful sample | 12 | 31m21.5s | 24m46s–56m34s | 251m44.5s |
| Successful full Apple coverage | 10 | 31m35s | 24m46s–56m34s | 253m02s |

Two successes were draft PRs with eight Apple lanes deferred. Four concurrent merge-group revisions are correlated. Revisions, caches and runner conditions differ. This sample excludes failed, cancelled and still-active runs, and represents one short busy period rather than controlled performance acceptance.

| Lane | N | Median queue | Median execution | Maximum queue |
| --- | ---: | ---: | ---: | ---: |
| Windows x86-64 checks | 12 | 2s | 28m25s | 5s |
| Windows x86-64 release | 12 | 3s | 12m15.5s | 35s |
| Windows AArch64 checks | 12 | 3s | 2m36s | 4s |
| macOS x86-64 checks | 10 | 5m53s | 22m48.5s | 28m29s |
| macOS AArch64 checks | 10 | 6m27.5s | 13m27s | 29m46s |
| macOS AArch64 release | 10 | 7m33s | 8m37s | 30m22s |
| Linux x86-64 checks | 12 | 3s | 17m23s | 3s |

Windows x86-64 checks was the final substantive job in eight of twelve successes; macOS x86-64 checks was final in four. Latest successful run [36721841107](https://github.com/buster14a/buster/actions/runs/36721841107) lasted 56m34s, with macOS x86-64 checks queued 28m29s and running 24m33s. Its ARM checks queue was 29m46s. Removing Apple x64 lanes can relieve Apple capacity, but queue relief cannot be inferred exactly from these start times.

Successful run IDs: `36721841107`, `36717500853`, `36717399085`, `36717095137`, `36716787536`, `36716787001`, `36716786355`, `36716784298`, `36716673825`, `36716658400`, `36716582136`, `36716556584`. Exact source/head SHAs, attempts, branches, event types and job timestamps are retained in the observation JSON.

## Exact-main phase evidence

Run [36716787001](https://github.com/buster14a/buster/actions/runs/36716787001), source exactly `6eb73e977c32699d8828ca73b05ded20262aec28`, completed successfully. The Windows x64 checks job was `109891942560`, artifact `11097453356`; ARM macOS checks was `109891942476`, artifact `11096874588`. Both `matrix-phases/summary.json` documents report complete evidence, no errors, exact source identity and compiler hashes. Each JSON retains the original monotonic phase intervals; Ninja/Actions replay timestamps must not be used as fixture clocks.

| Windows checks row | Configure | Build | Runtime tests |
| --- | ---: | ---: | ---: |
| Clang sanitized Debug, fuzz available | 4.35s | 107.41s | 1567.95s |
| Clang sanitized Release | 4.33s | 377.77s | 398.75s |
| MSVC Debug compile/link | 5.18s | 86.01s | none by existing contract |
| GCC Debug compile/link | 6.01s | 227.90s | none by existing contract |
| Zig Debug compile/link | 1.02s | 376.18s | none by existing contract |

Windows CPU budget and outer pool were four; each sanitized Clang row had two inner workers and `BUSTER_TEST_JOBS=2`. Debug testing started about 115 seconds into the matrix and overlapped Release work. A config-only split cannot halve the 26-minute Debug test interval. Existing fixed-duration declaration-order replay evaluated all 120 tree permutations and predicted no improvement.

Windows Debug module durations: `compiler_driver_tests` 849.06s, `c_frontend_tests` 333.44s, `metamorphic_tests` 98.17s, `x86_64_metadata_tests` 51.65s, `aarch64_memory_semantics_tests` 44.15s, `machine_tests` 35.76s. Its 54 module durations sum to 1589.46s; actual test wall time is 1567.95s because eligible modules overlap. Windows Release's driver was 253.00s and frontend 76.63s.

ARM macOS checks used a CPU/outer budget of three. Shared sanitized Clang Debug+Release build was 270.23s, then Debug tests 352.65s and Release tests 115.84s ran serially. GCC and Zig compile/link intervals were 193.08s and 138.24s. Debug driver was 199.21s; Release driver 61.14s. All six tree-order permutations predicted the same total.

Current driver source was inspected at blob `e63aeb03aeb27a196750fee08d6a53361bcf6b6a`. Its major fixture loops are serial. `buster_test_worker_count` changes only an explicitly two-worker unit-batch control and a small prewarm gang in this file. The registered test runner's parallel gang covers three AArch64 modules. Raising the worker environment from two to four alone does not establish an improvement to the 849-second driver tail.

No `TEST_FIXTURE_TIMING_V1` records occurred: `BUSTER_TEST_FIXTURE_TIMING` was unset. Consequently no current fixture winner can honestly be named. The existing switch and `DRIVER_OPERATION_TIMING_V1` counters already distinguish in-process compilation, identity checks, host links/launches and execution waits. The cheapest attribution experiment is to enable those clocks on an exact Windows runner invocation before selecting a driver refactor.

## Projections and ownership

- Apple x64 removal replay excludes macOS x64 native/release/checks and iOS x64 while fixing surviving jobs' original start times. In ten full runs it frees a median 3074 runner seconds (51m14s, roughly one fifth of a complete run's runner occupancy). Median final substantive completion changes from 30m59s to 30m05s in this deliberately queue-blind replay. It does not model capacity improvements or constitute acceptance of the pending Apple-platform policy change.
- Pure Windows config splitting is unlikely to yield a large reduction with fixed phase durations because sanitized Debug already overlaps Release and dominates. Separate disjoint Debug module invocations have a fixed-duration optimistic bound of `max(849s driver, approximately740s other modules)`, about 14m09s of runtime instead of 26m08s. Charge duplicate initialization/build/artifact transport, CPU/memory contention and exact complete-module accounting before claiming this speedup.
- ARM macOS config splitting could overlap its serial 353s Debug and 116s Release test intervals, but must charge extra configuration/build/setup and scarce Apple runner occupancy. It was not the main critical path in this sample. Reconsider after Apple x64 removal.
- Preserve [#1885](https://github.com/buster14a/buster/issues/1885), draft [#1892](https://github.com/buster14a/buster/pull/1892), #949 and #709 ownership. Existing #949/#970 Windows diagnostic evidence, expressly historical rather than refreshed here, attributes 272.79/280.19s of native-frame/vector fixture time to in-process compiler work, while all listed external host/link/run components total about 5.64s. The issue rejects an unmeasured subprocess-dominance assumption; #1892 remains opt-in and disabled pending admission. The 3456 primary native-frame compiler cells and 648 strict primary fallback cells are source counts, not new operation timings.

Go: investigate module/process partitioning with independent exact coverage, enable existing fixture clocks, and measure full-run runner occupancy and completion on standard hosted runners. No-go: claim config-only halving, promise queue savings, tune declaration order again, attribute driver time to process creation, or enable #1892 based solely on these observations. The strongest counterargument to splitting is that extra duplicated builds and Apple queue demand can worsen throughput even when an isolated critical job becomes shorter.

## Current failures retained separately

The fourteen latest sampled failures all fail `CI complete` at `Verify every desktop partition exists`; eleven have successful substantive job conclusions. This is not evidence to weaken the aggregate. For [36721695412](https://github.com/buster14a/buster/actions/runs/36721695412), head `1262f189c7716b4403347cd7669a27324aaa752e`, attempt 2, job `109936844247`, artifact `11103774913` reports macOS x64 checks' required combination, result, Zig-install and log-retention step records all absent after four snapshots within the 30-second budget. A later connector snapshot still shows job `109936842936` completed success with `steps=[]`. This is a recorded API metadata omission and unresolved proof, not a compiler-test failure or a verified inventory implementation bug. Fresh evidence was posted to [#1984](https://github.com/buster14a/buster/issues/1984#issuecomment-5914556995) and historical #1015; the aggregate remains strict.

A separate sampled failure [36721573171](https://github.com/buster14a/buster/actions/runs/36721573171), Windows ARM Release job `109908182298`, failed verified Zig installation despite an exact cache hit; subsequent configure/result failures were consequences. It took 9m51s before job failure. The decoded job log confirms WinError 5 at staging publication, after successful exact archive restore; fresh evidence is posted to [#1493](https://github.com/buster14a/buster/issues/1493#issuecomment-5915401561). The locking process remains unknown; verification stays mandatory.

The telemetry collection phase was read-only. Findings were subsequently posted to their GitHub owners; the separate candidate implementation is described below. The offline parser completed successfully; compilation and performance acceptance gates were not run locally.


Apple removal subsequently landed in [#1990](https://github.com/buster14a/buster/pull/1990) at merge `55e0e562e841f98dd16daa3d20e139dd6b318eaa` on 2026-09-30. Buster CI changes from 25 to 21 jobs and Apple runner jobs from eight to four. The measurements above remain the pre-removal cohort; a post-removal timing comparison is still required.

## Recent primary research and the mechanism it supports

These papers and first-party engineering evidence inform the hypothesis; their measured gains are not Buster acceptance measurements.

| Primary source | Relevant observation | Consequence for this experiment |
| --- | --- | --- |
| [Juloori et al., *CI at Scale: Lean, Green, and Fast*, ICSE 2025; author preprint v2, 2025-05-19](https://arxiv.org/abs/2501.03440) | Duration-aware prioritization and bounded speculation improved resource use and tail waiting in Uber's studied monorepos. | Count aggregate resource demand and waiting as well as critical-path duration; faster isolated jobs can still overload a queue. |
| [Henderson et al., *Speculative Testing at Google with Transition Prediction*, 2025](https://research.google/pubs/speculative-testing-at-google-with-transition-prediction/) | Production speculative testing prioritized discovery of novel breakages; useful failure discovery differs from complete test-cycle completion. | Report earliest attributable failure separately from successful full completion. Keep the full evidence population. |
| [Fallahzadeh, Bavand and Rigby, *Accelerating Continuous Integration with Parallel Batch Testing*, ESEC/FSE 2023](https://arxiv.org/abs/2308.13129) | Feedback improvements were nonlinear in machine allocation in the studied Chrome history, because queue delays compound. | Measure saturation and contention; do not assume doubling workers doubles throughput. |
| [Fallahzadeh, Rigby and Adams, *Contrasting Test Selection, Prioritization, and Batch Testing at Scale*, accepted manuscript, 2025](https://mcislab.github.io/publications/2025/emse_scale.pdf) | Their studied test-selection approaches missed failures, whereas their evaluated batching preserved the failure population. | Preserve independently checked module coverage before considering probabilistic test omission. |
| [Pinterest Engineering, *Slashing CI Wait Times*, 2025-11-10](https://medium.com/pinterest-engineering/slashing-ci-wait-times-how-pinterest-cut-android-testing-build-times-by-36-feb6ff121d91) | Duration-based greedy sharding improved the slowest Android shard over a count-based split in this first-party case study. | Use measured duration to choose groups, conservative estimates for new modules, and a defined fallback; equal module counts are not equal work. |
| [GitHub Actions limits](https://docs.github.com/en/actions/reference/limits) | Hosted concurrency and execution limits bound available capacity. | More jobs buy latency only when the resulting runner demand fits the available capacity. No live quota change is proposed by this report. |

The external sources above are literature and service documentation, not inspected software projects or introduced dependencies. No implementation license is inferred from a paper describing a production system. Buster's first-party license remains unresolved as recorded above.

## Bounded diagnostic implementation and evidence protocol

The candidate separates the already measured expensive driver module from the remaining enabled registered modules into two isolated native child invocations. It retains the ordinary full invocation as the baseline. The native proof protocol supplies complete per-child registry rows (including disabled whole-table audits), immutable canonical indices, group ownership, selected rows, passed/failed assertion counts, ordinary terminal summaries, process status/capture/cleanup results, monotonic child intervals and the parent partition total. The exact source and executable SHA-256 bind the parent plan.

The new [offline measurement helper](../../tools/ci_unit_tests_measure.py) parses retained logs and independently supplied query inventories. The [integrity controls](../../tools/ci_unit_tests_measure_test.py) cover missing and duplicate evidence, omitted audits, changed canonical identities/counts, failed processes, unresolved source/binary identity, CPU oversubscription, native interval inconsistency, unfair timer scope and incomplete campaigns. These are diagnostic tools; they do not change an existing workflow or admission policy.

A sample manifest has this structure:

~~~json
{
  "schema": "buster-ci-unit-tests-measure-v1",
  "arm": "baseline",
  "mode": "serial",
  "identity": {
    "source_revision": "<exact 40-character source SHA>",
    "binary_sha256": "<exact 64-character executable SHA-256>",
    "runner_image": "<exact image version>",
    "platform": "windows",
    "architecture": "x86_64",
    "configuration": "Debug",
    "sanitize": true,
    "fuzz": true,
    "table_audits": false,
    "toolchain": "<exact compiler identity>",
    "cpu_budget": 4
  },
  "inventory": [
    {"index": 0, "name": "<canonical module name>", "table_audit": false}
  ],
  "log": "baseline.log",
  "exit_code": 0,
  "elapsed_us": 123456789
}
~~~

The example is a schema illustration, not executable evidence: the inventory must include every actual canonical registry row. Candidate manifests use arm=candidate and mode=groups; the helper verifies every native proof record before considering timing. Both arms' elapsed_us intervals must cover the same outer CMake/Ninja test lifecycle. A candidate outer interval must contain its inner native group interval; both are retained separately. If no candidate outer interval is supplied, the helper retains the inner interval for diagnostic use, which must not be compared against a broader baseline timer.

Campaign manifests contain schema=buster-ci-unit-tests-campaign-v1 and samples listing relative sample-manifest paths in actual execution order. Screening permits exactly one baseline/candidate pair; formal review requires at least three alternating samples per arm.

The screening stop rule is declared before hosted measurements: continue to a
three-pair campaign only when complete exact-count evidence passes and candidate
outer wall is at least 10% shorter. A neutral, slower, failed or incomplete pair
does not authorize enabling the option. The formal campaign must preserve
every module's assertions and audit policy in all six invocations and improve
median outer wall by at least 10%. This accepts only the isolated mechanism;
production adoption additionally needs an admission policy that respects the
whole runner quota and a matched full-matrix comparison with shorter checks
completion and no more than 5% additional total runner execution. Queue delay
and post-Apple-removal demand remain separate observations. None of these
conditions closes the broader five-successful-run #709 acceptance contract.

~~~sh
python tools/ci_unit_tests_measure_test.py -v
python tools/ci_unit_tests_measure.py sample sample.json --output observation.json
python tools/ci_unit_tests_measure.py screen campaign.json --output screening.json
python tools/ci_unit_tests_measure.py compare campaign.json --output comparison.json
~~~

The integrity suite completed successfully with 20 tests in this implementation session. No hosted baseline/candidate performance result is accepted by that check. Every successful parser output keeps ci_complete=false and performance_accepted=false. A one-pair screen also keeps measurement_review_ready=false; formal review readiness only means the declared evidence population is complete.

Report outer test wall time, every module's distribution and counts, the declared concurrent child-worker maximum, and summed child process wall separately. Summed child wall on one runner is not runner occupancy or CPU time. Full workflow latency, total runner occupancy, setup/build/transport cost, CPU/memory contention and ordinary supported-platform correctness remain separate acceptance evidence.

The smallest implementation decision remains conditional: retain this opt-in diagnostic route for measurements; enable a production partition only after equivalent coverage and useful end-to-end improvement are demonstrated. An incomplete or neutral campaign leaves existing CI unchanged. Coordinating ownership is #1826 with #709/#949 cost attribution and the separate #1885/#1892 batching work.

A source census found three `os_tests` concurrency assertions present at two test workers and absent at one. The diagnostic workflow therefore retains baseline workers=2 and candidate child workers=2 within an available overall budget of four, and records actual invocation workers separately from that budget. Current-source driver worker caps do not alter its passing assertion population between two and four; hosted counts must still establish equivalence. A strict four-worker production matrix cannot overlap two such partitions with concurrent compile/test trees. An all-build barrier plus serialized partitions preserves that quota but a fixed-duration Windows replay estimates only about 6.6% whole-matrix improvement, versus the optimistic 46% isolated test interval reduction. These remain proposals, not measured results.


## First hosted screening and review corrections

[Experiment 36749609684](https://github.com/buster14a/buster/actions/runs/36749609684)
used merge source `9520dfde9539d073b55f8d03fd4b1fffc5e06e88`. Its Linux
artifact `11115391066` records binary SHA-256
`d0a043536a6560573efbdf1da5c012662340c2fbc8f5f7360abcc81740cdc408`, image
`ubuntu26/20260927.149.1`, and Clang 23.1.2.

| Linux screening evidence | Baseline | Candidate |
| --- | ---: | ---: |
| Complete target wall, seconds | 974.489791 | 581.412628 |
| Passed enabled modules | 54 | 54 |
| Passed assertions | 4,811,053 | 4,811,053 |
| Driver module, seconds | 554.928048 | 573.931569 |
| Frontend module, seconds | 131.062990 | 131.827525 |

Exact per-module counts matched; both child exit, timeout, capture and cleanup
proofs passed. Candidate wall was 40.34% shorter. Its summed child process wall
was 1008.911861 seconds, 3.53% above baseline outer wall; that sum does not
measure runner occupancy or CPU time. This one pair passes the screening stop
rule on a dedicated four-CPU runner, with baseline workers=2 and two candidate
children of two workers each. It does not establish a benefit within an
ordinary two-worker tree quota, formal three-pair acceptance, or full-matrix
throughput. All acceptance flags remain false.

Windows failed before its candidate ran: nine driver assertions in the serial
baseline, comprising one COFF linker-result failure and eight unresolved
`_exit` links. The diagnostic workflow had switched from its Visual Studio
PowerShell bootstrap into Git Bash for native commands. The source-level
Windows environment-name incompatibility is tracked in
[#2014](https://github.com/buster14a/buster/issues/2014); causality for this old
attempt remains an inference because it did not retain environment names or
the resolved linker path. The workflow correction keeps Windows native
commands in PowerShell, retains SDK/CRT discovery across steps, and checks its
MSVC linker and CRT before running a fresh pair.

The [review on #2005](https://github.com/buster14a/buster/pull/2005) also corrected
three behaviors: an omitted source revision now resolves through a bounded
native Git query; source archives run with `unknown` identity and remain
ineligible for measurement. The fallback inherits output handles and adds no
capture quota or parent deadline. Every outer sample deadline is 6000 seconds,
with headroom over the 5400-second native child deadline. Clang, GCC, serial
Clang and TCC self-tests, ten synthetic native-invocation controls, twenty
comparison controls, ten campaign controls and actionlint passed. Synthetic
controls are functional evidence only; they execute no production compiler
module and make no speed claim.

Ordinary Windows x64 and Linux x64 checks at the first submitted head each
recorded 108 passing module results across their two sanitizer suites, with
partition mode disabled. Their run's required Windows ARM64 workflow-tools
step exhausted its existing five-minute outer budget during the separately
merged Visual Studio fixture suite. Exact evidence was posted to the owner
[#1582](https://github.com/buster14a/buster/issues/1582#issuecomment-5916784757).
The aggregate correctly remained failed; the reason those PowerShell fixtures
ran slowly is unproved. This does not waive ordinary correctness admission.

## 2026-10-01 module census and hosted screening

The source census below is bound to `src/buster/tests/test.c` blob
`8de0c6a3593aeb9014b2ea4f57194e0055374497`, present at source
`8f173cd9831e94357b340ba0d9d9b25d439b0e0d`. The x86-64 registry contains 56
modules: 53 serial descriptors and three existing parallel descriptors.
AArch64 contains 54: 51 serial and the same three parallel descriptors.
The parallel descriptors remain `aarch64_direct_simd_tests`,
`aarch64_complex_simd_tests` and `aarch64_memory_semantics_tests`.

The following census records every x86-64 serial descriptor in registration
order. Except for `compiler_driver_tests`, each stays in the original serial
rest sequence: individual thread independence has not been established.
`os_tests` additionally checks thread liveness and context ownership.
`x86_64_completion_census_tests` is a whole-table audit, excluded from the
ordinary audits-off invocation; it and `x86_64_tests` are absent on AArch64.

```text
byte_writer_tests
arena_tests
integer_tests
sanitizer_tests
hash_tests
simd_tests
string_tests
os_tests
file_tests
target_tests
truetype_tests
image_tests
c_frontend_tests
c_once_tests
c_type_layout_tests
c_macro_conditional_tests
record_layout_tests
metamorphic_tests
aarch64_encoding_tests
aarch64_exact_bridge_tests
aarch64_control_semantics_tests
aarch64_system_registers_tests
aarch64_semantics_tests
aarch64_system_semantics_tests
aarch64_syntax_tests
aarch64_semantic_vm_tests
aarch64_alias_projection_tests
assembly_tests
x86_64_forwarding_tests
x86_64_metadata_tests
x86_64_tls_tests
executable_padding_tests
x86_64_got_tests
x86_64_completion_census_tests
ir_tests
vector_contract_tests
llvm_bitcode_tests
machine_selection_tests
machine_tests
codegen_tests
aarch64_stride_tests
debug_model_tests
dwarf_tests
codeview_tests
pdb_tests
object_tests
jit_tests
link_tests
gpu_pipeline_tests
compiler_diagnostic_tests
compiler_driver_tests
compiler_driver_object_path_tests
x86_64_tests
```

The driver/rest process cut isolates compiler globals and lazy caches,
watchdog slots, thread contexts, arena pools and temporary-root state. Each
child retains initialization and prewarm; rest retains the original gang
boundary and deterministic replay. Temporary roots include PID, invocation
serial and monotonic timestamp, require exclusive creation, and are removed
only by their owner. The bounded driver audit found writable artifacts,
response-batch default objects and frame-vector caches under that owned root;
CWD changes are process-local and restored. The intentional `/dev/full` probe
and driver-only opt-in `BUSTER_ELF_DATA_OUTPUT` have no rest consumer or current
workflow setting. This supports the isolated cut, not additional in-process
parallelism for the remaining modules.

Partition admission requires at least four parent workers. Two child quotas
sum to at most that parent quota and each retains at least two workers;
smaller quotas use the original streaming invocation. Captures drain
concurrently and replay driver then rest. The 64 MiB capture limit and
5,400-second owned-process deadline reject incomplete, failed or unclean
execution; the serial fallback retains its original unlimited parent wait.
Existing individual synchronous fixture waits can remain unbounded within
the partition's overall deadline. Memory, CPU and repeated-run reliability
still require hosted qualification.

[Hosted screening run 36840375184, attempt 1](https://github.com/buster14a/buster/actions/runs/36840375184)
completed one baseline/candidate pair per platform on actual PR-merge source
`f3835072e6cd46af3573e47b803b10d86163e8fb`. Independently replayed native
intervals and inventory/assertion proofs give:

| Platform | Baseline outer wall | Partitioned outer wall | Reduction | Assertions in each arm |
| --- | ---: | ---: | ---: | ---: |
| Windows x86-64 | 1551.277493 s | 913.774750 s | 41.0953% | 4,964,144 |
| Linux x86-64 | 1109.190984 s | 678.297523 s | 38.8475% | 4,967,013 |

Both arms execute the same 55 modules, with zero external tests and the
whole-table audit explicitly skipped. Baseline uses two workers; candidate
uses two isolated children of two workers each under a four-worker host
budget. This is not a four-worker baseline comparison. Native exit,
deadline, capture and cleanup proofs pass. CPU time and peak RSS are unknown.
Summed child wall changes by +0.5609% on Windows and +13.4305% on Linux;
these sums do not measure aggregate CI runner seconds or CPU time.

Retained ZIP bytes match the published artifact SHA-256:

| Platform | Artifact ID | ZIP SHA-256 |
| --- | ---: | --- |
| Windows | 11154335209 | `754680f0c2b2f051d63e30f1d838e12d029453720a35a0e13d9febbd4aed6326` |
| Linux | 11153620583 | `488383d9c7320ec4b2eb14a9ae9795324dd683bf545b09b478b134b457a3c70c` |

The independent screening verdict is `screening_valid=true`,
`measurement_review_ready=false`, `ci_complete=false` and
`performance_accepted=false`. This separate one-pair cohort does not qualify
the newer integrated source, the Windows all-build barrier or split checks.
Those proposals retain their default policies until three matched complete
first attempts per variant and resource/reliability review satisfy #2119 and
#2120. The earlier cohorts above remain separate.
