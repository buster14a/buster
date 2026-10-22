// SPDX-License-Identifier: (Apache-2.0 WITH LLVM-exception) AND MIT
// Original material Copyright (c) 2026 David Gonzalez Martin.
// Counter/timing/sort helpers adapted through c_lexer_benchmark.c retain MIT and the upstream notice:
// Copyright (c) 2023 Niles Salter. See THIRD_PARTY_NOTICES.md and LICENSES/Accelerated-Zig-Parser.txt.
//
// The preprocessor benchmark: preprocesses translation units from scratch, as `-E` does without printing, with the
// vectorized kernels and with their scalar twins, and reports per token what each costs.
//
// Usage: build/compiler benchmark-preprocessor [--clang] [--only vectorized|scalar] [-E's options] file...
//
// Each pass preprocesses every file once with each implementation in turn, each run from nothing: a new intern table
// and preprocessor, so that every file it includes is read, lexed, refined and given its directive table, as a
// compile of one translation unit does. After a warm-up pass come BUSTER_PP_BENCHMARK_PASSES passes; it reports the
// medians of wall time and of user-space cycles and instructions (perf_event_open; zero when unavailable), per input
// token (the tokens of every file the runs prepared) and per output token, each phase's share of the time and its median
// time (PPPhase: reading, lexing, refining and directive tables; the rest is the main loop with its directives), -E -P's
// throughput with its printing (to /dev/null, timed after each run: input bytes and output tokens per second), and what
// a run did (PPStatistics). With --clang it also times `clang -E -P` with the same options (but --predefines, which is
// ours) writing to /dev/null, a process per file, the median of as many runs, for scale. --history-cost and
// --guard-cost replace the scalar twins with the vectorized kernels without recording macro expansion history, or
// without skipping the inclusions of guarded files (#120), whose output must be the same. --only runs one
// implementation (for profiles). Run it pinned to one CPU (taskset -c 2 ...). The exit status is 1 if the arguments
// are invalid or a run fails.
//
// With --tables, the arguments are paths, and it times the directive tables alone (#118): every .c and .h file under
// them is read and prepared once, then each pass builds every file's table (pp_directive_table) by each kernel, whose
// tables must be the same, and it reports the medians per input token and per directive, and the scan's share.
//
// Layout: the clock and the counters, the passes, the directive tables alone, and run_preprocessor_benchmark.

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
#include <sys/wait.h>

#define BUSTER_ARRAY_LENGTH(x) (sizeof(x) / sizeof((x)[0]))

#include "buster/compiler/c_lexer.c"
#include "buster/compiler/lane.c"
#include "buster/compiler/c_intern.c"
#include "buster/compiler/c_refine.c"
#include "buster/compiler/c_preprocessor.c"

#define BUSTER_PP_BENCHMARK_PASSES 21
#define BUSTER_PP_BENCHMARK_IMPLEMENTATIONS 2

typedef struct PPBenchmarkSample PPBenchmarkSample;
struct PPBenchmarkSample
{
    PPStatistics statistics;
    uint64_t nanoseconds;
    uint64_t cycles;
    uint64_t instructions;
    // -E -P's printing of the output, after the run (to /dev/null), and the bytes of the files the runs read.
    uint64_t print_nanoseconds;
    uint64_t input_bytes;
    uint64_t ticks[PP_PHASE_COUNT + 1]; // The phases', and the whole runs'.
    uint64_t history_bytes;
    uint64_t history_max_bytes;
    uint64_t temporary_max_bytes;
    uint64_t expansions;
    uint64_t substitutions;
    uint64_t generated;
    uint64_t runs;
    // The output's kinds, flags and spellings (values, and lengths for the tokens whose values are locations), and with
    // its locations and values: --guard-cost must keep the first (a file entered rather than skipped takes locations)
    // and --history-cost the second.
    uint64_t token_digest;
    uint64_t digest;
};

static int pp_benchmark_open_counter(uint64_t config)
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

static uint64_t pp_benchmark_read_counter(int fd)
{
    uint64_t value = 0;
    if (fd >= 0 && read(fd, &value, sizeof(value)) != sizeof(value))
    {
        value = 0;
    }
    return value;
}

