# Clang analyzer shards

`build.c` owns this gate through `tools/clang_analyze.c`. The authoritative
input is the selected CMake `compile_commands.json`, with its original working
directories, compiler and configuration. It never discovers source files by
walking the checkout or synthesizes compilation flags from a module list.

The ordinary `clang_analyze` command and Ninja target now prepare a complete
manifest, launch independent native shard workers, and aggregate their results.
The existing optimized unity gate remains: a unity TU is indivisible. CI also
requires **Clang analyzer shards**, using a Clang Release, unsanitized,
non-unity tree so the split source graph is actually exercised. This job runs
independently of the desktop combinations; `CI complete` requires it.

## Reproduction

Bootstrap with `build.sh`/`build.ps1` as usual. Keep a configured tree and all
its sources and generated inputs unchanged until aggregation finishes. Generation
owns the build tree and must not overlap a reader. For a split analyzer tree:

```sh
./build.sh generate --build-directory build/analyzer-tree --ci --no-sanitize --no-fuzz --no-lto --linker DEFAULT -- -DBUSTER_UNITY_BUILD=OFF
./build.sh clang_analyze build/analyzer-tree --config Release --shards 8 --jobs 2 --results build/analyze-full
```

`--results` must name a fresh directory with an existing parent. Without it,
the driver creates a unique directory beside the database. Default limits are
eight shards, at most two simultaneous workers (also bounded by host CPU count),
and 600 seconds per TU. Available worker slots are refilled as shards finish. Each worker runs one
analyzer at a time, keeps going
after failures and reaps every child. `--timeout` changes the per-TU bound;
`--jobs 1` uses the same worker path with serial scheduling. The standalone
`--clang` override remains available for analyzer selection and test oracles.

Independent execution uses the same database and options at every step.
Bootstrap once, then invoke the built driver so parallel workers do not
concurrently overwrite the bootstrap executable:

```sh
build/build clang_analyze build/analyzer-tree --config Release --shards 8 --results build/analyze-manual --prepare
build/build clang_analyze build/analyzer-tree --config Release --shards 8 --results build/analyze-manual --shard 3
# Run the other zero-based shard indices in separate processes, then:
build/build clang_analyze build/analyzer-tree --config Release --shards 8 --results build/analyze-manual --aggregate
```

A single worker reproduces its own shard, not the complete gate. A second worker
claiming the same shard directory fails. To repeat it, prepare a fresh run.
`--shards 1 --jobs 1` exercises full coverage through one worker.

## Coverage and evidence

The manifest retains the complete original database, configuration, analyzer
projection, module assignments and run directory. SHA-256 binds each shard
report to that exact manifest. Configuration/language exclusions are counted;
malformed entries, unusable commands, duplicate TU identities and empty selected
coverage fail before work starts. The module key is the source basename without
`.c` or `_test.c`; fixed FNV-1a maps that key to a shard. Source/test pairs stay
together and assignments do not depend on database order or completion order.
Identical basenames in different directories share a shard but keep distinct TU
identities. Empty shards publish an explicit empty report.

Projection replaces the compile operation with `--analyze`, uses text
diagnostics and removes object/dependency output options. All semantic arguments
are retained in order, including build-host definitions previously discarded by
the old gate. Windows command-field decoding preserves escaped quotes and paths
with spaces. Commands must explicitly name the selected source. Response files
are rejected because they could override the projected action; they are not
emitted by the canonical database. The manifest records both original and
projected arguments.

Every TU gets a diagnostic log and one terminal row in its shard report,
including a SHA-256 of the log. Missing or changed logs fail aggregation too. A
warning on either output stream fails even with a zero exit status. Nonzero
exits, crashes, deadlines, launch failures and evidence write failures fail with
the source/shard named. Aggregation checks every expected shard and TU, rejects
duplicates, unexpected indices, wrong assignments, stale fingerprints, truncated
or extra rows, and reports omitted files. Worker failure does not suppress
other workers or the final accounting. A report is published only after the
worker finishes; absence is never interpreted as a clean analysis.

There is **no analysis cache**. Every new run reanalyzes every selected TU,
including transitive and generated headers, even when only one header changed.
Results are retained evidence, not permission to skip subsequent work. The
fingerprint proves command/coverage identity within an immutable run; it is not
an incremental provenance proof for a changed checkout or analyzer installation.

