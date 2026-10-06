# Whole-compiler work ledger

The work ledger counts what the compiler does, exactly and independently of the
machine it runs on, and groups the counts by **mechanism** rather than by
function: re-deriving answers from token ranges, materializing rollback
snapshots, touching whole tables per query, re-decoding literals, hashing,
preparing target tables, producing machine records and output bytes. It exists
to rank avoidable work and to prove that a change removed work instead of
moving it. It is never a timing subject.

## Build and read

The counters live in the existing allocation diagnostic build
(`BUSTER_BENCH_ALLOCATIONS`); ordinary builds expand `WORK_LEDGER_RECORD` and
`WORK_LEDGER_PHASE` to nothing and keep no storage. Build a diagnostic compiler
in a separate directory and ask it for metrics. First use the
[session-owned detached worktree](agents/workflow.md#parallel-sessions-on-one-machine)
and run from `"$session_root/src"`; the session directory's generated name
keeps this diagnostic tree distinct:

```sh
census_build="build-$(basename "$session_root")-census"
./build.sh generate --build-directory "$census_build" -- -DBUSTER_BENCH_ALLOCATIONS=ON
./build.sh build --build-directory "$census_build" --config Release -t ide
"$census_build/Release/ide" cc -g0 -c tests/basic_c_operations.c -o "$census_build/o.o" \
    -fsource-metrics="$census_build/o.metrics"
grep '^work\.' "$census_build/o.metrics"
```

Retain the diagnostic binary hash, cache and compile commands with its metrics.
Keep its build and output paths owned by this session, and wait for every user
before regeneration or cleanup. These counters are diagnostic evidence; use
the separate tests-off [trusted performance recipe](agents/benchmarking.md#benchmarking-a-compiler-change-ab)
for timing comparisons.

Every counter is written as `work.<mechanism>.<name>=<count>` beside the
existing `ir_construction.*` and `allocation.*` keys; keys are only ever added.
`work.version` is 1. The families, and what one unit of each is, are listed in
the orientation comment of `src/buster/lib/compiler/work_ledger.h`.

`work.phase.<phase>.<field>` rows attribute two sources to the pipeline stage
that was current when they happened: minor page faults (first touches of pages,
from `getrusage`; zero on Windows) and the calling thread's arena requests
(`arena_calls`, `arena_bytes`, `arena_zero_written`). The phases are `startup`,
`preprocess`, `parse`, `semantic`, `lower`, `prepare`, `target_prewarm`,
`codegen`, `object` and `output`; `marks` counts entries. Each row is a
difference of two samples of the same monotonic totals, so the rows of one
invocation partition its totals exactly (`compiler_driver_test_work_ledger`
asserts this for arena traffic). Arena bytes are requested, not touched: a
per-query scratch array sized by a table that is only partly written counts in
full here and costs nothing physical; the fault rows are the touched-memory
measure.

## Limits

Counters are calling-thread totals, like `ir_construction`: opt-in
`-fcompile-jobs` workers are not aggregated. Minor faults are process-wide, which
is exact for the default one-worker compile. A counter counts events at its
site, not every equivalent operation elsewhere (for example batch identifier
interning in the lexer does not pass through `c_symbol_intern`). Counts are
exact for a given source, compiler and flags; they do not price an operation.
Pair them with callgrind instruction totals of the uninstrumented compiler for
cost, and with ordinary tests and the self-host fixed point for correctness.

## Corpus runner

`tools/work_ledger/run_corpus.py` compiles a frozen corpus with a subject's
ledger and plain builds (`tools/work_ledger/build_subject.sh` makes both from one
checkout), requires the two to produce byte-identical outputs, records every
unit's metrics and rusage, and optionally runs the plain build under callgrind.
The self-host unit compiles a pinned tree given by `--self-host-root`, so a
subject that edits the compiler's own source still compiles the same program,
and every unit pins `-march=znver3` so outputs do not follow the host CPU (or
valgrind's CPUID). `tools/work_ledger/fixtures.txt` freezes the repository
fixtures that compile with plain `-c` at the pinned tree. The external inputs
are the pinned cJSON, Lua and SQLite archives of
`tools/throughput/workloads/`.

`tools/work_ledger/ledger_report.py base=results.json candidate=results.json ...`
sums every counter per corpus family and over the corpus, prints the mechanism
ledger and the phase attribution, and for each later subject the per-counter
change, the callgrind instruction change and whether every plain artifact is
byte-identical to the baseline's. Counts are summed, never averaged, so work a
change moved to another counter remains visible in the same table.
