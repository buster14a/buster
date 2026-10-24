# Performance website implementation plan

[Roadmap #384](https://github.com/buster14a/buster/issues/384) owns acceptance.
[The format design](../performance.md) reuses Perfetto, Arrow/JSONL and a thin
Buster manifest. This PR publishes the design and reference schema; implementation
work remains open and must not be closed when the design merges.

| Order | Work item | Dependencies |
| --- | --- | --- |
| 1 | [#385 Manifest and native-table validation](https://github.com/buster14a/buster/issues/385) | Foundation |
| 2 | [#386 Faithful historical-family imports](https://github.com/buster14a/buster/issues/386) | #385 |
| 3 | [#387 Arbitrary-metric history and charts](https://github.com/buster14a/buster/issues/387) | #385, #386 |
| 4 | [#388 Native profile capture/import fidelity](https://github.com/buster14a/buster/issues/388) | #385 |
| 5 | [#389 Embedded Perfetto and synchronization](https://github.com/buster14a/buster/issues/389) | #385, #387, #388 |
| 6 | [#390 Reproducible Pages publication and budgets](https://github.com/buster14a/buster/issues/390) | #385–#389 |

Each issue defines bounded scope, unchecked acceptance criteria and required
evidence. All implementation slices are initially unclaimed; read the live
issue discussion and claim a bounded slice before edits. Coordinate shared
paths rather than treating a documentation roadmap as ownership of the compiler.

## Reference material and repository boundary

The [reference evidence set](https://github.com/buster14a/buster-performance/tree/5a022380cab1843b73627920d2e8a58dfe43ec42/docs/measurements/performance-format-2026-10-24) is pinned at performance commit
`5a022380cab1843b73627920d2e8a58dfe43ec42`. It contains the 19-family frozen coverage inventory,
300 archived table rows, a two-row arbitrary typed example, native synthetic
Perfetto capture, original C generator, SQL and validation receipts.
The receipts establish 18 native queries and 7,320 exact typed table cells;
independent production readers, real imports and browser acceptance remain open.

The original CSV/source evidence remains pinned to the archive commit identified
in the manifests. Reference fixture validation is not 9700X performance evidence.
The frozen design census does not silently claim coverage of newer archive paths.

Keep raw observations, logs, captures, receipts and reproduction harnesses in
buster-performance. Keep the design/schema and website source in Buster. Pin
reviewed archive commits in production/CI consumers; the candidate pin above
needs its evidence PR reviewed before production use. The existing
`Measurement storage policy` remains unchanged.

## Merge and release acceptance

1. Review this design/schema together with the linked fixture PR; integrate
   the reference material without claiming the full website implemented.
2. Implement #385 and qualify the Arrow/JSONL/native format boundaries.
   The historical importer and profile adapter can then proceed in their own
   non-overlapping slices.
3. Qualify history/comparison rules and real sampling fidelity before declaring
   profile inspection or cross-view synchronization complete.
4. Attach deployed /buster/perf/ browser evidence, reviewed dependency/license
   inventory and actual load/archive budgets before closing #390 or #384.

No compiler build, physical-host benchmark, workflow change, upstream runtime
vendoring or Pages deployment is performed by this documentation PR.

## Provenance

The design, schema and plan are original agent-assisted contributions directed
by David under the repository's existing original-material terms. The schema
is hand-authored JSON Schema, not a copied upstream Arrow or Perfetto schema.
The Arrow/Perfetto specifications are authoritative references, with their
versions and source links recorded in the design and fixture provenance.
No third-party runtime/source is redistributed here. Existing licenses and
component notices are unchanged.
