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

Build this identical harness against the baseline and candidate with the same
main changes. The baseline is merge tree
`2eada6eb5b62ed32242608cfd5e5c5750d9a06b4`, produced by `git merge-tree
--write-tree 6a2b4837e72b0f50ba998573d2fc32e8da3a9550
42630114aeb0553c3e2fabfd7fe9bc84b0ce6126`: the pre-handoff head plus main's
Tier 1 frame work. Archive that tree into the baseline snapshot. Copy this
harness into its same relative directory; keep its existing checker
measurement helper unchanged. Take the normal shared 9700X lock for
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

## Controlled source observations

Candidate `65b3951fc1bd1d9db30543f5bdc89b014777d995` has the same compiler
implementation as producer head `f881741`; it only repairs the standalone
harness's unused dispatcher declaration. On the approved Ryzen 7 9700X,
Clang 23.1.1 and the performance governor, both snapshots used the identical
harness (`f7330c715465edc28c36e40344c4d321ec4b26da316df8fbcfabbb0210e8baa3`)
and original checker helper. All 36 processes completed under separate shared
locks, pinned to CPU 2, with identical checked outputs. The
[raw results](results.txt) retain every process median, fingerprint, byte count,
input/harness/executable hash and revision boundary.

These are ranges of three independent process medians, each over 21 passes:

| Source, path/state | Baseline ns/token | Retained history ns/token | Baseline user cycles/token | Retained history user cycles/token |
| --- | ---: | ---: | ---: | ---: |
| small, scalar | 146.403–150.407 | 147.148–204.333 | 288.671–303.136 | 295.901–301.815 |
| small, vector | 211.342–216.786 | 207.675–217.115 | 324.280–338.235 | 324.770–357.568 |
| small, conditioned | 336.103–360.593 | 301.926–358.901 | 526.605–550.539 | 500.058–602.774 |
| large, scalar | 52.572–53.741 | 53.480–53.697 | 271.800–278.781 | 277.140–278.560 |
| large, vector | 56.600–58.099 | 58.597–60.273 | 278.463–286.907 | 288.983–298.121 |
| large, conditioned | 61.404–62.061 | 61.715–62.834 | 299.714–303.173 | 301.496–307.551 |

The large vector path's median of process medians rose from 57.597 to
59.635 ns/token, an observed 3.5% cost. Its useful throughput ranges were
17.21–17.67 versus 16.59–17.07 million tokens/second. Scalar medians were
52.839 versus 53.681 ns/token, and conditioned vector medians were 61.583
versus 62.521; their ranges overlap. The tiny input has substantial fixed
cost and spread, so these observations establish neither a universal overhead
nor a throughput improvement. Wall time includes arena operations; user
cycles exclude kernel work. Conditioned means the documented full scalar
pipeline ran for at least 100 ms before each sample.

The small source has 243 tokens, 143 expression nodes, 108 binding events,
six file activations and four bodies. History retains 7,960 bytes; input/output
arenas retain 44,828/19,448 bytes. The large source has 237,579 tokens,
143,363 expression nodes, 106,500 events, 4,098 file activations and 4,096
bodies. History retains 2,447,072 bytes; input/output arenas retain
38,321,676/19,497,368 bytes. Both contain one tag. These byte counts follow the
exclusions above and are not peak-live or whole-process memory.

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
On the same locked host, `65b3951` passed 420,202 environment checks and 69,425
checker checks with ASan, UBSan and leak detection enabled and fatal errors.
No sanitizer findings were reported.
