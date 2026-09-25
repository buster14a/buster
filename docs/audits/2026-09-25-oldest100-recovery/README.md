# Oldest-100 branch recovery, 2026-09-25

## Status and exact scope

The [100-row inventory](../2026-09-25-oldest100-recovery.tsv) selects the oldest
100 of **460 captured branch refs**, ordered by the tip commit's committer
instant, then branch name. Dates are normalized to UTC. This is **not** a claim
to know the last push/ref-update time; a newly created branch can point at an
old commit.

Comparison main: `d9e736e2cf08804a2c603d153e9fb608f18c5c49`.
Main tree: `ae9f274b6768b023fe6e0ef80a161e14b290e0a2`.

All 100 selected heads matched the separately fetched branch API identities.
None was marked protected or was a head/base of the 17 open PRs in that
snapshot. Those facts are not an authorization to delete a subsequently moved
ref or a replacement for checking current workflow activity and ownership.

**No branch has been deleted by this recovery session.** The available connected
GitHub action set does not expose branch deletion, and direct Git networking is
unavailable. The new maintenance branch is the sole writer's recovery PR branch,
not part of the old-100 selection. Its unused inventory prototype was removed;
this PR's final diff contains only documentation and inert source excerpts.

## Fresh, complete preservation source

The existing read-only `One-off branch preservation evidence` job was inspected
before reuse. Its immutable workflow commit is
`a597d229787bbc04eaa6dfbaec62657459bec5d4`. The rerun completed successfully on
September 25; it fetched live branch refs, without building or executing branch
code and without changing repository refs.

