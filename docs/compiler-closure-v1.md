# Frozen baseline closure v1 (#3211)

The ordinary comparison still defaults to the qualified legacy base → head →
base-build preparation. `compiler_compare.py --closure-policy snapshot-v1`
is an explicit qualification path; it does not change request authorization,
profiles, sampling, workloads, thresholds, main-only scheduling or publication.
Trusted-main staging followed by admitted before/after and A/A controls is
required before changing that default. Hosted success is functional evidence,
not a performance saving under #2761.

The trusted native build driver owns `compiler_closure`. Historical baselines
do not need that command. The interface is:

```text
compiler_closure snapshot|restore|verify ROOT SNAPSHOT BASE TREE RECEIPT EXPECTED_SHA256
compiler_closure self-test
```

For snapshot, EXPECTED_SHA256 is `-`. ROOT is the canonical candidate checkout;
SNAPSHOT is fresh attempt-owned storage outside it, on the same filesystem.
The helper refuses unsupported moves, incomplete state and mismatches without
a rebuild fallback. An interrupted attempt has no successful complete closure;
the outer comparison checkpoints and process-group cleanup retain the failing
phase and prevent children from surviving into measurement.

Snapshot first asks the actual baseline `build.sh bench_throughput help` to
prepare its own native corpus executable. It then inventories and saves every
source file outside `.git`, `build` and `.cache`, including ignored generated
source, empty files, directories and executable modes. Tracked bytes must match
the authorized baseline Git tree. It parks the entire baseline `build` tree
and `.cache/bootstrap-driver` by same-filesystem rename. The full CMake cache,
generated headers, graph, compile commands, compiler and corpus executable,
and immutable bootstrap dependency manifests are retained. No source-generation
or compiler build is performed on restore.

Restore verifies the saved source, generated products, cache, matched absolute
root, baseline revision/tree, configured compiler/linker/make executable,
resolved producer tools and complete configured Clang resource tree before
removing candidate state. It materializes the saved baseline source with its
original modes and file modification timestamps, removes candidate-only source
files, and returns the parked build and baseline bootstrap cache to their
original absolute paths. It verifies the complete closure again before
measurement. The corpus and explicit scaling extension run the preserved
baseline native harness directly; no candidate build driver or corpus source
is selected after restore.

The manifest is UTF-8 with a version header, canonical root, base and tree.
Each file/directory record carries scope, kind, mode, mtime seconds/nanoseconds,
size, SHA-256 (or `-` for directories) and relative path. Resolved tool bindings
record absolute paths and explicit optional-tool absence. END binds count and
bytes. Records are deterministic, iterative and bounded to 65,536 entries,
8 GiB of consumed bytes and an 8 MiB manifest; symlinks and special files are
refused. Native receipts bind the manifest SHA-256 and root SHA-256. The hosted
publisher hashes and compares the saved/restored manifests, validates complete
inventory and configured-tool bindings, and joins the baseline compiler hash
to the measured binary. Missing, changed, partial or repaired state is invalid.

Host time records baseline and candidate build, snapshot (including separately
reported baseline harness preparation), baseline checkout, restore/validation,
lab measurement, corpus, optional extensions and total. Snapshot and restore
include hashing/materialization overhead; do not double-count the contained
harness preparation when calculating net preparation cost. Source copies plus
the parked baseline and candidate build are confined to the attempt workspace;
ordinary scratch cleanup owns failed and cancelled remnants. Builds and copies
are serial and precede the quiet measurement phase.

Native hosted controls exercise the real producer and consumer with changed
ignored/generated headers, baseline cache/driver and corpus identity, candidate
extras, empty files, spaces, wrong revision, unfinished/missing markers,
tampering and timestamp mismatches. Existing receipt/publication/corpus and
explicit-extension tests remain required.

9700X acceptance remains incomplete until the same source/configuration,
toolchain and matched build root have controlled legacy-versus-snapshot traces
reporting net host cost, plus immutable-binary A/A and same-source build/root
controls. Retain every attempt, including failures; do not claim equivalence
from unchanged flags or one noisy timing.

