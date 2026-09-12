# Reproducible recovery of historical branch work

Reviewed main: `719ba64e3022a0ae1db078ea97cb951a70ac02a0` (September 12, 2026).
This is a preservation-only change. It adds a read-only recovery tool, a pinned
provenance manifest and offline tests. It does not delete branches, alter GitHub
state, apply compiler patches or execute historical branch code. No compiler,
workflow, build policy or compiler test-registration file is changed.

## Recover from the saved bundle

[recover_branch_work.py](../../../tools/recover_branch_work.py) accepts only a
local, self-contained Git bundle file. It makes a new local bare mirror,
verifies the bundle, checks that the source file did not change, and exports
selected data with a `.txt` suffix. It does not accept a remote URL or a working
tree as its input and has no GitHub API client or branch-deletion mode.
Python 3.10+ and Git are the only requirements.

Run from the repository root:

```sh
python3 tests/recover_branch_work_test.py
python3 tools/recover_branch_work.py --bundle /path/to/buster-before-cleanup.bundle --output ../buster-recovered-work
```

Use a fresh output directory; existing output is never overwritten. The tool
writes `recovery.json` with an explicit completion flag, a local `history.git`,
and `recovery/` containing the copied manifest, exported data and
`exported.json` hashes. A failed export remains incomplete and produces a
nonzero exit code. Neither the original bundle nor any source ref is modified.

The previously supplied `buster-preservation-september11.zip` contains the
required `buster-before-cleanup.bundle`. It came from preservation artifact
`10263714534` of [run 34601030109](https://github.com/buster14a/buster/actions/runs/34601030109).
That hosted artifact expires October 11, 2026; keep the local copy. It is a
September 11 snapshot, not a backup of every later branch tip. This PR records
exact recovery locations instead of committing the full Git bundle or old
workflows. A manifest alone is not a substitute for retaining the actual bundle.

## Selected historical material

The [manifest](manifest.json) identifies 14 records by source branch, full
commit, path, export method and provenance. Every source commit must remain
reachable from a ref in the supplied history.

| Group | Preserved material |
|---|---|
| Fast register allocator | Readable edge-indexing implementation/test patches, a control-flow fixture and the original corrupt encoded transport. |
| Canonical complex values | A complete author-preserving implementation commit, an alternate IR fixture and the original corrupt encoded transport. |
| EVEX conversion oracles | Three candidate patches and three independent oracle/input-preservation/audit scripts. |
| Canonical entry layout | The complete newer-baseline implementation/regression commit with author provenance. |

Twelve exports are checked against their original Git blob, byte length and
SHA-256. Two complete commit patches are regenerated with full object IDs and
original author/commit provenance. Their formatting can differ from the earlier
packet: that packet's hashes are named `original_export_sha256`, while
`exported.json` records the generated output hashes. The source trees are pinned.

Two encoded transports are explicitly `known-corrupt-encoded-data`. The tool
copies their original bytes without decoding them. This is preservation, not
a claim that the transports work. The separate readable candidates remain
available for review. Recovered Python files are never imported or executed.

## Reconcile before integrating

At the reviewed main revision, `machine_fast_index_edges` already exists and
`canonical_entry_test_internal.h` is registered in the codegen tests. Do not
reinstall the old versions over newer source. The September 6/7 audit bundles,
SIMD harness, LLVM boundary controls and subsequent September 11 recovery records
already on main are not duplicated by this PR.

Other candidate implementations and alternate fixtures require current-source
comparison, registration review and applicable compiler validation before use.
No current miscompilation, speedup, clean patch application or merge readiness
of a historical compiler patch is asserted. Active PRs and intentional archives
remain unchanged. The maintenance branch now hosts this recovery work rather
than serving as an empty disposable helper.

## Validation actually performed

The 16 offline tests in [recover_branch_work_test.py](../../../tests/recover_branch_work_test.py)
cover exact bytes, source checksums, invalid destinations, unknown input data,
corrupt transport preservation, inert Python export, author-preserving patches,
bundle recovery, source immutability and incomplete-failure reporting.

The container's default temporary filesystem returned an I/O error on Git's
pack-index `fsync`. The full suite passed using
`TMPDIR=/dev/shm python3 tests/recover_branch_work_test.py`, without disabling
Git durability or changing the tool's behavior. A full replay of the actual
September 11 bundle into that filesystem recovered all 14 records: 12 exact
blobs and two complete commit patches.

All seven readable recovered patches pass `git apply --numstat`; all three
recovered Python scripts pass AST parsing without execution. Fresh source
Python parsing, local documentation links and whitespace checks pass. These
are recovery checks, not current-main patch application, compiler builds,
self-host, native platform execution or new performance measurements.
Hosted exact-head CI remains separate; no unrun gate is called green.
