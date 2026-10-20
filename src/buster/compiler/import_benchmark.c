// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The import path's benchmark: what reading files into the link representation costs on each reader's vector path and
// on its exact path (docs/object-writer-and-linker.md, "Step 5's phases, data-parallel").
//
// Usage: build/compiler benchmark-import [--units N] [--functions N] [--seed N] [--library PATH]... [--archive PATH]...
//
// Run it under the 9700X's lock pinned to one CPU (taskset -c 2 ...). The object cases:
//   - program: the objects the object writer makes of a generated program of --units units (64 by default) of
//     --functions workers each (200), like benchmark-object-writer's: about one of synth's translation units each;
//   - function sections: the same objects with a section per function;
//   - many sections: one unit of 33,000 functions in sections of their own, past SHN_LORESERVE sections;
//   - runtime objects: this system's crt objects and libc_nonshared.a's members, as runtime_import finds them.
// Each run imports every object of its case into a link of its own (import_object_capacity's room), the paths taking
// turns, and checks that both make the same records: the time per object, and per record (each symbol table entry and
// relocation). Then the files: this system's libc.so.6 and each --library, read by import_library's two paths, per
// .dynsym entry; libc_nonshared.a and each --archive, by import_archive's two paths, per index entry; and for each
// library, the exports' index (import_library_index) against the open-addressing table it replaced, built and then
// looked up for every export's name and every reference's; libc.so's script, which only the exact path reads; and
// runtime_import end to end, as a link without the snapshot would run it. Each figure is the median of BUSTER_IMPORT_BENCHMARK_RUNS runs after a warm-up, with cycles and instructions from the
// hardware counters (zero when unavailable).
// The exit status is 1 if the arguments are invalid, a case or a file can't be made or read, or the paths disagree.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

// OS-dependent includes
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <linux/perf_event.h>
#include <sys/syscall.h>

typedef struct String8 String8;
struct String8
{
    char* pointer;
    uint64_t length;
};

#ifndef BUSTER_ARRAY_LENGTH
#define BUSTER_ARRAY_LENGTH(x) (sizeof(x) / sizeof((x)[0]))
#endif

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

#define BUSTER_IMPORT_BENCHMARK_DEFAULT_UNITS 64
#define BUSTER_IMPORT_BENCHMARK_DEFAULT_FUNCTIONS 200
#define BUSTER_IMPORT_BENCHMARK_MANY_FUNCTIONS 33000
#define BUSTER_IMPORT_BENCHMARK_RUNS 21
#define BUSTER_IMPORT_BENCHMARK_MAX_OBJECTS 4096
#define BUSTER_IMPORT_BENCHMARK_ARENA_BYTES (1ull << 36)
#define BUSTER_IMPORT_BENCHMARK_MAX_FILES 16

typedef enum ImportBenchmarkPath
{
    IMPORT_BENCHMARK_VECTOR,
    IMPORT_BENCHMARK_EXACT,
    IMPORT_BENCHMARK_PATHS,
} ImportBenchmarkPath;

static const char* const import_benchmark_path_names[IMPORT_BENCHMARK_PATHS] = {
    [IMPORT_BENCHMARK_VECTOR] = "vector",
    [IMPORT_BENCHMARK_EXACT] = "exact",
};

// A case: its objects' bytes, and what they hold.
typedef struct ImportBenchmarkCase ImportBenchmarkCase;
struct ImportBenchmarkCase
{
    const char* name;
    String8 objects[BUSTER_IMPORT_BENCHMARK_MAX_OBJECTS];
    uint32_t object_count;
    LinkCapacity capacity;
    // Symbol table entries and relocations, over the objects.
    uint64_t records;
};

// One run's figures.
typedef struct ImportBenchmarkRun ImportBenchmarkRun;
struct ImportBenchmarkRun
{
    uint64_t nanoseconds;
    uint64_t cycles;
    uint64_t instructions;
};

// ---------------------------------------------------------------------------------------------------------------------
// The clock and the hardware counters

