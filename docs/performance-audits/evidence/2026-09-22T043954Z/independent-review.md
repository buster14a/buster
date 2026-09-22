# Independent IR discovery and adversarial review

Evidence pin: `ec1897b900c694e57a10ee5ddaacd89c4cd7c57c`, tree
`1e20ddfb534ba16a05a277136ff4f391a93dc28f`. Inspected local source and issue
snapshots `novelty_0.json`/`novelty_1.json`; no publication, production edits,
compiler build, compiler timing, or hardware performance claim.

Read AGENTS.md, research lifecycle, relevant benchmarking/machine/frontend
guidance and newest audit `docs/performance-audits/2026-09-22T023624Z.md`.
That newest audit concerns diagnostic observer overhead, not compiler speed.

## Negative discovery results

### Sparse instruction-extra permutation: plausible kernel, weak materiality

`src/buster/lib/compiler/ir/ir_cfg.c:106–142` remaps each sparse extra's key,
bottom-up mergesorts full `IrCfgExtraOrder` records, then copies them back.
It runs from `ir_cfg_publish_instruction_rows` at line 351 only after an
actual instruction-row permutation. On x86-64 the extra is 56 bytes and the
extra-plus-ID row occupies 64 bytes. Two full temporary arrays retain 128E
bytes; each merge level reads/writes about 128E logical bytes. This is a
source-derived traffic count, not measured DRAM traffic or time.

The strongest scalar change is indirect sorting: pack `(new_instruction_id,
old_extra_index)` into 8-byte keys, sort those, then cycle-permute the 56-byte
payload once. The sorted keys themselves can carry a consumed permutation,
as the existing dense row inverse map does. This uses 16E scratch bytes for
two sorting arrays and moves payload O(E), rather than every merge level.
Potential SIMD work should follow this scalar alternative, not compare only
to full-payload sorting.

A more ambitious sparse permutation join can scan the already-existing
inverse instruction order, query old-row membership/rank, and compact selected
extra IDs. A dense old-row-to-extra-index table costs 4N initialization bytes;
a per-64-row membership/rank directory costs roughly 0.19N bytes and gathers.
Both pay O(N) work to avoid O(E log E), so they are poor for rare E/N.

Reason not shortlisted: extras are explicitly rare asm/name/literal payloads
(`ir.h:465–481`). The old counted unity profile at
`docs/performance-audits/evidence/2026-09-13T130555Z/final-znver3/unity-lines.txt:676`
shows just 151 executions at the extra-count increment. This is historical,
not current incidence, but strongly argues against promoting an elaborate
SIMD design without a new `(N,E,moved)` census. #38 already owns the dense CFG
publication architecture; no matching exact sparse-sort mechanism appeared in
the bounded search. Do not file a speculative issue merely for this kernel.

### Global relocation overlap validation: duplicate research

`ir.c:4719–4740` still checks every earlier relocation's overlap, O(R²).
Private checked-interval sorting would remove it. However
`docs/performance-audits/2026-09-13T132324Z.md:105,300` already proposes that exact
mechanism with stable error selection and vector/scalar alternatives. #564
owns validation attribution; #229 separately owns producer-side designated
initializer relocation compaction. Reject as a new discovery.

### Lazy machine dominance construction: small distinct mechanism, wrong route

`machine.c:4753` unconditionally builds a dominance forest before checking
immutable uses; same-block uses and same-source-block edge copies do not query
it (`:4781–4784`, `:4810–4812`). Constructing the unchanged forest on the first
cross-block query would preserve the verified predicate and avoid graph work
on functions containing only local immutable uses. It does not assume block
order equals dominance and does not weaken unreachable-component checks when
they are needed.

But `codegen.c:12119–12120` skips the verifier for producer-certified ordinary
MIR unless `verify_invariants` is requested; scheduled verification at :12165
is also explicit. This mainly helps explicit verification/replay, not normal
certified compilation. Reject from the primary throughput shortlist; no census
or timing was run.

## Switch-certificate adversarial review

Current semantic overlap check: `c_parse.c:20246–20257`.
Current lowering overlap check: `c_gen.c:36927–36953`.
After each path's existing normalization, a sorted private `(low,high)` array
can certify disjointness using only adjacent intervals. Inclusive equality is
an overlap. Proof: if sorted interval i overlaps later j, the immediate
successor k=i+1 has `low[k] <= low[j] <= high[i]`, so some adjacent pair
overlaps. A prefix-maximum recurrence is unnecessary for a Boolean certificate.

This is useful for SIMD: after sorting, unsigned-qword adjacent comparisons
are independent; no scan dependency must be invented. Return success only
when every adjacent pair is disjoint. On overlap/unknown, retain the original
source-order check for the exact diagnostic. Do not infer the original first
bad label from the first sorted adjacent collision: source labels
`ordinal0:[0,10], ordinal1:[3,4], ordinal2:[1,2]` first fail at ordinal1,
whereas the only first adjacent witness may involve ordinal2.

