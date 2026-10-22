// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The foreign inputs' benchmark: what steps 1 to 5 of linking other compilers' objects cost (foreign.c;
// docs/object-writer-and-linker.md, "Linking other compilers' objects"), phase by phase and lane by lane, against
// mold's and wild's reading and resolving on one thread.
//
// Usage: build/compiler benchmark-foreign [--functions LIST] [--filler N] [--seed N] [--lanes LIST] [--cpus LIST]
//                                         [--runs N] [--directory PATH] [--kinds LIST] [--linkers] [--keep]
//
// The programs are link_program.c's that call libc, written as objects by the object writer, 200 workers per unit as
// synth's translation units have: by default one worker and 2,000 (about hello world's and tcc's outputs), and one of
// synth's shape, 102,400 workers in 512 units with up to 64 filler instructions each; or with --functions, a program of
// each count of workers, of up to --filler (8) filler instructions each. Each is resolved as GCC's driver links it
// (--kinds: dynamic, a PIE against libc.so's script, libgcc and the crt objects; static, against libgcc.a,
// libgcc_eh.a and libc.a in a group, whose members the rounds load; and members, the dynamic one with every unit but
// the first in an archive after it), by each path and lane count of --lanes (1 by default), its lanes pinned to the
// CPUs of --cpus in order. Each figure is the median of --runs runs (21) after a warm-up: the whole resolution from
// foreign_run's call to its return, and each phase's time (ForeignLane.nanoseconds): with several lanes, the slowest
// lane's. Then what the resolution did: its inputs, the objects it loaded, its rounds, symbols, groups and names.
// With --linkers, mold (--perf) and wild (--time) link the same command line on one thread, 11 times after a warm-up,
// and the medians of the phases that do the same work are printed: mold's read_input_files and resolve_symbols, wild's
// Open input files, Load inputs into symbol DB, Resolve symbols, Resolve alternative symbol definitions and Section
// resolution (mold prints milliseconds, wild hundredths of them).
//
// Run it under the 9700X's lock with taskset -c 2 for one lane, or with --cpus for several (lane_run pins them). The
// exit status is 1 if the arguments are invalid, a program can't be made, or a resolution fails or differs between the
// paths.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

// OS-dependent includes
#include <time.h>
#include <spawn.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

typedef struct String8 String8;
struct String8
{
    char* pointer;
    uint64_t length;
};

#ifndef BUSTER_ARRAY_LENGTH
#define BUSTER_ARRAY_LENGTH(x) (sizeof(x) / sizeof((x)[0]))
#endif

extern char** environ;

// The encoder's other entry points serve its own tests and benchmark: the generated programs use x86_encode alone.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "buster/compiler/x86_64_encoder.c"
#pragma clang diagnostic pop
#include "buster/compiler/lane.c"
#include "buster/compiler/link.c"
#include "buster/compiler/link_program.c"
#include "buster/compiler/object_writer.c"
#include "buster/compiler/import.c"
#include "buster/compiler/runtime.c"
#include "buster/compiler/foreign.c"

#define BUSTER_FOREIGN_BENCHMARK_RUNS 21
#define BUSTER_FOREIGN_BENCHMARK_LINKER_RUNS 11
#define BUSTER_FOREIGN_BENCHMARK_MAX_PROGRAMS 16
#define BUSTER_FOREIGN_BENCHMARK_MAX_LANES 16
#define BUSTER_FOREIGN_BENCHMARK_UNIT_FUNCTIONS 200
#define BUSTER_FOREIGN_BENCHMARK_PATH_BYTES 4096
#define BUSTER_FOREIGN_BENCHMARK_ARGUMENTS 4096
#define BUSTER_FOREIGN_BENCHMARK_ARENA_BYTES (1ull << 34)

typedef struct ForeignBenchmarkProgram ForeignBenchmarkProgram;
struct ForeignBenchmarkProgram
{
    uint32_t functions;
    uint32_t filler;
};

static const ForeignBenchmarkProgram foreign_benchmark_default_programs[] = {
    {
        .functions = 1,
        .filler = 8,
    },
    {
        .functions = 2000,
        .filler = 8,
    },
    {
        .functions = 102400,
        .filler = 64,
    },
};

