// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The executable's entry point, main, which includes most of the compiler and all of its tests. build.sh compiles this
// file once per unit, in parallel, so that the build waits for the largest unit rather than for all of this code in one
// clang process (#270): main.c alone is the driver's unit, and each main_unit_*.c names another unit and includes this
// file. Shared interfaces and fixtures are read by their consumers; private implementation and independent tests
// stay with the units that use them, keeping their parsing and edit dependencies there. Each command first tests its
// unit (and an independent test's branch is included with its test), so the other units' branches fold to false. Clang
// emits a static function only once emitted code refers to it, and emits none of theirs. main, in the driver's unit, asks
// each other unit for the command in turn, then its own commands, then runs the compiler itself.
//
// The modules that several units' commands run compile in one unit alone, their home, which the others call (#270):
// the lexer in the driver's unit, the preprocessor and the parser in the front end's, and the linker's (the runtime's
// import and binding, a PIE's inputs and the executable writer) in the dynamic PIEs' unit. A module's entry points
// take its macro (BUSTER_LEXER_ENTRY and the others, below) where its other functions take static: in the home it is
// nothing, so that they have external linkage there, and in the other units BUSTER_ELSEWHERE, which makes their
// definitions inline ones that are never inlined, which clang doesn't emit: their calls go to the home's, and the
// static functions that only the entry points reach aren't emitted there either. A unit without main.c, a benchmark's,
// has them static. The parser, preprocessor and executable writer expose declarations to other units; their bodies
// stay with their existing homes and direct private consumers, and the complete driver's (below). A command
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
// Source checking is emitted only in CHECKER; code generation also has consumers in BACKEND. The backend and
// builtin tests share semantic actions without the source reader, graph or lowering. The complete driver is DRIVER
// with the whole compiler as well: the source, checker and code-generation consumers and the preprocessor's and
// executable writer's private bodies, which other units own. A fuzz build's callbacks run in DRIVER and need it,
// including a custom callback's access to the normal compiler modules; the self-tests and benchmarks that take main.c
// as their input pass -DBUSTER_COMPLETE_DRIVER=1 to read all of the compiler's code, not only what DRIVER emits. An
// ordinary build passes neither, and the other units ignore both.
#if BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && (BUSTER_FUZZ_AVAILABLE || BUSTER_COMPLETE_DRIVER)
#undef BUSTER_COMPLETE_DRIVER
#define BUSTER_COMPLETE_DRIVER 1
#else
#undef BUSTER_COMPLETE_DRIVER
#define BUSTER_COMPLETE_DRIVER 0
#endif
#define BUSTER_SOURCE_CONSUMER (BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER) || BUSTER_COMPLETE_DRIVER)
#define BUSTER_BACKEND_CONSUMER (BUSTER_SOURCE_CONSUMER || BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND))
#define BUSTER_CHECKER_CONSUMER (BUSTER_BACKEND_CONSUMER || BUSTER_IN_UNIT(BUSTER_UNIT_CHECK_BUILTINS))

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
#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
#define BUSTER_HOME_CHECKER
#else
#define BUSTER_HOME_CHECKER BUSTER_ELSEWHERE
#endif
// The modules' homes (at the top): the lexer's (c_lexer.c), the preprocessor's (c_preprocessor.c), the parser's
// (c_parser.c) and the linker's (link.c's); and the checker's, for the IR's commands on real C (ir_source_test.c's
// ir_source_prepare, the driver's check and emission of a file), which the backend's unit runs with the IR (#784).
#define BUSTER_LEXER_ENTRY BUSTER_HOME_DRIVER
#if BUSTER_COMPLETE_DRIVER
#define BUSTER_PREPROCESSOR_ENTRY BUSTER_HOME_FRONTEND
#else
#define BUSTER_PREPROCESSOR_ENTRY
#endif
// Parser bodies are included only by their frontend home; the other units use these external declarations.
#define BUSTER_PARSER_ENTRY
#define BUSTER_LINKER_ENTRY BUSTER_HOME_DYNAMIC
#define BUSTER_IR_SOURCE_ENTRY BUSTER_HOME_CHECKER

// The ISA requirement tables are immutable and need no initialization. Share their frontend copy without sharing
// encoder storage or adding calls/loads to the local table-building kernels. Standalone inclusions stay static.
#if BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND)
#define BUSTER_X86_TARGET_DATA __attribute__((visibility("hidden")))
#else
#define BUSTER_X86_TARGET_DATA_ELSEWHERE
#endif
// So are the option spellings (c_option_table.c): a compile reads them in the driver's, the front end's and the
// checker's units, from the front end's copy.
#if BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND)
#define BUSTER_C_OPTION_DATA __attribute__((visibility("hidden")))
#else
#define BUSTER_C_OPTION_DATA_ELSEWHERE
#endif

#include <stdint.h>
#include <inttypes.h>
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

#include "buster/compiler/string8.h"

// The tests' normalized temporary directory, for the test files and benchmarks that build their trees from it.
#include "buster/compiler/test_paths.h"

// The external tools the cross-checks spawn, probed once per command (#1129), and their tests, for the test-tool command.
// Every test file after this point sees its types and functions.
#if BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) || BUSTER_IN_UNIT(BUSTER_UNIT_TYPES) || BUSTER_IN_UNIT(BUSTER_UNIT_X86) || BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND) || BUSTER_IN_UNIT(BUSTER_UNIT_LINK) || BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/test_tool.c"
#endif

// The lexer benchmark is a separate translation unit, linked into the compiler executable.
int run_c_lexer_benchmark(int argc, char** argv);

// The lexer tests, for the test-lexer command.
#if BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER)
#include "buster/compiler/c_lexer_test.c"
#endif

// The x86-64 encoder and its tests, for the test-x86-encoder command. Its benchmark is another translation unit.
#include "buster/compiler/x86_64_encoder.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) || BUSTER_IN_UNIT(BUSTER_UNIT_X86) || BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND) || BUSTER_IN_UNIT(BUSTER_UNIT_ASSEMBLER) || BUSTER_IN_UNIT(BUSTER_UNIT_LINK) || BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/x86_64_encoder_test.c"
#endif
#include "buster/compiler/x86_zen5.c"
#include "buster/compiler/x86_64_disassembler.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_X86)
#include "buster/compiler/x86_64_disassembler_test.c"
#endif
#include "buster/compiler/x86_64_disassembler_cli.c"
int run_x86_disassembler_benchmark(int argc, char** argv);
int run_x86_encoder_benchmark(int argc, char** argv);

// The lane base layer (groups of threads, and the arenas they allocate from) and its tests, for the test-lanes command.
// Its benchmark is another translation unit.
#include "buster/compiler/lane.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) || BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER) || BUSTER_IN_UNIT(BUSTER_UNIT_EXECUTABLE)
#include "buster/compiler/lane_test.c"
#endif
int run_lane_benchmark(int argc, char** argv);

#include "buster/compiler/diagnostic.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER)
#include "buster/compiler/diagnostic_test.c"
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER)
#include "buster/compiler/diagnostic_benchmark.c"
#endif

// Liveness and interference over aligned bitsets, independent of the future IR's use/def construction.
#include "buster/compiler/dataflow.c"
#include "buster/compiler/dataflow_program.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER)
#include "buster/compiler/dataflow_test.c"
#endif
int run_dataflow_benchmark(int argc, char** argv);
// The tier-neutral emission metadata protocol's tests, for test-emission-metadata.
int run_emission_metadata_tests(int argc, char** argv);

// Entry-rooted dominance, frontiers and natural loops over borrowed CFG columns.
#if BUSTER_BACKEND_CONSUMER
#include "buster/compiler/dominators.c"
#include "buster/compiler/dominators_export.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/dominators_test.c"
#endif

// Local promotion over a completed CFG: vector SSA construction, its nonrecursive scalar twin and semantic tests.
#include "buster/compiler/ssa.c"
#include "buster/compiler/ssa_program.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/ssa_test.c"
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/ssa_benchmark.c"
#endif

#include "buster/compiler/tier1_allocate.c"
#include "buster/compiler/tier1_rematerialize.c"
// #939's allocation certificates: the checker of a source/stream program, the interval allocator's fragment adapter and
// the CFG allocator's producer, which Tier 1's consumers run when their options ask for it.
#include "buster/compiler/tier1_certificate.c"
#include "buster/compiler/tier1_certificate_interval.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/tier1_allocate_test.c"
#endif
#include "buster/compiler/tier1_cfg.c"
#include "buster/compiler/tier1_certificate_cfg.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/tier1_cfg_test.c"
#endif
#endif
int run_tier1_allocate_benchmark(int argc, char** argv);
int run_dominator_benchmark(int argc, char** argv);

// The preprocessor's fuzz tooling (#109): the generator, the clang oracle, the comparison and the minimizer, and their
// self-tests, for the test-preprocessor-fuzz command. test-preprocessor --fuzz runs the preprocessor against them
// (c_preprocessor_fuzz.c).
#if BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND)
#include "buster/compiler/preprocessor_fuzz.c"
#include "buster/compiler/preprocessor_fuzz_generate.c"
#include "buster/compiler/preprocessor_fuzz_clang.c"
#include "buster/compiler/preprocessor_fuzz_test.c"
#include "buster/compiler/preprocessor_fuzz_benchmark.c"
#endif

// External randomized C compiler comparison and its independent process/accounting fixtures.
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/differential.c"
#include "buster/compiler/differential_test.c"
#endif

// Token refinement: identifier interning (c_intern.c) and the per-file pass after the lexer (c_refine.c), and their
// tests, for the test-refinement command. Its benchmark is another translation unit.
#include "buster/compiler/c_intern.c"
#include "buster/compiler/c_refine.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND)
#include "buster/compiler/c_refine_test.c"
#endif
int run_refinement_benchmark(int argc, char** argv);

// The preprocessor (c_preprocessor.c, with the predefined macros of c_predefines.c) and its tests, for the -E and
// test-preprocessor commands.
#include "buster/compiler/c_predefines.c"
#include "buster/compiler/c_preprocessor.c"
#include "buster/compiler/c_builtin_catalog.c"
#include "buster/compiler/test_process.c"
// libm's functions for the tests' oracles, which the compiler's link doesn't take (#652).
#include "buster/compiler/test_libm.c"
// The clang and gcc runs the tests compare with, kept between runs (#542), and its tests, for the test-oracle-cache
// command.
#include "buster/compiler/oracle_cache.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER)
#include "buster/compiler/oracle_cache_test.c"
#endif
#include "buster/compiler/c_preprocessor_test.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND)
#include "buster/compiler/c_predefines_test.c"
#include "buster/compiler/c_preprocessor_fuzz.c"
#endif
#include "buster/compiler/c_preprocessor_oracle.c"
#include "buster/compiler/x86_target_forms.c"
int run_preprocessor_benchmark(int argc, char** argv);

