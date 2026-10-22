// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The per-file jobs' benchmark (c_files.c): a file cache filled on one lane to eight, as a process that compiles from
// nothing fills it, and how the time scales.
//
// Usage: build/compiler benchmark-files [--lanes LIST] [--cpus LIST] [--runs N] [--spin MS] [--flat] [--bytes map|read|populate]
//                                       [--lane-bytes N] [--read-limit N] [options] path...
//
// The paths (files, or the .c and .h files under directories) are the walk's roots, with -E's options (-I, -isystem,
// -D, --predefines, -std=, ...); with --flat they are only prepared, without following their includes (a corpus such as
// the C library's, which is files rather than translation units). For each lane count of --lanes (1 to 8 by default),
// lane i runs on the i-th CPU of --cpus (by default the CPUs the process may use, in order: one per core on the 9700X).
// Each run is from nothing: a new intern table and cache, and every file probed (opened, then mapped with one lane or
// read with more), lexed, refined and given its directive table; its lanes first spin --spin ms (2 by default), so that
// a core that slept doesn't start slow, then meet at a sync that starts the time. For --runs runs (21 by default) per
// lane count it reports medians: the run's time, its speedup over the first lane count's, and the time until the roots
// were prepared (round 0, which a walk from one big file spends on that file alone); the tokens prepared per second;
// each phase's time on the lane that spent the most on it (FilesPhase: preparing, the merge's lookups, exchange,
// additions and rewrite, reading the include lines, the searches with their probes, the work one lane does while the
// others wait, and waiting at syncs); and how busy the least and the most busy lane were (all but waiting), as a share
// of the run. Then what a run did: its rounds and waves, files, bytes, tokens, shared ids, the merge's candidates, and
// the opens, mappings and reads of its probes. Without --flat, it then preprocesses each root as a translation unit with
// the cache of a run on one lane, and counts the files the walk prepared that no unit entered, with their tokens and
// bytes, and the largest of them. Run it under the 9700X's lock without taskset: it pins its lanes itself. The exit
// status is 1 if the arguments are invalid or a run fails.
//
// Layout: the clock, a run (files_benchmark_lane, files_benchmark_run), the unused files (files_benchmark_unused), and
// run_files_benchmark.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// OS-dependent includes
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>

#define BUSTER_ARRAY_LENGTH(x) (sizeof(x) / sizeof((x)[0]))

#include "buster/compiler/c_lexer.c"
#include "buster/compiler/lane.c"
#include "buster/compiler/c_intern.c"
#include "buster/compiler/c_refine.c"
#include "buster/compiler/c_preprocessor.c"
#include "buster/compiler/c_files.c"

#define BUSTER_FILES_BENCHMARK_MAX_LANES 64
#define BUSTER_FILES_BENCHMARK_MAX_RUNS 1001

// ---------------------------------------------------------------------------------------------------------------------
// The clock

