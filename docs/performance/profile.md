# First-party native profiles and Pages

The performance tools and `/buster/perf/` interface are original C and browser
JavaScript. The existing Clang, C/POSIX/Linux runtime, Git and browser are the
only local requirements. There is no added library, package setup, generated
upstream implementation, downloader, query service, embedded viewer or WASM.
Arrow IPC and Perfetto remain interoperable file formats. Independent upstream
readers used as qualification oracles remain outside the project.

## Collection and exact identity

```sh
bash tools/performance/build.sh
build/performance/capture NEW_CAPTURE_DIR 2 1000000 -- /absolute/buster benchmark-lexer src
build/performance/capture --no-sampling NEW_CONTROL_DIR 2 1000000 -- /absolute/buster benchmark-lexer src
build/performance/profile NEW_CAPTURE_DIR /absolute/buster NEW_CAPTURE_DIR/capture.pftrace
build/performance/trace-query NEW_CAPTURE_DIR/capture.pftrace [START_NS END_NS]
```

On the approved 9700X, **each** build, test, conversion and measured execution
takes its own existing `exclusive.sh` lock. Do not hold it around a campaign.
Keep the binary/input/configuration identical between profiled and unprofiled
executions and record order, independent repetitions and warmups. A frame-pointer
build improves user callchains; record its flags and source revision. Its results
do not establish the behavior of another binary or build configuration.

`capture` uses `perf_event_open` and a bounded mmap ring directly. It pins its
child to the specified CPU and observes the launched leader through a pidfd. It
records original `perf_event_attr` bytes, native records, stdout/stderr, before/
after executable SHA-256, exit/signal, clock calibration and resource receipts.
It has a 60-second deadline, a 64 MiB retained-record limit and an explicit
failure receipt. A workload that never launched has null execution timing with
a reason. Failed collection/workloads cannot become qualified native profiles.
The tool changes no machine permissions, packages or tracing service settings.

The collector supports user-mode software CPU-clock sampling, per-sample kernel
periods, PID/TID, CPU, timestamps and frame-pointer callchains. Its scope is the
selected CPU and observed leader lifetime. Tasks that move outside that CPU or
outlive the leader have unknown additional coverage. This is not a general
system profiler, DWARF unwinder or kernel-stack collector.

`profile` checks the exact event configuration and successful receipt, and opens
only the explicitly supplied matching executable. Its original ELF64 x86-64
reader checks executable segments, function symbols and GNU build ID. Other
libraries and addresses remain unresolved. Mapping and PC translation require
that matching binary; no file path from a capture is opened for symbolization.

The output is native Perfetto v58.2 `StackSample`, `InternedData`, `Mapping`,
`Frame`, `Callstack`, `ClockSnapshot` and `TrackEvent` messages. Kernel per-sample
periods become `primary_weight` with a nanosecond counter descriptor.
`PerfSample.timebase_count` is cumulative state and is never used as a period.
Source counts/weights are statistical attribution, not invocation counts or
instrumented function durations. User-mode observations do not prove that the
preceding CPU-clock period contained only user-mode work.

Only paired switch-in/switch-out records produce observed-running intervals.
Unknown initial/final edges remain omitted and counted. These source-labelled
native TrackEvent intervals are distinct from ftrace scheduling. Raw loss,
throttle, task and edge totals remain in the adapter report and original records.
The native PerfSample loss diagnostic covers `PERF_RECORD_LOST`; consult the
original report for separate `LOST_SAMPLES` and throttle counters. A zero loss
diagnostic alone does not prove complete unwind or scheduling coverage.

## Reader and view boundaries