// The per-file jobs on lanes and the run-ahead that fill the preprocessor's file cache (c_files.c), and their tests, for
// the test-files command. Its benchmark is another translation unit.
#include "buster/compiler/c_files.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND)
#include "buster/compiler/c_files_test.c"
#endif
int run_files_benchmark(int argc, char** argv);

// The parser (c_parser.c) and its tests, for the default command, which preprocesses and parses a file, and the
// test-parser command.
#include "buster/compiler/c_parser.c"
#include "buster/compiler/c_parser_test.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND)
#include "buster/compiler/c_gnu_test.c"
#include "buster/compiler/c_gnu_benchmark.c"
#endif
int run_parser_benchmark(int argc, char** argv);

// Scoped name-resolution kernels and their event-stream oracle; c_parser.c includes the shared kernels.
#if BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND)
#include "buster/compiler/c_bindings_test.c"
#include "buster/compiler/c_bindings_benchmark.c"
#endif
#include "buster/compiler/c_parser_bindings.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND)
#include "buster/compiler/c_parser_bindings_test.c"
#include "buster/compiler/c_parser_bindings_benchmark.c"
#endif

// Selective lowering consumes resolved definition/reference columns; the checker producer is still pending.
#include "buster/compiler/c_lowering.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND)
#include "buster/compiler/c_lowering_test.c"
#include "buster/compiler/c_lowering_benchmark.c"
#endif

// The link representation (atoms, symbols, fixups and names, which the object writer and the linker share), the test
// programs generated in it, and their tests, for the test-link command.
#include "buster/compiler/link.c"
#include "buster/compiler/link_program.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_LINK) || BUSTER_IN_UNIT(BUSTER_UNIT_DYNAMIC) || BUSTER_IN_UNIT(BUSTER_UNIT_EXECUTABLE)
#include "buster/compiler/link_test.c"
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_LINK) || BUSTER_IN_UNIT(BUSTER_UNIT_DYNAMIC) || BUSTER_IN_UNIT(BUSTER_UNIT_EXECUTABLE)
#include "buster/compiler/link_order_test.c"
#endif

// The object writer (ET_REL objects from the link representation, for -c) and its tests, for the test-object-writer
// command. Its benchmark is another translation unit.
#include "buster/compiler/object_writer.c"
#include "buster/compiler/object_writer_test.c"
#include "buster/compiler/debug.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_LINK) || BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) || BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
#include "buster/compiler/debug_test.c"
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_LINK) || BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/debug_benchmark.c"
#endif
int run_object_writer_benchmark(int argc, char** argv);

// The import path's readers (ELF objects, archives, shared libraries and linker scripts into the link representation),
// the C runtime's files, found and imported with them, and their tests, for the test-import command.
#include "buster/compiler/import.c"
#include "buster/compiler/runtime.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_LINK) || BUSTER_IN_UNIT(BUSTER_UNIT_DYNAMIC) || BUSTER_IN_UNIT(BUSTER_UNIT_EXECUTABLE)
#include "buster/compiler/import_test.c"
#endif
int run_import_benchmark(int argc, char** argv);

// Binding: the C runtime joined to a program's link and each name bound, and its tests, for the test-bind command. Its
// benchmark is another translation unit.
#include "buster/compiler/bind.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_LINK) || BUSTER_IN_UNIT(BUSTER_UNIT_DYNAMIC) || BUSTER_IN_UNIT(BUSTER_UNIT_EXECUTABLE)
#include "buster/compiler/bind_test.c"
#endif
int run_bind_benchmark(int argc, char** argv);

// The direct path's executables (layout, patching, the build ID and the output file) and their tests, for the
// test-executable command. Its benchmark is another translation unit.
#include "buster/compiler/executable.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_LINK) || BUSTER_IN_UNIT(BUSTER_UNIT_EXECUTABLE) || BUSTER_IN_UNIT(BUSTER_UNIT_DYNAMIC) || BUSTER_IN_UNIT(BUSTER_UNIT_ASSEMBLER) || BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) || BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
#include "buster/compiler/executable_test.c"
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_LINK) || BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/debug_executable_test.c"
#endif
// The object writer's DWARF checks (ObjectOptions.debug), which test-object-writer runs: they need debug.c's units and
// the executable tests' ELF reading.
#if BUSTER_IN_UNIT(BUSTER_UNIT_LINK)
#include "buster/compiler/object_debug_test.c"
#endif
int run_executable_benchmark(int argc, char** argv);

// A dynamic PIE's inputs from the runtime and binding, and the dynamic PIE's tests, for the test-dynamic command.
#include "buster/compiler/dynamic.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_DYNAMIC) || BUSTER_IN_UNIT(BUSTER_UNIT_EXECUTABLE) || BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
#include "buster/compiler/dynamic_test.c"
#endif

// The static executables' tests with the C runtime, for the test-static command.
#if BUSTER_IN_UNIT(BUSTER_UNIT_DYNAMIC) || BUSTER_IN_UNIT(BUSTER_UNIT_EXECUTABLE)
#include "buster/compiler/static_test.c"
#endif

// The runtime snapshot (the imported runtime kept in the cache directory) and its tests, for the test-snapshot command.
// Its benchmark is another translation unit.
#include "buster/compiler/snapshot.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_EXECUTABLE) || BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
#include "buster/compiler/snapshot_test.c"
#endif
int run_snapshot_benchmark(int argc, char** argv);

// Foreign inputs (other compilers' objects, archives, shared libraries and scripts: opened, parsed and resolved, with
// the archive members the link needs loaded and COMDAT groups picked) and their tests, for the test-foreign command. Its
// benchmark is another translation unit.
#include "buster/compiler/inflate.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_EXECUTABLE)
#include "buster/compiler/inflate_test.c"
#endif
#include "buster/compiler/foreign.c"
#include "buster/compiler/foreign_unwind.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_EXECUTABLE)
#include "buster/compiler/foreign_unwind_benchmark.c"
#endif
#include "buster/compiler/foreign_gc.c"
#include "buster/compiler/foreign_link.c"
#include "buster/compiler/foreign_gc_link.c"
#include "buster/compiler/foreign_unwind_dedup.c"
#include "buster/compiler/foreign_unwind_dedup_link.c"
#include "buster/compiler/foreign_strings.c"
#include "buster/compiler/foreign_strings_link.c"
#include "buster/compiler/foreign_driver.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_EXECUTABLE) || BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
#include "buster/compiler/foreign_test.c"
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_EXECUTABLE)
#include "buster/compiler/foreign_gc_benchmark.c"
#include "buster/compiler/foreign_unwind_dedup_benchmark.c"
#endif
int run_foreign_benchmark(int argc, char** argv);
// The link benchmark (the direct path against the object writer and the linkers, on the same programs) and the floors'
// (what the machine and the kernel charge a linker) are other translation units.
int run_link_benchmark(int argc, char** argv);
int run_floors_benchmark(int argc, char** argv);
int run_floors_input_tests(void);

// Branch sizing (each branch's rel8 or rel32 form inside a function, before it's encoded), the functions its tests
// generate, and its tests, for the test-branch-sizing command. Its benchmark is another translation unit.
#include "buster/compiler/branch_sizing.c"
#include "buster/compiler/branch_sizing_generate.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) || BUSTER_IN_UNIT(BUSTER_UNIT_X86)
#include "buster/compiler/branch_sizing_test.c"
#endif
int run_branch_sizing_benchmark(int argc, char** argv);

// What the x86 forms a code generator emits mean (their effects, the immediate's contract, the operand-resolved effects
// and the value-preserving form choice), before the assembler's matcher, which takes its values' immediates from it.
// Its tests, for the test-x86-semantics command, follow the assembler's (they take the encoder's tests' printer and
// branch sizing's tests' runner, and check x86_match.c against the shared rule). Its benchmark is another translation
// unit.
#include "buster/compiler/x86_semantics.c"

#include "buster/compiler/x86_match.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND)
#include "buster/compiler/x86_target_test.c"
#endif
#include "buster/compiler/assembler.c"
#include "buster/compiler/assembler_driver.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_ASSEMBLER)
#include "buster/compiler/assembler_test.c"
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_ASSEMBLER)
#include "buster/compiler/assembler_benchmark.c"
#endif

#include "buster/compiler/x86_semantics_test.c"
// The Zen 5 model's harness and tests (#181), whose fixtures come from the form table and the effects tables, and
// which run only the forms the host's target allows.
#if BUSTER_IN_UNIT(BUSTER_UNIT_X86)
#include "buster/compiler/x86_zen5_measure.c"
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_X86)
#include "buster/compiler/x86_zen5_test.c"
#endif
int run_x86_semantics_benchmark(int argc, char** argv);
// The compiler's code of the headers' x86 intrinsics against clang -O0's, for the test-x86-intrinsics command: the objects
// decoded by the disassembler and followed by the effects above.
#if BUSTER_IN_UNIT(BUSTER_UNIT_X86) || BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
#include "buster/compiler/x86_intrinsics_test.c"
#endif

// Experimental postallocation scheduling over resolved machine-record effects and measured operand costs.
#if BUSTER_BACKEND_CONSUMER
#include "buster/compiler/tier1_schedule.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/tier1_schedule_test.c"
#endif
#endif
int run_tier1_schedule_benchmark(int argc, char** argv);

// The C type table (TypeIds, hash-consing, layout, conversions, compatibility and printing) and its tests, for the
// test-types command. Its benchmark is another translation unit.
#include "buster/compiler/c_types.c"
#include "buster/compiler/c_builtin_intrinsic_types.c"
#include "buster/compiler/c_builtin_intrinsic_features.c"
#include "buster/compiler/c_builtin_intrinsic_catalog.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_TYPES) || BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND) || BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) || BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
#include "buster/compiler/c_types_test.c"
#endif
int run_type_benchmark(int argc, char** argv);

