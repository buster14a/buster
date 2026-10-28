// SPDX-License-Identifier: (Apache-2.0 WITH LLVM-exception) AND MIT
// Original material Copyright (c) 2026 David Gonzalez Martin.
// The corpus loader and the counter/timing/sort helpers adapted through c_lexer_benchmark.c retain MIT and the
// upstream notice: Copyright (c) 2023 Niles Salter. See THIRD_PARTY_NOTICES.md and LICENSES/Accelerated-Zig-Parser.txt.
//
// The token refinement benchmark: refines every file of a corpus of C (or C++) sources with refine_tokens and
// refine_tokens_scalar (c_refine.c), and lexes it with c_tokenize for scale, and reports per token what each costs.
//
// Usage: build/compiler benchmark-refinement [--cplusplus] [--trigraphs] [--after-scalar MS] [--only NAME] path...
//
// Paths are files or directories, searched for .c and .h files (with --cplusplus, C++ sources and headers, lexed as
// C++). Every file is lexed once and its tokens kept. The check refines every file with both paths, one intern table
// each, from the predefined names, in the corpus's order, and compares the values and leading-space words; it reports
// the share of identifiers found in the table (the others are added) and how often the vectorized path takes a scalar
// one (refine_benchmark_count_rare). Then, after a warm-up, BUSTER_REFINE_BENCHMARK_PASSES passes, each running every
// implementation once over the whole corpus in turn: the refinements in place over a fresh copy of the lexer's ends
// (made before the timing starts), each from an intern table reset to the predefined names, so that it fills up
// through the pass as a process that compiles the corpus fills it; c_tokenize; and c_tokenize then refine_tokens for
// each file, as the compiler runs them: what that costs more than c_tokenize alone is what refinement costs when the
// file's tokens are still in the caches, which a pass over the whole corpus's tokens (for a large corpus, from memory)
// doesn't show. It reports the medians of wall time and of user-space cycles and instructions (perf_event_open; zero
// when unavailable), per token and per identifier. --only NAME runs one implementation (for profiles, and for its passes
// back to back). --after-scalar MS runs the scalar path over the corpus for MS milliseconds before each timed pass of a
// vectorized one (refine_benchmark_after_scalar): the state a compiler runs its vector phases in, after lighter code
// that lets the clock boost, where a heavy AVX-512 loop runs under the 9700X's power cap (#199). Run it pinned to one CPU
// (taskset -c 2 ...). The exit status is 1 if a path can't be read or the paths refine a file differently.
//
// Layout, in this order: the corpus (CorpusFile, the loader), the check, the clock and counters, the passes and
// run_refinement_benchmark.

#include "host_isa.h"
#if BUSTER_HOST_VECTOR
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

#define BUSTER_ARRAY_LENGTH(x) (sizeof(x) / sizeof((x)[0]))

#include "buster/compiler/c_lexer.c"
#include "buster/compiler/lane.c"
#include "buster/compiler/c_intern.c"
#include "buster/compiler/c_refine.c"

#define BUSTER_REFINE_BENCHMARK_PASSES 21
#define BUSTER_REFINE_BENCHMARK_AFTER_SCALAR_MAX_MS 10000
#define BUSTER_REFINE_BENCHMARK_MEMORY (64ull << 30)
#define BUSTER_REFINE_BENCHMARK_MAX_FILES (1u << 20)
#define BUSTER_REFINE_BENCHMARK_MAX_DEPTH 64
#define BUSTER_REFINE_BENCHMARK_MAX_PATH 4096
// An extensionless file is a C++ header only if its first bytes have no NUL byte.
#define BUSTER_REFINE_BENCHMARK_TEXT_CHECK_BYTES 4096

// ---------------------------------------------------------------------------------------------------------------------
// The corpus

typedef struct CorpusFile CorpusFile;
struct CorpusFile
{
    const char* path;
    const uint8_t* bytes;
    uint64_t length;
    // The lexer's output, and the ends again: the refinements overwrite a working copy of them.
    uint8_t* kinds;
    uint32_t* starts;
    uint32_t* ends;
    uint32_t* pristine_ends;
    uint64_t* space;
    uint64_t count;
    uint64_t identifiers;
};

typedef struct Corpus Corpus;
struct Corpus
{
    Arena arena;
    CorpusFile* files;
    uint64_t file_count;
    uint64_t bytes;
    uint64_t tokens;
    uint64_t identifiers;
    uint32_t flags;
};

