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
// The command starts one fixed lane team before its warm-ups and reuses it for every run, including the final
// single-lane cache. As before, the reported run starts inside the entry after its warm-up sync, excluding startup.
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
#include "buster/compiler/c_predefines.c"
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

// EXPERIMENT (#262, not for merging): a way to run the walk, which --variant options list and the runs take in turn.
#define BUSTER_FILES_BENCHMARK_MAX_VARIANTS 8
typedef struct FilesBenchmarkVariant FilesBenchmarkVariant;
struct FilesBenchmarkVariant
{
    FileCacheBytes bytes;
    uint32_t read_limit;
    uint64_t lane_bytes;
    bool old_merge;
    bool partial_take;
    const char* label;
};

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

// One run from nothing on `lanes` lanes (pinned to `cpus`): its times in `sample`, what it did in `counts` and, unless
// `trace` is null, where its time went round by round. Leaves the cache and its table filled when `keep` (for
// files_benchmark_unused), else releases them.
static bool files_benchmark_run(const PPOptions* options, const char* const* roots, uint32_t root_count, bool follow, const FilesBenchmarkVariant* variant,
                                uint32_t lanes, const uint32_t* cpus, uint64_t spin_ticks, FilesBenchmarkSample* sample, FilesBenchmarkCounts* counts, FilesTrace* trace,
                                InternTable* table, FileCache* cache, bool keep)
{
    static Preprocessor search;
    static FilesBenchmark benchmark;
    PPOptions with = *options;
    bool result = intern_initialize(table) && file_cache_initialize(cache, table, pp_prepare_options(&with));
    with.files = cache;
    result = result && pp_initialize(&search, &with, table) && files_run_initialize(&benchmark.run, cache, &search, roots, root_count, follow, lanes);
    benchmark.run.bytes = variant->bytes;
    benchmark.run.read_limit = variant->read_limit;
    benchmark.run.lane_bytes = variant->lane_bytes;
    benchmark.run.old_merge = variant->old_merge;
    benchmark.run.partial_take = variant->partial_take;
    benchmark.spin_ticks = spin_ticks;
    benchmark.ticks = 0;
    if (trace != 0)
    {
        *trace = (FilesTrace){ 0 };
        benchmark.run.trace = trace;
    }
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
// The rounds

// The ticks of `phase` (or of every merge phase, for FILES_PHASE_COUNT) lane `lane` spent in a trace's interval `at`.
static uint64_t files_benchmark_interval(const FilesTrace* trace, uint32_t at, uint32_t lane, uint32_t phase)
{
    const uint64_t* begin = trace->ticks[at][lane];
    const uint64_t* end = trace->ticks[at + 1][lane];
    uint64_t result = 0;
    for (uint32_t p = FILES_PHASE_LOOKUP; phase == FILES_PHASE_COUNT && p <= FILES_PHASE_REMAP; p += 1)
    {
        result += end[p] - begin[p];
    }
    return phase == FILES_PHASE_COUNT ? result : end[phase] - begin[phase];
}

// Prints where the runs' time went round by round (--rounds), as medians of `runs` traces of `lanes` lanes: per interval
// (the roots' search, then each round: its preparation and the searches for the next round's files), what it did, its
// wall time on lane 0, lane 0's phases, and the other traced lanes' most preparing and searching and least and most
// waiting.
static void files_benchmark_rounds(const FilesTrace* traces, uint32_t runs, uint32_t lanes, double milliseconds)
{
    static uint64_t values[BUSTER_FILES_BENCHMARK_MAX_RUNS];
    const uint32_t traced = lanes < BUSTER_FILES_TRACE_LANES ? lanes : BUSTER_FILES_TRACE_LANES;
    printf("rounds on %u lanes (medians of %u runs, ms; lane 0, then the other lanes' most and least):\n", lanes, runs);
    printf("  round  files      bytes  lanes  waves  probes     wall  prepare   merge  requests  search  alone    wait |  prepare  search    waits\n");
    for (uint32_t at = 0; at <= traces[0].rounds; at += 1)
    {
        // What each value is: 0 the wall time, 1 to 6 lane 0's phases, 7 to 10 the other lanes'.
        double medians[11];
        for (uint32_t value = 0; value < 11; value += 1)
        {
            for (uint32_t r = 0; r < runs; r += 1)
            {
                const FilesTrace* trace = &traces[r];
                static const uint32_t phases[] = { FILES_PHASE_PREPARE, FILES_PHASE_COUNT, FILES_PHASE_REQUESTS, FILES_PHASE_SEARCH, FILES_PHASE_ALONE, FILES_PHASE_WAIT };
                uint64_t result = 0;
                if (value == 0)
                {
                    result = trace->clock[at + 1][0] - trace->clock[at][0];
                }
                else if (value <= 6)
                {
                    result = files_benchmark_interval(trace, at, 0, phases[value - 1]);
                }
                else
                {
                    result = value == 9 ? UINT64_MAX : 0;
                    for (uint32_t l = 1; l < traced; l += 1)
                    {
                        const uint32_t phase = value == 7 ? FILES_PHASE_PREPARE : value == 8 ? FILES_PHASE_SEARCH : FILES_PHASE_WAIT;
                        const uint64_t spent = files_benchmark_interval(trace, at, l, phase);
                        result = value == 9 ? (spent < result ? spent : result) : spent > result ? spent : result;
                    }
                    result = traced > 1 ? result : 0;
                }
                values[r] = result;
            }
            medians[value] = (double)files_benchmark_median(values, runs) * milliseconds;
        }
        const FilesTrace* first = &traces[0];
        char what[64];
        if (at == 0)
        {
            snprintf(what, sizeof(what), "  roots      -          -      -");
        }
        else
        {
            snprintf(what, sizeof(what), "  %5u %6u %10lu %6u", at - 1, first->files[at - 1], (unsigned long)first->bytes[at - 1], first->working[at - 1]);
        }
        printf("%s %6lu %7lu %8.3f %8.3f %7.3f %9.3f %7.3f %6.3f %7.3f | %8.3f %7.3f %.3f-%.3f\n", what, (unsigned long)(first->waves[at + 1] - first->waves[at]),
               (unsigned long)(first->probes[at + 1] - first->probes[at]), medians[0], medians[1], medians[2], medians[3], medians[4], medians[5], medians[6], medians[7],
               medians[8], medians[9], medians[10]);
    }
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
    bool rounds = false;
    const char* variant_specs[BUSTER_FILES_BENCHMARK_MAX_VARIANTS];
    uint32_t variant_count = 0;
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
                                 pp_own_option_with_value(argument) || strcmp(argument, "-x") == 0 || strcmp(argument, "-target") == 0;
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
        else if (strcmp(argument, "--rounds") == 0)
        {
            rounds = true;
        }
        else if (strcmp(argument, "--variant") == 0 && next != 0 && variant_count < BUSTER_FILES_BENCHMARK_MAX_VARIANTS)
        {
            // EXPERIMENT (#262): a comma-separated list of lane-bytes=N, read-limit=N, bytes=map|read|populate, old-merge
            // and partial-take, each over the command line's settings.
            variant_specs[variant_count] = next;
            variant_count += 1;
            i += 1;
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
    static FilesBenchmarkVariant variants[BUSTER_FILES_BENCHMARK_MAX_VARIANTS];
    const uint32_t variant_total = variant_count != 0 ? variant_count : 1;
    for (uint32_t v = 0; valid && v < variant_total; v += 1)
    {
        variants[v] = (FilesBenchmarkVariant){ .bytes = bytes, .read_limit = read_limit, .lane_bytes = lane_bytes, .label = variant_count != 0 ? variant_specs[v] : "" };
        const char* p = variant_count != 0 ? variant_specs[v] : "";
        while (valid && *p != 0)
        {
            const char* end = strchr(p, ',');
            const size_t length = end != 0 ? (size_t)(end - p) : strlen(p);
            if (length > 11 && strncmp(p, "lane-bytes=", 11) == 0)
            {
                variants[v].lane_bytes = strtoull(p + 11, 0, 10);
            }
            else if (length > 11 && strncmp(p, "read-limit=", 11) == 0)
            {
                variants[v].read_limit = (uint32_t)strtoul(p + 11, 0, 10);
            }
            else if (length == 12 && strncmp(p, "bytes=", 6) == 0 && strncmp(p + 6, "populate", 6) == 0)
            {
                variants[v].bytes = FILE_CACHE_MAP_POPULATED;
            }
            else if (length == 10 && strncmp(p, "bytes=read", 10) == 0)
            {
                variants[v].bytes = FILE_CACHE_READ;
            }
            else if (length == 9 && strncmp(p, "bytes=map", 9) == 0)
            {
                variants[v].bytes = FILE_CACHE_MAP;
            }
            else if (length == 9 && strncmp(p, "old-merge", 9) == 0)
            {
                variants[v].old_merge = true;
            }
            else if (length == 12 && strncmp(p, "partial-take", 12) == 0)
            {
                variants[v].partial_take = true;
            }
            else
            {
                fprintf(stderr, "benchmark-files: an unknown variant setting in %s\n", variant_specs[v]);
                valid = false;
            }
            p += length + (end != 0);
        }
    }
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
        fprintf(stderr,
                "usage: %s [--lanes LIST] [--cpus LIST] [--runs N] [--spin MS] [--flat] [--rounds] [--variant SPEC]... [--bytes map|read|populate] [--lane-bytes N] "
                "[--read-limit N] [options] path...\n",
                argv[0]);
    }
    const double ticks_per_nanosecond = valid ? files_benchmark_ticks_per_nanosecond() : 1.0;
    const uint64_t spin_ticks = (uint64_t)((double)spin * 1e6 * ticks_per_nanosecond);
    const double milliseconds = 1e-6 / ticks_per_nanosecond;
    static InternTable table;
    static FileCache cache;
    static FilesBenchmarkSample samples[BUSTER_FILES_BENCHMARK_MAX_VARIANTS][BUSTER_FILES_BENCHMARK_MAX_RUNS];
    static uint64_t values[BUSTER_FILES_BENCHMARK_MAX_RUNS];
    // With --rounds, every run's trace, by variant and lane count, for the tables after the runs'.
    FilesTrace* traces = valid && rounds ? calloc((size_t)runs * lane_count * variant_total, sizeof(FilesTrace)) : 0;
    bool result = valid && (!rounds || traces != 0);
    uint32_t capacity = 1;
    for (uint32_t l = 0; valid && l < lane_count; l += 1)
    {
        capacity = lanes[l] > capacity ? lanes[l] : capacity;
    }
    LaneTeam team = { 0 };
    if (result)
    {
        result = lane_team_start(&team, capacity, cpus);
        if (!result)
        {
            fprintf(stderr, "benchmark-files: can't start or pin the command's %u lanes\n", capacity);
        }
    }
    if (result)
    {
        printf("benchmark-files: %u roots%s, %u runs per lane count, lanes on CPUs", sources.count, flat ? " (flat: their includes not followed)" : "", runs);
        for (uint32_t c = 0; c < capacity; c += 1)
        {
            printf(" %u", cpus[c]);
        }
        printf(", %u ms of spinning before each run\n", spin);
        printf("one command team, reused across runs; run times exclude thread startup and shutdown\n");
        printf("lanes  run ms  speedup  roots ms  Mtokens/s  prepare  lookup  exchange  add  rewrite  requests  search  alone   wait  (ms, most of a lane)  busy\n");
    }
    double first_runs[BUSTER_FILES_BENCHMARK_MAX_VARIANTS] = { 0 };
    FilesBenchmarkCounts counts = { 0 };
    for (uint32_t l = 0; result && l < lane_count; l += 1)
    {
        // A warm-up run of each variant, then the measured ones, the variants in an order that rotates by run.
        for (uint32_t v = 0; result && v < variant_total; v += 1)
        {
            result = files_benchmark_run(&options, sources.paths, sources.count, !flat, &variants[v], lanes[l], cpus, spin_ticks, &samples[v][0], &counts, 0, &table, &cache, false);
        }
        for (uint32_t r = 0; result && r < runs; r += 1)
        {
            for (uint32_t k = 0; result && k < variant_total; k += 1)
            {
                const uint32_t v = (r + k) % variant_total;
                result = files_benchmark_run(&options, sources.paths, sources.count, !flat, &variants[v], lanes[l], cpus, spin_ticks, &samples[v][r], &counts,
                                             traces != 0 ? &traces[((uint64_t)v * lane_count + l) * runs + r] : 0, &table, &cache, false);
            }
        }
        for (uint32_t v = 0; result && v < variant_total; v += 1)
        {
            const FilesBenchmarkSample* const run_samples = samples[v];
            double first_run = first_runs[v];
            for (uint32_t r = 0; r < runs; r += 1)
            {
                values[r] = run_samples[r].ticks;
            }
            const double run = (double)files_benchmark_median(values, runs) * milliseconds;
            first_run = l == 0 ? run : first_run;
            first_runs[v] = first_run;
            for (uint32_t r = 0; r < runs; r += 1)
            {
                values[r] = run_samples[r].roots;
            }
            const double roots = (double)files_benchmark_median(values, runs) * milliseconds;
            double phases[FILES_PHASE_COUNT];
            for (uint32_t phase = 0; phase < FILES_PHASE_COUNT; phase += 1)
            {
                for (uint32_t r = 0; r < runs; r += 1)
                {
                    values[r] = run_samples[r].phases[phase];
                }
                phases[phase] = (double)files_benchmark_median(values, runs) * milliseconds;
            }
            for (uint32_t r = 0; r < runs; r += 1)
            {
                values[r] = run_samples[r].ticks != 0 ? run_samples[r].busy_least * 1000 / run_samples[r].ticks : 0;
            }
            const double least = (double)files_benchmark_median(values, runs) / 10.0;
            for (uint32_t r = 0; r < runs; r += 1)
            {
                values[r] = run_samples[r].ticks != 0 ? run_samples[r].busy_most * 1000 / run_samples[r].ticks : 0;
            }
            const double most = (double)files_benchmark_median(values, runs) / 10.0;
            printf("%5u  %6.2f  %7.2f  %8.2f  %9.1f  %7.2f  %6.2f  %8.2f  %4.2f  %7.2f  %8.2f  %6.2f  %5.2f  %5.2f  %19s  %4.1f-%.1f%%\n", lanes[l], run,
                   run != 0.0 ? first_run / run : 0.0, roots, run != 0.0 ? (double)counts.tokens / run / 1e3 : 0.0, phases[FILES_PHASE_PREPARE],
                   phases[FILES_PHASE_LOOKUP], phases[FILES_PHASE_EXCHANGE], phases[FILES_PHASE_ADD], phases[FILES_PHASE_REMAP], phases[FILES_PHASE_REQUESTS],
                   phases[FILES_PHASE_SEARCH], phases[FILES_PHASE_ALONE], phases[FILES_PHASE_WAIT], variants[v].label, least, most);
        }
        if (!result)
        {
            fprintf(stderr, "benchmark-files: a run on %u lanes failed\n", lanes[l]);
        }
    }
    for (uint32_t v = 0; result && traces != 0 && v < variant_total; v += 1)
    {
        for (uint32_t l = 0; l < lane_count; l += 1)
        {
            printf("%s", variants[v].label[0] != 0 ? "variant " : "");
            printf("%s%s", variants[v].label, variants[v].label[0] != 0 ? ": " : "");
            files_benchmark_rounds(&traces[((uint64_t)v * lane_count + l) * runs], runs, lanes[l], milliseconds);
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
        result = files_benchmark_run(&options, sources.paths, sources.count, true, &variants[0], 1, cpus, 0, &sample, &counts, 0, &table, &cache, true) &&
                 files_benchmark_unused(&options, sources.paths, sources.count, &table, &cache);
        file_cache_release(&cache);
        intern_release(&table);
    }
    lane_team_stop(&team);
    free(traces);
    arena_release(&arena);
    arena_release(&sources.arena);
    free(passed);
    return result ? 0 : 1;
}