// Immutable source-position histories retain declaration visibility for deferred body consumers.
#include "buster/compiler/c_environment.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND) || BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
#include "buster/compiler/c_environment_test.c"
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND)
#include "buster/compiler/c_environment_benchmark.c"
#endif
// The checker's own code goes in a section of its own, .text.buster_checker, which every unit starts on a page (#502):
// the driver's time depends on where its hot loops fall in the instruction caches, and code compiled before them, in
// their unit or in another, moved them by any number of bytes. In their section, only the checker's own code moves
// them. Its tests and the builtins' actions, which the driver seldom runs, stay in .text.
// BUSTER_CHECKER_OFFSET (0 unless BUSTER_BUILD_DEFINES sets it: a multiple of 16 below a page) starts the checker's
// code that many bytes into its page, after int3 padding. Builds of one tree at several offsets sample where its hot
// loops fall, which ./build.sh shifts can't: it moves units' .text, never this section (#1233, docs/profiling.md).
#ifndef BUSTER_CHECKER_OFFSET
#define BUSTER_CHECKER_OFFSET 0
#endif
_Static_assert(BUSTER_CHECKER_OFFSET >= 0 && BUSTER_CHECKER_OFFSET < 4096 && BUSTER_CHECKER_OFFSET % 16 == 0, "BUSTER_CHECKER_OFFSET must be a multiple of 16 below 4096");
#define BUSTER_CHECKER_OFFSET_TEXT_(offset) #offset
#define BUSTER_CHECKER_OFFSET_TEXT(offset) BUSTER_CHECKER_OFFSET_TEXT_(offset)
__asm__(".section .text.buster_checker,\"ax\",@progbits\n\t.p2align 12\n\t.skip " BUSTER_CHECKER_OFFSET_TEXT(BUSTER_CHECKER_OFFSET) ", 0xcc\n\t.text");
#pragma clang section text = ".text.buster_checker"
#if BUSTER_SOURCE_CONSUMER
#include "buster/compiler/c_check_environment.c"
#include "buster/compiler/c_check_body_bindings.c"
#include "buster/compiler/c_check_lazy.c"
#endif
#pragma clang section text = ""

#include "buster/compiler/c_builtin.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_TYPES) || BUSTER_IN_UNIT(BUSTER_UNIT_CHECK_BUILTINS)
#include "buster/compiler/c_builtin_test.c"
#endif
#if BUSTER_CHECKER_CONSUMER
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
#include "buster/compiler/c_check_builtin_library.c"
#include "buster/compiler/c_check_builtin_atomic.c"
#include "buster/compiler/c_check_builtin_atomic_rmw.c"
#include "buster/compiler/c_check_builtin_atomic_c11.c"
#include "buster/compiler/c_check_builtin_vector.c"
#include "buster/compiler/c_check_builtin_typed_overflow.c"
#include "buster/compiler/c_check_builtin_rounds.c"
#pragma clang section text = ".text.buster_checker"
#include "buster/compiler/c_checker.c"
#if BUSTER_SOURCE_CONSUMER
#include "buster/compiler/c_check_literals.c"
#include "buster/compiler/c_check_warning_source.c"
#include "buster/compiler/c_check_initializer.c"
#include "buster/compiler/c_check_data.c"
#include "buster/compiler/c_check_input.c"
// #113's deferred bodies: check_input with every function body read apart, out of order and on lanes.
#include "buster/compiler/c_check_deferred.c"
#include "buster/compiler/c_check_diagnostic.c"
#include "buster/compiler/c_check_initializer_input.c"
#include "buster/compiler/c_check_data_input.c"
#elif BUSTER_IN_UNIT(BUSTER_UNIT_CHECK_BUILTINS)
#include "buster/compiler/c_check_constant_bytes.c"
#include "buster/compiler/c_check_os_log_input.c"
#endif
#pragma clang section text = ""
#endif
int run_checker_benchmark(int argc, char** argv);
int run_builtin_benchmark(int argc, char** argv);

// The System V calling convention (each type's classes, and each call's argument locations) and its tests, for the
// test-calling-convention command. Its benchmark is another translation unit.
#include "buster/compiler/calling_convention.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_TYPES) || BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
#include "buster/compiler/calling_convention_test.c"
#endif
int run_calling_convention_benchmark(int argc, char** argv);

// The tier-neutral source and frame events and their resolution against a final layout (#1056), which the frame
// emitters below publish and the emission consumes.
#include "buster/compiler/emission_metadata.c"

// Frame storage, explicit prologue/epilogue planning and its native instruction consumer.
#if BUSTER_BACKEND_CONSUMER
#include "buster/compiler/tier1_frame.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/tier1_frame_test.c"
#endif
#include "buster/compiler/tier1_frame_emit.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/tier1_frame_execution_test.c"
#endif

// Tier 0 (a function's typed stream to instruction records, by templates with registers from the value stack's
// depths), the programs its tests and benchmark generate, and its tests, for the test-tier0 command. Its benchmark
// (benchmark-backend) is another translation unit.
#include "buster/compiler/tier0.c"
#include "buster/compiler/tier0_inline.c"
#include "buster/compiler/unwind.c"
#include "buster/compiler/unwind_tier0.c"
// The tier-neutral emitted function (#1056): Tier 0's adapter, the frame events' resolution and their verifier.
#include "buster/compiler/emitted_code.c"
#include "buster/compiler/tier0_generate.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/tier0_test.c"
#endif
#include "buster/compiler/c_builtin_lower.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/c_builtin_lower_test.c"
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/c_builtin_lower_benchmark.c"
#endif
#include "buster/compiler/c_builtin_varargs_lower.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/c_builtin_varargs_lower_test.c"
#endif
#include "buster/compiler/c_builtin_misc_lower.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/c_builtin_misc_lower_test.c"
#endif
#include "buster/compiler/c_builtin_float_lower.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/c_builtin_float_lower_test.c"
#endif
#include "buster/compiler/c_builtin_terminal_helpers.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/c_builtin_terminal_helpers_test.c"
#endif
#include "buster/compiler/c_builtin_atomic_helpers.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/c_builtin_atomic_helpers_test.c"
#endif
#include "buster/compiler/c_builtin_atomic_rmw_helpers.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/c_builtin_atomic_rmw_helpers_test.c"
#endif
#include "buster/compiler/c_builtin_rounds_helper.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/c_builtin_rounds_helper_test.c"
#endif
#include "buster/compiler/c_builtin_math_helpers.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/c_builtin_math_helpers_test.c"
#endif
#include "buster/compiler/c_builtin_x86_helpers.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/tier0_variadic_frame_test.c"
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/unwind_tier0_test.c"
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/unwind_test.c"
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/unwind_native_test.c"
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/unwind_benchmark.c"
#endif
// Streaming the output (#88): Tier 0's code written while it compiles, in the data-first order, against the end.
#include "buster/compiler/streaming.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/streaming_test.c"
#include "buster/compiler/streaming_benchmark.c"
#include "buster/compiler/streaming_benchmark_test.c"
#endif
#endif
int run_backend_benchmark(int argc, char** argv);
int run_tier1_frame_benchmark(int argc, char** argv);

// Source checking's program consumer shares the ABI and Tier 0 interfaces. Keep the source driver and its
// integration tests after those consumers so one checked unit can retain both executable bodies and static data.
#if BUSTER_BACKEND_CONSUMER
#pragma clang section text = ".text.buster_checker"
#include "buster/compiler/c_check_program.c"
#pragma clang section text = ""
#elif BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER)
#pragma clang section text = ".text.buster_checker"
#include "buster/compiler/c_check_clock.c"
#include "buster/compiler/worker_budget.c"
#pragma clang section text = ""
#endif
// #129's oracle: the functions and objects two objects define, compared (ours from -c, clang -O0 -c's), with the names
// the checked source makes, for the checker's selection tests, test-checker-corpus --symbols and test-c-testsuite
// --cross-check.
#include "buster/compiler/c_lowering_symbols.c"
#if BUSTER_SOURCE_CONSUMER
#pragma clang section text = ".text.buster_checker"
#include "buster/compiler/c_check_prefix.c"
#include "buster/compiler/c_check_repair.c"
// One request's semantic configuration (#1057): the code generation and layout refusals, the checked program's target
// and predefines and the measurements' receipt, which the driver, the corpus check and the source preparation share.
#include "buster/compiler/c_check_request.c"
#include "buster/compiler/c_checker_driver.c"
// The driver's commands of several source files (#152): their units on lanes, and one executable of them all.
#include "buster/compiler/c_checker_units.c"
// #2531's measure-attested: attested declarations' census, beside a command's units (check_driver_attested).
#include "buster/compiler/c_check_attested.c"
#pragma clang section text = ""
// #2524's node machine: a full expression's typed entries in one visit a node. Its state and its records' accessors, then
// one file a sweep: T (types), C (contexts and templates) and X (the entries). T's transition table is baked below; its
// shadow (measure-node-machine --shadow) is c_check_node_census.c's.
#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
#include "buster/compiler/c_check_node_machine.c"
#include "buster/compiler/c_check_node_machine_type.c"
#include "buster/compiler/c_check_node_machine_contexts.c"
#include "buster/compiler/c_check_node_machine_expand.c"
#endif
// bake-tables (#590): the backend's tables written into this executable's own file, built, by build.sh, from this
// unit, whose Tier 0 and unwind copies the C driver above uses (baked.h).
#include "buster/compiler/baked.c"
#endif
// The checker unit's own tests compile unoptimized, between `optimize off` and `on` (#681). On the 9700X, the measured
// 716c980d to 4ce693ad pair's checker-unit mean fell from 33.7 to 27.35 s, and compilation-phase wall from 33.7 to
// 28.5 s (two builds per role, excluding final linking and remaining script overhead). AGENTS.md links the frozen
// record, including the small observed SQLite runtime costs, controller limitations and correctness scope.
// No benchmark's timed code calls these tests: they generate inputs and compare results outside the samples.
// A new test of the checker's goes inside.
#pragma clang optimize off
#if BUSTER_CHECKER_CONSUMER
#include "buster/compiler/c_checker_test.c"
#endif
#pragma clang optimize on
#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECK_BUILTINS)
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
#include "buster/compiler/c_check_builtin_bits_test.c"
#include "buster/compiler/c_check_builtin_rounds_test.c"
#endif
#if BUSTER_BACKEND_CONSUMER
#include "buster/compiler/c_check_corpus.c"
#endif
#if BUSTER_SOURCE_CONSUMER
// #1057: a real C file prepared as -c prepares it, which the source benchmarks and the IR's commands on real C share.
#include "buster/compiler/c_check_source_prepare.c"
#endif
#pragma clang optimize off
#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
#include "buster/compiler/c_check_deferred_test.c"
#include "buster/compiler/c_check_mutation_test.c"
#include "buster/compiler/c_check_constraints_test.c"
#include "buster/compiler/c_checker_driver_test.c"
#include "buster/compiler/c_check_debug_test.c"
#endif
// #151's test-check-lanes: the functions' lanes, every output one lane's (check_program_emit_lanes).
#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
#include "buster/compiler/c_check_lanes_test.c"
#endif
// #152's test-check-units: the driver's commands of several source files, every unit's output its own alone's.
#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
#include "buster/compiler/c_check_units_test.c"
#include "buster/compiler/c_check_unwind_test.c"
#include "buster/compiler/c_check_asm_test.c"
#endif
#pragma clang optimize on
#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
#include "buster/compiler/c_check_source_benchmark.c"
#endif
// #113's benchmark-checker-deferred: the deferred reader against source order's, and its position checks.
#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
#include "buster/compiler/c_check_deferred_benchmark.c"
#endif
// #129's benchmark-lowering-source: what lowering only the referenced definitions saves on real units.
#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
#include "buster/compiler/c_lowering_source_benchmark.c"
#endif
// #1048's measure-lowering-templates: how much of the lowering a table of node templates covers.
#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
#include "buster/compiler/c_check_lower_inventory.c"
#endif
// #2524's measure-node-machine: the node machine's census, what its set and silence tests would take.
#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
#include "buster/compiler/c_check_node_census.c"
#endif
// #136's benchmark-backend-source: benchmark-backend's variants on the functions -c compiles of real units.
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) || BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
// Both source-benchmark families use these counters; keep their first inclusion before the IR source helpers.
#include "buster/compiler/tier0_benchmark_measure.c"
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
#include "buster/compiler/tier0_source_benchmark.c"
#endif
// #1398's test-tier0-inline: Tier 0's splice of always_inline functions, its vector path against its twin, on real C.
#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
#include "buster/compiler/c_check_tier0_inline_test.c"
#endif
// #151's benchmark-check-lanes: a unit's functions compiled on lanes, against one lane.
#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
#include "buster/compiler/c_check_lanes_benchmark.c"
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
#include "buster/compiler/c_check_binding_benchmark.c"
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
#include "buster/compiler/c_check_debug_benchmark.c"
#endif
#pragma clang optimize off
#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
#include "buster/compiler/c_check_source_benchmark_test.c"
#include "buster/compiler/c_lowering_source_benchmark_test.c"
#include "buster/compiler/c_check_lower_inventory_test.c"
#include "buster/compiler/c_check_node_census_test.c"
#include "buster/compiler/c_check_attested_test.c"
#endif
// #1048's test-lowering-templates: node templates against the tasks' rules, function for function.
#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
#include "buster/compiler/c_check_program_templates_test.c"
#include "buster/compiler/tier0_source_benchmark_test.c"
#include "buster/compiler/c_check_lanes_benchmark_test.c"
#include "buster/compiler/c_option_table_test.c"
#include "buster/compiler/c_check_request_test.c"
#include "buster/compiler/c_check_corpus_symbols_test.c"
#endif
#pragma clang optimize on

