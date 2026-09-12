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

The analyzer CI job bootstraps the PR base's build driver and the candidate on
the same checkout, with the same Clang, then analyzes the **same split database**.
`--baseline-driver` runs that reference first and records metrics in `baseline.log` (diagnostics remain in the full CI log); its
failure fails the comparison. On this change's base the reference is the
monolithic, CPU-count-batched scheduler. On later revisions it measures whatever
implementation that reference contains. A main/dispatch run uses its own revision
as reference. No historical issue timing is presented as a current measurement.

`ANALYZE_BASELINE` and `ANALYZE_RUN` record complete wall microseconds and process
limits. `peak_pending_workers` is launched-but-not-yet-reaped worker concurrency;
actual analyzer overlap can be lower, particularly for empty or tiny shards.
On Linux, both runs also sample the coordinator and its descendants every
25 ms: `peak_live_processes` and `sampled_peak_tree_rss_bytes` report observed
process concurrency and summed resident memory. Shared resident pages count in
each process; sampling and process-exit races make this a lower bound on the
actual peak, not a PSS or physical-memory measurement. Other hosts report zero
samples for unavailable tree metrics.

`ANALYZE_SHARD` and `ANALYZE_AGGREGATE` record the largest child high-water RSS
from POSIX `getrusage`, in bytes. It is **not a sum of simultaneous process-tree
RSS**. Windows reports zero for unavailable RSS. Compare wall time and the same
eligible TU count alongside these memory/concurrency limits; no platform-wide
speedup follows from a single hosted-runner sample. CI retains the revision,
Clang version, database, CMake cache, manifest, shard reports and logs.
The initial complete comparison and its measurement limits are recorded in
[the CI performance audit](performance-audits/2026-09-12T192036Z.md).

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