| Source | Qualified first-party behavior | Explicit limits |
| --- | --- | --- |
| Native PerfSample v58.2 fixture | Ordered snapshots; root/leaf call trees; self/inclusive sample counts; thread/CPU and exact half-open filtering | Cumulative counter state supplies no inferred per-sample weight |
| Original Linux collector → StackSample v58.2 | Exact source periods; same sampling views; matching ELF symbols/build identities; source-labelled paired-running intervals | User mode, selected CPU and leader lifetime; unresolved libraries; no DWARF/source content or assembly |
| Native TrackEvent | Literal/interned names, begin/end intervals, instants, integer/double counters, explicit flows | Legacy/delta event encodings and incremental counters are refused; no dependency inferred from correlation |
| Native ftrace sched_switch | Paired running/off-CPU intervals; explicit runnable post-state; observed changes of endpoint CPU | Compact ftrace unsupported; sleeping wakeup boundaries, exact migration instant, blocking reasons and task reuse remain unknown |
| Superluminal packages | Original resource may be retained/downloaded | No direct proprietary package or instance-CSV sampling import is claimed |

The C query accepts supported dictionary-backed samples in one authoritative
clock domain; unsupported inline stacks/counter encodings or unaligned clocks
fail explicitly. The browser additionally handles inline StackSample task,
execution and counter descriptors and measured clock snapshots. Readers cap
native files at 64 MiB, packets/samples at one million, stack depth at 128,
retained frame occurrences at two million and decoding work at 32 million fields.
The C dictionary holds at most 262,144 entries. These are refusal budgets, not
peak-memory guarantees. A native file can have a valid complete-packet prefix;
the manifest's exact size/hash and capture receipt identify the intended artifact.

The worker retains the parsed native trace. Time queries do not claim arbitrary
byte/window slicing. Drawing caps are separate from query totals: up to 20,000
sample snapshots, 50,000 frame marks, 8,000 flame nodes and 1,000 function rows.
The native download remains available on unsupported, corrupt or oversized input.
The inspector has no cross-origin iframe/message bridge. One exact selection
drives all its views. Shared URLs contain dataset/trace identity and raw `[start,
end)` nanoseconds. Timeline dragging derives a displayed range from screen
coordinates; raw inputs remain exact. Source/assembly paths are never opened
without separately available, matching content.

## Static publication

```sh
build/performance/publish BUSTER_REPO SOURCE_COMMIT PERFORMANCE_REPO EVIDENCE_COMMIT \
    docs/measurements/performance-pages-2026-10-25/site-index.json \
    NEW_PAGES_ARTIFACT RECORDED_AT
```

The original C publisher reads source blobs using Git at the exact revision.
It validates each manifest/table and checks every evidence object against the
reviewed Git revision and declared size/hash. The matching evidence checkout
must be present for offline validation. It refuses missing/corrupt objects and
writes the complete catalog and generation seal only after qualification.
`perf/generation.json` identifies every published asset by SHA-256 and size.
Repeat with the same inputs/timestamp to reproduce identical output bytes.

The Pages artifact holds website code, schema and a small catalog. Original
tables/captures/harnesses/receipts stay in `buster-performance`, addressed through
immutable raw-GitHub commit URLs and verified by browser Fetch/Web Crypto.
Native profiles load on request. The frozen source census has ordered JSONL
partitions with ordinary Arrow schema sidecars and separate family drilldowns;
unconverted cells, failed/planned/unsupported family dispositions and original
precision remain visible. Alternative representations never add observations.

The site uses relative assets and hash routing below `/buster/perf/`. It needs
neither root service workers nor special isolation headers. There are no WASM
assets requiring a MIME exception. Preserve other Pages content when publishing
the `perf/` directory. Publication is a separate GitHub Pages build/deployment
step; a local successful seal does not establish deployed-site acceptance.

Charts discover metric annotations and exact columns. Lines, small multiples,
scatter and heatmaps preserve populations and missing gaps; distributions require
retained observations from one compatible population. Exact inspection/export
retains integers/decimals. Only plotting coordinates approximate after origin
subtraction. The fixed first-party renderer registry permits reviewed extensions;
measurement data cannot supply executable renderer code. Specialized model views
explain required work/traffic/energy/quality/lifetime assumptions when unavailable.
Comparison eligibility, independent units, paired/independent estimators,
uncertainty, coverage and exclusions follow [the reader contract](readers.md).

Production evidence, exact pins, commands, hardware overhead spread, independent
native queries and actual-browser receipts belong in
[the performance archive](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/performance-pages-2026-10-25).
