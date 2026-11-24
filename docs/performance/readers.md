# Performance resource readers

`tools/performance/` and `web/perf/` contain original C and browser JavaScript.
They require no added project libraries, packages, generated upstream code,
network build step, or embedded viewer. The C tools use the existing Clang and
C/POSIX runtime. The browser uses native modules, BigInt, TextDecoder, Fetch and
Web Crypto on HTTPS or localhost. Arrow IPC, JSONL and Perfetto protobuf are
interoperable file formats; their implementations are first-party code here.

The canonical contract is
[`buster-manifest-v1.schema.json`](buster-manifest-v1.schema.json). Resource
paths, hashes and bytes identify evidence; table schemas remain ordinary Arrow.
Unknown optional metadata stays available in the parsed manifest. Unknown
required capabilities, manifest versions and unsupported storage features fail
explicitly. Original measurement bytes and receipts live in
[buster-performance](https://github.com/buster14a/buster-performance).

## Native validation

```sh
bash tools/performance/build.sh
build/performance/bmf --self-test
build/performance/bmf validate /path/to/manifest.bmf.json \
    --evidence-root /path/to/frozen-performance-checkout
```

`--evidence-root` resolves a remote resource through its declared `source.path`.
Alternatively, `--cache DIR` reads files named by expected SHA-256. Both paths
still verify the exact declared size and hash. There is no network downloader.
Local resources are opened below the dataset directory, component by component;
symlinks, traversal, devices, pipes and paths outside that directory are refused.
Commands, paths and renderer strings in evidence are never executed.

`validate` checks the canonical schema, unique collection IDs, supported required
capabilities, references, resource integrity, native envelopes, physical table
schemas, exact ordered alternatives, row counts, scalar keys and foreign keys,
column bindings, context kinds, measured-value status/reason and sample counts.
Partitions concatenate in declared order exactly once. Alternatives represent
the same complete table and never add observations. Empty tables require a valid
schema. A JSONL primary table needs its zero-row Arrow schema sidecar.

`check-manifest` checks structural schema only and explicitly reports that
resources are unverified. `inspect-arrow` checks supported IPC storage and row
counts. `compare-jsonl ARROW JSONL SCHEMA` checks exact typed equivalence.
`sha256 FILE` produces the content hash. These commands do not establish capture
fidelity, comparison eligibility, publication or target-machine acceptance.

`write-schema INPUT.arrow OUTPUT.schema.arrow` writes an ordinary zero-row Arrow
IPC stream with original C code. It retains schema/field custom metadata and
exact Unicode field names. Native producers can call `perf_arrow_schema_write`
directly with the parsed Arrow schema. The schema writer uses forward references
and explicit bounds; it does not add a Buster physical-schema file format.

## Supported physical formats

| Format | Reader boundary |
| --- | --- |
| JSON | Strict RFC 8259, exact numeric tokens, decoded UTF-8, duplicate-key rejection, iterative nesting |
| Arrow IPC | Little-endian V4/V5 streams; uncompressed ordinary record batches; optional stream EOS; zero-row schema streams |
| Arrow types | Null; signed/unsigned 8/16/32/64-bit integers; float16/32/64; Decimal32/64/128/256; UTF-8/binary and large variants; bool; fixed binary; list, large list, fixed list and struct |
| Unsupported Arrow features | Dictionaries, compression, union/map/view/run-end types, temporal types and required schema features; retain the native resource and expose an unavailable-reader reason |
| Perfetto | Bounded native Trace/TracePacket wire-envelope validation; semantic profile capabilities are qualified separately |

Field names are exact, including Unicode, spaces, NUL and prototype-like names.
Repeated sibling fields are refused for binding and JSONL equivalence. Schema
and field custom metadata are retained for schema equality. No Buster physical
type language is introduced.

The JSONL mapping uses strings for int64/uint64 and decimals, JSON integers for
smaller integer types, finite JSON numbers for float32/64, canonical base64 for
binary, and ordinary booleans/UTF-8/null. Lists and structs apply the mapping
iteratively. Decimal precision, scale and signed integer bounds are checked
without floating-point arithmetic. Decimal spelling may differ only when its
exact scaled value is identical. Legitimate zero stays distinct from null.

Float16 and nonfinite floating-point values remain native Arrow. A native-only
table cannot claim a JSONL alternative or unverified key relationship. Native
validation reports `native_only_tables`; dependent mapped-row semantic checks
are unavailable for those tables. Internal comparison buffers are discarded.

## Resource and browser budgets

The native reader caps each input/comparison buffer at 64 MiB, each JSON document
at 262,144 tokens, nesting at 128, each schema at 4,096 fields and each table at
1,000,000 rows. Retained resource plus normalized-table payload is limited to
256 MiB; working buffers and indexes add memory. Larger data needs partitions or
an explicitly revised reader budget. These are refusal limits, not peak-memory
measurements or promises of arbitrary trace slicing.

The browser caps fetched resources at 64 MiB before materialization, checks their
hash and size, and caps loaded tables at 2,000,000 materialized cells and
1,000,000 rows. It retains BigInt and Decimal values through ingestion,
inspection and export. Plot coordinates approximate only after exact origin
subtraction. Missing and censored values remain unavailable for automatic
regression classification. A matched-pair effect is the median of pair
differences; independent populations use a difference of medians. Bootstrap
resampling uses independent units or pairs and exposes the estimator and
exclusions.

JSONL partitions share the aggregate cell budget, including nested values. The
interactive bootstrap caps a selected comparison at 512 independent
observations; larger populations retain descriptive values and an explicit
unavailable-inference reason, rather than an automatic regression label.

`web/tests/reader.html` runs browser-native interoperability and rejection probes
without a package or test framework. `build/performance/serve DIRECTORY PORT`
serves a prepared static tree on localhost for those checks. Production Pages
is static and does not run that server.

## Paired performance verdicts

Build the first-party `paired` reader with `bash tools/performance/build.sh`.
It consumes a frozen sampling plan and a v1 benchmark manifest. The plan for
the nightly and requested SQLite checker comparison is
[`checker-sqlite.json`](../../tools/performance/plans/checker-sqlite.json).

```sh
build/performance/paired --self-test
build/performance/paired report PLAN.json manifest.bmf.json > verdict.json
```

The report command validates the manifest and its resources with the native BMF
reader. A remote resource can be resolved with `--evidence-root DIRECTORY`.
A valid comparison, including one reporting regression or inconclusive, exits
zero. Invalid input exits nonzero. A readable plan still produces a JSON
verdict on missing, damaged or incomplete evidence. JSON null denotes an
unrecoverable identity. A planned manifest with identities and no observations
reports `not_measured`.

### Population and decision

Each plan covers a single case with one argument list and three time-runs
builds, in order: `main`, `head` and `main-again`. The last build repeats the
base source and every base binary. The reader checks the declared count, role,
layout, completed warmup rounds and slots, measured round, copy, Williams
order, zero exit status and successful end of each invocation. It accepts wall, user and system milliseconds. PMU and profile
fields supply no verdict inputs. The reader checks BMF byte lengths and SHA-256
before interpreting observations.

Choose the plan before running either revision. Each metric has a role and a
lower-is-better regression budget in parts per million. The plan fixes the
number of sessions, rounds, copies, warmups and ordered layout IDs. Total
processes including warmups are bounded by
`3 × sessions × layouts × copies × (warmups + rounds)`. The checked-in recipe
uses four verified checker offsets, two copies, one warmup, five measured
rounds and a 90-second capture limit. This is 144 processes and **four** units.
Its initial practical wall-time budget is 1%. User time is diagnostic.
Changing the budget changes the series.

- For a layout-sensitive case, `unit: relative-layout` uses one matched
  relative code layout per unit. The producer must establish the relative
  offsets or link orders. Distinct hashes alone cannot establish layout
  variation. Copies, inner rounds and repeated sessions all contribute to
  the median for the same layout.
- For a layout-insensitive case, `unit: block` uses one executable pair and
  one complete Williams-ordered block per unit. `rounds_per_block` specifies
  the contiguous rounds in each block and must divide each session exactly.
  Persistent binary placement and host drift limit inference across blocks.
- Each entry in a metric's `units` array gives the absolute medians for
  base, candidate and same-base control, in that order. The point estimate is
  the mean of the paired log ratios. Exponentiate it to obtain the geometric
  mean ratio. A zero inner sample contributes to the median. A zero unit
  median makes its log ratio unavailable. The report includes the percentage
  effect and the interval on the log scale.
- The paired Student interval assumes approximately normal independent
  unit effects. Degrees of freedom are rounded down to the next tabulated
  value and critical values are rounded up. A Bonferroni adjustment covers
  the declared primary metrics and the two references at 95% family
  confidence. Diagnostic intervals are descriptive. Selected offsets,
  autocorrelated blocks and small populations limit the interpretation.
- An upper bound at or below the budget passes. A lower bound above the
  budget establishes regression. A bound crossing the budget or fewer than
  two units gives an inconclusive comparison. Both bounds on every primary
  A/A ratio must fit the symmetric multiplicative budget. A control crossing
  either limit makes the campaign inconclusive. With qualified controls,
  any primary regression determines the verdict, and certification requires
  every primary to pass. A missing or invalid primary prevents certification.
- Stop at the declared budget. Interrupted pairs and failed processes give
  `invalid_incomplete`. Report a retest as a separate attempt with its own
  plan and identities. The reader never accumulates units across reports.

The archived demonstration replayed three Benchpress invocations. The control
raised zero regression alerts in three comparisons, and two controls fit the
symmetric budget. The third made the candidate verdict inconclusive. The
original runs shared the same layouts and host. These counts describe observed
false-alert behavior and give little precision about its frequency in future
runs. The retrospective budgets and partial toolchain provenance limit this
example to a demonstration. [Frozen plans, source and binary identities, raw observations and reports](https://github.com/buster14a/buster-performance/tree/e88d0fb8762465660c9564bd886a5d4473c1e382/docs/measurements/paired-verdicts-2037-2026-11-24)

### Input identity and baseline promotion

The manifest's `extensions.paired` object binds the SHA-256 of the plan's
original bytes (`plan_sha256`) and content identities for `harness`, `recipe`,
`toolchain`, `corpus` and `host`. These six digests determine the series.
Each role has a Git `source` ID and an ordered `binaries` array of SHA-256s
corresponding to the plan's layouts. New `observations` resources use
`time-runs-v2`. Before its first measured row, each invocation records how
many warmup rounds completed successfully and how many slots each contained.
The reader requires exactly one matching receipt per session. Imported
`time-runs-v1` resources support historical replay. Only measured v2 captures are eligible for promotion. A measured v1
capture without warmup receipts retains its statistics and reports
`invalid_incomplete`.
The producer checks source revisions, binary digests and environment before
timing. Both revisions must share a build recipe fingerprint covering the
build driver, CMake tree and presets, code generation tools and toolchain setup.
The report retains the manifest ID, observation digest, identities, sample
plan and number of complete units. Keep the manifests and evidence with the
history in buster-performance.

```sh
mkdir series-history
build/performance/paired record PLAN.json series-history SOURCE_SHA latest.bmf.json anchor.bmf.json
```

The first promotion requires a complete, calibrated self-comparison at the
chosen seed. Supply the same manifest twice when latest and anchor coincide.
For subsequent revisions, provide fresh, matched comparisons against both the
latest accepted binary set and the original fixed anchor. Both manifests must
be measured, cover the same candidate, match the stored references and pass
the complete primary plan. For example, consecutive 1.5% losses pass against
successive references with a 2% budget. The second candidate costs 3.02%
against the fixed anchor and its promotion fails.

Use a dedicated trusted directory for each series. The command records each
decision in a numbered, hash-linked JSON file, including both reports, the
candidate source supplied by the caller, a link to its prior attempt and the
resulting anchor and latest build IDs. Its exit status indicates whether the
decision was durably recorded. Read `promoted` for the decision. A directory
lock serializes writers. An atomic create preserves earlier events. An exact
retry returns the existing event. A previous adverse attempt for the same
source prevents promotion by a later favorable retest. If another writer
advanced the latest reference, a passing pair against a previously accepted
reference records a stale attempt. Fresh comparisons against the current
latest and original anchor can then qualify. Git preserves the
archived event files and both input manifests.

For a changed toolchain, host, corpus, recipe or harness, initialize a new
series. To establish overlap for a case, run the same common source pair over
the same relative layouts and plan in both environments, then run
`paired overlap PLAN.json OLD_MANIFEST NEW_MANIFEST`. Both within-series
comparisons must qualify. The bridge also checks the paired old/new absolute
values for each common build against the symmetric primary budgets. For
matching measured sources and layouts, it reports these absolute comparisons
across format versions when both within-series statistical tests pass.
Missing warmup receipts leave the bridge unqualified. Its JSON
and exit status record overlap qualification for that case. Keep both histories
and their anchors.