## CI controls and measurements

`./build.sh clang_analyze --self-test` compiles a small native process oracle and
exercises warnings on both streams, nonzero exit, crash, timeout, large concurrent
stdout/stderr output, failed launch, continued coverage, missing/duplicate/stale/
malformed reports, changed configuration, duplicate workers, invalid/empty
inventories, independent prepare/worker/aggregate, and a one-shard run. A real
Clang control checks relative includes and confirms a changed transitive header
is reanalyzed. Every desktop combination entrypoint and the independent analyzer
job run these controls.

The required analyzer job executes **one complete candidate analysis**, followed
by independent aggregation of its retained results. Pull requests, merge groups,
main/tag pushes and manual runs all use that same path. Changes to `build.c`, its
dependencies, the analyzer or the workflow cannot select a reference pass.
There is no `analyzer_comparison` workflow input or `--baseline-driver` CLI option;
removed comparison options are rejected rather than silently ignored. Existing
exact queue-to-main reuse remains separate: a valid reuse receipt can replace
fresh execution, but never an incomplete candidate run.

`Bootstrap and identify candidate build driver` compiles one driver with Clang's
`-MMD` dependency output. The existing `tools/analyzer_reference.py` helper now
owns candidate provenance only: it verifies compiler-selected repository inputs
against their Git blobs and binds the commit/tree, dependency/policy hashes,
executable SHA-256, resolved Clang identity/version, exact bootstrap command and
hashed compiler/driver environment. Missing policy inputs or incomplete provenance
fail bootstrap. No historical source tree or reference executable is created.
The module name and its read-only `load_selection` decoder remain for historical
source-pinned measurement readers; the comparison selector/materializer and their
CLI commands have been removed.

`Analyze candidate and aggregate all module shards` independently rebuilds that
manifest immediately before launching the candidate and requires byte equality
with the bootstrap record. Missing, symlinked, changed or stale evidence, source,
executable, Clang or environment fails before analysis. Every event binds the
executable context. This verifies the actual candidate, not equivalence to any
other driver. The native analyzer's command projection, selected TU set, worker
budget, deadlines and failure controls are unchanged.

The workflow then invokes `clang_analyze` once for execution and once with
`--aggregate` to independently verify the reports. Aggregation does not launch
Clang. `ANALYZER_POLICY candidate-only-v1` makes the policy visible in the job
log; `ANALYZE_RUN` records the complete candidate wall microseconds. No reference
result or `ANALYZE_BASELINE` success is fabricated. Historical comparison logs
retain their original meanings and are never relabeled as candidate-only data.
The frozen #2610/#2119/#2120 population reader still requires its original source,
producer blobs, dispatch inputs and selection-record schema; this removal does
not amend or qualify that historical campaign. Replay it at its pinned revision.
The generic timing reader accepts either historical or current campaign step
names, refuses missing/duplicate campaign steps and keeps different workflow
blobs in different measurement cohorts.

`peak_pending_workers` is launched-but-not-yet-reaped worker concurrency;
actual analyzer overlap can be lower, particularly for empty or tiny shards.
On Linux the coordinator and descendants are also sampled every 25 ms:
`peak_live_processes` and `sampled_peak_tree_rss_bytes` report observed process
concurrency and summed resident memory. Shared resident pages count in each
process; sampling and process-exit races make this a lower bound on the actual
peak, not a PSS or physical-memory measurement. Other hosts report zero samples
for unavailable tree metrics.

`ANALYZE_SHARD` and `ANALYZE_AGGREGATE` record the largest child high-water RSS
from POSIX `getrusage`, in bytes. It is **not a sum of simultaneous process-tree
RSS**. Windows reports zero for unavailable RSS. Compare wall time and the same
eligible TU count alongside these memory/concurrency limits; no platform-wide
speedup follows from a single hosted-runner sample. CI retains the revision,
Clang version, database, CMake cache, manifest, shard reports and logs.
The initial complete comparison and its measurement limits are recorded in
[the CI performance audit](performance-audits/2026-09-12T192036Z.md).

