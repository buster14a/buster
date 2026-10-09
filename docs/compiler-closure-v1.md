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
