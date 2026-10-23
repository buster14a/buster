# Source environment handoff measurements, October 23, 2026

`source_measure.c` measures the available production path: parsing a frozen
preprocessed stream, fresh type-table initialization, the sequential source
reader, its retained environment publication, and expression-graph checking.
Preprocessing happens once outside timing. The original checker source
measurement's arena/type setup, equality checks, clock and median helpers
are reused directly. No external source or implementation is incorporated.

Use the original checker generator and frozen **4-function / 4,096-function**
inputs documented in [the checker measurements](../c-checker-2026-10-23/README.md).
Their hashes are respectively
`be568cb5db8dd471edc19be730653ce43623143c033ccc8cc883b640e263dcee`
and `0e33678843ad025277ad6bf0c918c556e526d9421ca78c3f42ab3dd5f5dc47a7`.
They contain declarations, scope restoration, shadowing and arithmetic in the
checker's supported subset. They are synthetic source workloads; they do not
establish SQLite, libc or c-testsuite coverage.

Build this identical harness against pre-handoff head
`6a2b4837e72b0f50ba998573d2fc32e8da3a9550` and the candidate. Copy this harness
into the same relative directory in the baseline snapshot; keep its existing
checker measurement helper unchanged. Take the normal shared 9700X lock for
each build and invocation. For the candidate, add
`-DBUSTER_MEASURE_ENVIRONMENT`; omit that definition for the baseline:

```sh
clang -O3 -march=native -Isrc -fwrapv -funsigned-char -fno-strict-aliasing \
  -Wall -Werror -DBUSTER_CHECK_MEASURE_EXTENDED -DBUSTER_MEASURE_ENVIRONMENT \
  docs/measurements/c-environments-2026-10-23/source_measure.c -o environment-source-measure
```

The definition enables candidate metadata and the optional query consumer.
It does not disable or alter environment publication in either build. The
baseline's `--queries` mode fails rather than silently timing no queries.

Run both paths separately, pinned to CPU 2. Repeat each configuration in at
least three independent processes, alternating baseline and candidate:

```sh
EXCLUSIVE_OWNER=codex-113 EXCLUSIVE_TITLE="Issue 113 source handoff" \
  EXCLUSIVE_LINK=https://github.com/buster14a/buster/pull/319 \
  ~/buster-fast-bench/exclusive.sh taskset -c 2 ./environment-source-measure \
  --only scalar source-small.c
EXCLUSIVE_OWNER=codex-113 EXCLUSIVE_TITLE="Issue 113 source handoff" \
  EXCLUSIVE_LINK=https://github.com/buster14a/buster/pull/319 \
  ~/buster-fast-bench/exclusive.sh taskset -c 2 ./environment-source-measure \
  --only vector source-small.c
EXCLUSIVE_OWNER=codex-113 EXCLUSIVE_TITLE="Issue 113 source handoff" \
  EXCLUSIVE_LINK=https://github.com/buster14a/buster/pull/319 \
  ~/buster-fast-bench/exclusive.sh taskset -c 2 ./environment-source-measure \
  --only vector --after-scalar 100 source-small.c
```

Repeat for the large input. The harness warms up once and records 21 passes;
each pass is compared with a retained scalar reference after timing. Equality
includes source declaration/object columns, graph topology, exact TypeIds and
typed expression values/conversions. Report ns/token, cycles/token, useful
tokens/second and observed process-median ranges. The printed integer-source
fingerprint must also match across baseline and candidate processes for these
frozen integer-only inputs; it hashes named fields rather than struct padding.
Denied or multiplexed cycle
counters print `-1`. Conditioned runs include at least 100 ms of the same
pipeline's scalar work before each vector sample; they do not isolate the
frequency transition within a sample.

The candidate's additional `--queries` mode consumes every ordinary/tag use
event through the file-position index, in batches of 16 with partial tails.
Some of these uses have a local declaration and would not reach the file
fallback in a deferred checker. This is a reproducible query trace measuring
the available producer/handoff/query consumer, not an implementation or timing
of deferred semantic checking. Its digest must match scalar position queries.

`environment_bytes` includes the retained event view, five u32/two byte event
columns, completion positions, body slots, file index and allocation alignment.
It excludes the already retained declaration/type columns, TypeTable, parser,
preprocessor and transient storage. Input/output arena sizes are also printed;
neither quantity is whole-process or peak-live memory. No source throughput
improvement is claimed from the earlier isolated environment measurements.

Results for this new production handoff remain pending target-host validation.

`test_sanitize.c` is a narrow entry point to the same production test commands
for separate sanitizer qualification. Build under the shared lock with the
normal native flags plus `-O1 -g -fsanitize=address,undefined
-fno-sanitize-recover=all -fno-omit-frame-pointer`, then invoke each command
under its own lock with leak detection and fatal sanitizer errors enabled:

```sh
./environment-sanitize test-environments --seed 113
./environment-sanitize test-checker --seed 133
```

This does not replace the normal optimized build, full CI or call-graph gate.
