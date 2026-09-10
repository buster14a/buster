# Reproducible compiler-throughput experiments

This measures **Buster's implementation**, not the running time of its output.
The harness is native C with no third-party dependencies. `build.c` owns tool
and compiler construction; the standalone utility owns experimental design,
process measurement, deterministic inputs, raw data and statistical replay.
The existing compiler correctness matrix and canonical `test_self_host` gate
are unchanged. A timing result is not a substitute for either.

## Start here

From the repository root, after building two uninstrumented Release compilers:

```sh
./build.sh bench_throughput self-test
./build.sh bench_throughput run --baseline /absolute/base/ide --candidate /absolute/candidate/ide --output build/throughput-comparison --baseline-id BASE_COMMIT --candidate-id HEAD_COMMIT
./build.sh bench_throughput compare --output build/throughput-comparison
```

On GitHub-hosted machines, bootstrap `build/build` with the image's Clang as
`.github/workflows/compiler-throughput.yml` does, then use `./build/build` in
place of `./build.sh`. Canonical local bootstrapping still uses TCC. On Windows
use `build.ps1` / `build/build.exe` from a configured native developer shell.
The tool itself supports Linux, macOS and Windows; native harness tests run on
all three. Hardware counters currently have a Linux implementation only.

`bench_throughput self-test` runs both the statistical/hash tests and native
integration tests, including fixed-seed corpus hashes, rejected sample paths,
deliberately invalid result bundles and timed-out children. Their expected error
diagnostics are not compiler failures.

A direct standalone build is useful when diagnosing the harness:

```sh
clang -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic -fwrapv -fno-strict-aliasing -funsigned-char tools/throughput/throughput.c -lm -o build/throughput
```

Omit `-lm` on Windows. The Windows implementation links Psapi through its
source-level import pragma. No compiler is built inside a timed observation.
Do not build, test, profile or run another benchmark concurrently on the
measurement host. Use a new output directory for every run; existing results
are not silently overwritten. Comparison never downloads historical numbers
from a different runner and never automatically updates a golden baseline.

## Workload contract

All synthetic files have a specified xorshift32 seed and SHA-256 digest. There
are no system includes, timestamps, random device reads, undefined signed
arithmetic, or unbounded runtime loops in these generated sources. Functions
have external linkage; volatile globals and opaque calls prevent the principal
work from disappearing merely because it is unused.

| Case | What it stresses | `ci` profile |
|---|---|---|
| `tiny_startup` | New-process startup, metadata initialization, one compile and object write | One five-line function/file |
| `large_function` | One large body, expression/IR traversal and allocator scaling | 1,024 dependent statements |
| `many_functions` | Per-function setup and teardown | 512 small definitions |
| `symbol_table` | Long common-prefix names, declarations and permuted lookups | 2,048 referenced volatile globals |
| `control_flow` | Diamonds, loops, switches and joins | 64 functions, 16 diamonds each |
| `backend_pressure` | Live ranges, opaque calls, call clobbers, operand constraints and emission | 64 functions, 32 live values, eight call/update rounds |

`smoke` divides the non-tiny counts by eight. `full` multiplies them by eight.
`--scale N` multiplies again (1–64). `--seed N` changes the specified seed.
The metadata/CSV report exact bytes, lines and source definitions, rather than
estimating functions by counting braces. Full profiles can expose genuine
pathological compiler scaling; a timeout is a failed experiment, not a sample
that may be deleted to make the compiler look fast.

```sh
./build.sh bench_throughput generate --output build/throughput-inputs --profile full --seed 20260907 --scale 2
./build.sh bench_throughput run --baseline /base/ide --candidate /candidate/ide --output build/throughput-smoke --profile smoke --mode all --pairs 2 --warmups 1 --no-guard
```

Default modes are `none`, `mir-stack`, `fast` and `quality`, kept as **separate
series**. `--mode` selects one mode or `all`. Ordinary workloads use `cc -c
-g0 -O0 -fregister-allocator=MODE`. `--flag ARG` appends a common compiler
argument; it may not override the operation, output, metrics path or allocator.
The selected allocator is appended after every optimization flag because
Buster processes options in order and `-O` options reset that selection.
Keep target, optimization, debug and feature flags identical across A/B and
retain all include/SDK dependencies used by additional flags. The exact argv
and working directory of every invocation are recorded without shell parsing.

Tiny startup is **warm-filesystem, fresh-process latency**, not a cold disk
cache experiment. Every invocation is a new compiler process, including every
warmup and sample. The harness does not drop host caches, disable security
controls, change the governor or attempt privileged machine configuration.

## Measurements and their limits