static uint64_t pp_benchmark_nanoseconds(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

static void pp_benchmark_sort(uint64_t* values, uint64_t count)
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

// One pass of one implementation (its options) over the files: each preprocessed from nothing. Adds the files' input
// and output tokens to `tokens` (the first time).
static bool pp_benchmark_pass(const PPOptions* run, char** files, int file_count, int cycles_fd, int instructions_fd, PPBenchmarkSample* sample, uint64_t* input_tokens,
                              uint64_t* output_tokens)
{
    static InternTable table;
    static Preprocessor pp;
    PPOptions options = *run;
    options.timing = true;
    bool result = true;
    *sample = (PPBenchmarkSample){ 0 };
    *input_tokens = 0;
    *output_tokens = 0;
    for (int f = 0; result && f < file_count; f += 1)
    {
        const uint64_t nanoseconds = pp_benchmark_nanoseconds();
        const uint64_t cycles = pp_benchmark_read_counter(cycles_fd);
        const uint64_t instructions = pp_benchmark_read_counter(instructions_fd);
        const uint64_t ticks = __rdtsc();
        result = intern_initialize(&table) && pp_initialize(&pp, &options, &table) && pp_run(&pp, files[f]);
        sample->ticks[PP_PHASE_COUNT] += __rdtsc() - ticks;
        sample->instructions += pp_benchmark_read_counter(instructions_fd) - instructions;
        sample->cycles += pp_benchmark_read_counter(cycles_fd) - cycles;
        sample->nanoseconds += pp_benchmark_nanoseconds() - nanoseconds;
        for (uint32_t phase = 0; phase < PP_PHASE_COUNT; phase += 1)
        {
            sample->ticks[phase] += pp.ticks[phase];
        }
        // Then -E -P's printing, timed apart (#124's throughput).
        static FILE* null_stream;
        null_stream = null_stream != 0 ? null_stream : fopen("/dev/null", "w");
        const uint64_t printing = pp_benchmark_nanoseconds();
        result = result && null_stream != 0 && pp_print(&pp, null_stream, false);
        sample->print_nanoseconds += pp_benchmark_nanoseconds() - printing;
        for (uint32_t d = 0; d < pp.data_count; d += 1)
        {
            sample->input_bytes += pp.datas[d].prepared ? pp.datas[d].length : 0;
        }
        *input_tokens += pp.input_tokens;
        *output_tokens += pp.output.count;
        const uint64_t history_bytes = (uint64_t)pp.history_count * sizeof(PPExpansion) + (uint64_t)pp.substitution_count * sizeof(PPSubstitution) +
                                       (uint64_t)pp.generated_count * sizeof(PPGenerated) + (uint64_t)pp.history_source_count * sizeof(PPSource) +
                                       (uint64_t)pp.history_run_count * sizeof(PPHistoryRun);
        const uint64_t temporary_bytes = ((uint64_t)pp.origin_peak[0] + pp.origin_peak[1] + pp.origin_peak[2]) * sizeof(uint32_t);
        sample->history_bytes += history_bytes;
        sample->history_max_bytes = history_bytes > sample->history_max_bytes ? history_bytes : sample->history_max_bytes;
        sample->temporary_max_bytes = temporary_bytes > sample->temporary_max_bytes ? temporary_bytes : sample->temporary_max_bytes;
        sample->expansions += pp.history_count;
        sample->substitutions += pp.substitution_count;
        sample->generated += pp.generated_count;
        sample->runs += pp.history_run_count;
        // Outside the timed interval: every history/control pass must preprocess identically, including locations.
        for (uint32_t i = 0; i < pp.output.count; i += 1)
        {
            sample->digest = (sample->digest ^ pp.output.locations[i]) * 1099511628211ull;
            sample->digest = (sample->digest ^ pp.output.values[i]) * 1099511628211ull;
            sample->digest = (sample->digest ^ pp.output.kinds[i] ^ ((uint64_t)pp.output.flags[i] << 8)) * 1099511628211ull;
            // An identifier's or keyword's value is its IdentId, a pragma end's nothing, and another token's its end's
            // location, of which the token's length doesn't depend on the locations.
            const uint8_t kind = pp.output.kinds[i];
            const bool named = kind == C_TOKEN_RAW_IDENTIFIER || (kind >= BUSTER_PP_KIND_KEYWORD && kind < BUSTER_PP_KIND_PRAGMA_END);
            const uint32_t value = named ? pp.output.values[i] : kind == BUSTER_PP_KIND_PRAGMA_END ? 0 : pp.output.values[i] - pp.output.locations[i];
            sample->token_digest = (sample->token_digest ^ value) * 1099511628211ull;
            sample->token_digest = (sample->token_digest ^ kind ^ ((uint64_t)pp.output.flags[i] << 8)) * 1099511628211ull;
        }
        const uint64_t* from = (const uint64_t*)&pp.statistics;
        uint64_t* to = (uint64_t*)&sample->statistics;
        const uint64_t longest = sample->statistics.longest_condition;
        for (uint32_t k = 0; k < sizeof(PPStatistics) / sizeof(uint64_t); k += 1)
        {
            to[k] += from[k];
        }
        sample->statistics.longest_condition = pp.statistics.longest_condition > longest ? pp.statistics.longest_condition : longest;
        pp_release(&pp);
        intern_release(&table);
    }
    return result;
}

// The wall time of `clang -E -P` with the options on each file, writing to /dev/null.
static uint64_t pp_benchmark_clang(char** arguments, int argument_count, char** files, int file_count)
{
    uint64_t result = 0;
    for (int f = 0; f < file_count; f += 1)
    {
        const uint64_t start = pp_benchmark_nanoseconds();
        const pid_t child = fork();
        if (child == 0)
        {
            char* argv[256];
            int n = 0;
            argv[n++] = "clang";
            for (int i = 0; i < argument_count && n < 245; i += 1)
            {
                argv[n++] = arguments[i];
            }
            argv[n++] = "-E";
            argv[n++] = "-P";
            argv[n++] = "-o";
            argv[n++] = "/dev/null";
            argv[n++] = files[f];
            argv[n] = 0;
            unsetenv("CPATH");
            unsetenv("C_INCLUDE_PATH");
            execvp("clang", argv);
            _exit(127);
        }
        int status = 0;
        waitpid(child, &status, 0);
        result += pp_benchmark_nanoseconds() - start;
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The directive tables alone (--tables)

#define BUSTER_PP_BENCHMARK_MAX_DEPTH 64
#define BUSTER_PP_BENCHMARK_MAX_PATH 4096
#define BUSTER_PP_BENCHMARK_MAX_FILES 65536

// The files whose tables --tables builds: each read once and prepared once by the vectorized kernels (pp_prepare_data),
// in one preprocessor whose arena then takes each pass's tables.
typedef struct PPBenchmarkTables PPBenchmarkTables;
struct PPBenchmarkTables
{
    InternTable table;
    Preprocessor pp;
    Arena bytes;
    PPFileData* datas;
    uint32_t count;
    uint64_t tokens;
    uint64_t directives;
};

static bool pp_benchmark_tables_file(PPBenchmarkTables* tables, const char* path)
{
    bool result = false;
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    struct stat file_stats;
    if (fd >= 0 && fstat(fd, &file_stats) == 0 && (uint64_t)file_stats.st_size < UINT32_MAX - 64 && tables->count < BUSTER_PP_BENCHMARK_MAX_FILES)
    {
        const uint64_t length = (uint64_t)file_stats.st_size;
        uint8_t* bytes = arena_push(&tables->bytes, length + 64, 64);
        uint64_t done = 0;
        for (ssize_t got = 1; bytes != 0 && done < length && got > 0; done += got > 0 ? (uint64_t)got : 0)
        {
            got = read(fd, bytes + done, length - done);
        }
        if (bytes != 0 && done == length)
        {
            memset(bytes + length, 0, 64);
            PPFileData* data = &tables->datas[tables->count];
            // A file of the unit's own, prepared into its arenas (no cache record).
            *data = (PPFileData){ .bytes = bytes, .length = (uint32_t)length, .record = BUSTER_PP_NONE };
            result = pp_prepare_data(&tables->pp, data, path);
            tables->count += result;
            tables->tokens += result ? data->token_count : 0;
            tables->directives += result ? data->directive_count : 0;
        }
    }
    if (fd >= 0)
    {
        close(fd);
    }
    return result;
}

// A file, or the .c and .h files under a directory (a stack of open directories, no recursion).
static bool pp_benchmark_tables_path(PPBenchmarkTables* tables, const char* path)
{
    struct stat path_stats;
    bool result = stat(path, &path_stats) == 0;
    if (result && !S_ISDIR(path_stats.st_mode))
    {
        result = pp_benchmark_tables_file(tables, path);
    }
    else if (result)
    {
        DIR* directories[BUSTER_PP_BENCHMARK_MAX_DEPTH];
        static char paths[BUSTER_PP_BENCHMARK_MAX_DEPTH][BUSTER_PP_BENCHMARK_MAX_PATH];
        directories[0] = opendir(path);
        snprintf(paths[0], sizeof(paths[0]), "%s", path);
        result = directories[0] != 0;
        uint32_t depth = result ? 1 : 0;
        while (depth != 0)
        {
            struct dirent* entry = readdir(directories[depth - 1]);
            if (entry == 0)
            {
                closedir(directories[depth - 1]);
                depth -= 1;
            }
            else if (entry->d_name[0] != '.')
            {
                char child[BUSTER_PP_BENCHMARK_MAX_PATH];
                snprintf(child, sizeof(child), "%s/%s", paths[depth - 1], entry->d_name);
                struct stat child_stats;
                const bool listed = lstat(child, &child_stats) == 0;
                const uint64_t name_length = strlen(entry->d_name);
                const bool source = name_length > 2 && entry->d_name[name_length - 2] == '.' && (entry->d_name[name_length - 1] == 'c' || entry->d_name[name_length - 1] == 'h');
                if (listed && S_ISDIR(child_stats.st_mode) && depth < BUSTER_PP_BENCHMARK_MAX_DEPTH)
                {
                    directories[depth] = opendir(child);
                    if (directories[depth] != 0)
                    {
                        snprintf(paths[depth], sizeof(paths[depth]), "%s", child);
                        depth += 1;
                    }
                }
                else if (listed && S_ISREG(child_stats.st_mode) && source)
                {
                    result &= pp_benchmark_tables_file(tables, child);
                }
            }
        }
    }
    return result;
}

// What a pass of one kernel measured: the whole tables, and the directive scan alone (both of its calls, as
// pp_directive_table makes them); and a digest of the tables, which must be the same for both kernels.
typedef struct PPBenchmarkTablesSample PPBenchmarkTablesSample;
struct PPBenchmarkTablesSample
{
    uint64_t nanoseconds;
    uint64_t cycles;
    uint64_t instructions;
    uint64_t scan_cycles;
    uint64_t digest;
};

static bool pp_benchmark_tables_pass(PPBenchmarkTables* tables, bool vectorized, int cycles_fd, int instructions_fd, PPBenchmarkTablesSample* sample)
{
    Preprocessor* pp = &tables->pp;
    pp->vectorized = vectorized;
    const uint64_t arena_length = pp->arena.length;
    bool result = true;
    *sample = (PPBenchmarkTablesSample){ .digest = 14695981039346656037ull };
    const uint64_t nanoseconds = pp_benchmark_nanoseconds();
    const uint64_t cycles = pp_benchmark_read_counter(cycles_fd);
    const uint64_t instructions = pp_benchmark_read_counter(instructions_fd);
    for (uint32_t f = 0; result && f < tables->count; f += 1)
    {
        result = pp_directive_table(pp->vectorized, &pp->arena, &tables->datas[f]);
    }
    sample->instructions = pp_benchmark_read_counter(instructions_fd) - instructions;
    sample->cycles = pp_benchmark_read_counter(cycles_fd) - cycles;
    sample->nanoseconds = pp_benchmark_nanoseconds() - nanoseconds;
    // Outside the time: the tables' digest, then the scan alone.
    for (uint32_t f = 0; result && f < tables->count; f += 1)
    {
        const PPFileData* data = &tables->datas[f];
        for (uint32_t d = 0; d < data->directive_count; d += 1)
        {
            const uint64_t entry = ((uint64_t)data->directive_hash[d] << 32 | data->directive_end[d]) ^ ((uint64_t)data->directive_next[d] << 8 | data->directive_kind[d]);
            sample->digest = (sample->digest ^ entry) * 1099511628211ull;
        }
        sample->digest = (sample->digest ^ ((uint64_t)data->guard << 32 | data->note_count)) * 1099511628211ull;
    }
    uint32_t largest = 0;
    for (uint32_t f = 0; f < tables->count; f += 1)
    {
        largest = tables->datas[f].token_count > largest ? tables->datas[f].token_count : largest;
    }
    Scratch scratch = scratch_begin(0);
    uint64_t* lines = arena_push_array(scratch.arena, (uint64_t)largest / 64 + 1, sizeof(uint64_t), 64);
    uint32_t* hashes = arena_push_array(scratch.arena, (uint64_t)largest + 1, sizeof(uint32_t), 64);
    result = result && lines != 0 && hashes != 0;
    const uint64_t scan_cycles = pp_benchmark_read_counter(cycles_fd);
    for (uint32_t f = 0; result && f < tables->count; f += 1)
    {
        const PPFileData* data = &tables->datas[f];
        if (vectorized)
        {
            pp_scan_directives_vector(data->kinds, data->token_count, 0, lines);
            pp_scan_directives_vector(data->kinds, data->token_count, hashes, lines);
        }
        else
        {
            pp_scan_directives_scalar(data->kinds, data->token_count, 0, lines);
            pp_scan_directives_scalar(data->kinds, data->token_count, hashes, lines);
        }
    }
    sample->scan_cycles = pp_benchmark_read_counter(cycles_fd) - scan_cycles;
    scratch_end(scratch);
    arena_pop_to(&pp->arena, arena_length);
    return result;
}

// --tables PATH...: builds the directive table of every .c and .h file under the paths, by each kernel in turn, 21
// passes after a warm-up, and reports the medians per input token and per directive (#118).
static int pp_benchmark_tables(char** paths, int path_count, uint32_t only)
{
    static PPBenchmarkTables tables;
    const PPOptions options = { .language = { .standard = 2017, .gnu = true } };
    tables.bytes = arena_reserve(1ull << 36);
    tables.datas = calloc(BUSTER_PP_BENCHMARK_MAX_FILES, sizeof(PPFileData));
    bool ran = tables.bytes.base != 0 && tables.datas != 0 && intern_initialize(&tables.table) && pp_initialize(&tables.pp, &options, &tables.table);
    for (int i = 0; ran && i < path_count; i += 1)
    {
        ran = pp_benchmark_tables_path(&tables, paths[i]);
    }
    const int cycles_fd = pp_benchmark_open_counter(PERF_COUNT_HW_CPU_CYCLES);
    const int instructions_fd = pp_benchmark_open_counter(PERF_COUNT_HW_INSTRUCTIONS);
    static PPBenchmarkTablesSample samples[BUSTER_PP_BENCHMARK_IMPLEMENTATIONS][BUSTER_PP_BENCHMARK_PASSES];
    uint64_t digest = 0;
    bool have_digest = false;
    for (uint32_t pass = 0; ran && pass <= BUSTER_PP_BENCHMARK_PASSES; pass += 1)
    {
        for (uint32_t order = 0; ran && order < BUSTER_PP_BENCHMARK_IMPLEMENTATIONS; order += 1)
        {
            const uint32_t implementation = order ^ (pass & 1);
            PPBenchmarkTablesSample sample;
            if (((only >> implementation) & 1) != 0)
            {
                ran = pp_benchmark_tables_pass(&tables, implementation == 0, cycles_fd, instructions_fd, &sample);
                ran = ran && (!have_digest || sample.digest == digest);
                digest = sample.digest;
                have_digest = true;
                if (pass > 0)
                {
                    samples[implementation][pass - 1] = sample;
                }
            }
        }
    }
    if (!ran)
    {
        fprintf(stderr, "a file can't be read or prepared, or the kernels' tables differ\n");
    }
    else
    {
        static const char* const names[BUSTER_PP_BENCHMARK_IMPLEMENTATIONS] = { "vectorized", "scalar" };
        printf("directive tables: %u files, %lu input tokens, %lu directives (one per %.1f tokens); %d passes, medians:\n", tables.count, (unsigned long)tables.tokens,
               (unsigned long)tables.directives, tables.directives != 0 ? (double)tables.tokens / (double)tables.directives : 0.0, BUSTER_PP_BENCHMARK_PASSES);
        for (uint32_t implementation = 0; implementation < BUSTER_PP_BENCHMARK_IMPLEMENTATIONS; implementation += 1)
        {
            uint64_t nanoseconds[BUSTER_PP_BENCHMARK_PASSES];
            uint64_t cycles[BUSTER_PP_BENCHMARK_PASSES];
            uint64_t instructions[BUSTER_PP_BENCHMARK_PASSES];
            uint64_t scan[BUSTER_PP_BENCHMARK_PASSES];
            for (uint32_t pass = 0; pass < BUSTER_PP_BENCHMARK_PASSES; pass += 1)
            {
                nanoseconds[pass] = samples[implementation][pass].nanoseconds;
                cycles[pass] = samples[implementation][pass].cycles;
                instructions[pass] = samples[implementation][pass].instructions;
                scan[pass] = samples[implementation][pass].scan_cycles;
            }
            pp_benchmark_sort(nanoseconds, BUSTER_PP_BENCHMARK_PASSES);
            pp_benchmark_sort(cycles, BUSTER_PP_BENCHMARK_PASSES);
            pp_benchmark_sort(instructions, BUSTER_PP_BENCHMARK_PASSES);
            pp_benchmark_sort(scan, BUSTER_PP_BENCHMARK_PASSES);
            const double median_cycles = (double)cycles[BUSTER_PP_BENCHMARK_PASSES / 2];
            const double median_instructions = (double)instructions[BUSTER_PP_BENCHMARK_PASSES / 2];
            const double tokens = tables.tokens != 0 ? (double)tables.tokens : 1.0;
            const double directives = tables.directives != 0 ? (double)tables.directives : 1.0;
            if (((only >> implementation) & 1) != 0)
            {
                printf("  %-10s %8.3f ms  %5.3f cycles/token  %6.2f cycles/directive  %5.3f instructions/token  IPC %.2f  (the scan %5.3f cycles/token)\n",
                       names[implementation], (double)nanoseconds[BUSTER_PP_BENCHMARK_PASSES / 2] / 1e6, median_cycles / tokens, median_cycles / directives,
                       median_instructions / tokens, median_cycles != 0 ? median_instructions / median_cycles : 0.0,
                       (double)scan[BUSTER_PP_BENCHMARK_PASSES / 2] / tokens);
            }
        }
    }
    if (cycles_fd >= 0)
    {
        close(cycles_fd);
    }
    if (instructions_fd >= 0)
    {
        close(instructions_fd);
    }
    pp_release(&tables.pp);
    intern_release(&tables.table);
    arena_release(&tables.bytes);
    free(tables.datas);
    return ran ? 0 : 1;
}

// ---------------------------------------------------------------------------------------------------------------------
// Definitions and lookups alone (--defines)

// What a pass of one kernel measured (#121), file by file: its #defines defined in order (pp_define), its macros so
// defined compiled (pp_compile_macro), and its identifiers looked up as the main loop's test looks one up
// (pp_is_macro), each in user-space cycles; the counts, and a digest of the macros, which both kernels must make alike.
typedef struct PPBenchmarkDefinesSample PPBenchmarkDefinesSample;
struct PPBenchmarkDefinesSample
{
    uint64_t define_cycles;
    uint64_t compile_cycles;
    uint64_t lookup_cycles;
    uint64_t defines;
    uint64_t macros;
    uint64_t lookups;
    uint64_t found;
    uint64_t digest;
};

static bool pp_benchmark_defines_pass(PPBenchmarkTables* tables, uint32_t first_data, bool vectorized, int cycles_fd, PPBenchmarkDefinesSample* sample)
{
    Preprocessor* pp = &tables->pp;
    pp->vectorized = vectorized;
    const uint32_t macros = pp->macro_count;
    const uint32_t items = pp->item_count;
    const uint32_t parameters = pp->parameter_count;
    *sample = (PPBenchmarkDefinesSample){ .digest = 14695981039346656037ull };
    bool result = true;
    // A file at a time, as a translation unit defines its macros: its #defines, then its macros compiled, then its
    // identifiers looked up; then, outside the time, its definitions taken back.
    for (uint32_t f = 0; f < tables->count; f += 1)
    {
        const PPFileData* data = &pp->datas[first_data + f];
        uint64_t before = pp_benchmark_read_counter(cycles_fd);
        for (uint32_t d = 0; d < data->directive_count; d += 1)
        {
            if (data->directive_kind[d] == PP_DIRECTIVE_DEFINE)
            {
                pp_define(pp, first_data + f, 0, data->directive_hash[d] + 2, data->directive_end[d]);
                sample->defines += 1;
            }
        }
        uint64_t now = pp_benchmark_read_counter(cycles_fd);
        sample->define_cycles += now - before;
        before = now;
        for (uint32_t id = macros; id < pp->macro_count; id += 1)
        {
            pp_compile_macro(pp, id);
        }
        now = pp_benchmark_read_counter(cycles_fd);
        sample->compile_cycles += now - before;
        before = now;
        // As the main loop's runs test them: the identifiers of 64 kinds at a time from one compare, then each one's bit.
        uint64_t found = 0;
        uint64_t lookups = 0;
        for (uint32_t t = 0; t < data->token_count; t += 64)
        {
            const __mmask64 lanes = _bzhi_u64(~0ull, data->token_count - t < 64 ? data->token_count - t : 64);
            const __m512i kind = _mm512_and_si512(_mm512_maskz_loadu_epi8(lanes, data->kinds + t), _mm512_set1_epi8(0x7F));
            for (uint64_t identifiers = _mm512_mask_cmpeq_epi8_mask(lanes, kind, _mm512_set1_epi8(C_TOKEN_RAW_IDENTIFIER)); identifiers != 0; identifiers &= identifiers - 1)
            {
                found += pp_is_macro(pp, data->values[t + (uint32_t)__builtin_ctzll(identifiers)]);
                lookups += 1;
            }
        }
        sample->lookup_cycles += pp_benchmark_read_counter(cycles_fd) - before;
        sample->found += found;
        sample->lookups += lookups;
        sample->macros += pp->macro_count - macros;
        for (uint32_t id = macros; id < pp->macro_count; id += 1)
        {
            const PPMacro* macro = &pp->macros[id];
            sample->digest = (sample->digest ^ ((uint64_t)macro->name << 32 | macro->body_begin)) * 1099511628211ull;
            sample->digest = (sample->digest ^ ((uint64_t)macro->flags << 32 | macro->parameter_count)) * 1099511628211ull;
        }
        for (uint32_t id = pp->macro_count; id > macros; id -= 1)
        {
            pp_set_macro(pp, pp->macros[id - 1].name, 0);
        }
        pp->macro_count = macros;
        pp->item_count = items;
        pp->parameter_count = parameters;
    }
    return result;
}

// --defines PATH...: each .c and .h file under the paths defines its #defines, compiles its macros and looks up its
// identifiers, by each kernel in turn, 21 passes after a warm-up, and it reports the medians per #define, per macro
// compiled and per lookup (#121).
static int pp_benchmark_defines(char** paths, int path_count, uint32_t only)
{
    static PPBenchmarkTables tables;
    FILE* null_stream = fopen("/dev/null", "w");
    // Diagnostics (the corpus's redefinitions, as its files are defined one after another) go nowhere.
    const PPOptions options = { .language = { .standard = 2017, .gnu = true }, .diagnostics = null_stream };
    tables.bytes = arena_reserve(1ull << 36);
    tables.datas = calloc(BUSTER_PP_BENCHMARK_MAX_FILES, sizeof(PPFileData));
    bool ran = null_stream != 0 && tables.bytes.base != 0 && tables.datas != 0 && intern_initialize(&tables.table) && pp_initialize(&tables.pp, &options, &tables.table);
    for (int i = 0; ran && i < path_count; i += 1)
    {
        ran = pp_benchmark_tables_path(&tables, paths[i]);
    }
    // The files become the preprocessor's file data, which definitions point into.
    const uint32_t first_data = tables.pp.data_count;
    ran = ran && first_data + tables.count <= BUSTER_PP_MAX_FILES;
    for (uint32_t f = 0; ran && f < tables.count; f += 1)
    {
        tables.pp.datas[first_data + f] = tables.datas[f];
    }
    tables.pp.data_count += ran ? tables.count : 0;
    const int cycles_fd = pp_benchmark_open_counter(PERF_COUNT_HW_CPU_CYCLES);
    static PPBenchmarkDefinesSample samples[BUSTER_PP_BENCHMARK_IMPLEMENTATIONS][BUSTER_PP_BENCHMARK_PASSES];
    uint64_t digest = 0;
    bool have_digest = false;
    for (uint32_t pass = 0; ran && pass <= BUSTER_PP_BENCHMARK_PASSES; pass += 1)
    {
        for (uint32_t order = 0; ran && order < BUSTER_PP_BENCHMARK_IMPLEMENTATIONS; order += 1)
        {
            const uint32_t implementation = order ^ (pass & 1);
            PPBenchmarkDefinesSample sample;
            if (((only >> implementation) & 1) != 0)
            {
                ran = pp_benchmark_defines_pass(&tables, first_data, implementation == 0, cycles_fd, &sample);
                ran = ran && (!have_digest || sample.digest == digest);
                digest = sample.digest;
                have_digest = true;
                if (pass > 0)
                {
                    samples[implementation][pass - 1] = sample;
                }
            }
        }
    }
    if (!ran)
    {
        fprintf(stderr, "a file can't be read or prepared, or the kernels' macros differ\n");
    }
    else
    {
        static const char* const names[BUSTER_PP_BENCHMARK_IMPLEMENTATIONS] = { "vectorized", "scalar" };
        const PPBenchmarkDefinesSample* counts = &samples[only == 2 ? 1 : 0][0];
        printf("definitions: %u files, %lu #define directives, %lu macros defined, %lu identifiers looked up (%.1f%% macros); %d passes, medians:\n",
               tables.count, (unsigned long)counts->defines, (unsigned long)counts->macros, (unsigned long)counts->lookups,
               counts->lookups != 0 ? 100.0 * (double)counts->found / (double)counts->lookups : 0.0, BUSTER_PP_BENCHMARK_PASSES);
        for (uint32_t implementation = 0; implementation < BUSTER_PP_BENCHMARK_IMPLEMENTATIONS; implementation += 1)
        {
            uint64_t defines[BUSTER_PP_BENCHMARK_PASSES];
            uint64_t compiles[BUSTER_PP_BENCHMARK_PASSES];
            uint64_t lookups[BUSTER_PP_BENCHMARK_PASSES];
            for (uint32_t pass = 0; pass < BUSTER_PP_BENCHMARK_PASSES; pass += 1)
            {
                defines[pass] = samples[implementation][pass].define_cycles;
                compiles[pass] = samples[implementation][pass].compile_cycles;
                lookups[pass] = samples[implementation][pass].lookup_cycles;
            }
            pp_benchmark_sort(defines, BUSTER_PP_BENCHMARK_PASSES);
            pp_benchmark_sort(compiles, BUSTER_PP_BENCHMARK_PASSES);
            pp_benchmark_sort(lookups, BUSTER_PP_BENCHMARK_PASSES);
            if (((only >> implementation) & 1) != 0)
            {
                printf("  %-10s %7.1f cycles per #define  %7.1f cycles per macro compiled  %5.2f cycles per lookup\n", names[implementation],
                       (double)defines[BUSTER_PP_BENCHMARK_PASSES / 2] / (double)(counts->defines != 0 ? counts->defines : 1),
                       (double)compiles[BUSTER_PP_BENCHMARK_PASSES / 2] / (double)(counts->macros != 0 ? counts->macros : 1),
                       (double)lookups[BUSTER_PP_BENCHMARK_PASSES / 2] / (double)(counts->lookups != 0 ? counts->lookups : 1));
            }
        }
    }
    if (cycles_fd >= 0)
    {
        close(cycles_fd);
    }
    // The files' data belong to the benchmark's arena, not the preprocessor.
    tables.pp.data_count = first_data;
    pp_release(&tables.pp);
    intern_release(&tables.table);
    arena_release(&tables.bytes);
    free(tables.datas);
    if (null_stream != 0)
    {
        fclose(null_stream);
    }
    return ran ? 0 : 1;
}

int run_preprocessor_benchmark(int argc, char** argv)
{
    int result = 1;
    bool with_clang = false;
    bool history_cost = false;
    bool guard_cost = false;
    bool tables = false;
    bool defines = false;
    uint32_t only = 3;
    char** passed = calloc((size_t)argc + 1, sizeof(char*));
    char** clang_passed = calloc((size_t)argc + 1, sizeof(char*));
    char** files = calloc((size_t)argc + 1, sizeof(char*));
    int passed_count = 0;
    int clang_passed_count = 0;
    int file_count = 0;
    for (int i = 1; passed != 0 && clang_passed != 0 && files != 0 && i < argc; i += 1)
    {
        const char* argument = argv[i];
        // --predefines (clang's predefines for the options, from `clang -dM -E`) and --location-limit are ours alone, with
        // their values (pp_own_option_with_value).
        const bool ours = pp_own_option_with_value(argument);
        const bool takes_value = ours || strcmp(argument, "-I") == 0 || strcmp(argument, "-D") == 0 || strcmp(argument, "-U") == 0 || strcmp(argument, "-isystem") == 0 ||
                                 strcmp(argument, "-iquote") == 0 || strcmp(argument, "-idirafter") == 0 || strcmp(argument, "-include") == 0;
        if (strcmp(argument, "--clang") == 0)
        {
            with_clang = true;
        }
        else if (strcmp(argument, "--history-cost") == 0)
        {
            history_cost = true;
        }
        else if (strcmp(argument, "--tables") == 0)
        {
            tables = true;
        }
        else if (strcmp(argument, "--guard-cost") == 0)
        {
            guard_cost = true;
        }
        else if (strcmp(argument, "--defines") == 0)
        {
            defines = true;
        }
        else if (strcmp(argument, "--only") == 0 && i + 1 < argc)
        {
            i += 1;
            only = strcmp(argv[i], "vectorized") == 0 ? 1 : strcmp(argv[i], "scalar") == 0 ? 2 : 0;
        }
        else if (argument[0] == '-')
        {
            passed[passed_count++] = argv[i];
            if (!ours)
            {
                clang_passed[clang_passed_count++] = argv[i];
            }
            if (takes_value && i + 1 < argc)
            {
                i += 1;
                passed[passed_count++] = argv[i];
                if (!ours)
                {
                    clang_passed[clang_passed_count++] = argv[i];
                }
            }
        }
        else
        {
            files[file_count++] = argv[i];
        }
    }
    if (tables && file_count != 0 && only != 0)
    {
        result = pp_benchmark_tables(files, file_count, only);
        file_count = 0;
    }
    else if (defines && file_count != 0 && only != 0)
    {
        result = pp_benchmark_defines(files, file_count, only);
        file_count = 0;
    }
    Arena arena = arena_reserve(1ull << 30);
    PPOptions options;
    const char* input;
    const char* output;
    bool markers;
    char** with_input = calloc((size_t)passed_count + 2, sizeof(char*));
    if (with_input != 0 && file_count != 0)
    {
        memcpy(with_input, passed, (size_t)passed_count * sizeof(char*));
        with_input[passed_count] = files[0];
    }
    if (tables || defines)
    {
        // --tables or --defines ran above.
    }
    else if (file_count == 0 || with_input == 0 || only == 0 || !pp_parse_options(passed_count + 1, with_input, &options, &arena, &input, &output, &markers))
    {
        fprintf(stderr, "usage: %s [--clang] [--history-cost | --guard-cost] [--only vectorized|scalar] [-E's options] file...\n"
                        "       %s --tables|--defines [--only vectorized|scalar] path...\n", argv[0], argv[0]);
    }
    else
    {
        const int cycles_fd = pp_benchmark_open_counter(PERF_COUNT_HW_CPU_CYCLES);
        const int instructions_fd = pp_benchmark_open_counter(PERF_COUNT_HW_INSTRUCTIONS);
        if (cycles_fd < 0 || instructions_fd < 0)
        {
            fprintf(stderr, "perf_event_open failed (see /proc/sys/kernel/perf_event_paranoid): cycles and instructions read as 0\n");
        }
        static PPBenchmarkSample samples[BUSTER_PP_BENCHMARK_IMPLEMENTATIONS][BUSTER_PP_BENCHMARK_PASSES];
        uint64_t clang_times[BUSTER_PP_BENCHMARK_PASSES];
        uint64_t input_tokens = 0;
        uint64_t output_tokens = 0;
        bool ran = true;
        uint64_t expected_digest = 0;
        uint64_t expected_token_digest = 0;
        bool have_digest = false;
        for (uint32_t pass = 0; ran && pass <= BUSTER_PP_BENCHMARK_PASSES; pass += 1)
        {
            for (uint32_t order = 0; ran && order < BUSTER_PP_BENCHMARK_IMPLEMENTATIONS; order += 1)
            {
                const uint32_t implementation = order ^ (pass & 1);
                if (((only >> implementation) & 1) == 0)
                {
                    continue;
                }
                // The second implementation: the scalar twins, or with --history-cost and --guard-cost the vectorized
                // kernels without recording history or without skipping guarded files.
                PPBenchmarkSample sample;
                PPOptions run = options;
                const bool control = history_cost || guard_cost;
                run.scalar = !control && implementation == 1;
                run.no_history = history_cost && implementation == 1;
                run.no_guard_skip = guard_cost && implementation == 1;
                ran = pp_benchmark_pass(&run, files, file_count, cycles_fd, instructions_fd, &sample, &input_tokens, &output_tokens);
                if (!have_digest)
                {
                    expected_digest = sample.digest;
                    expected_token_digest = sample.token_digest;
                    have_digest = true;
                }
                else if ((history_cost && sample.digest != expected_digest) || (guard_cost && sample.token_digest != expected_token_digest))
                {
                    fprintf(stderr, "the control changed the output token digest\n");
                    ran = false;
                }
                if (pass > 0)
                {
                    samples[implementation][pass - 1] = sample;
                }
            }
            if (with_clang && pass > 0)
            {
                clang_times[pass - 1] = pp_benchmark_clang(clang_passed, clang_passed_count, files, file_count);
            }
        }
        if (!ran)
        {
            fprintf(stderr, "a run failed\n");
        }
        else
        {
            static const char* const names[BUSTER_PP_BENCHMARK_IMPLEMENTATIONS] = { "vectorized", "scalar" };
            static const char* const phase_names[PP_PHASE_COUNT + 1] = { "read", "lex", "refine", "directives", "main loop" };
            printf("%d files, %lu input tokens, %lu output tokens; %d passes, medians:\n", file_count, (unsigned long)input_tokens, (unsigned long)output_tokens,
                   BUSTER_PP_BENCHMARK_PASSES);
            for (uint32_t implementation = 0; implementation < BUSTER_PP_BENCHMARK_IMPLEMENTATIONS; implementation += 1)
            {
                if (((only >> implementation) & 1) == 0)
                {
                    continue;
                }
                uint64_t nanoseconds[BUSTER_PP_BENCHMARK_PASSES];
                uint64_t cycles[BUSTER_PP_BENCHMARK_PASSES];
                uint64_t instructions[BUSTER_PP_BENCHMARK_PASSES];
                uint64_t with_printing[BUSTER_PP_BENCHMARK_PASSES];
                uint64_t printing[BUSTER_PP_BENCHMARK_PASSES];
                uint64_t phases[PP_PHASE_COUNT + 1][BUSTER_PP_BENCHMARK_PASSES];
                for (uint32_t pass = 0; pass < BUSTER_PP_BENCHMARK_PASSES; pass += 1)
                {
                    const PPBenchmarkSample* sample = &samples[implementation][pass];
                    nanoseconds[pass] = sample->nanoseconds;
                    cycles[pass] = sample->cycles;
                    instructions[pass] = sample->instructions;
                    with_printing[pass] = sample->nanoseconds + sample->print_nanoseconds;
                    printing[pass] = sample->print_nanoseconds;
                    uint64_t phase_sum = 0;
                    for (uint32_t phase = 0; phase < PP_PHASE_COUNT; phase += 1)
                    {
                        phases[phase][pass] = sample->ticks[phase];
                        phase_sum += sample->ticks[phase];
                    }
                    phases[PP_PHASE_COUNT][pass] = sample->ticks[PP_PHASE_COUNT] > phase_sum ? sample->ticks[PP_PHASE_COUNT] - phase_sum : 0;
                }
                pp_benchmark_sort(nanoseconds, BUSTER_PP_BENCHMARK_PASSES);
                pp_benchmark_sort(cycles, BUSTER_PP_BENCHMARK_PASSES);
                pp_benchmark_sort(instructions, BUSTER_PP_BENCHMARK_PASSES);
                const double median_cycles = (double)cycles[BUSTER_PP_BENCHMARK_PASSES / 2];
                const double median_instructions = (double)instructions[BUSTER_PP_BENCHMARK_PASSES / 2];
                const char* name = history_cost ? (implementation == 0 ? "history" : "no-history")
                                   : guard_cost ? (implementation == 0 ? "guards" : "no-guards")
                                                : names[implementation];
                printf("  %-10s %8.3f ms  %6.2f cycles/input token  %6.2f cycles/output token  %6.2f instructions/input token  IPC %.2f\n", name,
                       (double)nanoseconds[BUSTER_PP_BENCHMARK_PASSES / 2] / 1e6, median_cycles / (double)input_tokens, median_cycles / (double)output_tokens,
                       median_instructions / (double)input_tokens, median_cycles != 0 ? median_instructions / median_cycles : 0.0);
                uint64_t total = 0;
                uint64_t medians[PP_PHASE_COUNT + 1];
                // The phases are timed with the time stamp counter: its ticks per nanosecond, over the passes' whole runs.
                uint64_t run_ticks = 0;
                uint64_t run_nanoseconds = 0;
                for (uint32_t pass = 0; pass < BUSTER_PP_BENCHMARK_PASSES; pass += 1)
                {
                    run_ticks += samples[implementation][pass].ticks[PP_PHASE_COUNT];
                    run_nanoseconds += samples[implementation][pass].nanoseconds;
                }
                const double milliseconds_per_tick = run_ticks != 0 ? (double)run_nanoseconds / (double)run_ticks / 1e6 : 0.0;
                for (uint32_t phase = 0; phase <= PP_PHASE_COUNT; phase += 1)
                {
                    pp_benchmark_sort(phases[phase], BUSTER_PP_BENCHMARK_PASSES);
                    medians[phase] = phases[phase][BUSTER_PP_BENCHMARK_PASSES / 2];
                    total += medians[phase];
                }
                printf("             ");
                for (uint32_t phase = 0; phase <= PP_PHASE_COUNT; phase += 1)
                {
                    printf("  %s %.1f%%", phase_names[phase], total != 0 ? 100.0 * (double)medians[phase] / (double)total : 0.0);
                }
                printf("\n             ");
                for (uint32_t phase = 0; phase <= PP_PHASE_COUNT; phase += 1)
                {
                    printf("  %s %.3f ms", phase_names[phase], (double)medians[phase] * milliseconds_per_tick);
                }
                printf("\n");
                const PPBenchmarkSample* memory = &samples[implementation][0];
                pp_benchmark_sort(with_printing, BUSTER_PP_BENCHMARK_PASSES);
                pp_benchmark_sort(printing, BUSTER_PP_BENCHMARK_PASSES);
                const double whole = (double)with_printing[BUSTER_PP_BENCHMARK_PASSES / 2];
                const uint64_t bytes = samples[implementation][0].input_bytes;
                printf("             -E -P with printing %.3f ms (printing %.3f ms): %.0f MB/s of input (%lu bytes), %.1f million output tokens/s\n", whole / 1e6,
                       (double)printing[BUSTER_PP_BENCHMARK_PASSES / 2] / 1e6, whole != 0 ? (double)bytes / whole * 1e3 : 0.0, (unsigned long)bytes,
                       whole != 0 ? (double)output_tokens / whole * 1e3 : 0.0);
                printf("             history: %lu bytes total, %.1f bytes/TU mean, %lu bytes/TU maximum; temporary origins peak %lu bytes/TU\n",
                       (unsigned long)memory->history_bytes, (double)memory->history_bytes / file_count, (unsigned long)memory->history_max_bytes,
                       (unsigned long)memory->temporary_max_bytes);
                printf("             records: %lu expansions, %lu argument runs, %lu generated tokens, %lu output runs\n", (unsigned long)memory->expansions,
                       (unsigned long)memory->substitutions, (unsigned long)memory->generated, (unsigned long)memory->runs);
            }
            const PPStatistics* statistics = &samples[only == 2 ? 1 : 0][0].statistics;
            printf("  per pass: %lu directives, %lu file runs (%lu tokens), %lu buffer runs (%lu tokens), %lu object-like expansions copied out and %lu "
                   "buffered, %lu invocations (%lu arguments, %lu expanded first), %lu pastes, %lu builtins, %lu includes skipped by their guards and "
                   "%lu by #pragma once\n",
                   (unsigned long)statistics->directives, (unsigned long)statistics->file_runs, (unsigned long)statistics->file_run_tokens,
                   (unsigned long)statistics->buffer_runs, (unsigned long)statistics->buffer_run_tokens, (unsigned long)statistics->direct_expansions,
                   (unsigned long)statistics->buffered_expansions, (unsigned long)statistics->invocations, (unsigned long)statistics->arguments,
                   (unsigned long)statistics->expanded_arguments, (unsigned long)statistics->pastes, (unsigned long)statistics->builtins, (unsigned long)statistics->skipped_guards,
                   (unsigned long)statistics->skipped_once);
            printf("  #if and #elif: %lu lines of %.1f tokens on average after expansion, %lu of more than 32, the longest %lu\n",
                   (unsigned long)statistics->conditions, statistics->conditions != 0 ? (double)statistics->condition_tokens / (double)statistics->conditions : 0.0,
                   (unsigned long)statistics->long_conditions, (unsigned long)statistics->longest_condition);
            if (with_clang)
            {
                pp_benchmark_sort(clang_times, BUSTER_PP_BENCHMARK_PASSES);
                printf("  clang -E   %8.3f ms (a process per file, writing to /dev/null)\n", (double)clang_times[BUSTER_PP_BENCHMARK_PASSES / 2] / 1e6);
            }
            result = 0;
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
    free(with_input);
    free(passed);
    free(clang_passed);
    free(files);
    arena_release(&arena);
    return result;
}
