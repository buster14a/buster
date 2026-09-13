# Native-retirement durable history gate

This read-only half of [#510](https://github.com/buster14a/buster/issues/510)
uses `native_retirement_archive.checked` and `digest` from the integrated
archive/replay implementation. It complements the census join/byte validator
and strict differential replay brought forward from
[#504](https://github.com/buster14a/buster/pull/504); it does not publish or
overwrite release assets.

## Frozen identity versus current availability

`history-catalog.json` records seventeen required archives discovered through the
original and newer #504 runs, including the failed-packaging raw strict bundle.
Each archive keeps its own Actions ID, producing workflow revision, candidate
commit/tree, byte count, SHA-256, observed expiration, and recorded outcome.
The complete paginated discovery in run 34728261000 added eight archives to
the initial nine-entry inventory. Unknown per-build binary hashes are explicitly null; identical source revisions
are not a license to reuse another run's binary identity or outcome.

The checker refreshes the evidence branch's workflow runs and artifact metadata,
follows pagination, and reports additional unregistered archives. It compares
immutable origin identities but records current retention dates rather than
requiring them to equal the historical observation. The September 19 warning
is still correct for the original bundles. The additional run 34726450979 has
September 20 UTC expirations; it is not a substitute for the older runs.

The approved destinations are the repository releases
`native-retirement-evidence-eb1bef2`, `native-retirement-evidence-2bb4ce9`, and
`native-retirement-evidence-history-20260912`. The catalog partitions every
artifact ID across exactly one of those tags. The checker resolves their actual
asset IDs, downloads bytes into a fresh temporary directory, applies the
existing helper's per-asset size/hash checks, and hashes ordered split parts
against each frozen original Actions ZIP identity. It never trusts only an
internally consistent release manifest and never uploads or overwrites anything.

Publication run
[34730125413](https://github.com/buster14a/buster/actions/runs/34730125413)
verified the complete transfer from the original Actions artifacts. The history
release contains 21 release assets totaling 12,809,466,853 bytes; together the
three releases preserve all seventeen original ZIP identities. Larger ZIPs are
split into deterministic 1,000,000,000-byte parts.

## Reproduction and CI

The `Native retirement history` workflow checks out the submitted revision and
uses the archive helper integrated beside this checker. The workflow has
read-only repository/Actions permissions and runs no compiler or benchmark.

From the submitted checkout:

```sh
python3 tests/native_retirement_inventory_test.py -v
python3 tools/native_retirement_inventory.py \
  --catalog docs/performance-audits/evidence/2026-09-12-retirement/history-catalog.json \
  --output /absolute/path/to/new-history-report
```

The live step requires authenticated `gh` with read access to this repository.
It processes archives sequentially and streams hashes in 1 MiB blocks; it
retains at most one archive's downloaded parts at a time. Original ZIP bytes
are not extracted or executed. CI retains metadata and `inventory.json`, even
on failure, without uploading the multi-gigabyte downloaded bundles. That
seven-day report artifact is diagnostic output, **not durable preservation**.

The offline regressions include a self-consistent replacement whose per-asset
check passes while its frozen-original check fails, modified downloads, missing
and ambiguous parts, expiration drift, and preservation of failed history.
Actual execution belongs to CI; source inspection alone is not a passed test.
The durable replay workflow separately rebuilds the pinned oracle, validates
the recorded and reproduced census joins, and reruns strict semantics. See
`RESULTS.md` for commands, hashes, and the actual publication/replay results.

## Separate acceptance domains

Exit zero requires every catalogued archive to have retrieved bytes matching its
original identity, all origin identities to resolve, and no discovered archive
to be absent from the catalog. A nonzero result retains verified entries as well
as missing/failed ones; there is no `continue-on-error` success conversion.

`source_reproduction`, `independent_join`, and `strict_semantic_replay` are always
`NOT RUN` inside this history-only report because the durable replay workflow
owns those checks. The direct source snapshots and build recipes are preserved,
while missing host/resource-header and process-environment closure remains
explicit follow-up under #508. An integrity pass does not change any historical
compiler failure, make a failed reference usable, satisfy the dedicated 9700X
performance gate, authorize a production cutover, or close #36.
`retirement_accepted` therefore remains false.