The controlled native entry point is `compiler_closure qualify ROOT OUTPUT BASE BASE_TREE HEAD HEAD_TREE TRUSTED_LAB PYTHON`. The admission layer supplies all pinned identities, the approved host, and the outer 90-minute reservation. This command refuses other processors. Both arms validate the original private checkout before either mutates it. The fixed order is legacy A/B and immutable A/A, snapshot A/B and immutable A/A, then cross-build baseline A/A. Each series retains the existing ten-minute lab settings, CPU 2, one warmup, the complete native CI corpus with twenty pairs per round and two warmups, and BASE/BASE labels for A/A. It never retries or reduces a population. Same-source controls require identical compiler output.

Each arm resets the same absolute private checkout and bootstrap cache before its first baseline build. The raw closure remains complete. A separate workload identity excludes file times, historical cache paths, and intermediate object bytes only for comparison between arms; it includes every source byte, generated compiler input, configured compiler/linker/resource identity, CMake/Ninja inputs, baseline bootstrap configuration and executable, and the native corpus executable. Both arms must match this workload identity, harness, bootstrap producer, and candidate compiler before snapshot measurements start.

`compiler_closure prepare ROOT OUTPUT POLICY BASE BASE_TREE HEAD HEAD_TREE` exposes preparation without measurement. Appending `SECONDARY_HEAD SECONDARY_TREE` admits a third arm for snapshot-v1 acquisition: the baseline is built and parked once, both pinned candidates are built and frozen, then the baseline closure is restored and verified once. All three commit objects, trees, and ancestry are checked before checkout/reset. ROOT and OUTPUT must remain campaign-owned at their original absolute paths when a later sampling packet reuses the prepared closure. The preparer exports `prepared.json`, full `prepared.manifest.tsv`, comparison `prepared.workload.tsv`, `phases.tsv`, baseline/candidate/candidate2 binary receipts and CMake caches, and snapshot/restore/verify receipts and manifests. Binary receipts bind source commit/tree, SHA-256, executable mode, bytes, and CMake cache digest.

Every experimental child starts in a fresh OS-owned process group. The native owner defers SIGINT/SIGTERM into signal-safe flags consumed by the existing OS deadline wait. After that wait releases its exact manager identity, the Linux subreaper proves that all adopted descendants, including private groups and sessions, are killed and reaped. Uncertain ownership or cleanup stops admission and destructive recovery. A nominally successful phase with escaped descendants is invalid even after successful cleanup. Every command retains bounded argv/stdout/stderr and a cleanup receipt with cleanup time, adoption waves, signals, reaps, deadline/cancellation, and ownership outcomes. Hard-killed owners leave incomplete ledgers, which cannot qualify a profile.

`harness_preparation_us` is required in all new snapshot receipts, including an explicit zero when restore/verify performs no harness preparation. A requested snapshot policy requires all three closure receipts and manifests; removing them cannot silently reinterpret the requested route as a historical legacy receipt.

The staged implementation is reviewed and validated on hosted CI first. Trusted request/dispatch support is integrated separately, then the admitted physical preparation controls run on the 9700X. Only matching controls and a reduction in complete measured preparation cost permit a separate default-activation change. Until that activation is reviewed and admitted, ordinary comparisons use legacy-rebuild and this issue remains open for physical qualification.

The data-only `tools/bench_direct/compiler_preparation.py` adapter exports
`validate_prepared(expected, prepared, bundle)` for two- or three-arm acquisition,
and `validate(expected, qualification, bundles)` for the five-series preparation
controls. Callers provide Git pins, the exact persistent ROOT and OUTPUT, and
qualification lab/Python paths and hashes from a trusted committed plan. Artifact
claims cannot establish those expected identities. Raw manifests, ledgers, every
child argv and cleanup receipt, frozen executable/cache receipts, and complete
lab/corpus evidence are replayed without launching anything. The observed
preparation wall span includes gaps between phases; stage sums are diagnostic.
Final publication time remains unavailable until an independent trusted outer
observation covers it, so those partial timings cannot establish net savings.
