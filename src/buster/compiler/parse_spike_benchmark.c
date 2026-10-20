// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The spike-parse command (#186): loads preprocessed C, parses it with the scalar parser and with the kernels of
// parse_spike.c, checks that every tree is the same, and times them.
//
// Usage: build/compiler spike-parse [--dump FILE] [--check-only] path...
//
// Paths are files, or directories whose files ending in .i are read (preprocessed with `clang -E -P`). Each file is
// lexed by c_tokenize and refined by spike_refine, with one identifier table for all of them, outside what is timed.
// The exit status is 1 if a path can't be read, a file doesn't parse, or the trees differ.
//
// Layout, in this order:
//   - Loading: SpikeFile, spike_load_file and spike_load_path (an explicit stack of open directories).
//   - Checks: spike_check_tree (every parent a token before or after it at no greater depth, every opener's parent its
//     closer, no cycle) and spike_print_tree.
//   - run_parse_spike, called by main.c.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// OS-dependent includes
#include <dirent.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <linux/perf_event.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>

#include "buster/compiler/c_lexer.c"
#include "buster/compiler/lane.c"
#include "buster/compiler/parse_spike.c"

// Address space for the files, their tokens and the trees, and for the identifier table.
#define BUSTER_SPIKE_MEMORY_RESERVE (64ull << 30)
#define BUSTER_SPIKE_NAME_CAPACITY (1u << 22)
#define BUSTER_SPIKE_NAME_BYTES (64ull << 20)
#define BUSTER_SPIKE_MAX_FILES (1u << 16)
#define BUSTER_SPIKE_DIRECTORY_DEPTH 64
// Tokens of context printed around an error.
#define BUSTER_SPIKE_CONTEXT_TOKENS 12

// ---------------------------------------------------------------------------------------------------------------------
// Fixtures

// Sources whose trees were checked by hand: each token's expected parent, -1 for the root. They cover declarations
// (function pointers, bit-fields, typedefs of struct bodies, attributes), every statement, the dangling else, casts,
// compound literals, designators, nested ternaries, sizeof, chained postfix operators and string concatenation.
static const char spike_fixture_a[] =
    "typedef int T;\n"
    "int f(int a, T *b) { if (a) x = b * c; else y = -z; return a + b * c - d; }\n";

static const int32_t spike_fixture_a_parents[] = { 3, 0, 1, -1, 40, 13, 13, 9, 7, 13, 9, 10, 11, 4, 40, 18, 18, 18, 40, 20, 24, 22, 20, 22, 18, 18, 27, 30, 27, 28, 25, 39, 33, 37, 35, 33, 35, 31, 37, 40, -1 };

static const char spike_fixture_b[] =
    "typedef struct S { int a : 3, b; struct S *next; } S;\n"
    "int (*fp)(int, char *), *q[2] = { 0 };\n"
    "void g(void) {\n"
    "  S s = { .a = 1, [2] = (S){ 3 } };\n"
    "  x = (int)*p + sizeof(int) * a ? b : c ? d : e;\n"
    "  if (a) if (b) x; else y;\n"
    "  do x++; while (c);\n"
    "  switch (n) { case 1: case 2 ... 4: f(a, b); break; default: L: goto L; }\n"
    "  for (int i = 0, j; i < n; i++, j--) { }\n"
    "  y = f(a)(b)[c]->d.e;\n"
    "  z = \"a\" \"b\" \"c\";\n"
    "}\n"
    "enum E { A = 1, B };\n"
    "extern int h(void) __attribute__((__nothrow__)) __attribute__((__const__));\n";

static const int32_t spike_fixture_b_parents[] = { 18, 16, 16, 16, 10, 6, 8, 6, 4, 8, 16, 12, 15, 12, 13, 16, 0, 16, -1, 40, 23, 23, 21, 29, 29, 26, 29, 26, 27, 30, 19, 36, 35, 35, 35, 31, 30, 39, 39, 36, -1, 187, 45, 45, 45, 41, 187, 67, 49, 47, 66, 53, 51, 55, 53, 66, 58, 58, 59, 55, 62, 62, 65, 65, 65, 59, 49, 187, 69, 90, 72, 72, 75, 72, 73, 84, 80, 79, 79, 76, 75, 80, 84, 84, 69, 88, 88, 88, 84, 88, 187, 94, 94, 94, 187, 98, 98, 98, 94, 100, 98, 98, 103, 101, 112, 106, 107, 104, 111, 111, 111, 112, 187, 116, 116, 116, 187, 142, 120, 120, 142, 125, 123, 125, 123, 120, 131, 131, 129, 131, 129, 132, 125, 134, 142, 136, 142, 138, 136, 141, 139, 138, 116, 161, 161, 151, 147, 149, 147, 145, 149, 161, 153, 155, 153, 161, 157, 158, 161, 160, 158, 187, 163, 161, 165, 180, 169, 169, 169, 172, 172, 172, 175, 175, 175, 176, 178, 176, 165, 178, 187, 182, 186, 182, 183, 184, 187, -1, 196, 196, 196, 192, 194, 192, 196, 194, 197, -1, 216, 198, 203, 203, 203, 209, 209, 209, 208, 208, 209, 215, 215, 215, 214, 214, 215, 199, -1 };

typedef struct SpikeFixture SpikeFixture;
struct SpikeFixture
{
    const char* name;
    const char* source;
    const int32_t* parents;
    uint32_t count;
};

static const SpikeFixture spike_fixtures[] =
{
    { "a", spike_fixture_a, spike_fixture_a_parents, sizeof(spike_fixture_a_parents) / sizeof(int32_t) },
    { "b", spike_fixture_b, spike_fixture_b_parents, sizeof(spike_fixture_b_parents) / sizeof(int32_t) },
};

// ---------------------------------------------------------------------------------------------------------------------
// Loading

typedef struct SpikeFile SpikeFile;
struct SpikeFile
{
    char* path;
    uint8_t* source;
    uint64_t length;
    SpikeTokens tokens;
    // For a fixture, each token's expected parent (-1 for the root).
    const int32_t* expected;
    uint32_t expected_count;
};

typedef struct SpikeCorpus SpikeCorpus;
struct SpikeCorpus
{
    Arena* memory;
    SpikeNames names;
    SpikeFile* files;
    uint32_t file_count;
    uint64_t token_count;
    uint64_t byte_count;
    uint32_t max_tokens;
    // The lexer's output for the largest file, reused for each.
    uint8_t* lexer_kinds;
    uint32_t* lexer_starts;
    uint32_t* lexer_ends;
    uint64_t lexer_capacity;
};

static bool spike_names_init(SpikeNames* names, Arena* memory)
{
    const uint64_t slots = 2ull * BUSTER_SPIKE_NAME_CAPACITY;
    names->slots = BUSTER_ARENA_PUSH_ARRAY(memory, uint32_t, slots);
    names->slot_mask = slots - 1;
    names->offsets = BUSTER_ARENA_PUSH_ARRAY(memory, uint32_t, BUSTER_SPIKE_NAME_CAPACITY);
    names->lengths = BUSTER_ARENA_PUSH_ARRAY(memory, uint32_t, BUSTER_SPIKE_NAME_CAPACITY);
    names->hashes = BUSTER_ARENA_PUSH_ARRAY(memory, uint64_t, BUSTER_SPIKE_NAME_CAPACITY);
    names->kinds = BUSTER_ARENA_PUSH_ARRAY(memory, uint8_t, BUSTER_SPIKE_NAME_CAPACITY);
    names->count = 0;
    names->capacity = BUSTER_SPIKE_NAME_CAPACITY;
    names->bytes = arena_push(memory, BUSTER_SPIKE_NAME_BYTES, 64);
    names->byte_count = 0;
    names->byte_capacity = BUSTER_SPIKE_NAME_BYTES;
    bool result = names->slots != 0 && names->kinds != 0 && names->bytes != 0;
    if (result)
    {
        memset(names->slots, 0, slots * sizeof(uint32_t));
        result = spike_names_add_keywords(names);
    }
    return result;
}

