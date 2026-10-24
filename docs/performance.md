# Buster benchmark manifest: Perfetto and Arrow/JSONL

Revision 2, October 24, 2026. This replaces the earlier unpublished BMF profile
table proposal. It is a design with executable data fixtures and validation;
the Buster website and production capture importers remain implementation work.

Tracking: [roadmap #384](https://github.com/buster14a/buster/issues/384) and
[implementation plan](performance/implementation-plan.md). The
[current manifest schema](performance/buster-manifest-v1.schema.json) and its
[previous-filename alias](performance/bmf-v1.manifest.schema.json) are here;
[reference examples, coverage, provenance and validation receipts](https://github.com/buster14a/buster-performance/tree/5a022380cab1843b73627920d2e8a58dfe43ec42/docs/measurements/performance-format-2026-10-24)
are pinned in buster-performance at `5a022380cab1843b73627920d2e8a58dfe43ec42`.

## Decision and ownership

Use a **thin Buster manifest around existing formats**:

| Responsibility | Authority |
| --- | --- |
| Sampling profiles, stacks, symbols, process/thread identities, scheduling, clocks, instrumented spans, flows, allocation and device trace data | Native **Perfetto protobuf traces** and upstream importers/Trace Processor |
| Arbitrary performance measurements, distributions, experiment sweeps and analysis tables | **Apache Arrow IPC**; **JSON Lines** for readable producer output |
| Dataset/build identities, metric meaning and units, timing scope, repetition/independence, provenance and comparison eligibility | The **Buster JSON manifest** |
| Profile inspection | A pinned **Perfetto UI** embedded in Buster Pages |
| Commit history, comparisons, general charts, coverage and graph extensions | Buster's website using Arrow/JSONL tables and manifest metadata |

Buster defines neither a binary codec, a replacement profile/stack schema, nor a
general trace viewer. Arrow owns table physical types and nullability. Perfetto
owns the trace representation and its interpretation. The manifest links those
resources and supplies the benchmark semantics necessary to compare results.

New metric names, units, dimensions, columns, nested values, workloads and
quantities do not require a new manifest version. Unknown measurements remain
readable through Arrow/JSONL and generic tables/charts. A specialized graph may
need a website renderer or a Perfetto plugin, independently of storage.

## Evidence and retained requirements

The inspected `buster-performance` snapshot is
`d3453eb2099d1e4dcd9c997720fcf6e0d86b81ea`: 19 measurement families and 1,043
tracked paths under `docs/measurements/`. Buster was inspected at
`a7741e836353642041aca0e3c653f38226153358`, repository ID `1421805693`.
[`existing-data-coverage.tsv`](https://github.com/buster14a/buster-performance/blob/5a022380cab1843b73627920d2e8a58dfe43ec42/docs/measurements/performance-format-2026-10-24/existing-data-coverage.tsv) retains the complete family mapping.

The evidence already requires process medians versus raw observations; matched
pairs/order/warmups; scalar conditioning; different measurement windows;
unavailable hardware counters; retained storage versus RSS; exact code/input
identities; correctness/coverage; negative results; and incomplete campaigns.
Reusing formats preserves all these requirements.

For example, foreign-string `samples.csv` contains 288 process/case/path
summaries over eleven links, with no retained individual link observations. Its
twelve resource rows cover complete processes, including conditioning and all
cases. TLS writer timings exclude work covered by whole-command perf counters;
the historical `workers` column is workload size, not writer lane count.
Cloud zero and checker `-1` counter placeholders sometimes mean unavailable.
Preprocessor digests can exceed JavaScript's exact-number range. The import
must preserve these meanings rather than infer them from column names.

Original evidence stays in `buster-performance`, following its
[frozen storage/provenance policy](https://github.com/buster14a/buster-performance/blob/d3453eb2099d1e4dcd9c997720fcf6e0d86b81ea/README.md).
Compiler design, native tools and website source stay in Buster.

## Resource layout and the thin manifest

```text
experiment/
  manifest.bmf.json                  Buster metadata and resource references
  measurements.arrow                ordinary Arrow IPC stream
  measurements.jsonl                optional equivalent readable representation
  measurements.schema.arrow         ordinary zero-row Arrow schema stream
  executions.arrow
  capture.pftrace                   native Perfetto Trace protobuf
  source-capture.perf.data           original capture, when retained locally
  MANIFEST.sha256                   detached file integrity manifest
```

`manifest.bmf.json` is retained as the entry-point filename for continuity with
the first draft; its `format` is now `buster-benchmark`, version 1. The previous
custom stack-table contract is superseded. `buster-manifest-v1.schema.json` is
the current structural schema; `bmf-v1.manifest.schema.json` contains the same
schema at the previously linked filename.

The manifest contains these application-level objects:

| Object | Contents |
| --- | --- |
| `resources` | IDs, ordinary format identifiers, relative paths or immutable URLs, SHA-256, byte counts, optional row counts and original-source references |
| `tables` | Resource IDs for ordered partitions, optional equivalent representations, an Arrow schema resource, row identity/relationships, dimension columns and measured-column annotations |
| `traces` | A native trace resource, execution identity, profiler configuration, source artifacts, capability summary and fidelity limitations |
| `contexts` | Builds, environments, workloads, protocols, executions or other identified facts, including explicitly unknown facts |
| `metrics`, `scopes`, `instruments` | Meaning, units, direction, boundary and measurement method for annotated table columns |
| `census`, `limitations`, `provenance` | Planned/observed coverage, incomplete/failed work, importer/analysis versions and evidence identity |

Only the common header and resource inventory are mandatory. There are no
physical column type definitions, frame/stack dictionaries, clock models,
profile-table role packs, or graph executable code in the Buster manifest.
The manifest's size follows its benchmark metadata, not the sample count.

Each resource uses a relative path or an immutable URL, never both. Local paths
exclude traversal, URL schemes, percent escapes and backslashes; resolved paths
must remain inside the dataset. Remote locations declare access/CORS needs in
metadata and remain content-verified. Original command strings and paths are
evidence text, not executable instructions.

Publish resources first and seal the manifest last. `complete`, `partial`,
`aborted` and `planned` describe the dataset's actual scope. Missing or corrupt
resources fail validation. The SHA-256 of exact manifest bytes identifies the
generation; the checksum is detached to avoid a circular self-hash. Checksums
prove content identity, while trusted publication and policy establish acceptance.

Readers check the manifest version and required capabilities before interpreting
a resource. Unknown measurements/optional metadata remain accessible; an unknown
required encoding or semantic extension disables dependent views with an explicit
reason. Publication checks references, field existence, unique IDs, row counts,
timestamp syntax, hashes and comparison semantics beyond JSON Schema structure.

## Arrow and JSON Lines tables

Arrow IPC stores the physical schema alongside record batches. Use the ordinary
Arrow schema and extension metadata; do not translate it into a Buster type
system. V1 publication starts with uncompressed IPC streams and ordinary record
batches, using types supported by the pinned browser reader. Unsupported types
remain preserved and downloadable; a view requiring them is explicitly disabled
until its reader supports their storage and semantics.

A readable JSONL representation references an ordinary Arrow IPC stream carrying
the same schema and zero rows. This small sidecar supplies types/nullability
without introducing another physical schema language. Each nonblank UTF-8 line
is one row object. Fields and nullability agree with Arrow. The narrow JSON
mapping is: 64-bit integers and decimals use exact decimal strings; small integer
types use JSON integers; booleans and UTF-8 use JSON booleans and strings;
finite floats use JSON numbers; binary uses base64; lists and structs apply
these rules recursively. Null is JSON null. Arrow extensions use their declared
storage representation only when that storage fits this mapping. The Arrow
schema controls integer bounds and decimal precision/scale. Other Arrow types
and nonfinite values remain available in native Arrow; a JSONL alternative
requires a separately declared mapping before equivalence can be claimed.
Neither encoding silently converts uint64, Decimal128, nulls, NaN sentinels or
unavailable counters into ordinary zeroes.

Partitions in a table's primary `data` list are read once in their declared
order. An `alternatives` entry is an equivalent representation of that complete
table, not extra rows to concatenate. Validate equivalence using exact typed
values, keys and order. An empty table has a valid schema and zero rows.

The table shape is unrestricted: wide measured columns, long-form observations,
nested arrays, distributions, sparse relationships and application-defined
tables all use the same existing Arrow machinery. Bind arbitrary columns to
chart axes, facets, grouping, color or size in the website. Renderer settings
are website configuration and have their own version; they are not a new data
exchange grammar.

Column references use exact Arrow field names, including Unicode and spaces;
they are distinct from the manifest's compact resource/context identifiers.
Repeated field names are rejected for JSONL equivalence and semantic binding.

## Metric and experiment semantics

Each measured column references a metric, scope and instrument. Its annotation
identifies the statistic, population/sample level, retained observations, sample
count or row binding, validity/reason columns, intervals and any derivation.
Context bindings identify execution, build, workload, protocol and dimensions.
Whole-process resource values live once in an execution table.

Metrics have open names and quantities, explicit aggregation semantics and a
direction of `lower`, `higher`, `neutral` or `target`. Units use a display symbol,
an open dimension map and an exact rational scale, with an optional affine
offset. For example, `ns/token`, `bytes/task`, custom PMU events, energy, work
quality, and simulator values need no new manifest version. Unit conversion
normalizes dimensions and scale; affine offsets are resolved before arithmetic.

Keep `core_cycle`, `tsc_tick`, `instruction`, `retired_macroop`, `token`, and other
work quantities distinct. A measured clock calibration can derive a time
estimate from ticks; it cannot turn TSC ticks into measured CPU core cycles.
Retained arena lengths, capacities, peak RSS and allocation traffic are separate
metrics. Original rounded values and higher-precision observations are retained
at their actual precision; display rounding is a final presentation step.

Missing/invalid measurements use null plus a reason. Valid zero remains zero.
Recommended statuses are `ok`, `missing`, `invalid` and `censored`; source-specific
sentinels are interpreted through a versioned adapter and preserved in the
original evidence. Censored observations retain their bound/window convention.
Unknown facts do not acquire invented defaults.

Record exact source/tree/dirty patch, binary hash, benchmark/harness revision,
toolchain/options, frozen input/header/configuration identities, seed, relevant
environment, host/CPU/microcode/kernel, affinity/lanes, governor/SMT, storage,
cache/reset lifecycle, isolation, warmup, conditioning, order and whether the
execution was profiled. Host compiler ISA, generated-program ISA, ABI and the
machine running generated code remain separate facts.

## Sampling profiles and arbitrary trace measurements

Store a native Perfetto trace as the profile's queryable representation.
Use upstream messages and importers for CPU stack samples, interned frames,
mappings and symbols, thread scheduling, TrackEvent slices/counters/flows,
heap profiles, clock snapshots, device queues and quality/loss information.
Keep the original `perf.data`, supported Superluminal export/package or other
source capture, its configuration, and the adapter version.

The manifest links the trace to the exact execution and lists verified
capabilities. It does not reproduce Perfetto's internal schema. Arrow tables
may contain derived hotspot summaries, historical comparisons or measurements
not expressible losslessly in a chosen trace path, but they are not mandatory
copies of every native stack, thread and sample. Mark derived tables with their
source trace digest, pinned Trace Processor/SQL, selection and denominator.

Preserve timestamped stacks, sample period/weight semantics, CPU/thread identity,
privilege/event domain, unwind quality, code identity, clock conversions and
losses when the source supplies them. For example, Perfetto `PerfSample`
`timebase_count` is cumulative counter state, not an individual sample weight.
An adapter must use the upstream meaning rather than invent a period from it.
The included synthetic fixture uses observed sample counts, not estimated CPU
durations.

Retain weighted source data that cannot be represented losslessly through the
chosen upstream import path; declare the limitation and keep an exact Arrow
side table if needed. Do not force arbitrary uint64/decimal measurements into
approximate numeric chart tracks. Perfetto serves trace inspection, while Arrow
retains exact arbitrary table values and provides general chart data.

The resulting profile views include flame graphs, chronological sampled stacks,
call trees and inverted trees, hot functions, source/assembly attribution when
matching code exists, instrumentation timelines, CPU/thread states, waits,
counter correlation and dependency inspection. Availability follows the actual
capture and the pinned upstream UI/plugin capabilities. Sampled functions are
not exact invocation spans. Missing samples do not prove idle time. Collapsed
stack totals cannot reconstruct a timeline. A rising percentage alone does not
prove a function became slower: show absolute weight and the denominator too.

Clock alignment, PID/TID reuse, recursion/inlining, scheduling causality and
symbolization follow upstream trace semantics. Keep unknown coverage and
uncertainty visible. Correlation is distinct from a measured causal dependency;
critical paths require the relevant dependency evidence. Profiler collection
overhead is part of the profiled execution; link a separately measured
unprofiled run when appropriate.

## Graph coverage and extensibility

| Views | Data/owner |
| --- | --- |
| Commit history, small multiples, regression tables and A/B distributions | Buster charts over annotated Arrow/JSONL measurements |
| Histograms, ECDF/CCDF, tail latency, box/violin plots and uncertainty | Raw/weighted observations or explicit bins and estimator metadata |
| Flame graphs, sampled stack timelines, call trees, source/assembly | Native Perfetto profiles and applicable upstream UI/plugins |
| Scheduling, off-CPU/waits, migration, spans, flows and correlated counters | Native trace evidence and upstream trace analysis |
| Allocation/retention, I/O, device/accelerator activity | Native trace data where supported; arbitrary exact tables for additional measures |
| Heatmaps, parameter sweeps, scaling, Pareto plots, energy/quality and roofline/model comparisons | Arbitrary typed columns, dimensions, units and explicit assumptions |
| Correctness, corpus coverage, failure matrices and incomplete campaigns | Outcome/census tables with source/oracle/policy identities |

Adding a graph is a renderer/plugin change. Adding a measurement is ordinarily
a new column/table and semantic annotation. Neither requires inventing another
binary format. Missing source evidence disables the corresponding interpretation
instead of generating plausible-looking data.

## Statistical comparison rules

Keep campaign, fresh process, matched process pair, pass, calibrated batch,
iteration, thread and work-item levels separate. Store the independent
experimental unit. Eleven inner samples from each of three processes do not
automatically create 33 independent process observations. Preserve warmups,
failed attempts, incomplete pairs and exclusions in the census.

Ranges identify observed spread, quantiles, confidence or credible intervals
explicitly, with their population and method. Resampling needs the correct
independent unit. Three process-median ranges are descriptive by default;
overlap is inconclusive, not equivalence. Independently summarized phase medians
must not be added to reconstruct total time.

A comparison declares its treatment fields and matching conditions: input and
configuration, benchmark revision, relevant environment epoch, scope/units,
clock/counter event and privilege, population/reducer, toolchain/options,
cache lifecycle, conditioning, affinity/lanes and profiling state. Intentional
hardware or toolchain contrasts identify those differences. Two unknowns do
not prove equality. Incompatible runs can be shown as labeled observations
without an automatic acceptance/regression classification.

For positive baseline `b` and candidate `c`, change is `c/b - 1`, time reduction
is `1 - c/b`, and time speedup is `b/c`. Median paired ratios differ from a ratio
of independent medians. Preserve estimator/method and inputs. Zero/negative
denominators need an appropriate absolute or bound-based result. Suite summaries
use frozen matched populations, weights and coverage. Strong versus weak scaling
and useful aggregate work/window are explicit.

Correctness CI, capture quality, performance acceptance, protected integration
and issue closure remain separate outcomes. A historical measured binary does
not qualify a later composed head.

## Buster GitHub Pages integration

Proposed route: `https://buster14a.github.io/buster/perf/`. Publish the ordinary
Buster charts there and a pinned upstream Perfetto UI under
`/buster/perf/vendor/perfetto/`. Keep data and resources under the same origin
where practical, with hash-based routing so nested profile links work on Pages.
Build the entire site artifact coherently and integrate other site pages.

Use the release's `perfetto-ui.zip` with its JS/WASM/assets intact, the documented
embedded mode, readiness handshake and trace-opening messages. Pin the release
and asset digests, retain license notices, serve WASM with the correct MIME
type, and make generated URLs base-path aware. Perfetto's documentation states
that subdirectory hosting works, with its service-worker optimization limited
to root hosting. Verify the chosen release on the `/buster/` path before
deployment. Do not rely on a service worker or special isolation headers.

The checked v58.2 message handler accepts an `ArrayBuffer` (or a view), supports
the `PING`/`PONG` handshake and time-range controls, and has no transferable-stream
input. Use the buffer path for this pin and set `keepApiOpen: true` when later
viewport messages are required. Newer documentation's stream interface requires
a separately verified viewer upgrade. Browser embedding remains an acceptance
test; source inspection alone does not establish it.

The initial integration loads traces and directs their viewport using the
documented API. Perfetto manages synchronized selections among its own views.
The checked embedding API does not document a general outbound time-selection
or SQL-result callback for Buster charts. Bidirectional cross-view synchronization
is a separate, version-pinned Perfetto plugin/extension or small integration
patch with explicit tests. Do not claim an iframe alone supplies it. Persist
producer/trace identities and timestamps for deep links, rather than unstable
Trace Processor row IDs. URL raw-nanosecond parameters and viewport-message
second values are converted deliberately.

All ordinary browsing is client-side; no query server, GitHub token or live
GitHub API is required. Validate and index the exact reviewed performance-repo
commit during publication. Preserve original data in `buster-performance`;
publish summary tables and selected trace resources as deployment outputs,
without tracking raw measurement archives back in Buster source.

History → compare → inspect remains the primary flow. History preserves source
ancestry/date and measurement time independently, including backfills/reruns.
Missing observations remain gaps and incompatible environment/corpus/harness
changes create visible segments. The evidence drawer exposes exact boundaries,
identities, commands, validity, population and reproduction references. General
tables/charts support new metrics immediately; specialized views are installed
trusted website code. Data files never execute supplied renderers or commands.

## Size, overhead and large captures

Reuse avoids maintaining codecs, profile dictionaries and a full trace viewer.
It still costs offline parsing, conversion/symbol resolution, browser JS/WASM
initialization, trace processing and retained indexes. Measure those costs on
representative captures. Buffer/serialize benchmark observations outside timed
operations; process-wide resource scopes still include their actual collection
and output cost. Capture overhead remains a separate measured condition.

Partition large Arrow datasets and lazy-load overview/drilldown tables in a
worker. The pinned v58.2 embedded viewer loads a native trace buffer; its budget
includes that input and the Trace Processor's decoded state/indexes. Use
explicitly prepared standalone captures or documented segmented-trace mechanisms
where needed. Arbitrarily slicing a protobuf file is invalid: interning, sequence,
clock, process/mapping state and crossing spans may be needed to interpret later
packets. Use upstream-supported extraction/re-emission and validate each output.

An upgraded viewer's streaming path may avoid a host-side whole-file buffer;
it still does not imply bounded total Trace Processor memory. Do not
promise arbitrary window loading or a fixed 256 MiB profile memory cap. Benchmark
initial payload, UI initialization, peak memory and time-range query latency.
Overview targets remain a 256 KiB catalog and a 1 MiB first dashboard payload;
the Perfetto viewer is lazy-loaded and has a separately measured budget.

GitHub Pages' published-size and bandwidth limits require an archive strategy.
Include the pinned viewer and selected traces/data in the Pages artifact; retain
large native captures in durable release assets or another explicit evidence
store with hashes and access metadata. Provide download/local-open fallback
when browser fetch is unavailable. Expiring Actions artifacts are not permanent
history. Do not rely on `.gz` filenames providing HTTP content-encoding headers.

## Import plan and implementation language

Use family-specific CSV/TSV/log adapters and reviewed metadata for existing data.
Keep original bytes and source-cell identity; distinguish readings, reported
summaries, estimates, derived values and unavailable data. Protocol-only evidence
becomes a planned manifest with no fabricated timings.

Reuse supported Perfetto/perf/trace import paths first. Record source format,
version, commands and fidelity, including lost event kinds, weights, clocks,
scheduling or source information. A native Superluminal package alone is not
a completed browser import. A production adapter needs a verified supported
export/API and comparisons against real captures; instance CSV cannot reconstruct
all sampled stacks or scheduling. Preserve the original package and report
capabilities honestly.

Application instrumentation can use the upstream **Perfetto C SDK**. Native
Buster producers and benchmark-specific adapters remain C; reuse reviewed Arrow
C facilities for tables. Upstream libraries/tools retain their own implementation
languages and licenses. The browser uses the upstream UI and Arrow JavaScript.
No requirement to rewrite these libraries or add a profiler engine to the compiler.

## Reference fixtures and validation

The former `bmf-role-contracts-v1.json` and mandatory custom profile-table
contract are superseded by native Perfetto. The linked reference fixture set
lives in buster-performance. The real foreign-string import
still contains all 288 process/case rows and twelve execution/resource rows, in
ordinary Arrow and equivalent JSONL, with zero-row Arrow schema sidecars.
All 7,308 original CSV cells remain preserved.

The profile example now contains an actual native `capture.pftrace`. It is
deliberately synthetic, with eight CPU stack samples, two threads, kernel
scheduling events, instrumentation, a dispatch flow, memory and energy counters,
and a labeled coverage-unknown interval. An arbitrary custom Arrow/JSONL metric
table demonstrates extensibility and exact large uint64/null values without a
custom profile schema. The manifest links that table to the trace execution.

The fixture is checked using **Perfetto Trace Processor v58.2**
(`add693d8b338ba9599dbcbc3e300b1ab8c000897`) and upstream SQL tables. Expected
sample leaves are `parse=4`, `check=2`, `encode=2`; `[2ms,7ms)` keeps five samples
with `parse=1`, `check=2`, `encode=2`. The trace origin is 1,000,000,000 ns; these
windows are converted to absolute trace time for queries. Instrumented parse
duration is 2.5 ms, and the parallel encode task lasts 3 ms. These are actual
synthetic interval boundaries, not durations inferred from sample counts.

Validation includes the thin JSON schema, resource hashes/row census, Arrow
schema authority and exact JSONL equivalence, semantic references/missing values,
and native trace queries for sample attribution, time filtering, counters,
scheduling and flow. Reports identify what was checked and what remains open.
The local fixture writer is a small original C emitter using the published
protobuf wire schema; it is not a production collector or replacement codec.
Its source and reproduction commands are included in the reference
[`profile-example/`](https://github.com/buster14a/buster-performance/tree/5a022380cab1843b73627920d2e8a58dfe43ec42/docs/measurements/performance-format-2026-10-24/profile-example).

Before release, complete the 19-family import census, real perf/Superluminal
fidelity checks, selected upstream UI/plugin validation, iframe loading and
time control, any bidirectional synchronization extension, large-capture cost
measurements, and an actual Pages deployment with license/storage boundaries.
This task updates the design and fixtures; it does not deploy a website.

## Primary references

- [Arrow columnar/IPC specification](https://arrow.apache.org/docs/format/Columnar.html)
  and [Arrow JavaScript](https://arrow.apache.org/docs/js/) define table storage.
- [JSON Lines](https://jsonlines.org/) defines the readable row framing.
- [Perfetto trace protocol](https://perfetto.dev/docs/reference/trace-packet-proto)
  and [native profile protobuf at v58.2](https://github.com/google/perfetto/blob/v58.2/protos/perfetto/trace/profiling/profile_packet.proto)
  define the reused profile data model; the source schema is authoritative.
- [Embedding Perfetto](https://perfetto.dev/docs/visualization/embedding-the-ui)
  and [embedding API reference](https://perfetto.dev/docs/visualization/embedding-api-reference)
  describe the supported UI integration. The exact deployed release needs its
  own smoke test; current documentation is not a promise about all old versions.
- The [v58.2 embedding handler](https://github.com/google/perfetto/blob/v58.2/ui/src/frontend/post_message_handler.ts)
  and [route schema](https://github.com/google/perfetto/blob/v58.2/ui/src/public/route_schema.ts)
  confirm the pinned buffer/handshake/time-range surface used in this design.
- [Perfetto v58.2 release](https://github.com/google/perfetto/releases/tag/v58.2)
  provides the pinned tools, UI and C SDK assets.
- [Perfetto conversion/import guidance](https://perfetto.dev/docs/getting-started/converting),
  [perf script](https://man7.org/linux/man-pages/man1/perf-script.1.html), and
  [Superluminal documentation](https://www.superluminal.eu/docs/documentation.html)
  inform source adapters and capability limits.
- [GitHub Pages limits](https://docs.github.com/en/pages/getting-started-with-github-pages/github-pages-limits)
  constrain publication; archive size and browser load budgets need separate checks.
