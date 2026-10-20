#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <assert.h>

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
    int file_descriptor;
    bool error;
};

static PrimitiveTokenizeResult primitive_tokenize_chunk(String8 chunk)
{
    PrimitiveTokenizeResult result = {0};
    uint64_t maximum_token_length = compute_maximum_token_count(chunk);
    (void)maximum_token_length;
    uint8_t* kinds = mmap(0, maximum_token_length * sizeof(*kinds), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint32_t* starts = mmap(0, maximum_token_length * sizeof(*starts), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint32_t* ends = mmap(0, maximum_token_length * sizeof(*ends), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint32_t flags = 0;

    int64_t tokenize_result = -1;
    if (kinds != MAP_FAILED && starts != MAP_FAILED && starts != MAP_FAILED)
    {
        tokenize_result = c_tokenize((const uint8_t*)chunk.pointer, chunk.length, kinds, starts, ends, flags);
    }

    result.content = chunk;
    result.error = tokenize_result < 0;
    result.token_count = (uint64_t)tokenize_result;

    if (!result.error)
    {
        result.kinds = kinds;
        result.starts = starts;
        result.ends = ends;
    }
    else
    {
        munmap(kinds, maximum_token_length * sizeof(*kinds));
        munmap(starts, maximum_token_length * sizeof(*starts));
        munmap(ends, maximum_token_length * sizeof(*ends));
    }

    return result;
}

static PrimitiveTokenizeResult primitive_tokenize_file(String8 path)
{
    PrimitiveTokenizeResult result = {0};

    assert(strlen(path.pointer) == path.length && path.pointer[path.length] == 0);
    int fd = open(path.pointer, O_RDONLY | O_CLOEXEC, 0644);
    if (fd != -1)
    {
        struct stat file_stats;
        if (fstat(fd, &file_stats) == 0)
        {
            uint64_t length = (uint64_t)file_stats.st_size;
            void* mapped = mmap(0, length, PROT_READ, MAP_PRIVATE, fd, 0);
            if (mapped != MAP_FAILED)
            {
                String8 chunk = { .pointer = (char*)mapped, .length = length };
                result = primitive_tokenize_chunk(chunk);
                result.file_descriptor = fd;
            }
        }
    }

    return result;
}

static bool parse_compile_unit(String8 path)
{
    bool result = false;

    String8 current_file_path = path;

    while (true)
    {
        PrimitiveTokenizeResult result = primitive_tokenize_file(current_file_path);
        (void)result;
        break;
    }

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

// The x86-64 encoder and its tests, for the test-x86-encoder command. Its benchmark is another translation unit.
#include "buster/compiler/x86_64_encoder.c"
#include "buster/compiler/x86_64_encoder_test.c"
int run_x86_encoder_benchmark(int argc, char** argv);

// The lane base layer (groups of threads, and the arenas they allocate from) and its tests, for the test-lanes command.
// Its benchmark is another translation unit.
#include "buster/compiler/lane.c"
#include "buster/compiler/lane_test.c"
int run_lane_benchmark(int argc, char** argv);

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

// The direct path's executables (layout, patching, the build ID and the output file) and their tests, for the
// test-executable command. Its benchmark is another translation unit.
#include "buster/compiler/executable.c"
#include "buster/compiler/executable_test.c"
int run_executable_benchmark(int argc, char** argv);

int main(int argc, char** argv)
{
    int result = 0;
    if (argc >= 2 && strcmp(argv[1], "test-lexer") == 0)
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
    else if (argc >= 2 && strcmp(argv[1], "test-executable") == 0)
    {
        result = run_executable_tests(argc - 2, argv + 2);
    }
    else if (argc >= 2 && strcmp(argv[1], "benchmark-executable") == 0)
    {
        result = run_executable_benchmark(argc - 1, argv + 1);
    }
    else
    {
        printf("Hello world\n");
        compile(BUSTER_S8(__FILE__), (CompileOptions){0});
    }
    return result;
}
