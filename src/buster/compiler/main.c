// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The executable's entry point, main, which includes most of the compiler and all of its tests. build.sh compiles this
// file once per unit, in parallel, so that the build waits for the largest unit rather than for all of this code in one
// clang process (#270): main.c alone is the driver's unit, and each main_unit_*.c names another unit and includes this
// file. Every unit reads all of it, and so has the whole file's diagnostics, but emits only its own commands' code: each
// of main_command's branches first tests its unit, so that the other units' branches fold to false, and clang, which
// emits a static function only once emitted code refers to it, emits none of theirs. main, in the driver's unit, asks
// each other unit for the command in turn, then its own commands, then runs the compiler itself.
//
// The modules that several units' commands run compile in one unit alone, their home, which the others call (#270):
// the lexer in the driver's unit, the preprocessor and the parser in the front end's, and the linker's (the runtime's
// import and binding, a PIE's inputs and the executable writer) in the dynamic PIEs' unit. A module's entry points
// take its macro (BUSTER_LEXER_ENTRY and the others, below) where its other functions take static: in the home it is
// nothing, so that they have external linkage there, and in the other units BUSTER_ELSEWHERE, which makes their
// definitions inline ones that are never inlined, which clang doesn't emit: their calls go to the home's, and the
// static functions that only the entry points reach aren't emitted there either. A unit without main.c, a benchmark's,
// has them static. Each unit still reads every definition, so that its diagnostics are the whole file's. A command
// whose code calls a home's entry point runs in two units' copies of the code, so a home's code must share no static
// variable with its callers' units: main.c's units have one lane context (lane.c), and ./build.sh check-call-graph
// checks the rest (call_graph.c).
#define BUSTER_UNIT_DRIVER 0
#define BUSTER_UNIT_X86 1
#define BUSTER_UNIT_FRONTEND 2
#define BUSTER_UNIT_ASSEMBLER 3
#define BUSTER_UNIT_LINK 4
#define BUSTER_UNIT_EXECUTABLE 5
#define BUSTER_UNIT_DYNAMIC 6
#define BUSTER_UNIT_TYPES 7
#define BUSTER_UNIT_BACKEND 8
#define BUSTER_UNIT_CHECKER 9
#define BUSTER_UNIT_CHECK_BUILTINS 10
#ifndef BUSTER_UNIT
#define BUSTER_UNIT BUSTER_UNIT_DRIVER
#endif
#define BUSTER_IN_UNIT(unit) (BUSTER_UNIT == (unit))

// An entry point in a unit that isn't its module's home: an inline definition, which clang emits only to inline it,
// and noinline keeps it from that, so that the unit's calls go to the home's external definition. Calling the module's
// static functions from it is a C2y feature that clang takes in C17 as an extension (-Wstatic-in-inline): the
// definition is only ever compiled in its home.
#define BUSTER_ELSEWHERE inline __attribute__((noinline))
#pragma clang diagnostic ignored "-Wstatic-in-inline"
#if BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER)
#define BUSTER_HOME_DRIVER
#else
#define BUSTER_HOME_DRIVER BUSTER_ELSEWHERE
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND)
#define BUSTER_HOME_FRONTEND
#else
#define BUSTER_HOME_FRONTEND BUSTER_ELSEWHERE
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_DYNAMIC)
#define BUSTER_HOME_DYNAMIC
#else
#define BUSTER_HOME_DYNAMIC BUSTER_ELSEWHERE
#endif
// The modules' homes (at the top): the lexer's (c_lexer.c), the preprocessor's (c_preprocessor.c), the parser's
// (c_parser.c) and the linker's (link.c's).
#define BUSTER_LEXER_ENTRY BUSTER_HOME_DRIVER
#define BUSTER_PREPROCESSOR_ENTRY BUSTER_HOME_FRONTEND
#define BUSTER_PARSER_ENTRY BUSTER_HOME_FRONTEND
#define BUSTER_LINKER_ENTRY BUSTER_HOME_DYNAMIC

// The ISA requirement tables are immutable and need no initialization. Share their frontend copy without sharing
// encoder storage or adding calls/loads to the local table-building kernels. Standalone inclusions stay static.
#if BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND)
#define BUSTER_X86_TARGET_DATA __attribute__((visibility("hidden")))
#else
#define BUSTER_X86_TARGET_DATA_ELSEWHERE
#endif

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <assert.h>
#include <errno.h>

#define BUSTER_ARRAY_LENGTH(x) (sizeof(x) / sizeof((x)[0]))
#define BUSTER_COMPILE_TIME_STRING_LENGTH(strlit) (BUSTER_ARRAY_LENGTH(strlit) - 1)
#define BUSTER_S8_INIT(strlit) {.pointer = (char*)(strlit), .length = BUSTER_COMPILE_TIME_STRING_LENGTH(strlit)}
#define BUSTER_S8(strlit) ((String8)BUSTER_S8_INIT(strlit))

// OS-dependent includes
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

// CPU architecture dependent includes
#include "host_isa.h"

#include "buster/compiler/c_lexer.c"

typedef struct String8 String8;
struct String8
{
    char* pointer;
    uint64_t length;
};

// The lexer benchmark is a separate translation unit, linked into the compiler executable.
int run_c_lexer_benchmark(int argc, char** argv);

// The lexer tests, for the test-lexer command.
#include "buster/compiler/c_lexer_test.c"

// The x86-64 encoder and its tests, for the test-x86-encoder command. Its benchmark is another translation unit.
#include "buster/compiler/x86_64_encoder.c"
#include "buster/compiler/x86_64_encoder_test.c"
#include "buster/compiler/x86_zen5.c"
#include "buster/compiler/x86_zen5_measure.c"
#include "buster/compiler/x86_zen5_test.c"
#include "buster/compiler/x86_64_disassembler.c"
#include "buster/compiler/x86_64_disassembler_test.c"
#include "buster/compiler/x86_64_disassembler_cli.c"
int run_x86_disassembler_benchmark(int argc, char** argv);
int run_x86_encoder_benchmark(int argc, char** argv);

// The lane base layer (groups of threads, and the arenas they allocate from) and its tests, for the test-lanes command.
// Its benchmark is another translation unit.
#include "buster/compiler/lane.c"
#include "buster/compiler/lane_test.c"
int run_lane_benchmark(int argc, char** argv);

#include "buster/compiler/diagnostic.c"
#include "buster/compiler/diagnostic_test.c"
#include "buster/compiler/diagnostic_benchmark.c"

// Liveness and interference over aligned bitsets, independent of the future IR's use/def construction.
#include "buster/compiler/dataflow.c"
#include "buster/compiler/dataflow_program.c"
#include "buster/compiler/dataflow_test.c"
int run_dataflow_benchmark(int argc, char** argv);

// Entry-rooted dominance, frontiers and natural loops over borrowed CFG columns.
#include "buster/compiler/dominators.c"
#include "buster/compiler/dominators_test.c"
int run_dominator_benchmark(int argc, char** argv);

// Local promotion over a completed CFG: vector SSA construction, its nonrecursive scalar twin and semantic tests.
#include "buster/compiler/ssa.c"
#include "buster/compiler/ssa_program.c"
#include "buster/compiler/ssa_test.c"
#include "buster/compiler/ssa_benchmark.c"