// Lexes and refines the source already in file->source, and adds the file to the corpus.
static bool spike_add_source(SpikeCorpus* corpus, SpikeFile* file, const char* path, uint64_t length, uint64_t read_bytes)
{
    bool result = false;
    {
        const uint64_t path_length = strlen(path);
        file->path = arena_push(corpus->memory, path_length + 1, 1);
        const uint64_t capacity = BUSTER_C_TOKENIZER_CAPACITY(length);
        if (capacity > corpus->lexer_capacity)
        {
            corpus->lexer_kinds = BUSTER_ARENA_PUSH_ARRAY(corpus->memory, uint8_t, capacity);
            corpus->lexer_starts = BUSTER_ARENA_PUSH_ARRAY(corpus->memory, uint32_t, capacity);
            corpus->lexer_ends = BUSTER_ARENA_PUSH_ARRAY(corpus->memory, uint32_t, capacity);
            corpus->lexer_capacity = capacity;
        }
        if (file->path == 0 || file->source == 0 || read_bytes != length || corpus->lexer_ends == 0)
        {
            fprintf(stderr, "can't load %s\n", path);
        }
        else
        {
            memcpy(file->path, path, path_length + 1);
            file->length = length;
            const int64_t lexed = c_tokenize(file->source, length, corpus->lexer_kinds, corpus->lexer_starts, corpus->lexer_ends, 0);
            const uint64_t slots = 2 * (lexed > 0 ? (uint64_t)lexed : 0) + 2 * BUSTER_SPIKE_TOKEN_PADDING;
            uint8_t* kinds = BUSTER_ARENA_PUSH_ARRAY(corpus->memory, uint8_t, slots);
            uint32_t* names = BUSTER_ARENA_PUSH_ARRAY(corpus->memory, uint32_t, slots);
            uint32_t* starts = BUSTER_ARENA_PUSH_ARRAY(corpus->memory, uint32_t, slots);
            uint32_t* ends = BUSTER_ARENA_PUSH_ARRAY(corpus->memory, uint32_t, slots);
            if (lexed < 0 || ends == 0)
            {
                fprintf(stderr, "can't lex %s\n", path);
            }
            else
            {
                file->tokens = (SpikeTokens){
                    .kinds = kinds + BUSTER_SPIKE_TOKEN_PADDING,
                    .names = names + BUSTER_SPIKE_TOKEN_PADDING,
                    .starts = starts + BUSTER_SPIKE_TOKEN_PADDING,
                    .ends = ends + BUSTER_SPIKE_TOKEN_PADDING,
                    .count = 0,
                    .pragma_tokens = 0,
                };
                result = spike_refine(file->source, corpus->lexer_kinds, corpus->lexer_starts, corpus->lexer_ends, (uint64_t)lexed, &corpus->names, &file->tokens);
                if (!result)
                {
                    fprintf(stderr, "the identifier table is full at %s\n", path);
                }
                else
                {
                    corpus->file_count += 1;
                    corpus->token_count += file->tokens.count;
                    corpus->byte_count += length;
                    corpus->max_tokens = file->tokens.count > corpus->max_tokens ? (uint32_t)file->tokens.count : corpus->max_tokens;
                }
            }
        }
    }
    return result;
}

// Reads a file, lexes and refines it, and adds it to the corpus. Returns false (with a message) if it can't.
static bool spike_load_file(SpikeCorpus* corpus, const char* path)
{
    bool result = false;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    struct stat file_stats;
    if (fd < 0 || fstat(fd, &file_stats) != 0)
    {
        fprintf(stderr, "can't read %s\n", path);
    }
    else if (corpus->file_count == BUSTER_SPIKE_MAX_FILES)
    {
        fprintf(stderr, "too many files\n");
    }
    else
    {
        const uint64_t length = (uint64_t)file_stats.st_size;
        SpikeFile* file = &corpus->files[corpus->file_count];
        *file = (SpikeFile){ 0 };
        file->source = arena_push(corpus->memory, length + 1, 64);
        uint64_t read_bytes = 0;
        ssize_t n = 1;
        while (file->source != 0 && n > 0 && read_bytes < length)
        {
            n = read(fd, file->source + read_bytes, length - read_bytes);
            read_bytes += n > 0 ? (uint64_t)n : 0;
        }
        result = spike_add_source(corpus, file, path, length, read_bytes);
    }
    if (fd >= 0)
    {
        close(fd);
    }
    return result;
}

// Adds the fixtures to the corpus, each with its expected parents.
static bool spike_load_fixtures(SpikeCorpus* corpus)
{
    bool result = true;
    for (uint32_t i = 0; result && i < sizeof(spike_fixtures) / sizeof(spike_fixtures[0]); i += 1)
    {
        const SpikeFixture* fixture = &spike_fixtures[i];
        const uint64_t length = strlen(fixture->source);
        SpikeFile* file = &corpus->files[corpus->file_count];
        *file = (SpikeFile){ 0 };
        file->source = arena_push(corpus->memory, length + 1, 64);
        result = file->source != 0;
        if (result)
        {
            memcpy(file->source, fixture->source, length);
            char path[64];
            snprintf(path, sizeof(path), "fixture %s", fixture->name);
            result = spike_add_source(corpus, file, path, length, length);
            file->expected = fixture->parents;
            file->expected_count = fixture->count;
        }
    }
    return result;
}

static bool spike_ends_with(const char* name, const char* suffix)
{
    const uint64_t name_length = strlen(name);
    const uint64_t suffix_length = strlen(suffix);
    return name_length >= suffix_length && memcmp(name + name_length - suffix_length, suffix, suffix_length) == 0;
}

// Sorts names in place, so that a directory loads in the same order everywhere.
static void spike_sort_names(char** names, uint32_t count)
{
    for (uint32_t i = 1; i < count; i += 1)
    {
        char* name = names[i];
        uint32_t j = i;
        while (j > 0 && strcmp(names[j - 1], name) > 0)
        {
            names[j] = names[j - 1];
            j -= 1;
        }
        names[j] = name;
    }
}