// Tier 1's arena-backed instruction columns, exact verifier twins, stable text form and typed-stream adapter.
#if BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && !BUSTER_SOURCE_CONSUMER
#define BUSTER_IR_STORAGE_ONLY
#include "buster/compiler/ir.c"
#undef BUSTER_IR_STORAGE_ONLY
#endif
#if BUSTER_BACKEND_CONSUMER
#include "buster/compiler/ir.c"
#include "buster/compiler/ir_simd_typed.c"
#include "buster/compiler/ir_lower.c"
#include "buster/compiler/ssa_ir.c"
#include "buster/compiler/ir_validate.c"
#include "buster/compiler/interpreter.c"
#include "buster/compiler/interpreter_native.c"
#include "buster/compiler/interpreter_typed_abi.c"
#include "buster/compiler/interpreter_link.c"
#include "buster/compiler/ir_parse.c"
#include "buster/compiler/ir_interpreter.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/ir_test.c"
#include "buster/compiler/ir_lower_test.c"
#include "buster/compiler/ssa_ir_test.c"
#include "buster/compiler/ir_parse_test.c"
#include "buster/compiler/interpreter_test.c"
#include "buster/compiler/tier0_mask_test.c"
#include "buster/compiler/interpreter_native_test.c"
#include "buster/compiler/interpreter_link_test.c"
#include "buster/compiler/ir_interpreter_test.c"
#endif
#include "buster/compiler/interpreter_driver.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/ir_benchmark.c"
#include "buster/compiler/ir_lower_benchmark.c"
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) || BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
#include "buster/compiler/ir_source_test.c"
#else
// Keep the production pass at its first include position without the source-test preparation interface.
#include "buster/compiler/ir_dataflow.c"
#endif
#include "buster/compiler/tier1_cfg_ir.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/tier1_cfg_source_test.c"
#include "buster/compiler/tier1_cfg_source_benchmark.c"
#include "buster/compiler/tier1_certificate_benchmark.c"
#endif

// Verified Tier 1 optimization passes and independent before/after semantic tests.
#include "buster/compiler/ir_optimize.c"
// #141: Tier 1's 128-bit integers as pairs of 64-bit halves, before the optimizer.
#include "buster/compiler/ir_wide.c"
// #1624: Tier 1's vectors wider than the target's registers as its widest vectors' pieces, before the optimizer.
#include "buster/compiler/ir_vector_split.c"
#include "buster/compiler/ir_rebuild.c"
#include "buster/compiler/ir_reduce.c"
#include "buster/compiler/ir_reduce_oracle.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/ir_reduce_test.c"
#include "buster/compiler/ir_reduce_driver.c"
#include "buster/compiler/ir_reduce_driver_test.c"
#endif
#include "buster/compiler/ir_optimize_inline.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/ir_optimize_inline_test.c"
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/ir_optimize_test.c"
#else
// The production pass was first included by the test root here. Keep that position without its fixture dependency.
#include "buster/compiler/ir_if_convert.c"
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/ir_optimize_benchmark.c"
#endif
#include "buster/compiler/ir_optimize_gvn.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/ir_optimize_gvn_test.c"
#endif
#include "buster/compiler/ir_optimize_licm.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/ir_optimize_licm_test.c"
#endif
#include "buster/compiler/ir_optimize_sroa.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/ir_optimize_sroa_test.c"
#include "buster/compiler/ir_optimize_inline_simd_test.c"
#include "buster/compiler/ir_optimize_inline_cfg_test.c"
#endif
// #175's first #139 pass tests in the IR's text form: hand-written before/after pairs under tests/ir/optimize.
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/ir_optimize_text_test.c"
#else
#include "buster/compiler/ir_optimize_verified.c"
#endif
// #866's GVN variants on real source's lowered IR are part of #131's IR benchmark, which needs GVN's definitions.
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/ir_source_benchmark.c"
#include "buster/compiler/ir_optimize_passes_benchmark.c"
#include "buster/compiler/ir_metamorphic.c"
#endif

// Selected effective uses feed liveness, allocation and the bounded native function consumer.
#include "buster/compiler/tier1_select.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) || BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
#include "buster/compiler/tier1_select_test.c"
#endif
#include "buster/compiler/tier1_liveness.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/tier1_liveness_test.c"
#endif
#include "buster/compiler/tier1_emit.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/tier1_emit_test.c"
#include "buster/compiler/tier1_pipeline_test.c"
#endif
// #179: hand-written IR of AVX-512's masks through Tier 1, natively against the shared IR's interpreter.
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/tier1_masks_test.c"
#endif
// #141: the 128-bit integers' split into halves through Tier 1, natively against the shared IR's interpreter.
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/tier1_wide_test.c"
#endif
#endif
// The driver remains the baked tables' storage home; it needs their declarations without the selection kernels.
#if BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && !BUSTER_SOURCE_CONSUMER
#define BUSTER_TIER1_BURS_STORAGE_ONLY
#include "buster/compiler/tier1_select_burs.c"
#undef BUSTER_TIER1_BURS_STORAGE_ONLY
#endif
// #179: what a mask's spill costs, to memory against to a general register, and the programs' checks.
#if BUSTER_IN_UNIT(BUSTER_UNIT_X86)
#include "buster/compiler/mask_spill_measure.c"
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_X86)
#include "buster/compiler/mask_spill_measure_test.c"
#endif
#if BUSTER_BACKEND_CONSUMER
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/tier1_pipeline_benchmark.c"
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) || BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
#include "buster/compiler/tier1_select_source_benchmark.c"
#endif
// #939: the certificate of the CFG consumer's code on real C, decoded from its records, beside the emission it certifies:
// the checker's unit's command, as its corpus's preparation's home.
#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
#include "buster/compiler/tier1_code_certificate_benchmark.c"
#endif
// The C driver's --tier1 (#138's piece for #140): each function Tier 1's pipeline takes compiled by it.
#pragma clang section text = ".text.buster_checker"
#if BUSTER_SOURCE_CONSUMER
#include "buster/compiler/c_check_tier1.c"
#endif
#pragma clang optimize off
#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
#include "buster/compiler/c_check_debug_tier1_parameters_test.c"
#include "buster/compiler/c_check_tier1_outcome_test.c"
#endif
#pragma clang optimize on
#pragma clang section text = ""
// #1056: the tier-neutral emitted function's tests (row programs, Tier 0's and Tier 1's frame events, the verifier).
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/emitted_code_test.c"
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/x86_target_selection_test.c"
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/ir_stack_test.c"
#include "buster/compiler/ir_simd_typed_test.c"
#include "buster/compiler/ir_simd_consumer_test.c"
#endif
#endif
int run_tier1_select_benchmark(int argc, char** argv);
bool run_x86_target_selection_tests(void);
int run_tier1_model_measure(int argc, char** argv);
int run_tier1_model_measure_tests(int argc, char** argv);

// The call graph check (the style's rule against recursion, on the compiler's own LLVM IR) and its tests, for the
// check-call-graph and test-call-graph commands.
#include "buster/compiler/call_graph.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER)
#include "buster/compiler/call_graph_test.c"
#endif

// The profiling tools (ab, and the reports of perf's and objdump's text: hot-loops, label-spills, spill-report and
// branch-misses) and their tests, for the test-profile command.
#include "buster/compiler/profile.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER)
#include "buster/compiler/profile_test.c"
#endif

// measure-compile (#518): whole compiles timed as processes against a reference compiler, with --time-stages' report of
// where their time goes.
#if BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER)
#include "buster/compiler/compile_measure.c"
#endif

// #1128's benchmark-check-units: a command of several units timed as processes by each lane count, with its system time
// against one lane's (time-runs' runs, of test-check-units' program of synth's shape).
#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
#include "buster/compiler/c_check_units_benchmark.c"
#endif
// measure-tier1-programs (#138, #1189): speedtest1 and Lua's scripts by both tiers, clang -O0 and -O2 and tcc, as whole
// processes in a Williams design's order, against each other.
#if BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER)
#include "buster/compiler/tier1_programs_measure.c"
#endif
// test-sqlite (#146): SQLite's shell by both tiers against clang's, over SQLite's CLI tests and tests/sqlite's scripts.
#if BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER)
#include "buster/compiler/sqlite_test.c"
// test-sqlite-testfixture (#146): SQLite's Tcl test suite through testfixture by both tiers against clang's.
#include "buster/compiler/sqlite_testfixture.c"
#endif
// measure-tier1-rows (#1283): vector code (test-checker-driver's machine rows' unit, a lexer's byte scan and the compiler's
// own lexer) by Tier 0, Tier 1 and another compiler's Tier 1, with the moves around Tier 1's machine rows counted.
#if BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER)
#include "buster/compiler/tier1_rows_measure.c"
#endif
// measure-frames (#177): the frames functions' code makes, by build, against another build's (Tier 1's against clang
// -O2's, with and without the objects' lifetimes).
#if BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER)
#include "buster/compiler/frame_measure.c"
#endif
// measure-helpers (#1059): the direct forms against the helpers they replace (--helper-forms=all), by both tiers, as
// whole processes in a Williams design's order: the primitives' kernels and speedtest1.
#if BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER)
#include "buster/compiler/helpers_measure.c"
#endif