`wall_seconds` spans process launch through wait completion. User and system
CPU seconds come from the operating system; `cpu_seconds` is their sum. Output
size is the produced file size, not a guessed `.text` size. Source throughput
uses actual lexed bytes/lines from `-fsource-metrics`; functions/second uses
known generated source definitions, not an assertion that all functions were
emitted. Self-hosting leaves function throughput unavailable and uses actual
lexed lines instead.

Peak RSS is the OS child high-water measurement: `wait4` on Unix (KiB converted
to bytes on Linux; bytes on macOS), and `PeakWorkingSetSize` on Windows. It is
**not** reserved address space, a sum of allocation sizes, or simultaneous
process-tree RSS. The resource APIs' child accounting semantics differ; do not
compare RSS across operating systems. Integration tests allocate/touch/free a
large buffer, then run a smaller child, checking that peaks are not a cumulative
maximum of previous trials. Compiler subprocess groups/jobs are bounded and
cleaned up on failure or timeout. All compiler work must finish before its
process exits; orphaned helpers are not valid benchmark work.

Linux `--cpu N` pins each child to one permitted logical CPU. `--cpu auto`
selects the first permitted CPU (also supported on Windows); CI uses this policy. Arrange an idle
physical core and idle SMT sibling for dedicated Zen 5 acceptance work; affinity
alone does not reserve either. An unsupported or invalid affinity request fails
instead of silently becoming unpinned. Without the option, affinity is inherited.
The bundle records CPU model/microcode, kernel, effective cpuset, cgroup quota,
SMT/governor information where readable, relevant environment values and binary
hashes. Missing platform facts are not invented. Revision labels are supplied
provenance, not a cryptographic proof of the source used to build a binary.
Retain build commands, compiler versions and CMake caches beside local bundles;
the CI workflow uploads both build caches and compile command databases.

### Hardware counters: separate diagnostic replays

```sh
./build.sh bench_throughput run --baseline /base/ide --candidate /candidate/ide --output build/throughput-pmu --cpu 4 --pmu
```

Choose a CPU allowed by your host rather than copying `4` blindly. The collector
uses `perf_event_open`, so no `perf` executable is required. It requests cycles,
retired instructions, branches, branch misses, cache references and cache misses.
These are Linux generic **user-space** events, inherited by descendants, not a
claim that generic `cache-misses` is an L1D, L2, instruction-cache or Zen 5 IBS
measurement. Counters are attached before exec, enabled on exec, and accepted
only when running for at least 90% of enabled time. Accepted values are scaled;
raw availability errors and scheduling fractions remain in `capabilities.jsonl`.
`--require-pmu` makes unavailable/insufficiently scheduled events an error.

Counter setup and reading are **not on the guarded timing path**. Three separate
replays per variant/case are retained in `telemetry.csv` and summarized under
`telemetry` in `summary.json`. These are diagnostic medians, not adequately
powered independent regression tests. Permission-denied, unsupported and
multiplexed-out observations are `NA`/`null`, never fabricated zero counts.

### Allocation requests: opt-in instrumented compilers

```sh
./build.sh generate --build-directory build/alloc-probe --ci -- -DBUSTER_BENCH_ALLOCATIONS=ON -DBUSTER_INCLUDE_TESTS=OFF
./build.sh build --build-directory build/alloc-probe --config Release -t ide
./build.sh bench_throughput run --baseline /base/ide --candidate /candidate/ide --allocation-baseline /base-instrumented/ide --allocation-candidate /candidate-instrumented/ide --output build/throughput-allocations
```

`BUSTER_BENCH_ALLOCATIONS` is OFF by default and preprocessing removes the hook
from normal allocation paths. The diagnostic build counts **calling-thread arena
bump requests and requested bytes**, including repeated allocations after arena
rewinds, excluding alignment padding. These are not libc `malloc` counts, OS
reservation/commit counts, peak live allocation, or all-thread totals. With opt-in `-fcompile-jobs=N`, these calling-thread keys exclude worker
bumps and must not be labeled whole-compiler allocation totals. The separate
exit census aggregates worker records; its snapshot and teardown scope differ. The metrics snapshot precedes report
formatting so the report does not count itself. Zero-size requests count as calls.

The two new fields are `allocation.arena_calls` and `allocation.arena_bytes` in
the existing extensible source-metrics file. Separate probes must emit these
fields and must produce exactly the same output hash as their uninstrumented
counterpart. Instrumented compilers are rejected as timing baselines/candidates.
For an old revision without the hook, backport only this diagnostic hook into a
separate probe build; do not measure that modified build as the timing baseline.

## Frozen-source self-host stages

```sh
./build.sh bench_throughput run --baseline /base/ide --candidate /candidate/ide --output build/throughput-self-host --profile full --self-host-root /frozen/buster --self-host-generated /frozen/buster/build/generated
```