#include "buster/compiler/tier1_allocate.c"
#include "buster/compiler/tier1_rematerialize.c"
#include "buster/compiler/tier1_allocate_test.c"
int run_tier1_allocate_benchmark(int argc, char** argv);

// The preprocessor's fuzz tooling (#109): the generator, the clang oracle, the comparison and the minimizer, and their
// self-tests, for the test-preprocessor-fuzz command. test-preprocessor --fuzz runs the preprocessor against them
// (c_preprocessor_fuzz.c).
#include "buster/compiler/preprocessor_fuzz.c"
#include "buster/compiler/preprocessor_fuzz_generate.c"
#include "buster/compiler/preprocessor_fuzz_clang.c"
#include "buster/compiler/preprocessor_fuzz_test.c"
#include "buster/compiler/preprocessor_fuzz_benchmark.c"

// External randomized C compiler comparison and its independent process/accounting fixtures.
#include "buster/compiler/differential.c"
#include "buster/compiler/differential_test.c"

// Token refinement: identifier interning (c_intern.c) and the per-file pass after the lexer (c_refine.c), and their
// tests, for the test-refinement command. Its benchmark is another translation unit.
#include "buster/compiler/c_intern.c"
#include "buster/compiler/c_refine.c"
#include "buster/compiler/c_refine_test.c"
int run_refinement_benchmark(int argc, char** argv);

// The preprocessor (c_preprocessor.c, with the predefined macros of c_predefines.c) and its tests, for the -E and
// test-preprocessor commands.
#include "buster/compiler/c_predefines.c"
#include "buster/compiler/c_preprocessor.c"
#include "buster/compiler/c_builtin_catalog.c"
#include "buster/compiler/test_process.c"
// The clang and gcc runs the tests compare with, kept between runs (#542), and its tests, for the test-oracle-cache
// command.
#include "buster/compiler/oracle_cache.c"
#include "buster/compiler/oracle_cache_test.c"
#include "buster/compiler/c_preprocessor_test.c"
#include "buster/compiler/c_predefines_test.c"
#include "buster/compiler/c_preprocessor_fuzz.c"
#include "buster/compiler/c_preprocessor_oracle.c"
#include "buster/compiler/x86_target_forms.c"
int run_preprocessor_benchmark(int argc, char** argv);

// The per-file jobs on lanes and the run-ahead that fill the preprocessor's file cache (c_files.c), and their tests, for
// the test-files command. Its benchmark is another translation unit.
#include "buster/compiler/c_files.c"
#include "buster/compiler/c_files_test.c"
int run_files_benchmark(int argc, char** argv);

// The parser (c_parser.c) and its tests, for the default command, which preprocesses and parses a file, and the
// test-parser command.
#include "buster/compiler/c_parser.c"
#include "buster/compiler/c_parser_test.c"
#include "buster/compiler/c_gnu_test.c"
#include "buster/compiler/c_gnu_benchmark.c"
int run_parser_benchmark(int argc, char** argv);

// Scoped name-resolution kernels and their event-stream oracle; c_parser.c includes the shared kernels.
#include "buster/compiler/c_bindings_test.c"
#include "buster/compiler/c_bindings_benchmark.c"
#include "buster/compiler/c_parser_bindings.c"
#include "buster/compiler/c_parser_bindings_test.c"
#include "buster/compiler/c_parser_bindings_benchmark.c"

// Selective lowering consumes resolved definition/reference columns; the checker producer is still pending.
#include "buster/compiler/c_lowering.c"
#include "buster/compiler/c_lowering_test.c"
#include "buster/compiler/c_lowering_benchmark.c"

// The link representation (atoms, symbols, fixups and names, which the object writer and the linker share), the test
// programs generated in it, and their tests, for the test-link command.
#include "buster/compiler/link.c"
#include "buster/compiler/link_program.c"
#include "buster/compiler/link_test.c"
#include "buster/compiler/link_order_test.c"

// The object writer (ET_REL objects from the link representation, for -c) and its tests, for the test-object-writer
// command. Its benchmark is another translation unit.
#include "buster/compiler/object_writer.c"
#include "buster/compiler/object_writer_test.c"
#include "buster/compiler/debug.c"
#include "buster/compiler/debug_test.c"
#include "buster/compiler/debug_benchmark.c"
int run_object_writer_benchmark(int argc, char** argv);

// The import path's readers (ELF objects, archives, shared libraries and linker scripts into the link representation),
// the C runtime's files, found and imported with them, and their tests, for the test-import command.
#include "buster/compiler/import.c"
#include "buster/compiler/runtime.c"
#include "buster/compiler/import_test.c"
int run_import_benchmark(int argc, char** argv);

// Binding: the C runtime joined to a program's link and each name bound, and its tests, for the test-bind command. Its
// benchmark is another translation unit.
#include "buster/compiler/bind.c"
#include "buster/compiler/bind_test.c"
int run_bind_benchmark(int argc, char** argv);

// The direct path's executables (layout, patching, the build ID and the output file) and their tests, for the
// test-executable command. Its benchmark is another translation unit.
#include "buster/compiler/executable.c"
#include "buster/compiler/executable_test.c"
#include "buster/compiler/debug_executable_test.c"
// The object writer's DWARF checks (ObjectOptions.debug), which test-object-writer runs: they need debug.c's units and
// the executable tests' ELF reading.
#include "buster/compiler/object_debug_test.c"
int run_executable_benchmark(int argc, char** argv);

// A dynamic PIE's inputs from the runtime and binding, and the dynamic PIE's tests, for the test-dynamic command.
#include "buster/compiler/dynamic.c"
#include "buster/compiler/dynamic_test.c"

// The static executables' tests with the C runtime, for the test-static command.
#include "buster/compiler/static_test.c"

// The runtime snapshot (the imported runtime kept in the cache directory) and its tests, for the test-snapshot command.
// Its benchmark is another translation unit.
#include "buster/compiler/snapshot.c"
#include "buster/compiler/snapshot_test.c"
int run_snapshot_benchmark(int argc, char** argv);

// Foreign inputs (other compilers' objects, archives, shared libraries and scripts: opened, parsed and resolved, with
// the archive members the link needs loaded and COMDAT groups picked) and their tests, for the test-foreign command. Its
// benchmark is another translation unit.
#include "buster/compiler/inflate.c"
#include "buster/compiler/inflate_test.c"
#include "buster/compiler/foreign.c"
#include "buster/compiler/foreign_unwind.c"
#include "buster/compiler/foreign_unwind_benchmark.c"
#include "buster/compiler/foreign_gc.c"
#include "buster/compiler/foreign_link.c"
#include "buster/compiler/foreign_gc_link.c"
#include "buster/compiler/foreign_unwind_dedup.c"
#include "buster/compiler/foreign_unwind_dedup_link.c"
#include "buster/compiler/foreign_strings.c"
#include "buster/compiler/foreign_strings_link.c"
#include "buster/compiler/foreign_driver.c"
#include "buster/compiler/foreign_test.c"
#include "buster/compiler/foreign_gc_benchmark.c"
#include "buster/compiler/foreign_unwind_dedup_benchmark.c"
int run_foreign_benchmark(int argc, char** argv);
// The link benchmark (the direct path against the object writer and the linkers, on the same programs) and the floors'
// (what the machine and the kernel charge a linker) are other translation units.
int run_link_benchmark(int argc, char** argv);
int run_floors_benchmark(int argc, char** argv);

