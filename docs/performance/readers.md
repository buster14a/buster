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

`web/tests/reader.html` runs browser-native interoperability and rejection probes
without a package or test framework. `build/performance/serve DIRECTORY PORT`
serves a prepared static tree on localhost for those checks. Production Pages
is static and does not run that server.