Configure a frozen worktree once to obtain generated headers. Both compilers
compile **the same frozen unity source and headers**, not their respective
moving source trees. Stage 1 measures each trusted host compiler. Stage 2 uses
the stage-1 outputs as compilers and is a separate series. Full source/header
trees are hashed before/after; symlinks and special files are refused. System
headers/SDKs must also remain fixed and their build provenance must be retained.

A frozen source tree may differ from either original compiler revision. The
appropriate executable fixed point is then **stage 2 == stage 3**, checked for
each variant after measurements. This is deliberately distinct from Buster's
canonical own-source `test_self_host`; the latter remains unchanged and required
for compiler changes. Repeated outputs within every series must be identical.
`--require-identical-output` additionally requires A/B byte equality; leave this
off for intentional generated-code changes and run correctness tests separately.

With allocation probes, the harness also builds a separately instrumented
frozen stage-1 compiler outside timing trials, then uses it to measure stage-2
allocation requests. Instrumentation is not inferred from the parent compiler.
The instrumented frozen source must contain the hook. Unix self-host commands
include `-lm`; macOS SDK/framework and Windows system-include arguments must be
supplied to match the canonical platform command in `docs/agents/build.md`.

## Statistical comparison and the CI guard

Each of two **predeclared rounds** contains at least 20 adjacent A/B pairs per
workload/mode. Within two-pair blocks, one pair is AB and the other BA, with the
first order chosen by the recorded seed. Workload order is also shuffled. Two
warmups per variant/case precede timing. All raw observations remain; no
best-of-N selection, outlier trimming, selective cancellation or rerun-until-green.

The exact one-sided paired sign test asks whether a majority of pairs crosses
**both** a relative and an absolute practical margin:

* Wall time: candidate exceeds baseline by 15% **and** 2 milliseconds.
* Peak RSS: candidate exceeds baseline by 20% **and** 16 MiB.

Bonferroni corrects the nominal family alpha of 1% over every workload/mode and
both guarded metrics. The same metric/case must be significant in **both rounds**
to fail. Medians, median paired ratios, relative median absolute deviations and
conservative distribution-free median-ratio confidence intervals are retained.
The sign-test error model assumes independent pairs; shared-host contention and
serial correlation can violate that model. These nominal levels are not a
universal false-positive guarantee. Run A/A experiments on the intended runner
before tightening thresholds or adjudicating small changes.

A broad interval or a one-round-only signal is **inconclusive**, reported as a
warning rather than equivalence or a hard CI failure. `--no-guard` explicitly
marks small smoke experiments as diagnostic. Standalone utility exit statuses: 0 = no confirmed
regression (inspect warnings), 1 = confirmed substantial regression, 2 = invalid
or incomplete experiment. A missing/failed compiler, timeout, stale artifact,
missing metrics, nondeterminism, changing inputs, or corrupted result bundle can
never be discarded into a passing timing result.

CI builds both immutable PR revisions on one runner, sequentially, using the
same Clang/build-driver policy and generated workload. It runs all 24 synthetic
case/mode combinations with two 20-pair rounds. It does not cache compilers,
reuse another machine's timing baseline, weaken existing tests, or run untrusted
code under `pull_request_target`. Permissions are read-only and checkout
credentials are not persisted. Full self-host/PMU/allocation investigations are
explicit local or dedicated-runner experiments, not hidden costs in every PR.
The weekly run is an A/A health check; manual dispatch accepts a baseline ref.

## Result bundle

`metadata.json` contains schema, workload/binary hashes, flags, selected environment
and scope declarations. `jobs.tsv` names series. `samples.csv` contains all timing
observations; `telemetry.csv` contains PMU (`round=0`) and allocation (`round=1`)
replays, never ordinary timing rounds. `commands.jsonl` records exact commands;
`capabilities.jsonl` records process status and PMU errors/fractions. Compiler logs,
metrics and last outputs live in `artifacts/`. Per-sample output hashes remain
in CSV even though the last output file is reused.

A completed run seals the six primary machine-readable evidence files with
SHA-256 in `complete.txt`. Comparison rechecks the seal, strict row counts,
unique pair slots/order positions, numeric validity, invariant workload units
and repeat output hashes, then regenerates `summary.json` and `summary.md`.
The seal detects accidental corruption; it is not a signature against a party
who can edit both files and hashes. Incomplete runs retain diagnostics but have
no valid completion marker. CI publishes the summary and keeps raw evidence for
14 days, including failures; archive important acceptance bundles elsewhere.

## Per-site allocation reports

The same `BUSTER_BENCH_ALLOCATIONS` recorder also supports an optional
[allocation census](../../docs/allocation-census.md). Set
`BUSTER_ALLOCATION_CENSUS=1` for a separate diagnostic invocation and validate
its saved stderr with the offline reader. This exit report includes worker and
cleanup traffic, whereas source metrics retain the original calling-thread
pre-formatting snapshot. Keep each process log separate and do not substitute
census timings for the normal uninstrumented series.
