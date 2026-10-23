# Source preparation and independent body binding measurements

`source_measure.c` measures the available complete path on the same original
4-function and 4,096-function source workloads as the
[history handoff measurements](../c-environments-2026-10-23/README.md). Frozen
input hashes remain `be568cb5db8dd471edc19be730653ce43623143c033ccc8cc883b640e263dcee`
and `0e33678843ad025277ad6bf0c918c556e526d9421ca78c3f42ab3dd5f5dc47a7`.
These generated sources are synthetic supported-subset workloads, not external
corpus acceptance. The harness uses GNU17 with no standard includes.

Each timed call includes parsing, fresh type-table initialization, the existing
sequential source reader and its environment publication, expression checking,
body-span preparation, independent body binding, file fallbacks and retained
results. Body jobs run in reverse source order. It deliberately retains the
current whole-unit resolution/checking before the independent body consumer;
this qualifies the new consumer while deferred semantic checking is pending.
It establishes no compiler speedup by repeating name resolution.

The harness reuses the original source measurement helpers. Build on the
protected 9700X with its shared lock, then take that lock separately for each
process. The normal native flags are unchanged:

```sh
clang -O3 -march=native -Isrc -fwrapv -funsigned-char -fno-strict-aliasing \
  -Wall -Werror -DBUSTER_CHECK_MEASURE_EXTENDED -DBUSTER_MEASURE_ENVIRONMENT \
  docs/measurements/c-body-bindings-2026-10-23/source_measure.c -o body-source-measure
```

The command is `body-source-measure SOURCE [scalar|vector] [1|8]
[after-scalar-ms]`. The default selects the vector parser, graph schedule,
body-view preparation, local binding kernel and file lookup path, on one lane.
Scalar mode selects their exact twins. For example, run each invocation under
`EXCLUSIVE_OWNER=codex-113 EXCLUSIVE_TITLE="Issue 113 body binding"
EXCLUSIVE_LINK=https://github.com/buster14a/buster/pull/319
~/buster-fast-bench/exclusive.sh`:

```sh
taskset -c 2 ./body-source-measure source-small.c scalar 1
taskset -c 2 ./body-source-measure source-small.c vector 1
taskset -c 2 ./body-source-measure source-small.c vector 1 100
taskset -c 2 ./body-source-measure source-small.c scalar 8
taskset -c 2 ./body-source-measure source-small.c vector 8
taskset -c 2 ./body-source-measure source-small.c vector 8 100
```

Repeat for the large source and at least three independent processes per
configuration. Each warms up once and records 21 passes. Scalar conditioning
runs this same full pipeline for at least 100 ms before each vector sample.
Those scalar calls still contain shared preparation work, so this describes
the measured conditioning procedure rather than a universal frequency state.

A persistent repository `LaneTeam` is initialized before warmup. The calling
thread runs on CPU2, and the eight-lane group pins lanes to CPU2–CPU9; verify
the approved host's topology when collecting results. Workers and reusable
scalar replay storage survive samples. Per-unit span preparation, private
views, sorting, fallbacks, result allocation and dispatch/join are timed;
thread-pool startup and first workspace allocation are outside steady-state
timing. Empty replay heads are reused across bodies instead of clearing the
complete name universe per body.

After timing, compare every body result with a complete scalar replay's
original activation handles, the source reader's declaration column and a
source-order reference digest. Reference replay is outside timing. The existing complete
graph/type/output equality check also compares the timed path with a retained
scalar source run. No verification is included in the timed body result scan.

Report median ns/token and useful tokens/second. Hardware user cycles/token
are reported for one lane when available and unmultiplexed. Eight-lane cycles
are unavailable (`-1`): the existing per-thread counter does not measure
aggregate worker cycles. Report process-median ranges, not confidence bounds.
`body_result_plan_bytes` includes spans/result descriptors, use token/handle
columns and alignment; `persistent_replay_bytes` counts initialized private
scalar workspace. These exclude the source reader, parser, TypeTable and
transient kernel scratch, so they are not whole-process or peak-live memory.

## Controlled source observations

Head `2971a85c29ce32c3b6aa6ab178f970ab82417d4b` completed all 36 processes
on the approved 9700X, Clang 23.1.1 and performance governor. Every process
matched exact activation handles, declaration columns and complete checked
output. The [raw results](results.txt) retain process medians, fingerprints,
byte counts, digests and input/source/executable hashes. This snapshot precedes
the subsequent main integration and direct parser-trace test additions.

Each cell gives the observed range of three process medians, each over 21
passes. Eight-lane user cycles are unavailable for the reason above:

| Source, path/state | One lane ns/token | Eight lanes ns/token | One lane user cycles/token |
| --- | ---: | ---: | ---: |
| small, scalar | 204.827–214.391 | 161.453–168.712 | 309.584–312.337 |
| small, vector | 222.679–227.218 | 241.523–243.626 | 351.700–369.082 |
| small, conditioned | 305.840–346.206 | 258.300–274.753 | 551.753–593.765 |
| large, scalar | 56.085–56.416 | 55.825–62.850 | 291.659–293.154 |
| large, vector | 66.444–67.839 | 63.055–63.408 | 332.910–340.150 |
| large, conditioned | 68.635–71.066 | 65.877–66.303 | 338.893–353.287 |

The large vector path's median of process medians was 67.306 ns/token on
one lane and 63.138 on eight, with useful throughput 14.74–15.05 versus
15.77–15.86 million tokens/second. Scalar medians were 56.178 and 56.088;
their ranges overlap. These complete paths still repeat binding after
sequential semantic preparation. The observations qualify this consumer and
do not establish a compiler speedup or scaling of deferred semantic checking.

The small/large inputs have 243/237,579 tokens and 4/4,096 bodies. Each
body has at most 23 events; total file fallbacks were 4/4,096. No bounded-walk
fallback was needed in these sources; focused correctness tests exercise it.
Result plans and use columns retain 928/835,696 bytes. Private scalar replay
retains 8,032/57,120 bytes on one lane and 64,256/456,960 on eight; the batch
path retains none of that replay workspace. The memory exclusions above apply.

The same head passed 115,075 checker checks on the locked 9700X with ASan,
UBSan and leak detection enabled and fatal errors; no findings were reported.
Correct independent name resolution still needs integration with private body
types, source-order linkage effects, semantic/IR output and full corpus
equivalence for #113.