The best scalar rival should include a zero-scratch bounding-hull certificate.
Maintain `min_low,max_high`; a new interval is certainly disjoint if
`high<min_low || low>max_high`, then extend the hull. A failure means UNKNOWN,
not overlap, because a valid interval can occupy a gap inside the hull. This
accepts ascending, descending, and alternating-outward source sequences in
O(C), often a realistic ordering for enum switches. Census its success rate
before attributing value to a vector sort.

Cross-stage certification is higher risk than independently optimizing both
checks. Semantic analysis obtains C types and sign-extended constant bits, then
masks/sign-biases (`c_parse.c:20080–20101`, `:20236–20245`); lowering first casts
its separate typed constants to a canonical promoted type
(`c_gen.c:36860–36921`). Fixed enums and bit-field promotion must not be assumed
equivalent merely because both paths are successful. Current guidance explicitly
retains checks for direct lowering callers (semantic-validation.md:23–27).

A later persistent fact must bind the exact switch token identity,
target/dialect/model lifetime, promoted width/signedness, ordered label tokens,
and normalized low/high pairs. Safest reuse guard: compare exact normalized
pairs in the existing lowering conversion loop, O(C), before omitting the
quadratic check. No hash-only proof; no unchecked/copied/stale model certificate.
The first implementation should optimize both overlap predicates separately,
preserving all normalization and diagnostics before considering persistent reuse.

## PDB stable-retirement map: independent correctness review

Source: `src/buster/lib/compiler/pdb/pdb.c:1080–1154` and consumer
`:1171–1177`. The existing code initializes `final_index` to the original global
index, then composes all N entries through `round_map` after every merging
round. `round_map` itself remains necessary for rewriting the surviving records.

Proposed map-only change: at each match of a live record into an earlier
survivor, write `final_index[child_original]=0x1000+parent_original`. Live
records are never parent-written. At termination, walk original ordinals in
ascending order with a cursor over final `live[]`: assign final roots their
compact indices, and give every other entry its earlier parent's already-final
index. Reuse the same `final_index` allocation without its initial fill or a
separate parent array.

Proof obligations satisfied by current stable loop:

1. `live` starts in ascending original order; retaining first representatives
   preserves that order every round.
2. Every parent is a strictly smaller original ordinal than the retired child.
3. A retired record never becomes live again, so it receives exactly one link.
4. A parent may retire later; its own smaller parent is resolved before the
   child's final ascending visit. One indexed read per nonroot is enough.
5. A no-merge terminal round writes no links. The eight-round cap, stable
   survivor choice, hash/equality rules and record-rewrite passes are unchanged.
6. A failed record rewrite already returns before any final map consumer, so
   the proof needs no changed failure recovery.
7. Current total-record bounds before provisional indexing keep bias addition
   within u32; retain those checks and the exact zero-record handling.

The independent executable `pdb_experiment.py` models arbitrary stable equality
partitions, not a particular hash behavior. Its candidate array begins poisoned;
every would-be uninitialized read, repeated retirement and nondecreasing parent
raises an assertion. It compares exact final arrays against current sequential
map composition.

Executed command: `python3 pdb_experiment.py > pdb_experiment_results.json`.
Results: 18,972 exhaustive complete transcripts for N=0..6, plus 20,000 random
transcripts N<=512 with seed 47336792102, all passing. Random runs reached the
eight-merging-round cap. Explicit controls include empty, no merges,
all-at-once merge, and an eight-link chain in which a parent retires in each
later round. Zero mismatches and zero poison reads. Script SHA-256:
`ff342e80dbcab8702af0ccab858dab29a95013adf20eaf41e3ca72afcbbf8468`.

This is a Python map-equivalence experiment. It does not validate real record
normalization, C undefined behavior, PDB bytes, compiler output, or performance.

Map-write counts: old `N*(1+R)` for R merging rounds; proposed `N+(N-F)` where
F is the final survivor count. The N=128/R=8/F=120 chain has 1152 versus 136
map writes. But N=128/R=1/F=1 has 256 versus 255: one-round merging alone gives
almost no map-write reduction. No-merger workloads retain N writes but may
regress because the old map initialization was fused with `live[]` initialization,
whereas the new final pass adds cursor comparisons and live-array loads.
Require no-merge and one-round controls. A scalar identity finalization path
can avoid survivor lookups when no merging round occurred; skipping identity
composition entirely is a later consumer change, not assumed in this model.

Performance is unresolved: census N/R/F, actual record-byte rewrite work,
PDB-enabled route incidence and final map phase share. Keep ordinary native
compilation without PDB separate. This redesign primarily shortens repeated
map traffic, but the final parent recurrence is not a free SIMD gather: arbitrary
earlier-parent dependencies within a vector block require explicit handling.