// The runner the 9700X job runs its tests through (a list of commands at once, each one's output printed in the list's
// order) and its tests, for the parallel and test-parallel commands.
#include "buster/compiler/parallel.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER)
#include "buster/compiler/parallel_test.c"
#endif

// The c-testsuite harness (#145), for the test-c-testsuite command: each program compiled and linked by this
// executable, run and compared with its expected output.
#if BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) || BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/c_testsuite.c"
#endif

// #134's test-ssa-source: every function of real and generated C promoted by both of ssa_ir.c's paths with the SSA
// verifier's checks, and each program with a main run by the IR's interpreter in its three forms (all locals in memory,
// and each path's SSA), against its expected output.
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#include "buster/compiler/ssa_source_generate.c"
#include "buster/compiler/ssa_source_test.c"
#include "buster/compiler/interpreter_source_driver.c"
#endif

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
int main_preprocessor_command(int count, char** arguments);
// Their modes that release what they map before returning, and main_source_command's work through them, for a caller
// that goes on running: the driver tests' in-process runs (#2071).
int run_source_check_command_reusable(int count, char** arguments);
int main_preprocessor_command_reusable(int count, char** arguments);
int main_source_command_reusable(int argc, char** argv);

#if BUSTER_UNIT == BUSTER_UNIT_DRIVER
#include "buster/compiler/c_driver_information.c"

// Select the GCC-style preprocessing action without mistaking an option's value for an action. Keep the value-taking
// options in step with pp_option_takes_value and the C driver's own options.
static bool main_preprocessing(int count, char** arguments)
{
    bool result = false;
    for (int i = 0; !result && i < count; i += 1)
    {
        // The argument's option ID (c_option_table.c).
        const COption option = c_option_exact(arguments[i]);
        if (option == C_OPTION_END_OF_OPTIONS)
        {
            break;
        }
        result = option == C_OPTION_E || option == C_OPTION_M || option == C_OPTION_MM;
        if ((pp_option_id_takes_value(option) || option == C_OPTION_LANES || option == C_OPTION_LANE_CPUS || option == C_OPTION_WORKER_BUDGET ||
             option == C_OPTION_ID_ORDER || option == C_OPTION_L_LIBRARY || option == C_OPTION_L_DIRECTORY || option == C_OPTION_XLINKER) &&
            i + 1 < count)
        {
            i += 1;
        }
    }
    return result;
}
#endif

#if BUSTER_SOURCE_CONSUMER && !BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
// These shared static entry points were referenced only by the checker fixtures now read in their owning unit.
// Keep those semantic references without parsing the fixtures: this unused function is never called or emitted.
// Other unused-function diagnostics remain enabled, and the checker unit still checks the interfaces' real callers.
static __attribute__((unused)) void main_checker_test_interfaces(void)
{
    (void)&check_clock_read;
    (void)&check_program_lower;
    (void)&check_program_lower_owners;
    (void)&check_source_left_out;
    (void)&check_source_print_receipt;
    (void)&check_source_report;
    (void)&tier1_certificate_emitted;
    (void)&tier1_certificate_emitted_check;
    (void)&c_body_bindings_span;
    (void)&c_body_bindings_spans;
    (void)&c_body_bindings_resolve;
    (void)&check_input_control_next;
    (void)&check_input_label_capacity;
    (void)&check_diagnostic_first_pragma_error;
    (void)&check_initializers_diagnose;
    (void)&check_data_input;
    (void)&check_program_lower_sources;
    (void)&check_program_lower_templates;
    (void)&check_debug_value_range_scan;
    (void)&check_debug_value_range_indexed;
    (void)&lowering_symbols_compare_strings;
    (void)&run_check_command;
    (void)&check_corpus_check;
    (void)&run_bake_command;
    (void)&run_baked_tables_test;
    (void)&run_checker_corpus;
}
#endif

#if BUSTER_COMPLETE_DRIVER
// The complete driver reads the source interfaces but not the checker-owned IR commands which normally call these.
static __attribute__((unused)) void main_ir_source_interfaces(void)
{
    (void)&check_program_access;
    (void)&check_request_describe_text;
    (void)&check_source_prepare;
    (void)&check_source_release;
}
#endif

#if !BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND)
// The frontend fixtures supply these shared helpers' callers. Retain their semantic references in the other units
// without parsing the fixtures or emitting code; the frontend still checks the real calls with unused warnings on.
static __attribute__((unused)) void main_frontend_test_interfaces(void)
{
    (void)&pp_test_read_output;
    (void)&parse_test_status_bytes;
    (void)&parse_test_lowest_descriptor;
    (void)&parse_test_read_capture;
    (void)&parse_test_clang;
    (void)&parse_test_write;
    (void)&c_environment_lookup;
    (void)&c_environment_types_scalar;
    (void)&c_environment_types;
}
#endif

#if BUSTER_BACKEND_CONSUMER && !BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
// The backend fixtures supply these shared static entry points' callers in their owning unit. Keep their
// semantic references in the others without parsing the fixtures or emitting code; the owner retains real callers.
static __attribute__((unused)) void main_backend_test_interfaces(void)
{
    (void)&dominators_export_begin;
    (void)&dominators_export_cfg;
    (void)&dominators_export_complete;
    (void)&dominators_export_read;
    (void)&tier0_tables_open;
    (void)&tier0_tables_close;
    (void)&ir_lower_typed_path;
    (void)&tier1_cfg_from_ir;
    (void)&ir_reduce_oracle;
    (void)&tier1_select_emit;
    (void)&tier1_select_plan;
    (void)&stream_program_generate;
    (void)&stream_write;
    (void)&stream_write_link;
    (void)&run_ir_interpreter;
}
#endif

#if !BUSTER_IN_UNIT(BUSTER_UNIT_EXECUTABLE)
// The foreign fixtures supply this shared helper's callers. Keep its semantic reference in the other units without
// parsing the fixtures or emitting code; the executable unit still checks the real callers with unused warnings on.
static __attribute__((unused)) void main_foreign_test_interfaces(void)
{
    (void)&inflate_adler32;
    (void)&foreign_unwind_dedup_section_offset;
}
#endif

#if !BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER)
static __attribute__((unused)) void main_driver_test_interfaces(void)
{
    (void)&diagnostic_append;
    (void)&diagnostic_merge;
    (void)&diagnostic_write;
    (void)&oracle_test_run;
    (void)&oracle_test_fresh_builtin;
    (void)&oracle_test_fresh_begin;
    (void)&oracle_test_fresh_end;
    (void)&oracle_cache_disable;
    (void)&oracle_epoch_begin;
    (void)&oracle_epoch_end;
    (void)&oracle_epoch_file;
}
#endif

static __attribute__((unused)) void main_x86_test_interfaces(void)
{
    (void)&x86_decode_classify_scalar;
    (void)&x86_target_forms_scalar;
    (void)&branch_sizing_generate;
    (void)&branch_sizing_chain;
    (void)&x86_zen5_case_find;
    (void)&x86_zen5_missing;
}

#if !BUSTER_IN_UNIT(BUSTER_UNIT_TYPES)
static __attribute__((unused)) void main_type_test_interfaces(void)
{
    (void)&cbuiltin_lookup_scalar;
    (void)&cbuiltin_lookup;
}
#endif

#if !BUSTER_IN_UNIT(BUSTER_UNIT_LINK)
static __attribute__((unused)) void main_link_test_interfaces(void)
{
    (void)&debug_write;
}
#endif

#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
static __attribute__((unused)) void main_benchmark_test_interfaces(void)
{
    (void)&tier0_benchmark_reserve;
    (void)&tier0_benchmark_add;
    (void)&tier0_benchmark_measure;
}
#endif

#if !BUSTER_SOURCE_CONSUMER
// Removing the source consumers also removes their references to shared helpers. Keep those references without
// parsing the source closure or emitting code; the owning units still check the real calls with warnings enabled.
static __attribute__((unused)) void main_checker_consumer_interfaces(void)
{
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
    (void)&check_varargs;
    (void)&check_builtin_float_strict;
    (void)&check_builtin_float;
    (void)&tier0_inline;
    (void)&tier0_inline_scalar;
    (void)&tier0_inline_code;
    (void)&cbuiltin_x86_helper_kind;
    (void)&cbuiltin_x86_helper;
    (void)&check_vector_baseline_resize;
    (void)&check_vector_baseline_extremes;
    (void)&check_vector_baseline_absolute;
    (void)&check_vector_baseline_shuffle_row;
    (void)&check_vector_baseline_complement;
    (void)&check_vector_lower_element_row;
    (void)&check_vector_lower_lanewise;
    (void)&check_vector_lower_brain;
    (void)&check_vector_lower_brain_rows;
    (void)&check_x86_lower_rule;
    (void)&check_x86_lower_reduce_row;
    (void)&check_test_value_equal;
#endif
#if BUSTER_CHECKER_CONSUMER
    (void)&check_warning_set_add;
    (void)&check_value_copy_floating;
    (void)&check_value_static_initializer;
    (void)&check_const_append;
    (void)&check_vector_member_decode;
    (void)&check_warning_null_integer;
    (void)&check_warning_ordered_event;
    (void)&check_builtin_atomic_c11_native;
    (void)&check_actions;
    (void)&check_action_integer_constant;
    (void)&check_action_i64;
    (void)&check_action_member_lookup;
    (void)&check_builtin_library_fold;
    (void)&check_builtin_library_fold_memory;
    (void)&check_library_plain_prototype;
    (void)&check_builtin_library_fold_plain;
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER)
    (void)&check_clock_start;
    (void)&check_clock_mark;
    (void)&check_clock_report;
#endif
    (void)&oracle_cache_report;
    (void)&pp_oracle_includes;
    (void)&parse_pragma_at;
    (void)&parse_postorder;
    (void)&parse_tree_columns;
    (void)&parse_tree_links;
    (void)&parse_release;
    (void)&parse_test_outcome;
    (void)&parse_test_random;
    (void)&parse_test_collect;
    (void)&parse_test_preprocessed;
    (void)&parse_bindings_build;
    (void)&parse_bindings_resolve;
    (void)&asm_assemble_code;
    (void)&debug_finalize;
    (void)&x86_zen5_initialize;
    (void)&x86_zen5_lookup_scalar;
    (void)&x86_zen5_lookup;
    (void)&c_environment_build;
    (void)&c_environment_lookup_batch;
    (void)&c_environment_bodies;
    (void)&cbuiltin_fold_scalar;
    (void)&cbuiltin_fold;
    (void)&lowering_symbols_read_file;
    (void)&lowering_symbols_compare;
    (void)&lowering_symbols_compare_strings;
}
#endif