typedef enum ForeignBenchmarkKind
{
    FOREIGN_BENCHMARK_DYNAMIC,
    FOREIGN_BENCHMARK_STATIC,
    FOREIGN_BENCHMARK_MEMBERS,
    FOREIGN_BENCHMARK_KINDS,
} ForeignBenchmarkKind;

static const char* const foreign_benchmark_kind_names[FOREIGN_BENCHMARK_KINDS] = {
    [FOREIGN_BENCHMARK_DYNAMIC] = "dynamic",
    [FOREIGN_BENCHMARK_STATIC] = "static",
    [FOREIGN_BENCHMARK_MEMBERS] = "members",
};

static const char* const foreign_benchmark_phase_names[FOREIGN_PHASES] = {
    [FOREIGN_PHASE_OPEN] = "open",
    [FOREIGN_PHASE_INDICES] = "indices",
    [FOREIGN_PHASE_HEADERS] = "headers",
    [FOREIGN_PHASE_SYMBOLS] = "symbols",
    [FOREIGN_PHASE_EXCHANGE] = "exchange",
    [FOREIGN_PHASE_RESOLVE] = "resolve",
    [FOREIGN_PHASE_LOAD] = "load",
    [FOREIGN_PHASE_GROUPS] = "groups",
    [FOREIGN_PHASE_WINNERS] = "winners",
};

// ---------------------------------------------------------------------------------------------------------------------
// Measuring

static uint64_t foreign_benchmark_now(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

static int foreign_benchmark_compare(const void* a, const void* b)
{
    const uint64_t x = *(const uint64_t*)a;
    const uint64_t y = *(const uint64_t*)b;
    return (x > y) - (x < y);
}

// The median of the values, in microseconds.
static double foreign_benchmark_median(uint64_t* values, uint32_t count)
{
    qsort(values, count, sizeof(uint64_t), foreign_benchmark_compare);
    return count != 0 ? (double)values[count / 2] / 1e3 : 0;
}

// Runs a command with its standard output and error into files, and returns its exit status, or -1.
static int foreign_benchmark_run(char* const* arguments, const char* output_path, const char* error_path)
{
    int result = -1;
    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) == 0)
    {
        pid_t child = 0;
        const bool ready = posix_spawn_file_actions_addopen(&actions, 1, output_path, O_WRONLY | O_CREAT | O_TRUNC, 0644) == 0 &&
                           posix_spawn_file_actions_addopen(&actions, 2, error_path, O_WRONLY | O_CREAT | O_TRUNC, 0644) == 0;
        int status = 0;
        if (ready && posix_spawnp(&child, arguments[0], &actions, 0, arguments, environ) == 0 && waitpid(child, &status, 0) == child && WIFEXITED(status))
        {
            result = WEXITSTATUS(status);
        }
        posix_spawn_file_actions_destroy(&actions);
    }
    return result;
}

