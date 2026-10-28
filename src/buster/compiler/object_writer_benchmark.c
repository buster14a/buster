// SPDX-License-Identifier: (Apache-2.0 WITH LLVM-exception) AND MIT
// Original material Copyright (c) 2026 David Gonzalez Martin.
// Counter/timing/sort helpers adapted through c_lexer_benchmark.c retain MIT and the upstream notice:
// Copyright (c) 2023 Niles Salter. See THIRD_PARTY_NOTICES.md and LICENSES/Accelerated-Zig-Parser.txt.
//
// The object writer's benchmark: what an object costs, from the link representation to its file, against what writing
// the same bytes costs alone: the file-write floor (docs/object-writer-and-linker.md, "The floors").
//
// Usage: build/compiler benchmark-object-writer [--units N] [--functions N] [--filler N] [--function-sections]
//                                               [--directory DIR] [--seed N]
//
// It generates a program with link_program_generate: --units units (BUSTER_OBJECT_BENCHMARK_DEFAULT_UNITS by default),
// each with --functions workers (BUSTER_OBJECT_BENCHMARK_DEFAULT_FUNCTIONS) of up to --filler filler instructions, and
// 8 globals, 2 thread-locals, 20 strings, an init and a fini function each: by default about one of synth's
// translation units per unit (68 KB of code, 2,000 relocations and 1,400 symbols in the document). It prepares the units
// once (object_prepare) and checks that each object comes out the same twice. Then, after a warm-up pass,
// BUSTER_OBJECT_BENCHMARK_PASSES passes, each of these in turn:
//   - prepare: object_prepare, which a link runs once for all its units' objects;
//   - build: every unit's object built with object_build, one after another into the same arena, the pass timed whole,
//     with cycles and instructions from the hardware counters (zero when unavailable);
//   - files: for each unit in turn, its object built, then written in --directory ($TMPDIR or /tmp by default) four
//     ways, each timed: object_write_file over the file from the pass before (in place), the floor in place (open,
//     pwrite over the file, which keeps its size, and close), unlink and object_write_file (a new file), and the floor
//     for a new file (unlink, open with O_CREAT | O_EXCL, pwrite and close). The object's bytes are where object_build
//     left them, as in a compile, which writes each object once it is built. Each way writes files of its own, so that
//     every way's file was last written by the same way a pass before, and the ways take turns going first: a write
//     costs twice as much when the page cache's pages are no longer in the CPU's caches, so a way that rewrote the
//     pages another way had just written would look cheaper than it is.
// It reports per object the median pass's time divided by the units, and for the files the build plus the write
// against the floor. Run it pinned to one CPU (taskset -c 2 ...), and with --directory on each file system to measure.
// The exit status is 1 if the arguments are invalid, or an object can't be built, comes out differently twice or can't
// be written.
//
// Layout, in this order: the clock and the hardware counters, the floors, and run_object_writer_benchmark.

#include "host_isa.h"
#if BUSTER_HOST_VECTOR
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// OS-dependent includes
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/perf_event.h>
#include <sys/syscall.h>

typedef struct String8 String8;
struct String8
{
    char* pointer;
    uint64_t length;
};

// The encoder's other entry points serve its own tests and benchmark: the test programs use x86_encode alone.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "buster/compiler/x86_64_encoder.c"
#pragma clang diagnostic pop
#include "buster/compiler/lane.c"
#include "buster/compiler/link.c"
#include "buster/compiler/link_program.c"
#include "buster/compiler/object_writer.c"

#define BUSTER_OBJECT_BENCHMARK_DEFAULT_UNITS 64
#define BUSTER_OBJECT_BENCHMARK_DEFAULT_FUNCTIONS 200
#define BUSTER_OBJECT_BENCHMARK_DEFAULT_FILLER 40
#define BUSTER_OBJECT_BENCHMARK_PASSES 21
#define BUSTER_OBJECT_BENCHMARK_ARENA_BYTES (1ull << 36)
#define BUSTER_OBJECT_BENCHMARK_PATH_BYTES 4096
// The ways the files passes write an object (object_benchmark_write).
#define BUSTER_OBJECT_BENCHMARK_WAYS 4

// ---------------------------------------------------------------------------------------------------------------------
// The clock and the hardware counters

