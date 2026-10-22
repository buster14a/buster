// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
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
#include <immintrin.h>

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

// Independent preprocessor fuzz tooling (#109); command names await agreement with #117.
#include "buster/compiler/preprocessor_fuzz.c"
#include "buster/compiler/preprocessor_fuzz_generate.c"
#include "buster/compiler/preprocessor_fuzz_clang.c"
#include "buster/compiler/preprocessor_fuzz_test.c"
int run_preprocessor_fuzz_benchmark(int argc, char** argv);

// Token refinement: identifier interning (c_intern.c) and the per-file pass after the lexer (c_refine.c), and their
// tests, for the test-refinement command. Its benchmark is another translation unit.
#include "buster/compiler/c_intern.c"
#include "buster/compiler/c_refine.c"
#include "buster/compiler/c_refine_test.c"
int run_refinement_benchmark(int argc, char** argv);

// The preprocessor (c_preprocessor.c) and its tests, for the -E and test-preprocessor commands.
#include "buster/compiler/c_preprocessor.c"
#include "buster/compiler/c_preprocessor_test.c"
#include "buster/compiler/c_preprocessor_oracle.c"
#include "buster/compiler/x86_target_forms.c"
#include "buster/compiler/x86_target_test.c"
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
int run_parser_benchmark(int argc, char** argv);

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
#include "buster/compiler/assembler.c"
#include "buster/compiler/assembler_driver.c"
#include "buster/compiler/assembler_test.c"
#include "buster/compiler/assembler_benchmark.c"

// The C type table (TypeIds, hash-consing, layout, conversions, compatibility and printing) and its tests, for the
// test-types command. Its benchmark is another translation unit.
#include "buster/compiler/c_types.c"
#include "buster/compiler/c_types_test.c"
int run_type_benchmark(int argc, char** argv);

// The System V calling convention (each type's classes, and each call's argument locations) and its tests, for the
// test-calling-convention command. Its benchmark is another translation unit.
#include "buster/compiler/calling_convention.c"
#include "buster/compiler/calling_convention_test.c"
int run_calling_convention_benchmark(int argc, char** argv);

// The call graph check (the style's rule against recursion, on the compiler's own LLVM IR) and its tests, for the
// check-call-graph and test-call-graph commands.
#include "buster/compiler/call_graph.c"
#include "buster/compiler/call_graph_test.c"

// The profiling tools (ab, and the reports of perf's and objdump's text: hot-loops, label-spills, spill-report and
// branch-misses) and their tests, for the test-profile command.
#include "buster/compiler/profile.c"
#include "buster/compiler/profile_test.c"

