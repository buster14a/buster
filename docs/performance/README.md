# Benchmark resource validation

[`buster-manifest-v1.schema.json`](buster-manifest-v1.schema.json) defines the
version 1 manifest. Benchmark semantics live in this JSON document; physical
tables are ordinary Arrow IPC streams, and profiles use upstream Perfetto.
The complete performance-site design and acceptance remain in
[issue #384](https://github.com/buster14a/buster/issues/384).

Build the C publication validator from the repository root:

```sh
bash tools/performance/build.sh
build/performance/bmf /path/to/manifest.bmf.json
bash tools/performance/build.sh --tests
build/performance/bmf-test /path/to/buster-performance
```

The build requires Clang, pkg-config, OpenSSL 3, PCRE2-8 and libcurl 7.85 or
newer. The pinned vendored libraries, generator inputs, licenses and checksums
are in [`tools/performance/vendor`](../../tools/performance/vendor/README.md).
This is separate offline publication tooling; it does not change compiler
execution, ISA, or the compiler build. Its control flow uses bounded work lists;
upstream libraries perform physical decoding, regex matching and hashing.

The validator enforces the vocabulary used by the canonical schema, then checks
identities, required capabilities, references, real calendar timestamps, safe
resource resolution, bytes and SHA-256. It loads ordered partitions once and
checks each alternative against the same complete table. Arrow schema metadata,
names, order, nullability, and row counts must agree; a JSONL sidecar must have
zero rows. It verifies primary-key uniqueness, foreign-key type/arity/targets,
metric/scope/instrument bindings, counts, context references and null reasons.
Unknown optional facts/metadata and new metric quantities or unit dimensions
are retained. Dataset status is reported exactly, including partial/aborted.

64-bit integers and decimals use JSON strings. Decimal equivalence compares
the exact unscaled value at the Arrow scale, allowing `1.25` and `1.250000`
to represent the same Decimal128 value without changing either source artifact.
Overflow and rounding are rejected. Small integer fields require integer JSON
tokens; finite floats, booleans, UTF-8, canonical base64, lists and structs follow
the corresponding Arrow storage. Dictionary/date/time/union and other mappings
not specified by v1 remain native-only. Nonfinite floats have no v1 JSON mapping.
An alternative that needs an unsupported mapping is rejected explicitly.

Network reads are disabled by default. `--cache DIR` resolves remote resources
as `DIR/<expected-sha256>` and still checks their content. `--fetch-https` permits
unauthenticated HTTPS reads with certificate verification, a timeout and no
redirects; URLs with credentials or fragments are rejected. No manifest command,
renderer or path is executed. Local paths must resolve inside the dataset;
escaping symlinks fail. Original command lines are inert evidence text.

Publication limits are explicit: 64 MiB per input/resource, 512 MiB of resource
bytes per dataset, 262,144 JSON/schema work items and 128 nested levels. Oversize
inputs fail. Strings with embedded NUL are unsupported at this C interface.
Large trace archives can remain separate hash-verified downloads; this validator
does not promise bounded full-viewer memory or arbitrary trace slicing.

The schema check implements the pinned contract's vocabulary, including local
references, oneOf/not, closed objects, exact types, bounds and regexes. It is not
a general validator for arbitrary external JSON Schemas. The validator's success
establishes resource/representation integrity; profile fidelity, statistical
comparison eligibility, hardware qualification, and deployed browser behavior
need their own evidence. It does not promote a synthetic or incomplete dataset
to a qualified performance result.

## Browser boundary

The pinned Apache Arrow JavaScript 21.1.0 reader is served locally from
`web/perf/vendor/arrow`, with its Apache-2.0 license and NOTICE. The browser
needs BigInt, typed arrays, native JavaScript modules, TextDecoder, and Web
Crypto in a secure context. No isolation headers or service worker is needed
for these table readers. 64-bit values stay BigInt; decimals retain an exact
unscaled integer and scale. JSON numeric tokens are captured lexically before
physical small-integer/float conversion. Approximate plotting coordinates are
computed only after subtracting exact origins; inspection and export retain
the exact original values.

`web/tests/reader.html` exercises the actual browser reader against the same
three independent reference tables, including large values and Unicode/spaced
field names. `web/tests/run.cjs` drives it using pinned Playwright 1.62.1.
The performance workflow serves the test at `/buster/tests/` using the local
C server, retaining the production subpath. Browser and native receipts belong
in buster-performance; the source tree contains the checks themselves.