static bool refine_benchmark_ends_with(const char* name, const char* suffix)
{
    const uint64_t name_length = strlen(name);
    const uint64_t suffix_length = strlen(suffix);
    return name_length >= suffix_length && memcmp(name + name_length - suffix_length, suffix, suffix_length) == 0;
}

// 0: not a source; 1: a source; 2: a C++ header if it is text (an extensionless file with --cplusplus).
static uint32_t refine_benchmark_source_kind(const char* name, bool cplusplus)
{
    static const char* const cplusplus_extensions[] = { ".cpp", ".cc", ".cxx", ".c++", ".hpp", ".hh", ".hxx", ".h++", ".h", ".inc", ".def", ".ipp", ".tcc", ".tpp" };
    uint32_t result = 0;
    if (cplusplus)
    {
        for (uint64_t i = 0; i < BUSTER_ARRAY_LENGTH(cplusplus_extensions); i += 1)
        {
            result |= refine_benchmark_ends_with(name, cplusplus_extensions[i]);
        }
        result = result == 0 && strchr(name, '.') == 0 ? 2 : result;
    }
    else
    {
        result = refine_benchmark_ends_with(name, ".c") || refine_benchmark_ends_with(name, ".h");
    }
    return result;
}

// Reads a file into the corpus and lexes it. A file that can't be read is skipped; returns false if memory runs out
// or the lexer fails.
static bool refine_benchmark_add_file(Corpus* corpus, const char* path, bool only_text)
{
    bool result = true;
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    struct stat file_stats;
    if (fd >= 0 && fstat(fd, &file_stats) == 0 && S_ISREG(file_stats.st_mode) && corpus->file_count < BUSTER_REFINE_BENCHMARK_MAX_FILES)
    {
        const uint64_t length = (uint64_t)file_stats.st_size;
        const uint64_t mark = corpus->arena.length;
        uint8_t* bytes = arena_push(&corpus->arena, length, 64);
        uint64_t read_bytes = 0;
        ssize_t n = 1;
        while (bytes != 0 && n > 0 && read_bytes < length)
        {
            n = read(fd, bytes + read_bytes, length - read_bytes);
            read_bytes += n > 0 ? (uint64_t)n : 0;
        }
        const uint64_t checked = read_bytes < BUSTER_REFINE_BENCHMARK_TEXT_CHECK_BYTES ? read_bytes : BUSTER_REFINE_BENCHMARK_TEXT_CHECK_BYTES;
        if (bytes == 0)
        {
            result = false;
        }
        else if (only_text && (read_bytes == 0 || memchr(bytes, 0, checked) != 0))
        {
            arena_pop_to(&corpus->arena, mark);
        }
        else
        {
            const uint64_t capacity = BUSTER_C_TOKENIZER_CAPACITY(read_bytes);
            CorpusFile file = {
                .bytes = bytes,
                .length = read_bytes,
                .kinds = arena_push(&corpus->arena, capacity, 64),
                .starts = arena_push_array(&corpus->arena, capacity, sizeof(uint32_t), 64),
                .ends = arena_push_array(&corpus->arena, capacity, sizeof(uint32_t), 64),
                .pristine_ends = arena_push_array(&corpus->arena, capacity, sizeof(uint32_t), 64),
                .space = arena_push_array(&corpus->arena, capacity / 64 + 1, sizeof(uint64_t), 64),
            };
            char* path_copy = arena_push(&corpus->arena, strlen(path) + 1, 1);
            result = file.kinds != 0 && file.starts != 0 && file.ends != 0 && file.pristine_ends != 0 && file.space != 0 && path_copy != 0;
            const int64_t count = result ? c_tokenize(bytes, read_bytes, file.kinds, file.starts, file.pristine_ends, corpus->flags) : -1;
            result = count >= 0;
            if (result)
            {
                memcpy(path_copy, path, strlen(path) + 1);
                file.path = path_copy;
                file.count = (uint64_t)count;
                for (uint64_t i = 0; i < file.count; i += 1)
                {
                    file.identifiers += (file.kinds[i] & 0x7F) == C_TOKEN_RAW_IDENTIFIER;
                }
                corpus->files[corpus->file_count] = file;
                corpus->file_count += 1;
                corpus->bytes += read_bytes;
                corpus->tokens += file.count;
                corpus->identifiers += file.identifiers;
            }
        }
    }
    if (fd >= 0)
    {
        close(fd);
    }
    return result;
}

