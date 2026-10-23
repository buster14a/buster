# Checker source handoff measurements, 2026-10-23

These original C fixtures measure declaration production, binding resolution and
typed expression checking together. They do not establish SQLite acceptance,
backend throughput, object-file throughput or multicore scaling. Preprocessing
and parsing happen once outside timing. Each timed run initializes a fresh type
table and runs the production `check_input` and `check_graph_run` paths; it
includes their arena resets, allocations and diagnostic collection. Output is
compared after timing against a retained independent scalar run, including
graph topology, exact TypeIds, conversions, values and declaration identities.
The current revision also compares all retained source declaration/object
columns with `BUSTER_CHECK_MEASURE_EXTENDED`.

The two frozen inputs contain 4 and 4,096 functions with local declarations,
shadowing/restoration, calls to no external library, arithmetic and a conditional
assignment. They exercise the common subset accepted by original PR head
`d9b6144f23d0c03a84324e704742e9f101522c9d` and the expanded adapter. Clang checks
both as GNU17. The generator and fixtures are original project work; no external
source corpus is copied here.

| Input | SHA-256 |
| --- | --- |
| `source-small.c`, 4 functions | `be568cb5db8dd471edc19be730653ce43623143c033ccc8cc883b640e263dcee` |
| `source-large.c`, 4,096 functions | `0e33678843ad025277ad6bf0c918c556e526d9421ca78c3f42ab3dd5f5dc47a7` |

The separate corpus acceptance probe uses the existing external SQLite 3.53.4
amalgamation, SHA-256
`b1dd5d74ec7f29055a6684fa06fb3c2f6821c87dd38f9a458dfd2e8a1db28189`,
with the target's system headers. That source is not vendored. An unsupported
corpus produces a diagnostic, not a throughput number.

Build from the repository root on the protected 9700X, taking the shared host
lock for each build or invocation. Use the repository's unchanged native
compiler flags:

```sh
clang -O3 -march=native -Isrc -fwrapv -funsigned-char -fno-strict-aliasing \
  -Wall -Werror -DBUSTER_CHECK_MEASURE_EXTENDED \
  docs/measurements/c-checker-2026-10-23/checker_source_measure.c -o /tmp/checker-source-measure
clang -O2 docs/measurements/c-checker-2026-10-23/generate_checker_source.c -o /tmp/checker-source-generate
/tmp/checker-source-generate 4 > /tmp/source-small.c
/tmp/checker-source-generate 4096 > /tmp/source-large.c
clang -std=gnu17 -fsyntax-only /tmp/source-small.c /tmp/source-large.c
```

For the original head, omit `BUSTER_CHECK_MEASURE_EXTENDED`, since that revision
does not retain the added columns. The switch affects only post-timing equality
checks and statistics printing, not the measured code. Pin runs to CPU 2 and
invoke the lock separately for each process, allowing CI to go first:

```sh
EXCLUSIVE_OWNER=codex-checker-133 ~/buster-fast-bench/exclusive.sh \
  taskset -c 2 /tmp/checker-source-measure --only scalar /tmp/source-small.c
EXCLUSIVE_OWNER=codex-checker-133 ~/buster-fast-bench/exclusive.sh \
  taskset -c 2 /tmp/checker-source-measure --only vector /tmp/source-small.c
EXCLUSIVE_OWNER=codex-checker-133 ~/buster-fast-bench/exclusive.sh \
  taskset -c 2 /tmp/checker-source-measure --only vector --after-scalar 200 /tmp/source-small.c
```

Repeat all three commands for the large input and for three independent processes
per configuration. Each process has one warmup and 21 recorded passes. These
isolated runs measure repeated scalar work, repeated vector-path work and vector
work after scalar conditioning separately. The harness's optional default mode
alternates both paths; it is not used for those two-state comparisons.
`--after-scalar 200` performs scalar work for at least 200 ms before each vector
sample. This reports whole-call medians, not a trace of the initial frequency
transition inside a call. Counter results are user-space hardware CPU cycles;
denied or multiplexed counters print `-1`. Input/output arena bytes omit the
type table, parser, source text and scratch peak; they are not total memory use.

The standalone graph benchmark remains a different measurement: it omits the
source producer and binding handoff. A faster graph action does not by itself
establish a faster source-checking pipeline. Neither benchmark has a runtime
backend consumer yet.
