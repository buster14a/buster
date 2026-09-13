# Native-retirement durable history gate

This read-only companion to [#510](https://github.com/buster14a/buster/issues/510)
uses the existing archive owner's `native_retirement_archive.checked` and
`digest`. It neither publishes an archive nor replaces
[#504](https://github.com/buster14a/buster/pull/504)'s census join/byte validator
or the strict differential runner. Their active branches remain unchanged.

## Frozen identity versus current availability

`history-catalog.json` records nine required archives discovered through the
original and newer #504 runs, including the failed-packaging raw strict bundle.
Each archive keeps its own Actions ID, producing workflow revision, candidate
commit/tree, byte count, SHA-256, observed expiration, and recorded outcome.
Unknown per-build binary hashes are explicitly null; identical source revisions
are not a license to reuse another run's binary identity or outcome.

The checker refreshes the evidence branch's workflow runs and artifact metadata,
follows pagination, and reports additional unregistered archives. It compares
immutable origin identities but records current retention dates rather than
requiring them to equal the historical observation. The September 19 warning
is still correct for the original bundles. The additional run 34726450979 has
September 20 UTC expirations; it is not a substitute for the older runs.

The approved destination inspected here is the **existing repository release**
`native-retirement-evidence-eb1bef2`, ID 387739401. The checker resolves actual
asset IDs, downloads their bytes into a fresh temporary directory, applies the
existing helper's per-asset size/hash checks, and hashes ordered split parts
against the frozen original Actions ZIP identity. It never trusts only an
internally consistent release manifest and never uploads or overwrites anything.
Missing assets are reported as missing **from this inspected destination**, not
as proof that no other durable copy exists. A separately approved destination
must be explicitly recorded before this gate may inspect it.

## Reproduction and CI

The `Native retirement history` workflow checks out the submitted revision and
pins the existing archive helper to
`b1ef825124b6bda13217b6fde62eae03f4f189a3`. This temporary cross-branch dependency
must be reconciled with the archive owner's reviewed integration; it is not a
copy of that implementation in the source tree. The workflow has read-only
repository/Actions permissions and runs no compiler or benchmark.

From the submitted checkout, with that existing helper checkout available:

```sh
export PYTHONPATH=/absolute/path/to/pinned-archive-checkout/tools
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

## Separate acceptance domains

Exit zero requires every catalogued archive to have retrieved bytes matching its
original identity, all origin identities to resolve, and no discovered archive
to be absent from the catalog. A nonzero result retains verified entries as well
as missing/failed ones; there is no `continue-on-error` success conversion.

`source_reproduction`, `independent_join`, and `strict_semantic_replay` are always
`NOT RUN` in this check. Those require the existing owner workflow's independent
source rebuild, join/byte validation, and strict sanitized replay on the retrieved
archives. The direct source snapshot/build recipe and missing host/resource
header and environment closure remain separate obligations under #510/#508.
An integrity pass does not change any historical compiler failure, make a failed
reference usable, satisfy the dedicated 9700X performance gate, authorize a
production cutover, or close #36. `retirement_accepted` remains false.
