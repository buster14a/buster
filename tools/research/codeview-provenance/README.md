# CodeView relocation provenance: research only

Follow-up to the CodeView lead already recorded in #1319, **not a new report of that quadratic**. This experiment asks whether preserving the input ownership known by the producer avoids building a general consumer-side index. No production files/defaults, canonical IR, persistent lanes, generated bindings, service, admission, deployment, or retirement code are changed. Do not merge as a production optimization.

## Exact source and scope

Investigated main commit: `ade6ac4b6ecb21f30b61b656439bac476c145e2f`.
Tree: `4c5306221fdb22fccc929b55e333163742de17d0`.
`src/buster/lib/compiler/link/link.c` blob: `ceef10d857cfdf3fe20a09d10407077563baa84e`.

The standalone C harness extracts the actual `link_pe_resolved_codeview` body from that source, adding just one test-only loop-visit counter. It uses small record/arena adapters; **it does not compile the real ObjectFile producer or the full compiler, parse a complete CodeView stream, produce a PE/PDB, prove self-hosting, or test the production arena's failure policy**. Its inputs are resolver fixtures, not a claimed real-program corpus. This boundary is intentional and must stay visible alongside every result.

Read source: `link.c` 428-513 (initializer collection and stable filtering), 1901-2077 (section/module construction), 2258-2290 (stable surviving relocation append), 6603-6669 (resolver), 8470-8509 (per-module PDB handoff); `object.h` 270-290 (module identity); `pdb.c` 950-1020 and 1450-1469 (names publication). Line numbers are navigational; the pinned blobs are authoritative.

Current AGENTS, frontend/linkage, build, testing, benchmarking, workflow and recent audit `docs/performance-audits/2026-09-25T215313Z.md` on `claude/laughing-noether-meufke` were read. Related issues/PRs include #1319, #1309, #1310, #987, #990, #623/#981, #204/#208 and #488/#798. The already-published canonical CFG join and the merged empty-promotion sweep are not rediscovered here.

Ownership: one writer on `research/codeview-provenance-20260926`; only new research paths and its branch-only correctness workflow. Existing production owners/branches are untouched. Connected GitHub is the publication/read path. No actual subagent launcher was available; **no independent agent review is claimed**. The editing container did not execute the experiment or any compiler test. Hosted worktrees are used by the workflow.

## Three candidates and provisional ranking

The ranking is research priority, not an empirically established ranking of wall-time wins. No current approved timing or real-input population census was acquired in this session.

| Rank | Relation, invocation and avoidable term | Invariant / replacement | Reach, proof difficulty, disposition |
|---|---|---|---|
| 1 | PE debug resolver runs once per debug module: M scans of all R relocations | Input-owned contiguous surviving relocation runs; restrict each module to its contributing input | Multi-input Windows debug links; moderate proof difficulty because bounds and later mutation are not already certified. Potentially removes a growing cross-input product with O(M) side data. Full-pipeline share is unmeasured; negligible for M <= 1. Research below follows #1319 rather than duplicating it. |
| 2 | Initializer collection searches R relocations per pointer slot E, stopping at the first match | Bounded aligned slot identity permits fill-first placement and final ordered compaction | Registration-heavy programs/foreign static initializers; low-to-moderate proof difficulty. Null slots, duplicate first-match policy, partial trailing bytes and destructor reversal matter. E near zero on ordinary C. Already explicitly proposed in #1319: no competing implementation here. |
| 3 | PDB names insertion republishes every source-name occurrence and probes occupied buckets even for identical names: at least c_p(c_p-1)/2 occupied probes for c_p identical paths | Establish one deterministic first-occurrence identity per distinct path before assigning offsets; retain per-module checksum relations | Header-heavy multi-module PDBs; potentially broad in that workload, no current population count. More difficult output-offset/checksum and format proof. Existing #1319 lead; no renamed duplicate issue or interning-everything proposal. |

For all three, required output work remains. The CodeView helper must copy B module bytes and apply Q matching relocations; the initializer helper must emit actual registrations; PDB must preserve each module's file/checksum relations even when name storage is shared.

## Cost model