// A file's bytes, NUL-terminated, from `arena`; a null pointer if it can't be read.
static String8 foreign_benchmark_read(Arena* arena, const char* path)
{
    String8 result = { 0 };
    const int file = open(path, O_RDONLY | O_CLOEXEC);
    struct stat status;
    if (file >= 0 && fstat(file, &status) == 0)
    {
        char* bytes = arena_push(arena, (uint64_t)status.st_size + 1, 1);
        uint64_t done = 0;
        while (bytes != 0 && done < (uint64_t)status.st_size)
        {
            const ssize_t count = read(file, bytes + done, (uint64_t)status.st_size - done);
            done += count > 0 ? (uint64_t)count : 0;
            bytes = count > 0 ? bytes : 0;
        }
        if (bytes != 0)
        {
            bytes[status.st_size] = 0;
            result.pointer = bytes;
            result.length = (uint64_t)status.st_size;
        }
    }
    if (file >= 0)
    {
        close(file);
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Command lines

typedef struct ForeignBenchmarkLine ForeignBenchmarkLine;
struct ForeignBenchmarkLine
{
    // The resolution's arguments, and the linkers' (theirs: the same with their own options first).
    char* ours[BUSTER_FOREIGN_BENCHMARK_ARGUMENTS];
    uint32_t our_count;
    char* theirs[BUSTER_FOREIGN_BENCHMARK_ARGUMENTS];
    uint32_t their_count;
};

static void foreign_benchmark_add(ForeignBenchmarkLine* line, char* argument, bool ours, bool theirs)
{
    if (ours && line->our_count < BUSTER_FOREIGN_BENCHMARK_ARGUMENTS)
    {
        line->ours[line->our_count] = argument;
        line->our_count += 1;
    }
    if (theirs && line->their_count < BUSTER_FOREIGN_BENCHMARK_ARGUMENTS)
    {
        line->theirs[line->their_count] = argument;
        line->their_count += 1;
    }
}

// What GCC's driver puts around the inputs: the system's crt objects of each kind and the -L directories.
typedef struct ForeignBenchmarkSystem ForeignBenchmarkSystem;
struct ForeignBenchmarkSystem
{
    char dynamic[RUNTIME_OBJECTS][BUSTER_FOREIGN_BENCHMARK_PATH_BYTES];
    char static_objects[RUNTIME_OBJECTS][BUSTER_FOREIGN_BENCHMARK_PATH_BYTES];
    char directories[3][BUSTER_FOREIGN_BENCHMARK_PATH_BYTES + 2];
    bool found;
};

static bool foreign_benchmark_system(ForeignBenchmarkSystem* system)
{
    // Zeroed, so that either releases whether or not its import ran (the second doesn't when the first fails).
    Runtime dynamic = { 0 };
    Runtime static_runtime = { 0 };
    *system = (ForeignBenchmarkSystem){ 0 };
    const bool imported = runtime_import(&dynamic) == RUNTIME_ERROR_NONE && runtime_import_static(&static_runtime) == RUNTIME_ERROR_NONE;
    if (imported)
    {
        for (uint32_t i = 0; i < RUNTIME_OBJECTS; i += 1)
        {
            snprintf(system->dynamic[i], sizeof(system->dynamic[i]), "%.*s", (int)dynamic.object_paths[i].length, dynamic.object_paths[i].pointer);
            snprintf(system->static_objects[i], sizeof(system->static_objects[i]), "%.*s", (int)static_runtime.object_paths[i].length, static_runtime.object_paths[i].pointer);
        }
        const String8 libc = foreign_directory(dynamic.libc_script);
        snprintf(system->directories[0], sizeof(system->directories[0]), "-L%.*s", (int)dynamic.gcc_directory.length, dynamic.gcc_directory.pointer);
        snprintf(system->directories[1], sizeof(system->directories[1]), "-L%.*s", (int)libc.length, libc.pointer);
        snprintf(system->directories[2], sizeof(system->directories[2]), "-L/lib");
        system->found = true;
    }
    runtime_release(&dynamic);
    runtime_release(&static_runtime);
    return system->found;
}

// The command line of a program's objects (`paths`, `count` of them, or with an archive the first and the archive) as
// GCC's driver links it for `kind`; the linkers' output goes to `output`.
static void foreign_benchmark_line(ForeignBenchmarkLine* line, ForeignBenchmarkSystem* system, ForeignBenchmarkKind kind, char** paths, uint32_t count, char* archive, char* output)
{
    *line = (ForeignBenchmarkLine){ 0 };
    const bool static_kind = kind == FOREIGN_BENCHMARK_STATIC;
    char (*objects)[BUSTER_FOREIGN_BENCHMARK_PATH_BYTES] = static_kind ? system->static_objects : system->dynamic;
    foreign_benchmark_add(line, (char*)"-o", false, true);
    foreign_benchmark_add(line, output, false, true);
    if (static_kind)
    {
        foreign_benchmark_add(line, (char*)"-static", true, true);
    }
    else
    {
        foreign_benchmark_add(line, (char*)"-pie", false, true);
        foreign_benchmark_add(line, (char*)"-dynamic-linker", false, true);
        foreign_benchmark_add(line, (char*)"/lib64/ld-linux-x86-64.so.2", false, true);
    }
    for (uint32_t i = 0; i < RUNTIME_OBJECT_END; i += 1)
    {
        foreign_benchmark_add(line, objects[i], true, true);
    }
    const uint32_t loose = archive != 0 ? 1 : count;
    for (uint32_t i = 0; i < loose; i += 1)
    {
        foreign_benchmark_add(line, paths[i], true, true);
    }
    if (archive != 0)
    {
        foreign_benchmark_add(line, archive, true, true);
    }
    for (uint32_t d = 0; d < 3; d += 1)
    {
        foreign_benchmark_add(line, system->directories[d], true, true);
    }
    static char* const dynamic_libraries[] = {
        "-lgcc",
        "--as-needed",
        "-lgcc_s",
        "--no-as-needed",
        "-lc",
        "-lgcc",
        "--as-needed",
        "-lgcc_s",
        "--no-as-needed",
    };
    static char* const static_libraries[] = {
        "--start-group",
        "-lgcc",
        "-lgcc_eh",
        "-lc",
        "--end-group",
    };
    char* const* libraries = static_kind ? static_libraries : dynamic_libraries;
    const uint32_t library_count = static_kind ? BUSTER_ARRAY_LENGTH(static_libraries) : BUSTER_ARRAY_LENGTH(dynamic_libraries);
    for (uint32_t i = 0; i < library_count; i += 1)
    {
        foreign_benchmark_add(line, libraries[i], true, true);
    }
    for (uint32_t i = RUNTIME_OBJECT_END; i < RUNTIME_OBJECTS; i += 1)
    {
        foreign_benchmark_add(line, objects[i], true, true);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// The resolutions

// Resolves a command `runs` times after a warm-up by `path` on `lanes` lanes (pinned to `cpus`), and prints the
// medians: the whole and each phase, the slowest lane's. Returns false if a resolution fails.
static bool foreign_benchmark_resolve(const ForeignCommand* command, ForeignPath path, uint32_t lanes, const uint32_t* cpus, uint32_t runs, Arena* arena)
{
    const uint64_t mark = arena->length;
    uint64_t* totals = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, runs + 1);
    uint64_t* phases = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, (uint64_t)FOREIGN_PHASES * (runs + 1));
    bool result = totals != 0 && phases != 0;
    for (uint32_t run = 0; result && run <= runs; run += 1)
    {
        Foreign foreign;
        const uint64_t start = foreign_benchmark_now();
        const ForeignFailure failure = foreign_run(&foreign, command, path, lanes, cpus);
        const uint64_t elapsed = foreign_benchmark_now() - start;
        result = failure.error == FOREIGN_ERROR_NONE;
        if (result && run != 0)
        {
            totals[run - 1] = elapsed;
            for (uint32_t p = 0; p < FOREIGN_PHASES; p += 1)
            {
                uint64_t slowest = 0;
                for (uint32_t l = 0; l < lanes; l += 1)
                {
                    slowest = foreign.lane_states[l].nanoseconds[p] > slowest ? foreign.lane_states[l].nanoseconds[p] : slowest;
                }
                phases[(uint64_t)p * runs + run - 1] = slowest;
            }
        }
        if (!result)
        {
            printf("  the resolution fails: error %d, input %u, member %u\n", failure.error, failure.input, failure.member);
        }
        foreign_release(&foreign);
    }
    if (result)
    {
        printf("  %-6s %2u lane%s %10.1f us:", path == FOREIGN_PATH_VECTOR ? "vector" : "exact", lanes, lanes == 1 ? " " : "s", foreign_benchmark_median(totals, runs));
        for (uint32_t p = 0; p < FOREIGN_PHASES; p += 1)
        {
            printf(" %s %.1f", foreign_benchmark_phase_names[p], foreign_benchmark_median(phases + (uint64_t)p * runs, runs));
        }
        printf("\n");
    }
    arena_pop_to(arena, mark);
    return result;
}

// What a resolution did, once: its inputs, the objects it loaded (members among them), its rounds, records, names, and
// whether both paths make the same names and loads.
static bool foreign_benchmark_describe(const ForeignCommand* command)
{
    Foreign vector;
    Foreign exact;
    const ForeignFailure vector_failure = foreign_run(&vector, command, FOREIGN_PATH_VECTOR, 1, 0);
    const ForeignFailure exact_failure = foreign_run(&exact, command, FOREIGN_PATH_EXACT, 1, 0);
    bool result = vector_failure.error == FOREIGN_ERROR_NONE && exact_failure.error == FOREIGN_ERROR_NONE && vector.object_count == exact.object_count &&
                  vector.names.count == exact.names.count && vector.round_count == exact.round_count;
    for (uint32_t n = 0; result && n < vector.names.count; n += 1)
    {
        result = vector.names.resolution[n] == exact.names.resolution[n] && vector.names.symbol[n] == exact.names.symbol[n] && vector.names.hash[n] == exact.names.hash[n];
    }
    if (result)
    {
        uint32_t members = 0;
        uint64_t bytes = 0;
        for (uint32_t id = 1; id < vector.object_count; id += 1)
        {
            members += vector.objects[id].member != 0 ? 1 : 0;
            bytes += vector.objects[id].bytes.length;
        }
        const ForeignRound* last = &vector.rounds[vector.round_count - 1];
        printf("  %u inputs, %u objects (%u members, %.1f MB), %u rounds, %u global symbols, %u groups, %u names, %u partitions\n", vector.input_count, vector.object_count - 1,
               members, (double)bytes / 1e6, vector.round_count, last->first_symbol + last->symbol_count, last->first_group + last->group_count, vector.names.count,
               vector.partition_count);
    }
    else
    {
        printf("  the paths' resolutions differ, or one fails (errors %d and %d)\n", vector_failure.error, exact_failure.error);
    }
    foreign_release(&vector);
    foreign_release(&exact);
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The linkers

// A phase's time in a linker's report: mold's "--perf" lines are user, system and real seconds then the name, wild's
// "--time" lines a tree, milliseconds, then the name. Returns microseconds, or -1 if the report has no such line.
static double foreign_benchmark_phase(String8 report, const char* name, bool mold)
{
    double result = -1;
    const uint64_t length = strlen(name);
    uint64_t at = 0;
    while (result < 0 && at < report.length)
    {
        const char* line = report.pointer + at;
        const char* end = memchr(line, '\n', report.length - at);
        const uint64_t line_length = end != 0 ? (uint64_t)(end - line) : report.length - at;
        at += line_length + 1;
        // The name ends the line.
        if (line_length > length && memcmp(line + line_length - length, name, length) == 0 && line[line_length - length - 1] == ' ')
        {
            if (mold)
            {
                double user = 0;
                double system = 0;
                double real = 0;
                result = sscanf(line, "%lf %lf %lf", &user, &system, &real) == 3 ? real * 1e6 : -1;
            }
            else
            {
                // The number before the name.
                const char* number = line + line_length - length - 1;
                while (number > line && number[-1] == ' ')
                {
                    number -= 1;
                }
                while (number > line && ((number[-1] >= '0' && number[-1] <= '9') || number[-1] == '.'))
                {
                    number -= 1;
                }
                double milliseconds = 0;
                result = sscanf(number, "%lf", &milliseconds) == 1 ? milliseconds * 1e3 : -1;
            }
        }
    }
    return result;
}

static int foreign_benchmark_compare_doubles(const void* a, const void* b)
{
    const double x = *(const double*)a;
    const double y = *(const double*)b;
    return (x > y) - (x < y);
}

// mold's and wild's links of the line on one thread: the medians of their phases that read and resolve.
static void foreign_benchmark_linkers(ForeignBenchmarkLine* line, const char* work, Arena* arena)
{
    static const char* const mold_phases[] = {
        "read_input_files",
        "resolve_symbols",
    };
    static const char* const wild_phases[] = {
        "Open input files",
        "Load inputs into symbol DB",
        "Resolve symbols",
        "Resolve alternative symbol definitions",
        "Section resolution",
    };
    char output_path[BUSTER_FOREIGN_BENCHMARK_PATH_BYTES];
    char error_path[BUSTER_FOREIGN_BENCHMARK_PATH_BYTES];
    snprintf(output_path, sizeof(output_path), "%s/linker.out", work);
    snprintf(error_path, sizeof(error_path), "%s/linker.err", work);
    for (uint32_t which = 0; which < 2; which += 1)
    {
        const bool mold = which == 0;
        const char* const* names = mold ? mold_phases : wild_phases;
        const uint32_t name_count = mold ? BUSTER_ARRAY_LENGTH(mold_phases) : BUSTER_ARRAY_LENGTH(wild_phases);
        char* arguments[BUSTER_FOREIGN_BENCHMARK_ARGUMENTS + 8];
        uint32_t count = 0;
        arguments[count++] = (char*)(mold ? "mold" : "wild");
        arguments[count++] = (char*)(mold ? "--perf" : "--time");
        arguments[count++] = (char*)"--no-threads";
        arguments[count++] = (char*)"--no-fork";
        for (uint32_t i = 0; i < line->their_count && count < BUSTER_ARRAY_LENGTH(arguments) - 1; i += 1)
        {
            arguments[count++] = line->theirs[i];
        }
        arguments[count] = 0;
        double samples[8][BUSTER_FOREIGN_BENCHMARK_LINKER_RUNS];
        bool linked = true;
        for (uint32_t run = 0; linked && run <= BUSTER_FOREIGN_BENCHMARK_LINKER_RUNS; run += 1)
        {
            const uint64_t mark = arena->length;
            linked = foreign_benchmark_run(arguments, output_path, error_path) == 0;
            const String8 report = foreign_benchmark_read(arena, mold ? output_path : error_path);
            const String8 other = foreign_benchmark_read(arena, mold ? error_path : output_path);
            for (uint32_t p = 0; linked && run != 0 && p < name_count; p += 1)
            {
                const double time = foreign_benchmark_phase(report, names[p], mold);
                samples[p][run - 1] = time >= 0 ? time : foreign_benchmark_phase(other, names[p], mold);
            }
            arena_pop_to(arena, mark);
        }
        if (linked)
        {
            double sum = 0;
            printf("  %-6s one thread:", mold ? "mold" : "wild");
            for (uint32_t p = 0; p < name_count; p += 1)
            {
                qsort(samples[p], BUSTER_FOREIGN_BENCHMARK_LINKER_RUNS, sizeof(double), foreign_benchmark_compare_doubles);
                const double median = samples[p][BUSTER_FOREIGN_BENCHMARK_LINKER_RUNS / 2];
                sum += median > 0 ? median : 0;
                printf(" %s %.0f us;", names[p], median);
            }
            printf(" together %.0f us\n", sum);
        }
        else
        {
            printf("  %s doesn't link the program\n", mold ? "mold" : "wild");
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// The command

// A comma-separated list of numbers into `values`, up to `capacity`: how many, or 0 if it doesn't parse.
static uint32_t foreign_benchmark_list(const char* text, uint32_t* values, uint32_t capacity)
{
    uint32_t count = 0;
    bool valid = text[0] != 0;
    const char* at = text;
    while (valid && *at != 0 && count < capacity)
    {
        char* end = 0;
        const unsigned long value = strtoul(at, &end, 10);
        valid = end != at && value <= UINT32_MAX && (*end == ',' || *end == 0);
        values[count] = (uint32_t)value;
        count += valid ? 1 : 0;
        at = *end == ',' ? end + 1 : end;
    }
    return valid ? count : 0;
}

int run_foreign_benchmark(int argc, char** argv)
{
    ForeignBenchmarkProgram programs[BUSTER_FOREIGN_BENCHMARK_MAX_PROGRAMS];
    uint32_t program_count = BUSTER_ARRAY_LENGTH(foreign_benchmark_default_programs);
    memcpy(programs, foreign_benchmark_default_programs, sizeof(foreign_benchmark_default_programs));
    uint32_t lane_counts[BUSTER_FOREIGN_BENCHMARK_MAX_LANES] = { 1 };
    uint32_t lane_count_count = 1;
    uint32_t cpus[BUSTER_LANE_MAX_COUNT];
    uint32_t cpu_count = 0;
    uint32_t runs = BUSTER_FOREIGN_BENCHMARK_RUNS;
    uint64_t seed = 1;
    uint32_t filler = 8;
    bool kinds[FOREIGN_BENCHMARK_KINDS] = { true, true, true };
    bool linkers = false;
    bool keep = false;
    const char* base = getenv("TMPDIR") != 0 ? getenv("TMPDIR") : "/tmp";
    bool valid = true;
    for (int i = 1; valid && i < argc; i += 1)
    {
        const bool has_value = i + 1 < argc;
        if (strcmp(argv[i], "--functions") == 0 && has_value)
        {
            uint32_t counts[BUSTER_FOREIGN_BENCHMARK_MAX_PROGRAMS];
            program_count = foreign_benchmark_list(argv[i + 1], counts, BUSTER_FOREIGN_BENCHMARK_MAX_PROGRAMS);
            for (uint32_t p = 0; p < program_count; p += 1)
            {
                programs[p] = (ForeignBenchmarkProgram){
                    .functions = counts[p],
                    .filler = filler,
                };
            }
            valid = program_count != 0;
            i += 1;
        }
        else if (strcmp(argv[i], "--filler") == 0 && has_value)
        {
            filler = (uint32_t)strtoul(argv[i + 1], 0, 10);
            for (uint32_t p = 0; p < program_count; p += 1)
            {
                programs[p].filler = filler;
            }
            i += 1;
        }
        else if (strcmp(argv[i], "--seed") == 0 && has_value)
        {
            seed = strtoull(argv[i + 1], 0, 0);
            i += 1;
        }
        else if (strcmp(argv[i], "--lanes") == 0 && has_value)
        {
            lane_count_count = foreign_benchmark_list(argv[i + 1], lane_counts, BUSTER_FOREIGN_BENCHMARK_MAX_LANES);
            valid = lane_count_count != 0;
            i += 1;
        }
        else if (strcmp(argv[i], "--cpus") == 0 && has_value)
        {
            cpu_count = foreign_benchmark_list(argv[i + 1], cpus, BUSTER_LANE_MAX_COUNT);
            valid = cpu_count != 0;
            i += 1;
        }
        else if (strcmp(argv[i], "--runs") == 0 && has_value)
        {
            runs = (uint32_t)strtoul(argv[i + 1], 0, 10);
            valid = runs != 0;
            i += 1;
        }
        else if (strcmp(argv[i], "--directory") == 0 && has_value)
        {
            base = argv[i + 1];
            i += 1;
        }
        else if (strcmp(argv[i], "--kinds") == 0 && has_value)
        {
            for (uint32_t k = 0; k < FOREIGN_BENCHMARK_KINDS; k += 1)
            {
                kinds[k] = strstr(argv[i + 1], foreign_benchmark_kind_names[k]) != 0;
            }
            i += 1;
        }
        else if (strcmp(argv[i], "--linkers") == 0)
        {
            linkers = true;
        }
        else if (strcmp(argv[i], "--keep") == 0)
        {
            keep = true;
        }
        else
        {
            valid = false;
        }
    }
    for (uint32_t l = 0; valid && l < lane_count_count; l += 1)
    {
        valid = lane_counts[l] >= 1 && lane_counts[l] <= BUSTER_LANE_MAX_COUNT && (cpu_count == 0 || lane_counts[l] <= cpu_count);
    }
    Arena arena = arena_reserve(BUSTER_FOREIGN_BENCHMARK_ARENA_BYTES);
    char work[BUSTER_FOREIGN_BENCHMARK_PATH_BYTES];
    snprintf(work, sizeof(work), "%s/buster-foreign-benchmark-XXXXXX", base);
    ForeignBenchmarkSystem system;
    bool result = valid;
    if (!valid)
    {
        printf("usage: benchmark-foreign [--functions LIST] [--filler N] [--seed N] [--lanes LIST] [--cpus LIST] [--runs N] [--directory PATH] [--kinds LIST] [--linkers] [--keep]\n");
    }
    else if (arena.base == 0 || mkdtemp(work) == 0 || !foreign_benchmark_system(&system))
    {
        printf("benchmark-foreign: no arena, no work directory, or no C runtime where GCC's driver finds it\n");
        result = false;
    }
    char output[BUSTER_FOREIGN_BENCHMARK_PATH_BYTES];
    char archive[BUSTER_FOREIGN_BENCHMARK_PATH_BYTES];
    snprintf(output, sizeof(output), "%s/program", work);
    for (uint32_t p = 0; result && p < program_count; p += 1)
    {
        const uint32_t functions = programs[p].functions;
        LinkProgramOptions options = link_program_default_options();
        options.seed = seed + p;
        options.units = functions < BUSTER_FOREIGN_BENCHMARK_UNIT_FUNCTIONS ? 1 : (functions + BUSTER_FOREIGN_BENCHMARK_UNIT_FUNCTIONS - 1) / BUSTER_FOREIGN_BENCHMARK_UNIT_FUNCTIONS;
        options.functions = functions;
        options.filler = programs[p].filler;
        options.globals = 8;
        options.thread_locals = 2;
        options.absolute = false;
        options.sizes = false;
        options.libc = true;
        LinkProgram program;
        const uint64_t mark = arena.length;
        char** paths = BUSTER_ARENA_PUSH_ARRAY(&arena, char*, options.units + 1);
        bool made = paths != 0 && link_program_generate(&program, options);
        ObjectUnits units;
        ObjectSymbols symbols;
        made = made && object_prepare(&units, &program.link, options.units, &arena) && object_map_symbols(&symbols, &program.link, &arena);
        uint64_t object_bytes = 0;
        for (uint32_t u = 0; made && u < options.units; u += 1)
        {
            paths[u] = arena_push(&arena, BUSTER_FOREIGN_BENCHMARK_PATH_BYTES, 1);
            snprintf(paths[u], BUSTER_FOREIGN_BENCHMARK_PATH_BYTES, "%s/p%u_u%u.o", work, p, u);
            const uint64_t before = arena.length;
            const ObjectOptions object_options = { 0 };
            char source[32];
            snprintf(source, sizeof(source), "u%u.c", u);
            const String8 source_name = {
                .pointer = source,
                .length = strlen(source),
            };
            const Object object = object_build(&program.link, &units, &symbols, u, object_options, source_name, &arena);
            made = object.error == OBJECT_ERROR_NONE && object_write_file(paths[u], object.bytes, object.size) == OBJECT_ERROR_NONE;
            object_bytes += object.size;
            arena_pop_to(&arena, before);
        }
        link_program_release(&program);
        // The members kind's archive: every unit but the first, written by ar.
        snprintf(archive, sizeof(archive), "%s/p%u.a", work, p);
        if (made && kinds[FOREIGN_BENCHMARK_MEMBERS] && options.units > 1)
        {
            char* ar[BUSTER_FOREIGN_BENCHMARK_ARGUMENTS + 4];
            uint32_t count = 0;
            ar[count++] = (char*)"ar";
            ar[count++] = (char*)"rcs";
            ar[count++] = archive;
            for (uint32_t u = 1; u < options.units && count < BUSTER_ARRAY_LENGTH(ar) - 1; u += 1)
            {
                ar[count++] = paths[u];
            }
            ar[count] = 0;
            unlink(archive);
            made = foreign_benchmark_run(ar, "/dev/null", "/dev/null") == 0;
        }
        printf("%u workers in %u units, %.1f MB of objects:\n", functions, options.units, (double)object_bytes / 1e6);
        result = made;
        for (uint32_t k = 0; result && k < FOREIGN_BENCHMARK_KINDS; k += 1)
        {
            if (!kinds[k] || (k == FOREIGN_BENCHMARK_MEMBERS && options.units == 1))
            {
                continue;
            }
            ForeignBenchmarkLine* line = BUSTER_ARENA_PUSH_ARRAY(&arena, ForeignBenchmarkLine, 1);
            ForeignCommand command;
            result = line != 0;
            if (result)
            {
                foreign_benchmark_line(line, &system, (ForeignBenchmarkKind)k, paths, options.units, k == FOREIGN_BENCHMARK_MEMBERS ? archive : 0, output);
                result = foreign_arguments(&command, (int)line->our_count, line->ours, &arena).error == FOREIGN_ERROR_NONE;
            }
            if (result)
            {
                printf(" %s:\n", foreign_benchmark_kind_names[k]);
                result = foreign_benchmark_describe(&command);
            }
            for (uint32_t l = 0; result && l < lane_count_count; l += 1)
            {
                for (uint32_t path = 0; result && path < 2; path += 1)
                {
                    result = foreign_benchmark_resolve(&command, path == 0 ? FOREIGN_PATH_VECTOR : FOREIGN_PATH_EXACT, lane_counts[l], cpu_count != 0 ? cpus : 0, runs, &arena);
                }
            }
            if (result && linkers)
            {
                foreign_benchmark_linkers(line, work, &arena);
            }
        }
        arena_pop_to(&arena, mark);
    }
    if (valid && !keep && work[0] != 0)
    {
        char* remove[] = {
            (char*)"rm",
            (char*)"-rf",
            work,
            0,
        };
        foreign_benchmark_run(remove, "/dev/null", "/dev/null");
    }
    else if (keep)
    {
        printf("benchmark-foreign: kept %s\n", work);
    }
    arena_release(&arena);
    return result ? 0 : 1;
}