static uint64_t object_benchmark_now(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

// A counter of this thread's user-space hardware events, or -1 if perf_event_open fails.
static int object_benchmark_open_counter(uint64_t config)
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

static uint64_t object_benchmark_read_counter(int fd)
{
    uint64_t value = 0;
    if (fd >= 0 && read(fd, &value, sizeof(value)) != sizeof(value))
    {
        value = 0;
    }
    return value;
}

static void object_benchmark_sort(uint64_t* values, uint64_t count)
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

static double object_benchmark_median(const uint64_t* values)
{
    uint64_t sorted[BUSTER_OBJECT_BENCHMARK_PASSES];
    memcpy(sorted, values, sizeof(sorted));
    object_benchmark_sort(sorted, BUSTER_OBJECT_BENCHMARK_PASSES);
    return (double)sorted[BUSTER_OBJECT_BENCHMARK_PASSES / 2];
}

// ---------------------------------------------------------------------------------------------------------------------
// The floors: the system calls of writing the bytes alone, as docs/object-writer-and-linker.md measured them.

// pwrite until every byte is written.
static bool object_benchmark_pwrite(int fd, const uint8_t* bytes, uint64_t size)
{
    uint64_t written = 0;
    bool result = true;
    while (result && written < size)
    {
        const ssize_t count = pwrite(fd, bytes + written, size - written, (off_t)written);
        result = count > 0;
        written += result ? (uint64_t)count : 0;
    }
    return result;
}

// In place: open, pwrite over a file of the same size, and close.
static bool object_benchmark_floor_in_place(const char* path, const uint8_t* bytes, uint64_t size)
{
    const int fd = open(path, O_WRONLY | O_CLOEXEC);
    bool result = fd != -1 && object_benchmark_pwrite(fd, bytes, size);
    result = fd != -1 && close(fd) == 0 && result;
    return result;
}

// A new file: unlink, open with O_CREAT | O_EXCL, pwrite and close.
static bool object_benchmark_floor_new(const char* path, const uint8_t* bytes, uint64_t size)
{
    unlink(path);
    const int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
    bool result = fd != -1 && object_benchmark_pwrite(fd, bytes, size);
    result = fd != -1 && close(fd) == 0 && result;
    return result;
}

// One of the ways the files passes write an object: object_write_file over the file (in place), the floor in place,
// unlink and object_write_file (a new file), or the floor for a new file.
static bool object_benchmark_write(uint32_t way, const char* path, const uint8_t* bytes, uint64_t size)
{
    bool result = false;
    switch (way)
    {
        case 0: result = object_write_file(path, bytes, size) == OBJECT_ERROR_NONE; break;
        case 1: result = object_benchmark_floor_in_place(path, bytes, size); break;
        case 2: result = unlink(path) == 0 && object_write_file(path, bytes, size) == OBJECT_ERROR_NONE; break;
        default: result = object_benchmark_floor_new(path, bytes, size); break;
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The command

static bool object_benchmark_parse(const char* text, uint64_t* value)
{
    char* end = 0;
    const unsigned long long parsed = strtoull(text, &end, 0);
    const bool result = end != text && *end == 0;
    *value = result ? parsed : *value;
    return result;
}

// A unit's source name, "u<unit>.c", in `buffer`.
static String8 object_benchmark_source_name(char* buffer, uint64_t size, uint32_t unit)
{
    const int length = snprintf(buffer, size, "u%u.c", unit);
    String8 result = {
        .pointer = buffer,
        .length = length > 0 ? (uint64_t)length : 0,
    };
    return result;
}

int run_object_writer_benchmark(int argc, char** argv);

int run_object_writer_benchmark(int argc, char** argv)
{
    uint64_t units_count = BUSTER_OBJECT_BENCHMARK_DEFAULT_UNITS;
    uint64_t functions = BUSTER_OBJECT_BENCHMARK_DEFAULT_FUNCTIONS;
    uint64_t filler = BUSTER_OBJECT_BENCHMARK_DEFAULT_FILLER;
    uint64_t seed = 1;
    bool function_sections = false;
    const char* temporary = getenv("TMPDIR");
    const char* directory = temporary != 0 && temporary[0] != 0 ? temporary : "/tmp";
    bool valid = true;
    for (int i = 1; i < argc; i += 1)
    {
        if (strcmp(argv[i], "--units") == 0 && i + 1 < argc)
        {
            valid = valid && object_benchmark_parse(argv[i + 1], &units_count) && units_count >= 1 && units_count <= BUSTER_LINK_PROGRAM_MAX_COUNT;
            i += 1;
        }
        else if (strcmp(argv[i], "--functions") == 0 && i + 1 < argc)
        {
            valid = valid && object_benchmark_parse(argv[i + 1], &functions) && functions >= 1;
            i += 1;
        }
        else if (strcmp(argv[i], "--filler") == 0 && i + 1 < argc)
        {
            valid = valid && object_benchmark_parse(argv[i + 1], &filler) && filler <= BUSTER_LINK_PROGRAM_MAX_FILLER;
            i += 1;
        }
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
        {
            valid = valid && object_benchmark_parse(argv[i + 1], &seed);
            i += 1;
        }
        else if (strcmp(argv[i], "--directory") == 0 && i + 1 < argc)
        {
            directory = argv[i + 1];
            i += 1;
        }
        else if (strcmp(argv[i], "--function-sections") == 0)
        {
            function_sections = true;
        }
        else
        {
            valid = false;
        }
    }
    // The program's counts: functions per unit, and per unit 8 globals, 2 thread-locals, 20 strings and a hook of each.
    valid = valid && units_count * functions <= BUSTER_LINK_PROGRAM_MAX_COUNT && units_count * 20 <= BUSTER_LINK_PROGRAM_MAX_COUNT;
    LinkProgramOptions options = link_program_default_options();
    options.seed = seed;
    options.units = (uint32_t)units_count;
    options.functions = (uint32_t)(units_count * functions);
    options.globals = (uint32_t)(units_count * 8);
    options.thread_locals = (uint32_t)(units_count * 2);
    options.strings = 20;
    options.init_functions = (uint32_t)units_count;
    options.fini_functions = (uint32_t)units_count;
    options.filler = (uint32_t)filler;
    const ObjectOptions object_options = {
        .function_sections = function_sections,
    };

    int result = 1;
    LinkProgram program = { 0 };
    Arena arena = arena_reserve(BUSTER_OBJECT_BENCHMARK_ARENA_BYTES);
    ObjectUnits units = { 0 };
    ObjectSymbols object_symbols = { 0 };
    char* paths = 0;
    if (!valid)
    {
        fprintf(stderr, "usage: %s [--units N] [--functions N] [--filler N] [--function-sections] [--directory DIR] [--seed N]\n", argv[0]);
    }
    else if (!link_program_generate(&program, options))
    {
        fprintf(stderr, "link_program_generate: %s\n", program.problem);
    }
    else if (arena.base == 0 || !object_prepare(&units, &program.link, options.units, &arena) || !object_map_symbols(&object_symbols, &program.link, &arena) ||
             (paths = arena_push(&arena, (uint64_t)units.count * BUSTER_OBJECT_BENCHMARK_WAYS * BUSTER_OBJECT_BENCHMARK_PATH_BYTES, 1)) == 0)
    {
        fprintf(stderr, "out of memory\n");
    }
    else
    {
        // Each unit's paths, one per way, and a check that each object comes out the same twice, with its sizes for the
        // report.
        uint64_t object_bytes = 0;
        uint64_t sections = 0;
        uint64_t symbols = 0;
        uint64_t relocations = 0;
        uint64_t atom_bytes = 0;
        bool built = true;
        for (uint32_t unit = 0; built && unit < units.count; unit += 1)
        {
            const uint64_t length = arena.length;
            char source[32];
            const String8 source_name = object_benchmark_source_name(source, sizeof(source), unit);
            const Object first = object_build(&program.link, &units, &object_symbols, unit, object_options, source_name, &arena);
            const Object second = object_build(&program.link, &units, &object_symbols, unit, object_options, source_name, &arena);
            built = first.error == OBJECT_ERROR_NONE && second.error == OBJECT_ERROR_NONE && first.size == second.size && memcmp(first.bytes, second.bytes, first.size) == 0;
            for (uint32_t way = 0; built && way < BUSTER_OBJECT_BENCHMARK_WAYS; way += 1)
            {
                char* path = paths + ((uint64_t)unit * BUSTER_OBJECT_BENCHMARK_WAYS + way) * BUSTER_OBJECT_BENCHMARK_PATH_BYTES;
                snprintf(path, BUSTER_OBJECT_BENCHMARK_PATH_BYTES, "%s/buster-object-writer-benchmark-%d-u%u-%u.o", directory, (int)getpid(), unit, way);
                built = object_write_file(path, first.bytes, first.size) == OBJECT_ERROR_NONE;
            }
            object_bytes += first.size;
            sections += first.section_count;
            symbols += first.symbol_count;
            relocations += first.relocation_count;
            arena_pop_to(&arena, length);
        }
        for (AtomId atom = 1; atom < program.link.atoms.count; atom += 1)
        {
            atom_bytes += program.link.atoms.size[atom];
        }
        if (!built)
        {
            fprintf(stderr, "an object can't be built, comes out differently twice or can't be written to %s\n", directory);
        }
        else
        {
            const int cycles_fd = object_benchmark_open_counter(PERF_COUNT_HW_CPU_CYCLES);
            const int instructions_fd = object_benchmark_open_counter(PERF_COUNT_HW_INSTRUCTIONS);
            if (cycles_fd < 0 || instructions_fd < 0)
            {
                fprintf(stderr, "perf_event_open failed (see /proc/sys/kernel/perf_event_paranoid): cycles and instructions read as 0\n");
            }
            static uint64_t prepare_nanoseconds[BUSTER_OBJECT_BENCHMARK_PASSES];
            static uint64_t build_nanoseconds[BUSTER_OBJECT_BENCHMARK_PASSES];
            static uint64_t build_cycles[BUSTER_OBJECT_BENCHMARK_PASSES];
            static uint64_t build_instructions[BUSTER_OBJECT_BENCHMARK_PASSES];
            static uint64_t pipeline_nanoseconds[BUSTER_OBJECT_BENCHMARK_PASSES];
            static uint64_t ways[BUSTER_OBJECT_BENCHMARK_WAYS][BUSTER_OBJECT_BENCHMARK_PASSES];
            bool written = true;
            for (uint64_t pass = 0; written && pass <= BUSTER_OBJECT_BENCHMARK_PASSES; pass += 1)
            {
                // Prepare, as every link does once, into the arena it then gives back.
                const uint64_t length = arena.length;
                ObjectUnits prepared;
                const uint64_t prepare_begin = object_benchmark_now();
                written = object_prepare(&prepared, &program.link, options.units, &arena);
                const uint64_t prepare_end = object_benchmark_now();
                arena_pop_to(&arena, length);
                // Build: every object, one after another, into the same arena.
                const uint64_t cycles_before = object_benchmark_read_counter(cycles_fd);
                const uint64_t instructions_before = object_benchmark_read_counter(instructions_fd);
                const uint64_t begin = object_benchmark_now();
                for (uint32_t unit = 0; unit < units.count; unit += 1)
                {
                    char source[32];
                    const Object object = object_build(&program.link, &units, &object_symbols, unit, object_options, object_benchmark_source_name(source, sizeof(source), unit), &arena);
                    written = written && object.error == OBJECT_ERROR_NONE;
                    arena_pop_to(&arena, length);
                }
                const uint64_t end = object_benchmark_now();
                const uint64_t cycles_after = object_benchmark_read_counter(cycles_fd);
                const uint64_t instructions_after = object_benchmark_read_counter(instructions_fd);
                // Files: each object built, then written each way, the ways in turn going first.
                uint64_t pipeline = 0;
                uint64_t totals[BUSTER_OBJECT_BENCHMARK_WAYS] = { 0 };
                for (uint32_t unit = 0; written && unit < units.count; unit += 1)
                {
                    char source[32];
                    const uint64_t build_begin = object_benchmark_now();
                    const Object object = object_build(&program.link, &units, &object_symbols, unit, object_options, object_benchmark_source_name(source, sizeof(source), unit), &arena);
                    pipeline += object_benchmark_now() - build_begin;
                    written = written && object.error == OBJECT_ERROR_NONE;
                    for (uint32_t turn = 0; written && turn < BUSTER_OBJECT_BENCHMARK_WAYS; turn += 1)
                    {
                        const uint32_t way = (uint32_t)((unit + pass + turn) % BUSTER_OBJECT_BENCHMARK_WAYS);
                        const char* path = paths + ((uint64_t)unit * BUSTER_OBJECT_BENCHMARK_WAYS + way) * BUSTER_OBJECT_BENCHMARK_PATH_BYTES;
                        const uint64_t write_begin = object_benchmark_now();
                        written = object_benchmark_write(way, path, object.bytes, object.size);
                        totals[way] += object_benchmark_now() - write_begin;
                    }
                    arena_pop_to(&arena, length);
                }
                if (pass > 0)
                {
                    prepare_nanoseconds[pass - 1] = prepare_end - prepare_begin;
                    build_nanoseconds[pass - 1] = end - begin;
                    build_cycles[pass - 1] = cycles_after - cycles_before;
                    build_instructions[pass - 1] = instructions_after - instructions_before;
                    pipeline_nanoseconds[pass - 1] = pipeline;
                    for (uint32_t way = 0; way < BUSTER_OBJECT_BENCHMARK_WAYS; way += 1)
                    {
                        ways[way][pass - 1] = totals[way];
                    }
                }
            }
            if (!written)
            {
                fprintf(stderr, "an object can't be built or written to %s\n", directory);
            }
            else
            {
                const double count = (double)units.count;
                const double average_bytes = (double)object_bytes / count;
                const double build = object_benchmark_median(build_nanoseconds) / count;
                const double cycles = object_benchmark_median(build_cycles) / count;
                const double instructions = object_benchmark_median(build_instructions) / count;
                const double in_place = object_benchmark_median(ways[0]) / count;
                const double floor_in_place = object_benchmark_median(ways[1]) / count;
                const double new_file = object_benchmark_median(ways[2]) / count;
                const double floor_new = object_benchmark_median(ways[3]) / count;
                const double pipeline_build = object_benchmark_median(pipeline_nanoseconds) / count;
                printf("%u objects%s in %s: per object on average %.1f KB of atoms (%.1f KB of code), %.0f relocations, %.0f symbols, %.0f sections and %.1f KB written; "
                       "%d passes, medians per object:\n",
                       units.count, function_sections ? " with function sections" : "", directory, (double)atom_bytes / count / 1024.0,
                       (double)program.code_bytes / count / 1024.0, (double)relocations / count, (double)symbols / count, (double)sections / count, average_bytes / 1024.0,
                       BUSTER_OBJECT_BENCHMARK_PASSES);
                const double prepare = object_benchmark_median(prepare_nanoseconds);
                printf("  prepare, once for the link    %8.2f µs  (%.2f µs per object)\n", prepare / 1000.0, prepare / count / 1000.0);
                printf("  build (count, place, fill)    %8.2f µs  %6.2f GB/s  %6.2f ns per relocation  %9.0f cycles  %9.0f instructions  IPC %.2f\n", build / 1000.0,
                       average_bytes / build, build / ((double)relocations / count), cycles, instructions, cycles != 0 ? instructions / cycles : 0.0);
                printf("  build, then write it:         %8.2f µs  (the build alone, just before each write)\n", pipeline_build / 1000.0);
                printf("  write in place                %8.2f µs  floor %8.2f µs  (+%.2f µs)\n", in_place / 1000.0, floor_in_place / 1000.0, (in_place - floor_in_place) / 1000.0);
                printf("  write a new file              %8.2f µs  floor %8.2f µs  (+%.2f µs)\n", new_file / 1000.0, floor_new / 1000.0, (new_file - floor_new) / 1000.0);
                printf("  object in place in all        %8.2f µs  %5.2fx the floor\n", (pipeline_build + in_place) / 1000.0, (pipeline_build + in_place) / floor_in_place);
                printf("  object in a new file in all   %8.2f µs  %5.2fx the floor\n", (pipeline_build + new_file) / 1000.0, (pipeline_build + new_file) / floor_new);
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
        for (uint64_t path = 0; path < (uint64_t)units.count * BUSTER_OBJECT_BENCHMARK_WAYS; path += 1)
        {
            unlink(paths + path * BUSTER_OBJECT_BENCHMARK_PATH_BYTES);
        }
    }
    arena_release(&arena);
    link_program_release(&program);
    return result;
}

#else
int run_object_writer_benchmark(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    return 1;
}
#endif // BUSTER_HOST_VECTOR