// Runs the command argv[1] names if it is one of this unit's (*found). Each branch first tests the unit whose code it
// is, so that the other units' branches fold to false (at the top). What no unit takes runs the compiler itself
// (main_source_command), in a frame of its own: noinline keeps this function's frame, the tests' locals, out of main's.
static __attribute__((noinline)) int main_command(int argc, char** argv, bool* found)
{
    int result = 0;
    *found = true;
    if (!BUSTER_HOST_VECTOR && argc >= 2 && (strncmp(argv[1], "benchmark-", 10) == 0 || strcmp(argv[1], "measure-x86-zen5") == 0 || strcmp(argv[1], "measure-mask-spills") == 0))
    {
        fprintf(stderr, "benchmarks require an AVX-512 host build; run tests with this scalar build\n");
        result = 1;
    }
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-ssa") == 0)
    {
        result = run_ssa_tests(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "benchmark-ssa") == 0)
    {
        result = run_ssa_benchmark(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-ssa-ir") == 0)
    {
        result = run_ssa_ir_tests(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc == 2 && strcmp(argv[1], "test-diagnostics") == 0)
    {
        result = run_diagnostic_tests();
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER)
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-diagnostics") == 0)
    {
        result = run_diagnostic_benchmark(argc - 2, argv + 2);
    }
#endif
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_ASSEMBLER) && argc >= 2 && strcmp(argv[1], "assemble") == 0)
    {
        result = run_assembler_command(argc - 2, argv + 2);
    }
#if BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "test-x86-semantics") == 0)
    {
        result = run_x86_semantics_tests(argc - 2, argv + 2);
    }
#endif
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-x86-semantics") == 0)
    {
        result = run_x86_semantics_benchmark(argc - 1, argv + 1);
    }
#if BUSTER_IN_UNIT(BUSTER_UNIT_X86)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_X86) && argc >= 2 && strcmp(argv[1], "test-x86-intrinsics") == 0)
    {
        result = run_x86_intrinsic_tests(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_ASSEMBLER)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_ASSEMBLER) && argc >= 2 && strcmp(argv[1], "test-assembler") == 0)
    {
        result = run_assembler_tests(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_ASSEMBLER)
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_ASSEMBLER) && argc >= 2 && strcmp(argv[1], "benchmark-assembler") == 0)
    {
        result = run_assembler_benchmark(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND) && argc >= 2 && strcmp(argv[1], "test-preprocessor") == 0)
    {
        result = run_preprocessor_tests(argc - 2, argv + 2);
    }
#endif
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-preprocessor") == 0)
    {
        result = run_preprocessor_benchmark(argc - 1, argv + 1);
    }
#if BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND) && argc >= 2 && strcmp(argv[1], "test-parser") == 0)
    {
        result = run_parser_tests(argc - 2, argv + 2);
    }
#endif
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-parser") == 0)
    {
        result = run_parser_benchmark(argc - 1, argv + 1);
    }
#if BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND)
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
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "test-lexer") == 0)
    {
        result = run_c_lexer_tests(argv[0], argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER)
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-lexer") == 0)
    {
        result = run_c_lexer_benchmark(argc - 1, argv + 1);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_X86)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_X86) && argc >= 2 && strcmp(argv[1], "test-x86-encoder") == 0)
    {
        result = run_x86_encoder_tests(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND) && argc >= 2 && strcmp(argv[1], "test-x86-target") == 0)
    {
        result = run_x86_target_tests();
    }
#endif
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-x86-target-selection") == 0)
    {
        result = run_x86_target_selection_tests() ? 0 : 1;
    }
#if BUSTER_IN_UNIT(BUSTER_UNIT_X86)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_X86) && argc >= 2 && strcmp(argv[1], "test-x86-zen5") == 0)
    {
        result = run_x86_zen5_tests();
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_X86)
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_X86) && argc >= 2 && strcmp(argv[1], "benchmark-x86-zen5") == 0)
    {
        result = run_x86_zen5_benchmark(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_X86)
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_X86) && argc >= 2 && strcmp(argv[1], "measure-x86-zen5") == 0)
    {
        result = run_x86_zen5_measure(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_X86)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_X86) && argc >= 2 && strcmp(argv[1], "test-mask-spills") == 0)
    {
        result = run_mask_spill_tests();
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_X86)
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_X86) && argc >= 2 && strcmp(argv[1], "measure-mask-spills") == 0)
    {
        result = run_mask_spill_measure(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND)
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND) && argc >= 2 && strcmp(argv[1], "benchmark-x86-target") == 0)
    {
        result = run_x86_target_benchmark(argc - 2, argv + 2);
    }
#endif
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-x86-encoder") == 0)
    {
        result = run_x86_encoder_benchmark(argc - 1, argv + 1);
    }
#if BUSTER_IN_UNIT(BUSTER_UNIT_X86)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_X86) && argc >= 2 && strcmp(argv[1], "test-x86-disassembler") == 0)
    {
        result = run_x86_disassembler_tests(argc - 2, argv + 2);
    }
#endif
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "disassemble") == 0)
    {
        result = run_x86_disassembler(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-x86-disassembler") == 0)
    {
        result = run_x86_disassembler_benchmark(argc - 1, argv + 1);
    }
#if BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "test-lanes") == 0)
    {
        result = run_lane_tests(argc - 2, argv + 2);
    }
#endif
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-lanes") == 0)
    {
        result = run_lane_benchmark(argc - 1, argv + 1);
    }
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-dominators") == 0)
    {
        result = run_dominator_tests(argc - 2, argv + 2);
    }
#endif
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "benchmark-dominators") == 0)
    {
        result = run_dominator_benchmark(argc - 2, argv + 2);
    }
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-tier1-allocation") == 0)
    {
        result = run_tier1_allocate_tests(argc - 2, argv + 2);
    }
#endif
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "benchmark-tier1-allocation") == 0)
    {
        result = run_tier1_allocate_benchmark(argc - 2, argv + 2);
    }
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-tier1-cfg") == 0)
    {
        result = run_tier1_cfg_tests(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-tier1-schedule") == 0)
    {
        result = run_tier1_schedule_tests(argc - 2, argv + 2);
    }
#endif
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "benchmark-tier1-schedule") == 0)
    {
        result = run_tier1_schedule_benchmark(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "test-emission-metadata") == 0)
    {
        result = run_emission_metadata_tests(argc - 2, argv + 2);
    }
#if BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "test-dataflow") == 0)
    {
        result = run_dataflow_tests(argc - 2, argv + 2);
    }
#endif
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-dataflow") == 0)
    {
        result = run_dataflow_benchmark(argc - 1, argv + 1);
    }
#if BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND)
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
#endif
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-refinement") == 0)
    {
        result = run_refinement_benchmark(argc - 1, argv + 1);
    }
#if BUSTER_IN_UNIT(BUSTER_UNIT_LINK)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_LINK) && argc >= 2 && strcmp(argv[1], "test-link") == 0)
    {
        result = run_link_tests(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_LINK)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_LINK) && argc >= 2 && strcmp(argv[1], "test-object-writer") == 0)
    {
        result = run_object_writer_tests(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_LINK)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_LINK) && argc >= 2 && strcmp(argv[1], "test-debug") == 0)
    {
        result = run_debug_tests(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_LINK)
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_LINK) && argc >= 2 && strcmp(argv[1], "benchmark-debug") == 0)
    {
        result = run_debug_benchmark(argc - 2, argv + 2);
    }
#endif
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-object-writer") == 0)
    {
        result = run_object_writer_benchmark(argc - 1, argv + 1);
    }
#if BUSTER_IN_UNIT(BUSTER_UNIT_LINK)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_LINK) && argc >= 2 && strcmp(argv[1], "test-import") == 0)
    {
        result = run_import_tests(argc - 2, argv + 2);
    }
#endif
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-import") == 0)
    {
        result = run_import_benchmark(argc - 1, argv + 1);
    }
#if BUSTER_IN_UNIT(BUSTER_UNIT_LINK)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_LINK) && argc >= 2 && strcmp(argv[1], "test-bind") == 0)
    {
        result = run_bind_tests(argc - 2, argv + 2);
    }
#endif
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-bind") == 0)
    {
        result = run_bind_benchmark(argc - 1, argv + 1);
    }
#if BUSTER_IN_UNIT(BUSTER_UNIT_EXECUTABLE)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_EXECUTABLE) && argc >= 2 && strcmp(argv[1], "test-executable") == 0)
    {
        result = run_executable_tests(argc - 2, argv + 2);
    }
#endif
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-executable") == 0)
    {
        result = run_executable_benchmark(argc - 1, argv + 1);
    }
#if BUSTER_IN_UNIT(BUSTER_UNIT_DYNAMIC)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DYNAMIC) && argc >= 2 && strcmp(argv[1], "test-dynamic") == 0)
    {
        result = run_dynamic_tests(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_DYNAMIC)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DYNAMIC) && argc >= 2 && strcmp(argv[1], "test-static") == 0)
    {
        result = run_static_tests(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_EXECUTABLE)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_EXECUTABLE) && argc >= 2 && strcmp(argv[1], "test-snapshot") == 0)
    {
        result = run_snapshot_tests(argc - 2, argv + 2);
    }
#endif
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-snapshot") == 0)
    {
        result = run_snapshot_benchmark(argc - 1, argv + 1);
    }
#if BUSTER_IN_UNIT(BUSTER_UNIT_EXECUTABLE)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_EXECUTABLE) && argc >= 2 && strcmp(argv[1], "test-inflate") == 0)
    {
        result = run_inflate_tests(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_EXECUTABLE)
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
#endif
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_EXECUTABLE) && argc >= 2 && strcmp(argv[1], "link-foreign") == 0)
    {
        result = run_foreign_link(argc - 2, argv + 2);
    }
#if BUSTER_IN_UNIT(BUSTER_UNIT_EXECUTABLE)
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_EXECUTABLE) && argc >= 2 && strcmp(argv[1], "benchmark-foreign-gc") == 0)
    {
        result = run_foreign_gc_benchmark(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_EXECUTABLE) && argc >= 2 && strcmp(argv[1], "benchmark-foreign-unwind-dedup") == 0)
    {
        result = run_foreign_unwind_dedup_benchmark(argc - 2, argv + 2);
    }
