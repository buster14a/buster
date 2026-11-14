# Provenance and third-party notices

Reviewed October 20, 2026 against all 30 tracked files at
[`c7eadb9989c48767f81c51a3b052a136bbb436ec`](https://github.com/buster14a/buster/tree/c7eadb9989c48767f81c51a3b052a136bbb436ec).
The earlier issue pin `d13038574531b064cc87b133bb97665d1728f60a` differs only
in `.github/workflows/9700x.yml`, `AGENTS.md` and `docs/coding-style.md`.
Those changes add the C-language rule, Markdown-only CI exclusion and stable
repository-ID check, with no new imported implementation. The latter is the
only change since the initial inventory at `76cab868a233fb81dac33933861a7b7d8d554649`.
This adoption adds legal texts, documentation
and source comments; it changes no executable statements.

The first-party grant and maintainer's historical-authority confirmation are
in [LICENSING.md](LICENSING.md). The inventory distinguishes provenance from
that confirmation. Existing upstream grants authorize the identified imports;
the maintainer's confirmation does not purport to grant anyone else's rights.

## Path and portion inventory

The production implementation-boundary changes keep existing material under
its recorded terms. `c_preprocessor_impl.c` and
`c_preprocessor_history_impl.c` contain definitions moved from
`c_preprocessor.c` and `c_preprocessor_history.c`; `executable_impl.c` contains
definitions moved from `executable.c`. The original definitions and their
provenance are retained. The generated predefined-macro arrays now live in
`c_preprocessor_impl.c`, while the generated feature tables remain in
`c_preprocessor.c`; their LLVM terms and generation inputs are recorded below.
The interface declarations, include guards, build-ownership checks and
documentation are original project material under the default terms. No new
upstream implementation or generated input is introduced by this reorganization.

The unknown-type comma-declaration recovery in `c_parser_impl.c`, its private
diagnostic view in `c_check_prefix.c`, and the original parser and semantic
driver regressions (#2640) are original project material under the default
terms. The `ParseDeclGroup` invalid-type gate and declaration recovery in
LLVM revision `7a941dfb2877a0b8cf30323fefa85ad019e0b0f5`'s
`clang/lib/Parse/ParseDecl.cpp` and `ParseStmt.cpp` were consulted for behavioral
facts. Clang is an external diagnostic oracle; no upstream implementation or
test fixture is imported.

The atomic compound-literal diagnostics and the narrow GNU statement-expression
empty-body recovery in `c_check_initializer_input.c`, `c_checker_driver.c` and
their semantic-driver regressions (#2441 item 5) are original project material
under the default terms. LLVM revision
`7a941dfb2877a0b8cf30323fefa85ad019e0b0f5`'s `clang/lib/Parse/ParseStmt.cpp`,
`clang/lib/Sema/SemaExpr.cpp`, `SemaStmt.cpp` and `SemaInit.cpp` were consulted
for the rejected-expression-statement, empty-body value and outer initialization
diagnostic behavior. Clang is an external diagnostic oracle; no upstream
implementation or test fixture is imported.

The command-line warning vocabulary, unknown-option diagnostic policy and
original CLI regressions in `c_preprocessor_warning_options.c`,
`c_driver_warning_options_test.c` and their integration (#1467) are original
project material under the default terms. LLVM revision
`7a941dfb2877a0b8cf30323fefa85ad019e0b0f5`'s `Warnings.cpp`, `Diagnostic.cpp`,
`DiagnosticIDs.cpp`, `DiagnosticFrontendKinds.td`, `Options.td`,
`CompilerInvocation.cpp` and the Clang driver supply behavioral facts;
Clang runs externally as the diagnostic oracle. The existing frozen warning
name catalog is reused. No upstream implementation or test fixture is imported.

The warning-group tree in `c_preprocessor_warning_options.c`
(`pp_warning_group_children` and `pp_warning_group_parents`, #1447) is factual
data generated from the output of `diagtool tree` of Ubuntu's clang 23.1.5
package (`diagtool-23`, LLVM `DiagnosticGroups.td`): each edge is a group and
a group listed directly above it, by the output's indentation, as indices into
the existing frozen warning-name catalog, leaving out the remark groups and the
bare `-W`. LLVM is licensed Apache-2.0 WITH LLVM-exception. No upstream
implementation or test fixture is imported; the diagnose_if group policy, its
code and its driver fixtures are original project material under the default
terms, with Clang 23.1.5 as the behavioral oracle.

The declaration-attribute completion work for #1447/#147 in
`c_check_attribute_expression.c`, `c_check_thread_safety_input.c`, their checker
and diagnostic integration, and the added attribute driver fixtures is original
project material under the default terms. Clang 23.1.4 is an external behavioral
oracle. The thread-safety handler rules and diagnostic wording were checked
against LLVM revision `7a941dfb2877a0b8cf30323fefa85ad019e0b0f5`,
`clang/lib/Sema/SemaDeclAttr.cpp`, `clang/lib/Sema/SemaDecl.cpp` and
`clang/include/clang/Basic/DiagnosticSemaKinds.td`; no upstream implementation
or test fixture is imported.

The GNU `noderef` source-type shadows, expression and initializer diagnostics and
`c_check_noderef_driver_test.c` fixtures (#1447) are original project material
under the default terms. Clang 23.1.4 is the behavioral oracle. Its
[attribute reference](https://clang.llvm.org/docs/AttributeReference.html#noderef)
and published `clang/lib/Sema/SemaType.cpp`, `SemaExpr.cpp`, `SemaExprMember.cpp`,
`SemaInit.cpp` and `clang/include/clang/AST/Expr.h` at LLVM revision
`7a941dfb2877a0b8cf30323fefa85ad019e0b0f5` were consulted for the placement,
type-sugar, conversion and source-location contracts on November 5, 2026; no
implementation or upstream test fixture is imported.

The positional mixed-input driver, per-lane assembly adapter and original
C/assembly/object/archive fixtures in `c_driver_inputs.c`, `c_driver_mixed.c`,
`c_driver_mixed_test.c` and their driver integration (#1120) are original project
material under the default terms. Existing project object, foreign-linker,
runtime-discovery and lane interfaces are reused. Clang and the system archiver
produce independent test inputs externally; no upstream implementation or
fixture corpus is imported.

The VECTOR/X86 typed adapter integration, sparse shared-interpreter SIMD storage,
`ir_simd_lower_test.c`, `ir_interpreter_simd_test.c` and the real-C
`tests/ir/vector-i32.c` fixture (#732) are original project material under the
default terms. They consume this repository's #717 producer, exact-form semantic
decoder and existing IR/action-model contracts. The independent lane expectations
are ordinary C arithmetic; no external implementation, fixtures or metadata are
imported. Native Tier 0 runs are a separate gated execution oracle.

`c_builtin_intrinsic_filter.c`, `c_builtin_intrinsic_filter_hash.h`,
`c_builtin_intrinsic_filter_test.c` and `tools/generate_builtin_intrinsic_filter.c`
(#893) are original project code under the default terms. The generated
`generated/clang_builtin_intrinsic_filter_23_1_1.inc` is a projection of the
existing reviewed Clang metadata's LLVM name facts and retains its
Apache-2.0 WITH LLVM-exception terms and provenance. No external implementation
is imported. Exact initial input and reproduction are recorded in
`docs/builtin-intrinsic-filter.md`; the underlying LLVM notices remain applicable.

The AMX value transport, per-unit instruction leaves and original oracle fixtures
in `c_builtin_amx_helpers.c`, `c_builtin_amx_leaves.inc`,
`c_check_program_amx_test.c` and their lowering/target integration (#126, #136,
#148) use the project default terms. Installed Clang 23 resource headers and
Clang 23's generated code supply interface, packed-layout and instruction/ABI
facts; Clang runs externally as the code-generation oracle. No upstream
implementation or header fixture is imported. The emitted leaves are original
project support code under Apache-2.0 WITH LLVM-exception.

The fortified libc builtin adapter and lowering in `c_builtin_fortify.c`,
`c_check_program_fortify.c`, their integration changes and original fixtures in
`c_check_fortify_driver_test.c` (#803) use the project default terms. The existing
Clang builtin metadata retains its recorded LLVM provenance; Clang and system
libc are external behavioral oracles. No implementation or header fixture is
imported. Scope and behavioral references are in `docs/c-builtins-fortify.md`.

`ir_rebuild.c` and the `ir_reduce*.c` test/tool family (#733) are original
project C under Apache-2.0 WITH LLVM-exception. They use this repository's
public IR, verifier, optimizer and interpreter interfaces; no external reducer,
compiler implementation or upstream test fixture was imported.

The canonical foreign-name permutation and its original C/assembly/property
fixtures in `foreign_order.c` and `foreign_order_test.c` (#734) use the project
default terms. The radix kernel follows this project's original
`c_bindings_sort` count/place/fill design. System C compilers, the ELF loader
and linkers are external test tools; no external implementation or corpus
fixture is incorporated.

The C11 atomic adapter (`c_check_builtin_atomic_c11.c`), object lowering
(`c_check_program_atomic.c`) and the added semantic, native and static-data
fixtures (#671) are original project material under the default terms. Exact
Clang/LLVM `llvmorg-23.1.1` Sema and code-generation sources supply behavioral
facts; Clang and system libatomic run externally as oracles/runtime consumers.
No external implementation, corpus fixture or generated data is incorporated.

`c_check_visibility_input.c`, `c_check_visibility_driver_test.c` and the
associated declaration/ELF integration (#620) are original project material
under the default terms. Clang is an external behavior and object-format oracle;
cJSON is an external compile qualification input. No upstream implementation or
fixtures are incorporated.

The implicit function binding/diagnostic additions and
`c_check_implicit_driver_test.c` (#623) are original project material under the
default terms. Clang and the unconfigured zlib 1.3.1 tree are external
qualification inputs; no external implementation or fixtures are imported.

The aggregate return control-flow walk, warning handling and original fixtures
in `c_check_program.c`, `c_check_program_test.c` and
`c_check_return_driver_test.c` (#670) use the project default terms. Clang is
an external behavior oracle. The dr_wav header used for compile qualification
remains an external input; no upstream implementation or fixtures are imported.

The system-header warning policy, retained pragma completion and `_Pragma`
diagnostic-source changes (#666/#662), including their original fixtures in
`c_preprocessor_system_warning_test.c` and `c_check_pragma_test.c`, use the
project default terms. LLVM/Clang 23.1.4 diagnostic metadata and engine sources
provide behavior facts, and Clang runs externally as an oracle. Miniz 3.0.2 is an
external compile/runtime corpus with its original license retained outside the
source tree; no implementation or corpus fixtures are copied into this project.

The diagnostic publication gate, command-line limit parsing and summary changes
in `c_preprocessor.c`, `c_preprocessor_impl.c`, `c_preprocessor_history.c`,
`c_preprocessor_history_impl.c` and `c_checker_driver.c`, with
`c_checker_driver_limit_test.c` (#669), are original project material under the
default terms. Clang/LLVM 23.1.4's diagnostic engine supplies behavior facts,
and Clang runs externally as an independent oracle. No external implementation
or fixture is copied.

The source-driver collection and independent-declaration diagnostic recovery
(#698, #699), `c_checker_driver_recovery_test.c`, and sparse warning completion
facts in `c_check_warning_events.c` and its model tests are original project
material under the default terms. They reuse this project's arenas, parser
forest, binding history, diagnostic gate and deferred type replay. Installed
Clang 23.1.4 supplies independent behavior and diagnostic-order observations;
no compiler implementation, external fixture or generated corpus is imported.

The deep aggregate-call setup, argument bound and extended generated/guarded
native fixtures in `tier0.c`, `tier0_generate.c`, `tier0_test.c`,
`tier0_validate_test.c` and `c_check_program_test.c` (#668/#663) are original
project material under the default terms. Clang is an external ABI oracle;
the Nuklear header used for compile qualification is an external input and is
not imported into this repository.

The large aggregate-copy lowering in `c_check_program_lower.c` and its focused
`c_check_program_copy_test.c` fixtures (#597) are original project material under
the default terms. They reuse the existing runtime-symbol, SysV ABI and typed-stream
interfaces. System libc and Clang run externally; no external implementation,
generated instruction data or corpus fixtures are incorporated.

The text IR parser and validation consumer (`ir_parse.c`, `ir_validate.c`), interpreter family (`interpreter*.c`,
`ir_interpreter*.c`), their test fixtures under `tests/ir/`, and
`docs/ir-tools.md` are original project material under the default terms.
They reuse this repository's IR, typed-stream, arena, Link and ABI contracts.
The SysV native-call bridge is original assembly written from the calling
convention; libc and host native test functions run externally. No external
implementation, generated data or corpus fixtures are incorporated.

Paths below are relative to the repository root. "Default" means
`Apache-2.0 WITH LLVM-exception` for original material.

| Paths or portions | Origin and disposition |
| --- | --- |
| `src/buster/compiler/calling_convention_builtins.c`, its focused tests in `calling_convention_test.c`, and `tools/generate_builtin_abi.c` | Original immutable projection of this project's builtin type and ABI classification rules, bounded initialization and independent scalar recomputation; Default. The initial input is project revision `e7f89091648f68940b595aadd648841c1ffc3ec5`, `c_types.c` and `calling_convention.c`. No external implementation or generated data is imported. The generator includes the existing `lane.c` module, which retains its MIT notice and grant. Reproduction and ownership limits are in `docs/calling-convention.md`. |
| `src/buster/compiler/c_check_clock.c`, `src/buster/compiler/compile_measure.c`, `docs/compile-time.md` and the `--time-stages` additions to `src/buster/compiler/c_checker_driver.c`, `src/buster/compiler/c_check_program_emit.c` and `src/buster/compiler/c_checker_driver_test.c` | Original opt-in stage clock, whole-process compile harness, driver tests and attribution notes (#518); Default. Reuses this repository's profiling helpers (`profile.c`) and the preprocessor's phase timers. tcc, perf and the frozen SQLite amalgamation run or are read externally; no external implementation or data is imported. |
| `src/buster/compiler/tier1_select.c`, `src/buster/compiler/tier1_select_test.c`, `src/buster/compiler/tier1_select_benchmark.c`, `src/buster/compiler/tier1_liveness.c`, `src/buster/compiler/tier1_liveness_test.c`, `src/buster/compiler/tier1_emit.c`, `src/buster/compiler/tier1_emit_test.c`, `src/buster/compiler/tier1_call_test.c`, `src/buster/compiler/tier1_narrow_test.c`, `src/buster/compiler/tier1_terminal_test.c`, `src/buster/compiler/tier1_pipeline_test.c`, `docs/tier1.md`, `docs/tier1-selection.md`, `docs/tier1-liveness.md`, `docs/tier1-pipeline.md` | Original selection, selected-use analysis, native fragment/call consumer, hostile native call helpers, guarded narrow-value and bounded terminal fixtures, typed-stream fixtures, independent Clang formulas, process supervisor and measurement helpers; Default. Existing project IR/SSA, dataflow, allocator, frame, encoder, target and model APIs are consumed under their retained provenance. Clang runs externally; no external implementation or corpus is imported. |
| `src/buster/compiler/tier1_address_coalesce_test.c` | Original #1217 allocated-address regression fixtures; Default. Reuses this project's selector, encoder and independent IR interpreter. No external implementation, fixture or timing data is imported. |
| `src/buster/compiler/tier1_model_measure.c`, `src/buster/compiler/tier1_model_measure_test.c`, `docs/tier1-model-measurements.md` | Original emitted-case inventory, register-normalized experiments and reader regression fixtures; Default. Uses the original project selector manifest, encoder/effects and Zen 5 counter helpers; introduces no third-party timing data or new model observations. |
| `src/buster/compiler/ir_optimize_inline.c`, `src/buster/compiler/ir_optimize_inline_test.c`, `docs/ir-inlining.md` | Original bounded translation-unit inlining, call-graph analysis and independent execution fixtures; Default. Tarjan's algorithm description is referenced; no external implementation or fixtures are copied. Shared IR and optimizer contracts retain their project provenance. |
| `src/buster/compiler/ir_optimize_sroa.c`, `src/buster/compiler/ir_optimize_sroa_test.c`, `src/buster/compiler/ir_optimize_passes_benchmark.c`, `docs/ir-scalar-replacement.md`, `docs/ir-optimization-measurements.md` | Original private-field replacement, independent fixtures and stage-cost harness; Default. The existing shared IR, SSA adapter and optimizer test representations are reused; no external implementation, fixture corpus or measurement dataset is imported. |
| `src/buster/compiler/ir_optimize.c`, `src/buster/compiler/ir_optimize_test.c`, `src/buster/compiler/ir_optimize_benchmark.c`, `src/buster/compiler/ir_optimize_gvn.c`, `src/buster/compiler/ir_optimize_gvn_test.c`, `src/buster/compiler/ir_optimize_licm.c`, `src/buster/compiler/ir_optimize_licm_test.c`, `docs/ir-optimization.md`, `docs/ir-value-numbering.md`, `docs/ir-loop-motion.md` | Original IR optimization passes, independent bounded interpreter and fixtures, scalar/vector comparisons and measurement helpers; Default. Wegman and Zadeck, Alpern/Wegman/Zadeck and LLVM loop descriptions provide algorithm definitions, not copied implementation or fixtures. The project IR, SSA and dominance modules are consumed under their retained provenance. |
| `src/buster/compiler/tier0_variadic_frame_test.c` and the optional incoming-frame additions to `src/buster/compiler/tier0.c` | Original bounded SysV variadic-frame layout, prologue and address-only local roles, with native ABI consumers, guarded fixtures and independent ordinary-code byte oracles; Default. System V AMD64 ABI and x86 instruction descriptions supply layout and instruction facts, not copied implementation. Existing project ABI, builtin recipes, branch sizing and encoder interfaces are reused. Clang compiles the original native callers externally as the approved host compiler. No external implementation or fixture corpus is imported. |
| `src/buster/compiler/c_check_builtin_bit_cast.c`, `src/buster/compiler/c_check_builtin_bit_cast_test.c` | Original bounded type-first bit-cast constraints, exact representation metadata and independent source/guarded/partition fixtures; Default. Exact Clang/LLVM 23.1.1 semantic-cast, expression/type and constant-evaluation sources supply behavior facts. Clang runs externally as an oracle; no external implementation, generated table or fixture corpus is copied. Existing type/checker interfaces retain their project provenance. |
| `src/buster/compiler/c_check_builtin_query.c`, `src/buster/compiler/c_check_builtin_query_test.c` | Original bounded source-proof/runtime-effect query actions and independent source, guarded and partition fixtures; Default. Exact Clang/LLVM 23.1.1 `ExprConstant.cpp` and `CGBuiltin.cpp` supply behavior facts for speculative evaluation, constant-context answers and emitted operand effects. Original approved-host floating-environment observations distinguish source proof from runtime evaluation. No external implementation or fixture corpus is copied. |
| `src/buster/compiler/c_lowering.c`, `src/buster/compiler/c_lowering_test.c`, `src/buster/compiler/c_lowering_benchmark.c` | Original selective-emission planner and independent graph/Clang fixtures from this repository's #325 at `5e8d24cc717ac405323c635978e5dbae2009373a`; Default. Imported unchanged for #133's semantic source and program consumers. Its original benchmark references are recorded in the source; no external implementation is incorporated. |
| `src/buster/compiler/c_check_builtin_atomic_rmw.c`, `src/buster/compiler/c_check_builtin_atomic_rmw_test.c`, `src/buster/compiler/c_builtin_atomic_rmw_helpers.c`, `src/buster/compiler/c_builtin_atomic_rmw_helpers_test.c` | Original scalar GNU/legacy atomic read/modify/write checking, baseline SysV helper records, bounded source/native/guarded fixtures and threaded consumer tests; Default. Exact Clang/LLVM 23.1.1 checking and code-generation sources, LLVM's atomic ordering description, System V ABI and x86 instruction facts guide behavior. Clang builtins serve as independent external and compiler-host oracles. Existing project type, checker, ABI, branch-sizing and encoder interfaces are reused. No external implementation, generated table or fixture corpus is imported or copied. Sources and support boundaries are recorded in `docs/c-builtins-integration.md`. |
| `src/buster/compiler/generated/clang_builtin_metadata_23_1_1.inc`, `src/buster/compiler/generated/clang_builtin_own_main_23_1_1.inc`, `docs/builtin-metadata/own-main.tsv`, `docs/builtin-metadata/own-main.names.txt` | Derived LLVM Project builtin metadata and identifier facts, `Apache-2.0 WITH LLVM-exception`, not first-party declarations or implementation. Extracted from the approved host's signed `clang 23.1.1-1` package using the original C tool below. Exact inputs, installed package provenance, source reference, tool/output digests and commands are retained in `docs/builtin-metadata/clang-23.1.1-inputs.json`; existing `LICENSES/LLVM.txt` is byte-identical to the referenced 23.1.1 license. |
| `tools/clang_builtin_metadata.c`, `src/buster/compiler/c_builtin_intrinsic_catalog.c`, `src/buster/compiler/c_builtin_intrinsic_catalog_test.c`, `src/buster/compiler/c_builtin_intrinsic_inventory_test.c` | Original bounded data extractor, exact-name scalar/SIMD lookup, inventory/decoder interoperability and independent guarded fixtures; Default. No LLVM implementation is copied. The included generated metadata retains its separate LLVM provenance and license above. |
| `src/buster/compiler/c_check_builtin_vector.c`, `src/buster/compiler/c_check_builtin_vector_test.c`, `src/buster/compiler/c_check_builtin_x86.c`, `src/buster/compiler/c_check_builtin_x86_test.c` | Original generic vector builtin relations, context-sensitive x86 operand constraints and independent fixtures; Default. Exact LLVM `llvmorg-23.1.1` Sema and AST definitions supply behavioral facts, names and operand bounds. The 185 x86 rule facts retain their source locations; no LLVM implementation or fixtures are copied. External Clang calls are behavior/diagnostic oracles. |
| `src/buster/compiler/c_builtin_intrinsic_types.c`, `src/buster/compiler/c_builtin_intrinsic_types_test.c`, `src/buster/compiler/c_builtin_intrinsic_features.c`, `src/buster/compiler/c_builtin_intrinsic_features_test.c` | Original bounded Clang descriptor decoding, target-feature expression evaluation and independent guarded/type/capacity fixtures; Default. Exact LLVM `llvmorg-23.1.1` behavior definitions supply grammar and target-model facts; no LLVM implementation or fixtures are copied. Successful decoding/evaluation grants no source or runtime capability. |
| `src/buster/compiler/c_check_builtin_math.c`, `src/buster/compiler/c_check_builtin_math_test.c`, `src/buster/compiler/c_builtin_math_helpers.c`, `src/buster/compiler/c_builtin_math_helpers_test.c` | Original corpus-driven math checking, raw-bit baseline leaf recipes and independent source/native/environment fixtures; Default. Exact Clang/LLVM 23.1.1, IEEE and Intel instruction/environment descriptions supply behavior facts; glibc's square-root wrapper was consulted only for errno sequencing. Clang and host libm are external oracles. No external implementation or fixture corpus is imported. |
| `src/buster/compiler/c_builtin_float_lower.c`, `src/buster/compiler/c_builtin_float_lower_test.c`, `src/buster/compiler/c_builtin_atomic_helpers.c`, `src/buster/compiler/c_builtin_atomic_helpers_test.c`, `src/buster/compiler/c_builtin_terminal_helpers.c`, `src/buster/compiler/c_builtin_terminal_helpers_test.c` | Original raw-representation floating recipes, baseline scalar atomic/fence and terminal helper bodies, guarded/native tests and bounded thread-handoff fixtures; Default. IEEE representation, System V ABI and x86 instruction/order facts guide the implementation. Existing project records, policy and encoder interfaces are reused; Clang supplies independent numeric/atomic behavior. No external implementation or fixtures are imported. |
| `src/buster/compiler/c_builtin_misc_lower.c`, `src/buster/compiler/c_builtin_misc_lower_test.c` | Original memory-loop and hint expansion through the existing typed stream, transactional publication and guarded native fixtures; Default. Clang/libc are external behavior oracles. No external implementation or fixtures are imported. |
| `src/buster/compiler/c_builtin_catalog.c`, `src/buster/compiler/c_builtin_varargs_lower.c`, `src/buster/compiler/c_builtin_varargs_lower_test.c` | Original shared builtin identities/capability metadata, SysV va_list typed-stream expansion and guarded/native fixtures; Default. The psABI and existing project ABI tables supply layout facts. Clang supplies independent varargs state and execution results; no external compiler implementation or fixtures are imported. |
| `src/buster/compiler/c_builtin_lower.c`, `src/buster/compiler/c_builtin_lower_test.c`, `src/buster/compiler/c_builtin_lower_benchmark.c` | Original integer-builtin expansion into the existing typed stream, scalar/vector publication, bounded-capacity checks, native execution fixtures and whole-recipe benchmark; Default. The bootstrap Clang compiler is an external semantic oracle. No Clang implementation or external fixtures are imported. |
| `src/buster/compiler/differential.c`, `src/buster/compiler/differential_test.c`, `tests/differential/tool.c`, `tests/differential/interesting.c`, `tests/differential/lifecycle.c`, `docs/differential.md` | Original external-generator compiler comparison harness, fake protocol fixtures, reducer adapter, CLI lifetime regressions and documentation; Default. Csmith and YARPGen run externally under their retained upstream terms; no generator implementation, runtime or generated corpus is imported. Generated artifacts stay outside the repository and require their own provenance/license review before retention. |
| `src/buster/compiler/ir_metamorphic.c`, `src/buster/compiler/ir_metamorphic_test.c` and their section of `docs/ir-tools.md` | Original bounded deterministic IR generator, representation transformations, independent unsigned arithmetic/memory models, consumer comparisons and replay/lifecycle controls; Default. Existing project IR, parser, verifier, optimizer and interpreter interfaces are reused under their retained provenance. No external implementation or fixture corpus is imported. |
| `src/buster/compiler/dominators.c`, `src/buster/compiler/dominators_test.c`, `src/buster/compiler/dominators_benchmark.c`, `docs/dominators.md` | Original entry-rooted dominance, frontier and natural-loop analyses, independent graph oracles, synthetic fixtures, measurement helpers and documentation; Default. Cooper, Harvey and Kennedy's paper and the linked Cornell and LLVM descriptions supply algorithm definitions, not copied code or fixtures. No external implementation or generated dataset is imported. |
| `src/buster/compiler/tier1_allocate.c`, `src/buster/compiler/tier1_rematerialize.c`, `src/buster/compiler/tier1_allocate_test.c`, `src/buster/compiler/tier1_moves_test.c`, `src/buster/compiler/tier1_allocate_benchmark.c`, `docs/tier1-allocation.md` | Original interval allocation/coloring, reload splitting, parallel move resolution, rematerialization, native fixtures and measurement helpers; Default. Uses the existing encoder and arena interfaces; no other compiler implementation, benchmark corpus or generated dataset is imported. |
| `src/buster/compiler/tier1_frame.c`, `src/buster/compiler/tier1_frame_test.c`, `src/buster/compiler/tier1_frame_benchmark.c`, `src/buster/compiler/tier1_frame_emit.c`, `src/buster/compiler/tier1_frame_execution_test.c`, `docs/tier1-frame.md` | Original interval slot-coloring, frame and prologue/epilogue planning, native instruction consumer, independent layout/prologue simulations, native ABI/probe fixtures and measurement helpers; Default. Existing calling-convention data and SysV architectural rules are consumed without importing another compiler implementation or external fixtures. |
| `src/buster/compiler/tier1_schedule.c`, `src/buster/compiler/tier1_schedule_test.c`, `src/buster/compiler/tier1_schedule_benchmark.c`, `src/buster/compiler/c_check_tier1_schedule.c`, `src/buster/compiler/tier1_schedule_ir.c`, `docs/tier1-scheduling.md` | Original bounded machine-record scheduler, the driver's block splitting and the preallocation IR list scheduler, independent ordering verifiers, fixtures, generated integer loop, measurement helpers and documentation; Default. Uses this repository's encoder, resolved effects and original Zen 5 observation table; the priority weights of forms that table hasn't measured are this repository's choices. No third-party implementation, fixture corpus or timing dataset is imported. |
| `src/buster/compiler/c_lexer.c` | Adapted/generated Accelerated-Zig-Parser lexer, with simdjson, Clang and identifier-data provenance described below; component terms, not the first-party default over the whole file. |
| `src/buster/compiler/c_lexer_test.c`, `src/buster/compiler/c_lexer_benchmark.c` | Ports of upstream `c/c_tokenizer_test.c`; MIT, including local changes. |
| `src/buster/compiler/lane.c` | Adapted RAD Debugger interface/base-layer patterns with a locally implemented barrier, scheduling and allocation; MIT, including local changes. |
| `src/buster/compiler/x86_64_encoder_benchmark.c`, `src/buster/compiler/x86_64_disassembler_benchmark.c`, `src/buster/compiler/object_writer_benchmark.c`, `src/buster/compiler/lane_benchmark.c` | Original benchmark bodies under Default; shared counter/timing/sort helpers adapted through the lexer benchmark retain MIT. |
| `src/buster/compiler/x86_64_encoder.c`, `src/buster/compiler/x86_64_encoder_test.c` | Original encoder, handwritten form families and tests; Default. Tool-oracle and catalogue distinctions below. |
| `src/buster/compiler/branch_sizing.c`, `src/buster/compiler/branch_sizing_benchmark.c`, [measurement evidence](https://github.com/buster14a/buster-performance/tree/a00d16e47b6f1090b8a0424ab0dd8cb4e6016713/docs/measurements/x86-lengths-2026-10-23/) | Original branch-sizing integration, lengths-only SIMD implementation, scalar reference, tests, timing and scaling harnesses, and generated observations for #305; Default. Existing repository generators supply the inputs. AMD's Zen 5 guide supplies architectural guidance, not copied implementation. Clang, GNU as and Linux perf run externally; no tool implementation or external fixture corpus is imported. The measurement record identifies source revisions, compiler/CPU identities, exact commands, hashes and qualification limits. |
| `src/buster/compiler/x86_semantics.c`, `src/buster/compiler/x86_semantics_test.c`, `src/buster/compiler/x86_semantics_benchmark.c`, `docs/x86-semantics.md` | Original form-effects tables, immediate contracts, form choice, tests, benchmark and documentation; Default. Intel and AMD manuals supply architectural facts, not copied implementation. The existing encoder catalogue and branch-sizing generator are reused; original fixtures execute on the processor, and GNU as runs externally as a byte oracle. The benchmark's measurement helpers are original to the module. No external implementation or fixture corpus is imported. |
| `src/buster/compiler/assembler.c`, `src/buster/compiler/assembler_words.c`, `src/buster/compiler/assembler_test.c`, `src/buster/compiler/assembler_benchmark.c`, `src/buster/compiler/x86_match.c`, `docs/assembler.md` | Original assembly frontend, register classifier, matcher, tests, benchmark and documentation; Default. Encoder form facts and the project's validation and ELF interfaces are reused. GNU as and Clang run externally on original fixtures; their implementation is not imported. uops.info supplies measured instruction costs. The digit pair/quartet multiply-add reduction is implemented from the arithmetic, without copied source. |
| `src/buster/compiler/assembler_sections.c`, `src/buster/compiler/assembler_cfi.c`, `src/buster/compiler/assembler_lines.c`, `src/buster/compiler/assembler_compat_test.c`, `src/buster/compiler/assembler_corpus_test.c`, `src/buster/compiler/assembler_programs_test.c`, `examples/assembler/compiler-corpus.c`, `examples/assembler/compiler-caller.c`, `examples/assembler/program-floating.c`, `examples/assembler/program-storage.c`, `examples/assembler/program-strings.c`, `examples/assembler/program-switch.c`, `examples/assembler/program-gather.c` | Original GNU assembly section/CFI adapters, differential fixtures and independent executable/unwind corpus, and #1115's original whole-program corpus, whose assembly GCC and Clang generate at test time (GCC 13.3 and Clang 23.1.5 when it was written) and whose objects GNU as's are compared with; Default. GNU as documentation and observed output supply syntax/encoding behavior; ELF and DWARF describe format facts. GCC 16.2.1 and Clang 23.1.1 generate assembly at test time from the original C corpus, and GNU as runs externally as its object oracle. No external implementation or fixture corpus is incorporated. #1115's line table (`assembler_lines.c`: `.file N`, `.loc` and view numbers) follows GNU as's behavior, read from binutils 2.42's and 2.47's `gas/dwarf2dbg.c` and `gas/as.c` (GPL-3.0-or-later, read for behavior only) and checked against GNU as's objects; none of their code or text is incorporated. Its `.debug_frame` (`assembler_cfi.c`, `.cfi_sections`) likewise follows GNU as's behavior, read from binutils 2.42's and 2.47's `gas/dw2gencfi.c` (GPL-3.0-or-later, read for behavior only) and checked against GNU as's objects, with none of its code or text incorporated. The recovered syntax/corpus work is this same repository's original stopped #233 attempt; its source and recovery boundaries are recorded with the qualification evidence. |
| `src/buster/compiler/x86_zen5.c`, `src/buster/compiler/x86_zen5_measure.c`, `src/buster/compiler/x86_zen5_test.c`, `docs/x86-zen5.md`, [measurement evidence](https://github.com/buster14a/buster-performance/tree/a00d16e47b6f1090b8a0424ab0dd8cb4e6016713/docs/measurements/x86-zen5-2026-10-22/) | Original partial instruction-cost model, C microbenchmark generator, tests and hardware observations; Default. Inputs are this repository's encoder forms and original operand fixtures executed on the owner's Ryzen 7 9700X. No external timing dataset or benchmark implementation is imported. Clang and Linux perf are external tools; PMCx0C1 is the architectural retired-macro-op event, not executed micro-ops. The guide records the generation procedure and evidence. |
| `src/buster/compiler/x86_target.c`, `src/buster/compiler/x86_target_forms.c`, `src/buster/compiler/x86_target_test.c`, `docs/x86-target.md` | Original target policy, form requirement rows, tests, benchmark and documentation; Default. CPUID fields and psABI levels are architectural facts. Macro/profile and builtin-query data were checked against Clang 23.1.3; the live tests rerun Clang as an external oracle. LLVM's target parser, target macro and host-detection sources were consulted for behavior, not copied. The existing encoder's original form-family macros are reused. Sources and reproduction are recorded in `docs/x86-target.md`. |
| `src/buster/compiler/x86_64_disassembler.c`, `src/buster/compiler/x86_64_disassembly.c`, `src/buster/compiler/x86_64_disassembler_cli.c`, `src/buster/compiler/x86_64_disassembler_test.c`, `examples/disassembler/corpus.c`, `docs/x86-64-disassembler.md` | Original decoder, bounded formatter/command, tests, C corpus and documentation; Default. Decoder indices are built from this repository's encoder catalogue, and generated test operands use its existing generator. Independent raw-byte fixtures are handwritten. GNU as/objdump and Clang run externally as oracles for locally authored inputs; no implementation or external test corpus is imported. uops.info and ISA manuals supply design measurements and instruction facts, not copied implementation. |
| `src/buster/compiler/main.c`, `src/buster/compiler/main_test.c`, `src/buster/compiler/lane_test.c` | Original integration, input handling, loader regressions and lane tests; Default. The loader fixtures and address-space-limit checks are written for this project; no external fixtures or implementation are imported. |
| `src/buster/compiler/diagnostic.c`, `src/buster/compiler/diagnostic_test.c`, `src/buster/compiler/diagnostic_benchmark.c`, `src/buster/compiler/c_preprocessor_order_test.c`, `src/buster/compiler/link_order_test.c`, `docs/determinism.md` | Original buffered diagnostic ordering, scalar/vector kernels, tests, measurement harness and documentation; Default. The property tests reuse this repository's fixtures and generators. The diagnostic benchmark's clock, sample selection and conditioning helpers are original; no external implementation, fixtures or generated data are imported. |
| `src/buster/compiler/dataflow.c`, `src/buster/compiler/dataflow_program.c`, `src/buster/compiler/dataflow_test.c`, `src/buster/compiler/dataflow_benchmark.c`, `docs/dataflow.md` | Original liveness/interference kernels, synthetic CFG generator, tests, benchmark and documentation; Default, with the public-domain SplitMix64 seed-mixing reference below. No external corpus is incorporated. |
| `src/buster/compiler/ssa.c`, `src/buster/compiler/ssa_program.c`, `src/buster/compiler/ssa_test.c`, `src/buster/compiler/ssa_benchmark.c`, `src/buster/compiler/ssa_ir.c`, `src/buster/compiler/ssa_ir_test.c`, `docs/ssa.md`, [measurement evidence](https://github.com/buster14a/buster-performance/tree/a00d16e47b6f1090b8a0424ab0dd8cb4e6016713/docs/measurements/ssa-2026-10-23/) | Original local-promotion implementation, IR adapter, CFG generator, interpreters, verifier, tests, benchmark and generated observations; Default. Braun et al. (CC 2013), Cytron et al. (TOPLAS 1991) and Cooper, Harvey and Kennedy (2001) supply algorithm descriptions, not copied code or fixtures. The generator is an original deterministic congruential generator; measurement helpers are original. Existing arena/dataflow/dominator/IR APIs, the typed-stream generator and test arithmetic helpers are reused. Sources and integration limits are in `docs/ssa.md`; the measurement record names its source revisions, compiler/CPU, commands and workflow. |
| `src/buster/compiler/ir.c`, `src/buster/compiler/ir_lower.c`, `src/buster/compiler/ir_test.c`, `src/buster/compiler/ir_lower_test.c`, `src/buster/compiler/ir_benchmark.c`, `src/buster/compiler/ir_lower_benchmark.c`, `docs/ir.md` | Original per-function IR, typed-stream adapter, scalar/vector verifiers, compact CFG/use builders, printer, fixtures, test-only interpreter, benchmarks and documentation; Default. Tests and pipeline measurements reuse this project's Tier 0 generator and typed-stream interpreter. The timing and median helpers are original to this module. No external implementation, generated data or fixture corpus is imported. |
| `src/buster/compiler/preprocessor_fuzz*.c`, `docs/preprocessor-fuzz.md` | Original independent test harness, generator, comparator, minimizer, tests and benchmark helpers; Default. Generated test programs are produced by this repository's C generator from a recorded seed, case ID and language mode. Clang 23 is an external oracle; no compiler implementation or external test fixtures are imported. |
| `src/buster/compiler/call_graph.c`, `src/buster/compiler/call_graph_test.c`, `src/buster/compiler/call_graph_capture/*` | Original call graph check, tests and IR-capture fixture; Default. LLVM IR's text format and Tarjan's algorithm are referenced, not copied. The tests' IR is hand-written, or clang 23's output for C snippets written for them and recorded beside it, cut down by hand; clang runs externally and is not vendored. |
| `src/buster/compiler/link.c`, `src/buster/compiler/link_program.c`, `src/buster/compiler/link_test.c` | Original representation, synthetic program generators and tests; Default, with the public-domain generator reference noted below. |
| `src/buster/compiler/object_writer.c`, `src/buster/compiler/object_writer_test.c` | Original ELF writer and tests; Default. ELF constants describe the format; no external linker implementation is vendored. |
| `src/buster/compiler/debug.c`, `src/buster/compiler/debug_test.c`, `src/buster/compiler/debug_executable_test.c`, `src/buster/compiler/debug_benchmark.c`, `docs/debug.md` | Original DWARF 5 emitter, guarded scalar/vector checks, native executable attachment fixtures, benchmark and documentation; Default. DWARF 5 supplies format facts. Linux perf's module-base calculation was consulted to diagnose native unwind compatibility; no implementation was copied. GNU as supplies code/CFI for original fixture sources, and readelf, GNU ld, GDB and perf run externally as consumers. |
| `src/buster/compiler/executable.c`, `src/buster/compiler/executable_impl.c`, `src/buster/compiler/executable_test.c`, `src/buster/compiler/executable_symbols_test.c`, `src/buster/compiler/executable_benchmark.c`, `docs/executable-symbols.md` | Original writer of static executables (with the C runtime too) and dynamic PIEs, its vector passes and exact twins, optional regular-symbol output, and its tests and benchmarks; Default, with the generator reference below. Shared types, hash helpers and readers remain in `executable.c`; private layout, patching and output definitions are in `executable_impl.c`. The ELF dynamic sections, symbol tables, RELR, `.gnu.hash` and IRELATIVE relocations are written from their specifications, the general- and local-dynamic TLS accesses' relaxation from the x86-64 TLS ABI's sequences, and the layout, tags, encodings, stubs and linker-defined names from lld's and GNU ld's documented behavior and observed output, not copied. |
| `src/buster/compiler/unwind.c`, `src/buster/compiler/unwind_tier0.c`, `src/buster/compiler/unwind_test.c`, `src/buster/compiler/unwind_tier0_test.c`, `src/buster/compiler/unwind_native_test.c`, `src/buster/compiler/unwind_benchmark.c`, `docs/unwind.md` | Original fixed-frame Tier 0 record consumer, CIE/FDE templates and scalar emitter, independent frame-state oracles, guarded fixtures, backend-chain measurement helper and documentation; Default. DWARF 5 and LSB exception-frame descriptions supply format facts, not implementation code. Existing project encoder, resolved effects, Tier 0 generator and Link/debug interfaces are reused. GNU as supplies an ELF container and the original process-entry wrapper's code/CFI; normal functions and their CFI are emitted natively. GDB and perf run externally as consumers. No external implementation or fixture corpus is imported. |
| `src/buster/compiler/dynamic.c`, `src/buster/compiler/dynamic_test.c`, `src/buster/compiler/dynamic_libraries_test.c` | Original inputs of a dynamic PIE from the C runtime and binding, and the dynamic PIE's tests; Default. The program interpreter's path is the one GCC's driver passes for x86-64 glibc, a fact, not copied code. The tests make their programs themselves and run them under the system's loader; `ld.lld`, `cc` (which builds the preloaded library whose `write` walks the stack with libgcc's unwinder), `objdump` and `readelf` are external oracles for `--cross-check`, not vendored. |
| `src/buster/compiler/static_test.c` | Original tests of the static executables with the C runtime; Default. The programs it compiles and assembles are its own, and the general- and local-dynamic sequences and their local-exec forms are the x86-64 TLS ABI's, as GNU ld and lld relax them, a fact, not copied code. The tests read the system's static runtime at run time without copying it; `cc` (with GNU ld, and with lld for `--cross-check`) is an external oracle, not vendored. |
| `src/buster/compiler/snapshot.c`, `src/buster/compiler/snapshot_test.c`, `src/buster/compiler/snapshot_benchmark.c` | Original runtime snapshot (its file, writer, loader and validation) and its tests and benchmark; Default. The cache directory follows the XDG base directory specification, a specification, not copied code. The tests and the benchmark read the system's C runtime at run time without copying it. The benchmark's clock and median helpers are its own. |
| `src/buster/compiler/link_benchmark.c`, `src/buster/compiler/floors_benchmark.c` | Original link and floors benchmarks; Default. They take over the harness and the floor programs this project wrote on the 9700X for the design's measurements, which the repository doesn't track. `ld.bfd`, `ld.lld`, `mold` and `wild` are external programs the link benchmark runs, not vendored; their command line is the clang driver's, as the design's appendix records it. The clock, median, spawn and parsing helpers are their own. |
| `src/buster/compiler/import.c`, `src/buster/compiler/runtime.c`, `src/buster/compiler/import_test.c`, `src/buster/compiler/import_benchmark.c` | Original readers of ELF relocatable objects, archives, shared libraries and linker scripts, their vector and exact paths, the C runtime's search and import, and their tests and benchmark; Default. The formats are read from their specifications. The archive header's vector size parse uses the widely published multiply-add method for decimal digits (as in simdjson's and Wojciech Muła's write-ups), written here from the method, not copied. The tests and the benchmark make their inputs themselves, and read the system's C runtime files and the libraries and archives named on the command line at run time without copying them; `readelf` is an external oracle for `--cross-check`, not vendored. The benchmark's clock, counter and median helpers are its own, not the lexer benchmark's MIT ones. |
| `src/buster/compiler/foreign_stack_test.c` | Original executable-stack metadata and selected-input refusal tests; Default. The assembly sources and archive/COMDAT selection cases are original. GNU as and ar run externally to create the test objects and archives; no external fixture source or linker implementation is copied. ELF section names and flags describe the format. These tests do not claim an external-linker oracle for the conservative refusal policy. |
| `src/buster/compiler/foreign_unwind.c`, `src/buster/compiler/foreign_unwind_link.c`, `src/buster/compiler/foreign_unwind_test.c`, `src/buster/compiler/foreign_unwind_runtime_test.c`, `src/buster/compiler/foreign_unwind_benchmark.c` | Original COMDAT unwind pruning, interval mapping, foreign conversion adapter, tests and synthetic phase benchmark; Default. ELF/DWARF encodings describe the formats, not copied implementations. The C++ sources and synthetic record/relocation oracles are original fixtures. GCC, GNU ld and lld run externally; system C++/C runtimes are read and linked at test time, not vendored. Timing, statistics and fixture helpers are original. |
| `src/buster/compiler/foreign_gc.c`, `src/buster/compiler/foreign_gc_link.c`, `src/buster/compiler/foreign_gc_test.c`, `src/buster/compiler/foreign_gc_link_test.c`, `src/buster/compiler/foreign_gc_benchmark.c` | Original stable-map section reachability, foreign FDE/CIE graph and compact-link adapter, independent closure oracles, original ELF/assembly fixtures and whole-link benchmark; Default. No implementation is copied from another linker. ELF/DWARF ownership and encoding rules describe the formats; GNU ld, lld and GNU as are external test oracles, not vendored. Timing, statistics and fixture helpers are original. |
| `src/buster/compiler/foreign_unwind_dedup.c`, `src/buster/compiler/foreign_unwind_dedup_link.c`, `src/buster/compiler/foreign_unwind_dedup_test.c`, `src/buster/compiler/foreign_unwind_dedup_link_test.c`, `src/buster/compiler/foreign_unwind_dedup_benchmark.c` | Original terminal CIE pooling planner, transactional foreign adapter, record/fixup identity checks, ELF/CFI oracles and whole-link benchmark; Default. Tests reuse this project's original C++ exception/cleanup fixture and invoke GNU ld/lld externally. No linker implementation or external test fixture is copied. Conditioning and timing helpers are original project code. |
| `src/buster/compiler/bind.c`, `src/buster/compiler/bind_test.c`, `src/buster/compiler/bind_benchmark.c` | Original binding of the C runtime into a program's link, its vector and exact paths, and its tests and benchmark; Default. GNU ld's and lld's symbol resolution are described from their documented behavior and observed output, not copied. The tests make their programs themselves and read the system's C runtime at run time without copying it; `ld.lld`, `ld.bfd`, `cc` and `readelf` are external oracles for `--cross-check`, not vendored. The benchmark's clock, counter and median helpers are its own. |
| `src/buster/compiler/c_intern.c`, `src/buster/compiler/c_refine.c`, `src/buster/compiler/c_refine_test.c` | Original intern table, token refinement and tests; Default. The hash's constants are wyhash's public-domain ones, and the keyword list is clang-generated data; both are noted below. |
| `src/buster/compiler/c_preprocessor.c`, `src/buster/compiler/c_preprocessor_impl.c` | Original preprocessor; Default, with the clang-generated predefined macros in `c_preprocessor_impl.c` and feature tables in `c_preprocessor.c` noted below. Shared state, declarations and readers remain in the interface; private bodies and tables retain their original definitions in the implementation. The `__is_target_*` queries follow clang 23's `clang/lib/Lex/PPMacroExpansion.cpp` and LLVM 23's `llvm/lib/TargetParser/Triple.cpp` and `TripleName.def` (how a name is compared and which names an architecture, operating system or environment parses from), read for their rules and written again in C. |
| `tools/preprocessor_pragma_oracle.cpp`, `tools/build-preprocessor-pragma-oracle.sh` | Original external test-oracle adapter and build glue; Default. The adapter uses the installed Clang 23 and LLVM frontend APIs to observe expanded OpenMP operands and unexpanded unknown pragma tokens. Its C++ is required by those APIs. No Clang implementation or headers are vendored; the installed libraries remain external oracle dependencies. The pragma expansion rule was checked against LLVM's `llvmorg-23.1.3` `clang/lib/Frontend/PrintPreprocessedOutput.cpp`, then implemented through the API. |
| `src/buster/compiler/c_predefines.c` | Original predefined macros of the target and the options; Default, with the clang-generated tables noted below. The options' rules follow clang 23's driver (`clang/lib/Driver/ToolChains/Clang.cpp`, `CommonArgs.cpp`, `Arch/X86.cpp`, `SanitizerArgs.cpp`, `ToolChain.cpp`, `ToolChains/Linux.cpp`, `clang/lib/Options/OptionUtils.cpp`, and `clang/include/clang/Options/Options.td`'s option groups, aliases and marshalling) and frontend (`InitPreprocessor.cpp`, `Basic/Targets/X86.cpp`, `Frontend/CompilerInvocation.cpp`, and `clang/include/clang/Basic/Features.def`'s sanitizer features), LLVM 23's `llvm/lib/Support/StringRef.cpp` (how both read a number), and `-march=native`'s detection LLVM 23's `llvm/lib/TargetParser/Host.cpp`: read for their rules (which option wins, which values an option takes and which options clang refuses together, which sanitizers clang supports on x86-64 Linux and which it refuses together, which CPUID bit names which feature, which family and model name which CPU) and written again in C, none of their code copied. The sanitizers' and their groups' names and members are those of clang 23.1.3's `clang/include/clang/Basic/Sanitizers.def`, in its order. |
| `src/buster/compiler/c_predefines_test.c` | Original tests and table generator; Default. clang runs externally as the oracle and the generator's source of answers, and is not vendored. |
| `src/buster/compiler/c_preprocessor_test.c` | Original preprocessor tests; Default. Some expected texts are clang 23's `-E` output for the cases' own inputs. `pp_test_keyword_candidates` and `pp_test_feature_names` list names from clang 23's `TokenKinds.def` and companions and from its `Features.def` (names only, noted below). |
| `src/buster/compiler/c_preprocessor_history.c`, `src/buster/compiler/c_preprocessor_history_impl.c`, `src/buster/compiler/c_preprocessor_history_test.c` | Original expansion-history readers, recording, diagnostic rendering and tests; Default. Shared readers and declarations remain in `c_preprocessor_history.c`; private recording and rendering definitions are in `c_preprocessor_history_impl.c`. Original fixture inputs are checked against the installed Clang as an external oracle. No Clang implementation or external fixture source is copied. |
| `src/buster/compiler/c_preprocessor_batch_test.c`, [measurement evidence](https://github.com/buster14a/buster-performance/tree/a00d16e47b6f1090b8a0424ab0dd8cb4e6016713/docs/measurements/preprocessor-cloud-2026-10-23/) | Original macro-batch fixtures, measurement helpers and cloud observations; Default. Existing project benchmarks supply the preprocessor workload and retain their notices. Clang is an external oracle; the downloaded SQLite amalgamation and installed system headers are runtime inputs, not vendored implementation or fixtures. |
| `src/buster/compiler/c_preprocessor_benchmark.c` | Original benchmark body under Default; the counter/timing/sort helpers adapted through `c_lexer_benchmark.c` retain MIT. |
| `src/buster/compiler/c_refine_benchmark.c` | Original benchmark body under Default; the corpus loader and the counter/timing/sort helpers adapted through `c_lexer_benchmark.c` retain MIT. |
| `src/buster/compiler/c_types.c`, `src/buster/compiler/c_types_test.c`, `src/buster/compiler/c_types_benchmark.c`, `docs/c-types.md` | Original type table, hash-consing, record layout, conversion kernels, compatibility, composite types and printer, and their tests, benchmark and documentation; Default, with the public-domain SplitMix64 reference below. They follow clang 23's behaviour: clang's source (`ASTContext`, `ItaniumRecordLayoutBuilder`, `SemaExpr`'s conversions, `mergeTypes`, `TypePrinter`) was read for its rules and none of it is copied. clang runs externally as the tests' oracle on C the tests generate, and is not vendored. The benchmark's clock, counter and median helpers are its own, not the lexer benchmark's MIT ones. |
| `src/buster/compiler/c_check_*.c`, `src/buster/compiler/c_checker*.c`, `tests/checker/` (the shared source fixtures, moved from `c_checker_test.c`'s table, and ten test files' own source tables, moved from them; #2053), `docs/c-checker.md`, [measurement evidence](https://github.com/buster14a/buster-performance/tree/a00d16e47b6f1090b8a0424ab0dd8cb4e6016713/docs/measurements/c-checker-2026-10-23/) | Original semantic actions, scalar/vector expression checker, source/declaration and binding-event adapters, literal decoder, static-data producer, command, tests, benchmarks and documentation; Default. Existing project type, parser, binding, arena, Link and ELF-writer interfaces are reused. Fixtures and deterministic generated source/expressions are authored for this module; Clang runs externally as an acceptance/diagnostic, numeric-conversion and object-byte/relocation oracle, with no Clang implementation or external corpus imported. C conversion and constant-expression rules and ELF layouts are specification facts; the documentation records the supported subset and oracle configuration. The benchmark's generators, timing, counter and sample-selection helpers are original. |
| `src/buster/compiler/c_check_attribute_clang.inc` | Derived LLVM data, not first-party: the attribute spellings clang 23.1.5 knows in C for x86-64 Linux; Apache-2.0 WITH LLVM-exception ([LICENSES/LLVM.txt](LICENSES/LLVM.txt)), as its header says. "Clang's attribute vocabulary (#147)" below has its inputs and procedure. |
| `src/buster/compiler/c_check_attribute_subjects.inc` | Derived LLVM data, not first-party: the subjects and facts of clang 23.1.5's attributes in C, generated from `clang-tblgen`'s output for `Attr.td`; Apache-2.0 WITH LLVM-exception ([LICENSES/LLVM.txt](LICENSES/LLVM.txt)), as its header says. "Clang's attribute subjects (#147)" below has its inputs and procedure. |
| `src/buster/compiler/c_check_attribute_arguments.inc` | Derived LLVM data, not first-party: how clang 23.1.5's parser reads each attribute's arguments, generated from `clang-tblgen --gen-clang-attr-parser-string-switches` of `Attr.td` at `llvmorg-23.1.5` (sha256 `6014bff340878cf3c656bf3d1ccae64cfd33615d8245c4c5472d10ffd4dc6a1d`; the switches' output sha256 `ec0fd6707c4e45b5587c34520924e87b3f288ed69e342909707f6ea3d50e2868`), by apt.llvm.org's clang-tools-23 1:23.1.5; Apache-2.0 WITH LLVM-exception ([LICENSES/LLVM.txt](LICENSES/LLVM.txt)), as its header says. `docs/c-checker.md` has its procedure. |
| `tools/generate_attribute_arguments.c` | Original generator of `c_check_attribute_arguments.inc` (#147); Default. It reads `clang-tblgen`'s generated output at run time and copies no clang code; its rules follow `ParseAttributeArgsCommon` and `ParseGNUAttributeArgs` in `clang/lib/Parse/ParseDecl.cpp` at `llvmorg-23.1.5` (sha256 `6e3c14424367c206a57ccac384941545fcece46b99fd8c32b347955a2a698a63`), read for their rules. |
| `parse_scalar_attribute_string_error` and the bounded attribute type-name recovery in `src/buster/compiler/c_parser_impl.c` | Original parser diagnostic adapters for #1447; Default. The string-slot lookup reuses the generated LLVM argument and subject/fact rows above without changing their notices. Recovery follows the external Clang 23.1.4 diagnostic oracle; no LLVM implementation is copied. |
| `tools/generate_attribute_subjects.c` | Original generator of `c_check_attribute_subjects.inc`, with its map of clang's subject predicates to C declarations; Default. It reads `clang-tblgen`'s generated output at run time and copies no clang code. |
| `src/buster/compiler/c_check_cpu_dispatch.inc` | Derived LLVM data, not first-party: the processors clang's `cpu_specific` and `cpu_dispatch` take, with their mangling characters, CPU-model masks and features, generated from LLVM `llvmorg-23.1.4`'s `X86TargetParser.cpp` and `X86TargetParser.def`; Apache-2.0 WITH LLVM-exception ([LICENSES/LLVM.txt](LICENSES/LLVM.txt)), as its header says. "clang's CPU dispatch table (#1447)" below has its inputs and procedure. |
| `tools/generate_x86_cpu_dispatch.c` | Original generator of `c_check_cpu_dispatch.inc` (#1447); Default. It reads LLVM's sources at run time, evaluates their feature-set expressions and copies no LLVM code. |
| `src/buster/compiler/c_check_target_versions.inc` | Derived LLVM data, not first-party: the features GNU function multiversioning's versions may name, with their priorities and CPU-model bits, the microarchitecture levels, and the processors' key features' priorities, generated from LLVM `llvmorg-23.1.4`'s `X86TargetParser.def` and `X86TargetParser.cpp`; Apache-2.0 WITH LLVM-exception ([LICENSES/LLVM.txt](LICENSES/LLVM.txt)), as its header says. "GNU multiversioning's target table (#1838)" below has its inputs and procedure. |
| `tools/generate_x86_multiversion.c` | Original generator of `c_check_target_versions.inc` (#1838); Default. It reads LLVM's sources at run time and copies no LLVM code. |
| `src/buster/compiler/c_check_feature_map.inc` | Derived LLVM data, not first-party: each X86 feature in `FeatureInfos` order with its direct implications, and each processor's features, generated from the `X86TargetParser.def` and `X86TargetParser.cpp` of llvm-project `c47d7823685a` (clang 23.1.6); Apache-2.0 WITH LLVM-exception ([LICENSES/LLVM.txt](LICENSES/LLVM.txt)), as its header says. "Clang's feature-map order (#2093)" below has its inputs and procedure. |
| `tools/generate_x86_feature_map.c`, `src/buster/compiler/c_check_feature_map.c` | Original generator of `c_check_feature_map.inc` and original emulation of the map's order (#2093); Default. The generator reads LLVM's sources at run time and copies no LLVM code. |
| `src/buster/compiler/xxh3_short.c` | A C translation of xxHash's XXH3 64-bit hash for inputs of up to 128 bytes, from LLVM's `llvm/lib/Support/xxhash.cpp`; BSD-2-Clause, Copyright (C) 2012-2023 Yann Collet ([LICENSES/xxHash.txt](LICENSES/xxHash.txt)), as its header says, local changes included. "Clang's feature-map order (#2093)" below. |
| `src/buster/compiler/c_check_target_versions.c`, `src/buster/compiler/c_check_program_target_versions.c`, `src/buster/compiler/c_check_target_versions_driver_test.c` | Original checks and code generation of GNU function multiversioning by target strings (`target_clones`, several `target` attributes), and their tests (#1838); Default. The rules follow LLVM `llvmorg-23.1.4`'s `SemaX86::checkTargetClonesAttr` in `clang/lib/Sema/SemaX86.cpp` (sha256 `004b0e8d94649b5a6a10c4ed78a592167341ab89b00ba17dae658da9007c8cb4`), `CheckMultiVersionFunction` and its helpers in `clang/lib/Sema/SemaDecl.cpp` (sha256 `9b2119593e60f6bda8aaa020f6ed77fbcd13325f30a904210a3624aa07fc7bdd`), `TargetInfo::parseTargetAttr` in `clang/lib/Basic/TargetInfo.cpp` (sha256 `eb94ce776fdda0340867b211d90997c0de3d0e8297f67faca8f688546f7329eb`) and `X86TargetInfo::getFMVPriority`, `validateCpuSupports` and `validateCpuIs` in `clang/lib/Basic/Targets/X86.cpp` (sha256 `4f0fdd41b3990b79a721f9839ff7e1a35524642b645df0d030bf5619dee6fbf0`), `emitMultiVersionFunctions`, `GetOrCreateMultiVersionResolver` and `getMangledNameImpl`'s `AppendTargetMangling` and `AppendTargetClonesMangling` in `clang/lib/CodeGen/CodeGenModule.cpp` (sha256 `047ac9ad37cfafb2e930cec0fd089dafed8d8aaaf2575532649526c4287bee8e`), `EmitX86MultiVersionResolver` and `FormX86ResolverCondition` in `clang/lib/CodeGen/CodeGenFunction.cpp` (sha256 `419ddade7e8d0119bbf6aba786bf06095bcd3fe78bf05d4fba859dbbf9b6e465`) and `EmitX86CpuIs` and `EmitX86CpuSupports` in `clang/lib/CodeGen/TargetBuiltins/X86.cpp` (sha256 `3f76a3d9cbc959070addd4b49e1dd2548cc07502c8f9668621fc6d081cf9f0fb`), read for their rules and written again in C; the diagnostics' texts are clang's, from `clang/include/clang/Basic/DiagnosticSemaKinds.td` at the same tag (sha256 `72ffb52ee210258c53fcbbe0b22ae8a07bff37096ad5524d92a6bfbf9891285c`). The resolver's instructions are original. No LLVM code is copied. clang runs externally as the tests' oracle. |
| `src/buster/compiler/c_check_cpu_dispatch.c`, `src/buster/compiler/c_check_program_cpu_dispatch.c`, `src/buster/compiler/c_check_cpu_dispatch_driver_test.c` | Original checks and code generation of clang's `cpu_specific` and `cpu_dispatch` multiversioning, and their tests (#1447); Default. The rules follow LLVM `llvmorg-23.1.4`'s `handleCPUSpecificAttr` in `clang/lib/Sema/SemaDeclAttr.cpp` (sha256 `6b0452504d5520bd6eff41e3b68f23fa57ab69c704f266d8da82b54a79bf8839`), `CheckMultiVersionFunction` and its helpers and `ActOnFinishFunctionBody`'s dispatcher warning in `clang/lib/Sema/SemaDecl.cpp` (sha256 `9b2119593e60f6bda8aaa020f6ed77fbcd13325f30a904210a3624aa07fc7bdd`), `CheckFallThroughForBody` in `clang/lib/Sema/AnalysisBasedWarnings.cpp` (sha256 `9ad92316fb41ab7dd6a68abecc0f0d2b39959d849331843bb21f0d7da2ab13af`), `emitCPUDispatchDefinition` in `clang/lib/CodeGen/CodeGenModule.cpp` (sha256 `047ac9ad37cfafb2e930cec0fd089dafed8d8aaaf2575532649526c4287bee8e`), `EmitX86MultiVersionResolver` and `FormX86ResolverCondition` in `clang/lib/CodeGen/CodeGenFunction.cpp` (sha256 `419ddade7e8d0119bbf6aba786bf06095bcd3fe78bf05d4fba859dbbf9b6e465`) and `EmitX86CpuSupports` in `clang/lib/CodeGen/TargetBuiltins/X86.cpp` (sha256 `3f76a3d9cbc959070addd4b49e1dd2548cc07502c8f9668621fc6d081cf9f0fb`), read for their rules and written again in C; the diagnostics' texts are clang's, from `clang/include/clang/Basic/DiagnosticSemaKinds.td` at the same tag (sha256 `72ffb52ee210258c53fcbbe0b22ae8a07bff37096ad5524d92a6bfbf9891285c`). The resolver's instructions are original. No LLVM code is copied. clang runs externally as the tests' oracle. |
| `src/buster/compiler/c_check_os_log_input.c` | Original os_log format reader and buffer layout (#148); Default. The rules follow LLVM `llvmorg-23.1.5`'s `clang/lib/AST/PrintfFormatString.cpp` (sha256 `8878ffdfa8a657010bdbfd1db1a225f99b4b125cd6c3d91c4074377cf3036323`), `clang/lib/AST/FormatString.cpp` (sha256 `ee0b5d7fb398cd015be4f0e56f4d365a21bd78edc35459fc36da385c3adcb248`), `clang/lib/AST/OSLog.cpp` (sha256 `f864111811074e880806e80aeab1fb1dafdaf9751620b2ea599ea184c13584f9`), `clang/include/clang/AST/OSLog.h` (sha256 `fc4208d7c46463d44d637c11c95715306927588c394cf2c68cd2ff57689caadd`), and `BuiltinOSLogFormat` in `clang/lib/Sema/SemaChecking.cpp` and `emitBuiltinOSLogFormat` in `clang/lib/CodeGen/CGBuiltin.cpp` at the same tag, read for their rules and written again in C; no LLVM code is copied. clang runs externally as the tests' oracle. |
| `src/buster/compiler/c_check_alloc_token_input.c` | Original allocation-token inference, type naming and hashing for `__builtin_infer_alloc_token` (#148); Default. The rules follow LLVM `llvmorg-23.1.5`'s `clang/lib/AST/InferAlloc.cpp` (sha256 `a23adc952e610432832635ff515fdcd0cb4f77a37fb0b779e8539dcf2c2cf594`) and `llvm/lib/Support/AllocToken.cpp` (sha256 `4cc44c97ee14394d6c0b3e1bdf8dd9642cee014c28a6cb7bd67a234081236d9e`), read for their rules and written again in C. The hash is SipHash-2-4 as Aumasson and Bernstein published it, under the 16-byte key of `getStableSipHash` in `llvm/lib/Support/SipHash.cpp` at the same tag (sha256 `20662e3d8e1149a7d09f651d70671c237b298283492fdc26900bc1730f09f1b8`), Apache-2.0 WITH LLVM-exception ([LICENSES/LLVM.txt](LICENSES/LLVM.txt)); no other LLVM code is copied. clang runs externally as the tests' oracle. |
| `src/buster/compiler/c_check_availability_input.c` | Original checks of `__builtin_available`'s specs and its unguarded uses (#148); Default. The rules follow LLVM `llvmorg-23.1.5`'s `ParseAvailabilitySpec`, `ParseAvailabilityCheckExpr` and `CheckAvailabilitySpecList` in `clang/lib/Parse/ParseExpr.cpp` (sha256 `63d587d720445975e20be22431dabd6dc61bbd4aa6db6200a3d9e22ecd0af477`), `ParseVersionTuple` in `clang/lib/Parse/ParseDecl.cpp` (sha256 `6e3c14424367c206a57ccac384941545fcece46b99fd8c32b347955a2a698a63`), `ActOnObjCAvailabilityCheckExpr` in `clang/lib/Sema/SemaExprObjC.cpp` (sha256 `7ea6d8351ffcc1489228738560fec473e467db5de4ee5d264f48506a9acb666e`), `DiagnoseUnguardedAvailability` in `clang/lib/Sema/SemaAvailability.cpp` (sha256 `ea871b3ceebf54fc2da5fc7ca1629f31b4f94fcf2e894fd931902b6f3b79d1a6`) and `validateAnyAppleOSVersion` in `clang/include/clang/AST/Availability.h` (sha256 `72a7ede3cb22ac6b10b9274335800a9810c4fa492593bd4c25672019e73f6506`), read for their rules and written again in C; the platforms' names are those of `AvailabilityAttr`'s `getPrettyPlatformName` and `canonicalizePlatformName` in `clang/include/clang/Basic/Attr.td` at the same tag (sha256 `6014bff340878cf3c656bf3d1ccae64cfd33615d8245c4c5472d10ffd4dc6a1d`), names only, and the diagnostics' texts are clang's, from `DiagnosticParseKinds.td` and `DiagnosticSemaKinds.td` there. No LLVM code is copied. clang runs externally as the tests' oracle. |
| `src/buster/compiler/c_check_counted_by_input.c`, the counted_by parts of `src/buster/compiler/c_checker.c` (`check_counted_lookup`, `check_graph_counted_member`, `check_graph_counted_size`) and `src/buster/compiler/c_check_program_lower.c` (`check_lower_counted_size`, `__builtin_counted_by_ref`'s code) | Original checks and code of clang's counted_by family and `__builtin_counted_by_ref` (#148); Default. The rules follow LLVM `llvmorg-23.1.5`'s `CheckCountedByAttrOnField` in `clang/lib/Sema/SemaBoundsSafety.cpp` (sha256 `b7ef9b463e1785cae7104cd57610dd5fd176a3cb12b603f13db76608bb4dfde0`), `handleCountedByAttrField` in `clang/lib/Sema/SemaDeclAttr.cpp` (sha256 `6b0452504d5520bd6eff41e3b68f23fa57ab69c704f266d8da82b54a79bf8839`), `BuiltinCountedByRef` and `CheckInvalidBuiltinCountedByRef` in `clang/lib/Sema/SemaChecking.cpp` (sha256 `77e982fe7436d52e22a7cc0ef00a086af4f9d81d638de3a93c3f4d46240d2271`) and their callers in `clang/lib/Sema/SemaExpr.cpp` (sha256 `a326f52b63a8dd23d2115c15453f1749a8158ed40f6e2fbbd473e661b5a988b7`), `emitCountedBySize` and its helpers in `clang/lib/CodeGen/CGBuiltin.cpp` (sha256 `794e593298059bfb323f3d3a72b4c50ae61200bd6c75d0c129d4a71e6e3cb6bf`), `GetCountedByFieldExprGEP` in `clang/lib/CodeGen/CGExpr.cpp` (sha256 `edf3a64d85a7e48759218036b9738c9db697b34f9e2122c1533e394528519b43`), `findStructFieldAccess` in `clang/lib/AST/Expr.cpp` (sha256 `5677e0ff4f9ef2b38d97d0df3f153c2a25c2d4e5925730ae03dacd664550bb9a`) and `tryEvaluateBuiltinObjectSize` in `clang/lib/AST/ExprConstant.cpp` (sha256 `087dcfe1b03da45dc4ccb08ac1631a2fffe39363ced9b9754b123e79b4374eae`), read for their rules and written again in C; the diagnostics' texts are clang's, from `DiagnosticSemaKinds.td` and `DiagnosticParseKinds.td` there. No LLVM code is copied. clang runs externally as the tests' oracle. |
| The masked memory builtins' and `__builtin_elementwise_clmul`'s code in `src/buster/compiler/c_check_program_vector_builtins.c` (`check_vector_masked_row`, `check_vector_clmul_packed`) and `src/buster/compiler/c_check_program_lower.c` (`check_lower_vector_lanes`) | Original lowering (#148); Default. What each computes follows LLVM `llvmorg-23.1.5`'s `clang/lib/CodeGen/CGBuiltin.cpp` (sha256 `794e593298059bfb323f3d3a72b4c50ae61200bd6c75d0c129d4a71e6e3cb6bf`): the llvm.masked.* intrinsics it emits, their passthrus and alignments, and the GEPs of the gathers' and scatters' addresses (a scatter's of the index's element type), read for those facts; the instruction sequences are this compiler's, compared with clang's builds. No LLVM code is copied. |
| `src/buster/compiler/c_check_thread_safety_input.c`, and the lifetimebound, loader_uninitialized, weak_import, internal_linkage, nocf_check and thread-safety rules of `src/buster/compiler/c_check_attribute.c`, `src/buster/compiler/c_check_input.c` and `src/buster/compiler/c_check_program_emit.c` | Original checks and effects of those attributes (#147); Default. The rules follow LLVM `llvmorg-23.1.5`'s `clang/lib/Sema/SemaDeclAttr.cpp` (sha256 `6b0452504d5520bd6eff41e3b68f23fa57ab69c704f266d8da82b54a79bf8839`: their handlers, `checkAttrArgsAreCapabilityObjs`), `clang/lib/Sema/SemaDecl.cpp` (sha256 `9b2119593e60f6bda8aaa020f6ed77fbcd13325f30a904210a3624aa07fc7bdd`: `checkLifetimeBoundAttr`, the redeclaration and initializer checks), `clang/lib/AST/DeclBase.cpp` (sha256 `5a011883aec9cf72686b054e9a9623b7c3a0da6d4ac4499a938eb92104f8496b`: `canBeWeakImported`) and `clang/lib/CodeGen/CodeGenModule.cpp` (sha256 `5e52afd91d486ccd8c77f321f84cab1f8bff0636a84d70a36244311bc0ac3e17`: their linkages), read for their rules and written again in C; the diagnostics' texts are clang's, from `DiagnosticSemaKinds.td` there. No LLVM code is copied. clang runs externally as the tests' oracle. |
| The attribute argument errors and warnings of `src/buster/compiler/c_check_attribute.c` (`CheckAttributeArgumentError`, `check_input_attribute_constant_error`, `check_input_attribute_mode`, `check_input_target_warning`, the format, alloc, visibility and no_sanitize checks), their rendering in `src/buster/compiler/c_checker_driver.c` and `src/buster/compiler/c_check_warning_render.c`, and `src/buster/compiler/c_check_attribute_arguments_driver_test.c` | Original checks (#147); Default. The rules follow LLVM `llvmorg-23.1.5`'s `clang/lib/Sema/SemaDeclAttr.cpp` (sha256 `6b0452504d5520bd6eff41e3b68f23fa57ab69c704f266d8da82b54a79bf8839`: `AddModeAttr`, `parseModeAttrArg`, `handleFormatAttrCommon`, `handleFormatAttr`, `getFormatAttrKind`, `handleFormatArgAttr`, `handleAllocSizeAttr`, `AddAllocAlignAttr`, `checkParamIsIntegerType`, `handleVisibilityAttr`, `checkTargetAttr`, `handleNoSanitizeAttr`), `TargetInfo::parseTargetAttr` in `clang/lib/Basic/TargetInfo.cpp` (sha256 `eb94ce776fdda0340867b211d90997c0de3d0e8297f67faca8f688546f7329eb`) and the parser's reading of string arguments in `clang/lib/Parse/ParseDecl.cpp` (sha256 `6e3c14424367c206a57ccac384941545fcece46b99fd8c32b347955a2a698a63`), read for their rules and written again in C; the diagnostics' texts are clang's, from `DiagnosticSemaKinds.td` (sha256 `72ffb52ee210258c53fcbbe0b22ae8a07bff37096ad5524d92a6bfbf9891285c`) and `DiagnosticCommonKinds.td` (sha256 `7f5580a35b761797fac0832bfd9f489f061d95d58ed542e061578e61950e1b43`) there. The test's fixtures are original. No LLVM code is copied. clang runs externally as the tests' oracle. |
| `src/buster/compiler/c_parser.c`, `src/buster/compiler/c_parser_impl.c`, `src/buster/compiler/c_parser_test.c`, `docs/c-parser.md` | Original parser (the vector path's kernels and their scalar twins, the checks, the scalar parser, the postorder and the driver), its tests and documentation; Default, with the public-domain SplitMix64 reference below. The kernels are ported from this project's own spike, #186 (branch `claude/spike-186-k2-spills` at `e896031`), under Default too. The tree's construction follows Bar-On and Vishkin's published algorithm (1985), referenced, not copied. The diagnostics follow clang 23's behaviour: their messages are clang's texts for the same errors and their places are where clang reports them, as clang's output shows, and the tokens allowed after a tag's body follow clang's `Parser::isValidAfterTypeSpecifier`, read for its rule; no clang code is copied. The predeclared typedef names are clang's (names only). clang runs externally as the tests' oracle on the tests' snippets, generated inputs and corpora, which are read at run time and not vendored. |
| `docs/c-parser-fragments.md` | Original record of the expression-fragment experiment (#104–#107), its measurements and its decision; Default. The experiment's code, removed by that decision, was original under Default too. Salter's operator-fragment discussion and Carbon's postorder bracketing are design references recorded in the document; no historical Zig implementation or external test fixture was copied. External corpora were runtime test inputs. |
| `src/buster/compiler/c_parser_benchmark.c` | Original benchmark body under Default; the counter/timing/sort helpers adapted through `c_lexer_benchmark.c`, by way of #186's spike benchmark, retain MIT. |
| `src/buster/compiler/c_gnu.c`, `src/buster/compiler/c_gnu_test.c`, `src/buster/compiler/c_gnu_benchmark.c`, `docs/c-gnu-extensions.md`, and the GNU statement-attribute/machine-mode additions to `c_parser.c`, `c_parser_impl.c`, `c_parser_test.c`, `c_types.c` and `c_types_test.c` | Original metadata extraction, parser/type actions, fixtures, benchmark helpers and documentation for #147; Default. Clang 23.1.1 runs externally as a syntax, canonical-type and layout oracle. Behavioral rules were read in LLVM `llvmorg-23.1.1` `clang/lib/Parse/ParseStmt.cpp` and `clang/lib/Sema/SemaDeclAttr.cpp`; installed Clang 23.1.1 `AttrParsedAttrImpl.inc` supplied statement-attribute spelling facts. No external implementation or fixture corpus was copied. Existing project parser, type and test interfaces are reused. |
| `src/buster/compiler/c_bindings.c`, `src/buster/compiler/c_bindings_test.c`, `src/buster/compiler/c_bindings_benchmark.c`, `docs/c-bindings.md` | Original scoped binding stacks, radix/segmented-scan kernels, event-stream tests, deterministic LCG generator, benchmark and documentation; Default. The benchmark's clock, perf counter and sample-selection helpers are original. No external implementation, generated table or test corpus is imported. Existing arena and intrinsic interfaces are used directly. |
| `src/buster/compiler/c_lowering.c`, `src/buster/compiler/c_lowering_test.c`, `src/buster/compiler/c_lowering_benchmark.c`, `docs/c-lowering.md`, [measurement evidence](https://github.com/buster14a/buster-performance/tree/a00d16e47b6f1090b8a0424ab0dd8cb4e6016713/docs/measurements/c-lowering-2026-10-23/) | Original selective-emission graph planner, vector/scalar reachability, independent oracle, C fixtures and measurements for #129; Default. Benchmark helpers follow this project's original binding benchmark. Clang runs externally on original C fixtures as an emission oracle; its implementation is not imported. |
| [measurement evidence](https://github.com/buster14a/buster-performance/tree/a00d16e47b6f1090b8a0424ab0dd8cb4e6016713/docs/measurements/parser-c-testsuite-2026-10-23/) | Original qualification adapter, control inputs, reports and hash manifest; Default, except the unmodified Clang-generated `predefines.h` dump retains the LLVM terms in `LICENSES/LLVM.txt`. The record identifies Clang 23.1.1 by executable hash, its GNU17/x86-64 configuration, empty source input and exact generation command. c-testsuite revision `5c7275656d751de0e68b2d340a95b5681858ed07` is a separately pinned runtime input, with its per-test origin and license metadata retained in that checkout; its source contents are not vendored. |
| `src/buster/compiler/c_environment.c`, `src/buster/compiler/c_environment_test.c`, `src/buster/compiler/c_environment_benchmark.c`, `src/buster/compiler/c_check_environment.c`, `src/buster/compiler/c_check_environment_test.c`, `docs/c-environments.md`, [measurement evidence](https://github.com/buster14a/buster-performance/tree/a00d16e47b6f1090b8a0424ab0dd8cb4e6016713/docs/measurements/c-environments-2026-10-23/) | Original source-position environment index, completion checks, parser-role body ranges, retained checker handoff, fixtures, LCG generator and measurements; Default. The benchmarks reuse the original binding/checker measurement helpers and original generated checker-source inputs. Clang runs externally as the fixture oracle; the existing parser test's file/oracle and type-table helpers are reused. No external implementation, generated table or corpus is imported. |
| `src/buster/compiler/c_check_body_bindings.c`, `src/buster/compiler/c_check_body_bindings_test.c`, `docs/c-body-bindings.md`, [measurement evidence](https://github.com/buster14a/buster-performance/tree/a00d16e47b6f1090b8a0424ab0dd8cb4e6016713/docs/measurements/c-body-bindings-2026-10-23/) | Original independent body-event views, scalar/vector translation, file fallbacks, source/guarded fixtures and pipeline measurements; Default. Reuses the original binding kernels, retained source/checker test helpers, measurement helpers and frozen generated checker inputs. No external implementation or corpus is imported. |
| `src/buster/compiler/c_parser_binding_trace.c`, `src/buster/compiler/c_parser_bindings.c`, `src/buster/compiler/c_parser_bindings_test.c`, `src/buster/compiler/c_parser_bindings_benchmark.c` | Original parser-to-binding source-event adapter, declaration columns, scope fixtures, seeded source generator and source benchmark; Default. Existing project grammar, arenas and original binding benchmark helpers are reused. Clang is an external acceptance oracle. SQLite 3.53.4, Zig 0.14.1 libc and c-testsuite at `5c7275656d751de0e68b2d340a95b5681858ed07` are external validation inputs, read at run time and not incorporated; input identities are recorded in `docs/c-bindings.md`. |
| `build.sh`, `.gitignore`, `.gitattributes`, `.github/workflows/9700x.yml` | Original build/configuration; Default. The referenced GitHub action runs externally and is not vendored. |
| `.github/actions/vector-runtime/`, `tests/ci/vector-runtime.cjs`, and the request accounting in `tools/ci/vector-draw.sh` and `tests/ci/vector-draw.sh` | Original same-run artifact protocol glue, bounded REST reads, request ledgers and offline fixtures for #2309; Default. The runtime token and Results service contracts were read in actions/runner `8cfa1049f86781d141056082d73802d56cd2b433` and actions/toolkit `0c3a9c332b678ff1e776f93d728a4ad279803abe`, with exact source URLs in the action header. No runner/toolkit implementation, package or generated client is copied or vendored. JavaScript is confined to the Node action runtime that receives GitHub's artifact credentials. |
| `CMakeLists.txt`, `CMakePresets.json`, `cmake/*.cmake`, `tools/ci/build_regression.c`, `tools/ci/setup-build-tools.sh`, `.github/workflows/build.yml` | Original CMake build description, presets, post-link/print/call-graph scripts, regression checks and tool staging for #1182; Default. They use CMake's and Ninja's documented interfaces; no CMake module or third-party build code is copied. CI downloads the official CMake 4.4.3 and Ninja 1.13.2 releases, verified by SHA-256, as external tools; they are not vendored. |
| `tools/allcore/allcore.c`, `docs/commands/all-core.md` | Original all-core gauge for #2536's step 0: a cohort runner, CPU-wide cycle sampling read from the kernel's perf_event ring buffers, ELF symbol anchors of the stage clock, and the stage and whole-compile reductions; Default. Its runner and reductions adapt the measurement record's own first-party `allcore.c` ([buster-performance at `eaa710db`](https://github.com/buster14a/buster-performance/blob/eaa710db255e42520666aaea225b142aa74a2bde/docs/measurements/all-core-design-672-2026-11-12/scripts/tools/allcore.c), the same author and terms); the `perf_event_open` ABI and the ELF layouts are Linux's and System V's interfaces, from the system's headers. No external implementation is copied. |
| `tools/codegen/split.c`, `tools/codegen/split_bitcode.cpp` | Original order-preserving partition of a unit's optimized module for parallel native generation; Default. They use LLVM's documented C API and, for the bitcode writer's use-list-order argument the C API lacks, its C++ `WriteBitcodeToFile`; the installed LLVM libraries are external build dependencies, not vendored, and no LLVM implementation is copied. |
| `tools/ci/sanitizers.sh`, `tests/sanitizers/`, `src/buster/compiler/fuzz.c`, `tests/fuzz/` and the sanitizer/fuzz sections of `docs/ci.md` | Original detector canaries, compressed-ID tail regression, runtime policy, child-report audit, in-process compiler callback, entry overrides, crash/replay control, dictionary, C seeds and documentation; Default. Clang sanitizer and libFuzzer documentation supplies public flags and API contracts. The system's matching compiler-rt/libFuzzer runtimes and GitHub artifact action run externally and are not vendored. Legacy Buster was inspected for interface and coverage comparison only; no legacy implementation or fixture was copied. |
| `src/buster/compiler/foreign_strings.c`, `src/buster/compiler/foreign_strings_link.c`, `src/buster/compiler/foreign_strings_test.c`, `src/buster/compiler/foreign_strings_output_test.c`, `src/buster/compiler/foreign_strings_benchmark.c`, `docs/foreign-string-merging.md` | Original string pooling, reference remapping, fixtures, measurements and documentation; Default. Existing masked-load, hashing, arena and ELF interfaces are reused. LLVM ELF symbol-address handling was consulted for semantics; no implementation or fixtures were copied. GNU ld and lld run externally on original fixtures. |
| `src/buster/compiler/foreign_driver.c`, `src/buster/compiler/foreign_driver_test.c`, `src/buster/compiler/foreign_driver_symbols_test.c`, `src/buster/compiler/foreign_driver_dedup_test.c`, `src/buster/compiler/foreign_driver_pipeline_test.c`, `docs/foreign-driver.md`, `docs/foreign-interpreter.md` | Original raw linker command, ordered-argument and named-symbol selection adapters, guarded and command/runtime fixtures and documentation; Default. Existing project resolver, conversion, transformations, writer and test helpers are reused. GNU ld, lld, GCC and system runtimes are external test inputs/tools; their implementation is not incorporated. |
| `src/buster/compiler/foreign_copy_test.c`, `src/buster/compiler/foreign_bounds_test.c` | Original copy-relocation, __start_/__stop_ and IFUNC-refusal tests: their C and assembly fixtures and their library are original; Default. GCC, GNU as and lld run externally (lld as the oracle under --cross-check); the policies follow GNU ld's and lld's documented behavior and observed output, not copied code. |
| `src/buster/compiler/foreign_corpus_test.c` | Original differential corpus test of link-foreign against lld and mold, its C and C++ fixture programs and its comparison rules; Default. gcc, clang, ar, lld and mold run externally. SQLite (public domain), Lua (MIT) and this repository's sources are external validation inputs, compiled and run at test time from paths the caller gives, not incorporated. |
| `src/buster/compiler/sqlite_test.c`, `tests/sqlite/*.sql`, `tools/ci/fetch-sqlite.sh` | Original SQLite test harness (#146): the shell's builds, their comparison with clang's, the SQL scripts and the pinned fetch; Default. SQLite (public domain) at commit `ccc132c5be20ab5c755c97a08d06b1b592fef330` (3.53.2) and the frozen corpus's amalgamation and shell are external validation inputs, fetched, compiled and run at test time; its CLI tests (`test/*.sql`) and build options are read from that tree or named, not incorporated. clang runs externally as the reference. |
| `src/buster/compiler/sqlite_testfixture.c`, `tests/sqlite/testfixture-subset.txt`, `tools/ci/tcl-config.sh` | Original testfixture harness (#146): testfixture's builds by SQLite's configure and Makefile, the comparison of their test runs with clang's, the per-PR subset (names of SQLite's test scripts, selected by this project's own timing run) and Tcl's discovery and pin; Default. SQLite (public domain) at commit `ccc132c5be20ab5c755c97a08d06b1b592fef330` (3.53.2): its build system, `tclsqlite.c`, test sources and Tcl test scripts are external validation inputs, fetched, built and run at test time, not incorporated. Tcl 8.6 (Tcl/Tk license, BSD-style) is the distribution's package, installed on the host and linked at test time (hosted: Ubuntu 26.04's `tcl8.6-dev` 8.6.17+dfsg-1build1; the 9700X: Arch's `tcl` 8.6.16-1), not incorporated. clang runs externally as the reference. |
| `src/buster/compiler/foreign_link_lanes.c` | Original foreign conversion on lanes, its views of the link and the lanes' check of the link (`link_check_lanes`, in `link.c`); Default. The project's existing lane, Link and conversion interfaces are reused; no other linker's implementation is incorporated. |
| `src/buster/compiler/streaming.c`, `src/buster/compiler/streaming_test.c`, `src/buster/compiler/streaming_benchmark.c`, `src/buster/compiler/streaming_benchmark_test.c` | Original streaming-output experiment: its data-first static executable writer, schedules, writer lane, DWARF units made as functions compile, generated-program glue, tests and benchmark; Default. Existing Tier 0, generator, branch sizing, encoder, unwind, debug, chunk-hash and executable writer code is reused. ELF, DWARF call-frame and DWARF 5 unit encodings are format facts. readelf, llvm-dwarfdump, gdb and perf run externally as consumers under --cross-check, not vendored. |
| `.claude/settings.json`, `.claude/hooks/join-tailnet.sh`, `.claude/hooks/test-join-tailnet.sh` | Maintainer configuration: settings adapted from his older Buster configuration; locally added tailnet hook and its original fixtures. Default. See the recorded import below. |
| `.githooks/commit-msg`, `.githooks/enable-sign-off.sh`, `.codex/hooks.json` | Original hooks that add the maintainer's sign-off where his opt-in is set, and Codex's configuration that runs one; Default. |
| `AGENTS.md`, `CLAUDE.md`, `docs/coding-style.md` | Original project/contributor guidance; Default. The style adapts conventions, not a grant to import old compiler code. |
| `docs/c-lexer.md`, `docs/x86-64-encoder.md`, `docs/object-writer-and-linker.md`, `docs/c-refinement.md`, `docs/c-preprocessor.md` | Original design/measurement reports; Default. Referenced tools, articles and measurements are distinguished from vendored source. |
| [measurement evidence](https://github.com/buster14a/buster-performance/tree/a00d16e47b6f1090b8a0424ab0dd8cb4e6016713/docs/measurements/header-replay-2026-10-23/) | Original isolated header transcript experiment, C batch/equivalence/guarded-tail probes, fixtures, run glue and observations for #111; Default. Its patches apply to the identified Buster revision, and generated inputs use this project's #109 generator with seed 111. musl 1.2.5, Lua 5.4.8, Zig's libc corpus and installed Clang/system headers are external runtime inputs, not imported source or fixtures. The record separates local diagnostic checks from locked 9700X qualification. No external implementation or corpus is vendored. |
| `README.md`, `LICENSING.md`, `CONTRIBUTING.md`, this file | Original adoption documentation; Default, except quoted upstream notices retain their terms. |
| `LICENSE`, `DCO`, `LICENSES/*` | Retained legal/attribution texts; their own terms. These are not original project prose. |

## Accelerated-Zig-Parser and simdjson

Source: [davidgmbb/Accelerated-Zig-Parser at
`f4482e2dd53413522d75d8c254b9a824021238ce`](https://github.com/davidgmbb/Accelerated-Zig-Parser/tree/f4482e2dd53413522d75d8c254b9a824021238ce).
The exact root license is retained as
[LICENSES/Accelerated-Zig-Parser.txt](LICENSES/Accelerated-Zig-Parser.txt),
including **Copyright (c) 2023 Niles Salter**, the MIT grant and the Apache-2.0
terms for marked simdjson/simdjzon-derived portions.

`c_lexer.c` combines `c/c_tokenizer.h`, `c/c_tokenizer_tables.h`,
`c/c_tokenizer_unicode.h`, `c/c_tokenizer_scalar.c` and `c/c_tokenizer.c`.
Import commit `6c602cd` and its section markers identify them. The lexer tests
and benchmark come from `c/c_tokenizer_test.c` (local commits `832eaf8` and
`d89d63e`). Local formatting, bounds handling, command integration, tests and
optimizations are modifications; the source headers and Git history record
them. The embedded upstream license is retained.

The upstream C implementation was introduced in `d0c062147011361411d1677eaec9bf69a3881bf8`
after the compacting-tokenizer port in
`178a2d27fe7a50d642cb0a48f193ee9723f49633`. That port identifies Niles Salter's
MIT-licensed compacting-tokenizer work and retains his 2024 notice. Both the
2023 license notice and **Copyright (c) 2024 Niles Salter** are retained here.
The reviewed fork's C changes were made by the maintainer and his agent.

The backslash-run calculation in `c_lexer.c`'s `classify` identifies John
Keiser's simdjson algorithm. The upstream Zig port links
[simdjson PR #2042](https://github.com/simdjson/simdjson/pull/2042), merged as
`9408298e0e64fbfe79b178278b58d81bc81eb7af`; its
`src/generic/stage1/json_escape_scanner.h` is the reference. That source uses
Apache-2.0 without the LLVM exception. Its author attribution (Daniel Lemire,
Geoff Langdale, John Keiser) is retained verbatim in
[LICENSES/simdjson-AUTHORS.txt](LICENSES/simdjson-AUTHORS.txt).
The examined simdjson revision and lexer import have no separate root `NOTICE`.
The upstream license also names simdjzon; no separately copied simdjzon file
was identified among the five imported C files. That upstream statement is
preserved, not narrowed or removed.

The MIT benchmark helper lineage extends beyond the lexer: the counter-open
and counter-read routines and clock sample in `x86_64_encoder_benchmark.c`
match the imported harness after renaming; the object writer benchmark uses
the same pattern. Their insertion-sort helpers, also used by
`lane_benchmark.c`, follow the local lexer benchmark, and so do those of
`c_parser_benchmark.c`, through #186's spike benchmark. These portions and
local changes to them remain MIT; the rest of those benchmark files uses
Default.

### Generated tables and Clang-following code

`c/gen_c_tokenizer_tables.py` generates the byte-property, operator and
punctuator tables from `c/c_tokenizer.h` and explicit C/C++ character rules.
It reads no external instruction or Unicode database. Those generated
representations follow the imported component's MIT terms.

`c/gen_c_tokenizer_unicode.py` instead probes every nonsurrogate non-ASCII
code point using `clang -cc1 -std=c23 -dump-raw-tokens`, then coalesces accepted
code points into ranges. The imported header records **clang 23.1.3**, with
695 start and 812 continue ranges. It is not output licensed solely by the
generator's own terms.

The scalar lexer explicitly follows Clang's `clang/lib/Lex/Lexer.cpp` and
names corresponding routines. Preserve LLVM's existing terms for any adapted
expression in those portions and the identifier-data representation:
[LICENSES/LLVM.txt](LICENSES/LLVM.txt) is the exact LLVM license from
[`llvmorg-23.1.3`, commit `0d261d1ca552c95a8f007e061c787ac7132fbcbc`](https://github.com/llvm/llvm-project/tree/0d261d1ca552c95a8f007e061c787ac7132fbcbc).
The corresponding `Lexer.cpp` and `UnicodeCharSets.h` carry
`Apache-2.0 WITH LLVM-exception`. The local changes are a C translation with
bounded slices, simplified diagnostics and different storage/control flow.
These existing LLVM terms are not the new first-party grant.

The #285 raw-error query follows the delimiter rules in that same pinned
`Lexer.cpp` (`LexRawStringLiteral`). Its diagnostic wording follows
`clang/include/clang/Basic/DiagnosticLexKinds.td` at `llvmorg-23.1.3`, under
the retained LLVM terms. The new fixtures and contextual error-record,
emission and recovery code are original project work; Clang runs externally
as their diagnostics and preprocessing oracle.

The range data reflects Unicode identifier properties plus Clang's
mathematical-notation extensions. Retain the Unicode data notice as well:
**Copyright © 1991-2026 Unicode, Inc.** and the complete
[Unicode License V3](LICENSES/Unicode-3.0.txt), copied from
[Unicode's license](https://www.unicode.org/license.txt).
This preserves data rights without asserting that all output of a compiler
inherits its implementation license.

The generator recorded a version string, not a hash of the Clang binary or an
independent Unicode input manifest. The LLVM tag above is the review
comparison pin, not a claim to have recovered that binary. The imported table
bytes and generator are pinned by the lexer revision; retain all these terms
when redistributing them. Regeneration must record the actual tool and inputs.

## Unicode character names (#1447)

Named universal character names (`\N{NAME}`) are read by a table of Unicode's
character names and by routines that follow LLVM's.

- **Data:** `src/buster/compiler/c_unicode_names.inc` is derived Unicode data,
  not first-party material: the names and aliases of Unicode's characters, the
  ranges of names Unicode makes by rule from code points, and the general
  categories' printable and formatting characters. It keeps the Unicode
  License V3's terms, **Copyright © 1991-2026 Unicode, Inc.**, whose complete
  text is [LICENSES/Unicode-3.0.txt](LICENSES/Unicode-3.0.txt), and its header
  says so.
- **Inputs:** `UnicodeData.txt` (sha256
  `0736451de439ae7baf1425136617da495e09ee5afbe6e394374db7009ea08950`) and
  `NameAliases.txt` (sha256
  `3d5cf5e468901b080cd99adf2230061b748083705fe633db43ac8b73ec7a13da`, its own
  header "NameAliases-18.0.0.txt, Date: 2026-02-03") of the Unicode Character
  Database 18.0.0, fetched from <https://www.unicode.org/Public/18.0.0/ucd/>
  on November 7, 2026. Unicode 18.0.0 is the version clang 23.1.6 (Ubuntu's
  `1:23.1.6~++20261103094202+2875c6073a7e`) reads: decoded, LLVM's generated
  dictionary at that commit
  (`llvm/lib/Support/UnicodeNameToCodepointGenerated.cpp`) holds exactly
  18.0.0's names and aliases plus 40 Chisoi names (U+16D80 to U+16DA9) of a
  draft that 18.0.0 doesn't have, which this table leaves out. LLVM's
  printable ranges (`llvm/lib/Support/Unicode.cpp`) are this table's but for
  those 40 code points, and its formatting ones are this table's.
- **Generator:** `tools/generate_unicode_names.c`, original project code under
  the default terms. It reads the inputs at run time and copies no LLVM code;
  which names it takes follows LLVM's generator
  (`llvm/utils/UnicodeData/UnicodeNameMappingGenerator.cpp`), read for its
  rules. It was run as:

  ```sh
  cc -std=c17 -O2 -Wall -Wextra -Werror tools/generate_unicode_names.c -o build/generate_unicode_names
  build/generate_unicode_names --self-test
  build/generate_unicode_names UnicodeData.txt NameAliases.txt 18.0.0 "https://www.unicode.org/Public/18.0.0/ucd/ (UnicodeData.txt sha256 $(sha256sum UnicodeData.txt | cut -d' ' -f1), NameAliases.txt sha256 $(sha256sum NameAliases.txt | cut -d' ' -f1))" > c_unicode_names.inc.new
  ```

  The generator succeeds only when it has read both inputs whole, taken every
  name and refused no range of names made by rule it doesn't know, and written
  and flushed the whole table; `--self-test` checks this against a synthetic
  input, a write to `/dev/full`, an unreadable input and malformed lines. The
  output's sha256 is
  `d6e1a03969e43939f9e846f4406f0ca284ef14e634435f6fc55391ce803900f3`: 35,832
  stored names in 283,451 bytes, 22 ranges, 740 printable and 21 formatting
  ranges.
- **Routines:** `src/buster/compiler/c_unicode_names.c` is a C translation of
  LLVM routines, which keeps the LLVM Project's `Apache-2.0 WITH
  LLVM-exception` terms ([LICENSES/LLVM.txt](LICENSES/LLVM.txt)), and its
  header says so: the exact, loose and nearest-name matching of
  `llvm/lib/Support/UnicodeNameToCodepoint.cpp` (its `startsWith`, the Hangul
  syllables' jamo table and `findSyllable`, the names made by rule, and
  `nearestMatchesForCodepointName`'s edit distance and order), and
  `EscapeStringForDiagnostic` of `clang/lib/Basic/Diagnostic.cpp`, at
  llvm-project `2875c6073a7e`. The local changes are a different storage (the
  front-coded table above, searched by loose key, where LLVM walks a trie),
  bounded slices, single-exit control flow and the diagnostics' assembly.
- **Behavior references:** `clang/lib/Lex/LiteralSupport.cpp`
  (`ProcessNamedUCNEscape`, `DiagnoseInvalidUnicodeCharacterName`,
  `CharLiteralParser`), `clang/lib/Lex/Lexer.cpp` (`tryReadNamedUCN`,
  `tryReadUCN`, `tryConsumeIdentifierUCN`) and
  `clang/include/clang/Basic/DiagnosticLexKinds.td`, at the same commit, were
  read for the rules and wording of the lexer, preprocessor and checker code
  that reads named UCNs (`c_lexer.c`, `c_refine.c`, `c_preprocessor.c`,
  `c_preprocessor_impl.c`,
  `c_check_literals.c`, `c_check_literal_diagnostic.c`, `c_checker_driver.c`),
  which is original project code under the default terms; no implementation
  was copied there. Clang 23.1.6 runs externally as the oracle of the tests,
  whose fixtures are original.

## RAD Debugger

The lane interface/base-layer reference is
[RAD Debugger at `62b715a30e5da6b8d7e02372a8e7458feab1df97`](https://github.com/EpicGames/raddebugger/tree/62b715a30e5da6b8d7e02372a8e7458feab1df97),
the upstream revision preceding the October 19 lane implementation.
The reviewed paths are `src/base/base_thread_context.{h,c}`,
`src/base/base_math.c`, `src/base/base_arena.{h,c}`,
`src/radbin/radbin.c` and `src/rdi_from_pdb/rdi_from_pdb.c`.
They carry **Copyright (c) Epic Games Tools** and MIT terms, retained verbatim
in [LICENSES/RAD-Debugger.txt](LICENSES/RAD-Debugger.txt).

Local commit `f72863b` and [PR #72](https://github.com/buster14a/buster/pull/72)
identify that reference. Lane metadata, scratch arenas, modulo task assignment
and equal-range partitioning follow those interfaces/patterns. The local
barrier, one-barrier broadcast, caller-as-lane-zero startup, bounded arena
allocation, cost partitioning and `mwaitx`/futex waiting differ substantially.
Keep the upstream MIT notice and provide the entire modified `lane.c` under
MIT instead of requiring a disputed line-by-line originality boundary.

The original implementation consulted `master` without retaining a commit
hash. The pin above is the historical source comparison used in this review;
it is not presented as a recovered original checkout identifier. Both the
referenced interface and inspected implementation have the same MIT grant.

## Encoder, tools and other generated material

[PR #14](https://github.com/buster14a/buster/pull/14), introducing the
encoder in `55f05ac`, explicitly records that no code was copied from
`buster14a/bloat-buster`'s historical encoder at
`e9b3dea9429996c7ad492d80a4923e75126ada87`. That implementation was an external
comparison, not a vendored component.

The early encoder document proposed generating a future table from the
original Buster catalogue. The subsequent implementation uses handwritten
X-macro rows/families, checked against GNU as. The reviewed tree contains
neither a XED/uops.info data projection nor that catalogue. Issue
[#17](https://github.com/buster14a/buster/issues/17) uses XED and
uops.info's March 29, 2026 list for coverage research; this does not itself
establish a copied implementation. GNU as 2.47's output bytes/relocations are
test oracles for the locally authored streams; no GNU as implementation is
included. Future generated imports require a fresh input/notice review.

`link_program.c` emits original synthetic support routines and fixtures;
the executable's hash constants are generated from SplitMix64, not imported
test corpora. Recognizable SplitMix64 arithmetic also appears in link/lane/
executable tests and benchmarks, in the dataflow generator's seed mixer, and in
the type table's tests and benchmark. Its finalizer also ends `link.c`'s name
hash and `c_types.c`'s key hash. The public-domain reference is
[Sebastiano Vigna's 2015 implementation](https://prng.di.unimi.it/splitmix64.c);
its dedication, permission and disclaimer are retained in
[LICENSES/SplitMix64.txt](LICENSES/SplitMix64.txt). This does not import the
Java implementation referenced by that source. No runtime or system header
package is vendored at this revision.

The concrete older-Buster import is `.claude/settings.json`, recorded by
`02a1ad65e157c9cb1affc0293c7ab4a6fe21fe0b`. Its allowlist matches the maintainer's
configuration at Buster commit `5180a65a57ef19e9a2b5251eb33356e57b7120e1`, with
the repository name changed and local Tailscale permissions added. This is
maintainer configuration under his confirmed contribution authority, not an
import of the older compiler's frameworks or implementation.

## wyhash's constants

`c_intern.c`'s identifier hash uses the four 64-bit constants of wyhash's
default secret, `_wyp` in `wyhash.h`, as the whitening of its lanes and the
round key of its chain of AES rounds
(`0xa0761d6478bd642f`, `0xe7037ed1a0b428db`, `0x8ebc6af09c88c6e3`,
`0x589965cc75374cc3`). They are as in
[wangyi-fudan/wyhash at `e92bc3e9adf862aad364ff74a24ef2cae854b0b1`](https://github.com/wangyi-fudan/wyhash/tree/e92bc3e9adf862aad364ff74a24ef2cae854b0b1),
the last revision that has them (later ones changed the default secret). Only
the constants are used, not wyhash's code. wyhash is released into the public
domain under the Unlicense; its text at that revision is retained in
[LICENSES/wyhash.txt](LICENSES/wyhash.txt).

## Clang's keywords

`c_intern.c`'s keyword list (`BUSTER_C_KEYWORDS`) is generated data:
- **Generator:** clang 23.1.3, the local package `clang 23.1.3-2` for target
  `x86_64-pc-linux-gnu`.
- **Inputs:** every identifier named in the entries of that package's
  `TokenKinds.def` and `RegularKeywordAttrInfo.inc` (`/usr/include/clang/Basic`).
  Only the names were read.
- **Procedure:** each name's `__is_identifier` was asked of
  `clang -E -P -std=MODE` in the ten modes from `c89` to `gnu23`. The list keeps
  the 148 names it answers 0 for in some mode, with the modes as one of six
  rules. `docs/c-refinement.md` records this, and LLVM's terms are retained in
  [LICENSES/LLVM.txt](LICENSES/LLVM.txt).

The same answers are behind `c_refine_test.c`'s spellings chosen apart.
`c_preprocessor_test.c` keeps the keyword candidates it asks clang about again
under `--clang` (`pp_test_keyword_candidates`): the spellings among those
names, with the ones of `TransformTypeTraits.def`, `OpenCLImageTypes.def` and
`HLSLIntangibleTypes.def` from the same directory. Only the names were read.

## Clang-generated predefines and feature tables

`src/buster/compiler/c_preprocessor_impl.c`'s predefined macros
(`pp_predefines_base`, `pp_predefines_c23`) and
`src/buster/compiler/c_preprocessor.c`'s feature tables (`pp_features_*`) are
generated data, the output of **clang 23.1.3** (the local package `clang 23.1.3-2`, target
`x86_64-pc-linux-gnu`), which the configuration decided in #117 takes as the
compiler's own. LLVM's terms are retained in [LICENSES/LLVM.txt](LICENSES/LLVM.txt).

- The predefines are `clang -dM -E -x c /dev/null` (gnu17), without the macros
  the language mode and options decide, and the difference of its output
  under `-std=c23`. `__BUSTER__` is the compiler's own.
- The feature tables are clang's nonzero answers, under `-std=gnu17` and
  `-std=c23` (the same), to `__has_attribute`, `__has_builtin`,
  `__has_c_attribute` and `__has_warning`. The names asked about are those that
  `/usr/include` (glibc 2.44), clang 23's resource headers, musl 1.2.5, SQLite
  3.53's amalgamation and Zig 0.14.1's `lib/libc` query, and a list of common
  GNU attribute names. Only the names were read from those inputs.
- The `__has_feature` and `__has_extension` tables are clang's nonzero answers
  for every `FEATURE` and `EXTENSION` name of clang 23.1.3's
  `clang/include/clang/Basic/Features.def`, in the ten modes from `-std=c89`
  to `-std=gnu23`, with the first standard of each answer that depends on the
  mode. `c_preprocessor_test.c` keeps the same names (`pp_test_feature_names`)
  to ask clang again under `--clang`. Only the names were read from
  `Features.def`.
- The `__has_constexpr_builtin` table is clang's nonzero answers, under
  `-std=gnu17` and `-std=c23` (the same), for the names of the `__has_builtin`
  table.
- A local script ran the queries; `docs/c-preprocessor.md` records its
  procedure.
- `c_predefines.c`'s generated block (`predefines_features`, `predefines_cpus`,
  `predefines_tunes`) is clang 23.1.3's answers, printed by
  `./build/compiler test-preprocessor --generate-targets` (`c_predefines_test.c`):
  the x86-64 CPUs of `-march` and `-mtune` from clang's errors, the features and
  their closures from the IR's `"target-features"` of each CPU and flag, each
  feature's own macros from `clang -dM -E` with exactly its features, and each
  CPU's macro name from its `__tune_*__`. Its long double, wide character and
  `TARGET_OS_*` texts are `clang -dM -E`'s for `-mlong-double-*`,
  `-fshort-wchar` and `-fdefine-target-os-macros`.

SQLite, musl, Lua and the system's headers are test oracle inputs only (the
tests read them where they are installed): none of their code is included, but
for the copies of musl's code in the history of #929, which the next section
describes.

The `ext_vector_type` attribute-table entry was added for the frozen #125
corpus after querying Clang 23.1.1 (the recorded 9700X reference) in C99,
GNU17 and C23, and Clang 23.1.3 (local package `clang 23.1.3-5`) in GNU17.
Only the attribute name was read from the corpus. The original regression
inputs and queries are described in `docs/preprocessor-oracle.md`; the entry
retains the existing LLVM terms. The pragma-printer correction follows
observable Clang 23.1.1 behavior, with its source references recorded there;
no Clang or corpus implementation is copied.

## musl, in #929's history (#147)

[#929](https://github.com/buster14a/buster/pull/929)'s first commits
(`dfb9aca9`, `659883cf` and `6a210867`, and `07a5ae89` after them) held native
fixtures in `src/buster/compiler/c_check_program_asm_test.c` that copied musl
1.2.5's code, as Zig 0.14.1's `lib/libc` tree has it (the frozen corpus of
[the preprocessor oracle](docs/preprocessor-oracle.md)), with local changes
(names, test harness, compared results):

- `arch/x86_64/atomic_arch.h`'s `a_cas`, `a_cas_p`, `a_swap`, `a_fetch_add`,
  `a_inc`, `a_dec`, `a_and`, `a_or`, `a_and_64`, `a_or_64`, `a_store`,
  `a_spin`, `a_crash`, `a_ctz_64` and `a_clz_64`;
- `arch/x86_64/pthread_arch.h`'s `__get_tp`;
- `arch/x86_64/syscall_arch.h`'s `__syscall0`, `__syscall2`, `__syscall4`,
  `__syscall5` and `__syscall6`;
- `src/math/x86_64`'s `fabs`, `fabsf`, `fabsl`, `fma`, `fmaf`, `sqrt`,
  `sqrtf`, `sqrtl`, `rintl`, `fmodl`, `remainderl`, `remquol`, `lrint`,
  `lrintf`, `lrintl` and `llrintl`.

musl is **Copyright © 2005-2020 Rich Felker, et al.**, under the standard MIT
license, and its x86_64 port, by Nicholas J. Kain, is under the same terms.
Its `COPYRIGHT` file at that release (SHA-256
`f9bc4423732350eb0b3f7ed7e91d530298476f8fec0c6c427a1c04ade22655af`, the same
in Zig 0.14.1's tree) is retained verbatim as
[LICENSES/musl.txt](LICENSES/musl.txt), for those copies, which the pull
request's history keeps. They are not first-party material.

`ae10833c` replaced them with programs of the test's own in the shapes of
musl's: they exercise the same instructions, constraints and operand shapes,
and copy no function of musl's. No file of this tree holds musl's code.

## C semantic compatibility references

The TLS redeclaration diagnostics and for-initializer unused-warning scope fixes
for #1784 are original project code and fixtures under the default terms.
Clang/LLVM revision `7a941dfb2877a0b8cf30323fefa85ad019e0b0f5`'s
`clang/lib/Sema/SemaDecl.cpp` and `clang/include/clang/Basic/DiagnosticSemaKinds.td`
supply diagnostic precedence and wording facts; Clang runs externally as the
fixture oracle. No upstream implementation or test fixture is incorporated.

The type-name prototype name tree and its original generated, nested-scope,
ordering and guarded membership fixtures (#604) use the project default terms.
Clang 23.1.1 and 23.1.4 run externally as acceptance and diagnostic oracles;
no external implementation, data or fixtures are incorporated.

Naked-function constraints in `c_check_naked_input.c` and their original
fixtures use Clang/LLVM `llvmorg-23.1.1`'s `SemaDecl.cpp`,
`SemaStmtAsm.cpp` and `Expr.cpp`'s type-trait child rules as behavior references,
checked with Clang 23.1.4 probes.
No LLVM implementation or fixtures were copied; the new code and fixtures
use the project default terms.

The original GNU integer folding helper in `c_check_integer_extension.c`
and its independent generated fixtures use observed Clang 23.1.1 and
23.1.3 behavior. The rules were checked against
`clang/lib/AST/ExprConstant.cpp` at LLVM tags `llvmorg-23.1.1` and
`llvmorg-23.1.3`. Switch constraint diagnostic ordering was checked against
`clang/lib/Sema/SemaStmt.cpp` and original compiled probes. These files
were read as behavior references; no LLVM implementation was copied.

Loop-hint diagnostic ordering, directive names and locations in
`c_check_loop_input.c` use original probes checked against Clang 23.1.4 and
`clang/lib/Sema/SemaStmtAttr.cpp` at `llvmorg-23.1.3` as behavior references.
No LLVM implementation or test fixtures were copied.
The diagnostic-only control prefix and `&&label` typing also use original
Clang probes and `SemaExpr.cpp` at that tag as behavior references, with no
implementation copied.

Wide-enum completion follows observed Clang behavior and the rules in
`clang/lib/Sema/SemaDecl.cpp` at `llvmorg-23.1.1`. Diagnostic warning-group
relationships were checked against that tag's
`clang/include/clang/Basic/DiagnosticGroups.td`, with original pragma fixtures.
The original `c_builtin_intrinsic_types.c` decoder and tests use the same tag's
`ASTContext.cpp`, `Basic/Targets/X86.h` and `BuiltinsBase.td` as descriptor-format
references. These modules contain no copied LLVM implementation or fixtures.

The original packed-member address diagnostic (`c_check_packed_input.c`,
`c_check_packed_diagnostic.c`) follows behavior described by `SemaChecking.cpp`
and `SemaExpr.cpp`, and the transparent-expression rules in `AST/Expr.cpp`,
at `llvmorg-23.1.3`. Its original differential probes in
`c_check_packed_test.c` run Clang externally as a diagnostic oracle. No LLVM
implementation, diagnostic table or fixture was copied.

## Review limits and distribution

The record covers the pinned tree and this adoption's changes. Unmerged PRs,
future imports and external system libraries are not silently covered.
Unrecorded original tool hashes are identified above; the retained compatible
component terms are the disposition for those portions, rather than an
assertion that they are entirely first-party.

Preserve source attribution/modification notices and distribute the applicable
license and notice files as described in [LICENSING.md](LICENSING.md).
Excluding something from the first-party grant would not by itself authorize
redistributing it. Any later discovery of other incorporated material must be
resolved before claiming coverage for it.

### Feature-query integration (#806)

`c_builtin_source_intrinsics.c`, `c_builtin_x86_rows.inc`,
`c_builtin_x86_leaves.inc` and `c_preprocessor_feature_test.c` under
`src/buster/compiler/` are original project material under the default license.
The two `.inc` inventories relocate the existing first-party native lowering
and helper records without importing implementation. The intrinsic query also
instantiates the already-reviewed
`generated/clang_builtin_own_main_23_1_1.inc` name inventory; that file's LLVM
provenance and license remain unchanged. No new upstream data was imported.

### Implicit library declarations (#804)

The original `c_check_implicit_input.c`, `c_check_implicit_library_test.c` and
related declaration, option and diagnostic changes use the existing frozen
Clang 23.1.1 builtin catalogue; its provenance and licensing are unchanged.
Clang 23.1.4 was run externally on original fixtures as a diagnostic, type
and execution oracle. `Builtins.cpp`, `SemaDecl.cpp`, `ASTContext.cpp`,
`CompilerInvocation.cpp` and driver `Clang.cpp` at `llvmorg-23.1.4` were read
as behavior references. No LLVM implementation or test fixtures were copied.

### The CPU model's names and values (#148)

`src/buster/compiler/c_builtin_cpu.inc` is derived LLVM data, not first-party
material: the names `__builtin_cpu_supports` and `__builtin_cpu_is` take, and
the CPU model's bits and values clang's code tests them against. It keeps the
LLVM Project's `Apache-2.0 WITH LLVM-exception` terms, whose text is
[LICENSES/LLVM.txt](LICENSES/LLVM.txt), and its header says so.

- **Input:** `llvm/include/llvm/TargetParser/X86TargetParser.def`, as
  installed by apt.llvm.org's `llvm-23-dev`
  `1:23.1.5~++20261028083854+a738f2737189-1~exp1~20261028203912.92` (LLVM
  23.1.5, built from llvm-project commit `a738f2737189`), at
  `/usr/include/llvm-23/llvm/TargetParser/X86TargetParser.def`, sha256
  `cc59e275156d08b658cf23bc950237ee288a54477b16c08b4cbb00242fa60f50`.
- **Rows taken:** the strings and ABI values of its `X86_FEATURE_COMPAT` and
  `X86_MICROARCH_LEVEL` rows (a feature's word is the value divided by 32, its
  bit the remainder), and of its `X86_VENDOR`, `X86_CPU_TYPE` and
  `X86_CPU_SUBTYPE` rows, aliases included. The rows keep the input's order,
  the features' first, and nothing else of it is copied.
- **Generator:** `tools/generate_x86_cpu_model.c`, which is original project
  code under the default terms. It was run as:

  ```sh
  cc -std=c17 -O2 -Wall -Wextra -Werror tools/generate_x86_cpu_model.c -o build/generate_x86_cpu_model
  build/generate_x86_cpu_model --self-test
  build/generate_x86_cpu_model /usr/include/llvm-23/llvm/TargetParser/X86TargetParser.def "llvm-23-dev 1:23.1.5~++20261028083854+a738f2737189-1~exp1~20261028203912.92 (apt.llvm.org)" "$(sha256sum /usr/include/llvm-23/llvm/TargetParser/X86TargetParser.def | cut -d' ' -f1)" > c_builtin_cpu.inc.new
  ```

  The generator succeeds only when it has read the whole input and written
  and flushed the whole table. `--self-test` checks this against a synthetic
  input, a write to `/dev/full`, an unreadable input and a malformed row.
  The output's sha256 is
  `fe9cf559b3168f24d27e2a228c980af2c0a4b0bab8d552b37422f9ffb47588d7`. Its 183
  rows are byte-identical to the table that first came in with #148. That
  table was read from an earlier clang 23.1's code for a call of each name,
  with no record of its exact inputs.
- **Oracle:** clang runs externally in `test-checker-driver`
  (`check_test_cpu_model_runs` in `c_check_library_driver_test.c`). It checks
  every row's test against clang's code over every bit and value of the
  model. Its use as an oracle copies none of its code.

### clang's CPU dispatch table (#1447)

`src/buster/compiler/c_check_cpu_dispatch.inc` is derived LLVM data, not
first-party material: the processor names clang's `cpu_specific` and
`cpu_dispatch` attributes take (`X86TargetInfo::validateCPUSpecificCPUDispatch`),
each with its mangling character (`CPUSpecificManglingCharacter`), the mask of
libgcc's CPU model a resolver tests it with (`llvm::X86::getCpuSupportsMask` of
its features that `validateCpuSupports` takes) and its features
(`getCPUSpecificCPUDispatchFeatures`, `llvm::X86::getFeaturesForCPU`). It keeps
the LLVM Project's `Apache-2.0 WITH LLVM-exception` terms, whose text is
[LICENSES/LLVM.txt](LICENSES/LLVM.txt), and its header says so.

- **Inputs:** LLVM `llvmorg-23.1.4` (commit
  `7a941dfb2877a0b8cf30323fefa85ad019e0b0f5`):
  `llvm/lib/TargetParser/X86TargetParser.cpp`, sha256
  `4ee870468b01b104e04dd08375285f790888f3428d41f3994236e49330c8b721`, and
  `llvm/include/llvm/TargetParser/X86TargetParser.def`, sha256
  `cc59e275156d08b658cf23bc950237ee288a54477b16c08b4cbb00242fa60f50` (the same
  bytes as apt.llvm.org's `llvm-23-dev` 1:23.1.5 installs).
- **Rows taken:** each `ProcessorDefs` row whose name is a C identifier, in its
  order: its name, its mangling character (0 where it is `'\0'`) and its
  feature set, an expression of the file's `constexpr FeatureBitset` constants
  that the generator evaluates; the `.def`'s `X86_FEATURE` and
  `X86_FEATURE_COMPAT` rows give the features' names, in their order, and the
  `COMPAT` rows' ABI values the masks. `64bit` is left out of the features, as
  `getFeaturesForCPU` leaves it out. Nothing else of either file is copied.
- **Generator:** `tools/generate_x86_cpu_dispatch.c`, which is original project
  code under the default terms. It was run as:

  ```sh
  cc -std=c17 -O2 -Wall -Wextra -Werror tools/generate_x86_cpu_dispatch.c -o build/generate_x86_cpu_dispatch
  build/generate_x86_cpu_dispatch --self-test
  build/generate_x86_cpu_dispatch X86TargetParser.def X86TargetParser.cpp "llvm-project llvmorg-23.1.4, commit 7a941dfb2877a0b8cf30323fefa85ad019e0b0f5" "$(sha256sum X86TargetParser.def | cut -d' ' -f1)" "$(sha256sum X86TargetParser.cpp | cut -d' ' -f1)" > c_check_cpu_dispatch.inc.new
  ```

  The generator succeeds only when it has read both inputs whole, evaluated
  every feature set and processor row, and written and flushed the whole
  table. `--self-test` checks this against synthetic inputs, a write to
  `/dev/full`, an unknown feature in an expression and a malformed row. The
  output's sha256 is
  `ec89b0b786f3739bde700a0f0425677f9020dd13ed90673e4d98bfcc428f3738`, 105 rows.
- **Oracle:** clang runs externally in `test-checker-driver`
  (`c_check_cpu_dispatch_driver_test.c`): its objects' symbols, and its
  resolver's choice over 28 CPU models, against this compiler's. Its use as an
  oracle copies none of its code.

### GNU multiversioning's target table (#1838)

`src/buster/compiler/c_check_target_versions.inc` is derived LLVM data, not
first-party material: the features a version of GNU function multiversioning
may name (the `X86_FEATURE_COMPAT` rows of a nonzero priority, which
`X86TargetInfo::getFMVPriority` gives), each with its priority and its bit of
libgcc's CPU model; the `X86_MICROARCH_LEVEL` rows' bits; and each processor
`llvm::X86::parseArchX86` finds, with its key feature's priority. It keeps the
LLVM Project's `Apache-2.0 WITH LLVM-exception` terms, whose text is
[LICENSES/LLVM.txt](LICENSES/LLVM.txt), and its header says so.

- **Inputs:** LLVM `llvmorg-23.1.4` (commit
  `7a941dfb2877a0b8cf30323fefa85ad019e0b0f5`):
  `llvm/include/llvm/TargetParser/X86TargetParser.def`, sha256
  `cc59e275156d08b658cf23bc950237ee288a54477b16c08b4cbb00242fa60f50` (the same
  bytes as apt.llvm.org's `llvm-23-dev` 1:23.1.5 installs, which were read), and
  `llvm/lib/TargetParser/X86TargetParser.cpp`, sha256
  `4ee870468b01b104e04dd08375285f790888f3428d41f3994236e49330c8b721`.
- **Rows taken:** each `X86_FEATURE_COMPAT` row's string, priority and ABI
  value where its priority isn't 0; each `X86_MICROARCH_LEVEL` row's string and
  ABI value; and the name of each `ProcessorDefs` row that isn't only
  `cpu_specific`'s and `cpu_dispatch`'s (the first of each name), with its key
  feature's priority from the `.def` (-1 for `~0U` or a feature of no compat
  row). Nothing else of either file is copied.
- **Generator:** `tools/generate_x86_multiversion.c`, which is original project
  code under the default terms. It was run as:

  ```sh
  cc -std=c17 -O2 -Wall -Wextra -Werror tools/generate_x86_multiversion.c -o build/generate_x86_multiversion
  build/generate_x86_multiversion --self-test
  build/generate_x86_multiversion X86TargetParser.def X86TargetParser.cpp "llvm-project llvmorg-23.1.4, commit 7a941dfb2877a0b8cf30323fefa85ad019e0b0f5" "$(sha256sum X86TargetParser.def | cut -d' ' -f1)" "$(sha256sum X86TargetParser.cpp | cut -d' ' -f1)" > c_check_target_versions.inc.new
  ```

  The generator succeeds only when it has read both inputs whole, parsed every
  row it takes, and written and flushed the whole table. `--self-test` checks
  this against synthetic inputs, a write to `/dev/full`, an unreadable input and
  a malformed row. The output's sha256 is
  `0fed41b66a3acb498c11a56c98cd0177ed8de8a542da8f0a2a2ed848b929d64f`, 149 rows.
- **Oracle:** clang runs externally in `test-checker-driver`
  (`c_check_target_versions_driver_test.c`). Its use as an oracle copies none
  of its code.

### Clang's feature-map order (#2093)

`src/buster/compiler/c_check_feature_map.inc` is derived LLVM data, not
first-party material: each feature of `X86TargetParser.def`'s `X86_FEATURE`
and `X86_FEATURE_COMPAT` rows in their order (`FeatureInfos`' index order),
with its name and its `ImpliedFeatures` set, and each processor of
`X86TargetParser.cpp`'s `ProcessorDefs` (the first row of each name) with its
`Features` set, both as feature indices. It keeps the LLVM Project's
`Apache-2.0 WITH LLVM-exception` terms, whose text is
[LICENSES/LLVM.txt](LICENSES/LLVM.txt), and its header says so.

- **Inputs:** llvm-project commit `c47d7823685a`, the revision of the
  `clang 23.1.6` package (`clang --version`: `++20261108033755+c47d7823685a`):
  `llvm/include/llvm/TargetParser/X86TargetParser.def`, sha256 `cc59e275156d08b658cf23bc950237ee288a54477b16c08b4cbb00242fa60f50`, and
  `llvm/lib/TargetParser/X86TargetParser.cpp`, sha256 `4ee870468b01b104e04dd08375285f790888f3428d41f3994236e49330c8b721`, as
  `raw.githubusercontent.com` serves them at that commit.
- **Rows taken:** each feature's string and its `ImpliedFeatures` expression,
  evaluated; each processor's name and its feature-set expression, evaluated.
  Nothing else of either file is copied.
- **Generator:** `tools/generate_x86_feature_map.c`, which is original project
  code under the default terms. It was run as:

  ```sh
  cc -std=c17 -O2 -Wall -Wextra -Werror tools/generate_x86_feature_map.c -o build/generate_x86_feature_map
  build/generate_x86_feature_map --self-test
  build/generate_x86_feature_map X86TargetParser.def X86TargetParser.cpp "llvm-project commit c47d7823685a (clang 23.1.6)" "$(sha256sum X86TargetParser.def | cut -d' ' -f1)" "$(sha256sum X86TargetParser.cpp | cut -d' ' -f1)" > c_check_feature_map.inc.new
  ```

  It succeeds only when it has read both inputs whole, evaluated every set it
  takes and written and flushed the whole table; `--self-test` checks this
  against synthetic inputs, a write to `/dev/full`, an unreadable input and an
  undefined name. The output's sha256 is `ab57edb2eedfe21c068c357f08b65d69e856d81c41fbe298798efe2a02ef355a`, 263 rows.
- **The emulation:** `c_check_feature_map.c` is original code under the default
  terms that follows the rules of `X86TargetInfo::initFeatureMap` and
  `setFeatureEnabled` (`clang/lib/Basic/Targets/X86.cpp`),
  `llvm::X86::updateImpliedFeatures` and `getFeaturesForCPU`, `StringMapImpl`'s
  bucket placement and growth (`llvm/lib/Support/StringMap.cpp`),
  `ASTContext::getFunctionFeatureMap`, `TargetInfo::parseTargetAttr`,
  `CodeGenFunction::checkTargetFeatures` and the driver's
  `x86::getX86TargetFeatures`, all read at the same commit; no implementation
  was copied from them.
- **xxHash:** `src/buster/compiler/xxh3_short.c` translates into C the paths of
  `llvm::xxh3_64bits` for inputs of up to 128 bytes, from
  `llvm/lib/Support/xxhash.cpp` at the same commit (sha256 `699389b388a56d0caf330bb872739b2e23d51a22b7e2f758f2c11fac9936752d`), which LLVM
  bases on xxHash commit `d5891596637d21366b9b1dcf2c0007a3edb26a9e` and keeps
  under xxHash's BSD 2-Clause License. The translation keeps that license, its
  copyright notice and its text ([LICENSES/xxHash.txt](LICENSES/xxHash.txt));
  its local changes (C, seed 0 alone, no longer inputs, `memcpy` loads, one
  return per function) are under the same terms. It was checked against
  `libLLVM-23`'s own `xxh3_64bits` on 2,000 random inputs of 0 to 128 bytes.
- **Oracle:** clang 23.1.6 runs externally in the driver tests; its use as an
  oracle copies none of its code.

### Clang's attribute subjects (#147)

`src/buster/compiler/c_check_attribute_subjects.inc` is derived LLVM data, not
first-party material: for each GNU and C23 spelling of an attribute clang checks
the subject of, the C declarations it takes, whether another is an error, and
the subjects clang's message names, by which the checker warns and ignores an
attribute on another declaration (`check_input_attribute_subjects`,
`c_check_attribute.c`); and for each spelling clang has on x86-64 Linux, its
facts: whether GCC knows it, whether it isn't C's, whether clang's generated
handler applies it alone, whether it is a type's or a statement's, whether its
arguments are parsed its own way or end in a variadic one, whether clang ignores
it everywhere, and its least and most arguments (`check_input_fact_row`). It
keeps the LLVM Project's
`Apache-2.0 WITH LLVM-exception` terms ([LICENSES/LLVM.txt](LICENSES/LLVM.txt)),
and its header says so.

- **Input:** `clang-tblgen --gen-clang-attr-parsed-attr-impl` of
  `clang/include/clang/Basic/Attr.td` at `llvmorg-23.1.5` (sha256
  `6014bff340878cf3c656bf3d1ccae64cfd33615d8245c4c5472d10ffd4dc6a1d`, with the
  files it includes), by apt.llvm.org's `clang-tools-23`
  `1:23.1.5~++20261028083854+a738f2737189-1~exp1~20261028203912.92`: clang's
  `AttrParsedAttrImpl.inc`. Its `diagAppertainsToDecl` functions, its
  `ParsedAttrInfo` records' fields (`NumArgs`, `OptArgs`, `HasCustomParsing`,
  `IsTargetSpecific`, `IsType`, `IsStmt`, `IsKnownToGCC`), its
  `acceptsLangOpts`, `existsInTarget`, `handleDeclAttribute` and
  `diagMutualExclusion` functions, and their spellings were read; nothing else
  was taken. Also
  `clang-tblgen --gen-clang-attr-parsed-attr-kinds` of the same file, by the
  same tool: clang's `AttrParsedAttrKinds.inc`, whose `IgnoredAttribute`
  spellings were read.
- **Generator:** `tools/generate_attribute_subjects.c` (original, Default)
  maps each subject predicate to the C declarations it takes. Its term table is
  written from the predicates' definitions in `Attr.td` (`GlobalVar`,
  `SharedVar`, `NonParmVar`, `FunctionPointer`, `HasFunctionProto` and the
  others), read for their rules. It writes 304 GNU rows and 225 C23 rows, each
  list in byte order, and fails on a predicate it doesn't know. For the facts,
  it evaluates `existsInTarget`'s conditions for x86-64 Linux by a table of
  their predicates, and fails on one it doesn't know; it writes 366 GNU rows
  and 283 C23 rows. It writes the 44 pairs of mutually exclusive attributes
  that both exist on x86-64, by GNU spelling, and fails on an excluded
  attribute of no `ParsedAttrInfo`. The file's sha256 is
  `f5a94de20e722ba50b65ae1a622c76e20364575edb4691d3a798436a4396817e`.
- **Not from the table:** `sentinel`'s and `transparent_union`'s subjects,
  which clang's handlers check themselves, and `nonstring`'s type check and
  `transparent_union`'s field checks, are rows and code of `c_check_attribute.c`
  and `c_check_input.c`, written from `SemaDeclAttr.cpp`'s
  `handleSentinelAttr`, `handleTransparentUnionAttr` and `handleNonStringAttr`
  at `llvmorg-23.1.5`, read for their rules. The vector description in
  messages follows `ASTDiagnostic.cpp`'s `ConvertTypeToDiagnosticString`, read
  the same way. So do, at the same tag, `handleNonNullAttr` and
  `checkFunctionOrMethodParameterIndex` (`SemaDeclAttr.cpp`), the type places
  of `processTypeAttrs` and `handleFunctionTypeAttr` (`SemaType.cpp`), the
  specifiers' check and the GCC-position warning of `ParseDecl.cpp` and
  `Parser.cpp`, and the x86-64 target's `checkCallingConvention`; and
  `BuildVectorType` (`SemaType.cpp`), `ActOnTag` and
  `checkNewAttributesAfterDef` (`SemaDecl.cpp`), and `ParseClassSpecifier`
  (`ParseDeclCXX.cpp`), for tags' attributes, type names' and `vector_size`'s;
  and `DiagnoseUseOfDecl` with its availability checks (`SemaAvailability.cpp`)
  for deprecated and unavailable uses. None of that code is copied.
- **Procedure:** [the checker guide](docs/c-checker.md)'s "Attributes on
  declarations they don't apply to (#147)".

### Clang's attribute vocabulary (#147)

`src/buster/compiler/c_check_attribute_clang.inc` is derived LLVM data, not
first-party material: the spellings of the attributes clang knows in C, by which
the checker ignores the others with clang's `-Wunknown-attributes` warning
(`c_check_attribute.c`). It keeps the LLVM Project's
`Apache-2.0 WITH LLVM-exception` terms, whose text is
[LICENSES/LLVM.txt](LICENSES/LLVM.txt), and its header says so.

- **Generator:** clang 23.1.5 for `x86_64-pc-linux-gnu`, apt.llvm.org's
  `clang-23` and `libclang-cpp23`
  `1:23.1.5~++20261028083854+a738f2737189-1~exp1~20261028203912.92` (built from
  llvm-project commit `a738f2737189`).
- **Inputs:** the candidate names were the identifier-shaped strings of that
  package's `libclang-cpp.so.23.1`, without a keyword's surrounding
  underscores, without the preprocessor's own operators (`__has_*`,
  `defined`, `_Pragma`, `__is_target_*` and the like) and without the
  function-like macros clang predefines. Only the strings were read.
- **Rows taken:** each candidate whose `__has_attribute` clang answers
  nonzero under `-std=gnu17` (366 GNU names), and each spelling, plain or
  scoped by `gnu::`, `clang::` or `msvc::`, whose `__has_c_attribute` clang
  answers nonzero under `-std=gnu23` (283 C23 spellings), each list in byte
  order. [The checker guide](docs/c-checker.md)'s "Attributes clang doesn't
  know (#147)" records the procedure.
- **Spelling list:** the 539 names and 9 scopes of every spelling in
  `clang/include/clang/Basic/Attr.td` at `llvmorg-23.1.5` (sha256
  `6014bff340878cf3c656bf3d1ccae64cfd33615d8245c4c5472d10ffd4dc6a1d`, with
  `AttrDocs.td`, `ASTNode.td`, `DeclNodes.td` and `StmtNodes.td` it includes),
  as `clang-tblgen --gen-attribute-spelling-list` of apt.llvm.org's
  `clang-tools-23` of the same version writes them (clang's
  `AttributeSpellingList.inc`), in its order, for clang's "did you mean"
  suggestion. Only the names were taken. The file's sha256 is
  `3eb1138c3a5692e91dc3a1b6a03afbbc0d6b94f76b4b732896eac951967bd218`.
- **Statement attributes:** the parser's C23 statement-attribute spellings
  (`parse_scalar_standard_statement_attribute`) are those of `Attr.td`'s
  statement attributes, read from `clang-tblgen --gen-clang-attr-parsed-attr-impl`'s
  output (names only).
- **Cross-check:** the lists were compared with `clang/include/clang/Basic/Attr.td`
  at `llvmorg-23.1.1`, read for its spellings and their targets, none of it
  copied: the GNU list is the spelling list's names whose `__has_attribute` is
  nonzero, and asking `__has_c_attribute` of them under every scope adds no C23
  spelling. clang's `SimpleTypoCorrection.cpp`, `Attributes.cpp` and
  `SemaDeclAttr.cpp`'s `DiagnoseUnknownAttribute`, and its parser's
  `TryParseCXX11AttributeIdentifier` and `ParseStatementOrDeclaration`, at
  `llvmorg-23.1.1`, were read for their rules, which `c_check_warning_render.c`
  and `c_parser_impl.c` write again in C. `test-checker-driver` asks the clang it runs every row again
  (`check_test_driver_unknown_attributes` in
  `c_check_label_attribute_driver_test.c`), which copies none of its code.
