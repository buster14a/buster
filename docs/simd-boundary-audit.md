# SIMD producer-consumer boundary audit

This is the implementation inventory for
[#298](https://github.com/buster14a/buster/issues/298), audited on October 23,
2026 against **`42630114aeb0553c3e2fabfd7fe9bc84b0ce6126`**. Source links in
the stage-guide audit sections deliberately name that revision. Later branch
work is a dependency, not evidence about this snapshot. The mandatory rule is
already in [the coding standard](coding-style.md#simd-producer-consumer-stage-contracts),
landed by [#299](https://github.com/buster14a/buster/pull/299).

The disposition is to retain the documented interfaces at this pin, preserve
two already eliminated materializations, and pursue the specific measurements
and integrations below. This audit changes no executable interface and makes
no new compiler speedup claim. An implemented kernel, a passing module test,
an integrated producer/consumer path and a measured whole compiler are different
claims. In particular, the normal C driver only preprocesses and parses;
`check` has a supported semantic/static-data path, but neither emits executable
C functions at this revision.

## Inventory and dispositions

Each linked stage section enumerates actual entry points, substages and shared
consumers, with storage, batching, waste, alternatives and validation. Its
existing detailed design remains authoritative for individual fields. An
"implemented" edge below can be exercised by a module test without being
connected to the normal source driver.

| Issue category and actual edges | Contract and disposition |
| --- | --- |
| Source bytes → raw token columns; mode-specific recovery | [Lexer](c-lexer.md#boundary-audit-298). Implemented; bounded source reads and separately provisioned output slack. |
| Raw columns → normalized identifiers/leading space → interned values; intern-table readers | [Refinement](c-refinement.md#boundary-audit-298). Implemented in-place ends/value handoff; hash/probe/budget work remains [#116](https://github.com/buster14a/buster/issues/116). |
| Per-file preparation → directive/guard tables → unit consumers; lane-local IDs → shared IDs; speculative include walk | [File cache](c-files.md#boundary-audit-298) and [preprocessor](c-preprocessor.md#boundary-audit-298). Implemented; deterministic remap/barriers, not concurrent mutation of one intern table. |
| Macro plans/arguments → expansion buffers → final tokens; history, pragmas, spelling and diagnostic readers | [Preprocessor](c-preprocessor.md#boundary-audit-298). Implemented ordered expansion; retained history uses runs rather than a final per-token origin column. |
| Final tokens → parser tokens → K1/K3/K2/K4/K5/check/fallback → tree/postorder | [Parser](c-parser.md#boundary-audit-298). Implemented; postorder deliberately absent from the syntax-only driver. Conversion specialization is [#335](https://github.com/buster14a/buster/issues/335). |
| Source-order binding events → stack/batch resolver → declaration/type columns | [Bindings](c-bindings.md#boundary-audit-298). Checker integration exists; parser extraction is pending [#320](https://github.com/buster14a/buster/pull/320). Shared consumption is [#336](https://github.com/buster14a/buster/issues/336). |
| Tree/postorder → declaration elaboration/graph → semantic actions/constants → typed columns and reports | [Checker](c-checker.md#boundary-audit-298). Implemented for its stated source subset; dependency levels are derived from operands, not bracket depth. |
| Type requests → canonical types/layout/conversions → checker/ABI | [Types](c-types.md#boundary-audit-298). Implemented; tag completion is an explicit mutation boundary. |
| Builtin descriptors/constant calls → folded values | [Builtins](c-builtins.md#boundary-audit-298). Implemented module; source and selector integration remains [#148](https://github.com/buster14a/buster/issues/148). |
| Types → ABI classification → call/parameter locations → Tier 0/frame consumers | [Calling convention](calling-convention.md#boundary-audit-298). Module and generated-record consumers exist; source-unit production remains [#133](https://github.com/buster14a/buster/issues/133)/[#137](https://github.com/buster14a/buster/issues/137). |
| Checked graph → executable typed function → Tier 0; typed function → SSA/IR → Tier 1 | [Tier 0](tier0.md#boundary-audit-298) and [pending integration](#pending-stage-integration). First source bridge missing; shared IR/SSA are active PRs, not files at the pin. |
| CFG/use/def → liveness/intersections; physical records/effects/costs → scheduling; object lifetimes/calls → frame plan → records | [Dataflow](dataflow.md#boundary-audit-298), [scheduling](tier1-scheduling.md#boundary-audit-298), [frames](tier1-frame.md#boundary-audit-298). Implemented independent modules with explicit upstream obligations. |
| Forms/operands → effects, legal forms and costs → selector/scheduler consumers | [Machine metadata](x86-semantics.md#boundary-audit-298), [target](x86-target.md#boundary-audit-298), [model](x86-zen5.md#boundary-audit-298). Keep legality, semantics and measured cost separate. |
| Selected/allocated records → short/near branches → lengths/offsets → encoded bytes/fixup ends | [Sizing](branch-sizing.md#boundary-audit-298) and [encoder](x86-64-encoder.md#boundary-audit-298). Implemented; discarded encoded-byte sizing scratch is deliberately eliminated. |
| Assembly source or preprocessed assembly → fragments/matching/layout → records/Link | [Assembler](assembler.md#boundary-audit-298). A real second producer; alignment makes its layout distinct from C branch sizing. |
| Encoded/raw bytes → bounded decoded records → formatting/listing/output | [Disassembler](x86-64-disassembler.md#boundary-audit-298). A real side consumer; actual encoded lengths and original listing bytes survive canonical semantic decoding. |
| Encoded code or checked scalar data → atoms/symbols/fixups → object or executable | [Object/linker](object-writer-and-linker.md#boundary-audit-298). Implemented shared representation; object writing reads bytes while executable patching mutates them. |
| ELF/archive/DSO → import/resolution → canonical Link; runtime import/snapshot → binding → dynamic inputs; layout → patch/hash/write | [Object/linker](object-writer-and-linker.md#boundary-audit-298). Implemented paths and side tables; foreign qualification, TLS, debug/source events and streaming retain their owners. |
| Per-unit/per-function work → ordered diagnostics and output | [Determinism](determinism.md#boundary-audit-298). Existing lane tests are evidence for their paths, not a missing multi-unit compiler driver. |

## Pending stage integration

The source tree and `main.c` include list at the pin contain no shared `ir.c`,
`ssa.c`, dominator module, general Tier 1 selector or allocator. The table records
their **branch implementations at the audit pin** and integration obligations;
it is not a live list of open PRs. No source lines from those branches are
silently mixed into this inventory.

Post-pin status checked at `47e1d96d9ab25fd21221e41c5c9162daf5a997ff`:
PRs [#333](https://github.com/buster14a/buster/pull/333) (allocator),
[#327](https://github.com/buster14a/buster/pull/327) (dominators),
[#331](https://github.com/buster14a/buster/pull/331) (differential harness) and
[#332](https://github.com/buster14a/buster/pull/332) (foreign references) have
since merged. Their own stage contracts and acceptance remain with their
owners; this audit's earlier source pin and validation run do not qualify those
new revisions or establish the missing complete source path.

| Missing or branch-only edge | Existing responsibility and concrete completion obligation |
| --- | --- |
| Parser source bindings and extension metadata → checker | [#320](https://github.com/buster14a/buster/pull/320), [#324](https://github.com/buster14a/buster/pull/324), [#128](https://github.com/buster14a/buster/issues/128)/[#147](https://github.com/buster14a/buster/issues/147)/[#133](https://github.com/buster14a/buster/issues/133); [#336](https://github.com/buster14a/buster/issues/336) requires one identity/activation contract and measures the combined extraction/check path. |
| Source-point environments → deferred body work | [#319](https://github.com/buster14a/buster/pull/319), [#113](https://github.com/buster14a/buster/issues/113); retain historical binding/tag completion, publish immutable environments before workers, preserve diagnostics. |
| Checked expressions/conversions/control flow → executable functions and selected static objects | [#133](https://github.com/buster14a/buster/issues/133), [#136](https://github.com/buster14a/buster/issues/136), [#129](https://github.com/buster14a/buster/issues/129), [#325](https://github.com/buster14a/buster/pull/325); emit actual ordered calls/branches, explicit conversions, symbols and body references, then execute/cross-check source programs. `CheckStream` is not `Tier0Function`. |
| Typed function → shared IR/use lists → SSA and dominators | [#322](https://github.com/buster14a/buster/pull/322), [#321](https://github.com/buster14a/buster/pull/321), [#327](https://github.com/buster14a/buster/pull/327), [#131](https://github.com/buster14a/buster/issues/131)/[#134](https://github.com/buster14a/buster/issues/134)/[#174](https://github.com/buster14a/buster/issues/174); integrate exact revisions, verify IDs/CFG/phi topology, share dominance rather than rebuild it, publish use lists and invalidation rules. |
| SSA → optimization → selection → selected-use liveness → intervals/allocation | [#138](https://github.com/buster14a/buster/issues/138), [#139](https://github.com/buster14a/buster/issues/139)/[#141](https://github.com/buster14a/buster/issues/141)/[#143](https://github.com/buster14a/buster/issues/143)/[#140](https://github.com/buster14a/buster/issues/140) and [#333](https://github.com/buster14a/buster/pull/333); define actual selected uses, CFG/phi edge uses, fixed/allowed registers, spill/rematerialization and analysis invalidation. Source-correlated compile/runtime and memory measurements remain required. |
| Allocation ↔ frame requirements → physical records → scheduler → sizing | [#140](https://github.com/buster14a/buster/issues/140)/[#177](https://github.com/buster14a/buster/issues/177)/[#172](https://github.com/buster14a/buster/issues/172); reserve frame registers before allocation, compute final frame after spills, carry source/branch/fixup remaps through a permutation, then size. Preallocation scheduling is a separate unimplemented contract. |
| Native records → source/CFI events → final-address debug/unwind | [#88](https://github.com/buster14a/buster/issues/88), [#329](https://github.com/buster14a/buster/pull/329); emitter must publish source and frame events with final PCs. An imported runtime FDE or synthetic DWARF fixture does not provide C function metadata. |
| Functions/units → lanes → complete executable; general semantic oracle | [#145](https://github.com/buster14a/buster/issues/145), [#151](https://github.com/buster14a/buster/issues/151)/[#152](https://github.com/buster14a/buster/issues/152)/[#153](https://github.com/buster14a/buster/issues/153)/[#176](https://github.com/buster14a/buster/issues/176)/[#142](https://github.com/buster14a/buster/issues/142) and [#331](https://github.com/buster14a/buster/pull/331); real source-to-executable execution, deterministic unit/function ordering and corpus/generated-program quality, not just harness self-tests. |

The active stage claims in those issues remain authoritative; this audit takes
none of their branches. The two focused new follow-ups are
[#335](https://github.com/buster14a/buster/issues/335) (PP/parser conversion)
and [#336](https://github.com/buster14a/buster/issues/336) (parser/checker binding
reuse). Their implementation is explicitly unclaimed and requires coordination
with the existing frontend owners. Both accept a measured retain/reject result,
not a compulsory new layout.

## Validation evidence and its limits

The exact audited main revision passed
[9700X run 39942816016](https://github.com/buster14a/buster/actions/runs/39942816016).
Its job log reports **48 commands passed, zero failed, zero not started**;
the call-graph gate and subsequent benchmarks also passed. The workflow at the
pin lists the exact commands, seeds and oracle flags. This audit inspected that
run and the corresponding source/tests; it did not rerun or change the compiler.

| Contract family | Existing executable checks at the pin |
| --- | --- |
| Raw/refined/per-file/PP | `test-lexer`, `test-refinement`, `test-files`, `test-preprocessor`, history/ordering/batch fixtures and the separate fuzz/oracle commands; scalar/reference output, guarded tails, macro/pragma/source identity and lane-count equality. |
| Parser and bindings | `test-parser --clang`, `test-bindings`; tree/postorder oracles, 64-byte alignments, deep nesting, recovery, allocation failure, scope/restore and guarded event arrays. A branch's new parser-binding corpus result does not qualify main's checker reuse. |
| Types, semantic actions, ABI | `test-types`, `test-checker`, `test-builtins`, `test-calling-convention --cross-check`; fresh-table scalar/vector comparisons, dependency partitions, failed publication, guarded slices, source/Clang semantics and generated callers/callees. |
| Backend | `test-tier0 --cross-check`, `test-dataflow`, `test-tier1-frame`, `test-tier1-frame-execution`, `test-tier1-schedule`, `test-x86-semantics`, `test-x86-target`, `test-x86-zen5`, sizing and encoder tests; independent semantic/byte oracles plus guard, count and exceptional paths. |
| Object/linker/other consumers | `test-assembler --cross-check`, `test-link`, `test-object-writer --cross-check`, `test-import`, `test-bind`, `test-snapshot`, executable/dynamic/static/foreign tests, `test-diagnostics` and `test-lanes`; byte, ELF, execution, failed-lane and per-stage schedule/ID-order checks. |

These checks validate the documented existing dispositions within their fixture
coverage. They do not prove all input programs correct or all interfaces optimal.
No new boundary is changed here, so no new interface-regression test is needed.
Future changes must add the missing crossing tests rather than merely rerun an
isolated kernel suite.

## Measurements and unresolved cost accounting

Two retained decisions have useful whole-path evidence: the syntax driver's
[tree-only comparison](c-parser.md#driver-measurements-2026-10-23), which includes
startup, preprocessing, parsing and cleanup on frozen inputs, and the
[length-only sizing qualification](measurements/x86-lengths-2026-10-23/refinement.md),
whose complete sizing/encoding controls are distinguished from the initial
isolated/cloud screens. These are historical comparisons at their recorded
revisions, not a new comparison of the audit commit.

The disassembler also has retained
[complete decode/format and command comparisons](x86-64-disassembler.md#complete-path-comparison),
including its actual record materialization and separate startup/storage
accounting. Those establish its stated side-consumer path, not source compilation.

The exact-main CI benchmarks are smoke/diagnostic observations. For example,
`benchmark-backend` reports 262,874 synthetic typed entries, 303,116 records and
9,414 deferred entries (3.58%), but times selection, sizing and encoding
separately. Adding those rows is not a measured whole handoff. The checker
benchmark in that run uses synthetic graphs. Neither result measures source
lowering, real ABI preparation, total allocation or a complete compiler.

| Cost not established by existing results | Blocker, owner and completion criterion |
| --- | --- |
| Read/lex/refine/directives/expansion + parser/checker, with conversion/replay memory | [#116](https://github.com/buster14a/buster/issues/116)/[#117](https://github.com/buster14a/buster/issues/117)/[#335](https://github.com/buster14a/buster/issues/335)/[#336](https://github.com/buster14a/buster/issues/336)/[#133](https://github.com/buster14a/buster/issues/133): active frontend qualification; freeze common small, header-heavy and SQLite inputs, include all preparation and cleanup, and compare whole-path totals. |
| Type/ABI/builtin work from actual checked functions, including ready-level occupancy | [#133](https://github.com/buster14a/buster/issues/133)/[#137](https://github.com/buster14a/buster/issues/137)/[#148](https://github.com/buster14a/buster/issues/148): missing complete function producer; count requested/used types, calls, arguments and exceptional actions, not all prebuilt synthetic table entries. |
| Full source → Tier 0 or SSA/Tier 1 → encoded/object/executable output | [#145](https://github.com/buster14a/buster/issues/145)/[#136](https://github.com/buster14a/buster/issues/136)/[#138](https://github.com/buster14a/buster/issues/138)/[#142](https://github.com/buster14a/buster/issues/142): missing source bridge and unpublished branches above. Include compile time, source/corpus execution and code size/runtime; preserve program quality. |
| Real selected-record/interval/frame/schedule profitability | [#141](https://github.com/buster14a/buster/issues/141)/[#140](https://github.com/buster14a/buster/issues/140)/[#177](https://github.com/buster14a/buster/issues/177)/[#172](https://github.com/buster14a/buster/issues/172)/[#181](https://github.com/buster14a/buster/issues/181): qualify the integrated producer and generated programs, including sorting, remapping, spill/reload traffic and unknown-cost fallback. |
| Source-generated link plus foreign/debug/streamed output | [#80](https://github.com/buster14a/buster/issues/80)/[#86](https://github.com/buster14a/buster/issues/86)/[#87](https://github.com/buster14a/buster/issues/87)/[#88](https://github.com/buster14a/buster/issues/88): existing generated-link oracles remain useful; source metadata, foreign corpus qualification and actual compilation/output overlap are unfinished. Charge remapping, snapshots, patch/hash/write, allocations, bytes and synchronization. |

For each follow-up use the existing `EXCLUSIVE_OWNER`, `EXCLUSIVE_TITLE` and
`EXCLUSIVE_LINK` protocol, acquiring `exclusive.sh` separately for every
9700X build/test/measurement and pinning single-thread timing to CPU 2. Compare
several rotated processes on frozen source/binary/toolchain identities, with
sustained vector work and representative lighter-phase conditioning. Existing
`--after-scalar` flags measure the named benchmark only; they do not silently
condition an enclosing pipeline. Record useful items, ns and cycles, peak live
and scratch bytes separately from reserved virtual space, allocations, bytes
copied/scanned, active occupancy and fallback. Mark unavailable counters or
metrics unavailable, not zero. Include empty/tiny and representative large cases.

The audit can be completed independently of those optimization projects because
each existing boundary has a documented current disposition and each missing
integration or measurement has a concrete open owner/issue boundary. Completion
of this inventory does not close any of those implementation issues or declare
the pipeline fully optimized.
