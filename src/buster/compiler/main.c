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
#define BUSTER_UNIT_DRIVER 0
#define BUSTER_UNIT_X86 1
#define BUSTER_UNIT_FRONTEND 2
#define BUSTER_UNIT_ASSEMBLER 3
#define BUSTER_UNIT_LINK 4
#define BUSTER_UNIT_EXECUTABLE 5
#define BUSTER_UNIT_DYNAMIC 6
#define BUSTER_UNIT_TYPES 7
#ifndef BUSTER_UNIT
#define BUSTER_UNIT BUSTER_UNIT_DRIVER
#endif
#define BUSTER_IN_UNIT(unit) (BUSTER_UNIT == (unit))

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

static void string_printf(String8 string)
{
    assert(string.length <= INT_MAX);
    printf("%.*s", (int)string.length, string.pointer);
}

typedef struct CompileOptions CompileOptions;
struct CompileOptions
{
    uint32_t foo;
};

typedef struct CompileResult CompileResult;
struct CompileResult
{
    bool success;
};

static uint64_t compute_maximum_token_count(String8 chunk)
{
    return BUSTER_C_TOKENIZER_CAPACITY(chunk.length);
}

typedef struct PrimitiveTokenizeResult PrimitiveTokenizeResult;
struct PrimitiveTokenizeResult
{
    String8 content;
    uint8_t* kinds;
    uint32_t* starts;
    uint32_t* ends;
    uint64_t token_count;
    uint64_t token_capacity;
    const char* operation;
    int error_number;
    bool error;
    bool owns_content;
};

static void primitive_tokenize_error(PrimitiveTokenizeResult* result, const char* operation, int error_number)
{
    if (!result->error)
    {
        result->error = true;
        result->operation = operation;
        result->error_number = error_number;
    }
}

// Chunk results borrow source bytes; file results own them. Both own their token arrays, and no result owns an fd.
static void primitive_tokenize_release(PrimitiveTokenizeResult* result)
{
    if (result->kinds != 0)
    {
        munmap(result->kinds, result->token_capacity * sizeof(*result->kinds));
    }
    if (result->starts != 0)
    {
        munmap(result->starts, result->token_capacity * sizeof(*result->starts));
    }
    if (result->ends != 0)
    {
        munmap(result->ends, result->token_capacity * sizeof(*result->ends));
    }
    if (result->owns_content && result->content.length != 0)
    {
        munmap(result->content.pointer, result->content.length);
    }
    *result = (PrimitiveTokenizeResult)
    {
        .operation = result->operation,
        .error_number = result->error_number,
        .error = result->error,
    };
}