`python3 tools/analyzer_selection_test.py -v` executes the actual bootstrap and
campaign bodies with a logging compiler/driver. It covers each event, changed
root/transitive/analyzer/workflow inputs, one bootstrap and one candidate
execution, independent aggregation, stale/tampered provenance and fatal candidate
or aggregate failures. The native `clang_analyze --self-test` additionally rejects
both spellings of the retired reference option and retains all real child and
coverage controls.

A deliberate analyzer performance comparison can invoke separately built native
drivers explicitly outside required CI. Such an experiment is not a correctness
prerequisite and cannot automatically reinstate a reference pass. Removing the
measured reference work reduces this job's work; it does not establish the same
whole-CI latency saving when another required job controls completion.

## Opt-in worker-budget qualification

The routine default remains two workers. `--qualify-workers` runs four complete
candidate-only inventories sequentially in fixed `2,4,4,2` order through the
ordinary native scheduler. It requires at least four reported host logical CPUs
and four shards, and refuses `--jobs`, independent worker/prepare/aggregate modes,
and self-tests. Host logical CPU count is only a preflight;
affinity, quota, physical topology and memory still need independent inspection.

```sh
./build.sh clang_analyze build/analyzer-tree --config Release --shards 8 --timeout 600 --quiet --qualify-workers --results build/analyzer-budget
python3 tools/analyzer_worker_budget.py host build/host-context.json
```

The result directory contains one fresh manifest, shard reports, diagnostic logs
and `run.txt` per arm, plus `qualification.txt`. All arms run even after an
earlier failure; any failed arm or changed database/command inventory fails the
campaign. The only intended manifest difference is the arm's output directory.
`children_cpu_us` is the POSIX waited-descendant user+system CPU-time delta,
including workers and their reaped analyzers; zero means unavailable. It is
separate from shard wall times and hosted VM occupancy. Neither CPU time nor
sampled summed RSS proves physical-core availability or physical-memory use.

The manually dispatched `Analyzer worker budget qualification` workflow runs
two independent `ubuntu-26.04` trials, with one frozen driver and authoritative
split database per VM. It retains source/tree and binary hashes, Clang identity,
CPU affinity/topology, cgroup ancestor limits/counters, host memory/pressure,
all failures and full per-TU evidence. `tools/analyzer_worker_budget.py verify`
independently checks the selected union, exact commands/deadlines, all shard
fingerprints and diagnostic checksums across arms. It supports downloaded,
relocated artifacts and never changes a default or admits a performance result.
Run `python3 tools/analyzer_worker_budget_test.py -v` after building the native
driver at `build/analyzer-driver`; `BUSTER_ANALYZER_TEST_DRIVER` overrides that
path. Native self-tests also exercise complete and failed qualification arms
on hosts reporting four or more logical CPUs.

#2033's predeclared decision requires at least 10% wall improvement in each
order-balanced pair on both trials, no more than 5% additional child CPU work,
identical successful coverage/diagnostics, four CPUs of affinity/quota and
sampled summed RSS below 25% of the smallest known host/cgroup memory budget,
with no new OOM/swap pressure. Missing budgets or inconsistent evidence are
inconclusive. A benefit in analyzer VM occupancy is not a whole-CI speedup;
compare the other required jobs and report bounded timestamp replays separately.
Rollback of the experiment removes the opt-in workflow, reader/tests and
qualification option; the ordinary one-worker reproduction and indivisible
unity TU keep their existing path.

## Split-analysis contracts

The first complete split run found paths hidden by unity analysis. The gate
keeps every checker enabled and every warning fatal. The accompanying source
changes short-circuit varargs validation before reading missing types, decline
layout parsing without its parser context, preserve empty-copy validity and
check optional backend type/CFG storage. Tests no longer dereference failed
lexer outputs or invalid register indices after recording an assertion failure.
Existing internal invariants are explicit at their consumers: list endpoints
are published together, index arrays exist for counted nonempty ranges, local
rows and symbol rows share allocation, and metadata helpers retain value
storage. These checks document the existing validated-input contracts rather
than suppressing analyzer reports.
The lexer's final emitted lane comes from the existing contiguous low-bit mask
using a fixed one-bit shift. This keeps the end-of-window computation defined
without relying on the analyzer carrying a range proof for a variable shift.
