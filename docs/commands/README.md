# Command and module guides

Read only the guide for the area you are working on. [AGENTS.md](../../AGENTS.md)
contains the shared rules, build entry point, host-use requirements and smoke
commands. These guides hold the detailed command contracts and module status
previously embedded there; existing design documents remain authoritative for
design. Edit the owning guide when adding flags, tests or benchmarks. Do not
append another repository-wide inventory to the root agent instructions.

Commands run from the repository root. Source entry points below are relative
to `src/buster/compiler/`, except paths beginning with `tools/`. A guide also
names its command-specific test and benchmark files. The commands in this
table are entry points, not an exhaustive list of every subtest or flag.

## By module

| Commands / area | Source entry point | Guide |
| --- | --- | --- |
| `test-assembler, benchmark-assembler` | `assembler.c` | [assembler](assembler.md) |
| `test-bind, benchmark-bind` | `bind.c` | [bind](bind.md) |
| `test-bindings, test-parser-bindings` | `c_bindings.c` | [bindings](bindings.md) |
| `test-branch-sizing, benchmark-branch-sizing` | `branch_sizing.c` | [branch-sizing](branch-sizing.md) |
| `./build.sh, bake-tables, test-baked-tables` | `baked.c` | [build](build.md) |
| `test-builtins, test-check-builtins, test-builtin-lowering, measure-helpers` | `c_builtin.c`, `helpers_measure.c` | [builtins](builtins.md) |
| `check-call-graph, test-call-graph` | `call_graph.c` | [call-graph](call-graph.md) |
| `test-calling-convention, benchmark-calling-convention` | `calling_convention.c` | [calling-convention](calling-convention.md) |
| `check, test-checker, test-checker-driver` | `c_checker.c` | [checker](checker.md) |
| `test-checker --requests` (request configuration, source preparation, option IDs) | `c_check_request.c`, `c_option_table.c` | [checker](checker.md) |
| `test-checker-asm` | `c_check_program_asm.c` | [checker-asm](checker-asm.md) |
| `test-checker-corpus` | `c_check_corpus.c` | [checker-corpus](checker-corpus.md) |
| `test-check-debug, benchmark-check-debug` | `c_check_debug.c` | [check-debug](check-debug.md) |
| benchpress (the 9700X) over the tailnet | `.claude/hooks/join-tailnet.sh` | [benchpress](benchpress.md) |
| agent memory in cloud sessions | `.claude/hooks/llm-memory.sh` | [llm-memory](llm-memory.md) |
| hooks of sessions started beside the checkout | `.claude/hooks/parent-folder.sh` | [llm-memory](llm-memory.md#sessions-that-start-beside-the-checkout) |
| `correctness.tests, costs / CI selection` | `tools/ci/correctness.tests`, `tools/ci/costs.c` | [ci](ci.md) |
| `rerun / setup failures re-run once` | `tools/ci/rerun.c`, `tools/ci/rerun.sh` | [ci](ci.md#setup-failures-re-run-once) |
| `measure-compile, --time-stages` | `c_check_clock.c` | [compile-time](compile-time.md) |
| `allcore counted, sampled, table, compare` (the all-core gauge) | `tools/allcore/allcore.c` | [all-core](all-core.md) |
| `test-c-testsuite, test-sqlite, test-sqlite-testfixture` | `c_testsuite.c`, `sqlite_test.c`, `sqlite_testfixture.c` | [c-testsuite](c-testsuite.md) |
| `dco status, audit, compose` | `tools/dco/dco.c` | [dco](dco.md) |
| `test-dataflow, benchmark-dataflow` | `dataflow.c` | [dataflow](dataflow.md) |
| `test-debug, benchmark-debug` | `debug.c` | [debug](debug.md) |
| `test-checker-deferred, benchmark-checker-deferred` | `c_check_deferred_test.c` | [deferred](deferred.md) |
| `test-diagnostics, benchmark-diagnostics` | `diagnostic.c` | [determinism](determinism.md) |
| `test-differential, test-differential-harness` | `differential.c` | [differential](differential.md) |
| `test-dominators, benchmark-dominators` | `dominators.c` | [dominators](dominators.md) |
| `test-emission-metadata` | `emission_metadata.c` | [emission-metadata](emission-metadata.md) |
| `-E, parse, -fsyntax-only, -c` | `main.c` | [driver](driver.md) |
| `test-emitted-code` | `emitted_code.c` | [emission](emission.md) |
| `test-executable, test-dynamic, test-static` | `executable.c` | [executable](executable.md) |
| `test-files, benchmark-files` | `c_files.c` | [files](files.md) |
| `benchmark-floors, test-floors-input` | `floors_benchmark.c` | [floors](floors.md) |
| `test-foreign, benchmark-foreign` | `foreign.c` | [foreign](foreign.md) |
| `test-foreign-corpus` | `foreign_corpus_test.c` | [foreign-corpus](foreign-corpus.md) |
| `link-foreign (DWARF)` | `foreign_debug.c` | [foreign-debug](foreign-debug.md) |
| `link-foreign` | `foreign_driver.c` | [foreign-driver](foreign-driver.md) |
| `benchmark-foreign-gc` | `foreign_gc.c` | [foreign-gc](foreign-gc.md) |
| `benchmark-foreign-strings` | `foreign_strings.c` | [foreign-strings](foreign-strings.md) |
| `benchmark-foreign-unwind` | `foreign_unwind.c` | [foreign-unwind](foreign-unwind.md) |
| `benchmark-foreign-unwind-dedup` | `foreign_unwind_dedup.c` | [foreign-unwind-dedup](foreign-unwind-dedup.md) |
| `test-check-lanes, benchmark-check-lanes` | `c_check_program_emit.c` | [function-lanes](function-lanes.md) |
| `test-gnu, benchmark-gnu` | `c_gnu.c` | [gnu](gnu.md) |
| `test-import, benchmark-import` | `import.c` | [import](import.md) |
| `test-ir, test-ir-lowering, benchmark-ir` | `ir.c` | [ir](ir.md) |
| `test-ir-optimize, test-ir-gvn, test-ir-licm` | `ir_optimize.c` | [ir-optimization](ir-optimization.md) |
| `test-ir-parse, reduce-ir, interpret-ir, interpret-c` | `ir_parse.c`, `interpreter_source.c` | [ir-tools](ir-tools.md) |
| `test-lanes, benchmark-lanes` | `lane.c` | [lanes](lanes.md) |
| `test-lexer, benchmark-lexer` | `c_lexer.c` | [lexer](lexer.md) |
| `test-link` | `link.c` | [link](link.md) |
| `benchmark-link` | `link_benchmark.c` | [link-benchmark](link-benchmark.md) |
| `test-lowering, benchmark-lowering-source` | `c_lowering.c` | [lowering](lowering.md) |
| `measure-lowering-templates` | `c_check_lower_inventory.c` | [lowering](lowering.md#node-templates-1048) |
| `test-lowering-templates` | `c_check_program_templates.c` | [lowering](lowering.md#node-templates-1048) |
| `measure-node-machine [--shadow]` | `c_check_node_census.c`, `c_check_node_machine*.c` | [lowering](lowering.md#node-machine-census-2524) |
| micro-architecture campaign (`run.sh`, `uarch test`) | `tools/microarch/run.sh`, `tools/microarch/uarch.c` | [microarch](microarch.md) |
| `test-object-writer, benchmark-object-writer` | `object_writer.c` | [object-writer](object-writer.md) |
| `test-oracle-cache, BUSTER_ORACLE_CACHE` | `oracle_cache.c` | [oracle-cache](oracle-cache.md) |
| `parallel, test-parallel, test-tool` | `parallel.c`, `test_tool.c` | [parallel](parallel.md) |
| `test-parser, benchmark-parser` | `c_parser.c` | [parser](parser.md) |
| `test-preprocessor, benchmark-preprocessor` | `c_preprocessor.c` | [preprocessor](preprocessor.md) |
| `test-preprocessor-fuzz, benchmark-preprocessor-fuzz` | `preprocessor_fuzz.c` | [preprocessor-fuzz](preprocessor-fuzz.md) |
| `ab, time-runs, start-order, test-profile` | `profile.c` | [profiling](profiling.md) |
| `test-refinement, benchmark-refinement` | `c_intern.c` | [refinement](refinement.md) |
| `test-snapshot, benchmark-snapshot` | `snapshot.c` | [snapshot](snapshot.md) |
| `test-ssa, test-ssa-ir, test-ssa-source` | `ssa.c` | [ssa](ssa.md) |
| `test-streaming, benchmark-streaming` | `streaming.c` | [streaming](streaming.md) |
| `test-tier0, test-tier0-inline, benchmark-backend, benchmark-backend-source` | `tier0.c`, `tier0_inline.c` | [tier0](tier0.md) |
| `test-tier1-select, test-tier1-pipeline, test-tier1-masks, measure-tier1-programs, measure-tier1-rows` | `tier1_select.c`, `tier1_programs_measure.c`, `tier1_rows_measure.c` | [tier1](tier1.md) |
| `test-tier1-allocation, benchmark-tier1-allocation` | `tier1_allocate.c` | [tier1-allocation](tier1-allocation.md) |
| `benchmark-tier1-certificate`, `benchmark-tier1-code-certificate`, allocation and code certificate checks | `tier1_certificate.c` | [tier1-certificate](tier1-certificate.md) |
| `test-tier1-cfg, test-tier1-cfg-source` | `tier1_cfg.c` | [tier1-cfg](tier1-cfg.md) |
| `test-tier1-frame, test-tier1-frame-execution, measure-frames` | `tier1_frame.c`, `frame_measure.c` | [tier1-frame](tier1-frame.md) |
| `test-tier1-schedule, benchmark-tier1-schedule, --tier1-schedule` | `tier1_schedule.c`, `c_check_tier1_schedule.c`, `tier1_schedule_ir.c` | [tier1-schedule](tier1-schedule.md) |
| `test-check-units, benchmark-check-units` | `c_checker_units.c` | [translation-units](translation-units.md) |
| `measure-attested` | `c_check_attested.c` | [translation-units](translation-units.md#attested-declarations-census-2531) |
| `test-types, benchmark-types` | `c_types.c` | [types](types.md) |
| `test-unwind, benchmark-unwind` | `unwind_tier0.c` | [unwind](unwind.md) |
| `disassemble, test-x86-disassembler` | `x86_64_disassembler.c` | [x86-disassembler](x86-disassembler.md) |
| `test-x86-encoder, benchmark-x86-encoder` | `x86_64_encoder.c` | [x86-encoder](x86-encoder.md) |
| `test-x86-intrinsics` | `x86_intrinsics_test.c` | [x86-intrinsics](x86-intrinsics.md) |
| `test-x86-semantics, benchmark-x86-semantics` | `x86_semantics.c` | [x86-semantics](x86-semantics.md) |
| `test-x86-target, test-x86-target-selection` | `x86_target.c` | [x86-target](x86-target.md) |
| `test-x86-zen5, measure-x86-zen5, measure-mask-spills` | `x86_zen5.c` | [x86-zen5](x86-zen5.md) |

## Detailed sections

The original section titles are retained in their owning guides.

- [C driver build-system integration](../cc-driver.md).
- [The merge queue](ci.md#the-merge-queue).
- [Sharing the 9700X](ci.md#sharing-the-9700x).
- [Shard costs](ci.md#shard-costs).
- [Setup failures re-run once](ci.md#setup-failures-re-run-once).
- [Reuse and the nightly backlog](ci.md#reuse-and-the-nightly-backlog).
- [Call graph check](call-graph.md#call-graph-check).
- [Lexer tests](lexer.md#lexer-tests).
- [Lexer benchmarks](lexer.md#lexer-benchmarks).
- [Profiling tools](profiling.md#profiling-tools).
- [The parallel runner](parallel.md#the-parallel-runner).
- [Test tool probing](parallel.md#test-tool-probing).
- [The oracle cache](oracle-cache.md#the-oracle-cache).
- [x86-64 disassembler](x86-disassembler.md#x86-64-disassembler).
- [x86-64 encoder tests](x86-encoder.md#x86-64-encoder-tests).
- [x86-64 encoder benchmark](x86-encoder.md#x86-64-encoder-benchmark).
- [Branch sizing tests](branch-sizing.md#branch-sizing-tests).
- [Branch sizing benchmark](branch-sizing.md#branch-sizing-benchmark).
- [x86 semantics tests](x86-semantics.md#x86-semantics-tests).
- [x86 semantics benchmark](x86-semantics.md#x86-semantics-benchmark).
- [Lane tests](lanes.md#lane-tests).
- [Deterministic output tests](determinism.md#deterministic-output-tests).
- [Lane benchmark](lanes.md#lane-benchmark).
- [Link representation tests](link.md#link-representation-tests).
- [Object writer tests](object-writer.md#object-writer-tests).
- [Object writer benchmark](object-writer.md#object-writer-benchmark).
- [Import tests](import.md#import-tests).
- [Import benchmark](import.md#import-benchmark).
- [Bind tests](bind.md#bind-tests).
- [Bind benchmark](bind.md#bind-benchmark).
- [Direct executable tests](executable.md#direct-executable-tests).
- [Dynamic PIE and shared library tests](executable.md#dynamic-pie-and-shared-library-tests).
- [Static executable tests](executable.md#static-executable-tests).
- [Direct executable benchmark](executable.md#direct-executable-benchmark).
- [Snapshot tests](snapshot.md#snapshot-tests).
- [Snapshot benchmark](snapshot.md#snapshot-benchmark).
- [Link benchmark](link-benchmark.md#link-benchmark).
- [Floors benchmark](floors.md#floors-benchmark).
- [Foreign tests](foreign.md#foreign-tests).
- [Foreign benchmark](foreign.md#foreign-benchmark).
- [Foreign corpus test](foreign-corpus.md#foreign-corpus-test).
- [DCO sign-off tooling](dco.md#dco-sign-off-tooling).
- [all-core gauge](all-core.md#all-core-gauge).
- [Micro-architecture campaign](microarch.md#micro-architecture-campaign).
- [Dataflow tests](dataflow.md#dataflow-tests).
- [Dataflow benchmark](dataflow.md#dataflow-benchmark).
- [Preprocessor harness tests](preprocessor-fuzz.md#preprocessor-harness-tests).
- [Preprocessor harness benchmark](preprocessor-fuzz.md#preprocessor-harness-benchmark).
- [Refinement tests](refinement.md#refinement-tests).
- [Refinement benchmark](refinement.md#refinement-benchmark).
- [Preprocessor tests](preprocessor.md#preprocessor-tests).
- [Preprocessor benchmark](preprocessor.md#preprocessor-benchmark).
- [Per-file jobs tests](files.md#per-file-jobs-tests).
- [Per-file jobs benchmark](files.md#per-file-jobs-benchmark).
- [Parser tests](parser.md#parser-tests).
- [Parser benchmark](parser.md#parser-benchmark).
- [Scoped binding kernels](bindings.md#scoped-binding-kernels).
- [Deferred function bodies](deferred.md#deferred-function-bodies).
- [Function lanes](function-lanes.md#function-lanes).
- [Translation units](translation-units.md#translation-units).
- [Attested declarations' census (#2531)](translation-units.md#attested-declarations-census-2531).
- [Selective lowering](lowering.md#selective-lowering).
- [Node templates (#1048)](lowering.md#node-templates-1048).
- [Node machine census (#2524)](lowering.md#node-machine-census-2524).
- [The node machine's shadow (step 1)](lowering.md#the-shadow-step-1).
- [Type tests](types.md#type-tests).
- [Type benchmark](types.md#type-benchmark).
- [Checker corpus and semantic mutations](checker-corpus.md#checker-corpus-and-semantic-mutations).
- [Calling convention tests](calling-convention.md#calling-convention-tests).
- [Calling convention benchmark](calling-convention.md#calling-convention-benchmark).
- [Tier 0 tests](tier0.md#tier-0-tests).
- [Inlining always_inline functions (#1398)](tier0.md#inlining-always_inline-functions-1398).
- [Backend benchmark](tier0.md#backend-benchmark).
- [Streaming tests](streaming.md#streaming-tests).
- [Streaming benchmark](streaming.md#streaming-benchmark).
- [IR tests and benchmarks](ir.md#ir-tests-and-benchmarks).