static uint64_t import_benchmark_now(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

// A counter of `config` (PERF_COUNT_HW_*) for the calling thread in user space, or -1 where perf_event_open refuses.
static int import_benchmark_counter(uint64_t config)
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

static uint64_t import_benchmark_count(int counter)
{
    uint64_t result = 0;
    if (counter < 0 || read(counter, &result, sizeof(result)) != (ssize_t)sizeof(result))
    {
        result = 0;
    }
    return result;
}

static int import_benchmark_compare(const void* a, const void* b)
{
    const uint64_t x = *(const uint64_t*)a;
    const uint64_t y = *(const uint64_t*)b;
    return (x > y) - (x < y);
}

static uint64_t import_benchmark_median(uint64_t* values, uint64_t count)
{
    qsort(values, count, sizeof(uint64_t), import_benchmark_compare);
    return values[count / 2];
}

// ---------------------------------------------------------------------------------------------------------------------
// The cases

// Adds an object to a case, a copy of its bytes in `arena`, and its records and room.
static bool import_benchmark_add(ImportBenchmarkCase* benchmark, const uint8_t* bytes, uint64_t size, Arena* arena)
{
    LinkCapacity capacity;
    char* copy = arena_push(arena, size, 64);
    bool result = copy != 0 && benchmark->object_count < BUSTER_IMPORT_BENCHMARK_MAX_OBJECTS;
    String8 object = {
        .pointer = copy,
        .length = size,
    };
    if (result)
    {
        memcpy(copy, bytes, size);
        result = import_object_capacity(object, &capacity).error == IMPORT_ERROR_NONE;
    }
    if (result)
    {
        benchmark->objects[benchmark->object_count] = object;
        benchmark->object_count += 1;
        benchmark->capacity.names += capacity.names;
        benchmark->capacity.atoms += capacity.atoms;
        benchmark->capacity.symbols += capacity.symbols;
        benchmark->capacity.fixups += capacity.fixups;
        benchmark->capacity.wide_addends += capacity.wide_addends;
        benchmark->records += (uint64_t)capacity.symbols + capacity.fixups;
    }
    return result;
}

// The objects of a generated program's units.
static bool import_benchmark_program(ImportBenchmarkCase* benchmark, LinkProgramOptions options, bool function_sections, Arena* arena)
{
    LinkProgram program;
    ObjectUnits units;
    ObjectSymbols symbols;
    Scratch scratch = scratch_begin(arena);
    bool result = link_program_generate(&program, options);
    result = result && object_prepare(&units, &program.link, options.units, scratch.arena) && object_map_symbols(&symbols, &program.link, scratch.arena);
    const ObjectOptions object_options = {
        .function_sections = function_sections,
    };
    for (uint32_t unit = 0; result && unit < units.count; unit += 1)
    {
        char source[64];
        snprintf(source, sizeof(source), "unit_%u.c", unit);
        const String8 source_name = {
            .pointer = source,
            .length = strlen(source),
        };
        const uint64_t position = scratch.arena->length;
        const Object built = object_build(&program.link, &units, &symbols, unit, object_options, source_name, scratch.arena);
        result = built.error == OBJECT_ERROR_NONE && import_benchmark_add(benchmark, built.bytes, built.size, arena);
        arena_pop_to(scratch.arena, position);
    }
    link_program_release(&program);
    scratch_end(scratch);
    return result;
}

// This system's crt objects and libc_nonshared.a's members.
static bool import_benchmark_runtime(ImportBenchmarkCase* benchmark, Runtime* runtime, Arena* arena)
{
    bool result = runtime_import(runtime) == RUNTIME_ERROR_NONE;
    for (uint32_t i = 0; result && i < RUNTIME_OBJECTS; i += 1)
    {
        result = import_benchmark_add(benchmark, (const uint8_t*)runtime->object_bytes[i].pointer, runtime->object_bytes[i].length, arena);
    }
    for (uint32_t m = 0; result && m < runtime->nonshared.member_count; m += 1)
    {
        const String8 member = runtime->nonshared.members[m].bytes;
        result = import_benchmark_add(benchmark, (const uint8_t*)member.pointer, member.length, arena);
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Runs

// Imports every object of the case on one path into `link`, made with the case's room from `arena`.
static bool import_benchmark_import(const ImportBenchmarkCase* benchmark, ImportBenchmarkPath path, Link* link, Arena* arena)
{
    bool result = link_initialize(link, arena, arena, benchmark->capacity);
    for (uint32_t i = 0; result && i < benchmark->object_count; i += 1)
    {
        ImportObject object;
        if (path == IMPORT_BENCHMARK_VECTOR)
        {
            result = import_object_vector(link, benchmark->objects[i], i, BUSTER_LINK_SOURCE_FOREIGN_OBJECT, arena, &object);
        }
        else
        {
            result = import_object_scalar(link, benchmark->objects[i], i, BUSTER_LINK_SOURCE_FOREIGN_OBJECT, arena, &object).error == IMPORT_ERROR_NONE;
        }
    }
    return result;
}

// Whether two links hold the same records, every column of every table.
static bool import_benchmark_same(const Link* a, const Link* b)
{
    bool result = a->names.count == b->names.count && a->atoms.count == b->atoms.count && a->symbols.count == b->symbols.count && a->fixups.count == b->fixups.count &&
                  a->fixups.wide_count == b->fixups.wide_count;
    for (NameId name = 1; result && name < a->names.count; name += 1)
    {
        result = a->names.length[name] == b->names.length[name] && a->names.hash[name] == b->names.hash[name] &&
                 memcmp(a->names.bytes[name], b->names.bytes[name], (uint64_t)a->names.length[name] + 1) == 0;
    }
    const uint64_t atoms = a->atoms.count;
    const uint64_t symbols = a->symbols.count;
    const uint64_t fixups = a->fixups.count;
    result = result && memcmp(a->atoms.class, b->atoms.class, atoms) == 0 && memcmp(a->atoms.alignment_log2, b->atoms.alignment_log2, atoms) == 0 &&
             memcmp(a->atoms.unit, b->atoms.unit, atoms * sizeof(uint32_t)) == 0 && memcmp(a->atoms.size, b->atoms.size, atoms * sizeof(uint64_t)) == 0 &&
             memcmp(a->atoms.bytes, b->atoms.bytes, atoms * sizeof(uint8_t*)) == 0 && memcmp(a->atoms.fixup_begin, b->atoms.fixup_begin, atoms * sizeof(FixupId)) == 0 &&
             memcmp(a->atoms.fixup_count, b->atoms.fixup_count, atoms * sizeof(uint32_t)) == 0;
    result = result && memcmp(a->symbols.name, b->symbols.name, symbols * sizeof(NameId)) == 0 && memcmp(a->symbols.atom, b->symbols.atom, symbols * sizeof(AtomId)) == 0 &&
             memcmp(a->symbols.value, b->symbols.value, symbols * sizeof(uint64_t)) == 0 && memcmp(a->symbols.size, b->symbols.size, symbols * sizeof(uint64_t)) == 0 &&
             memcmp(a->symbols.flags, b->symbols.flags, symbols) == 0 && memcmp(a->symbols.source, b->symbols.source, symbols) == 0;
    result = result && memcmp(a->fixups.offset, b->fixups.offset, fixups * sizeof(uint32_t)) == 0 && memcmp(a->fixups.target, b->fixups.target, fixups * sizeof(SymbolId)) == 0 &&
             memcmp(a->fixups.addend, b->fixups.addend, fixups * sizeof(int32_t)) == 0 && memcmp(a->fixups.kind, b->fixups.kind, fixups) == 0 &&
             memcmp(a->fixups.wide_addends, b->fixups.wide_addends, (uint64_t)a->fixups.wide_count * sizeof(int64_t)) == 0;
    return result;
}

// The case's runs, the paths taking turns, after a warm-up run of each that checks they agree.
static bool import_benchmark_case(const ImportBenchmarkCase* benchmark, Arena* arena)
{
    const uint64_t position = arena->length;
    Link vector;
    Link exact;
    bool result = import_benchmark_import(benchmark, IMPORT_BENCHMARK_VECTOR, &vector, arena) && import_benchmark_import(benchmark, IMPORT_BENCHMARK_EXACT, &exact, arena);
    result = result && import_benchmark_same(&vector, &exact);
    arena_pop_to(arena, position);
    const int cycles_counter = import_benchmark_counter(PERF_COUNT_HW_CPU_CYCLES);
    const int instructions_counter = import_benchmark_counter(PERF_COUNT_HW_INSTRUCTIONS);
    uint64_t nanoseconds[IMPORT_BENCHMARK_PATHS][BUSTER_IMPORT_BENCHMARK_RUNS];
    uint64_t cycles[IMPORT_BENCHMARK_PATHS][BUSTER_IMPORT_BENCHMARK_RUNS];
    uint64_t instructions[IMPORT_BENCHMARK_PATHS][BUSTER_IMPORT_BENCHMARK_RUNS];
    for (uint32_t run = 0; result && run < BUSTER_IMPORT_BENCHMARK_RUNS; run += 1)
    {
        for (uint32_t turn = 0; result && turn < IMPORT_BENCHMARK_PATHS; turn += 1)
        {
            const ImportBenchmarkPath path = (ImportBenchmarkPath)((run + turn) % IMPORT_BENCHMARK_PATHS);
            Link link;
            const uint64_t cycles_before = import_benchmark_count(cycles_counter);
            const uint64_t instructions_before = import_benchmark_count(instructions_counter);
            const uint64_t start = import_benchmark_now();
            result = import_benchmark_import(benchmark, path, &link, arena);
            const uint64_t end = import_benchmark_now();
            cycles[path][run] = import_benchmark_count(cycles_counter) - cycles_before;
            instructions[path][run] = import_benchmark_count(instructions_counter) - instructions_before;
            nanoseconds[path][run] = end - start;
            arena_pop_to(arena, position);
        }
    }
    if (!result)
    {
        printf("%s: the paths disagree or fail\n", benchmark->name);
    }
    for (uint32_t path = 0; result && path < IMPORT_BENCHMARK_PATHS; path += 1)
    {
        const double time = (double)import_benchmark_median(nanoseconds[path], BUSTER_IMPORT_BENCHMARK_RUNS);
        const double cycle_count = (double)import_benchmark_median(cycles[path], BUSTER_IMPORT_BENCHMARK_RUNS);
        const double instruction_count = (double)import_benchmark_median(instructions[path], BUSTER_IMPORT_BENCHMARK_RUNS);
        const double records = (double)benchmark->records;
        printf("  %-6s %10.2f µs  %8.2f µs/object  %6.2f ns/record  %6.2f cycles/record  %6.2f instructions/record  IPC %4.2f\n", import_benchmark_path_names[path],
               time / 1000.0, time / 1000.0 / benchmark->object_count, time / records, cycle_count / records, instruction_count / records,
               cycle_count != 0 ? instruction_count / cycle_count : 0.0);
    }
    if (cycles_counter >= 0)
    {
        close(cycles_counter);
    }
    if (instructions_counter >= 0)
    {
        close(instructions_counter);
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Files: libraries, archives and the exports' index

// A file's bytes, read into `arena`, or an empty slice.
static String8 import_benchmark_read(const char* path, Arena* arena)
{
    String8 result = { 0 };
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    struct stat status;
    char* bytes = fd >= 0 && fstat(fd, &status) == 0 ? arena_push(arena, (uint64_t)status.st_size, 64) : 0;
    uint64_t done = 0;
    bool reading = bytes != 0;
    while (reading && done < (uint64_t)status.st_size)
    {
        const ssize_t got = read(fd, bytes + done, (uint64_t)status.st_size - done);
        reading = got > 0;
        done += got > 0 ? (uint64_t)got : 0;
    }
    if (bytes != 0 && done == (uint64_t)status.st_size)
    {
        result.pointer = bytes;
        result.length = done;
    }
    if (fd >= 0)
    {
        close(fd);
    }
    return result;
}

typedef enum ImportBenchmarkFile
{
    IMPORT_BENCHMARK_LIBRARY,
    IMPORT_BENCHMARK_ARCHIVE,
} ImportBenchmarkFile;

// One read of a library or an archive on one path; whether it read.
static bool import_benchmark_read_file(ImportBenchmarkFile kind, ImportBenchmarkPath path, String8 bytes, ImportLibrary* library, ImportArchive* archive, Arena* arena)
{
    bool result = false;
    if (kind == IMPORT_BENCHMARK_LIBRARY)
    {
        result = path == IMPORT_BENCHMARK_VECTOR ? import_library_vector(library, bytes, arena) : import_library_scalar(library, bytes, arena).error == IMPORT_ERROR_NONE;
    }
    else
    {
        result = path == IMPORT_BENCHMARK_VECTOR ? import_archive_vector(archive, bytes, arena) : import_archive_scalar(archive, bytes, arena).error == IMPORT_ERROR_NONE;
    }
    return result;
}

// Whether the two paths read a library the same: its exports and references, column by column, and its index.
static bool import_benchmark_same_library(const ImportLibrary* a, const ImportLibrary* b)
{
    const ImportExports* x = &a->exports;
    const ImportExports* y = &b->exports;
    const ImportReferences* p = &a->references;
    const ImportReferences* q = &b->references;
    const uint64_t e = x->count;
    const uint64_t r = p->count;
    const uint64_t buckets = (uint64_t)1 << a->index.bucket_bits;
    bool result = e == y->count && r == q->count && a->version_count == b->version_count && a->soname.pointer == b->soname.pointer && a->index.bucket_bits == b->index.bucket_bits;
    result = result && memcmp(x->name, y->name, e * sizeof(*x->name)) == 0 && memcmp(x->length, y->length, e * sizeof(uint32_t)) == 0 &&
             memcmp(x->hash, y->hash, e * sizeof(uint64_t)) == 0 && memcmp(x->size, y->size, e * sizeof(uint64_t)) == 0 && memcmp(x->flags, y->flags, e) == 0 &&
             memcmp(x->version, y->version, e * sizeof(uint16_t)) == 0;
    result = result && memcmp(p->name, q->name, r * sizeof(*p->name)) == 0 && memcmp(p->length, q->length, r * sizeof(uint32_t)) == 0 &&
             memcmp(p->hash, q->hash, r * sizeof(uint64_t)) == 0 && memcmp(p->weak, q->weak, r) == 0;
    result = result && memcmp(a->index.start, b->index.start, (buckets + 1) * sizeof(uint32_t)) == 0 && memcmp(a->index.order, b->index.order, e * sizeof(uint32_t)) == 0;
    return result;
}

// Field by field: ImportArchiveSymbol has padding.
static bool import_benchmark_same_archive(const ImportArchive* a, const ImportArchive* b)
{
    bool result = a->member_count == b->member_count && a->symbol_count == b->symbol_count;
    for (uint32_t m = 0; result && m < a->member_count; m += 1)
    {
        result = a->members[m].name.pointer == b->members[m].name.pointer && a->members[m].name.length == b->members[m].name.length &&
                 a->members[m].bytes.pointer == b->members[m].bytes.pointer && a->members[m].bytes.length == b->members[m].bytes.length &&
                 a->members[m].offset == b->members[m].offset;
    }
    for (uint32_t i = 0; result && i < a->symbol_count; i += 1)
    {
        result = a->symbols[i].name.pointer == b->symbols[i].name.pointer && a->symbols[i].name.length == b->symbols[i].name.length &&
                 a->symbols[i].member == b->symbols[i].member;
    }
    return result;
}

// Prints a figure's medians per item.
static void import_benchmark_print(const char* what, uint64_t* nanoseconds, uint64_t* cycles, uint64_t* instructions, uint64_t items, const char* item)
{
    const double time = (double)import_benchmark_median(nanoseconds, BUSTER_IMPORT_BENCHMARK_RUNS);
    const double cycle_count = (double)import_benchmark_median(cycles, BUSTER_IMPORT_BENCHMARK_RUNS);
    const double instruction_count = (double)import_benchmark_median(instructions, BUSTER_IMPORT_BENCHMARK_RUNS);
    const double count = items != 0 ? (double)items : 1.0;
    printf("  %-22s %10.2f µs  %7.2f ns/%s  %7.2f cycles/%s  %7.2f instructions/%s  IPC %4.2f\n", what, time / 1000.0, time / count, item, cycle_count / count, item,
           instruction_count / count, item, cycle_count != 0 ? instruction_count / cycle_count : 0.0);
}

// A library or an archive read on both paths, which must agree and the vector path take, the paths taking turns.
static bool import_benchmark_file(ImportBenchmarkFile kind, const char* name, String8 bytes, int cycles_counter, int instructions_counter, Arena* arena)
{
    const uint64_t position = arena->length;
    ImportLibrary libraries[IMPORT_BENCHMARK_PATHS];
    ImportArchive archives[IMPORT_BENCHMARK_PATHS];
    const bool vector = import_benchmark_read_file(kind, IMPORT_BENCHMARK_VECTOR, bytes, &libraries[0], &archives[0], arena);
    const bool exact = import_benchmark_read_file(kind, IMPORT_BENCHMARK_EXACT, bytes, &libraries[1], &archives[1], arena);
    bool result = exact && vector;
    result = result && (kind == IMPORT_BENCHMARK_LIBRARY ? import_benchmark_same_library(&libraries[0], &libraries[1]) : import_benchmark_same_archive(&archives[0], &archives[1]));
    const uint64_t items = kind == IMPORT_BENCHMARK_LIBRARY ? (uint64_t)libraries[1].exports.count + libraries[1].references.count : archives[1].symbol_count;
    if (result)
    {
        printf("%s: %llu bytes, ", name, (unsigned long long)bytes.length);
        if (kind == IMPORT_BENCHMARK_LIBRARY)
        {
            printf("%u exports, %u references, %u versions\n", libraries[1].exports.count, libraries[1].references.count, libraries[1].version_count);
        }
        else
        {
            printf("%u members, %u index entries\n", archives[1].member_count, archives[1].symbol_count);
        }
    }
    else
    {
        printf("%s: the paths disagree, fail, or the vector path leaves it to the exact path (vector %s, exact %s)\n", name, vector ? "read it" : "didn't",
               exact ? "read it" : "didn't");
    }
    arena_pop_to(arena, position);
    uint64_t nanoseconds[IMPORT_BENCHMARK_PATHS][BUSTER_IMPORT_BENCHMARK_RUNS];
    uint64_t cycles[IMPORT_BENCHMARK_PATHS][BUSTER_IMPORT_BENCHMARK_RUNS];
    uint64_t instructions[IMPORT_BENCHMARK_PATHS][BUSTER_IMPORT_BENCHMARK_RUNS];
    for (uint32_t run = 0; result && run < BUSTER_IMPORT_BENCHMARK_RUNS; run += 1)
    {
        for (uint32_t turn = 0; result && turn < IMPORT_BENCHMARK_PATHS; turn += 1)
        {
            const ImportBenchmarkPath path = (ImportBenchmarkPath)((run + turn) % IMPORT_BENCHMARK_PATHS);
            const uint64_t cycles_before = import_benchmark_count(cycles_counter);
            const uint64_t instructions_before = import_benchmark_count(instructions_counter);
            const uint64_t start = import_benchmark_now();
            result = import_benchmark_read_file(kind, path, bytes, &libraries[path], &archives[path], arena);
            const uint64_t end = import_benchmark_now();
            cycles[path][run] = import_benchmark_count(cycles_counter) - cycles_before;
            instructions[path][run] = import_benchmark_count(instructions_counter) - instructions_before;
            nanoseconds[path][run] = end - start;
            arena_pop_to(arena, position);
        }
    }
    const char* item = kind == IMPORT_BENCHMARK_LIBRARY ? "name" : "entry";
    for (uint32_t path = 0; result && path < IMPORT_BENCHMARK_PATHS; path += 1)
    {
        import_benchmark_print(import_benchmark_path_names[path], nanoseconds[path], cycles[path], instructions[path], items, item);
    }
    return result;
}

// The table the exports' index replaced, for comparison: open addressing with linear probing, two slots per export
// rounded up to a power of two, each an export's index plus 1, filled one export at a time, a name exported twice
// keeping its first entry.
typedef struct ImportBenchmarkTable ImportBenchmarkTable;
struct ImportBenchmarkTable
{
    uint32_t* slots;
    uint64_t mask;
};

static bool import_benchmark_table_build(ImportBenchmarkTable* table, const ImportExports* exports, Arena* arena)
{
    uint64_t capacity = 16;
    while (capacity < 2 * (uint64_t)exports->count + 1)
    {
        capacity *= 2;
    }
    table->slots = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, capacity);
    table->mask = capacity - 1;
    const bool result = table->slots != 0;
    if (result)
    {
        memset(table->slots, 0, capacity * sizeof(uint32_t));
        for (uint32_t i = 0; i < exports->count; i += 1)
        {
            uint64_t slot = exports->hash[i] & table->mask;
            bool duplicate = false;
            while (table->slots[slot] != 0 && !duplicate)
            {
                const uint32_t other = table->slots[slot] - 1;
                duplicate = exports->hash[other] == exports->hash[i] && exports->length[other] == exports->length[i] &&
                            memcmp(exports->name[other], exports->name[i], exports->length[i]) == 0;
                slot = duplicate ? slot : (slot + 1) & table->mask;
            }
            table->slots[slot] = duplicate ? table->slots[slot] : i + 1;
        }
    }
    return result;
}

static uint32_t import_benchmark_table_find(const ImportBenchmarkTable* table, const ImportExports* exports, const uint8_t* name, uint64_t length, uint64_t hash)
{
    uint32_t result = 0;
    bool searching = true;
    for (uint64_t slot = hash & table->mask; searching; slot = (slot + 1) & table->mask)
    {
        const uint32_t entry = table->slots[slot];
        const bool match = entry != 0 && exports->hash[entry - 1] == hash && exports->length[entry - 1] == length && import_same_bytes(exports->name[entry - 1], name, length);
        result = match ? entry : 0;
        searching = entry != 0 && !match;
    }
    return result;
}

// The exports' index against the table: each built, then looked up for every export's name and every reference's;
// both must find the same.
static bool import_benchmark_index(const char* name, String8 bytes, int cycles_counter, int instructions_counter, Arena* arena)
{
    const uint64_t position = arena->length;
    ImportLibrary library;
    bool result = import_library_vector(&library, bytes, arena);
    const ImportExports* exports = &library.exports;
    const ImportReferences* references = &library.references;
    const uint32_t lookups = result ? exports->count + references->count : 0;
    const uint64_t built = arena->length;
    uint64_t nanoseconds[4][BUSTER_IMPORT_BENCHMARK_RUNS];
    uint64_t cycles[4][BUSTER_IMPORT_BENCHMARK_RUNS];
    uint64_t instructions[4][BUSTER_IMPORT_BENCHMARK_RUNS];
    for (uint32_t run = 0; result && run < BUSTER_IMPORT_BENCHMARK_RUNS; run += 1)
    {
        ImportBenchmarkTable table;
        uint64_t found[2] = { 0 };
        for (uint32_t step = 0; result && step < 4; step += 1)
        {
            const uint64_t cycles_before = import_benchmark_count(cycles_counter);
            const uint64_t instructions_before = import_benchmark_count(instructions_counter);
            const uint64_t start = import_benchmark_now();
            if (step == 0)
            {
                arena_pop_to(arena, built);
                result = import_library_index(&library, arena);
            }
            else if (step == 1)
            {
                result = import_benchmark_table_build(&table, exports, arena);
            }
            else
            {
                for (uint32_t i = 0; i < lookups; i += 1)
                {
                    const bool export = i < exports->count;
                    const uint32_t r = i - (export ? 0 : exports->count);
                    const uint8_t* bytes_of = export ? exports->name[i] : references->name[r];
                    const uint32_t length = export ? exports->length[i] : references->length[r];
                    const uint64_t hash = export ? exports->hash[i] : references->hash[r];
                    found[step - 2] += step == 2 ? import_library_find(&library, bytes_of, length, hash) : import_benchmark_table_find(&table, exports, bytes_of, length, hash);
                }
            }
            const uint64_t end = import_benchmark_now();
            cycles[step][run] = import_benchmark_count(cycles_counter) - cycles_before;
            instructions[step][run] = import_benchmark_count(instructions_counter) - instructions_before;
            nanoseconds[step][run] = end - start;
        }
        result = result && found[0] == found[1];
    }
    if (result)
    {
        printf("%s's exports' index: %u exports, %u lookups\n", name, exports->count, lookups);
        import_benchmark_print("index: build", nanoseconds[0], cycles[0], instructions[0], exports->count, "export");
        import_benchmark_print("table: build", nanoseconds[1], cycles[1], instructions[1], exports->count, "export");
        import_benchmark_print("index: lookups", nanoseconds[2], cycles[2], instructions[2], lookups, "lookup");
        import_benchmark_print("table: lookups", nanoseconds[3], cycles[3], instructions[3], lookups, "lookup");
    }
    else
    {
        printf("%s's exports' index: the index and the table disagree, or the library doesn't read\n", name);
    }
    arena_pop_to(arena, position);
    return result;
}

// libc.so's script, read by import_script, which has no vector path: the whole script, per byte.
static bool import_benchmark_script(String8 path, int cycles_counter, int instructions_counter, Arena* arena)
{
    const uint64_t position = arena->length;
    char name[BUSTER_RUNTIME_PATH_BYTES];
    snprintf(name, sizeof(name), "%.*s", (int)path.length, path.pointer);
    const String8 bytes = import_benchmark_read(name, arena);
    ImportScript script;
    bool result = bytes.length != 0 && import_script(&script, bytes, arena).error == IMPORT_ERROR_NONE;
    const uint64_t read = arena->length;
    uint64_t nanoseconds[BUSTER_IMPORT_BENCHMARK_RUNS];
    uint64_t cycles[BUSTER_IMPORT_BENCHMARK_RUNS];
    uint64_t instructions[BUSTER_IMPORT_BENCHMARK_RUNS];
    for (uint32_t run = 0; result && run < BUSTER_IMPORT_BENCHMARK_RUNS; run += 1)
    {
        const uint64_t cycles_before = import_benchmark_count(cycles_counter);
        const uint64_t instructions_before = import_benchmark_count(instructions_counter);
        const uint64_t start = import_benchmark_now();
        result = import_script(&script, bytes, arena).error == IMPORT_ERROR_NONE;
        const uint64_t end = import_benchmark_now();
        cycles[run] = import_benchmark_count(cycles_counter) - cycles_before;
        instructions[run] = import_benchmark_count(instructions_counter) - instructions_before;
        nanoseconds[run] = end - start;
        arena_pop_to(arena, read);
    }
    if (result)
    {
        printf("%s: %llu bytes, %u inputs\n", name, (unsigned long long)bytes.length, script.input_count);
        import_benchmark_print("exact", nanoseconds, cycles, instructions, bytes.length, "byte");
    }
    else
    {
        printf("%s: can't be read\n", name);
    }
    arena_pop_to(arena, position);
    return result;
}

// runtime_import end to end, as a link without a snapshot would run it: the search's probes, each file read, imported
// and bound.
static bool import_benchmark_runtime_import(int cycles_counter, int instructions_counter)
{
    uint64_t nanoseconds[BUSTER_IMPORT_BENCHMARK_RUNS];
    uint64_t cycles[BUSTER_IMPORT_BENCHMARK_RUNS];
    uint64_t instructions[BUSTER_IMPORT_BENCHMARK_RUNS];
    bool result = true;
    uint32_t probes = 0;
    for (uint32_t run = 0; result && run < BUSTER_IMPORT_BENCHMARK_RUNS + 1; run += 1)
    {
        Runtime runtime;
        const uint64_t cycles_before = import_benchmark_count(cycles_counter);
        const uint64_t instructions_before = import_benchmark_count(instructions_counter);
        const uint64_t start = import_benchmark_now();
        result = runtime_import(&runtime) == RUNTIME_ERROR_NONE;
        const uint64_t end = import_benchmark_now();
        // The first run is the warm-up.
        if (run != 0)
        {
            cycles[run - 1] = import_benchmark_count(cycles_counter) - cycles_before;
            instructions[run - 1] = import_benchmark_count(instructions_counter) - instructions_before;
            nanoseconds[run - 1] = end - start;
        }
        probes = runtime.probe_count;
        runtime_release(&runtime);
    }
    if (result)
    {
        printf("runtime_import, end to end: %u probes\n", probes);
        import_benchmark_print("vector", nanoseconds, cycles, instructions, 1, "import");
    }
    return result;
}

static bool import_benchmark_parse(const char* text, uint64_t* value)
{
    char* end = 0;
    const unsigned long long parsed = strtoull(text, &end, 10);
    const bool result = end != text && *end == 0;
    *value = result ? (uint64_t)parsed : *value;
    return result;
}

int run_import_benchmark(int argc, char** argv);

int run_import_benchmark(int argc, char** argv)
{
    uint64_t units = BUSTER_IMPORT_BENCHMARK_DEFAULT_UNITS;
    uint64_t functions = BUSTER_IMPORT_BENCHMARK_DEFAULT_FUNCTIONS;
    uint64_t seed = 1;
    const char* libraries[BUSTER_IMPORT_BENCHMARK_MAX_FILES];
    const char* archives[BUSTER_IMPORT_BENCHMARK_MAX_FILES];
    uint32_t library_count = 0;
    uint32_t archive_count = 0;
    bool valid = true;
    for (int i = 1; valid && i < argc; i += 1)
    {
        const bool has_value = i + 1 < argc;
        if (strcmp(argv[i], "--units") == 0 && has_value)
        {
            valid = import_benchmark_parse(argv[i + 1], &units) && units >= 1 && units <= BUSTER_IMPORT_BENCHMARK_MAX_OBJECTS;
            i += 1;
        }
        else if (strcmp(argv[i], "--functions") == 0 && has_value)
        {
            valid = import_benchmark_parse(argv[i + 1], &functions) && functions >= 1 && functions * units <= BUSTER_LINK_PROGRAM_MAX_COUNT;
            i += 1;
        }
        else if (strcmp(argv[i], "--seed") == 0 && has_value)
        {
            valid = import_benchmark_parse(argv[i + 1], &seed);
            i += 1;
        }
        else if (strcmp(argv[i], "--library") == 0 && has_value && library_count < BUSTER_IMPORT_BENCHMARK_MAX_FILES)
        {
            libraries[library_count] = argv[i + 1];
            library_count += 1;
            i += 1;
        }
        else if (strcmp(argv[i], "--archive") == 0 && has_value && archive_count < BUSTER_IMPORT_BENCHMARK_MAX_FILES)
        {
            archives[archive_count] = argv[i + 1];
            archive_count += 1;
            i += 1;
        }
        else
        {
            valid = false;
        }
    }
    bool result = valid;
    if (!valid)
    {
        printf("usage: benchmark-import [--units N] [--functions N] [--seed N] [--library PATH]... [--archive PATH]...\n");
    }
    Arena arena = arena_reserve(BUSTER_IMPORT_BENCHMARK_ARENA_BYTES);
    ImportBenchmarkCase* cases = valid ? BUSTER_ARENA_PUSH_ARRAY(&arena, ImportBenchmarkCase, 4) : 0;
    Runtime runtime;
    bool runtime_started = false;
    if (cases != 0)
    {
        memset(cases, 0, 4 * sizeof(ImportBenchmarkCase));
        LinkProgramOptions options = link_program_default_options();
        options.seed = seed;
        // Program-wide counts, as benchmark-object-writer's: per unit, --functions workers, 8 globals, 2 thread-locals,
        // 20 strings, an init and a fini function.
        options.units = (uint32_t)units;
        options.functions = (uint32_t)(units * functions);
        options.globals = (uint32_t)(units * 8);
        options.thread_locals = (uint32_t)(units * 2);
        options.strings = 20;
        options.init_functions = (uint32_t)units;
        options.fini_functions = (uint32_t)units;
        options.filler = 40;
        cases[0].name = "program";
        cases[1].name = "function sections";
        cases[2].name = "many sections";
        cases[3].name = "runtime objects";
        result = import_benchmark_program(&cases[0], options, false, &arena) && import_benchmark_program(&cases[1], options, true, &arena);
        LinkProgramOptions many = options;
        many.units = 1;
        many.functions = BUSTER_IMPORT_BENCHMARK_MANY_FUNCTIONS;
        many.filler = 0;
        result = result && import_benchmark_program(&cases[2], many, true, &arena);
        runtime_started = result;
        result = result && import_benchmark_runtime(&cases[3], &runtime, &arena);
    }
    for (uint32_t c = 0; result && c < 4; c += 1)
    {
        const ImportBenchmarkCase* benchmark = &cases[c];
        printf("%s: %u objects, %llu records (%u symbol table entries, %u relocations)\n", benchmark->name, benchmark->object_count, (unsigned long long)benchmark->records,
               benchmark->capacity.symbols, benchmark->capacity.fixups);
        result = import_benchmark_case(benchmark, &arena);
    }
    // The files: the runtime's libc.so.6 and libc_nonshared.a, then the ones the arguments name.
    const int cycles_counter = result ? import_benchmark_counter(PERF_COUNT_HW_CPU_CYCLES) : -1;
    const int instructions_counter = result ? import_benchmark_counter(PERF_COUNT_HW_INSTRUCTIONS) : -1;
    if (result)
    {
        result = import_benchmark_file(IMPORT_BENCHMARK_LIBRARY, "libc.so.6", runtime.libc_bytes, cycles_counter, instructions_counter, &arena) &&
                 import_benchmark_index("libc.so.6", runtime.libc_bytes, cycles_counter, instructions_counter, &arena);
        result = result && import_benchmark_file(IMPORT_BENCHMARK_ARCHIVE, "libc_nonshared.a", runtime.nonshared_bytes, cycles_counter, instructions_counter, &arena);
        result = result && import_benchmark_script(runtime.libc_script, cycles_counter, instructions_counter, &arena);
        result = result && import_benchmark_runtime_import(cycles_counter, instructions_counter);
    }
    for (uint32_t i = 0; result && i < library_count + archive_count; i += 1)
    {
        const bool library = i < library_count;
        const char* path = library ? libraries[i] : archives[i - library_count];
        const uint64_t position = arena.length;
        const String8 bytes = import_benchmark_read(path, &arena);
        result = bytes.length != 0;
        if (!result)
        {
            printf("%s: can't be read\n", path);
        }
        result = result && import_benchmark_file(library ? IMPORT_BENCHMARK_LIBRARY : IMPORT_BENCHMARK_ARCHIVE, path, bytes, cycles_counter, instructions_counter, &arena);
        result = result && (!library || import_benchmark_index(path, bytes, cycles_counter, instructions_counter, &arena));
        arena_pop_to(&arena, position);
    }
    if (cycles_counter >= 0)
    {
        close(cycles_counter);
    }
    if (instructions_counter >= 0)
    {
        close(instructions_counter);
    }
    if (runtime_started)
    {
        runtime_release(&runtime);
    }
    arena_release(&arena);
    return result ? 0 : 1;
}