// Loads a file, or every .i file of a directory (not its subdirectories), in name order.
static bool spike_load_path(SpikeCorpus* corpus, const char* path)
{
    bool result = true;
    struct stat path_stats;
    if (stat(path, &path_stats) != 0)
    {
        fprintf(stderr, "can't open %s\n", path);
        result = false;
    }
    else if (!S_ISDIR(path_stats.st_mode))
    {
        result = spike_load_file(corpus, path);
    }
    else
    {
        DIR* directory = opendir(path);
        char** names = BUSTER_ARENA_PUSH_ARRAY(corpus->memory, char*, BUSTER_SPIKE_MAX_FILES);
        uint32_t name_count = 0;
        struct dirent* entry = directory != 0 && names != 0 ? readdir(directory) : 0;
        while (entry != 0 && result)
        {
            if (entry->d_name[0] != '.' && spike_ends_with(entry->d_name, ".i") && name_count < BUSTER_SPIKE_MAX_FILES)
            {
                const uint64_t length = strlen(path) + 1 + strlen(entry->d_name);
                char* name = arena_push(corpus->memory, length + 1, 1);
                result = name != 0;
                if (result)
                {
                    snprintf(name, length + 1, "%s/%s", path, entry->d_name);
                    names[name_count] = name;
                    name_count += 1;
                }
            }
            entry = readdir(directory);
        }
        if (directory != 0)
        {
            closedir(directory);
        }
        spike_sort_names(names, name_count);
        for (uint32_t i = 0; result && i < name_count; i += 1)
        {
            result = spike_load_file(corpus, names[i]);
        }
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Generated worst cases

// Text appended to a buffer in the arena; `ok` turns false if the buffer is full.
typedef struct SpikeText SpikeText;
struct SpikeText
{
    char* bytes;
    uint64_t length;
    uint64_t capacity;
    bool ok;
};

static void spike_text(SpikeText* text, const char* format, ...) __attribute__((format(printf, 2, 3)));

#include <stdarg.h>

static void spike_text(SpikeText* text, const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    const uint64_t room = text->capacity - text->length;
    const int written = vsnprintf(text->bytes + text->length, room, format, arguments);
    va_end(arguments);
    if (written < 0 || (uint64_t)written >= room)
    {
        text->ok = false;
    }
    else
    {
        text->length += (uint64_t)written;
    }
}

typedef enum SpikeCase
{
    // One expression of `size` binary operators of every precedence.
    SPIKE_CASE_CHAIN,
    // Parentheses nested `size` deep, then blocks and if-statements nested a hundredth as deep.
    SPIKE_CASE_NESTING,
    // Tokens without brackets: a declaration of `size` names with initializers.
    SPIKE_CASE_FLAT,
    // Nothing but brackets: a call of a call of ..., `size` times.
    SPIKE_CASE_BRACKETS,
    // Casts of casts: each `(` after a cast's `)` is decided by that cast, the longest chain for K2's rounds.
    SPIKE_CASE_CASTS,
    // Ternaries nested in their third operands, and in their middle ones.
    SPIKE_CASE_TERNARIES,
    // if ... else if ... chains and switch statements, for the statement stack.
    SPIKE_CASE_STATEMENTS,
    // Declarations of typedef'd types, pointers to functions and structs.
    SPIKE_CASE_DECLARATIONS,
    SPIKE_CASE_COUNT,
} SpikeCase;

static const char* const spike_case_names[SPIKE_CASE_COUNT] = { "chain", "nesting", "flat", "brackets", "casts", "ternaries", "statements", "declarations" };

static void spike_generate(SpikeText* text, SpikeCase which, uint32_t size)
{
    static const char* const operators[] = { "*", "/", "+", "-", "<<", ">>", "<", ">=", "==", "!=", "&", "^", "|", "&&", "||" };
    spike_text(text, "typedef int T;\nint f(int a, int b, int c, int d)\n{\n    int x;\n");
    switch (which)
    {
        case SPIKE_CASE_CHAIN:
            spike_text(text, "    x = a");
            for (uint32_t i = 0; i < size; i += 1)
            {
                spike_text(text, " %s %c", operators[(i * 7) % (sizeof(operators) / sizeof(operators[0]))], 'a' + (int)(i % 4));
            }
            spike_text(text, ";\n");
            break;
        case SPIKE_CASE_NESTING:
            spike_text(text, "    x = ");
            for (uint32_t i = 0; i < size; i += 1)
            {
                spike_text(text, "(");
            }
            spike_text(text, "a");
            for (uint32_t i = 0; i < size; i += 1)
            {
                spike_text(text, i % 3 == 0 ? ") + b" : ")");
            }
            spike_text(text, ";\n");
            for (uint32_t i = 0; i < size / 100; i += 1)
            {
                spike_text(text, "    if (a) {\n");
            }
            spike_text(text, "    x = 1;\n");
            for (uint32_t i = 0; i < size / 100; i += 1)
            {
                spike_text(text, "    }\n");
            }
            break;
        case SPIKE_CASE_FLAT:
            spike_text(text, "    int y0 = 0");
            for (uint32_t i = 1; i < size; i += 1)
            {
                spike_text(text, ", y%u = a %s b", i, operators[i % (sizeof(operators) / sizeof(operators[0]))]);
            }
            spike_text(text, ";\n");
            break;
        case SPIKE_CASE_BRACKETS:
            spike_text(text, "    x = g");
            for (uint32_t i = 0; i < size; i += 1)
            {
                spike_text(text, i % 5 == 4 ? "[0]" : "()");
            }
            spike_text(text, ";\n");
            break;
        case SPIKE_CASE_CASTS:
            for (uint32_t i = 0; i < size / 100; i += 1)
            {
                spike_text(text, "    x = ");
                for (uint32_t j = 0; j < 100; j += 1)
                {
                    spike_text(text, "(T)");
                }
                spike_text(text, "(a) * b;\n");
            }
            break;
        case SPIKE_CASE_TERNARIES:
            spike_text(text, "    x = a");
            for (uint32_t i = 0; i < size; i += 1)
            {
                spike_text(text, i % 2 == 0 ? " ? b : c" : " ? b ? c : d : a");
            }
            spike_text(text, ";\n");
            break;
        case SPIKE_CASE_STATEMENTS:
            for (uint32_t i = 0; i < size / 20; i += 1)
            {
                spike_text(text, "    if (a == %u) x = 1; else if (b) { x = 2; } else if (c) do x++; while (d); else\n", i);
                spike_text(text, "    switch (x) { case 1: case 2 ... 3: x = 4; break; default: L%u: goto L%u; }\n", i, i);
            }
            break;
        case SPIKE_CASE_DECLARATIONS:
            for (uint32_t i = 0; i < size / 30; i += 1)
            {
                spike_text(text, "    T v%u, *p%u = &v%u, (*q%u)(T, const char *) = 0;\n", i, i, i, i);
                spike_text(text, "    struct s%u { T m : 3; unsigned n; struct s%u *next; } w%u = { .m = 1, .n = (unsigned)(T)2 };\n", i, i, i);
            }
            break;
        default:
            break;
    }
    spike_text(text, "    return x;\n}\n");
}

// Adds the generated cases to the corpus, each `size` operations long.
static bool spike_load_generated(SpikeCorpus* corpus, uint32_t size)
{
    bool result = true;
    for (uint32_t which = 0; result && which < SPIKE_CASE_COUNT; which += 1)
    {
        SpikeText text = { .capacity = 64ull * size + 4096, .length = 0, .ok = true };
        text.bytes = arena_push(corpus->memory, text.capacity, 64);
        result = text.bytes != 0;
        if (result)
        {
            spike_generate(&text, (SpikeCase)which, size);
            result = text.ok;
        }
        if (result)
        {
            SpikeFile* file = &corpus->files[corpus->file_count];
            *file = (SpikeFile){ 0 };
            file->source = (uint8_t*)text.bytes;
            char path[64];
            snprintf(path, sizeof(path), "generated %s", spike_case_names[which]);
            result = spike_add_source(corpus, file, path, text.length, text.length);
        }
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Checks

static void spike_print_token_text(const SpikeFile* file, uint32_t token)
{
    if (token < file->tokens.count)
    {
        const uint32_t start = file->tokens.starts[token];
        const uint32_t end = file->tokens.ends[token];
        const uint32_t length = end - start < 40 ? end - start : 40;
        printf("%.*s", (int)length, (const char*)file->source + start);
    }
    else
    {
        printf("<end>");
    }
}

static void spike_print_context(const SpikeFile* file, uint32_t token)
{
    const uint32_t begin = token > BUSTER_SPIKE_CONTEXT_TOKENS ? token - BUSTER_SPIKE_CONTEXT_TOKENS : 0;
    const uint64_t end = (uint64_t)token + BUSTER_SPIKE_CONTEXT_TOKENS < file->tokens.count ? token + BUSTER_SPIKE_CONTEXT_TOKENS : file->tokens.count;
    printf("   ");
    for (uint32_t i = begin; i < end; i += 1)
    {
        printf(i == token ? " >>" : " ");
        spike_print_token_text(file, i);
        printf(i == token ? "<<" : "");
    }
    printf("\n");
}

// One line per token: index, text, role, depth and parent.
static void spike_print_tree(const SpikeFile* file, const SpikeTree* tree, uint32_t begin, uint32_t end)
{
    for (uint32_t i = begin; i < end && i < file->tokens.count; i += 1)
    {
        printf("%6u  %-24.*s %-26s depth %-3u parent ", i, (int)(file->tokens.ends[i] - file->tokens.starts[i] < 24 ? file->tokens.ends[i] - file->tokens.starts[i] : 24),
               (const char*)file->source + file->tokens.starts[i], spike_role_names[tree->roles[i]], tree->depths[i]);
        if (tree->parents[i] == BUSTER_SPIKE_ROOT)
        {
            printf("root\n");
        }
        else
        {
            printf("%u ", tree->parents[i]);
            spike_print_token_text(file, tree->parents[i]);
            printf("\n");
        }
    }
}

// The tree's invariants: every parent is another token or the root, no deeper than its child; every opener's parent is
// a closer after it; and following parents always reaches the root (no cycle). `marks` has a byte per token. Returns
// the first token that breaks one, or BUSTER_SPIKE_NONE.
static uint32_t spike_check_tree(const SpikeTree* tree, uint32_t count, uint8_t* marks)
{
    uint32_t result = BUSTER_SPIKE_NONE;
    for (uint32_t i = 0; i < count && result == BUSTER_SPIKE_NONE; i += 1)
    {
        const uint32_t parent = tree->parents[i];
        const bool valid = parent == BUSTER_SPIKE_ROOT || (parent < count && parent != i && tree->depths[parent] <= tree->depths[i]);
        const bool opener = spike_role_rules[tree->roles[i]] == SPIKE_RULE_OPENER;
        if (!valid || (opener && (parent == BUSTER_SPIKE_ROOT || parent < i)))
        {
            result = i;
        }
    }
    // 0: not visited; 1: on the current path; 2: reaches the root.
    memset(marks, 0, count);
    for (uint32_t i = 0; i < count && result == BUSTER_SPIKE_NONE; i += 1)
    {
        uint32_t node = i;
        while (node != BUSTER_SPIKE_ROOT && marks[node] == 0)
        {
            marks[node] = 1;
            node = tree->parents[node];
        }
        if (node != BUSTER_SPIKE_ROOT && marks[node] == 1)
        {
            result = node;
        }
        node = i;
        while (node != BUSTER_SPIKE_ROOT && marks[node] == 1)
        {
            marks[node] = 2;
            node = tree->parents[node];
        }
    }
    return result;
}

// The role the kernels can tell: a function declarator's parentheses look like a call's (both are postfix groups after
// an operand, with the same tree), so they compare as one.
static uint8_t spike_canonical_role(uint8_t role)
{
    uint8_t result = role;
    if (role == SPIKE_ROLE_OPEN_FUNCTION)
    {
        result = SPIKE_ROLE_OPEN_CALL;
    }
    else if (role == SPIKE_ROLE_CLOSE_FUNCTION)
    {
        result = SPIKE_ROLE_CLOSE_CALL;
    }
    return result;
}

// Compares a kernel path's tree with the scalar parser's. Returns the first token whose parent differs, or
// BUSTER_SPIKE_NONE; *role_differences counts the tokens whose (canonical) roles differ outside flagged declarations.
static uint32_t spike_compare(const SpikeTree* expected, const SpikeKernels* kernels, uint32_t count, uint64_t* role_differences, uint32_t* first_role_difference)
{
    uint32_t result = BUSTER_SPIKE_NONE;
    for (uint32_t i = 0; i < count; i += 1)
    {
        if (result == BUSTER_SPIKE_NONE && expected->parents[i] != kernels->parents[i])
        {
            result = i;
        }
        if (spike_canonical_role(expected->roles[i]) != spike_canonical_role(kernels->roles[i]))
        {
            if (*role_differences == 0)
            {
                *first_role_difference = i;
            }
            *role_differences += 1;
        }
    }
    return result;
}

static void spike_print_both(const SpikeFile* file, const SpikeTree* expected, const SpikeKernels* kernels, uint32_t begin, uint32_t end)
{
    for (uint32_t i = begin; i < end && i < file->tokens.count; i += 1)
    {
        printf("%6u  %-20.*s %-24s %-24s %c parents %10d %10d%s\n", i, (int)(file->tokens.ends[i] - file->tokens.starts[i] < 20 ? file->tokens.ends[i] - file->tokens.starts[i] : 20),
               (const char*)file->source + file->tokens.starts[i], spike_role_names[expected->roles[i]], spike_role_names[kernels->roles[i]], kernels->flags[i] ? 'F' : ' ',
               (int)expected->parents[i], (int)kernels->parents[i], expected->parents[i] != kernels->parents[i] ? "  <<" : "");
    }
}

// Each kernel against its scalar twin on one file, after spike_parse_twins has filled `kernels` (whose inputs the
// vector kernels then read). `copy` holds a token array's worth of each output for the comparison. Returns the number
// of kernels whose output differs, and prints the first difference of each.
typedef struct SpikeKernelCopy SpikeKernelCopy;
struct SpikeKernelCopy
{
    uint32_t* depths;
    uint32_t* enclosing;
    uint32_t* open_stack;
    uint32_t* parents;
    uint32_t* lefts;
    uint16_t* left_keys;
    SpikeCandidates candidates;
    uint8_t* typed;
    uint8_t* roles;
    uint8_t* flags;
    uint8_t* classified_flags;
    uint8_t* group_roles;
    uint8_t* group_typed;
    uint64_t rounds[BUSTER_SPIKE_ROLE_ROUNDS + 1];
    // K4 in byte lanes: 64-token blocks, and those whose depths span too much for bytes.
    uint64_t byte_blocks;
    uint64_t wide_blocks;
};

static uint32_t spike_check_kernels(const SpikeFile* file, SpikeKernels* kernels, SpikeKernelCopy* copy, uint32_t count)
{
    uint32_t result = 0;
    const uint8_t* kinds = file->tokens.kinds;
    // K1 on ( [ {.
    const bool twin_balanced = spike_brackets_scalar(kinds, spike_kind_deltas, count, kernels->depths, kernels->enclosing, kernels->stack);
    const bool narrow_balanced = spike_brackets16_avx512(kinds, spike_kind_deltas, count, copy->depths, copy->enclosing, copy->open_stack);
    uint32_t first = BUSTER_SPIKE_NONE;
    for (uint32_t i = 0; i < count && first == BUSTER_SPIKE_NONE; i += 1)
    {
        if (kernels->depths[i] != copy->depths[i] || kernels->enclosing[i] != copy->enclosing[i])
        {
            first = i;
        }
    }
    if (first != BUSTER_SPIKE_NONE || twin_balanced != narrow_balanced)
    {
        printf("%s: K1 in 16-bit lanes differs at token %u\n", file->path, first);
        result += 1;
    }
    const bool vector_balanced = spike_brackets_avx512(kinds, spike_kind_deltas, count, copy->depths, copy->enclosing, copy->open_stack);
    first = BUSTER_SPIKE_NONE;
    for (uint32_t i = 0; i < count && first == BUSTER_SPIKE_NONE; i += 1)
    {
        if (kernels->depths[i] != copy->depths[i] || kernels->enclosing[i] != copy->enclosing[i])
        {
            first = i;
        }
    }
    if (first != BUSTER_SPIKE_NONE || twin_balanced != vector_balanced)
    {
        printf("%s: K1 differs at token %u (balanced %d %d)\n", file->path, first, twin_balanced, vector_balanced);
        if (first != BUSTER_SPIKE_NONE)
        {
            printf("  depth %u %u, enclosing %d %d\n", kernels->depths[first], copy->depths[first], (int)kernels->enclosing[first], (int)copy->enclosing[first]);
            spike_print_context(file, first);
        }
        result += 1;
    }
    // K4, on the twins' roles and depths up to the second K1 pass.
    kernels->unbalanced = !twin_balanced;
    kernels->unsupported = false;
    if (!kernels->unbalanced)
    {
        spike_typedefs(kernels, kinds, file->tokens.names, count);
    }
    // K3's classification: both vector variants against the twin.
    spike_classify_gather_avx512(kernels, kinds, file->tokens.names, count);
    memcpy(copy->roles, kernels->vkinds, count);
    memcpy(copy->flags, kernels->flags, count);
    spike_classify_mask_avx512(kernels, kinds, file->tokens.names, count);
    memcpy(copy->typed, kernels->vkinds, count);
    memcpy(copy->classified_flags, kernels->flags, count);
    spike_classify_scalar(kernels, kinds, file->tokens.names, count);
    if (memcmp(copy->roles, kernels->vkinds, count) != 0 || memcmp(copy->typed, kernels->vkinds, count) != 0 || memcmp(copy->flags, kernels->flags, count) != 0 ||
        memcmp(copy->classified_flags, kernels->flags, count) != 0)
    {
        printf("%s: K3's classification differs\n", file->path);
        result += 1;
    }
    memcpy(copy->classified_flags, kernels->flags, count);
    // K2: the vector kernel first, then the twin, which the later kernels read.
    spike_roles_avx512(kernels, count, copy->typed, copy->group_roles, copy->group_typed, copy->rounds);
    memcpy(copy->roles, kernels->roles, count);
    memcpy(copy->flags, kernels->flags, count);
    memcpy(kernels->flags, copy->classified_flags, count);
    spike_roles_scalar(kernels, count, copy->typed);
    first = BUSTER_SPIKE_NONE;
    for (uint32_t i = 0; i < count && first == BUSTER_SPIKE_NONE; i += 1)
    {
        if (kernels->roles[i] != copy->roles[i] || kernels->flags[i] != copy->flags[i])
        {
            first = i;
        }
    }
    if (first != BUSTER_SPIKE_NONE)
    {
        printf("%s: K2 differs at token %u: twin %s%s, vector %s%s\n", file->path, first, spike_role_names[kernels->roles[first]], kernels->flags[first] ? " (flagged)" : "",
               spike_role_names[copy->roles[first]], copy->flags[first] ? " (flagged)" : "");
        spike_print_context(file, first);
        result += 1;
    }
    spike_secondary_scalar(kernels, count, kernels->stack);
    spike_after_colons(kernels, count);
    for (uint32_t i = 0; i < kernels->typedef_count; i += 1)
    {
        kernels->typedef_names[kernels->typedef_log[i]] = 0;
    }
    uint32_t deepest = 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        deepest = kernels->node_depths[i] > deepest ? kernels->node_depths[i] : deepest;
    }
    spike_parents_scalar(kernels, count);
    if (deepest <= BUSTER_SPIKE_MAX_VECTOR_DEPTH)
    {
        spike_parents_left_avx512(kernels->roles, kernels->node_depths, count, copy->lefts, copy->left_keys, &copy->candidates);
        spike_parents_right_avx512(kernels->roles, kernels->node_depths, count, copy->lefts, copy->left_keys, copy->parents, &copy->candidates);
        first = BUSTER_SPIKE_NONE;
        for (uint32_t i = 0; i < count && first == BUSTER_SPIKE_NONE; i += 1)
        {
            if (kernels->parents[i] != copy->parents[i])
            {
                first = i;
            }
        }
        if (first != BUSTER_SPIKE_NONE)
        {
            printf("%s: K4 differs at token %u: twin %d, vector %d (L %d)\n", file->path, first, (int)kernels->parents[first], (int)copy->parents[first],
                   (int)copy->lefts[first]);
            spike_print_context(file, first);
            result += 1;
        }
        // In byte lanes.
        uint64_t wide = 0;
        spike_parents_left8_avx512(kernels->roles, kernels->node_depths, count, copy->lefts, copy->left_keys, &copy->candidates, &wide);
        spike_parents_right8_avx512(kernels->roles, kernels->node_depths, count, copy->lefts, copy->left_keys, copy->parents, &copy->candidates);
        copy->wide_blocks += wide;
        copy->byte_blocks += (count + 63) / 64;
        first = BUSTER_SPIKE_NONE;
        for (uint32_t i = 0; i < count && first == BUSTER_SPIKE_NONE; i += 1)
        {
            if (kernels->parents[i] != copy->parents[i])
            {
                first = i;
            }
        }
        if (first != BUSTER_SPIKE_NONE)
        {
            printf("%s: K4 in byte lanes differs at token %u: twin %d, vector %d (L %d)\n", file->path, first, (int)kernels->parents[first], (int)copy->parents[first],
                   (int)copy->lefts[first]);
            spike_print_context(file, first);
            result += 1;
        }
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Timing

// Passes after one warm-up pass; each implementation runs once per pass, in turn, and the medians are reported.
#define BUSTER_SPIKE_PASSES 21

typedef struct SpikeCounters SpikeCounters;
struct SpikeCounters
{
    int cycles;
    int instructions;
};

typedef struct SpikeSample SpikeSample;
struct SpikeSample
{
    uint64_t nanoseconds;
    uint64_t cycles;
    uint64_t instructions;
};

// A counter of this thread's user-space events, or -1 where perf_event_open isn't allowed.
static int spike_open_counter(uint64_t event)
{
    struct perf_event_attr attributes;
    memset(&attributes, 0, sizeof(attributes));
    attributes.size = sizeof(attributes);
    attributes.type = PERF_TYPE_HARDWARE;
    attributes.config = event;
    attributes.exclude_kernel = 1;
    attributes.exclude_hv = 1;
    return (int)syscall(SYS_perf_event_open, &attributes, 0, -1, -1, PERF_FLAG_FD_CLOEXEC);
}

static uint64_t spike_read_counter(int fd)
{
    uint64_t value = 0;
    if (fd < 0 || read(fd, &value, sizeof(value)) != sizeof(value))
    {
        value = 0;
    }
    return value;
}

static SpikeSample spike_sample(const SpikeCounters* counters)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    SpikeSample result = {
        .nanoseconds = (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec,
        .cycles = spike_read_counter(counters->cycles),
        .instructions = spike_read_counter(counters->instructions),
    };
    return result;
}

static SpikeSample spike_difference(SpikeSample before, SpikeSample after)
{
    SpikeSample result = {
        .nanoseconds = after.nanoseconds - before.nanoseconds,
        .cycles = after.cycles - before.cycles,
        .instructions = after.instructions - before.instructions,
    };
    return result;
}

static void spike_sort(uint64_t* values, uint32_t count)
{
    for (uint32_t i = 1; i < count; i += 1)
    {
        const uint64_t value = values[i];
        uint32_t j = i;
        while (j > 0 && values[j - 1] > value)
        {
            values[j] = values[j - 1];
            j -= 1;
        }
        values[j] = value;
    }
}

// Prints the medians of `samples` per token.
static void spike_report(const char* name, const SpikeSample* samples, uint32_t count, uint64_t tokens)
{
    uint64_t nanoseconds[BUSTER_SPIKE_PASSES];
    uint64_t cycles[BUSTER_SPIKE_PASSES];
    uint64_t instructions[BUSTER_SPIKE_PASSES];
    for (uint32_t i = 0; i < count; i += 1)
    {
        nanoseconds[i] = samples[i].nanoseconds;
        cycles[i] = samples[i].cycles;
        instructions[i] = samples[i].instructions;
    }
    spike_sort(nanoseconds, count);
    spike_sort(cycles, count);
    spike_sort(instructions, count);
    const double t = (double)tokens;
    printf("  %-28s %9.3f ms  %7.3f ns/token  %7.3f cycles/token [%.3f-%.3f]  %7.3f instructions/token  IPC %.2f\n", name,
           (double)nanoseconds[count / 2] / 1e6, (double)nanoseconds[count / 2] / t, (double)cycles[count / 2] / t, (double)cycles[0] / t,
           (double)cycles[count - 1] / t, (double)instructions[count / 2] / t, cycles[count / 2] ? (double)instructions[count / 2] / (double)cycles[count / 2] : 0.0);
}

// ---------------------------------------------------------------------------------------------------------------------
// The benchmark

// What is timed: whole paths over every file (each file through all of a path before the next), then each kernel alone
// over every file, the twins' and the vector path's, in order, with each file's arrays kept between the kernels.
typedef enum SpikeTimed
{
    SPIKE_TIMED_SCALAR_PARSER,
    SPIKE_TIMED_TWINS,
    SPIKE_TIMED_VECTOR,
    SPIKE_TIMED_TWIN_K1,
    SPIKE_TIMED_TWIN_K3_PASS,
    SPIKE_TIMED_TWIN_K3,
    SPIKE_TIMED_TWIN_K2,
    SPIKE_TIMED_TWIN_SECONDARY,
    SPIKE_TIMED_TWIN_K4,
    SPIKE_TIMED_TWIN_K5,
    SPIKE_TIMED_TWIN_FALLBACK,
    SPIKE_TIMED_VECTOR_K1_16,
    SPIKE_TIMED_VECTOR_K1,
    SPIKE_TIMED_VECTOR_K3_PASS,
    SPIKE_TIMED_VECTOR_K3_GATHER,
    SPIKE_TIMED_VECTOR_K3,
    SPIKE_TIMED_VECTOR_K2,
    SPIKE_TIMED_VECTOR_SECONDARY,
    SPIKE_TIMED_VECTOR_K4_8,
    SPIKE_TIMED_VECTOR_K4,
    SPIKE_TIMED_VECTOR_K5,
    SPIKE_TIMED_VECTOR_FALLBACK,
    SPIKE_TIMED_COUNT,
} SpikeTimed;

static const char* const spike_timed_names[SPIKE_TIMED_COUNT] =
{
    "scalar-parser",
    "twins",
    "vector",
    "twin-k1",
    "twin-k3-pass",
    "twin-k3",
    "twin-k2",
    "twin-k1-secondary",
    "twin-k4",
    "twin-k5",
    "twin-fallback",
    "vector-k1-16",
    "vector-k1",
    "vector-k3-pass",
    "vector-k3-gather",
    "vector-k3",
    "vector-k2",
    "vector-k1-secondary",
    "vector-k4-8",
    "vector-k4",
    "vector-k5",
    "vector-fallback",
};

// A file's arrays for the kernels run one at a time over the corpus.
typedef struct SpikeStageFile SpikeStageFile;
struct SpikeStageFile
{
    SpikeKernels kernels;
    bool statements_ok;
};

// Each file's own arrays, with the scratch they share from `shared`.
static bool spike_stage_files_reserve(SpikeStageFile* stages, const SpikeCorpus* corpus, const SpikeKernels* shared, Arena* arena)
{
    bool result = true;
    for (uint32_t f = 0; result && f < corpus->file_count; f += 1)
    {
        const uint64_t slots = corpus->files[f].tokens.count + 2 * BUSTER_SPIKE_TOKEN_PADDING;
        SpikeKernels* k = &stages[f].kernels;
        *k = *shared;
        uint8_t* vkinds = arena_push(arena, slots, 64);
        uint8_t* roles = arena_push(arena, slots, 64);
        uint8_t* flags = arena_push(arena, slots, 64);
        k->depths = arena_push_array(arena, slots, sizeof(uint32_t), 64);
        k->node_depths = arena_push_array(arena, slots, sizeof(uint32_t), 64);
        k->enclosing = arena_push_array(arena, slots, sizeof(uint32_t), 64);
        k->parents = arena_push_array(arena, slots, sizeof(uint32_t), 64);
        k->typedef_log = arena_push_array(arena, slots, sizeof(uint32_t), 64);
        k->typedef_tokens = arena_push_array(arena, slots, sizeof(uint32_t), 64);
        k->typedef_scopes = arena_push(arena, slots, 64);
        result = vkinds != 0 && roles != 0 && flags != 0 && k->depths != 0 && k->node_depths != 0 && k->enclosing != 0 && k->parents != 0 && k->typedef_log != 0 &&
                 k->typedef_tokens != 0 && k->typedef_scopes != 0;
        if (result)
        {
            memset(vkinds, SPIKE_KIND_END, slots);
            memset(roles, SPIKE_ROLE_NONE, slots);
            memset(flags, 0, slots);
            k->vkinds = vkinds + BUSTER_SPIKE_TOKEN_PADDING;
            k->roles = roles + BUSTER_SPIKE_TOKEN_PADDING;
            k->flags = flags + BUSTER_SPIKE_TOKEN_PADDING;
            k->typedef_count = 0;
        }
    }
    return result;
}

typedef struct SpikeBench SpikeBench;
struct SpikeBench
{
    const SpikeCorpus* corpus;
    SpikeMeanings* meanings;
    SpikeTree* tree;
    Arena* scratch;
    SpikeKernels* kernels;
    SpikeVectorBuffers* buffers;
    SpikeStageFile* stages;
    uint8_t* typed;
    SpikeStatement* statements;
    uint64_t wide_blocks;
};

static void spike_run_stage(SpikeBench* bench, SpikeTimed which)
{
    const SpikeCorpus* corpus = bench->corpus;
    SpikeVectorBuffers* buffers = bench->buffers;
    for (uint32_t f = 0; f < corpus->file_count; f += 1)
    {
        const SpikeFile* file = &corpus->files[f];
        const uint32_t count = (uint32_t)file->tokens.count;
        const uint8_t* kinds = file->tokens.kinds;
        const uint32_t* names = file->tokens.names;
        SpikeStageFile* stage = &bench->stages[f];
        SpikeKernels* k = &stage->kernels;
        switch (which)
        {
            case SPIKE_TIMED_SCALAR_PARSER: spike_parse_scalar(kinds, names, count, bench->meanings, bench->tree, bench->scratch); break;
            case SPIKE_TIMED_TWINS: spike_parse_twins(bench->kernels, kinds, names, count, bench->meanings, bench->scratch, bench->typed, bench->statements); break;
            case SPIKE_TIMED_VECTOR: spike_parse_vector(bench->kernels, kinds, names, count, bench->meanings, bench->scratch, buffers); break;
            case SPIKE_TIMED_TWIN_K1: k->unbalanced = !spike_brackets_scalar(kinds, spike_kind_deltas, count, k->depths, k->enclosing, k->stack); break;
            case SPIKE_TIMED_VECTOR_K1: k->unbalanced = !spike_brackets_avx512(kinds, spike_kind_deltas, count, k->depths, k->enclosing, buffers->open_stack); break;
            case SPIKE_TIMED_VECTOR_K1_16: k->unbalanced = !spike_brackets16_avx512(kinds, spike_kind_deltas, count, k->depths, k->enclosing, buffers->open_stack); break;
            case SPIKE_TIMED_TWIN_K3_PASS:
            case SPIKE_TIMED_VECTOR_K3_PASS:
                k->unsupported = false;
                k->typedef_count = 0;
                if (!k->unbalanced)
                {
                    spike_typedefs(k, kinds, names, count);
                }
                spike_typedefs_clear(k);
                break;
            case SPIKE_TIMED_TWIN_K3:
                spike_typedefs_mark(k);
                spike_classify_scalar(k, kinds, names, count);
                spike_typedefs_clear(k);
                break;
            case SPIKE_TIMED_VECTOR_K3_GATHER:
                spike_typedefs_mark(k);
                spike_classify_gather_avx512(k, kinds, names, count);
                spike_typedefs_clear(k);
                break;
            case SPIKE_TIMED_VECTOR_K3:
                spike_typedefs_mark(k);
                spike_classify_mask_avx512(k, kinds, names, count);
                spike_typedefs_clear(k);
                break;
            case SPIKE_TIMED_TWIN_K2: spike_roles_scalar(k, count, bench->typed); break;
            case SPIKE_TIMED_VECTOR_K2: spike_roles_avx512(k, count, buffers->typed, buffers->group_roles, buffers->group_typed, buffers->rounds); break;
            case SPIKE_TIMED_TWIN_SECONDARY:
                spike_secondary_scalar(k, count, k->stack);
                spike_after_colons(k, count);
                break;
            case SPIKE_TIMED_VECTOR_SECONDARY:
                spike_secondary_avx512(k, count, buffers->secondary);
                spike_after_colons_avx512(k, count);
                break;
            case SPIKE_TIMED_TWIN_K4: spike_parents_scalar(k, count); break;
            case SPIKE_TIMED_VECTOR_K4:
            case SPIKE_TIMED_VECTOR_K4_8:
                if (spike_deepest(k, count) > BUSTER_SPIKE_MAX_VECTOR_DEPTH)
                {
                    spike_parents_scalar(k, count);
                }
                else if (which == SPIKE_TIMED_VECTOR_K4)
                {
                    spike_parents_left_avx512(k->roles, k->node_depths, count, buffers->lefts, buffers->left_keys, &buffers->candidates);
                    spike_parents_right_avx512(k->roles, k->node_depths, count, buffers->lefts, buffers->left_keys, k->parents, &buffers->candidates);
                }
                else
                {
                    spike_parents_left8_avx512(k->roles, k->node_depths, count, buffers->lefts, buffers->left_keys, &buffers->candidates, &bench->wide_blocks);
                    spike_parents_right8_avx512(k->roles, k->node_depths, count, buffers->lefts, buffers->left_keys, k->parents, &buffers->candidates);
                }
                break;
            case SPIKE_TIMED_TWIN_K5: stage->statements_ok = spike_statements(k, count, bench->statements); break;
            case SPIKE_TIMED_VECTOR_K5: stage->statements_ok = spike_statements_avx512(k, count, buffers->statements); break;
            case SPIKE_TIMED_TWIN_FALLBACK:
            case SPIKE_TIMED_VECTOR_FALLBACK:
            {
                spike_typedefs_mark(k);
                const bool everything = k->unbalanced || k->unsupported || !stage->statements_ok;
                spike_fallback(k, kinds, names, count, everything, bench->meanings, bench->scratch);
                spike_typedefs_clear(k);
                break;
            }
            default: break;
        }
    }
}

// The stages run in this order, so that each finds its inputs; the variants of a kernel come right before it.
static const SpikeTimed spike_stage_order[] =
{
    SPIKE_TIMED_TWIN_K1,
    SPIKE_TIMED_TWIN_K3_PASS,
    SPIKE_TIMED_TWIN_K3,
    SPIKE_TIMED_TWIN_K2,
    SPIKE_TIMED_TWIN_SECONDARY,
    SPIKE_TIMED_TWIN_K4,
    SPIKE_TIMED_TWIN_K5,
    SPIKE_TIMED_TWIN_FALLBACK,
    SPIKE_TIMED_VECTOR_K1_16,
    SPIKE_TIMED_VECTOR_K1,
    SPIKE_TIMED_VECTOR_K3_PASS,
    SPIKE_TIMED_VECTOR_K3_GATHER,
    SPIKE_TIMED_VECTOR_K3,
    SPIKE_TIMED_VECTOR_K2,
    SPIKE_TIMED_VECTOR_SECONDARY,
    SPIKE_TIMED_VECTOR_K4_8,
    SPIKE_TIMED_VECTOR_K4,
    SPIKE_TIMED_VECTOR_K5,
    SPIKE_TIMED_VECTOR_FALLBACK,
};

// Times everything (or only `only`), `passes` interleaved passes after a warm-up one, and prints the medians.
static void spike_benchmark(SpikeBench* bench, uint32_t passes, int32_t only)
{
    SpikeCounters counters = {
        .cycles = spike_open_counter(PERF_COUNT_HW_CPU_CYCLES),
        .instructions = spike_open_counter(PERF_COUNT_HW_INSTRUCTIONS),
    };
    if (counters.cycles < 0 || counters.instructions < 0)
    {
        fprintf(stderr, "perf_event_open failed (see /proc/sys/kernel/perf_event_paranoid): cycles and instructions read as 0\n");
    }
    static SpikeSample samples[SPIKE_TIMED_COUNT][BUSTER_SPIKE_PASSES];
    for (uint32_t pass = 0; pass <= passes; pass += 1)
    {
        for (uint32_t which = SPIKE_TIMED_SCALAR_PARSER; which <= SPIKE_TIMED_VECTOR; which += 1)
        {
            if (only < 0 || only == (int32_t)which)
            {
                const SpikeSample before = spike_sample(&counters);
                spike_run_stage(bench, (SpikeTimed)which);
                const SpikeSample after = spike_sample(&counters);
                if (pass > 0)
                {
                    samples[which][pass - 1] = spike_difference(before, after);
                }
            }
        }
        for (uint32_t i = 0; i < sizeof(spike_stage_order) / sizeof(spike_stage_order[0]); i += 1)
        {
            const SpikeTimed which = spike_stage_order[i];
            if (only < 0 || only == (int32_t)which || only == SPIKE_TIMED_COUNT)
            {
                const SpikeSample before = spike_sample(&counters);
                spike_run_stage(bench, which);
                const SpikeSample after = spike_sample(&counters);
                if (pass > 0)
                {
                    samples[which][pass - 1] = spike_difference(before, after);
                }
            }
        }
    }
    printf("%u interleaved passes over %u files, %lu tokens, medians:\n", passes, bench->corpus->file_count, (unsigned long)bench->corpus->token_count);
    for (uint32_t which = 0; which < SPIKE_TIMED_COUNT; which += 1)
    {
        if (only < 0 || only == (int32_t)which)
        {
            spike_report(spike_timed_names[which], samples[which], passes, bench->corpus->token_count);
        }
    }
    if (counters.cycles >= 0)
    {
        close(counters.cycles);
    }
    if (counters.instructions >= 0)
    {
        close(counters.instructions);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// The command

static bool spike_allocate_tree(Arena* arena, SpikeTree* tree, uint64_t count)
{
    tree->roles = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, count + BUSTER_SPIKE_TOKEN_PADDING);
    tree->depths = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, count + BUSTER_SPIKE_TOKEN_PADDING);
    tree->parents = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, count + BUSTER_SPIKE_TOKEN_PADDING);
    return tree->roles != 0 && tree->depths != 0 && tree->parents != 0;
}

int run_parse_spike(int argc, char** argv)
{
    int result = 1;
    const char* dump = 0;
    bool check_only = false;
    bool show_flags = false;
    bool fixtures = false;
    uint32_t generated = 0;
    uint32_t passes = BUSTER_SPIKE_PASSES;
    int32_t only = -1;
    bool valid = true;
    uint32_t path_count = 0;
    for (int i = 1; i < argc; i += 1)
    {
        if (strcmp(argv[i], "--dump") == 0 && i + 1 < argc)
        {
            dump = argv[i + 1];
            i += 1;
        }
        else if (strcmp(argv[i], "--check-only") == 0)
        {
            check_only = true;
        }
        else if (strcmp(argv[i], "--flags") == 0)
        {
            show_flags = true;
        }
        else if (strcmp(argv[i], "--fixtures") == 0)
        {
            fixtures = true;
        }
        else if (strcmp(argv[i], "--generated") == 0 && i + 1 < argc)
        {
            generated = (uint32_t)strtoul(argv[i + 1], 0, 10);
            i += 1;
        }
        else if (strcmp(argv[i], "--passes") == 0 && i + 1 < argc)
        {
            passes = (uint32_t)strtoul(argv[i + 1], 0, 10);
            passes = passes == 0 ? 1 : passes > BUSTER_SPIKE_PASSES ? BUSTER_SPIKE_PASSES : passes;
            i += 1;
        }
        else if (strcmp(argv[i], "--only") == 0 && i + 1 < argc)
        {
            // A path or a stage alone (for perf), or "stages" for every stage.
            only = strcmp(argv[i + 1], "stages") == 0 ? SPIKE_TIMED_COUNT : -2;
            for (uint32_t t = 0; t < SPIKE_TIMED_COUNT; t += 1)
            {
                only = strcmp(argv[i + 1], spike_timed_names[t]) == 0 ? (int32_t)t : only;
            }
            valid &= only != -2;
            i += 1;
        }
        else if (argv[i][0] == '-')
        {
            valid = false;
        }
        else
        {
            path_count += 1;
        }
    }
    Arena memory = arena_reserve(BUSTER_SPIKE_MEMORY_RESERVE);
    Arena scratch = arena_reserve(BUSTER_SPIKE_MEMORY_RESERVE);
    SpikeCorpus corpus = {
        .memory = &memory,
        .files = BUSTER_ARENA_PUSH_ARRAY(&memory, SpikeFile, BUSTER_SPIKE_MAX_FILES),
    };
    if (!valid || (path_count == 0 && !fixtures && generated == 0))
    {
        fprintf(stderr, "usage: %s [--fixtures] [--generated N] [--check-only] [--flags] [--dump FILE] [--passes N] [--only NAME|stages] path...\n", argv[0]);
    }
    else if (corpus.files == 0 || !spike_names_init(&corpus.names, &memory))
    {
        fprintf(stderr, "can't reserve memory\n");
    }
    else
    {
        bool loaded = (!fixtures || spike_load_fixtures(&corpus)) && (generated == 0 || spike_load_generated(&corpus, generated));
        for (int i = 1; loaded && i < argc; i += 1)
        {
            if (strcmp(argv[i], "--dump") == 0 || strcmp(argv[i], "--generated") == 0 || strcmp(argv[i], "--passes") == 0 || strcmp(argv[i], "--only") == 0)
            {
                i += 1;
            }
            else if (argv[i][0] != '-')
            {
                loaded = spike_load_path(&corpus, argv[i]);
            }
        }
        SpikeMeanings meanings = {
            .meanings = BUSTER_ARENA_PUSH_ARRAY(&memory, uint8_t, BUSTER_SPIKE_NAME_CAPACITY),
            .capacity = BUSTER_SPIKE_NAME_CAPACITY,
        };
        SpikeTree tree;
        SpikeKernels kernels;
        uint8_t* marks = BUSTER_ARENA_PUSH_ARRAY(&memory, uint8_t, corpus.max_tokens + 1);
        uint8_t* typed = BUSTER_ARENA_PUSH_ARRAY(&memory, uint8_t, corpus.max_tokens + 1);
        SpikeStatement* statements = BUSTER_ARENA_PUSH_ARRAY(&memory, SpikeStatement, corpus.max_tokens + 1);
        SpikeVectorBuffers buffers;
        bool buffers_reserved = spike_vector_buffers_reserve(&buffers, &memory, corpus.max_tokens);
        SpikeKernelCopy copy = {
            .depths = BUSTER_ARENA_PUSH_ARRAY(&memory, uint32_t, corpus.max_tokens + 2 * BUSTER_SPIKE_TOKEN_PADDING),
            .enclosing = BUSTER_ARENA_PUSH_ARRAY(&memory, uint32_t, corpus.max_tokens + 2 * BUSTER_SPIKE_TOKEN_PADDING),
            .open_stack = BUSTER_ARENA_PUSH_ARRAY(&memory, uint32_t, corpus.max_tokens + 2 * BUSTER_SPIKE_TOKEN_PADDING),
            .parents = BUSTER_ARENA_PUSH_ARRAY(&memory, uint32_t, corpus.max_tokens + 2 * BUSTER_SPIKE_TOKEN_PADDING),
            .lefts = BUSTER_ARENA_PUSH_ARRAY(&memory, uint32_t, corpus.max_tokens + 2 * BUSTER_SPIKE_TOKEN_PADDING),
            .left_keys = BUSTER_ARENA_PUSH_ARRAY(&memory, uint16_t, corpus.max_tokens + 2 * BUSTER_SPIKE_TOKEN_PADDING),

            .typed = BUSTER_ARENA_PUSH_ARRAY(&memory, uint8_t, corpus.max_tokens + 2 * BUSTER_SPIKE_TOKEN_PADDING),
            .roles = BUSTER_ARENA_PUSH_ARRAY(&memory, uint8_t, corpus.max_tokens + 2 * BUSTER_SPIKE_TOKEN_PADDING),
            .flags = BUSTER_ARENA_PUSH_ARRAY(&memory, uint8_t, corpus.max_tokens + 2 * BUSTER_SPIKE_TOKEN_PADDING),
            .classified_flags = BUSTER_ARENA_PUSH_ARRAY(&memory, uint8_t, corpus.max_tokens + 2 * BUSTER_SPIKE_TOKEN_PADDING),
            .group_roles = BUSTER_ARENA_PUSH_ARRAY(&memory, uint8_t, corpus.max_tokens + 2 * BUSTER_SPIKE_TOKEN_PADDING),
            .group_typed = BUSTER_ARENA_PUSH_ARRAY(&memory, uint8_t, corpus.max_tokens + 2 * BUSTER_SPIKE_TOKEN_PADDING),
            .rounds = { 0 },
        };
        buffers_reserved &= spike_candidates_reserve(&copy.candidates, &memory, corpus.max_tokens);
        // The check's K1 and K2 use the vector path's padded stacks.
        copy.open_stack = buffers.open_stack;
        copy.group_roles = buffers.group_roles;
        copy.group_typed = buffers.group_typed;
        if (loaded && (meanings.meanings == 0 || marks == 0 || typed == 0 || statements == 0 || !spike_allocate_tree(&memory, &tree, corpus.max_tokens) ||
                       !spike_kernels_reserve(&kernels, &memory, corpus.max_tokens, BUSTER_SPIKE_NAME_CAPACITY) || !buffers_reserved))
        {
            fprintf(stderr, "can't reserve memory\n");
            loaded = false;
        }
        if (loaded)
        {
            memset(meanings.meanings, 0, BUSTER_SPIKE_NAME_CAPACITY);
            uint64_t pragma_tokens = 0;
            for (uint32_t i = 0; i < corpus.file_count; i += 1)
            {
                pragma_tokens += corpus.files[i].tokens.pragma_tokens;
            }
            printf("%u files, %lu bytes, %lu tokens (%lu pragma tokens dropped), %u names\n", corpus.file_count, (unsigned long)corpus.byte_count,
                   (unsigned long)corpus.token_count, (unsigned long)pragma_tokens, corpus.names.count);
            uint32_t failed = 0;
            uint32_t broken = 0;
            uint64_t failed_tokens = 0;
            static uint64_t role_stats[128][128];
            uint32_t twin_failures = 0;
            uint32_t kernel_failures = 0;
            uint32_t vector_failures = 0;
            uint64_t vector_fallback_tokens = 0;
            uint32_t flag_reports = 0;
            uint32_t role_reports = 0;
            uint32_t everything_files = 0;
            uint64_t total_role_differences = 0;
            uint64_t flagged_tokens = 0;
            uint64_t fallback_tokens = 0;
            uint64_t fallback_items = 0;
            for (uint32_t i = 0; i < corpus.file_count; i += 1)
            {
                const SpikeFile* file = &corpus.files[i];
                const uint32_t count = (uint32_t)file->tokens.count;
                const SpikeParseResult parse = spike_parse_scalar(file->tokens.kinds, file->tokens.names, count, &meanings, &tree, &scratch);
                if (parse.error_token != BUSTER_SPIKE_NONE)
                {
                    if (failed < 20)
                    {
                        printf("%s: token %u: %s\n", file->path, parse.error_token, parse.error_message);
                        spike_print_context(file, parse.error_token);
                    }
                    failed += 1;
                    failed_tokens += count;
                }
                else
                {
                    if (file->expected != 0)
                    {
                        uint32_t wrong = count == file->expected_count ? BUSTER_SPIKE_NONE : 0;
                        for (uint32_t t = 0; t < count && wrong == BUSTER_SPIKE_NONE; t += 1)
                        {
                            wrong = (int32_t)tree.parents[t] != file->expected[t] ? t : BUSTER_SPIKE_NONE;
                        }
                        if (wrong != BUSTER_SPIKE_NONE)
                        {
                            printf("%s: token %u's parent isn't the expected one (%u tokens, %u expected)\n", file->path, wrong, count, file->expected_count);
                            spike_print_context(file, wrong);
                            broken += 1;
                        }
                    }
                    const uint32_t bad = spike_check_tree(&tree, count, marks);
                    if (bad != BUSTER_SPIKE_NONE)
                    {
                        if (broken < 20)
                        {
                            printf("%s: token %u breaks the tree's invariants\n", file->path, bad);
                            spike_print_context(file, bad);
                            spike_print_tree(file, &tree, bad > 8 ? bad - 8 : 0, bad + 8);
                        }
                        broken += 1;
                    }
                    if (dump != 0 && strcmp(dump, file->path) == 0)
                    {
                        spike_print_tree(file, &tree, 0, count);
                    }
                    // The kernels' scalar twins, against the scalar parser.
                    const SpikeKernelResult twins = spike_parse_twins(&kernels, file->tokens.kinds, file->tokens.names, count, &meanings, &scratch, typed, statements);
                    for (uint32_t t = 0; t < count; t += 1)
                    {
                        role_stats[kernels.vkinds[t]][kernels.roles[t]] += 1;
                    }
                    uint64_t role_differences = 0;
                    uint32_t first_role_difference = 0;
                    const uint32_t different = spike_compare(&tree, &kernels, count, &role_differences, &first_role_difference);
                    flagged_tokens += twins.flagged_tokens;
                    for (uint32_t t = 0; show_flags && t < count && flag_reports < 60; t += 1)
                    {
                        if (kernels.flags[t])
                        {
                            printf("flag %s:%u ", file->path, t);
                            spike_print_context(file, t);
                            flag_reports += 1;
                        }
                    }
                    fallback_tokens += twins.fallback.tokens;
                    fallback_items += twins.fallback.items;
                    everything_files += twins.everything;
                    total_role_differences += role_differences;
                    if (twins.fallback.error_token != BUSTER_SPIKE_NONE)
                    {
                        printf("%s: the scalar parser fails on a fallback declaration at token %u\n", file->path, twins.fallback.error_token);
                        spike_print_context(file, twins.fallback.error_token);
                        twin_failures += 1;
                    }
                    else if (different != BUSTER_SPIKE_NONE)
                    {
                        if (twin_failures < 12)
                        {
                            printf("%s: the twins' parent of token %u differs\n", file->path, different);
                            spike_print_context(file, different);
                            spike_print_both(file, &tree, &kernels, different > 10 ? different - 10 : 0, different + 6);
                        }
                        twin_failures += 1;
                    }
                    else if (spike_check_kernels(file, &kernels, &copy, count) != 0)
                    {
                        kernel_failures += 1;
                    }
                    else
                    {
                        // The whole vector path, against the scalar parser.
                        const SpikeKernelResult vector = spike_parse_vector(&kernels, file->tokens.kinds, file->tokens.names, count, &meanings, &scratch, &buffers);
                        uint64_t vector_role_differences = 0;
                        uint32_t vector_first_role = 0;
                        const uint32_t vector_different = spike_compare(&tree, &kernels, count, &vector_role_differences, &vector_first_role);
                        vector_fallback_tokens += vector.fallback.tokens;
                        if (vector.fallback.error_token != BUSTER_SPIKE_NONE || vector_different != BUSTER_SPIKE_NONE || vector_role_differences != 0)
                        {
                            if (vector_failures < 12)
                            {
                                printf("%s: the vector path's tree differs at token %u (roles: %lu differ, the first at %u)\n", file->path, vector_different,
                                       (unsigned long)vector_role_differences, vector_first_role);
                                const uint32_t at = vector_different != BUSTER_SPIKE_NONE ? vector_different : vector_first_role;
                                spike_print_context(file, at);
                                spike_print_both(file, &tree, &kernels, at > 8 ? at - 8 : 0, at + 6);
                            }
                            vector_failures += 1;
                        }
                    }
                    if (role_differences != 0 && role_reports < 12)
                    {
                        printf("%s: %lu roles differ, the first at token %u\n", file->path, (unsigned long)role_differences, first_role_difference);
                        spike_print_both(file, &tree, &kernels, first_role_difference > 6 ? first_role_difference - 6 : 0, first_role_difference + 4);
                        role_reports += 1;
                    }
                }
            }
            printf("scalar parser: %u files fail (%lu tokens), %u trees break an invariant\n", failed, (unsigned long)failed_tokens, broken);
            printf("twins: %u files differ from the scalar parser; %lu roles differ; %lu tokens flagged; fallback: %lu declarations, %lu tokens (%.2f%%), %u whole files\n",
                   twin_failures, (unsigned long)total_role_differences, (unsigned long)flagged_tokens, (unsigned long)fallback_items, (unsigned long)fallback_tokens,
                   corpus.token_count ? 100.0 * (double)fallback_tokens / (double)corpus.token_count : 0.0, everything_files);
            printf("kernels: %u files where a vector kernel differs from its twin\n", kernel_failures);
            printf("vector path: %u files differ from the scalar parser; fallback: %lu tokens (%.2f%%)\n", vector_failures, (unsigned long)vector_fallback_tokens,
                   corpus.token_count ? 100.0 * (double)vector_fallback_tokens / (double)corpus.token_count : 0.0);
            if (getenv("SPIKE_ROLE_STATS") != 0)
            {
                for (uint32_t k = 0; k < BUSTER_SPIKE_VKINDS; k += 1)
                {
                    uint64_t total = 0;
                    uint32_t best = 0;
                    for (uint32_t r = 0; r < SPIKE_ROLE_COUNT; r += 1)
                    {
                        total += role_stats[k][r];
                        best = role_stats[k][r] > role_stats[k][best] ? r : best;
                    }
                    if (total != 0)
                    {
                        printf("vkind %-22s %9lu  best %-24s %.2f%%\n", k < SPIKE_KIND_COUNT ? spike_kind_names[k] : "TYPE_NAME", (unsigned long)total, spike_role_names[best],
                               100.0 * (double)role_stats[k][best] / (double)total);
                    }
                }
            }
            printf("K2 rounds per block:");
            for (uint32_t r = 0; r <= BUSTER_SPIKE_ROLE_ROUNDS; r += 1)
            {
                printf(" %u:%lu", r, (unsigned long)copy.rounds[r]);
            }
            printf(" (0: the twin)\n");
            printf("K4 in byte lanes: %lu of %lu blocks span too many depths and run as 16-bit blocks\n", (unsigned long)copy.wide_blocks, (unsigned long)copy.byte_blocks);
            result = failed == 0 && broken == 0 && twin_failures == 0 && kernel_failures == 0 && vector_failures == 0 ? 0 : 1;
            SpikeStageFile* stages = BUSTER_ARENA_PUSH_ARRAY(&memory, SpikeStageFile, corpus.file_count + 1);
            if (result == 0 && !check_only && (stages == 0 || !spike_stage_files_reserve(stages, &corpus, &kernels, &memory)))
            {
                fprintf(stderr, "can't reserve memory for the stages\n");
                result = 1;
            }
            if (result == 0 && !check_only)
            {
                SpikeBench bench = {
                    .corpus = &corpus,
                    .meanings = &meanings,
                    .tree = &tree,
                    .scratch = &scratch,
                    .kernels = &kernels,
                    .buffers = &buffers,
                    .stages = stages,
                    .typed = typed,
                    .statements = statements,
                    .wide_blocks = 0,
                };
                spike_benchmark(&bench, passes, only);
            }
        }
    }
    return result;
}