// Branch sizing (each branch's rel8 or rel32 form inside a function, before it's encoded), the functions its tests
// generate, and its tests, for the test-branch-sizing command. Its benchmark is another translation unit.
#include "buster/compiler/branch_sizing.c"
#include "buster/compiler/branch_sizing_generate.c"
#include "buster/compiler/branch_sizing_test.c"
int run_branch_sizing_benchmark(int argc, char** argv);

#include "buster/compiler/x86_match.c"
#include "buster/compiler/x86_target_test.c"
#include "buster/compiler/assembler.c"
#include "buster/compiler/assembler_driver.c"
#include "buster/compiler/assembler_test.c"
#include "buster/compiler/assembler_benchmark.c"

// What the x86 forms a code generator emits mean (their effects, the immediate's contract, the operand-resolved effects
// and the value-preserving form choice) and its tests, for the test-x86-semantics command (which takes the encoder's
// tests' printer and branch sizing's tests' runner, and checks x86_match.c's ranges). Its benchmark is another
// translation unit.
#include "buster/compiler/x86_semantics.c"
#include "buster/compiler/x86_semantics_test.c"
int run_x86_semantics_benchmark(int argc, char** argv);

// Experimental postallocation scheduling over resolved machine-record effects and measured operand costs.
#include "buster/compiler/tier1_schedule.c"
#include "buster/compiler/tier1_schedule_test.c"
int run_tier1_schedule_benchmark(int argc, char** argv);

// The C type table (TypeIds, hash-consing, layout, conversions, compatibility and printing) and its tests, for the
// test-types command. Its benchmark is another translation unit.
#include "buster/compiler/c_types.c"
#include "buster/compiler/c_builtin_intrinsic_types.c"
#include "buster/compiler/c_builtin_intrinsic_features.c"
#include "buster/compiler/c_builtin_intrinsic_catalog.c"
#include "buster/compiler/c_types_test.c"
int run_type_benchmark(int argc, char** argv);

// Immutable source-position histories retain declaration visibility for deferred body consumers.
#include "buster/compiler/c_environment.c"
#include "buster/compiler/c_environment_test.c"
#include "buster/compiler/c_environment_benchmark.c"
// The checker's own code goes in a section of its own, .text.buster_checker, which every unit starts on a page (#502):
// the driver's time depends on where its hot loops fall in the instruction caches, and code compiled before them, in
// their unit or in another, moved them by any number of bytes. In their section, only the checker's own code moves
// them. Its tests and the builtins' actions, which the driver seldom runs, stay in .text.
__asm__(".section .text.buster_checker,\"ax\",@progbits\n\t.p2align 12\n\t.text");
#pragma clang section text = ".text.buster_checker"
#include "buster/compiler/c_check_environment.c"
#include "buster/compiler/c_check_body_bindings.c"
#include "buster/compiler/c_check_lazy.c"
#pragma clang section text = ""

#include "buster/compiler/c_builtin.c"
#include "buster/compiler/c_builtin_test.c"
#pragma clang section text = ".text.buster_checker"
#include "buster/compiler/c_check_actions.c"
#pragma clang section text = ""
#include "buster/compiler/c_check_builtin_x86.c"
#include "buster/compiler/c_check_builtin.c"
#include "buster/compiler/c_check_varargs.c"
#include "buster/compiler/c_check_builtin_misc.c"
#include "buster/compiler/c_check_builtin_query.c"
#include "buster/compiler/c_check_builtin_bit_cast.c"
#include "buster/compiler/c_check_builtin_float.c"
#include "buster/compiler/c_check_builtin_math.c"
#include "buster/compiler/c_check_builtin_atomic.c"
#include "buster/compiler/c_check_builtin_atomic_rmw.c"
#include "buster/compiler/c_check_builtin_atomic_c11.c"
#include "buster/compiler/c_check_builtin_vector.c"
#include "buster/compiler/c_check_builtin_typed_overflow.c"
#include "buster/compiler/c_check_builtin_rounds.c"
#pragma clang section text = ".text.buster_checker"
#include "buster/compiler/c_checker.c"
#include "buster/compiler/c_check_literals.c"
#include "buster/compiler/c_check_initializer.c"
#include "buster/compiler/c_check_data.c"
#include "buster/compiler/c_check_input.c"
// #113's deferred bodies: check_input with every function body read apart, out of order and on lanes.
#include "buster/compiler/c_check_deferred.c"
#include "buster/compiler/c_check_diagnostic.c"
#include "buster/compiler/c_check_initializer_input.c"
#include "buster/compiler/c_check_data_input.c"
#pragma clang section text = ""
int run_checker_benchmark(int argc, char** argv);
int run_builtin_benchmark(int argc, char** argv);

// The System V calling convention (each type's classes, and each call's argument locations) and its tests, for the
// test-calling-convention command. Its benchmark is another translation unit.
#include "buster/compiler/calling_convention.c"
#include "buster/compiler/calling_convention_test.c"
int run_calling_convention_benchmark(int argc, char** argv);

// Frame storage, explicit prologue/epilogue planning and its native instruction consumer.
#include "buster/compiler/tier1_frame.c"
#include "buster/compiler/tier1_frame_test.c"
#include "buster/compiler/tier1_frame_emit.c"
#include "buster/compiler/tier1_frame_execution_test.c"
int run_tier1_frame_benchmark(int argc, char** argv);

// Tier 0 (a function's typed stream to instruction records, by templates with registers from the value stack's
// depths), the programs its tests and benchmark generate, and its tests, for the test-tier0 command. Its benchmark
// (benchmark-backend) is another translation unit.
#include "buster/compiler/tier0.c"
#include "buster/compiler/unwind.c"
#include "buster/compiler/unwind_tier0.c"
#include "buster/compiler/tier0_generate.c"
#include "buster/compiler/tier0_test.c"
#include "buster/compiler/c_builtin_lower.c"
#include "buster/compiler/c_builtin_lower_test.c"
#include "buster/compiler/c_builtin_lower_benchmark.c"
#include "buster/compiler/c_builtin_varargs_lower.c"
#include "buster/compiler/c_builtin_varargs_lower_test.c"
#include "buster/compiler/c_builtin_misc_lower.c"
#include "buster/compiler/c_builtin_misc_lower_test.c"
#include "buster/compiler/c_builtin_float_lower.c"
#include "buster/compiler/c_builtin_float_lower_test.c"
#include "buster/compiler/c_builtin_terminal_helpers.c"
#include "buster/compiler/c_builtin_terminal_helpers_test.c"
#include "buster/compiler/c_builtin_atomic_helpers.c"
#include "buster/compiler/c_builtin_atomic_helpers_test.c"
#include "buster/compiler/c_builtin_atomic_rmw_helpers.c"
#include "buster/compiler/c_builtin_atomic_rmw_helpers_test.c"
#include "buster/compiler/c_builtin_rounds_helper.c"
#include "buster/compiler/c_builtin_rounds_helper_test.c"
#include "buster/compiler/c_builtin_math_helpers.c"
#include "buster/compiler/c_builtin_math_helpers_test.c"
#include "buster/compiler/c_builtin_x86_helpers.c"
#include "buster/compiler/tier0_variadic_frame_test.c"
#include "buster/compiler/unwind_tier0_test.c"
#include "buster/compiler/unwind_test.c"
#include "buster/compiler/unwind_native_test.c"
#include "buster/compiler/unwind_benchmark.c"
// Streaming the output (#88): Tier 0's code written while it compiles, in the data-first order, against the end.
#include "buster/compiler/streaming.c"
#include "buster/compiler/streaming_test.c"
#include "buster/compiler/streaming_benchmark.c"
#include "buster/compiler/streaming_benchmark_test.c"
int run_backend_benchmark(int argc, char** argv);

