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
int run_x86_encoder_benchmark(int argc, char** argv);

// The lane base layer (groups of threads, and the arenas they allocate from) and its tests, for the test-lanes command.
// Its benchmark is another translation unit.
#include "buster/compiler/lane.c"
#include "buster/compiler/lane_test.c"
int run_lane_benchmark(int argc, char** argv);

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

// The link representation (atoms, symbols, fixups and names, which the object writer and the linker share), the test
// programs generated in it, and their tests, for the test-link command.
#include "buster/compiler/link.c"
#include "buster/compiler/link_program.c"
#include "buster/compiler/link_test.c"

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

// The call graph check (the style's rule against recursion, on the compiler's own LLVM IR) and its tests, for the
// check-call-graph and test-call-graph commands.
#include "buster/compiler/call_graph.c"
#include "buster/compiler/call_graph_test.c"

int main(int argc, char** argv)
{
    int result = 0;
    if (argc >= 2 && strcmp(argv[1], "test-lexer") == 0)
    {
        result = run_c_lexer_tests(argv[0], argc - 2, argv + 2);
        if (result == 0)
        {
            result = run_main_tests();
        }
    }
    else if (argc >= 2 && strcmp(argv[1], "benchmark-lexer") == 0)
    {
        result = run_c_lexer_benchmark(argc - 1, argv + 1);
    }
    else if (argc >= 2 && strcmp(argv[1], "test-x86-encoder") == 0)
    {
        result = run_x86_encoder_tests(argc - 2, argv + 2);
    }
    else if (argc >= 2 && strcmp(argv[1], "benchmark-x86-encoder") == 0)
    {
        result = run_x86_encoder_benchmark(argc - 1, argv + 1);
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
    else if (argc >= 2 && strcmp(argv[1], "check-call-graph") == 0)
    {
        result = run_check_call_graph(argc - 2, argv + 2);
    }
    else if (argc >= 2 && strcmp(argv[1], "test-call-graph") == 0)
    {
        result = run_call_graph_tests(argc - 2, argv + 2);
    }
    else
    {
        printf("Hello world\n");
        const CompileResult compiled = compile(BUSTER_S8(__FILE__), (CompileOptions){0});
        result = compiled.success ? 0 : 1;
    }
    return result;
}