static PrimitiveTokenizeResult primitive_tokenize_chunk(String8 chunk)
{
    PrimitiveTokenizeResult result = { 0 };
    result.content = chunk;
    if (chunk.length > UINT32_MAX - BUSTER_C_TOKENIZER_OUTPUT_SLACK)
    {
        primitive_tokenize_error(&result, "tokenize", EFBIG);
    }
    else if (chunk.length != 0)
    {
        result.token_capacity = compute_maximum_token_count(chunk);
        const uint64_t sizes[] =
        {
            result.token_capacity * sizeof(*result.kinds),
            result.token_capacity * sizeof(*result.starts),
            result.token_capacity * sizeof(*result.ends),
        };
        const char* operations[] =
        {
            "mmap token kinds",
            "mmap token starts",
            "mmap token ends",
        };
        void* mappings[3] = { 0 };
        for (uint32_t index = 0; index < 3 && !result.error; index += 1)
        {
            void* mapping = mmap(0, sizes[index], PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (mapping == MAP_FAILED)
            {
                primitive_tokenize_error(&result, operations[index], errno);
            }
            else
            {
                mappings[index] = mapping;
            }
        }
        result.kinds = mappings[0];
        result.starts = mappings[1];
        result.ends = mappings[2];
        if (!result.error)
        {
            const int64_t count = c_tokenize((const uint8_t*)chunk.pointer, chunk.length, result.kinds, result.starts, result.ends, 0);
            if (count < 0)
            {
                primitive_tokenize_error(&result, "tokenize", EFBIG);
            }
            else
            {
                result.token_count = (uint64_t)count;
            }
        }
    }
    if (result.error)
    {
        primitive_tokenize_release(&result);
    }
    return result;
}

static PrimitiveTokenizeResult primitive_tokenize_file(String8 path)
{
    PrimitiveTokenizeResult result = { 0 };
    assert(strlen(path.pointer) == path.length && path.pointer[path.length] == 0);
    const int fd = open(path.pointer, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
    {
        primitive_tokenize_error(&result, "open", errno);
    }
    else
    {
        struct stat file_stats;
        if (fstat(fd, &file_stats) != 0)
        {
            primitive_tokenize_error(&result, "fstat", errno);
        }
        else if (file_stats.st_size < 0 || (uint64_t)file_stats.st_size > UINT32_MAX - BUSTER_C_TOKENIZER_OUTPUT_SLACK)
        {
            primitive_tokenize_error(&result, "file size", EFBIG);
        }
        else if (file_stats.st_size != 0)
        {
            const uint64_t length = (uint64_t)file_stats.st_size;
            void* mapped = mmap(0, length, PROT_READ, MAP_PRIVATE, fd, 0);
            if (mapped == MAP_FAILED)
            {
                primitive_tokenize_error(&result, "mmap source", errno);
            }
            else
            {
                const String8 chunk =
                {
                    .pointer = mapped,
                    .length = length,
                };
                result = primitive_tokenize_chunk(chunk);
                result.content = chunk;
                result.owns_content = true;
            }
        }
        if (close(fd) != 0)
        {
            primitive_tokenize_error(&result, "close", errno);
        }
    }
    if (result.error)
    {
        primitive_tokenize_release(&result);
    }
    return result;
}

static bool parse_compile_unit(String8 path)
{
    PrimitiveTokenizeResult tokens = primitive_tokenize_file(path);
    const bool result = !tokens.error;
    if (tokens.error)
    {
        fprintf(stderr, "%.*s: %s: %s\n", (int)path.length, path.pointer, tokens.operation, strerror(tokens.error_number));
    }
    primitive_tokenize_release(&tokens);
    return result;
}

static CompileResult compile(String8 path, CompileOptions options)
{
    CompileResult result = {0};
    printf("Compiling ");
    string_printf(path);
    printf("...\n");
    bool parse_result = parse_compile_unit(path);
    result.success = parse_result;
    return result;
}

// The lexer benchmark is a separate translation unit, linked into the compiler executable.
int run_c_lexer_benchmark(int argc, char** argv);

// The lexer tests, for the test-lexer command.
#include "buster/compiler/c_lexer_test.c"
#include "buster/compiler/main_test.c"

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
#include "buster/compiler/preprocessor_fuzz_benchmark.c"

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
#include "buster/compiler/c_preprocessor_test.c"
#include "buster/compiler/c_predefines_test.c"
#include "buster/compiler/c_preprocessor_oracle.c"
#include "buster/compiler/x86_target_forms.c"
#include "buster/compiler/x86_target_test.c"
int run_preprocessor_benchmark(int argc, char** argv);

// The per-file jobs on lanes and the run-ahead that fill the preprocessor's file cache (c_files.c), and their tests, for
// the test-files command. Its benchmark is another translation unit.
#include "buster/compiler/c_files.c"
#include "buster/compiler/c_files_test.c"
int run_files_benchmark(int argc, char** argv);

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

// What the x86 forms a code generator emits mean (their effects, the immediate's contract, the operand-resolved effects
// and the value-preserving form choice) and its tests, for the test-x86-semantics command (which takes the encoder's
// tests' printer and branch sizing's tests' runner, and checks x86_match.c's ranges). Its benchmark is another
// translation unit.
#include "buster/compiler/x86_semantics.c"
#include "buster/compiler/x86_semantics_test.c"
int run_x86_semantics_benchmark(int argc, char** argv);

// The C type table (TypeIds, hash-consing, layout, conversions, compatibility and printing) and its tests, for the
// test-types command. Its benchmark is another translation unit.
#include "buster/compiler/c_types.c"
#include "buster/compiler/c_types_test.c"
int run_type_benchmark(int argc, char** argv);

#include "buster/compiler/c_builtin.c"
#include "buster/compiler/c_builtin_test.c"
int run_builtin_benchmark(int argc, char** argv);

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

// The runner the 9700X job runs its tests through (a list of commands at once, each one's output printed in the list's
// order) and its tests, for the parallel and test-parallel commands.
#include "buster/compiler/parallel.c"
#include "buster/compiler/parallel_test.c"

// The other units' main_command, each in its own unit (at the top).
int main_unit_x86(int argc, char** argv, bool* found);
int main_unit_frontend(int argc, char** argv, bool* found);
int main_unit_assembler(int argc, char** argv, bool* found);
int main_unit_link(int argc, char** argv, bool* found);
int main_unit_executable(int argc, char** argv, bool* found);
int main_unit_dynamic(int argc, char** argv, bool* found);
int main_unit_types(int argc, char** argv, bool* found);

// Runs the command argv[1] names if it is one of this unit's (*found). Each branch first tests the unit whose code it
// is, so that the other units' branches fold to false (at the top). The driver's last branch, which no command takes,
// runs the compiler itself.
static int main_command(int argc, char** argv, bool* found)
{
    int result = 0;
    *found = true;
    if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc == 2 && strcmp(argv[1], "test-diagnostics") == 0)
    {
        result = run_diagnostic_tests();
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-diagnostics") == 0)
    {
        result = run_diagnostic_benchmark(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_ASSEMBLER) && argc >= 2 && (strcmp(argv[1], "assemble") == 0 || strcmp(argv[1], "-c") == 0))
    {
        result = run_assembler_command(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "test-x86-semantics") == 0)
    {
        result = run_x86_semantics_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-x86-semantics") == 0)
    {
        result = run_x86_semantics_benchmark(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_ASSEMBLER) && argc >= 2 && strcmp(argv[1], "test-assembler") == 0)
    {
        result = run_assembler_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_ASSEMBLER) && argc >= 2 && strcmp(argv[1], "benchmark-assembler") == 0)
    {
        result = run_assembler_benchmark(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND) && argc >= 2 && strcmp(argv[1], "-E") == 0)
    {
        result = run_preprocessor_command(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND) && argc >= 2 && strcmp(argv[1], "test-preprocessor") == 0)
    {
        result = run_preprocessor_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-preprocessor") == 0)
    {
        result = run_preprocessor_benchmark(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "test-lexer") == 0)
    {
        result = run_c_lexer_tests(argv[0], argc - 2, argv + 2);
        if (result == 0)
        {
            result = run_main_tests();
        }
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-lexer") == 0)
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
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_X86) && argc >= 2 && strcmp(argv[1], "test-x86-zen5") == 0)
    {
        result = run_x86_zen5_tests();
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_X86) && argc >= 2 && strcmp(argv[1], "benchmark-x86-zen5") == 0)
    {
        result = run_x86_zen5_benchmark(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_X86) && argc >= 2 && strcmp(argv[1], "measure-x86-zen5") == 0)
    {
        result = run_x86_zen5_measure(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND) && argc >= 2 && strcmp(argv[1], "benchmark-x86-target") == 0)
    {
        result = run_x86_target_benchmark(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-x86-encoder") == 0)
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
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-x86-disassembler") == 0)
    {
        result = run_x86_disassembler_benchmark(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "test-lanes") == 0)
    {
        result = run_lane_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-lanes") == 0)
    {
        result = run_lane_benchmark(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "test-dataflow") == 0)
    {
        result = run_dataflow_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-dataflow") == 0)
    {
        result = run_dataflow_benchmark(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND) && argc >= 2 && strcmp(argv[1], "test-preprocessor-fuzz") == 0)
    {
        result = run_preprocessor_fuzz_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND) && argc >= 2 && strcmp(argv[1], "benchmark-preprocessor-fuzz") == 0)
    {
        result = run_preprocessor_fuzz_benchmark(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND) && argc >= 2 && strcmp(argv[1], "test-refinement") == 0)
    {
        result = run_refinement_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-refinement") == 0)
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
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-object-writer") == 0)
    {
        result = run_object_writer_benchmark(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_LINK) && argc >= 2 && strcmp(argv[1], "test-import") == 0)
    {
        result = run_import_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-import") == 0)
    {
        result = run_import_benchmark(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_LINK) && argc >= 2 && strcmp(argv[1], "test-bind") == 0)
    {
        result = run_bind_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-bind") == 0)
    {
        result = run_bind_benchmark(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_EXECUTABLE) && argc >= 2 && strcmp(argv[1], "test-executable") == 0)
    {
        result = run_executable_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-executable") == 0)
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
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-snapshot") == 0)
    {
        result = run_snapshot_benchmark(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-link") == 0)
    {
        result = run_link_benchmark(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-floors") == 0)
    {
        result = run_floors_benchmark(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_X86) && argc >= 2 && strcmp(argv[1], "test-branch-sizing") == 0)
    {
        result = run_branch_sizing_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-branch-sizing") == 0)
    {
        result = run_branch_sizing_benchmark(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_FRONTEND) && argc >= 2 && strcmp(argv[1], "test-files") == 0)
    {
        result = run_files_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-files") == 0)
    {
        result = run_files_benchmark(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_TYPES) && argc >= 2 && strcmp(argv[1], "test-types") == 0)
    {
        result = run_type_tests(argc - 2, argv + 2);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-types") == 0)
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
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-builtins") == 0)
    {
        result = run_builtin_benchmark(argc - 1, argv + 1);
    }
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER) && argc >= 2 && strcmp(argv[1], "benchmark-calling-convention") == 0)
    {
        result = run_calling_convention_benchmark(argc - 1, argv + 1);
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
              strcmp(argv[1], "spill-report") == 0 || strcmp(argv[1], "branch-misses") == 0))
    {
        result = run_profile_command(argc - 1, argv + 1);
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
    else if (BUSTER_IN_UNIT(BUSTER_UNIT_DRIVER))
    {
        printf("Hello world\n");
        const CompileResult compiled = compile(BUSTER_S8(__FILE__), (CompileOptions){0});
        result = compiled.success ? 0 : 1;
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
    bool found = false;
    int result = main_unit_x86(argc, argv, &found);
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
#endif