- [Run 34601030109](https://github.com/buster14a/buster/actions/runs/34601030109)
- [Fresh job 108195537730](https://github.com/buster14a/buster/actions/runs/34601030109/job/108195537730)
- Fresh artifact: `10881392277`, `oldest-branch-preservation`, created
  `2026-09-25T18:19:18Z`, 68,180,649 bytes.
- ZIP SHA-256: `342ab4b7c8ede374dd97f4d98f3fb1dd3877d55b1ea5b0bcec07d140f4802aa3`.
- Contained `buster-before-cleanup.bundle` SHA-256:
  `a2d67b0bb09ec06c26b0dfcb3e1c54523cd478dfab577f8c9290aeef484c57a5`.
- Git verified the bundle successfully. It was imported into a new local bare
  repository for read-only comparison.

**The hosted artifact expires October 25, 2026. Retain the downloaded ZIP.** A
manifest, a PR, a commit URL, or an expiring artifact link alone is not a durable
replacement for the complete Git objects. The separate September 11 artifact
`10263714534` is historical and must not be confused with this fresh capture.

The reused audit script compares its original `oldest` records against an old
September 11 pin and only analyzes 40 in depth. This recovery **does not reuse
those stale comparisons**. It re-sorts the full fresh branch array and compares
the selected 100 with the `live_main_sha` above using the fresh Git bundle.

## Recovery coverage and limits

All 100 heads and their reachable history are retained in the full bundle.
Their branch-side history contains **482 distinct commits not reachable from
comparison main**. Lack of ancestry is not proof of missing implementation:
squashed or independently landed fixes can already provide the same behavior.

For the 99 selected branches with one merge base, the offline packet exports
branch/base binary-capable diffs and **941 changed, extant file occurrences**,
with original Git blob IDs, byte counts and SHA-256 values. **335 occurrences
are byte-identical to a blob at some path on comparison main**. Occurrences are
not unique files or semantic fixes. Different blobs are not automatically
useful, missing, or safe to apply. Deleted files and intermediate content remain
available through the full Git history.

Rank 81 (`codex/36-verification-integration`) has two merge bases:
`942843b63e5953f49a914b3b34229305df8f8761` and
`aa389ff95688959eba541a3bb3bdf11ed2174135`. It is deliberately **not** flattened
into a misleading single-base diff. Its tree and history are preserved.

Ranks 27 and 76 have empty final tree deltas against their single merge bases,
but respectively retain six and four branch-side commits. Their intermediate
payloads must not be discarded merely because the tip diff is empty.

This is a complete identity/graph capture and mechanical diff/blob census of
the 100, with focused semantic reconciliation. It is **not** a claim that every
historical compiler change has been semantically revalidated on current main.
No compiler, self-host, benchmark, 9700X workload or historical publisher was run.

## Useful material reconciled with comparison main

### LLVM integer wire-encoding coverage

[The recovered test function](llvm-integer-encoding.c.txt) is an exact source
excerpt from `bb25896d405551817be91da5edf56b267ce4b2b3`,
`src/buster/tests/compiler/llvm/bitcode_test.c`, function
`llvm_bitcode_test_integer_encoding`. Rank 11 is the original candidate;
rank 21 carries another version of that work.

It checks explicit wire values for five signed minima, exhausts every bit
pattern at widths 1 through 16, and checks boundaries through width 64. This
is useful **additional test-design material**, not a revived production fix.
Main already restricts the special minimum sentinel to width 64. Main's
serialized-reader test currently uses widths `{1, 8, 16, 32, 64}` and six
patterns per width; the recovered exhaustive loop is not registered there.
The independent consumer fixtures were already recovered by the September 11
LLVM boundary work; do not duplicate or weaken them.

The historical test depends on `llvm_bitcode_test_encode_integer_bits`, which
is not the current registered test interface. Any integration must adapt to
current test boundaries, retain an independent decoder/oracle, avoid exposing
an unnecessary production API, and bound test memory/cost. The `.txt` excerpt
is intentionally not compiled or registered by this PR. #222 remains fixed.

### Conditional Clang native-feature workaround

[The recovered CMake block](clang-native-avx10.cmake.txt) is an exact excerpt
from `8c3b9944ff0ecee0c25d89b312ca005729f51abb:CMakeLists.txt` (rank 2).
It reprobes native Clang flags on configure, and appends `-mno-avx10.1-256`
only if the ordinary native probe fails and the corrected probe succeeds.
It does not globally disable AVX-512 or warnings-as-errors.

The block is absent from comparison main, but that alone is **not a current
build defect**. #68's later closure evidence uses supported Clang 21 builds;
the historical Clang 18/Granite Rapids diagnostic and any support obligation
must be reconciled before enabling a workaround. Preserve the option for a
future supported-toolchain reproduction, rather than reinstalling old policy.

### Assembly-output measurement prototype

Rank 48, `ddd3aff3944a3b8d8a1f3aa51ff65a0153f5c600`, retains the complete
6,635-byte `.github/z16/prepare_harness.py` as source data in the bundle and
offline packet. It describes an opt-in `--emit object|assembly` extension to
the existing harness, not a new runner:

- Synthetic assembly output changes `-c` to `-S` and uses `.s`; object output
  remains the default. Frozen self-host stages still emit executables.
- Metadata records `synthetic_output`; compiler argv is retained. Assembly and
  object output are separate measurement populations, never opposite A/B arms.
- Tests cover both emission modes across three stages and four allocators,
  default/last-option behavior, invalid and missing option values, and continued
  rejection of output/allocator overrides through `--flag`.
- Existing corpus, output-identity, diagnostics and admission controls remain
  distinct. Historical suggested benchmark commands are not current approval.

Neither `emit_assembly` nor `synthetic_output` is present in comparison main's
`tools/throughput/throughput.c`. #116 is already closed: this is an optional
measurement-enablement recipe, not evidence that its printer fix is missing.
Do not execute the historical source-rewriting helper against current main.

### Already preserved or superseded work

The September 12 recovery tool/manifest already covers readable FAST allocator
patches, canonical complex-value candidates, EVEX oracle scripts and the newer
canonical-entry-layout candidate. Reuse
[that recovery record](../2026-09-12-branch-recovery/README.md); do not open
competing implementations solely because their old branches still exist.

The rank-94 #108 file-copy reproduction is superseded in scope by current
`file_test_copy_aliases` and the current failure/publication tests. Retain the
original evidence, but do not transplant its obsolete failure-injection wiring.

Rank 56's `.github/z13-transfer.bin` is a zlib stream, not an empty binary.
Strict decompression fails with `incorrect data check`. Its raw bytes are
preserved; it is **not** represented as a successfully decoded or validated
QUALITY implementation. The older canonical/FAST corrupt transports similarly
must not be silently repaired or declared equivalent to readable candidates.

Native-retirement archives, verification trees, census recovery and atomic
transport in ranks 77-81, 96 and 100 are historical evidence, **not current
acceptance**. Do not edit generated bindings or displace #522/#1014/#923 or
other current owners while recovering them.

## Safe continuation of cleanup

Use the exact 100-row inventory, not a later alphabetic branch list. Before
any deletion, verify the complete backup remains available, retrieve current
heads/protection/open-PR head-and-base references/active workflows, reconcile
intentional archives and exact-head evidence references, and preserve any
newly discovered useful material in its existing issue/PR ownership lane.

A ref that moved must be held, not deleted at its new head under old approval.
Use an exact expected-head lease for a permitted deletion and verify the
result afterwards. Never run an old cleanup workflow whose hard-coded targets
belong to an earlier inventory. No destructive workflow or deletion script
is installed by this PR.

Local inspection of the retained bundle needs no network:

```sh
git clone --mirror /path/to/buster-before-cleanup.bundle recovered.git
git -C recovered.git bundle verify /path/to/buster-before-cleanup.bundle
git -C recovered.git show bb25896d405551817be91da5edf56b267ce4b2b3:src/buster/tests/compiler/llvm/bitcode_test.c
```

The original workflow-authoring attempt was blocked and was not retried through
a different write path. Reusing the separately inspected read-only preservation
job supplied the backup without introducing a new workflow.