#endif
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
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc == 2 && strcmp(argv[1], "test-floors-input") == 0)
    {
        result = run_floors_input_tests();
    }
#if BUSTER_IN_UNIT(BUSTER_UNIT_X86)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_X86) && argc >= 2 && strcmp(argv[1], "test-branch-sizing") == 0)
    {
        result = run_branch_sizing_tests(argc - 2, argv + 2);
    }
#endif
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-branch-sizing") == 0)
    {
        result = run_branch_sizing_benchmark(argc - 1, argv + 1);
    }
#if BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND) && argc >= 2 && strcmp(argv[1], "test-files") == 0)
    {
        result = run_files_tests(argc - 2, argv + 2);
    }
#endif
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-files") == 0)
    {
        result = run_files_benchmark(argc - 1, argv + 1);
    }
#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER) && argc >= 2 && strcmp(argv[1], "bake-tables") == 0)
    {
        result = run_bake_command(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER) && argc >= 2 && strcmp(argv[1], "test-baked-tables") == 0)
    {
        result = run_baked_tables_test(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER) && argc >= 2 && strcmp(argv[1], "check") == 0)
    {
        arena_static_base_pages();
        arena_limits_fix();
        result = run_check_command_mode(argc - 2, argv + 2, false, false);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND) && argc >= 2 && strcmp(argv[1], "parse") == 0)
    {
        arena_static_base_pages();
        arena_limits_fix();
        result = run_parse_command(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
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
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER) && argc >= 2 && strcmp(argv[1], "test-checker-fortify") == 0)
    {
        result = run_check_fortify_tests(argc - 2, argv + 2);
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
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER) && argc >= 2 && strcmp(argv[1], "test-check-units") == 0)
    {
        result = run_check_units_tests(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECK_BUILTINS)
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
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_CHECK_BUILTINS) && argc >= 2 && strcmp(argv[1], "test-check-builtins-bits") == 0)
    {
        result = run_check_builtin_bits_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_CHECK_BUILTINS) && argc >= 2 && strcmp(argv[1], "test-check-builtins-rounds") == 0)
    {
        result = run_check_builtin_rounds_tests(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-builtin-lower") == 0)
    {
        result = run_builtin_lower_tests(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-builtin-varargs-lower") == 0)
    {
        result = run_builtin_varargs_lower_tests(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-builtin-misc-lower") == 0)
    {
        result = run_builtin_misc_lower_tests(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-builtin-float-lower") == 0)
    {
        result = run_builtin_float_lower_tests(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-builtin-terminal-helpers") == 0)
    {
        result = run_builtin_terminal_helper_tests(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-builtin-atomic-helpers") == 0)
    {
        result = run_builtin_atomic_helper_tests(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-builtin-rounds-helper") == 0)
    {
        result = run_builtin_rounds_helper_tests(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-builtin-math-helpers") == 0)
    {
        result = run_builtin_math_helper_tests(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND) && argc >= 2 && strcmp(argv[1], "test-environments") == 0)
    {
        result = run_c_environment_tests(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND)
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND) && argc >= 2 && strcmp(argv[1], "benchmark-environments") == 0)
    {
        result = run_c_environment_benchmark(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER) && argc >= 2 && strcmp(argv[1], "test-checker-corpus") == 0)
    {
        result = run_checker_corpus(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER) && argc >= 2 && strcmp(argv[1], "test-checker-deferred") == 0)
    {
        result = run_checker_deferred_tests(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER) && argc >= 2 && strcmp(argv[1], "benchmark-checker-deferred") == 0)
    {
        result = run_checker_deferred_benchmark(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER) && argc >= 2 && strcmp(argv[1], "benchmark-checker-source") == 0)
    {
        result = run_checker_source_benchmark(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER) && argc >= 2 && strcmp(argv[1], "benchmark-lowering-source") == 0)
    {
        result = run_lowering_source_benchmark(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER) && argc >= 2 && strcmp(argv[1], "measure-lowering-templates") == 0)
    {
        result = run_lowering_inventory(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER) && argc >= 2 && strcmp(argv[1], "measure-node-machine") == 0)
    {
        result = run_node_census(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER) && argc >= 2 && strcmp(argv[1], "measure-attested") == 0)
    {
        arena_static_base_pages();
        arena_limits_fix();
        result = run_attested_census(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER) && argc >= 2 && strcmp(argv[1], "test-lowering-templates") == 0)
    {
        result = run_lowering_templates_tests(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-ir-source") == 0)
    {
        result = run_ir_source_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-tier1-cfg-source") == 0)
    {
        result = run_tier1_cfg_source_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "benchmark-tier1-cfg-source") == 0)
    {
        result = run_tier1_cfg_source_benchmark(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "benchmark-tier1-certificate") == 0)
    {
        result = run_tier1_certificate_benchmark(argc - 2, argv + 2);
    }
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "benchmark-tier1-select-source") == 0)
    {
        result = run_tier1_select_source_benchmark(argc - 2, argv + 2);
    }
#endif
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER) && argc >= 2 && strcmp(argv[1], "benchmark-tier1-code-certificate") == 0)
    {
        // The checker's unit, the home of its corpus's preparation (ir_source_prepare), whose emitter is the one it times:
        // the code certificate's command reaches one copy of the emitter's lazily built tables (x86_zen5_id's).
        result = run_tier1_code_certificate_benchmark(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-ssa-source") == 0)
    {
        result = run_ssa_source_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "benchmark-ir-source") == 0)
    {
        result = run_ir_source_benchmark(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER) && argc >= 2 && strcmp(argv[1], "test-tier0-inline") == 0)
    {
        result = run_tier0_inline_tests(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER) && argc >= 2 && strcmp(argv[1], "benchmark-backend-source") == 0)
    {
        result = run_backend_source_benchmark(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER) && argc >= 2 && strcmp(argv[1], "benchmark-check-lanes") == 0)
    {
        result = run_check_lanes_benchmark(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER) && argc >= 2 && strcmp(argv[1], "benchmark-check-units") == 0)
    {
        result = run_check_units_benchmark(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER) && argc >= 2 && strcmp(argv[1], "benchmark-checker-bindings") == 0)
    {
        result = run_checker_binding_benchmark(argc - 2, argv + 2);
    }
#endif
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-checker") == 0)
    {
        result = run_checker_benchmark(argc - 1, argv + 1);
    }
#if BUSTER_IN_UNIT(BUSTER_UNIT_TYPES)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_TYPES) && argc >= 2 && strcmp(argv[1], "test-types") == 0)
    {
        result = run_type_tests(argc - 2, argv + 2);
    }
#endif
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-types") == 0)
    {
        result = run_type_benchmark(argc - 1, argv + 1);
    }
#if BUSTER_IN_UNIT(BUSTER_UNIT_TYPES)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_TYPES) && argc >= 2 && strcmp(argv[1], "test-calling-convention") == 0)
    {
        result = run_calling_convention_tests(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_TYPES)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_TYPES) && argc >= 2 && strcmp(argv[1], "test-builtins") == 0)
    {
        result = run_builtin_tests(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECK_BUILTINS)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_CHECK_BUILTINS) && argc >= 2 && strcmp(argv[1], "test-check-builtins-atomic-rmw") == 0)
    {
        result = run_check_builtin_atomic_rmw_tests(argc - 2, argv + 2);
    }
#endif
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-builtins") == 0)
    {
        result = run_builtin_benchmark(argc - 1, argv + 1);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-calling-convention") == 0)
    {
        result = run_calling_convention_benchmark(argc - 1, argv + 1);
    }
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-differential") == 0)
    {
        result = run_differential_command(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-differential-harness") == 0)
    {
        result = run_differential_tests(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-tier1-frame") == 0)
    {
        result = run_tier1_frame_tests(argc - 1, argv + 1);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-tier1-frame-execution") == 0)
    {
        result = run_tier1_frame_execution_tests(argc - 2, argv + 2);
    }
#endif
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "benchmark-tier1-frame") == 0)
    {
        result = run_tier1_frame_benchmark(argc - 1, argv + 1);
    }
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-tier0") == 0)
    {
        result = run_tier0_tests(argc - 2, argv + 2);
    }
#endif
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-builtin-lowering") == 0)
    {
        result = run_builtin_lower_tests(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-builtin-varargs") == 0)
    {
        result = run_builtin_varargs_lower_tests(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-tier0-variadic-frame") == 0)
    {
        result = test_tier0_variadic_frame(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-builtin-misc-lowering") == 0)
    {
        result = run_builtin_misc_lower_tests(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-builtin-float-lowering") == 0)
    {
        result = run_builtin_float_lower_tests(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-builtin-atomic-rmw-helpers") == 0)
    {
        result = run_builtin_atomic_rmw_helper_tests(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "benchmark-builtin-lowering") == 0)
    {
        result = run_builtin_lower_benchmark(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-emitted-code") == 0)
    {
        result = run_emitted_code_tests(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-unwind") == 0)
    {
        result = run_unwind_tests(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "benchmark-unwind") == 0)
    {
        result = run_unwind_benchmark(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-streaming") == 0)
    {
        result = run_streaming_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "benchmark-streaming") == 0)
    {
        result = run_streaming_benchmark(argc - 2, argv + 2);
    }
#endif
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-backend") == 0)
    {
        result = run_backend_benchmark(argc - 1, argv + 1);
    }
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-ir") == 0)
    {
        result = run_ir_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-ir-simd") == 0)
    {
        result = run_ir_simd_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-ir-lowering") == 0)
    {
        result = run_ir_lower_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-ir-parse") == 0)
    {
        result = run_ir_parse_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-ir-reduce") == 0)
    {
        result = run_ir_reduce_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "reduce-ir") == 0)
    {
        result = run_ir_reduce(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
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
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "interpret-ir") == 0)
    {
        result = run_ir_interpreter(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "interpret-c") == 0)
    {
        result = run_typed_source(argc - 2, argv + 2);
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
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-ir-metamorphic") == 0)
    {
        result = run_ir_metamorphic_tests(argc - 2, argv + 2);
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
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-tier1-select") == 0)
    {
        result = run_tier1_select_tests(argc - 2, argv + 2);
    }
#endif
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
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-tier1-masks") == 0)
    {
        result = run_tier1_mask_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "test-tier1-wide") == 0)
    {
        result = run_tier1_wide_tests(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "benchmark-tier1-pipeline") == 0)
    {
        result = run_tier1_pipeline_benchmark(argc - 2, argv + 2);
    }
#endif
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
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "benchmark-ir-optimize-passes") == 0)
    {
        result = run_ir_optimize_passes_benchmark(argc - 1, argv + 1);
    }
    else if (BUSTER_HOST_VECTOR && BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND) && argc >= 2 && strcmp(argv[1], "benchmark-ir-optimize") == 0)
    {
        result = run_ir_optimize_benchmark(argc - 1, argv + 1);
    }
#endif
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "check-call-graph") == 0)
    {
        result = run_check_call_graph(argc - 2, argv + 2);
    }
#if BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "test-call-graph") == 0)
    {
        result = run_call_graph_tests(argc - 2, argv + 2);
    }
#endif
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 &&
             (strcmp(argv[1], "ab") == 0 || strcmp(argv[1], "hot-loops") == 0 || strcmp(argv[1], "label-spills") == 0 ||
              strcmp(argv[1], "spill-report") == 0 || strcmp(argv[1], "branch-misses") == 0 || strcmp(argv[1], "time-runs") == 0))
    {
        result = run_profile_command(argc - 1, argv + 1);
    }
#if BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "measure-compile") == 0)
    {
        result = run_measure_compile_command(argc - 1, argv + 1);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "measure-tier1-programs") == 0)
    {
        result = run_tier1_programs_measure(argc - 1, argv + 1);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "test-sqlite") == 0)
    {
        result = run_sqlite_tests(argc - 1, argv + 1);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "test-sqlite-testfixture") == 0)
    {
        result = run_sqlite_testfixture(argc - 1, argv + 1);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "measure-tier1-rows") == 0)
    {
        result = run_tier1_rows_measure(argc - 1, argv + 1);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "measure-frames") == 0)
    {
        result = run_frame_measure(argc - 1, argv + 1);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "measure-helpers") == 0)
    {
        result = run_helpers_measure(argc - 1, argv + 1);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "test-profile") == 0)
    {
        result = run_profile_tests(argc - 2, argv + 2);
    }
#endif
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "parallel") == 0)
    {
        result = run_parallel_command(argc - 1, argv + 1);
    }
#if BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "test-parallel") == 0)
    {
        result = run_parallel_tests(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "test-tool") == 0)
    {
        result = test_tool_command(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "test-oracle-cache") == 0)
    {
        result = run_oracle_cache_tests(argc - 2, argv + 2);
    }
#endif
#if BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER)
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "test-c-testsuite") == 0)
    {
        result = run_c_testsuite_tests(argc - 2, argv + 2);
    }
#endif
    else
    {
        *found = false;
    }
    return result;
}

#if BUSTER_UNIT == BUSTER_UNIT_DRIVER
#include "buster/compiler/main_response.c"

// The commands of source files (-E, -fsyntax-only, -c and a link, main_information's): what no unit's main_command
// takes. It runs after the driver's main_command has returned rather than inside it (#652): that function's frame is 140
// KB, the inlined locals of the tests' and benchmarks' commands, and a compile run below it touched pages of it that its
// build's inlining decided, 5 more of them in an empty file's -E after an unrelated change. `advised`: main_driver has
// advised .bss already (arena_static_base_pages).
static __attribute__((noinline)) int main_source_command(int argc, char** argv, bool advised)
{
    int result = 1;
    if (!advised)
    {
        arena_static_base_pages();
    }
    arena_limits_fix();
    // Semantic checking and object emission share the same typed source stream in the checker's unit.
    if (main_response_expand(&argc, &argv) && !main_information(argc - 1, argv + 1, &result))
    {
        result = main_preprocessing(argc - 1, argv + 1) ? main_preprocessor_command(argc - 1, argv + 1) : run_source_check_command(argc - 1, argv + 1);
    }
    // The process exits next: one that started no lane leaves its address space's teardown to a helper (#1444).
    lane_exit_helper_start();
    return result;
}

// main_source_command's routing, by the commands' reusable modes, for a caller that goes on running (#2071): the
// process's page policy and address limit stay the caller's, and a response file (@FILE), whose expansion keeps its
// storage until the process exits, is left to a process of its own: -1 for one, else the command's exit status.
int main_source_command_reusable(int argc, char** argv)
{
    int result = -1;
    bool response = false;
    for (int i = 1; !response && i < argc; i += 1)
    {
        response = argv[i][0] == '@' && argv[i][1] != 0;
    }
    if (!response && !main_information(argc - 1, argv + 1, &result))
    {
        result = main_preprocessing(argc - 1, argv + 1) ? main_preprocessor_command_reusable(argc - 1, argv + 1) : run_source_check_command_reusable(argc - 1, argv + 1);
    }
    return result;
}

#if BUSTER_FUZZ_AVAILABLE
#include BUSTER_FUZZ_ENTRY
extern int LLVMFuzzerRunDriver(int* argc, char*** argv, int (*callback)(const uint8_t*, size_t));
// Weak: present whenever LeakSanitizer is linked (the fuzz builds' combined `address` mode), null otherwise.
extern int __lsan_do_recoverable_leak_check(void) __attribute__((weak));
#endif
static int main_driver(int argc, char** argv)
{
    // A source command's .bss in base pages (arena_static_base_pages) before the process touches any of it (#1444): with
    // transparent huge pages `always`, a read of a 2 MB stretch of .bss maps the huge zero page, and a write to it, even
    // after the advice, then takes a huge page of its own. arena_page_policy's statics shared such a stretch with 220 MB
    // of others, and took one of an empty file's two huge pages. A test or benchmark keeps its .bss as the kernel gives it.
    const bool source = argc >= 2 && argv[1][0] == '-';
    if (source)
    {
        arena_static_base_pages();
    }
    // Base pages on a virtual machine, or as BUSTER_HUGE_PAGES says (#1109), before any arena is reserved.
    arena_page_policy(getenv("BUSTER_HUGE_PAGES"));
#if BUSTER_SANITIZER_RESOURCES
    const char* no_threads = getenv("BUSTER_TEST_NO_LANE_THREADS");
    lane_test_resources.no_threads = no_threads != 0 && strcmp(no_threads, "1") == 0;
#endif
    // A test's hook, in every build (#905): the session's lane of this index fails to start (LaneProcess.fail_at),
    // written only when it is set.
    const char* fail_at = getenv("BUSTER_TEST_LANE_FAIL_AT");
    if (fail_at != 0)
    {
        lane_process.fail_at = (uint32_t)strtoul(fail_at, 0, 10);
    }
    const int guardian = worker_budget_guardian_command(argc, argv);
    // A pipe-style jobserver requires '+' recipes, which make executes even under -n. Honor its dry-run flag before
    // dispatch, including -E and parse commands that need no worker lease themselves.
    bool found = guardian >= 0 || worker_budget_dry_run(getenv("MAKEFLAGS"));
    int result = guardian >= 0 ? guardian : 0;
    // An option first (-E, -fsyntax-only, -c, ...) is a source command's (#652), but for the assembler unit's -c of a .s
    // or .S file (main_assembly_source): no unit's command name starts with '-' (the two that do, --fuzz and the
    // guardian's, are main's and the guardian's above), so it skips the units' chains of strcmp against every command's
    // name, which were most of an empty file's 930 strcmp calls and touched pages of the string pool and .text that a
    // compile needs nothing else of.
    if (!found && source)
    {
        result = main_source_command(argc, argv, true);
        found = true;
    }
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
    if (!found)
    {
        result = main_source_command(argc, argv, false);
    }
    return result;
}

#if defined(BUSTER_HOST_STATIC) && BUSTER_HOST_STATIC
// A static executable's start without registering its .eh_frame with libgcc's unwinder (#652). Where the unwinder is
// linked, which glibc's pthread_exit and pthread_cancel bring, crtbeginT.o's frame_dummy registers it, and libgcc 13
// and later then sorts its every FDE: 1.16 million of the 1.41 million instructions of an empty file's -fsyntax-only, and
// its 634 KB read at every start. The link wraps both functions (cmake/BusterSettings.cmake) into these, which do
// nothing: libgcc finds the frames through glibc's _dl_find_object without the registration, as a dynamic executable's
// crtbegin.o leaves it to, and test-lanes checks that backtrace walks them.
void __wrap___register_frame_info(const void* begin, void* object);
void* __wrap___deregister_frame_info(const void* begin);

void __wrap___register_frame_info(const void* begin, void* object)
{
    (void)begin;
    (void)object;
}

void* __wrap___deregister_frame_info(const void* begin)
{
    (void)begin;
    return 0;
}
#endif

int main(int argc, char** argv)
{
    int result;
#if BUSTER_FUZZ_AVAILABLE
    // The no-main engine takes over before normal command dispatch. Keep its
    // signal handlers, argument parsing, crash artifacts and replay protocol.
    if (argc >= 2 && strcmp(argv[1], "--fuzz") == 0)
    {
        // LeakSanitizer probes ptrace once per process, before its first leak scan, by forking a child and waiting
        // for it; LLVM 23.1.4's wait neither retries EINTR nor checks its result. libFuzzer's -timeout arms a
        // repeating SIGALRM without SA_RESTART, so an alarm during that wait reads an uninitialized status as a
        // child killed by a signal and reports "ptrace appears to be blocked" (#2060). Scanning once here, before
        // the engine arms its timer, runs that probe with no timer to interrupt it. A leak it finds is still
        // reported and still fails the hosted audit.
        if (__lsan_do_recoverable_leak_check)
        {
            __lsan_do_recoverable_leak_check();
        }
        argv[1] = argv[0];
        argc -= 1;
        argv += 1;
        result = LLVMFuzzerRunDriver(&argc, &argv, buster_fuzz_test_input);
    }
    else
#endif
    {
        result = main_driver(argc, argv);
    }
    return result;
}
#elif BUSTER_UNIT == BUSTER_UNIT_X86
int main_unit_x86(int argc, char** argv, bool* found)
{
    return main_command(argc, argv, found);
}
#elif BUSTER_UNIT == BUSTER_UNIT_FRONTEND
int main_preprocessor_command(int count, char** arguments)
{
    return run_preprocessor_command_mode(count, arguments, false);
}

int main_preprocessor_command_reusable(int count, char** arguments)
{
    return run_preprocessor_command_mode(count, arguments, true);
}

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

int run_source_check_command_reusable(int count, char** arguments)
{
    return run_check_command_mode(count, arguments, true, true);
}

int main_unit_checker(int argc, char** argv, bool* found)
{
    return main_command(argc, argv, found);
}
#elif BUSTER_UNIT == BUSTER_UNIT_CHECK_BUILTINS
int main_unit_check_builtins(int argc, char** argv, bool* found)
{
    const char* command = argc >= 2 ? argv[1] : 0;
    OracleCache* cache = oracle_test_fresh_builtin(command) ? oracle_cache() : 0;
    const OracleMode mode = oracle_test_fresh_begin(cache);
    const int result = main_command(argc, argv, found);
    oracle_test_fresh_end(cache, mode);
    return result;
}
#elif BUSTER_UNIT == BUSTER_UNIT_BACKEND
int main_unit_backend(int argc, char** argv, bool* found)
{
    return main_command(argc, argv, found);
}
#endif