// Source checking's program consumer shares the ABI and Tier 0 interfaces. Keep the source driver and its
// integration tests after those consumers so one checked unit can retain both executable bodies and static data.
#pragma clang section text = ".text.buster_checker"
#include "buster/compiler/c_check_program.c"
#pragma clang section text = ""
// #129's oracle: the functions and objects two objects define, compared (ours from -c, clang -O0 -c's), with the names
// the checked source makes, for the checker's selection tests, test-checker-corpus --symbols and test-c-testsuite
// --cross-check.
#include "buster/compiler/c_lowering_symbols.c"
#pragma clang section text = ".text.buster_checker"
#include "buster/compiler/c_check_prefix.c"
#include "buster/compiler/c_check_repair.c"
#include "buster/compiler/c_checker_driver.c"
#pragma clang section text = ""
// The checker unit's own tests compile unoptimized, between `optimize off` and `on` (#681). On the 9700X, the measured
// 716c980d to 4ce693ad pair's checker-unit mean fell from 33.7 to 27.35 s, and compilation-phase wall from 33.7 to
// 28.5 s (two builds per role, excluding final linking and remaining script overhead). AGENTS.md links the frozen
// record, including the small observed SQLite runtime costs, controller limitations and correctness scope.
// No benchmark's timed code calls these tests: they generate inputs and compare results outside the samples.
// A new test of the checker's goes inside.
#pragma clang optimize off
#include "buster/compiler/c_checker_test.c"
#pragma clang optimize on
#include "buster/compiler/c_builtin_intrinsic_types_test.c"
#include "buster/compiler/c_builtin_intrinsic_features_test.c"
#include "buster/compiler/c_builtin_intrinsic_catalog_test.c"
#include "buster/compiler/c_builtin_intrinsic_inventory_test.c"
#include "buster/compiler/c_check_builtin_x86_test.c"
#include "buster/compiler/c_check_builtin_vector_test.c"
#include "buster/compiler/c_check_builtin_test.c"
#include "buster/compiler/c_check_varargs_test.c"
#include "buster/compiler/c_check_builtin_misc_test.c"
#include "buster/compiler/c_check_builtin_query_test.c"
#include "buster/compiler/c_check_builtin_bit_cast_test.c"
#include "buster/compiler/c_check_builtin_float_test.c"
#include "buster/compiler/c_check_builtin_math_test.c"
#include "buster/compiler/c_check_builtin_atomic_test.c"
#include "buster/compiler/c_check_builtin_atomic_rmw_test.c"
#include "buster/compiler/c_check_builtin_typed_overflow_test.c"
#include "buster/compiler/c_check_builtin_rounds_test.c"
#include "buster/compiler/c_check_corpus.c"
#pragma clang optimize off
#include "buster/compiler/c_check_deferred_test.c"
#include "buster/compiler/c_check_mutation_test.c"
#include "buster/compiler/c_check_constraints_test.c"
#include "buster/compiler/c_checker_driver_test.c"
#include "buster/compiler/c_check_debug_test.c"
// #151's test-check-lanes: the functions' lanes, every output one lane's (check_program_emit_lanes).
#include "buster/compiler/c_check_lanes_test.c"
#include "buster/compiler/c_check_unwind_test.c"
#include "buster/compiler/c_check_asm_test.c"
#pragma clang optimize on
#include "buster/compiler/c_check_source_benchmark.c"
// #113's benchmark-checker-deferred: the deferred reader against source order's, and its position checks.
#include "buster/compiler/c_check_deferred_benchmark.c"
// #129's benchmark-lowering-source: what lowering only the referenced definitions saves on real units.
#include "buster/compiler/c_lowering_source_benchmark.c"
// #136's benchmark-backend-source: benchmark-backend's variants on the functions -c compiles of real units.
#include "buster/compiler/tier0_source_benchmark.c"
// #151's benchmark-check-lanes: a unit's functions compiled on lanes, against one lane.
#include "buster/compiler/c_check_lanes_benchmark.c"
#include "buster/compiler/c_check_binding_benchmark.c"
#include "buster/compiler/c_check_debug_benchmark.c"
#pragma clang optimize off
#include "buster/compiler/c_check_source_benchmark_test.c"
#include "buster/compiler/c_lowering_source_benchmark_test.c"
#include "buster/compiler/tier0_source_benchmark_test.c"
#include "buster/compiler/c_check_corpus_symbols_test.c"
#pragma clang optimize on

// Tier 1's arena-backed instruction columns, exact verifier twins, stable text form and typed-stream adapter.
#include "buster/compiler/ir.c"
#include "buster/compiler/ir_lower.c"
#include "buster/compiler/ssa_ir.c"
#include "buster/compiler/ir_validate.c"
#include "buster/compiler/interpreter.c"
#include "buster/compiler/interpreter_native.c"
#include "buster/compiler/interpreter_link.c"
#include "buster/compiler/ir_parse.c"
#include "buster/compiler/ir_interpreter.c"
#include "buster/compiler/ir_test.c"
#include "buster/compiler/ir_lower_test.c"
#include "buster/compiler/ssa_ir_test.c"
#include "buster/compiler/ir_parse_test.c"
#include "buster/compiler/interpreter_test.c"
#include "buster/compiler/interpreter_native_test.c"
#include "buster/compiler/interpreter_link_test.c"
#include "buster/compiler/ir_interpreter_test.c"
#include "buster/compiler/interpreter_driver.c"
#include "buster/compiler/ir_benchmark.c"
#include "buster/compiler/ir_lower_benchmark.c"

// Verified Tier 1 optimization passes and independent before/after semantic tests.
#include "buster/compiler/ir_optimize.c"
#include "buster/compiler/ir_optimize_inline.c"
#include "buster/compiler/ir_optimize_inline_test.c"
#include "buster/compiler/ir_optimize_test.c"
#include "buster/compiler/ir_optimize_benchmark.c"
#include "buster/compiler/ir_optimize_gvn.c"
#include "buster/compiler/ir_optimize_gvn_test.c"
#include "buster/compiler/ir_optimize_licm.c"
#include "buster/compiler/ir_optimize_licm_test.c"
#include "buster/compiler/ir_optimize_sroa.c"
#include "buster/compiler/ir_optimize_sroa_test.c"
#include "buster/compiler/ir_optimize_passes_benchmark.c"

