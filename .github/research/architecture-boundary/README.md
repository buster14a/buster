# Preparation ownership must survive rediscovery

## Decision and scope

**Keep Buster's overall architecture. Replace the duplicated special-builtin preparation policy with one private authority, and enforce that authority on replay as well as first discovery.** A helper extraction without the replay boundary is insufficient. A complete local repair also works; the structural alternative earns its place by removing coordinated policy edits, not by outperforming that local repair.

This is an experimental investigation extending [#181](https://github.com/buster14a/buster/issues/181), not a second issue tracker or production-acceptance claim. The original #180/#181 repair remains completed. The additional current-source preparation paths are not yet repaired on main.

Production source: commit `ade6ac4b6ecb21f30b61b656439bac476c145e2f`, tree `4c5306221fdb22fccc929b55e333163742de17d0`; `c_gen.c` blob `ede2de412850975123da3f4a473f017591655a75`. Main was re-read before publication and still matched. The exact tested candidate is [shared-owner-candidate.patch](./shared-owner-candidate.patch), Git blob `2d61fae73e5d68c0980d85f556a91996a5f8943f`, SHA256 `37adac468183d75188326cf619dda8cfcc398eab56f30d09c4589d1ed3bf15d1`. Applied to the production pin, including its registered test, it yields tree `4a5c0ea57ba7da08992ba8323d42fff0960402ae`, frontend blob `21469ba1e1e03225563cc3298d7bee7e2f99ac99`.

Only this session's research branch, `codex/architecture-boundary-scout-20260925-astra-ade6`, was written. Compiler prototypes were applied to separate detached hosted worktrees. No existing owner branch, generated binding, retirement/cutover/deletion, service, admission, deployment, protection or lane-system code was changed. No merge or production PR was performed.

## Scouting and ranking

The ranking is an investigation judgment, not a benchmark ranking. Current AGENTS and frontend, build, testing, workflow, semantic-validation, foundations and benchmarking guidance, relevant audit records, and matching all-state issues/PRs were inspected.

| Lead | Impact / reach | Evidence and falsification cost | Disposition |
|---|---|---|---|
| Split expression-preparation ownership | Wrong effects before canonical IR; shared by frontend forms and downstream backends | Current source asymmetry, repeated repair history, cheap harmless-counter witnesses | Highest priority; deepened through two falsified partial repairs |
| Emitted-to-canonical function identity | Repeated symbol matching for debug consumers | `debug_model_build` currently matches correct symbol identity; #987 already owns indexing/census | Independent alternative retained as existing work, not a new architecture migration |
| Scalar type/signature facts | Potentially broad target/semantic reach | Current `c_semantic_integer_count_parameter_kind` is already shared by parser/lowerer; other fixes involve distinct promotions and lifetimes | No demonstrated common invalidation/authority root; do not combine unlike facts |
| Token-view identity | Queries over synthesized versus original token ranges | #629/#1191's current original-token projection is already present | Already fixed, not a new finding |

The recent promoted-member audit is a useful negative precedent: an attractive synthetic bound did not establish material population-wide savings. Likewise, the counters below do not establish a compiler-wide bottleneck or speedup.

## The missing invariant

**An operand span has one preparation owner. Discovering that a call is already recorded does not transfer ownership of that operand to the current scanner.**

There are four uses of that fact: eager control preparation, fresh call discovery, already-recorded call discovery, and the initial selected-child preparation handoff. The current source encodes the first, second and fourth separately; the third can bypass them.

| Builtin family | Call discovery skips its operand | Control preparation skips it | Child selection owns preparation |
|---|---:|---:|---:|
| `_Generic`, `__builtin_choose_expr` | yes | yes | yes |
| `__builtin_object_size`, `__builtin_constant_p` | yes on first discovery | **no** | no |

Relevant pinned `src/buster/lib/compiler/frontend/c/c_gen.c` paths:

- `c_ir_lower_expression_core_step`, around 27527 and 27660–27690, starts control preparation before call preparation.
- `c_ir_prepare_control_expressions_step`, around 17171–17236, excludes selection builtins but not the two predicates. A nested assignment/comma or conditional group can therefore be lowered before predicate dispatch.
- `c_ir_prepare_calls_discover`, around 17976–18381, has a fresh-discovery owned-span jump at 18378 and a separately authored `.deferred_calls` initializer at 18339.
- Its earlier `c_ir_prepared_call_find(builder, callee_start)` rejection at 18272 continues past the call head without consuming the owned operand. Repeat discovery can descend into it even after the first mismatch is repaired.
- `c_ir_emit_prepared_call_step`, around 18841 onward, implements predicate results without evaluating their operands, but cannot retract already emitted calls/stores.

A representative tested expression is `__builtin_constant_p((hits = mark(), 7))`, where `mark` increments a volatile unsigned counter and returns 7. Observation is sequenced after the full expression. Baseline reports counter 7; the initial control-only repair reports 1; the complete boundary reports the required 0. The object-size form has the same three outcomes. Initializer, return and discarded contexts expose replay; the ordinary-argument context distinguishes a path which the partial fix already repairs.

The [GNU constant-p contract](https://gcc.gnu.org/onlinedocs/gcc/Other-Builtins.html) discards operand side effects. The [object-size contract](https://gcc.gnu.org/onlinedocs/gcc/Object-Size-Checking.html) also does not evaluate its operand. Object-size precision is a separate question: Clang can retain a precise size where GCC returns an unknown sentinel. Our common oracle asserts the absence of effects and records result values separately; it does not require those two reference compilers to choose identical precision.

## Why the history supports this cause

[#180/#181](https://github.com/buster14a/buster/pull/180) repaired direct predicate call discovery after a real fortified-macro destination was evaluated twice during self-hosting. Its accepted direct/comma controls still pass; the original repair is not being retroactively invalidated.

[#1097/#1101](https://github.com/buster14a/buster/pull/1101) needed changes to both eager preparers and the selected-child handoff for choose-expression. The actual patch changes these separate policy sites, rather than merely sharing terminology with this report.

[#1132/#1155](https://github.com/buster14a/buster/pull/1155) repaired the same two-prepass mismatch for fixed-size `sizeof`: call discovery skipped an operand while control preparation descended into a conditional index. Current source contains that fix and its shared unary-boundary predicate.

[#1131/#1152](https://github.com/buster14a/buster/pull/1152) is the essential migration counterexample: a saved declaration-time VLA size does not authorize dropping required effects of the original operand. The remaining operand type decides evaluation. Saved size, type classification and effects are different facts; the four-builtin policy must not replace that type-driven machinery. #1040/#1102's comma sequencing similarly concerns genuinely evaluated operands and remains separate.

## Executed comparison, including the losing prototypes

All compiler builds and executions ran on authorized standard GitHub-hosted Ubuntu 26.04 x86-64. The container only inspected source and artifacts. C compiler configuration/build went through `build.c`, with matched correctness flags in isolated roots. References were Clang 21.1.8 and GCC 15.2.0, both O0/O2. There was no desktop execution, ad hoc SSH, 9700X access, performance acceptance or wall-time/RSS comparison.

Each native row below summarizes eight profiles: frontend SSA/memory × NONE/MIR_STACK/FAST/QUALITY. The replay experiment and complete-local comparison explicitly reject machine fallback in the three MIR modes; NONE omits that inapplicable option. This is not an additional Buster O0/O2 matrix.

| Variant | Targeted evaluation failures, per profile | Compatibility controls, per profile |
|---|---:|---:|
| Unmodified production lowering | 56 / 60 | 56 / 56 pass |
| Add the two missing control exclusions | 6 / 60 | 56 / 56 pass |
| Share policy across first-discovery paths only | 6 / 60 | 56 / 56 pass |
| Shared authority including recorded-call replay | **0 / 60** | **56 / 56 pass** |
| Complete local repair, including replay but retaining independent lists | **0 / 60** | **56 / 56 pass** |

The 60 targeted cases cover 15 forms in initializer, ordinary argument, return and discarded contexts: assignments, conditional/short-circuit groups, nested expressions, nested calls, all four object-size modes, selected generic/choose expressions, and an ordinary evaluated sibling. The 56 controls include original direct-call predicates, both selection directions, generic controlling operands, fixed `sizeof`, required VLA effects, ordinary effects, comma ordering and runtime short circuiting.

The complete local and shared candidates produce **byte-identical objects for all three generated sources**: targeted cases, compatibility controls and handoff-derived cases. They also produce identical observed runtime rows in the compared profiles. Baseline and shared-candidate control objects are identical. Across the original targeted source, baseline and shared-candidate recorded result values are identical; the repaired observation is unwanted evaluation. These are bounded equivalence observations, not a universal program-equivalence proof.

The new registered `c_test_architecture_preparation_replay` directly inspects canonical calls and volatile stores for six functions × six layouts × two frontend forms. It requires no native backend to expose the defect. The baseline test-only tree fails **84 assertions**, all in this new family; the shared candidate passes **3,233,981/3,233,981 assertions and 53/53 modules**. The complete local candidate passes the same full suite. Before the new fixture was added, both baseline and the incomplete shared candidate passed the existing 3,225,250 assertions, illustrating the previous coverage gap.

The shared candidate also passes the two-generation self-host compile/SHA256 fixed-point subset. Its stage-1 and stage-2 binary hash is `633ccea6166b6ee7eb617f12e6fdb24742937337d586d1f3296d05635e99b2af`. This deliberately omits the ordinary combined target's benchmark steps and is not the complete self-host acceptance gate. Ten foreign object compilations pass, covering five foreign targets and both frontend forms; foreign execution and strict no-fallback coverage for those cross-object commands are not claimed.

Negative cases for an undeclared operand name, nonexistent member, and invalid object-size mode remain rejected. Each status and diagnostic is byte-identical between baseline and shared candidate. This protects those driver paths, not every possible malformed input or direct-lowering caller.

## Authority, lifetime and total cost

The authoritative identity already exists: `c_ir_token_builtin_kind` uses the existing `CSymbolBuiltin` identity. The prototype adds a pure, private projection to CALLER, SELECTION or UNEVALUATED preparation ownership. The producer is that projection, not a second symbol table. The four consumers derive their decisions from it.

Prepared records are created by the existing constructor and live in the per-function temporal lowering arena. The existing token-index lookup, `emitting` exclusion, saved open/close indices, prepared-control containment and continuation lifetimes are preserved. On replay the prototype skips an owned span only for a direct builtin whose opening token matches and whose close is inside the current scan. This matters because a callee-range query can otherwise find an enclosing call.

No owner field is stored in `CIrPreparedCall`, tokens, types or IR. There is no new arena allocation, index, cache generation, initialization walk, invalidation path or conversion. The additional state is transient query results and an existing-record pointer. Code size, switch lowering, instruction-cache behavior and total timing remain unmeasured. The C implementation adds 49 and removes 5 lines; that count is a cost description, not the argument for adoption.

Within the migrated paths, conflicting policy membership is no longer independently authored: all four decisions consume one authority. This is not a type-system proof for the entire expression machine. The default CALLER case still requires classification/coverage review when a new special builtin is introduced; production review should add a policy-coverage test so a new enumerator cannot silently escape review.

**Eager versus demand-driven:** an eager per-token evaluation plan would add retained state and validity requirements across token views, scopes, target-sensitive types, VLA evaluation and suspended frames. This experiment does not establish enough reuse to justify that representation. The demand-driven query needs only the existing builtin identity and reuses the existing bounded call span on replay. A static immutable owner table is also a valid implementation of this same authority; unlike a mutable cache, it has no invalidation problem. No measurement here distinguishes that table from the switch, so replacing one with the other is not part of the recommendation.

Diagnostic instrumentation observed control-preparation visits drop from **5,763 to 5,012**, and group candidates from **1,063 to 771**, across the two generated sources. All four instrumented objects matched their corresponding uninstrumented objects. This is 751 avoided visits and 292 avoided group considerations on deliberately targeted inputs, not a timing speedup. Replay traces show 45 baseline and 39 candidate predicate-record hits; all were emitted records with contained spans. The six problematic initializer/return/discard paths show recorded hits in both variants, confirming that the repair changes authority on a real replay path rather than eliminating lookup altogether. Counters did not measure total call-discovery visits or whole-compiler population.

## Competing designs and rejection reasons

**Status quo:** rejected for this slice because valid programs demonstrably execute forbidden effects.

**Control-only local repair:** rejected as incomplete, despite passing the old suite. Six targeted cases remain wrong.

**Helper-only refactor:** rejected as incomplete for the same reason. An interface alone did not establish an architectural boundary.

**Complete local repair:** a valid, tested alternative with the same observed outputs. It is the correctness-preserving fallback. It still leaves the ownership policy repeated across control, first discovery, replay and handoff. Do not claim the shared design removes more work than this local equivalent.

**Shared authority plus replay guard:** preferred narrowly because four roles become projections of one fact without new persistent state or lifetime obligations. This is the bounded architectural change justified by the experiment.

**New expression IR, eager plan or mutable cache:** rejected for this task. They add migration and invalidation costs not required by the demonstrated cause. No object framework, dependency or lane replacement is justified.

## Symbol-level migration and deletion map

| Symbol / site | Change | What can disappear | What must stay |
|---|---|---|---|
| `c_ir_token_builtin_kind` | No replacement | Nothing | Existing symbol/spelling identity |
| `c_ir_prepare_control_expressions_step` | Consume owner projection | Independent selection-only exclusion list | Range/diagnostic validation and real control lowering |
| `c_ir_prepare_calls_discover`, fresh path | Consume owner projection | Independent four-builtin skip list | Ordinary-call discovery and lazy/comma state |
| Same function, recorded path | Check owner and contained matching span | Head-only early continue for owned operands | `emitting` exclusion, partial-callee boundaries and ordinary-call traversal |
| `CIrPreparedCall.deferred_calls` constructor | Derive initial selection handoff | Separate Generic/Choose membership test | Field itself and later dynamic deferred state |
| `c_ir_prepared_call_suspend_preparation` / `request_expression` | No replacement | Nothing | Existing suspension and selected/lazy child ownership |
| `C_IR_UNEVALUATED_OPERAND_WORDS`, `c_ir_unevaluated_operand_end`, `c_ir_sizeof_vla_suffix_evaluated` | No replacement | Nothing | Type-driven VLA effects and saved-size semantics |
| `CIrLazyOperandScan`, prepared-control containment | No replacement | Nothing | Runtime short circuiting, comma ordering and already-emitted checks |

The disappearing coordination rule is “remember to edit every preparation list whenever a builtin gains special operand ownership.” No canonical representation, conversion, semantic check or lane system is deleted by this slice.

## Independent stages, compatibility and rollback

1. Review the failure-first canonical fixture and runtime witnesses independently, without merging a deliberately failing test-only prefix. Add production runtime registration and policy-coverage assertions through existing test infrastructure.
2. Review the four-role migration and replay bounds as one small semantic patch. Preserve builtin diagnostics, selected effects exactly once, lazy/comma sequencing and VLA positive controls. No exported API, persistent format, ABI or canonical-IR contract changes.
3. Obtain normal current-source sanitizer, non-unity, supported-platform and complete self-host correctness gates before treating it as production-ready. The hosted research suite is evidence, not a waiver of repository acceptance.

Rollback retains the tests and both semantic corrections, using the executed complete local implementation if the shared policy is rejected. Do not roll back to the control-only patch or original source while claiming correctness. No feature flag or long-lived API bridge is necessary. Exit criteria for temporary experimental wiring are: all four roles migrated, no independent ownership lists in those roles, coverage and ordinary gates passing, and diagnostic counters/research transport absent from the production delta. Preserve raw evidence before removing transport.

## Ownership handoff and remaining defects

The separate `research/meaning-seams-20260925-d9e7-astra` owner explicitly preserved this preparation scope. Its [handoff comment 5838907405](https://github.com/buster14a/buster/issues/181#issuecomment-5838907405) supplied nested logical, arithmetic and cast forms. Our derived 12-context fixture passes the no-extra-evaluation assertion in both complete candidates and all eight profiles. **Nine value-observing rows still return predicate 1 while Clang/GCC return 0.** Discarded contexts do not observe the predicate bit. Those separate value-folding defects are tracked by #1224/#1225; this prototype does not modify `c_ir_constant_apply_binary`, normalization or truth conversion and does not claim to repair them.

The actual model was GPT-6 Astra Pro. Tools were connected GitHub, source/artifact processing in a container and standard hosted Actions. No callable subagent executor was available, so the history, data-flow and alternative tracks were not independent agent reviews. The separate owner's handoff is external evidence, not a subagent fabricated for this report.

## Reproducibility and raw attempts

| Run | Artifact | Record |
|---|---:|---|
| [36183313870](https://github.com/buster14a/buster/actions/runs/36183313870) | 10885250901 | First oracle attempt; stopped before Buster build on an over-specific Clang object-size expectation; 13 manifest hashes verified |
| [36183459094](https://github.com/buster14a/buster/actions/runs/36183459094) | 10885178165 | Initial three-way comparison; six residual failures; 405 hashes verified |
| [36186095616](https://github.com/buster14a/buster/actions/runs/36186095616) | 10886941252 | Replay-aware candidate, failure-first canonical fixture, strict native profiles, diagnostics, suite, fixed point and counters; 412 hashes verified |
| [36187523981](https://github.com/buster14a/buster/actions/runs/36187523981) | 10886892675 | Complete-local comparison; all recorded commands pass and objects match, but wrapper falls through after its completed effective experiment and raises NameError; 350 hashes verified |
| [36188122984](https://github.com/buster14a/buster/actions/runs/36188122984) | See run | Wrapper-corrected repeat; disposition is recorded separately on #181, not assumed by this immutable report |

ZIP SHA256 in the same order for the first four: `df48849e2fa83cc7179d237e81ac53a759452b7ad0b7f91f4b4b814a385e5e02`, `3367c0e056acfaa138922be29e9b7e4f05b8e1f40107ac7b745a884ed0bcf841`, `99ac87be9c9b87a19ea875081a817513755e2e168109b8e26ff29e0e79b08fec`, `c9a81310034aff50473654ff3dfc661cf203d4f0aa652cb15f5f5e180ed556a4`. All were downloaded and checked. The first comparison's six invalid `-S -emit-llvm` requests remain raw setup failures; no LLVM-export evidence is claimed. Later canonical evidence comes from the registered direct-IR fixture instead.

The replay transport is commit `010c0e8e8d9860600b9acd29497d86e93b98e949`, tree `ed53c7882d16c3209379100f9f5564baf5ccb46d`. The first complete comparison transport is `10f6d87456d84571e730a5169e5590b101efc7f4`, tree `c702819f6903b4c495dcc38ce8f453ee9e51a4a7`. These are not production compiler commits. Its complete-local source tree is `5902f2265c4b890e622590f0ce9c8383aac71301`; the shared tree and patch are exactly identical to the replay experiment. Binaries are rebuilt per host and their individual hashes are retained, not assumed interchangeable.

Each archive contains sources, patches, result rows, exact argv, statuses, stdout/stderr, build configuration, identities and SHA256 manifests. Retention is 30 days, so preserve the portable evidence bundle or download the raw archives before expiration. The effective `experiment.py` in the replay artifact and `comparison-experiment.py` in the complete-comparison artifact are standalone replay programs. Run only inside an authorized disposable hosted full checkout of the exact production pin, with a fresh evidence directory and `ARCH_EVIDENCE` set; they deliberately create/stage experimental C worktrees. The small research source export is for inspection, not a full build checkout. The current branch workflow is the hosted entry point and runs the comparison generator once, avoiding the retained wrapper fallthrough.

No performance acceptance was attempted. The runnable experiment establishes correctness and structural work counts, not approval for a new physical workload, service change or 9700X campaign. The evidence justifies this small authority repair; it does not justify rewriting the frontend.
