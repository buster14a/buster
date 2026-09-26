# Shared-promotion consumer quotient: bounded research packet

Source: `ade6ac4b6ecb21f30b61b656439bac476c145e2f`, tree
`4c5306221fdb22fccc929b55e333163742de17d0`; `ir_promote.c` blob
`3a0bf7fd859afeb9c3d34d2db49368717740903a`.

No production source or default is changed by this branch. `overlay.patch`
applies only in an isolated hosted worktree after asserting the original blob.
The sole writer owns this new investigation directory and its unique workflow;
existing implementation branches, generated bindings, retirement, service,
admission and deployment are outside scope. No merge is requested.

## Domain and exact equivalence

Domain: existing eligible non-block-local owners reaching `ir_promote_global`
inside ONE function's promotion invocation, AFTER event classification and CFG
validation/materialization, BEFORE dense compaction. The fixed context is the
entry block, reachability, terminator targets including their multiplicities,
and ordered deduplicated predecessor lists. Do not reuse across functions,
modules, targets, invalidated CFGs, or later transformation epochs.

For each owner fold its existing ordered events into:

* `E[b]`: 0 = forward incoming state, 1 = last definition is STORE, 2 = last
  definition is LOCAL/lifetime reset;
* `U[b]`: a LOAD occurred before any definition/reset in block b;
* `A`: every event is reachable and no LOAD follows a LOCAL without a STORE.

Owners x and y are equivalent precisely when their A, E and U are equal in
that fixed context. This is a conservative sufficient quotient, NOT a minimal
semantic equivalence over C variables. Values, types and provenance are not
interchangeable. A key encodes E | (U << 2) in one byte per block plus A.

The expensive shared answer is only `(analysis_safe, propagated_live_in)`.
Let `base[b]` mean entry, unreachable, or predecessor-free. The least fixed
point is:

```
bad_in[b]  = base[b] OR any predecessor p: bad_out[p]
bad_out[b] = (E[b] == reset) OR (E[b] == forward AND bad_in[b])
analysis_safe = A AND NOT any b: U[b] AND bad_in[b]
live[b] = U[b] OR (E[b] == forward AND any successor s: live[s])
```

The last equation runs only when analysis_safe. Equality of inputs to identical
monotone equations preserves every future worklist step and the least fixed
point. `model.c` compares an independent synchronous solver with a transcription
of the queue algorithm across every directed three-block graph, entry, ternary
transfer vector, seed vector and A bit. Equal event summaries are also checked
under common appended events: by induction this extends to every finite common
suffix. Arbitrary insert/delete/reorder or CFG mutation invalidates the key;
this is not a cache certificate surviving mutation.

## Facts retained per original owner

The existing event chains, last stored value per block, canonical type and local
IDs, instruction/value IDs, source ranges, debug names/scopes, eligibility,
qualifiers and escape/barrier decisions remain in their original representations.
Parameter numbering, current block/value capacity checks, typed incoming values,
ordered predecessor lists, single-predecessor value propagation, event removal,
alias replacement, compaction and canonical validation still run per owner.
In particular the FINAL success flag cannot be shared: an earlier owner may
consume the last available parameter ID. The prototype stores the analysis
answer before those checks, never the final return value.

The producer event rows and terminator targets are stable during this invocation.
Earlier promotions mark removal bits and append parameters; they do not rewrite
the later owners' event opcodes/blocks before the single final compaction.
The new sidecar uses the same existing scratch arena and dies at scratch_end.
No canonical/public record, arena lifetime, validation certificate, lane,
worker pool, function-pointer dispatch or frontend is added.

## Counterexamples that narrow the relation

LOAD;STORE and STORE;LOAD have the same final effect but different U. A reset
is not a definition. An unreachable predecessor feeding a reachable read must
still poison initialization. An unreachable event or load-after-reset rejection
cannot be omitted from A. Equal E/U under different CFGs is not sufficient.
Sharing typed values or final parameter-allocation success is unsound even when
the Boolean kernel answer is equal. Equal layout/hash/instructions is not used.
`family.c` is an explicit synthetic high-reuse control with distinct values and
mixed int/double/pointer owners; ordinary repository inputs remain separate.

## Prototype and full cost accounting

Eight representatives per function, first-seen order, no eviction, no persistent
or warm cache. Exact byte equality is checked; hashes do not establish equality.
Overflow falls back to the existing algorithm. `observe` executes every original
kernel and checks every hit against its representative. `share` actually skips
only the two Boolean kernels on hits and copies the retained live-in result.
Unset `BUSTER_RESEARCH_PROMOTION_QUOTIENT` preserves original decisions.

Extra compiler scratch requested per function is `17 * block_count + sizeof
(PromotionQuotient)`: eight keys, eight result vectors and one current key;
metadata is fixed-size. The original block arrays and value work remain.
For N owners, construction touches N*B key cells; exact comparison bytes,
class insert copies (2*B), hit result copies (B), actual skipped block/edge
iterations, and maximum single-function sidecar request are recorded. These
are DIFFERENT operation units, not a cycle model or peak RSS. Diagnostic
formatting/I/O and its temporary buffer are additional costs and are explicitly
not part of a performance claim. The offline census also retains every full
key under bounded workspace limits; that workspace is not a proposed compiler
allocation. Zero kernel calls mean zero quotient opportunity, not zero frontend
SSA or whole-compiler work.

`model --census log` computes the exact uncapped class count by function and
complete key, separately from the eight-class implementation's hits. Each log
must be one single-TU compile; do not pool per-process function IDs across files.
Default direct SSA and forced memory form MUST remain separate populations.
The frozen full compiler unity input is the representative scale workload;
selected checked-in fixtures exercise boundaries; the synthetic family is not
representative evidence. Rejected compilations remain visible and are not
counted as successful object comparisons.

## Predeclared disposition and validation gates

Reject the default-path integration hypothesis if the full frozen compiler's
default lowering has no reusable global kernels, or fewer than 5% of its global
kernels are reusable. Do not rescue that hypothesis using memory-form or
synthetic counts. Even above that screening threshold, integration needs
complete cost evidence; key construction/comparison/copy counts can veto a
candidate, but are not a measured timing verdict. Reject any semantic mismatch.

Hosted checks: Clang/GCC finite model, ASan/UBSan model, before/after self-host
fixed points, baseline/prototype regression suites, and identical objects and
diagnostics for frozen inputs across both lowering forms. Foreign object
comparisons are not foreign runtime execution. Report every incomplete gate.

No performance runs are authorized by this workflow: it uses only standard
GitHub-hosted Ubuntu, never self-hosted/9700X labels or SSH. An uninstrumented
performance decision, if warranted, separately requires the exclusively leased
approved 9700X path, matched trusted Clang builds and rebuild/path controls,
frozen inputs/flags/targets, A/A controls, fixed uninstrumented A/B sampling,
complete artifact timing, peak memory and all setup/conversion costs. No
baseline/candidate elapsed-time claim may be inferred from these hosted logs.

Actual execution provenance and the final research disposition belong in the
linked GitHub issue. This packet does not claim independent subagent review:
no callable subagent executor was available to this session.