static uint64_t files_benchmark_nanoseconds(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

// The time stamp counter's ticks per nanosecond, from 20 ms of both clocks.
static double files_benchmark_ticks_per_nanosecond(void)
{
    const uint64_t begin = files_benchmark_nanoseconds();
    const uint64_t first = __rdtsc();
    uint64_t now = begin;
    while (now - begin < 20000000)
    {
        now = files_benchmark_nanoseconds();
    }
    return (double)(__rdtsc() - first) / (double)(now - begin);
}

static int files_benchmark_compare(const void* a, const void* b)
{
    const uint64_t x = *(const uint64_t*)a;
    const uint64_t y = *(const uint64_t*)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

// The median of `count` values, which it sorts.
static uint64_t files_benchmark_median(uint64_t* values, uint32_t count)
{
    qsort(values, count, sizeof(uint64_t), files_benchmark_compare);
    return count != 0 ? values[count / 2] : 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// A run

// What the lanes of a run share: the run, how long each lane spins first, and the run's ticks (lane 0's, from the sync
// that starts the time to the sync after the run).
typedef struct FilesBenchmark FilesBenchmark;
struct FilesBenchmark
{
    FilesRun run;
    uint64_t spin_ticks;
    uint64_t ticks;
    uint64_t start;
};

// What a run did, which is the same in every run of a lane count.
typedef struct FilesBenchmarkCounts FilesBenchmarkCounts;
struct FilesBenchmarkCounts
{
    uint32_t rounds;
    uint32_t waves;
    uint64_t files;
    uint64_t bytes;
    uint64_t tokens;
    uint32_t ids;
    uint64_t candidates;
    uint64_t opens;
    uint64_t maps;
    uint64_t reads;
};

// A run's times: its ticks and those until its roots were prepared, each phase's on the slowest lane, and the busiest and
// least busy lane's ticks in all phases.
typedef struct FilesBenchmarkSample FilesBenchmarkSample;
struct FilesBenchmarkSample
{
    uint64_t ticks;
    uint64_t roots;
    uint64_t phases[FILES_PHASE_COUNT];
    uint64_t busy_least;
    uint64_t busy_most;
};

static void files_benchmark_lane(void* argument)
{
    FilesBenchmark* benchmark = argument;
    const uint64_t spin_end = __rdtsc() + benchmark->spin_ticks;
    while (__rdtsc() < spin_end)
    {
        _mm_pause();
    }
    lane_sync();
    const uint64_t start = __rdtsc();
    benchmark->start = lane_index() == 0 ? start : benchmark->start;
    files_run_ahead(&benchmark->run);
    lane_sync();
    if (lane_index() == 0)
    {
        benchmark->ticks = __rdtsc() - start;
    }
}

// One run from nothing on `lanes` lanes (pinned to `cpus`): its times in `sample` and what it did in `counts`. Leaves
// the cache and its table filled when `keep` (for files_benchmark_unused), else releases them.
static bool files_benchmark_run(const PPOptions* options, const char* const* roots, uint32_t root_count, bool follow, FileCacheBytes bytes, uint32_t read_limit,
                                uint64_t lane_bytes, uint32_t lanes, const uint32_t* cpus, uint64_t spin_ticks, FilesBenchmarkSample* sample, FilesBenchmarkCounts* counts,
                                InternTable* table, FileCache* cache, bool keep)
{
    static Preprocessor search;
    static FilesBenchmark benchmark;
    PPOptions with = *options;
    bool result = intern_initialize(table) && file_cache_initialize(cache, table, pp_prepare_options(&with));
    with.files = cache;
    result = result && pp_initialize(&search, &with, table) && files_run_initialize(&benchmark.run, cache, &search, roots, root_count, follow, lanes);
    benchmark.run.bytes = bytes;
    benchmark.run.read_limit = read_limit;
    benchmark.run.lane_bytes = lane_bytes;
    benchmark.spin_ticks = spin_ticks;
    benchmark.ticks = 0;
    result = result && lane_run(lanes, files_benchmark_lane, &benchmark, cpus) && !benchmark.run.failed;
    *sample = (FilesBenchmarkSample){
        .ticks = benchmark.ticks,
        .roots = benchmark.run.roots_prepared > benchmark.start ? benchmark.run.roots_prepared - benchmark.start : 0,
        .busy_least = UINT64_MAX,
    };
    *counts = (FilesBenchmarkCounts){ .rounds = benchmark.run.rounds, .waves = benchmark.run.waves, .candidates = benchmark.run.candidates_exchanged };
    for (uint32_t l = 0; result && l < lanes; l += 1)
    {
        const FilesLane* lane = &benchmark.run.lanes[l];
        uint64_t busy = 0;
        for (uint32_t phase = 0; phase < FILES_PHASE_COUNT; phase += 1)
        {
            sample->phases[phase] = lane->ticks[phase] > sample->phases[phase] ? lane->ticks[phase] : sample->phases[phase];
            busy += phase != FILES_PHASE_WAIT ? lane->ticks[phase] : 0;
        }
        sample->busy_least = busy < sample->busy_least ? busy : sample->busy_least;
        sample->busy_most = busy > sample->busy_most ? busy : sample->busy_most;
        counts->opens += lane->counts.opens;
        counts->maps += lane->counts.maps;
        counts->reads += lane->counts.reads;
    }
    if (benchmark.run.lanes != 0)
    {
        files_run_release(&benchmark.run);
    }
    pp_release(&search);
    counts->files = cache->prepared_records;
    counts->tokens = cache->prepared_tokens;
    counts->ids = table->count;
    for (uint32_t r = 0; r < cache->record_count; r += 1)
    {
        counts->bytes += cache->records[r].prepared ? cache->records[r].length : 0;
    }
    if (!keep || !result)
    {
        file_cache_release(cache);
        intern_release(table);
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The unused files

// Preprocesses each root as a translation unit with a filled cache, and prints how many of the files the walk prepared
// no unit entered, with their tokens and bytes. The walk's records come first: a unit may add others after them (the
// predefines' file, a computed include's), which aren't counted.
static bool files_benchmark_unused(const PPOptions* options, const char* const* roots, uint32_t root_count, InternTable* table, FileCache* cache)
{
    static Preprocessor unit;
    const uint32_t walked = cache->record_count;
    uint8_t* used = calloc((size_t)walked + 1, 1);
    PPOptions with = *options;
    with.files = cache;
    bool result = used != 0;
    uint32_t units_failed = 0;
    for (uint32_t u = 0; result && u < root_count; u += 1)
    {
        result = pp_initialize(&unit, &with, table);
        units_failed += result && !pp_run(&unit, roots[u]);
        for (uint32_t d = 0; result && d < unit.data_count; d += 1)
        {
            if (unit.datas[d].record < walked && unit.datas[d].prepared)
            {
                used[unit.datas[d].record] = 1;
            }
        }
        pp_release(&unit);
    }
    uint64_t files = 0;
    uint64_t unused = 0;
    uint64_t unused_tokens = 0;
    uint64_t tokens = 0;
    uint64_t unused_bytes = 0;
    for (uint32_t r = 0; result && r < walked; r += 1)
    {
        const PPFileData* record = &cache->records[r];
        files += record->prepared;
        tokens += record->prepared ? record->token_count : 0;
        unused += record->prepared && used[r] == 0;
        unused_tokens += record->prepared && used[r] == 0 ? record->token_count : 0;
        unused_bytes += record->prepared && used[r] == 0 ? record->length : 0;
    }
    if (result)
    {
        printf("unused: %lu of the %lu files the walk prepared (%lu of %lu tokens, %.1f%%; %lu bytes) no unit entered; %u of %u units failed\n", (unsigned long)unused,
               (unsigned long)files, (unsigned long)unused_tokens, (unsigned long)tokens, tokens != 0 ? 100.0 * (double)unused_tokens / (double)tokens : 0.0,
               (unsigned long)unused_bytes, units_failed, root_count);
        // The largest unused files, by a path of each (the first path the cache found it by).
        for (uint32_t shown = 0; shown < 8; shown += 1)
        {
            uint32_t largest = BUSTER_PP_NONE;
            for (uint32_t r = 0; r < walked; r += 1)
            {
                const bool candidate = cache->records[r].prepared && used[r] == 0;
                largest = candidate && (largest == BUSTER_PP_NONE || cache->records[r].token_count > cache->records[largest].token_count) ? r : largest;
            }
            const char* path = "";
            for (uint32_t p = 0; largest != BUSTER_PP_NONE && path[0] == 0 && p < cache->path_count; p += 1)
            {
                path = cache->paths[p].record == largest ? cache->paths[p].path : path;
            }
            if (largest != BUSTER_PP_NONE)
            {
                printf("  %8u tokens  %s\n", cache->records[largest].token_count, path);
                used[largest] = 1;
            }
        }
    }
    free(used);
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The command

// Parses a list of numbers and ranges ("0-3,8", "1,2,4") into `values`, at most `capacity` of them. Returns how many,
// or 0 if the list is invalid.
static uint32_t files_benchmark_parse_list(const char* text, uint32_t* values, uint32_t capacity)
{
    uint32_t count = 0;
    bool valid = true;
    const char* p = text;
    while (valid && *p != 0)
    {
        char* end = 0;
        const unsigned long first = strtoul(p, &end, 10);
        unsigned long last = first;
        valid = *p >= '0' && *p <= '9' && end != p;
        p = end;
        if (valid && *p == '-')
        {
            last = strtoul(p + 1, &end, 10);
            valid = p[1] >= '0' && p[1] <= '9' && end != p + 1 && last >= first;
            p = end;
        }
        for (unsigned long value = first; valid && value <= last; value += 1)
        {
            valid = count < capacity && value < UINT32_MAX;
            if (valid)
            {
                values[count] = (uint32_t)value;
                count += 1;
            }
        }
        valid = valid && (*p == 0 || (*p == ',' && p[1] != 0));
        p += *p == ',';
    }
    return valid ? count : 0;
}

// The CPUs this process may run on, in order, up to `capacity` of them.
static uint32_t files_benchmark_allowed_cpus(uint32_t* cpus, uint32_t capacity)
{
    uint64_t mask[BUSTER_LANE_CPU_MASK_WORDS] = { 0 };
    uint32_t count = 0;
    if (syscall(SYS_sched_getaffinity, 0, sizeof(mask), mask) > 0)
    {
        for (uint32_t cpu = 0; cpu < BUSTER_LANE_CPU_MASK_WORDS * 64 && count < capacity; cpu += 1)
        {
            if ((mask[cpu / 64] >> (cpu % 64)) & 1)
            {
                cpus[count] = cpu;
                count += 1;
            }
        }
    }
    return count;
}

int run_files_benchmark(int argc, char** argv);

int run_files_benchmark(int argc, char** argv)
{
    uint32_t lanes[BUSTER_FILES_BENCHMARK_MAX_LANES] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    uint32_t lane_count = 8;
    uint32_t cpus[BUSTER_FILES_BENCHMARK_MAX_LANES];
    uint32_t cpu_count = 0;
    uint32_t runs = 21;
    uint32_t spin = 2;
    bool flat = false;
    FileCacheBytes bytes = BUSTER_FILES_BYTES;
    uint64_t lane_bytes = BUSTER_FILES_LANE_BYTES;
    uint32_t read_limit = BUSTER_FILES_READ_LIMIT;
    char** passed = calloc((size_t)argc + 2, sizeof(char*));
    int passed_count = 0;
    FilesSources sources = { 0 };
    bool valid = passed != 0;
    for (int i = 1; valid && i < argc; i += 1)
    {
        const char* argument = argv[i];
        const bool takes_value = strcmp(argument, "-I") == 0 || strcmp(argument, "-D") == 0 || strcmp(argument, "-U") == 0 || strcmp(argument, "-isystem") == 0 ||
                                 strcmp(argument, "-iquote") == 0 || strcmp(argument, "-idirafter") == 0 || strcmp(argument, "-include") == 0 ||
                                 strcmp(argument, "--predefines") == 0 || strcmp(argument, "-x") == 0;
        const char* next = i + 1 < argc ? argv[i + 1] : 0;
        if (strcmp(argument, "--lanes") == 0 && next != 0)
        {
            lane_count = files_benchmark_parse_list(next, lanes, BUSTER_FILES_BENCHMARK_MAX_LANES);
            valid = lane_count != 0;
            i += 1;
        }
        else if (strcmp(argument, "--cpus") == 0 && next != 0)
        {
            cpu_count = files_benchmark_parse_list(next, cpus, BUSTER_FILES_BENCHMARK_MAX_LANES);
            valid = cpu_count != 0;
            i += 1;
        }
        else if (strcmp(argument, "--runs") == 0 && next != 0)
        {
            runs = (uint32_t)strtoul(next, 0, 10);
            valid = runs >= 1 && runs <= BUSTER_FILES_BENCHMARK_MAX_RUNS;
            i += 1;
        }
        else if (strcmp(argument, "--spin") == 0 && next != 0)
        {
            spin = (uint32_t)strtoul(next, 0, 10);
            i += 1;
        }
        else if (strcmp(argument, "--flat") == 0)
        {
            flat = true;
        }
        else if (strcmp(argument, "--lane-bytes") == 0 && next != 0)
        {
            lane_bytes = strtoull(next, 0, 10);
            i += 1;
        }
        else if (strcmp(argument, "--read-limit") == 0 && next != 0)
        {
            read_limit = (uint32_t)strtoul(next, 0, 10);
            i += 1;
        }
        else if (strcmp(argument, "--bytes") == 0 && next != 0)
        {
            bytes = strcmp(next, "populate") == 0 ? FILE_CACHE_MAP_POPULATED : strcmp(next, "read") == 0 ? FILE_CACHE_READ : FILE_CACHE_MAP;
            valid = strcmp(next, "populate") == 0 || strcmp(next, "read") == 0 || strcmp(next, "map") == 0;
            i += 1;
        }
        else if (argument[0] == '-')
        {
            passed[passed_count++] = argv[i];
            if (takes_value && next != 0)
            {
                i += 1;
                passed[passed_count++] = argv[i];
            }
        }
        else
        {
            valid = files_list_sources(&sources, argument);
            if (!valid)
            {
                fprintf(stderr, "can't read %s\n", argument);
            }
        }
    }
    cpu_count = cpu_count != 0 ? cpu_count : files_benchmark_allowed_cpus(cpus, BUSTER_FILES_BENCHMARK_MAX_LANES);
    // Lane counts past the CPUs are left out (a CI job may allow fewer CPUs than the machine has).
    uint32_t kept = 0;
    for (uint32_t l = 0; valid && l < lane_count; l += 1)
    {
        if (lanes[l] > cpu_count)
        {
            printf("benchmark-files: %u lanes left out: %u CPUs\n", lanes[l], cpu_count);
        }
        lanes[kept] = lanes[l];
        kept += lanes[l] >= 1 && lanes[l] <= cpu_count;
    }
    lane_count = kept;
    valid = valid && lane_count != 0;
    valid = valid && sources.count != 0;
    Arena arena = arena_reserve(1ull << 30);
    PPOptions options;
    const char* input;
    const char* output;
    bool markers;
    if (valid)
    {
        // pp_parse_options wants one input: give it the first path, and use its options for every root.
        passed[passed_count] = (char*)sources.paths[0];
        valid = pp_parse_options(passed_count + 1, passed, &options, &arena, &input, &output, &markers);
    }
    if (!valid)
    {
        fprintf(stderr, "usage: %s [--lanes LIST] [--cpus LIST] [--runs N] [--spin MS] [--flat] [--bytes map|read|populate] [--lane-bytes N] [--read-limit N] [options] path...\n",
                argv[0]);
    }
    const double ticks_per_nanosecond = valid ? files_benchmark_ticks_per_nanosecond() : 1.0;
    const uint64_t spin_ticks = (uint64_t)((double)spin * 1e6 * ticks_per_nanosecond);
    const double milliseconds = 1e-6 / ticks_per_nanosecond;
    static InternTable table;
    static FileCache cache;
    static FilesBenchmarkSample samples[BUSTER_FILES_BENCHMARK_MAX_RUNS];
    static uint64_t values[BUSTER_FILES_BENCHMARK_MAX_RUNS];
    bool result = valid;
    if (result)
    {
        printf("benchmark-files: %u roots%s, %u runs per lane count, lanes on CPUs", sources.count, flat ? " (flat: their includes not followed)" : "", runs);
        for (uint32_t c = 0; c < cpu_count && c < lanes[lane_count - 1]; c += 1)
        {
            printf(" %u", cpus[c]);
        }
        printf(", %u ms of spinning before each run\n", spin);
        printf("lanes  run ms  speedup  roots ms  Mtokens/s  prepare  lookup  exchange  add  rewrite  requests  search  alone   wait  (ms, most of a lane)  busy\n");
    }
    double first_run = 0.0;
    FilesBenchmarkCounts counts = { 0 };
    for (uint32_t l = 0; result && l < lane_count; l += 1)
    {
        // A warm-up run, then the measured ones.
        result = files_benchmark_run(&options, sources.paths, sources.count, !flat, bytes, read_limit, lane_bytes, lanes[l], cpus, spin_ticks, &samples[0], &counts, &table, &cache, false);
        for (uint32_t r = 0; result && r < runs; r += 1)
        {
            result = files_benchmark_run(&options, sources.paths, sources.count, !flat, bytes, read_limit, lane_bytes, lanes[l], cpus, spin_ticks, &samples[r], &counts, &table, &cache, false);
        }
        if (result)
        {
            for (uint32_t r = 0; r < runs; r += 1)
            {
                values[r] = samples[r].ticks;
            }
            const double run = (double)files_benchmark_median(values, runs) * milliseconds;
            first_run = l == 0 ? run : first_run;
            for (uint32_t r = 0; r < runs; r += 1)
            {
                values[r] = samples[r].roots;
            }
            const double roots = (double)files_benchmark_median(values, runs) * milliseconds;
            double phases[FILES_PHASE_COUNT];
            for (uint32_t phase = 0; phase < FILES_PHASE_COUNT; phase += 1)
            {
                for (uint32_t r = 0; r < runs; r += 1)
                {
                    values[r] = samples[r].phases[phase];
                }
                phases[phase] = (double)files_benchmark_median(values, runs) * milliseconds;
            }
            for (uint32_t r = 0; r < runs; r += 1)
            {
                values[r] = samples[r].ticks != 0 ? samples[r].busy_least * 1000 / samples[r].ticks : 0;
            }
            const double least = (double)files_benchmark_median(values, runs) / 10.0;
            for (uint32_t r = 0; r < runs; r += 1)
            {
                values[r] = samples[r].ticks != 0 ? samples[r].busy_most * 1000 / samples[r].ticks : 0;
            }
            const double most = (double)files_benchmark_median(values, runs) / 10.0;
            printf("%5u  %6.2f  %7.2f  %8.2f  %9.1f  %7.2f  %6.2f  %8.2f  %4.2f  %7.2f  %8.2f  %6.2f  %5.2f  %5.2f  %19s  %4.1f-%.1f%%\n", lanes[l], run,
                   run != 0.0 ? first_run / run : 0.0, roots, run != 0.0 ? (double)counts.tokens / run / 1e3 : 0.0, phases[FILES_PHASE_PREPARE],
                   phases[FILES_PHASE_LOOKUP], phases[FILES_PHASE_EXCHANGE], phases[FILES_PHASE_ADD], phases[FILES_PHASE_REMAP], phases[FILES_PHASE_REQUESTS],
                   phases[FILES_PHASE_SEARCH], phases[FILES_PHASE_ALONE], phases[FILES_PHASE_WAIT], "", least, most);
        }
        else
        {
            fprintf(stderr, "benchmark-files: a run on %u lanes failed\n", lanes[l]);
        }
    }
    if (result)
    {
        printf("a run: %u rounds, %u waves; %lu files, %lu bytes, %lu tokens; %u shared ids; %lu candidates exchanged, %lu opens, %lu mappings and %lu reads (on %u "
               "lanes)\n",
               counts.rounds, counts.waves, (unsigned long)counts.files, (unsigned long)counts.bytes, (unsigned long)counts.tokens, counts.ids,
               (unsigned long)counts.candidates, (unsigned long)counts.opens, (unsigned long)counts.maps, (unsigned long)counts.reads, lanes[lane_count - 1]);
    }
    if (result && !flat)
    {
        FilesBenchmarkSample sample;
        result = files_benchmark_run(&options, sources.paths, sources.count, true, bytes, read_limit, lane_bytes, 1, cpus, 0, &sample, &counts, &table, &cache, true) &&
                 files_benchmark_unused(&options, sources.paths, sources.count, &table, &cache);
        file_cache_release(&cache);
        intern_release(&table);
    }
    arena_release(&arena);
    arena_release(&sources.arena);
    free(passed);
    return result ? 0 : 1;
}