// Selected effective uses feed liveness, allocation and the bounded native function consumer.
#include "buster/compiler/tier1_select.c"
#include "buster/compiler/tier1_select_test.c"
#include "buster/compiler/tier1_liveness.c"
#include "buster/compiler/tier1_liveness_test.c"
#include "buster/compiler/tier1_emit.c"
#include "buster/compiler/tier1_emit_test.c"
#include "buster/compiler/tier1_pipeline_test.c"
bool run_x86_target_selection_tests(void);
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/x86_target_selection_test.c"
#endif
#include "buster/compiler/ir_stack_test.c"
int run_tier1_select_benchmark(int argc, char** argv);
int run_tier1_model_measure(int argc, char** argv);
int run_tier1_model_measure_tests(int argc, char** argv);

// The call graph check (the style's rule against recursion, on the compiler's own LLVM IR) and its tests, for the
// check-call-graph and test-call-graph commands.
#include "buster/compiler/call_graph.c"
#include "buster/compiler/call_graph_test.c"

// The profiling tools (ab, and the reports of perf's and objdump's text: hot-loops, label-spills, spill-report and
// branch-misses) and their tests, for the test-profile command.
#include "buster/compiler/profile.c"
#include "buster/compiler/profile_test.c"

// measure-compile (#518): whole compiles timed as processes against a reference compiler, with --time-stages' report of
// where their time goes.
#include "buster/compiler/compile_measure.c"

// The runner the 9700X job runs its tests through (a list of commands at once, each one's output printed in the list's
// order) and its tests, for the parallel and test-parallel commands.
#include "buster/compiler/parallel.c"
#include "buster/compiler/parallel_test.c"

// The c-testsuite harness (#145), for the test-c-testsuite command: each program compiled and linked by this
// executable, run and compared with its expected output.
#include "buster/compiler/c_testsuite.c"

// The other units' main_command, each in its own unit (at the top).
int main_unit_x86(int argc, char** argv, bool* found);
int main_unit_frontend(int argc, char** argv, bool* found);
int main_unit_assembler(int argc, char** argv, bool* found);
int main_unit_link(int argc, char** argv, bool* found);
int main_unit_executable(int argc, char** argv, bool* found);
int main_unit_dynamic(int argc, char** argv, bool* found);
int main_unit_types(int argc, char** argv, bool* found);
int main_unit_checker(int argc, char** argv, bool* found);
int main_unit_check_builtins(int argc, char** argv, bool* found);
int main_unit_backend(int argc, char** argv, bool* found);
int run_source_check_command(int count, char** arguments);

// Keep the historical -c assembly spelling while sending C compilation to the semantic driver. Options which
// take a separate argument cannot make an output or include path look like an assembly input.
static bool main_assembly_source(int count, char** arguments)
{
    bool compile = false;
    bool assembly = false;
    bool explicit_c = false;
    for (int i = 0; i < count; i += 1)
    {
        const char* argument = arguments[i];
        if (strcmp(argument, "-c") == 0)
        {
            compile = true;
        }
        else if (strcmp(argument, "-x") == 0 && i + 1 < count)
        {
            explicit_c = strcmp(arguments[++i], "c") == 0;
        }
        else if ((strcmp(argument, "-o") == 0 || strcmp(argument, "-I") == 0 || strcmp(argument, "-D") == 0 ||
                  strcmp(argument, "-U") == 0 || strcmp(argument, "-include") == 0 || strcmp(argument, "-isystem") == 0 ||
                  strcmp(argument, "-iquote") == 0 || strcmp(argument, "-idirafter") == 0 || strcmp(argument, "--predefines") == 0) && i + 1 < count)
        {
            i += 1;
        }
        else if (argument[0] != '-')
        {
            const uint64_t length = strlen(argument);
            assembly = length >= 2 && (strcmp(argument + length - 2, ".s") == 0 || strcmp(argument + length - 2, ".S") == 0);
        }
    }
    return compile && assembly && !explicit_c;
}