int main(int argc, char** argv)
{
    int result = 0;
    if (argc == 2 && strcmp(argv[1], "test-diagnostics") == 0)
    {
        result = run_diagnostic_tests();
    }
    else if (argc >= 2 && strcmp(argv[1], "benchmark-diagnostics") == 0)
    {
        result = run_diagnostic_benchmark(argc - 2, argv + 2);
    }
    else if (argc >= 2 && (strcmp(argv[1], "assemble") == 0 || strcmp(argv[1], "-c") == 0))
    {
        result = run_assembler_command(argc - 2, argv + 2);
    }
    else if (argc >= 2 && strcmp(argv[1], "test-assembler") == 0)
    {
        result = run_assembler_tests(argc - 2, argv + 2);
    }
    else if (argc >= 2 && strcmp(argv[1], "benchmark-assembler") == 0)
    {
        result = run_assembler_benchmark(argc - 2, argv + 2);
    }
    else if (argc >= 2 && strcmp(argv[1], "-E") == 0)
    {
        result = run_preprocessor_command(argc - 1, argv + 1);
    }
    else if (argc >= 2 && strcmp(argv[1], "test-preprocessor") == 0)
    {
        result = run_preprocessor_tests(argc - 2, argv + 2);
    }
    else if (argc >= 2 && strcmp(argv[1], "benchmark-preprocessor") == 0)
    {
        result = run_preprocessor_benchmark(argc - 1, argv + 1);
    }
    else if (argc >= 2 && strcmp(argv[1], "test-parser") == 0)
    {
        result = run_parser_tests(argc - 2, argv + 2);
    }
    else if (argc >= 2 && strcmp(argv[1], "benchmark-parser") == 0)
    {
        result = run_parser_benchmark(argc - 1, argv + 1);
    }
    else if (argc >= 2 && strcmp(argv[1], "test-lexer") == 0)
    {
        result = run_c_lexer_tests(argv[0], argc - 2, argv + 2);
    }
    else if (argc >= 2 && strcmp(argv[1], "benchmark-lexer") == 0)
    {
        result = run_c_lexer_benchmark(argc - 1, argv + 1);
    }
    else if (argc >= 2 && strcmp(argv[1], "test-x86-encoder") == 0)
    {
        result = run_x86_encoder_tests(argc - 2, argv + 2);
    }
    else if (argc >= 2 && strcmp(argv[1], "test-x86-target") == 0)
    {
        result = run_x86_target_tests();
    }
    else if (argc >= 2 && strcmp(argv[1], "test-x86-zen5") == 0)
    {
        result = run_x86_zen5_tests();
    }
    else if (argc >= 2 && strcmp(argv[1], "benchmark-x86-zen5") == 0)
    {
        result = run_x86_zen5_benchmark(argc - 2, argv + 2);
    }
    else if (argc >= 2 && strcmp(argv[1], "measure-x86-zen5") == 0)
    {
        result = run_x86_zen5_measure(argc - 2, argv + 2);
    }
    else if (argc >= 2 && strcmp(argv[1], "benchmark-x86-target") == 0)
    {
        result = run_x86_target_benchmark(argc - 2, argv + 2);
    }
    else if (argc >= 2 && strcmp(argv[1], "benchmark-x86-encoder") == 0)
    {
        result = run_x86_encoder_benchmark(argc - 1, argv + 1);
    }
    else if (argc >= 2 && strcmp(argv[1], "test-x86-disassembler") == 0)
    {
        result = run_x86_disassembler_tests(argc - 2, argv + 2);
    }
    else if (argc >= 2 && strcmp(argv[1], "disassemble") == 0)
    {
        result = run_x86_disassembler(argc - 2, argv + 2);
    }
    else if (argc >= 2 && strcmp(argv[1], "benchmark-x86-disassembler") == 0)
    {
        result = run_x86_disassembler_benchmark(argc - 1, argv + 1);
    }
    else if (argc >= 2 && strcmp(argv[1], "test-lanes") == 0)
    {
        result = run_lane_tests(argc - 2, argv + 2);
    }
    else if (argc >= 2 && strcmp(argv[1], "benchmark-lanes") == 0)
    {
        result = run_lane_benchmark(argc - 1, argv + 1);
    }
    else if (argc >= 2 && strcmp(argv[1], "test-dataflow") == 0)
    {
        result = run_dataflow_tests(argc - 2, argv + 2);
    }
    else if (argc >= 2 && strcmp(argv[1], "benchmark-dataflow") == 0)
    {
        result = run_dataflow_benchmark(argc - 1, argv + 1);
    }
    else if (argc >= 2 && strcmp(argv[1], "test-preprocessor-fuzz") == 0)
    {
        result = run_preprocessor_fuzz_tests(argc - 2, argv + 2);
    }
    else if (argc >= 2 && strcmp(argv[1], "benchmark-preprocessor-fuzz") == 0)
    {
        result = run_preprocessor_fuzz_benchmark(argc - 1, argv + 1);
    }
    else if (argc >= 2 && strcmp(argv[1], "test-refinement") == 0)
    {
        result = run_refinement_tests(argc - 2, argv + 2);
    }
    else if (argc >= 2 && strcmp(argv[1], "benchmark-refinement") == 0)
    {
        result = run_refinement_benchmark(argc - 1, argv + 1);
    }
    else if (argc >= 2 && strcmp(argv[1], "test-link") == 0)
    {
        result = run_link_tests(argc - 2, argv + 2);
    }
    else if (argc >= 2 && strcmp(argv[1], "test-object-writer") == 0)
    {
        result = run_object_writer_tests(argc - 2, argv + 2);
    }
    else if (argc >= 2 && strcmp(argv[1], "benchmark-object-writer") == 0)
    {
        result = run_object_writer_benchmark(argc - 1, argv + 1);
    }
    else if (argc >= 2 && strcmp(argv[1], "test-import") == 0)
    {
        result = run_import_tests(argc - 2, argv + 2);
    }
    else if (argc >= 2 && strcmp(argv[1], "benchmark-import") == 0)
    {
        result = run_import_benchmark(argc - 1, argv + 1);
    }
    else if (argc >= 2 && strcmp(argv[1], "test-bind") == 0)
    {
        result = run_bind_tests(argc - 2, argv + 2);
    }
    else if (argc >= 2 && strcmp(argv[1], "benchmark-bind") == 0)
    {
        result = run_bind_benchmark(argc - 1, argv + 1);
    }
    else if (argc >= 2 && strcmp(argv[1], "test-executable") == 0)
    {
        result = run_executable_tests(argc - 2, argv + 2);
    }
    else if (argc >= 2 && strcmp(argv[1], "benchmark-executable") == 0)
    {
        result = run_executable_benchmark(argc - 1, argv + 1);
    }
    else if (argc >= 2 && strcmp(argv[1], "test-dynamic") == 0)
    {
        result = run_dynamic_tests(argc - 2, argv + 2);
    }
    else if (argc >= 2 && strcmp(argv[1], "test-static") == 0)
    {
        result = run_static_tests(argc - 2, argv + 2);
    }
    else if (argc >= 2 && strcmp(argv[1], "test-snapshot") == 0)
    {
        result = run_snapshot_tests(argc - 2, argv + 2);
    }
    else if (argc >= 2 && strcmp(argv[1], "benchmark-snapshot") == 0)
    {
        result = run_snapshot_benchmark(argc - 1, argv + 1);
    }
    else if (argc >= 2 && strcmp(argv[1], "benchmark-link") == 0)
    {
        result = run_link_benchmark(argc - 1, argv + 1);
    }
    else if (argc >= 2 && strcmp(argv[1], "benchmark-floors") == 0)
    {
        result = run_floors_benchmark(argc - 1, argv + 1);
    }
    else if (argc >= 2 && strcmp(argv[1], "test-branch-sizing") == 0)
    {
        result = run_branch_sizing_tests(argc - 2, argv + 2);
    }
    else if (argc >= 2 && strcmp(argv[1], "benchmark-branch-sizing") == 0)
    {
        result = run_branch_sizing_benchmark(argc - 1, argv + 1);
    }
    else if (argc >= 2 && strcmp(argv[1], "test-files") == 0)
    {
        result = run_files_tests(argc - 2, argv + 2);
    }
    else if (argc >= 2 && strcmp(argv[1], "benchmark-files") == 0)
    {
        result = run_files_benchmark(argc - 1, argv + 1);
    }
    else if (argc >= 2 && strcmp(argv[1], "test-types") == 0)
    {
        result = run_type_tests(argc - 2, argv + 2);
    }
    else if (argc >= 2 && strcmp(argv[1], "benchmark-types") == 0)
    {
        result = run_type_benchmark(argc - 1, argv + 1);
    }
    else if (argc >= 2 && strcmp(argv[1], "test-calling-convention") == 0)
    {
        result = run_calling_convention_tests(argc - 2, argv + 2);
    }
    else if (argc >= 2 && strcmp(argv[1], "benchmark-calling-convention") == 0)
    {
        result = run_calling_convention_benchmark(argc - 1, argv + 1);
    }
    else if (argc >= 2 && strcmp(argv[1], "check-call-graph") == 0)
    {
        result = run_check_call_graph(argc - 2, argv + 2);
    }
    else if (argc >= 2 && strcmp(argv[1], "test-call-graph") == 0)
    {
        result = run_call_graph_tests(argc - 2, argv + 2);
    }
    else if (argc >= 2 && (strcmp(argv[1], "ab") == 0 || strcmp(argv[1], "hot-loops") == 0 || strcmp(argv[1], "label-spills") == 0 ||
                           strcmp(argv[1], "spill-report") == 0 || strcmp(argv[1], "branch-misses") == 0))
    {
        result = run_profile_command(argc - 1, argv + 1);
    }
    else if (argc >= 2 && strcmp(argv[1], "test-profile") == 0)
    {
        result = run_profile_tests(argc - 2, argv + 2);
    }
    else
    {
        // The driver: preprocess and parse a file (and with no file, its usage).
        result = run_parse_command(argc - 1, argv + 1);
    }
    return result;
}
