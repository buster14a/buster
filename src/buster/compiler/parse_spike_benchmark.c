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
// Loading

typedef struct SpikeFile SpikeFile;
struct SpikeFile
{
    char* path;
    uint8_t* source;
    uint64_t length;
    SpikeTokens tokens;
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
        const uint64_t path_length = strlen(path);
        SpikeFile* file = &corpus->files[corpus->file_count];
        file->path = arena_push(corpus->memory, path_length + 1, 1);
        file->source = arena_push(corpus->memory, length + 1, 64);
        uint64_t read_bytes = 0;
        ssize_t n = 1;
        while (file->source != 0 && n > 0 && read_bytes < length)
        {
            n = read(fd, file->source + read_bytes, length - read_bytes);
            read_bytes += n > 0 ? (uint64_t)n : 0;
        }
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
    if (fd >= 0)
    {
        close(fd);
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
    if (!valid || path_count == 0)
    {
        fprintf(stderr, "usage: %s [--dump FILE] path...\n", argv[0]);
    }
    else if (corpus.files == 0 || !spike_names_init(&corpus.names, &memory))
    {
        fprintf(stderr, "can't reserve memory\n");
    }
    else
    {
        bool loaded = true;
        for (int i = 1; loaded && i < argc; i += 1)
        {
            if (strcmp(argv[i], "--dump") == 0)
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
        if (loaded && (meanings.meanings == 0 || marks == 0 || typed == 0 || statements == 0 || !spike_allocate_tree(&memory, &tree, corpus.max_tokens) ||
                       !spike_kernels_reserve(&kernels, &memory, corpus.max_tokens, BUSTER_SPIKE_NAME_CAPACITY)))
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
            uint32_t twin_failures = 0;
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
                    else if (role_differences != 0 && role_reports < 12)
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
            result = failed == 0 && broken == 0 && twin_failures == 0 ? 0 : 1;
            if (result == 0 && !check_only)
            {
                SpikeCounters counters = {
                    .cycles = spike_open_counter(PERF_COUNT_HW_CPU_CYCLES),
                    .instructions = spike_open_counter(PERF_COUNT_HW_INSTRUCTIONS),
                };
                SpikeSample samples[BUSTER_SPIKE_PASSES];
                for (uint32_t pass = 0; pass <= BUSTER_SPIKE_PASSES; pass += 1)
                {
                    const SpikeSample before = spike_sample(&counters);
                    for (uint32_t i = 0; i < corpus.file_count; i += 1)
                    {
                        const SpikeFile* file = &corpus.files[i];
                        spike_parse_scalar(file->tokens.kinds, file->tokens.names, (uint32_t)file->tokens.count, &meanings, &tree, &scratch);
                    }
                    const SpikeSample after = spike_sample(&counters);
                    if (pass > 0)
                    {
                        samples[pass - 1] = spike_difference(before, after);
                    }
                }
                printf("%d passes over %lu tokens, medians:\n", BUSTER_SPIKE_PASSES, (unsigned long)corpus.token_count);
                spike_report("scalar parser", samples, BUSTER_SPIKE_PASSES, corpus.token_count);
            }
        }
    }
    return result;
}