// Runs the command argv[1] names if it is one of this unit's (*found). Each branch first tests the unit whose code it
// is, so that the other units' branches fold to false (at the top). The driver's last branch, which no command takes,
// runs the compiler itself.
static int main_command(int argc, char** argv, bool* found)
{
    int result = 0;
    *found = true;
    if (!BUSTER_HOST_VECTOR && argc >= 2 && (strncmp(argv[1], "benchmark-", 10) == 0 || strcmp(argv[1], "measure-x86-zen5") == 0))
    {
        fprintf(stderr, "benchmarks require an AVX-512 host build; run tests with this scalar build\n");
        result = 1;
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-ssa") == 0)
    {
        result = run_ssa_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "benchmark-ssa") == 0)
    {
        result = run_ssa_benchmark(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-ssa-ir") == 0)
    {
        result = run_ssa_ir_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc == 2 && strcmp(argv[1], "test-diagnostics") == 0)
    {
        result = run_diagnostic_tests();
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-diagnostics") == 0)
    {
        result = run_diagnostic_benchmark(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_ASSEMBLER) && argc >= 2 && (strcmp(argv[1], "assemble") == 0 || main_assembly_source(argc - 1, argv + 1)))
    {
        const int skip = strcmp(argv[1], "assemble") == 0 ? 2 : 1;
        result = run_assembler_command(argc - skip, argv + skip);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "test-x86-semantics") == 0)
    {
        result = run_x86_semantics_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-x86-semantics") == 0)
    {
        result = run_x86_semantics_benchmark(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_ASSEMBLER) && argc >= 2 && strcmp(argv[1], "test-assembler") == 0)
    {
        result = run_assembler_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_ASSEMBLER) && argc >= 2 && strcmp(argv[1], "benchmark-assembler") == 0)
    {
        result = run_assembler_benchmark(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND) && argc >= 2 && strcmp(argv[1], "-E") == 0)
    {
        result = run_preprocessor_command_mode(argc - 1, argv + 1, false);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND) && argc >= 2 && strcmp(argv[1], "test-preprocessor") == 0)
    {
        result = run_preprocessor_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-preprocessor") == 0)
    {
        result = run_preprocessor_benchmark(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND) && argc >= 2 && strcmp(argv[1], "test-parser") == 0)
    {
        result = run_parser_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-parser") == 0)
    {
        result = run_parser_benchmark(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND) && argc >= 2 && strcmp(argv[1], "test-gnu") == 0)
    {
        result = run_gnu_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND) && argc >= 2 && strcmp(argv[1], "benchmark-gnu") == 0)
    {
        result = run_gnu_benchmark(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND) && argc >= 2 && strcmp(argv[1], "test-bindings") == 0)
    {
        result = run_c_bindings_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND) && argc >= 2 && strcmp(argv[1], "benchmark-bindings") == 0)
    {
        result = run_c_bindings_benchmark(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND) && argc >= 2 && strcmp(argv[1], "test-lowering") == 0)
    {
        result = run_c_lowering_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND) && argc >= 2 && strcmp(argv[1], "benchmark-lowering") == 0)
    {
        result = run_c_lowering_benchmark(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND) && argc >= 2 && strcmp(argv[1], "test-parser-bindings") == 0)
    {
        result = run_parser_bindings_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND) && argc >= 2 && strcmp(argv[1], "benchmark-parser-bindings") == 0)
    {
        result = run_parser_bindings_benchmark(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "test-lexer") == 0)
    {
        result = run_c_lexer_tests(argv[0], argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-lexer") == 0)
    {
        result = run_c_lexer_benchmark(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_X86) && argc >= 2 && strcmp(argv[1], "test-x86-encoder") == 0)
    {
        result = run_x86_encoder_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND) && argc >= 2 && strcmp(argv[1], "test-x86-target") == 0)
    {
        result = run_x86_target_tests();
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-x86-target-selection") == 0)
    {
        result = run_x86_target_selection_tests() ? 0 : 1;
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_X86) && argc >= 2 && strcmp(argv[1], "test-x86-zen5") == 0)
    {
        result = run_x86_zen5_tests();
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_X86) && argc >= 2 && strcmp(argv[1], "benchmark-x86-zen5") == 0)
    {
        result = run_x86_zen5_benchmark(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_X86) && argc >= 2 && strcmp(argv[1], "measure-x86-zen5") == 0)
    {
        result = run_x86_zen5_measure(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND) && argc >= 2 && strcmp(argv[1], "benchmark-x86-target") == 0)
    {
        result = run_x86_target_benchmark(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-x86-encoder") == 0)
    {
        result = run_x86_encoder_benchmark(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_X86) && argc >= 2 && strcmp(argv[1], "test-x86-disassembler") == 0)
    {
        result = run_x86_disassembler_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "disassemble") == 0)
    {
        result = run_x86_disassembler(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-x86-disassembler") == 0)
    {
        result = run_x86_disassembler_benchmark(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "test-lanes") == 0)
    {
        result = run_lane_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-lanes") == 0)
    {
        result = run_lane_benchmark(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-dominators") == 0)
    {
        result = run_dominator_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "benchmark-dominators") == 0)
    {
        result = run_dominator_benchmark(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-tier1-allocation") == 0)
    {
        result = run_tier1_allocate_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "benchmark-tier1-allocation") == 0)
    {
        result = run_tier1_allocate_benchmark(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-tier1-schedule") == 0)
    {
        result = run_tier1_schedule_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "benchmark-tier1-schedule") == 0)
    {
        result = run_tier1_schedule_benchmark(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "test-dataflow") == 0)
    {
        result = run_dataflow_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-dataflow") == 0)
    {
        result = run_dataflow_benchmark(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND) && argc >= 2 && strcmp(argv[1], "test-preprocessor-fuzz") == 0)
    {
        result = run_preprocessor_fuzz_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND) && argc >= 2 && strcmp(argv[1], "benchmark-preprocessor-fuzz") == 0)
    {
        result = run_preprocessor_fuzz_benchmark(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND) && argc >= 2 && strcmp(argv[1], "test-refinement") == 0)
    {
        result = run_refinement_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-refinement") == 0)
    {
        result = run_refinement_benchmark(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_LINK) && argc >= 2 && strcmp(argv[1], "test-link") == 0)
    {
        result = run_link_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_LINK) && argc >= 2 && strcmp(argv[1], "test-object-writer") == 0)
    {
        result = run_object_writer_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_LINK) && argc >= 2 && strcmp(argv[1], "test-debug") == 0)
    {
        result = run_debug_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_LINK) && argc >= 2 && strcmp(argv[1], "benchmark-debug") == 0)
    {
        result = run_debug_benchmark(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-object-writer") == 0)
    {
        result = run_object_writer_benchmark(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_LINK) && argc >= 2 && strcmp(argv[1], "test-import") == 0)
    {
        result = run_import_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-import") == 0)
    {
        result = run_import_benchmark(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_LINK) && argc >= 2 && strcmp(argv[1], "test-bind") == 0)
    {
        result = run_bind_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-bind") == 0)
    {
        result = run_bind_benchmark(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_EXECUTABLE) && argc >= 2 && strcmp(argv[1], "test-executable") == 0)
    {
        result = run_executable_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-executable") == 0)
    {
        result = run_executable_benchmark(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DYNAMIC) && argc >= 2 && strcmp(argv[1], "test-dynamic") == 0)
    {
        result = run_dynamic_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DYNAMIC) && argc >= 2 && strcmp(argv[1], "test-static") == 0)
    {
        result = run_static_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_EXECUTABLE) && argc >= 2 && strcmp(argv[1], "test-snapshot") == 0)
    {
        result = run_snapshot_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-snapshot") == 0)
    {
        result = run_snapshot_benchmark(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_EXECUTABLE) && argc >= 2 && strcmp(argv[1], "test-inflate") == 0)
    {
        result = run_inflate_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_EXECUTABLE) && argc >= 2 && strcmp(argv[1], "test-foreign") == 0)
    {
        result = run_foreign_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_EXECUTABLE) && argc >= 2 && strcmp(argv[1], "test-foreign-corpus") == 0)
    {
        result = run_foreign_corpus_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_EXECUTABLE) && argc >= 2 && strcmp(argv[1], "benchmark-foreign-output") == 0)
    {
        result = run_foreign_link_benchmark(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_EXECUTABLE) && argc >= 2 && strcmp(argv[1], "benchmark-foreign-strings") == 0)
    {
        result = run_foreign_strings_benchmark(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_EXECUTABLE) && argc >= 2 && strcmp(argv[1], "benchmark-foreign-unwind") == 0)
    {
        result = run_foreign_unwind_benchmark(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_EXECUTABLE) && argc >= 2 && strcmp(argv[1], "link-foreign") == 0)
    {
        result = run_foreign_link(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_EXECUTABLE) && argc >= 2 && strcmp(argv[1], "benchmark-foreign-gc") == 0)
    {
        result = run_foreign_gc_benchmark(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_EXECUTABLE) && argc >= 2 && strcmp(argv[1], "benchmark-foreign-unwind-dedup") == 0)
    {
        result = run_foreign_unwind_dedup_benchmark(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-foreign") == 0)
    {
        result = run_foreign_benchmark(argc - 1, argv + 1);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-link") == 0)
    {
        result = run_link_benchmark(argc - 1, argv + 1);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-floors") == 0)
    {
        result = run_floors_benchmark(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_X86) && argc >= 2 && strcmp(argv[1], "test-branch-sizing") == 0)
    {
        result = run_branch_sizing_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-branch-sizing") == 0)
    {
        result = run_branch_sizing_benchmark(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND) && argc >= 2 && strcmp(argv[1], "test-files") == 0)
    {
        result = run_files_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-files") == 0)
    {
        result = run_files_benchmark(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER) && argc >= 2 && strcmp(argv[1], "check") == 0)
    {
        result = run_check_command_mode(argc - 2, argv + 2, false, false);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND) && argc >= 2 && strcmp(argv[1], "parse") == 0)
    {
        result = run_parse_command(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER) && argc >= 2 && strcmp(argv[1], "test-checker") == 0)
    {
        result = run_checker_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER) && argc >= 2 && strcmp(argv[1], "test-checker-asm") == 0)
    {
        result = run_checker_asm_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER) && argc >= 2 && strcmp(argv[1], "test-checker-driver") == 0)
    {
        result = run_checker_driver_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER) && argc >= 2 && strcmp(argv[1], "test-check-debug") == 0)
    {
        result = run_check_debug_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER) && argc >= 2 && strcmp(argv[1], "benchmark-check-debug") == 0)
    {
        result = run_check_debug_benchmark(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER) && argc >= 2 && strcmp(argv[1], "test-check-lanes") == 0)
    {
        result = run_check_lanes_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_CHECK_BUILTINS) && argc >= 2 && strcmp(argv[1], "test-check-builtins") == 0)
    {
        result = run_check_builtin_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_CHECK_BUILTINS) && argc >= 2 && strcmp(argv[1], "test-check-varargs") == 0)
    {
        result = run_check_varargs_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_CHECK_BUILTINS) && argc >= 2 && strcmp(argv[1], "test-check-builtins-misc") == 0)
    {
        result = run_check_builtin_misc_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_CHECK_BUILTINS) && argc >= 2 && strcmp(argv[1], "test-check-builtins-query") == 0)
    {
        result = run_check_builtin_query_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_CHECK_BUILTINS) && argc >= 2 && strcmp(argv[1], "test-check-builtins-bit-cast") == 0)
    {
        result = run_check_builtin_bit_cast_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_CHECK_BUILTINS) && argc >= 2 && strcmp(argv[1], "test-check-builtins-float") == 0)
    {
        result = run_check_builtin_float_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_CHECK_BUILTINS) && argc >= 2 && strcmp(argv[1], "test-builtin-intrinsic-types") == 0)
    {
        result = run_builtin_intrinsic_type_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_CHECK_BUILTINS) && argc >= 2 && strcmp(argv[1], "test-builtin-intrinsic-features") == 0)
    {
        result = run_builtin_intrinsic_features_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_CHECK_BUILTINS) && argc >= 2 && strcmp(argv[1], "test-check-builtins-x86") == 0)
    {
        result = run_check_builtin_x86_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_CHECK_BUILTINS) && argc >= 2 && strcmp(argv[1], "test-builtin-intrinsic-inventory") == 0)
    {
        result = run_builtin_intrinsic_inventory_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_CHECK_BUILTINS) && argc >= 2 && strcmp(argv[1], "test-builtin-intrinsic-catalog") == 0)
    {
        result = test_builtin_intrinsic_catalog(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_CHECK_BUILTINS) && argc >= 2 && strcmp(argv[1], "test-check-builtins-vector") == 0)
    {
        result = run_check_builtin_vector_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_CHECK_BUILTINS) && argc >= 2 && strcmp(argv[1], "test-check-builtins-math") == 0)
    {
        result = run_check_builtin_math_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_CHECK_BUILTINS) && argc >= 2 && strcmp(argv[1], "test-check-builtins-atomic") == 0)
    {
        result = run_check_builtin_atomic_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_CHECK_BUILTINS) && argc >= 2 && strcmp(argv[1], "test-check-builtins-typed-overflow") == 0)
    {
        result = run_check_builtin_typed_overflow_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_CHECK_BUILTINS) && argc >= 2 && strcmp(argv[1], "test-check-builtins-rounds") == 0)
    {
        result = run_check_builtin_rounds_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-builtin-lower") == 0)
    {
        result = run_builtin_lower_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-builtin-varargs-lower") == 0)
    {
        result = run_builtin_varargs_lower_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-builtin-misc-lower") == 0)
    {
        result = run_builtin_misc_lower_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-builtin-float-lower") == 0)
    {
        result = run_builtin_float_lower_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-builtin-terminal-helpers") == 0)
    {
        result = run_builtin_terminal_helper_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-builtin-atomic-helpers") == 0)
    {
        result = run_builtin_atomic_helper_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-builtin-rounds-helper") == 0)
    {
        result = run_builtin_rounds_helper_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-builtin-math-helpers") == 0)
    {
        result = run_builtin_math_helper_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND) && argc >= 2 && strcmp(argv[1], "test-environments") == 0)
    {
        result = run_c_environment_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND) && argc >= 2 && strcmp(argv[1], "benchmark-environments") == 0)
    {
        result = run_c_environment_benchmark(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER) && argc >= 2 && strcmp(argv[1], "test-checker-corpus") == 0)
    {
        result = run_checker_corpus(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER) && argc >= 2 && strcmp(argv[1], "test-checker-deferred") == 0)
    {
        result = run_checker_deferred_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER) && argc >= 2 && strcmp(argv[1], "benchmark-checker-deferred") == 0)
    {
        result = run_checker_deferred_benchmark(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER) && argc >= 2 && strcmp(argv[1], "benchmark-checker-source") == 0)
    {
        result = run_checker_source_benchmark(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER) && argc >= 2 && strcmp(argv[1], "benchmark-lowering-source") == 0)
    {
        result = run_lowering_source_benchmark(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER) && argc >= 2 && strcmp(argv[1], "benchmark-backend-source") == 0)
    {
        result = run_backend_source_benchmark(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER) && argc >= 2 && strcmp(argv[1], "benchmark-check-lanes") == 0)
    {
        result = run_check_lanes_benchmark(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER) && argc >= 2 && strcmp(argv[1], "benchmark-checker-bindings") == 0)
    {
        result = run_checker_binding_benchmark(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-checker") == 0)
    {
        result = run_checker_benchmark(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_TYPES) && argc >= 2 && strcmp(argv[1], "test-types") == 0)
    {
        result = run_type_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-types") == 0)
    {
        result = run_type_benchmark(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_TYPES) && argc >= 2 && strcmp(argv[1], "test-calling-convention") == 0)
    {
        result = run_calling_convention_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_TYPES) && argc >= 2 && strcmp(argv[1], "test-builtins") == 0)
    {
        result = run_builtin_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_CHECK_BUILTINS) && argc >= 2 && strcmp(argv[1], "test-check-builtins-atomic-rmw") == 0)
    {
        result = run_check_builtin_atomic_rmw_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-builtins") == 0)
    {
        result = run_builtin_benchmark(argc - 1, argv + 1);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-calling-convention") == 0)
    {
        result = run_calling_convention_benchmark(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-differential") == 0)
    {
        result = run_differential_command(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-differential-harness") == 0)
    {
        result = run_differential_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-tier1-frame") == 0)
    {
        result = run_tier1_frame_tests(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-tier1-frame-execution") == 0)
    {
        result = run_tier1_frame_execution_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "benchmark-tier1-frame") == 0)
    {
        result = run_tier1_frame_benchmark(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-tier0") == 0)
    {
        result = run_tier0_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-builtin-lowering") == 0)
    {
        result = run_builtin_lower_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-builtin-varargs") == 0)
    {
        result = run_builtin_varargs_lower_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-tier0-variadic-frame") == 0)
    {
        result = test_tier0_variadic_frame(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-builtin-misc-lowering") == 0)
    {
        result = run_builtin_misc_lower_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-builtin-float-lowering") == 0)
    {
        result = run_builtin_float_lower_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-builtin-atomic-rmw-helpers") == 0)
    {
        result = run_builtin_atomic_rmw_helper_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "benchmark-builtin-lowering") == 0)
    {
        result = run_builtin_lower_benchmark(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-unwind") == 0)
    {
        result = run_unwind_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "benchmark-unwind") == 0)
    {
        result = run_unwind_benchmark(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-streaming") == 0)
    {
        result = run_streaming_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "benchmark-streaming") == 0)
    {
        result = run_streaming_benchmark(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-backend") == 0)
    {
        result = run_backend_benchmark(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-ir") == 0)
    {
        result = run_ir_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-ir-lowering") == 0)
    {
        result = run_ir_lower_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-ir-parse") == 0)
    {
        result = run_ir_parse_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-interpreter") == 0)
    {
        result = run_interpreter_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-interpreter-native") == 0)
    {
        result = run_interpreter_native_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-interpreter-link") == 0)
    {
        result = run_interpreter_link_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-ir-interpreter") == 0)
    {
        result = run_ir_interpreter_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "interpret-ir") == 0)
    {
        result = run_ir_interpreter(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "benchmark-ir") == 0)
    {
        result = run_ir_benchmark(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "benchmark-ir-lowering") == 0)
    {
        result = run_ir_lower_benchmark(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-ir-optimize") == 0)
    {
        result = run_ir_optimize_tests(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-ir-gvn") == 0)
    {
        result = run_ir_gvn_tests(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-ir-inline") == 0)
    {
        result = run_ir_optimize_inline_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-ir-licm") == 0)
    {
        result = run_ir_licm_tests(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-ir-sroa") == 0)
    {
        result = run_ir_sroa_tests(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-tier1-select") == 0)
    {
        result = run_tier1_select_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-tier1-liveness") == 0)
    {
        result = run_tier1_liveness_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-tier1-emit") == 0)
    {
        result = run_tier1_emit_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-tier1-pipeline") == 0)
    {
        result = run_tier1_pipeline_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "benchmark-tier1-select") == 0)
    {
        result = run_tier1_select_benchmark(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-tier1-model") == 0)
    {
        result = run_tier1_model_measure_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "measure-tier1-model") == 0)
    {
        result = run_tier1_model_measure(argc - 1, argv + 1);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "benchmark-ir-optimize-passes") == 0)
    {
        result = run_ir_optimize_passes_benchmark(argc - 1, argv + 1);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "benchmark-ir-optimize") == 0)
    {
        result = run_ir_optimize_benchmark(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "check-call-graph") == 0)
    {
        result = run_check_call_graph(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "test-call-graph") == 0)
    {
        result = run_call_graph_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 &&
             (strcmp(argv[1], "ab") == 0 || strcmp(argv[1], "hot-loops") == 0 || strcmp(argv[1], "label-spills") == 0 ||
              strcmp(argv[1], "spill-report") == 0 || strcmp(argv[1], "branch-misses") == 0 || strcmp(argv[1], "time-runs") == 0))
    {
        result = run_profile_command(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "measure-compile") == 0)
    {
        result = run_measure_compile_command(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "test-profile") == 0)
    {
        result = run_profile_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "parallel") == 0)
    {
        result = run_parallel_command(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "test-parallel") == 0)
    {
        result = run_parallel_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "test-oracle-cache") == 0)
    {
        result = run_oracle_cache_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "test-c-testsuite") == 0)
    {
        result = run_c_testsuite_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER))
    {
        // Semantic checking and object emission share the same typed source stream in the checker's unit.
        result = run_source_check_command(argc - 1, argv + 1);
    }
    else
    {
        *found = false;
    }
    return result;
}

#if BUSTER_UNIT == BUSTER_UNIT_DRIVER
int main(int argc, char** argv)
{
    const int guardian = worker_budget_guardian_command(argc, argv);
    // A pipe-style jobserver requires '+' recipes, which make executes even under -n. Honor its dry-run flag before
    // dispatch, including -E and parse commands that need no worker lease themselves.
    bool found = guardian >= 0 || worker_budget_dry_run(getenv("MAKEFLAGS"));
    int result = guardian >= 0 ? guardian : 0;
    if (!found)
    {
        result = main_unit_x86(argc, argv, &found);
    }
    if (!found)
    {
        result = main_unit_frontend(argc, argv, &found);
    }
    if (!found)
    {
        result = main_unit_assembler(argc, argv, &found);
    }
    if (!found)
    {
        result = main_unit_link(argc, argv, &found);
    }
    if (!found)
    {
        result = main_unit_executable(argc, argv, &found);
    }
    if (!found)
    {
        result = main_unit_dynamic(argc, argv, &found);
    }
    if (!found)
    {
        result = main_unit_types(argc, argv, &found);
    }
    if (!found)
    {
        result = main_unit_checker(argc, argv, &found);
    }
    if (!found)
    {
        result = main_unit_check_builtins(argc, argv, &found);
    }
    if (!found)
    {
        result = main_unit_backend(argc, argv, &found);
    }
    if (!found)
    {
        result = main_command(argc, argv, &found);
    }
    return result;
}
#elif BUSTER_UNIT == BUSTER_UNIT_X86
int main_unit_x86(int argc, char** argv, bool* found)
{
    return main_command(argc, argv, found);
}
#elif BUSTER_UNIT == BUSTER_UNIT_FRONTEND
int main_unit_frontend(int argc, char** argv, bool* found)
{
    return main_command(argc, argv, found);
}
#elif BUSTER_UNIT == BUSTER_UNIT_ASSEMBLER
int main_unit_assembler(int argc, char** argv, bool* found)
{
    return main_command(argc, argv, found);
}
#elif BUSTER_UNIT == BUSTER_UNIT_LINK
int main_unit_link(int argc, char** argv, bool* found)
{
    return main_command(argc, argv, found);
}
#elif BUSTER_UNIT == BUSTER_UNIT_EXECUTABLE
int main_unit_executable(int argc, char** argv, bool* found)
{
    return main_command(argc, argv, found);
}
#elif BUSTER_UNIT == BUSTER_UNIT_DYNAMIC
int main_unit_dynamic(int argc, char** argv, bool* found)
{
    return main_command(argc, argv, found);
}
#elif BUSTER_UNIT == BUSTER_UNIT_TYPES
int main_unit_types(int argc, char** argv, bool* found)
{
    return main_command(argc, argv, found);
}
#elif BUSTER_UNIT == BUSTER_UNIT_CHECKER
int run_source_check_command(int count, char** arguments)
{
    return run_check_command_mode(count, arguments, true, false);
}

int main_unit_checker(int argc, char** argv, bool* found)
{
    return main_command(argc, argv, found);
}
#elif BUSTER_UNIT == BUSTER_UNIT_CHECK_BUILTINS
int main_unit_check_builtins(int argc, char** argv, bool* found)
{
    return main_command(argc, argv, found);
}
#elif BUSTER_UNIT == BUSTER_UNIT_BACKEND
int main_unit_backend(int argc, char** argv, bool* found)
{
    return main_command(argc, argv, found);
}
#endif