Let there be I contributing input objects. Input i has m_i debug modules and r_i surviving relocations; M = sum m_i and R = sum r_i. D is the number of CodeView-section relocations; Q is the total module/relocation matches, counting repeats for overlapping module intervals. B is the sum of module output byte lengths. K is the number of distinct relocated sites and U their update multiplicity. Symbol-domain size S is independent of those quantities.

For successful full scans, the current resolver performs exactly **M*R relocation visits**, plus required O(B+Q) copying/patching. The redundant part is discovering repeatedly that another input's relocation cannot belong to this input's module.

A certified span performs **sum_i m_i*r_i** resolver visits, plus O(R+M) construction guards and span writes. Guards are charged even though they can be piggybacked on the existing append loop. They are not represented as free lookups. New side data is two u32 values per module (8M bytes) plus a fixed certificate record. There is no relocation packing, sort, domain-sized zeroing or deterministic merge. M <= 1 bypasses index/certificate setup in the modeled algorithm.

For m_i = 1, the resolver term becomes R. For one already-merged input containing all M modules it remains M*R: the proposal does **not** eliminate the local product. Nested linking loses the benefit unless child ownership is retained or deliberately rebuilt; no such propagation is implemented here.

### Optimized baseline, not only the current full scan

`index_build` uses already-ordered module boundaries directly, or a bounded stable bottom-up merge when unordered; validates nonempty intervals are disjoint; then performs count/prefix/stable scatter using binary ownership searches. It preserves original relocation order within each module. Overlapping/malformed intervals use the original full scan instead of changing semantics. M <= 1 bypasses setup.

On the disjoint case its work is O(M + R + D log M + Q), with a second R scan and second set of ownership searches to avoid an owner-per-relocation map. An unordered boundary array adds O(M log M). Its exact ledger includes two R scans, actual key comparisons, M zeroed counts, prefix/cursor writes, Q copied records, sort moves, and all requested scratch/packed storage. With the test adapter's 24-byte boundary and 32-byte relocation, requested index storage is 60M + 4 + 32Q bytes. This is requested algorithm storage, **not RSS**; fixture storage, common output arenas, allocator metadata and lifetime/peak effects are separate. A still more specialized index or tuning the crossover may beat both candidates.

The CSV deliberately does not add unlike counters into a purported instruction count or speed ratio. `metadata_writes` also includes one per-module containment-guard visit; it is a work ledger, not a PMU counter. `setup_rows` is additional construction-guard visits for provenance, versus actual indexing passes for the index.

## Construction certificate and semantic argument

1. Input debug-symbol contributions occupy disjoint, checked byte intervals. Every participating module lies within its own input contribution, with subtraction-based bounds checks and no overflowing rebasing. Inputs with ambiguous duplicate neutral debug-section kinds cannot receive this certificate without an explicit additional proof.
2. **Every surviving CodeView relocation**, including those from inputs without debug modules, lies within its own contribution and has checked rebasing. One escaping relocation disables the entire certificate: checking only the destination module's input is insufficient.
3. The producer appends surviving relocations in input order. COMDAT removal precedes append, so each input's surviving records are one contiguous run, even when original offsets are unsorted. Record run endpoints from the output cursor, not original source relocation counts.
4. If module j belongs to input i, every matching relocation belongs to that same input by disjoint containment. Removing records outside input i's run removes only records that the original helper would skip before symbol/kind validation. The unchanged helper sees the same remaining records in the same order, so copied bytes, last-writer effects, validation failures and invalid-symbol skips are preserved.
5. Symbol definitions may be forward-referenced: the inspected merger constructs all symbol maps before appending relocations. No source-order definition assumption is used. Repeated or overlapping writes inside a module remain ordered; duplicate/overlapping modules **inside the same input** are safe for spans too.
6. A certificate is valid only for its sealed relocation/module view and lifetime. Pointer/count identity is a useful guard, **not** proof against in-place same-count mutation. The fixture models explicit epoch invalidation. Real integration must own and enforce invalidation, not add an unmaintained epoch field.

The containment property is **not already guaranteed by current link_objects**. Its relocation loop rebases offsets without validating containment, and its module-copy loop does not validate module extents. Consequently this prototype uses a conservative fallback, not a new rejection rule. Otherwise an escaping relocation from an earlier input could legitimately be observed by the current resolver while the proposed restricted view silently omitted it.