// Adds a file, or the sources under a directory (a stack of open directories; names starting with '.' and symbolic
// links are skipped).
static bool refine_benchmark_add_path(Corpus* corpus, const char* path)
{
    struct stat path_stats;
    bool result = stat(path, &path_stats) == 0;
    const bool cplusplus = (corpus->flags & C_TOKENIZER_CPLUSPLUS) != 0;
    if (!result)
    {
        fprintf(stderr, "can't open %s\n", path);
    }
    else if (!S_ISDIR(path_stats.st_mode))
    {
        result = refine_benchmark_add_file(corpus, path, false);
    }
    else
    {
        DIR* directories[BUSTER_REFINE_BENCHMARK_MAX_DEPTH];
        static char paths[BUSTER_REFINE_BENCHMARK_MAX_DEPTH][BUSTER_REFINE_BENCHMARK_MAX_PATH];
        uint32_t depth = 0;
        directories[0] = opendir(path);
        if (directories[0] != 0)
        {
            snprintf(paths[0], sizeof(paths[0]), "%s", path);
            depth = 1;
        }
        while (depth != 0)
        {
            struct dirent* entry = result ? readdir(directories[depth - 1]) : 0;
            if (entry == 0)
            {
                closedir(directories[depth - 1]);
                depth -= 1;
            }
            else if (entry->d_name[0] != '.')
            {
                char child[BUSTER_REFINE_BENCHMARK_MAX_PATH];
                snprintf(child, sizeof(child), "%s/%s", paths[depth - 1], entry->d_name);
                struct stat child_stats;
                if (lstat(child, &child_stats) == 0 && S_ISDIR(child_stats.st_mode) && depth < BUSTER_REFINE_BENCHMARK_MAX_DEPTH)
                {
                    directories[depth] = opendir(child);
                    if (directories[depth] != 0)
                    {
                        snprintf(paths[depth], sizeof(paths[depth]), "%s", child);
                        depth += 1;
                    }
                }
                else if (S_ISREG(child_stats.st_mode))
                {
                    const uint32_t kind = refine_benchmark_source_kind(entry->d_name, cplusplus);
                    if (kind != 0)
                    {
                        result = refine_benchmark_add_file(corpus, child, kind == 2);
                    }
                }
            }
        }
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The check

// The implementations: the variants of the vectorized path (RefineVariant; refine_tokens is REFINE_VARIANT_BATCH_64),
// the scalar path, the lexer, and the lexer followed by refine_tokens file by file, as the compiler runs them (each
// file's tokens are then still in the caches when they are refined).
#define BUSTER_REFINE_IMPLEMENTATION_SCALAR REFINE_VARIANT_COUNT
#define BUSTER_REFINE_IMPLEMENTATION_LEXER (REFINE_VARIANT_COUNT + 1)
#define BUSTER_REFINE_IMPLEMENTATION_PIPELINE (REFINE_VARIANT_COUNT + 2)
#define BUSTER_REFINE_IMPLEMENTATION_COUNT (REFINE_VARIANT_COUNT + 3)

static const char* refine_implementation_name(uint32_t implementation)
{
    const char* result = "c_tokenize";
    if (implementation < REFINE_VARIANT_COUNT)
    {
        result = refine_variant_names[implementation];
    }
    else if (implementation == BUSTER_REFINE_IMPLEMENTATION_SCALAR)
    {
        result = "refine_tokens_scalar";
    }
    else if (implementation == BUSTER_REFINE_IMPLEMENTATION_PIPELINE)
    {
        result = "c_tokenize+refine";
    }
    return result;
}

// How often refine_tokens leaves its vectorized path, counted from the lexer's output: identifiers longer than its
// masked load reads (BUSTER_INTERN_FAST_LENGTH bytes), identifiers with a backslash (a line splice or a UCN; with
// trigraphs, any '?'), which go to the scalar key, and tokens whose gap ends in a newline although they don't start a
// line (only a line splice can do that), whose leading space refine_gap_is_splices decides.
static void refine_benchmark_count_rare(const Corpus* corpus, uint64_t* long_identifiers, uint64_t* backslash_identifiers, uint64_t* splice_gaps)
{
    const bool trigraphs = (corpus->flags & C_TOKENIZER_TRIGRAPHS) != 0;
    for (uint64_t f = 0; f < corpus->file_count; f += 1)
    {
        const CorpusFile* file = &corpus->files[f];
        for (uint64_t i = 0; i < file->count; i += 1)
        {
            const uint32_t start = file->starts[i];
            const uint32_t end = file->pristine_ends[i];
            if ((file->kinds[i] & 0x7F) == C_TOKEN_RAW_IDENTIFIER)
            {
                bool backslash = false;
                for (uint32_t p = start; p < end; p += 1)
                {
                    backslash |= file->bytes[p] == '\\' || (trigraphs && file->bytes[p] == '?');
                }
                *long_identifiers += end - start > BUSTER_INTERN_FAST_LENGTH;
                *backslash_identifiers += backslash;
            }
            const bool start_of_line = (file->kinds[i] & BUSTER_C_TOKEN_START_OF_LINE) != 0;
            const uint32_t previous_end = i != 0 ? file->pristine_ends[i - 1] : 0;
            *splice_gaps += !start_of_line && start != previous_end && start != 0 && (file->bytes[start - 1] == '\n' || file->bytes[start - 1] == '\r');
        }
    }
}

// Refines every file with every variant of the vectorized path and with the scalar path, each with a table of its own,
// and compares them. Returns how many files differ.
static uint64_t refine_benchmark_check(Corpus* corpus, InternTable* tables)
{
    uint64_t different = 0;
    uint64_t largest = 0;
    for (uint64_t f = 0; f < corpus->file_count; f += 1)
    {
        largest = corpus->files[f].count > largest ? corpus->files[f].count : largest;
    }
    uint32_t* values = malloc((largest + 1) * sizeof(uint32_t));
    uint64_t* space = malloc((largest / 64 + 1) * sizeof(uint64_t));
    const bool allocated = values != 0 && space != 0;
    for (uint64_t f = 0; allocated && f < corpus->file_count; f += 1)
    {
        CorpusFile* file = &corpus->files[f];
        const uint64_t words = (file->count + 63) / 64;
        bool same = refine_tokens_scalar(&tables[BUSTER_REFINE_IMPLEMENTATION_SCALAR], file->bytes, file->length, file->kinds, file->starts, file->pristine_ends,
                                         file->count, values, space, corpus->flags);
        for (uint32_t variant = 0; variant < REFINE_VARIANT_COUNT; variant += 1)
        {
            memcpy(file->ends, file->pristine_ends, file->count * sizeof(uint32_t));
            same &= refine_tokens_variant((RefineVariant)variant, &tables[variant], file->bytes, file->length, file->kinds, file->starts, file->ends, file->count,
                                          file->ends, file->space, corpus->flags) &&
                    memcmp(values, file->ends, file->count * sizeof(uint32_t)) == 0 && memcmp(space, file->space, words * sizeof(uint64_t)) == 0;
        }
        if (!same && different < 10)
        {
            printf("%s: the paths refine it differently\n", file->path);
        }
        different += !same;
    }
    if (!allocated)
    {
        different = corpus->file_count;
    }
    free(values);
    free(space);
    // Every spelling first met is a miss; the predefined names are already in the table.
    const uint64_t added = tables[0].count - C_NAME_COUNT;
    uint64_t long_identifiers = 0;
    uint64_t backslash_identifiers = 0;
    uint64_t splice_gaps = 0;
    refine_benchmark_count_rare(corpus, &long_identifiers, &backslash_identifiers, &splice_gaps);
    printf("check: %lu of %lu files differ; %u ids in use, %lu added: %.2f%% of identifiers are found\n", (unsigned long)different,
           (unsigned long)corpus->file_count, tables[0].count, (unsigned long)added,
           corpus->identifiers != 0 ? 100.0 * (double)(corpus->identifiers - added) / (double)corpus->identifiers : 0.0);
    printf("scalar paths: %lu identifiers longer than %u bytes, %lu with a backslash%s, %lu tokens after a line splice\n", (unsigned long)long_identifiers,
           BUSTER_INTERN_FAST_LENGTH, (unsigned long)backslash_identifiers, (corpus->flags & C_TOKENIZER_TRIGRAPHS) ? " or a '?'" : "", (unsigned long)splice_gaps);
    return different;
}

// ---------------------------------------------------------------------------------------------------------------------
// The clock and the counters

typedef struct RefineSample RefineSample;
struct RefineSample
{
    uint64_t nanoseconds;
    uint64_t cycles;
    uint64_t instructions;
};

static int refine_benchmark_open_counter(uint64_t config)
{
    struct perf_event_attr attributes;
    memset(&attributes, 0, sizeof(attributes));
    attributes.size = sizeof(attributes);
    attributes.type = PERF_TYPE_HARDWARE;
    attributes.config = config;
    attributes.exclude_kernel = 1;
    attributes.exclude_hv = 1;
    return (int)syscall(SYS_perf_event_open, &attributes, 0, -1, -1, PERF_FLAG_FD_CLOEXEC);
}

static uint64_t refine_benchmark_read_counter(int fd)
{
    uint64_t value = 0;
    if (fd >= 0 && read(fd, &value, sizeof(value)) != sizeof(value))
    {
        value = 0;
    }
    return value;
}

static RefineSample refine_benchmark_now(int cycles_fd, int instructions_fd)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    RefineSample result = {
        .nanoseconds = (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec,
        .cycles = refine_benchmark_read_counter(cycles_fd),
        .instructions = refine_benchmark_read_counter(instructions_fd),
    };
    return result;
}

static void refine_benchmark_sort(uint64_t* values, uint64_t count)
{
    for (uint64_t i = 1; i < count; i += 1)
    {
        const uint64_t value = values[i];
        uint64_t j = i;
        while (j > 0 && values[j - 1] > value)
        {
            values[j] = values[j - 1];
            j -= 1;
        }
        values[j] = value;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// The passes

// --after-scalar: the scalar path over the corpus's files, again and again, for `milliseconds`, before a timed pass of a
// vectorized one. A compiler runs its vector phases in bursts after lighter ones, which let the clock boost, and at that
// clock a heavy vector loop runs under the core's power cap for 5 to 20 ms (#199; docs/x86-64-encoder.md, "Zen 5's
// costs, measured"). Back-to-back passes miss that state. It writes the working ends and words, which the pass restores.
static void refine_benchmark_after_scalar(Corpus* corpus, InternTable* table, uint64_t milliseconds)
{
    const uint64_t end = refine_benchmark_now(-1, -1).nanoseconds + milliseconds * 1000000;
    intern_reset(table);
    for (uint64_t f = 0; refine_benchmark_now(-1, -1).nanoseconds < end; f = f + 1 < corpus->file_count ? f + 1 : 0)
    {
        CorpusFile* file = &corpus->files[f];
        if (!refine_tokens_scalar(table, file->bytes, file->length, file->kinds, file->starts, file->pristine_ends, file->count, file->ends, file->space, corpus->flags))
        {
            intern_reset(table);
        }
    }
}

// One implementation over the whole corpus, after `after_scalar` ms of the scalar path for a vectorized one. The
// refinements' working ends are restored before the clock starts.
static RefineSample refine_benchmark_pass(Corpus* corpus, uint32_t implementation, InternTable* table, int cycles_fd, int instructions_fd, uint64_t after_scalar)
{
    if (after_scalar != 0 && (implementation < REFINE_VARIANT_COUNT || implementation == BUSTER_REFINE_IMPLEMENTATION_PIPELINE))
    {
        refine_benchmark_after_scalar(corpus, table, after_scalar);
    }
    if (implementation != BUSTER_REFINE_IMPLEMENTATION_LEXER)
    {
        for (uint64_t f = 0; f < corpus->file_count; f += 1)
        {
            memcpy(corpus->files[f].ends, corpus->files[f].pristine_ends, corpus->files[f].count * sizeof(uint32_t));
        }
        intern_reset(table);
    }
    bool fits = true;
    const RefineSample before = refine_benchmark_now(cycles_fd, instructions_fd);
    for (uint64_t f = 0; f < corpus->file_count; f += 1)
    {
        CorpusFile* file = &corpus->files[f];
        if (implementation < REFINE_VARIANT_COUNT)
        {
            fits &= refine_tokens_variant((RefineVariant)implementation, table, file->bytes, file->length, file->kinds, file->starts, file->ends, file->count,
                                          file->ends, file->space, corpus->flags);
        }
        else if (implementation == BUSTER_REFINE_IMPLEMENTATION_SCALAR)
        {
            fits &= refine_tokens_scalar(table, file->bytes, file->length, file->kinds, file->starts, file->ends, file->count, file->ends, file->space,
                                         corpus->flags);
        }
        else if (implementation == BUSTER_REFINE_IMPLEMENTATION_LEXER)
        {
            // Into the working ends, which the next refinement pass restores anyway.
            fits &= c_tokenize(file->bytes, file->length, file->kinds, file->starts, file->ends, corpus->flags) >= 0;
        }
        else
        {
            fits &= c_tokenize(file->bytes, file->length, file->kinds, file->starts, file->ends, corpus->flags) >= 0 &&
                    refine_tokens(table, file->bytes, file->length, file->kinds, file->starts, file->ends, file->count, file->ends, file->space, corpus->flags);
        }
    }
    const RefineSample after = refine_benchmark_now(cycles_fd, instructions_fd);
    if (!fits)
    {
        fprintf(stderr, "%s failed during a pass\n", refine_implementation_name(implementation));
    }
    return (RefineSample){
        .nanoseconds = after.nanoseconds - before.nanoseconds,
        .cycles = after.cycles - before.cycles,
        .instructions = after.instructions - before.instructions,
    };
}

static void refine_benchmark_run(Corpus* corpus, InternTable* table, uint32_t only, uint64_t after_scalar)
{
    const int cycles_fd = refine_benchmark_open_counter(PERF_COUNT_HW_CPU_CYCLES);
    const int instructions_fd = refine_benchmark_open_counter(PERF_COUNT_HW_INSTRUCTIONS);
    if (cycles_fd < 0 || instructions_fd < 0)
    {
        fprintf(stderr, "perf_event_open failed (see /proc/sys/kernel/perf_event_paranoid): cycles and instructions read as 0\n");
    }
    static RefineSample samples[BUSTER_REFINE_IMPLEMENTATION_COUNT][BUSTER_REFINE_BENCHMARK_PASSES];
    for (uint64_t pass = 0; pass <= BUSTER_REFINE_BENCHMARK_PASSES; pass += 1)
    {
        for (uint32_t implementation = 0; implementation < BUSTER_REFINE_IMPLEMENTATION_COUNT; implementation += 1)
        {
            const RefineSample sample = (only >> implementation) & 1 ? refine_benchmark_pass(corpus, implementation, table, cycles_fd, instructions_fd, after_scalar)
                                                                      : (RefineSample){ 0 };
            if (pass > 0)
            {
                samples[implementation][pass - 1] = sample;
            }
        }
    }
    char after_text[64] = "";
    if (after_scalar != 0)
    {
        snprintf(after_text, sizeof(after_text), ", the vectorized ones each after %lu ms of the scalar path", (unsigned long)after_scalar);
    }
    printf("%d interleaved passes over %lu tokens (%lu identifiers, %u distinct)%s, medians (cycles/token also min-max):\n", BUSTER_REFINE_BENCHMARK_PASSES,
           (unsigned long)corpus->tokens, (unsigned long)corpus->identifiers, table->count - 1, after_text);
    for (uint32_t implementation = 0; implementation < BUSTER_REFINE_IMPLEMENTATION_COUNT; implementation += 1)
    {
        if (((only >> implementation) & 1) == 0)
        {
            continue;
        }
        uint64_t nanoseconds[BUSTER_REFINE_BENCHMARK_PASSES];
        uint64_t cycles[BUSTER_REFINE_BENCHMARK_PASSES];
        uint64_t instructions[BUSTER_REFINE_BENCHMARK_PASSES];
        for (uint64_t pass = 0; pass < BUSTER_REFINE_BENCHMARK_PASSES; pass += 1)
        {
            nanoseconds[pass] = samples[implementation][pass].nanoseconds;
            cycles[pass] = samples[implementation][pass].cycles;
            instructions[pass] = samples[implementation][pass].instructions;
        }
        refine_benchmark_sort(nanoseconds, BUSTER_REFINE_BENCHMARK_PASSES);
        refine_benchmark_sort(cycles, BUSTER_REFINE_BENCHMARK_PASSES);
        refine_benchmark_sort(instructions, BUSTER_REFINE_BENCHMARK_PASSES);
        const double tokens = (double)corpus->tokens;
        const double median_nanoseconds = (double)nanoseconds[BUSTER_REFINE_BENCHMARK_PASSES / 2];
        const double median_cycles = (double)cycles[BUSTER_REFINE_BENCHMARK_PASSES / 2];
        const double median_instructions = (double)instructions[BUSTER_REFINE_BENCHMARK_PASSES / 2];
        printf("  %-20s %8.3f ms  %6.2f ns/token  %6.2f cycles/token [%.2f-%.2f]  %6.2f instructions/token  IPC %.2f  %6.2f cycles/identifier\n",
               refine_implementation_name(implementation), median_nanoseconds / 1e6, median_nanoseconds / tokens, median_cycles / tokens,
               (double)cycles[0] / tokens, (double)cycles[BUSTER_REFINE_BENCHMARK_PASSES - 1] / tokens, median_instructions / tokens,
               median_cycles != 0 ? median_instructions / median_cycles : 0.0, median_cycles / (double)corpus->identifiers);
    }
    if (cycles_fd >= 0)
    {
        close(cycles_fd);
    }
    if (instructions_fd >= 0)
    {
        close(instructions_fd);
    }
}

int run_refinement_benchmark(int argc, char** argv)
{
    int result = 1;
    static Corpus corpus;
    bool valid = true;
    uint64_t paths = 0;
    uint32_t only = (1u << BUSTER_REFINE_IMPLEMENTATION_COUNT) - 1;
    uint64_t after_scalar = 0;
    for (int i = 1; i < argc; i += 1)
    {
        if (strcmp(argv[i], "--after-scalar") == 0 && i + 1 < argc)
        {
            i += 1;
            char* end = 0;
            after_scalar = strtoull(argv[i], &end, 10);
            valid = valid && argv[i][0] >= '0' && argv[i][0] <= '9' && *end == 0 && after_scalar <= BUSTER_REFINE_BENCHMARK_AFTER_SCALAR_MAX_MS;
        }
        else if (strcmp(argv[i], "--only") == 0 && i + 1 < argc)
        {
            i += 1;
            only = 0;
            for (uint32_t implementation = 0; implementation < BUSTER_REFINE_IMPLEMENTATION_COUNT; implementation += 1)
            {
                only |= (uint32_t)(strcmp(argv[i], refine_implementation_name(implementation)) == 0) << implementation;
            }
            valid = valid && only != 0;
        }
        else if (strcmp(argv[i], "--cplusplus") == 0)
        {
            corpus.flags |= C_TOKENIZER_CPLUSPLUS;
        }
        else if (strcmp(argv[i], "--trigraphs") == 0)
        {
            corpus.flags |= C_TOKENIZER_TRIGRAPHS;
        }
        else if (argv[i][0] == '-')
        {
            valid = false;
        }
        else
        {
            paths += 1;
        }
    }
    static InternTable tables[BUSTER_REFINE_IMPLEMENTATION_COUNT];
    if (!valid || paths == 0)
    {
        fprintf(stderr, "usage: %s [--cplusplus] [--trigraphs] [--after-scalar MS] [--only NAME] path...\n", argv[0]);
    }
    else
    {
        corpus.arena = arena_reserve(BUSTER_REFINE_BENCHMARK_MEMORY);
        corpus.files = arena_push_array(&corpus.arena, BUSTER_REFINE_BENCHMARK_MAX_FILES, sizeof(CorpusFile), 64);
        bool loaded = corpus.files != 0;
        for (uint32_t implementation = 0; loaded && implementation < BUSTER_REFINE_IMPLEMENTATION_LEXER; implementation += 1)
        {
            loaded = intern_initialize(&tables[implementation]);
        }
        for (int i = 1; loaded && i < argc; i += 1)
        {
            if (strcmp(argv[i], "--only") == 0 || strcmp(argv[i], "--after-scalar") == 0)
            {
                i += 1;
            }
            else if (argv[i][0] != '-')
            {
                loaded = refine_benchmark_add_path(&corpus, argv[i]);
            }
        }
        if (!loaded)
        {
            fprintf(stderr, "can't load the corpus\n");
        }
        else if (corpus.tokens == 0)
        {
            fprintf(stderr, "the corpus has no tokens\n");
        }
        else
        {
            printf("%lu files, %lu bytes, %lu tokens, %lu identifiers\n", (unsigned long)corpus.file_count, (unsigned long)corpus.bytes,
                   (unsigned long)corpus.tokens, (unsigned long)corpus.identifiers);
            const uint64_t different = refine_benchmark_check(&corpus, tables);
            refine_benchmark_run(&corpus, &tables[0], only, after_scalar);
            result = different == 0 ? 0 : 1;
        }
    }
    return result;
}

#else
int run_refinement_benchmark(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    return 1;
}
#endif // BUSTER_HOST_VECTOR
