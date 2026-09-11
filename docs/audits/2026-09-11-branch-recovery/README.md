# Remaining historical records recovered during branch cleanup

Audited main: `cc25a0d1dba8f1c21447e3712ca5f35678012815`, September 11, 2026.
This is preservation of reports and reproducer data, not a new compiler fix or
performance result. No compiler source, build policy, test registration, or
workflow is changed. Original report contents are copied without edits.

## Scope-index study

The September 7 `2026-09-07T220133Z` report was stranded on
`audit/semantic-scaling-20260907`. Its scope-search implementation is superseded
by merged #244; do not reapply the old patch. The report still provides detailed
work-count methodology, negative/small-input controls and the separate shared
type-DAG finding tracked by #241. All timings and validation remain historical.

`semantic_stress.py.txt` preserves the original generator as inactive reference
material, not a second supported benchmark launcher. `semantic-code.patch.gz.b64.txt`
is the original compressed candidate, also inactive. Decode only as data for
comparison with current source. Historical commands in the report refer to its
original workspace and require reconciliation; current performance work must
use the existing supported harnesses and their acceptance policy.

## Independent post-139 recovery record

PR #416 already restored the actual survey fixes, SIMD harness and September 6
and 7 bundles. A second branch carried a distinct recovery-time report,
`2026-09-11T131903Z`, with its manifest and check-only validation record. Preserve
that evidence and its `RECOVERY.md` caveats without copying the same 56 files
again or replacing #416's README warning. No historical number is relabeled as
a measurement from this branch audit.

## Provenance and index

The adjacent manifest records the source branch, commit, path, original blob,
SHA-256, size and destination for every recovered file. Renaming the archived
Python/encoded patch does not change its contents. The two historical index
entries are restored at their chronological positions. Existing timestamp
entries are ordered newest-first, and only three exact duplicate index lines
introduced by union merges are removed. Older non-timestamp entry ordering and
all descriptions and report bodies are retained.

Recovery validation checks original bytes and Python parsing/generator self-test;
it is not a Buster build, runtime result or new timing run. The audit checker has
the same pre-existing `2026-08-24T131821Z` opening-format failure on the baseline.
This PR does not change that report or suppress its check. Hosted current-head
checks remain separate from historical evidence.