The current initializer stripper copies and filters the relocation array after merging. A production implementation must invalidate or remap spans there. Because runs are ordered, a bounded walk can translate endpoints during stable filtering in O(R+M), without a full R-element inverse map, but **that remapping is not implemented or validated here**. The prototype tests invalidation/fallback instead. Unknown reorderings, reused arenas, mutated debug metadata, foreign objects and unavailable ownership use the original path. No certificate is published as a production field.

## Predeclared predictions, before hosted execution

Default scaling family: I separate inputs, one module each, four distinct sites, one update, paired 32-/16-bit debug relocations, eight unrelated relocations per module. Thus M=I, R=16I, D=Q=8I.

| I | Full resolver visits | Provenance resolver visits | Provenance construction row guards | Indexed resolver visits |
|---:|---:|---:|---:|---:|
| 1 | 16 | 16 | 0 | 16 (small-case bypass) |
| 2 | 64 | 32 | 32 | 16 |
| 32 | 16,384 | 512 | 512 | 256 |
| 128 | 262,144 | 2,048 | 2,048 | 1,024 |
| 1,024 | 16,777,216 | 16,384 | 16,384 | 8,192 |

The indexed resolver count alone is lower than provenance because it discards unrelated relocations. Its two R passes, binary probes, prefix arrays and record copies must still be counted. At I=1 both alternatives deliberately bypass setup.

The decisive independent-dimension family fixes **M=128 and R=2,048**, varying only input partitioning I in {1,2,8,32,128}. Predicted full visits stay 262,144; provenance visits are 262,144/I. This demonstrates both the opportunity and its negative control without conflating more modules with more relocations.

Other families independently vary distinct sites, repeated updates, unrelated records and symbol-domain size; include forward identities; and test empty populations, overlapping writes, negative/overflowing addends, invalid symbol skips, ignored kinds, field-width boundaries, escaping offsets/modules, dropped records, replaced arrays, same-count reversal, post-merge filtering, empty modules, overlapping modules within an input, and unordered module boundaries. The harness contains 301 deterministic cases, comparing both alternatives with the reference, plus cross-build output checks. This is not exhaustive enumeration of all object inputs.

Mode-zero cases assert the exact predicted full/index/span visit formulas. Error cases may stop inside the real resolver before scanning the complete view, so successful-input visit formulas do not apply to them; their gate is identical success/failure and output bytes. Fallbacks and their paid setup are visible in the CSV. No test result is asserted in this pre-execution document.

## Crossover and pipeline ceiling

Against full scanning, a necessary work opportunity is M*R - sum_i m_i*r_i exceeding the added R/M guards and metadata traffic; branch, cache and allocation costs still require measurement. Against the stable index, provenance is attractive for many small independently owned inputs, while the index can win for large m_i or a large unrelated-relocation fraction. One aggregate input is an intentional losing case for provenance. Do not select a production threshold from these abstract counters.

Let f be the share of complete artifact time spent on removable matching, and let g be its measured remaining fraction after counting setup and memory effects. Even an ideal implementation is bounded by 1 / ((1-f)+f*g); eliminating matching entirely caps improvement at 1/(1-f). Neither f nor g is measured here. Frontend, code generation, required debug copying, type merging, final PDB packing and output I/O remain. No full-pipeline speedup, regression budget, or 9700X acceptance is claimed.

## Hosted execution and remaining gates

The branch-only workflow uses a standard GitHub-hosted Ubuntu executor and an isolated worktree at the exact submitted head. It verifies the pinned parent/source blob; records compiler versions and hashes; extracts the reference body; builds and runs Clang -O2, GCC -O2, and Clang ASan/UBSan; compares full CSV/results; and retains artifacts. No benchmark, desktop test, SSH, dedicated runner, secret, deployment or write-enabled job is requested.

Counts/correctness results must be attached to the exact completed run, not inferred from a queued workflow. This prototype still needs independent review, a real multi-TU PE/PDB population census, real-producer certificate/invalidation integration tests, full compiler/self-host/platform correctness, and approved exclusively leased 9700X matched trusted Clang A/A and uninstrumented A/B measurements with complete artifact and memory/setup accounting before any production decision.
