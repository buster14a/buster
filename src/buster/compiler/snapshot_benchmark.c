// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The runtime snapshot's benchmark: what a link pays for the C runtime with the snapshot (snapshot.c) and without it
// (runtime_import), phase by phase, and what the alternatives the design weighed cost.
//
// Usage: build/compiler benchmark-snapshot [--directory PATH] [--runs N]
//
// Run it under the 9700X's lock pinned to one CPU (taskset -c 2 ...), with --directory on each file system to measure
// (TMPDIR or /tmp by default; the cache directory is usually under $HOME). Each figure is the median of --runs runs (41
// by default) after a warm-up:
//   - the import: runtime_import and runtime_release, as a link without a snapshot pays them;
//   - the write: snapshot_write of the imported runtime into the directory, over the snapshot of the run before;
//   - the load: runtime_open's, as a link pays it: this executable's identity (a stat of /proc/self/exe), then
//     snapshot_load's phases (the mapping, the header and the hash, the view, the probes' stats), and runtime_release's
//     munmap after it;
//   - the alternatives: the snapshot read with pread into new memory, as the design first had it, instead of mapped;
//     and mapped without the hash, touching only the pages a hello world's binding reads;
//   - binding: a program of one worker that calls libc, bound against the imported runtime and against a loaded one
//     (bind_begin and bind_end), which must make the same link.
// The exit status is 1 if the arguments are invalid, the runtime doesn't import, or a snapshot can't be written or
// loaded.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// OS-dependent includes
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

typedef struct String8 String8;
struct String8
{
    char* pointer;
    uint64_t length;
};

#include "buster/compiler/lane.c"
// The encoder's batch widths and the variants it measures, which only its own benchmark and tests call.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "buster/compiler/x86_64_encoder.c"
#pragma clang diagnostic pop
#include "buster/compiler/link.c"
#include "buster/compiler/link_program.c"
// The import's relocation types are the object writer's; the snapshot's hash is executable.c's.
#include "buster/compiler/object_writer.c"
#include "buster/compiler/import.c"
#include "buster/compiler/runtime.c"
#include "buster/compiler/bind.c"
#include "buster/compiler/executable.c"
#include "buster/compiler/snapshot.c"

#define BUSTER_SNAPSHOT_BENCHMARK_PATH_BYTES 512
#define BUSTER_SNAPSHOT_BENCHMARK_DEFAULT_RUNS 41
#define BUSTER_SNAPSHOT_BENCHMARK_MAX_RUNS 10001
#define BUSTER_SNAPSHOT_BENCHMARK_BIND_BYTES (1ull << 30)

static int snapshot_benchmark_compare(const void* a, const void* b)
{
    const uint64_t x = *(const uint64_t*)a;
    const uint64_t y = *(const uint64_t*)b;
    return (x > y) - (x < y);
}

// The median of the values, in microseconds; sorts them.
static double snapshot_benchmark_median(uint64_t* values, uint64_t count)
{
    qsort(values, count, sizeof(uint64_t), snapshot_benchmark_compare);
    return (double)values[count / 2] / 1e3;
}

static bool snapshot_benchmark_parse_u64(const char* text, uint64_t* value)
{
    char* end = 0;
    const unsigned long long parsed = strtoull(text, &end, 0);
    const bool result = end != text && *end == 0;
    *value = result ? parsed : *value;
    return result;
}

// The snapshot read whole with pread into memory mapped for it, which its first touches commit, as the design first
// had it: the nanoseconds open, fstat, the mmap, the pread and close take, or 0 if a call failed.
static uint64_t snapshot_benchmark_pread(const char* path)
{
    const uint64_t start = executable_now();
    const int file = open(path, O_RDONLY | O_CLOEXEC);
    struct stat status;
    const bool known = file >= 0 && fstat(file, &status) == 0;
    uint8_t* bytes = known ? mmap(0, (uint64_t)status.st_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    const bool read_whole = bytes != MAP_FAILED && pread(file, bytes, (uint64_t)status.st_size, 0) == (ssize_t)status.st_size;
    if (file >= 0)
    {
        close(file);
    }
    const uint64_t end = executable_now();
    if (bytes != MAP_FAILED)
    {
        munmap(bytes, (uint64_t)status.st_size);
    }
    return read_whole ? end - start : 0;
}

// The snapshot mapped without its hash checked: the mapping, then the view and the probes as snapshot_load has them,
// and the pages a hello world's binding reads (the filter, and the index's and names' entries of a dozen names): the
// nanoseconds it takes, or 0 if it wasn't a snapshot that loads.
static uint64_t snapshot_benchmark_unhashed(const char* path, const RuntimeProbe* compiler, const Runtime* imported)
{
    const uint64_t start = executable_now();
    Runtime runtime = { 0 };
    const int descriptor = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    struct stat status;
    const bool regular = descriptor >= 0 && fstat(descriptor, &status) == 0 && S_ISREG(status.st_mode);
    const uint64_t bytes = regular ? (uint64_t)status.st_size : 0;
    uint8_t* file = bytes >= sizeof(SnapshotHeader) ? mmap(0, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE, descriptor, 0) : MAP_FAILED;
    if (descriptor >= 0)
    {
        close(descriptor);
    }
    bool loaded = file != MAP_FAILED && snapshot_valid_header((const SnapshotHeader*)file, bytes, &runtime) &&
                  snapshot_same_identity(&((const SnapshotHeader*)file)->compiler, compiler);
    if (loaded)
    {
        snapshot_view(&runtime, file, bytes);
        loaded = runtime_probes_current(&runtime);
    }
    // A dozen lookups, of names spread over the global names.
    const RuntimeNames* names = &imported->global_names;
    uint32_t found = 0;
    for (uint32_t i = 0; loaded && i < 12; i += 1)
    {
        const uint32_t e = (uint32_t)((uint64_t)i * names->count / 12);
        found += runtime_find_global_name(&runtime.global_names, runtime_global_name(names, e), names->length[e], names->hash[e]) == e + 1 ? 1 : 0;
    }
    const uint64_t end = executable_now();
    if (file != MAP_FAILED)
    {
        munmap(file, bytes);
    }
    return loaded && found == 12 ? end - start : 0;
}

// A program of one worker that calls libc, as hello world is, bound against `runtime`: the nanoseconds bind_begin and
// bind_end take (0 if it didn't bind), without the program's atoms between them, and whether it made the same link as
// `reference`, by its tables' counts and the symbols' sources, versions and flags (with `keep`, kept there instead).
static uint64_t snapshot_benchmark_bind(const Runtime* runtime, LinkProgram* reference, bool keep, bool* same)
{
    LinkProgramOptions options = link_program_default_options();
    options.functions = 1;
    options.units = 1;
    options.absolute = false;
    options.libc = true;
    uint64_t name_bytes = 0;
    uint64_t atom_bytes = 0;
    const LinkCapacity extra = bind_capacity(runtime, &name_bytes, &atom_bytes);
    Arena arena = arena_reserve(BUSTER_SNAPSHOT_BENCHMARK_BIND_BYTES);
    Bind bind = { 0 };
    LinkProgram program;
    bool result = link_program_generate_begin(&program, options, extra, name_bytes, atom_bytes) && arena.base != 0;
    const uint64_t begin_start = executable_now();
    result = result && bind_begin(&bind, &program.link, runtime, BIND_PATH_VECTOR, &program.bytes, options.units, &arena);
    const uint64_t begin_end = executable_now();
    result = result && link_program_generate_atoms(&program);
    const uint64_t end_start = executable_now();
    result = result && bind_end(&bind, &program.link);
    const uint64_t end_end = executable_now();
    result = result && link_program_generate_end(&program);
    const LinkSymbols* a = &program.link.symbols;
    const LinkSymbols* b = &reference->link.symbols;
    *same = result && (keep || (a->count == b->count && program.link.atoms.count == reference->link.atoms.count &&
                                program.link.fixups.count == reference->link.fixups.count && memcmp(a->source, b->source, a->count) == 0 &&
                                memcmp(a->flags, b->flags, a->count) == 0 && memcmp(a->version, b->version, a->count * sizeof(uint16_t)) == 0));
    if (keep && result)
    {
        *reference = program;
    }
    else
    {
        link_program_release(&program);
    }
    arena_release(&arena);
    return result ? (begin_end - begin_start) + (end_end - end_start) : 0;
}

static const char* const snapshot_benchmark_phase_names[SNAPSHOT_PHASES] = {
    [SNAPSHOT_PHASE_MAP] = "map",
    [SNAPSHOT_PHASE_HASH] = "hash",
    [SNAPSHOT_PHASE_VIEW] = "view",
    [SNAPSHOT_PHASE_PROBES] = "probes",
};

int run_snapshot_benchmark(int argc, char** argv);

int run_snapshot_benchmark(int argc, char** argv)
{
    const char* parent = getenv("TMPDIR") != 0 ? getenv("TMPDIR") : "/tmp";
    uint64_t runs = BUSTER_SNAPSHOT_BENCHMARK_DEFAULT_RUNS;
    bool valid = true;
    for (int i = 1; i < argc; i += 1)
    {
        if (strcmp(argv[i], "--directory") == 0 && i + 1 < argc)
        {
            parent = argv[i + 1];
            i += 1;
        }
        else if (strcmp(argv[i], "--runs") == 0 && i + 1 < argc)
        {
            valid = valid && snapshot_benchmark_parse_u64(argv[i + 1], &runs) && runs >= 1 && runs <= BUSTER_SNAPSHOT_BENCHMARK_MAX_RUNS;
            i += 1;
        }
        else
        {
            valid = false;
        }
    }
    char directory[BUSTER_SNAPSHOT_BENCHMARK_PATH_BYTES];
    char path[BUSTER_SNAPSHOT_BENCHMARK_PATH_BYTES];
    const int length = snprintf(directory, sizeof(directory), "%s/buster-snapshot-benchmark-XXXXXX", parent);
    const bool made = valid && length > 0 && (uint64_t)length + 64 < sizeof(directory) && mkdtemp(directory) != 0;
    snprintf(path, sizeof(path), "%s/" BUSTER_SNAPSHOT_NAME, directory);
    Runtime imported;
    RuntimeProbe compiler;
    const bool identified = snapshot_compiler(&compiler);
    const RuntimeError error = made ? runtime_import(&imported) : RUNTIME_ERROR_NONE;
    bool result = made && identified && error == RUNTIME_ERROR_NONE && (x86_encoder_initialized || x86_encoder_initialize());
    if (!valid)
    {
        fprintf(stderr, "usage: benchmark-snapshot [--directory PATH] [--runs N]\n");
    }
    else if (!made)
    {
        fprintf(stderr, "benchmark-snapshot: can't make a directory in %s\n", parent);
    }
    else if (error != RUNTIME_ERROR_NONE)
    {
        fprintf(stderr, "the runtime didn't import: error %d, %s\n", (int)error, imported.failed_file);
    }
    uint64_t* times = result ? calloc(runs * (SNAPSHOT_PHASES + 8), sizeof(uint64_t)) : 0;
    result = result && times != 0;
    uint64_t* import_times = times;
    uint64_t* write_times = times + runs;
    uint64_t* identity_times = times + 2 * runs;
    uint64_t* open_times = times + 3 * runs;
    uint64_t* release_times = times + 4 * runs;
    uint64_t* pread_times = times + 5 * runs;
    uint64_t* unhashed_times = times + 6 * runs;
    uint64_t* phase_times = times + 7 * runs;
    // The import, as a link without a snapshot pays it.
    for (int64_t r = -1; result && r < (int64_t)runs; r += 1)
    {
        Runtime runtime;
        const uint64_t start = executable_now();
        result = runtime_import(&runtime) == RUNTIME_ERROR_NONE;
        runtime_release(&runtime);
        const uint64_t end = executable_now();
        if (r >= 0)
        {
            import_times[r] = end - start;
        }
    }
    // The write, over the snapshot of the run before.
    for (int64_t r = -1; result && r < (int64_t)runs; r += 1)
    {
        SnapshotResult written = { 0 };
        const uint64_t start = executable_now();
        result = snapshot_write(&imported, directory, &compiler, &written);
        const uint64_t end = executable_now();
        if (r >= 0)
        {
            write_times[r] = end - start;
        }
    }
    struct stat status;
    const uint64_t snapshot_bytes = result && stat(path, &status) == 0 ? (uint64_t)status.st_size : 0;
    // The load, as runtime_open does it, phase by phase.
    for (int64_t r = -1; result && r < (int64_t)runs; r += 1)
    {
        Runtime runtime;
        RuntimeProbe identity;
        uint64_t phases[SNAPSHOT_PHASES] = { 0 };
        const uint64_t start = executable_now();
        const bool known = snapshot_compiler(&identity);
        const uint64_t identified_at = executable_now();
        result = known && snapshot_load(&runtime, path, &identity, phases) == SNAPSHOT_STATUS_LOADED;
        const uint64_t loaded_at = executable_now();
        if (result)
        {
            runtime_release(&runtime);
        }
        const uint64_t end = executable_now();
        if (r >= 0)
        {
            identity_times[r] = identified_at - start;
            open_times[r] = loaded_at - start;
            release_times[r] = end - loaded_at;
            for (uint32_t p = 0; p < SNAPSHOT_PHASES; p += 1)
            {
                phase_times[p * runs + (uint64_t)r] = phases[p];
            }
        }
    }
    // The alternatives.
    for (int64_t r = -1; result && r < (int64_t)runs; r += 1)
    {
        const uint64_t read = snapshot_benchmark_pread(path);
        const uint64_t unhashed = snapshot_benchmark_unhashed(path, &compiler, &imported);
        result = read != 0 && unhashed != 0;
        if (r >= 0)
        {
            pread_times[r] = read;
            unhashed_times[r] = unhashed;
        }
    }
    if (result)
    {
        printf("The runtime: %u probes, %u global names, %u exports of libc.so.6, %u symbols; a %llu-byte snapshot in %s\n", imported.probe_count, imported.global_names.count,
               imported.libc.exports.count, imported.link.symbols.count - 1, (unsigned long long)snapshot_bytes, directory);
        printf("  runtime_import and runtime_release   %9.2f us\n", snapshot_benchmark_median(import_times, runs));
        printf("  snapshot_write                        %9.2f us\n", snapshot_benchmark_median(write_times, runs));
        printf("  runtime_open from the snapshot        %9.2f us:", snapshot_benchmark_median(open_times, runs));
        printf(" identity %.2f", snapshot_benchmark_median(identity_times, runs));
        for (uint32_t p = 0; p < SNAPSHOT_PHASES; p += 1)
        {
            printf(" %s %.2f", snapshot_benchmark_phase_names[p], snapshot_benchmark_median(phase_times + p * runs, runs));
        }
        printf("\n");
        printf("  runtime_release of a loaded runtime   %9.2f us\n", snapshot_benchmark_median(release_times, runs));
        printf("  read with pread into new memory       %9.2f us\n", snapshot_benchmark_median(pread_times, runs));
        printf("  loaded without the hash, 12 lookups   %9.2f us\n", snapshot_benchmark_median(unhashed_times, runs));
    }
    // Binding one worker's program against each runtime: the same link, and its time.
    Runtime loaded;
    const bool loaded_runtime = result && snapshot_load(&loaded, path, &compiler, 0) == SNAPSHOT_STATUS_LOADED;
    LinkProgram reference;
    bool kept = false;
    const bool referenced = loaded_runtime && snapshot_benchmark_bind(&imported, &reference, true, &kept) != 0 && kept;
    uint64_t* bind_times = referenced ? calloc(2 * runs, sizeof(uint64_t)) : 0;
    bool same = bind_times != 0;
    for (int64_t r = -1; same && r < (int64_t)runs; r += 1)
    {
        bool same_imported = false;
        bool same_loaded = false;
        const uint64_t a = snapshot_benchmark_bind(&imported, &reference, false, &same_imported);
        const uint64_t b = snapshot_benchmark_bind(&loaded, &reference, false, &same_loaded);
        same = a != 0 && b != 0 && same_imported && same_loaded;
        if (r >= 0)
        {
            bind_times[r] = a;
            bind_times[runs + (uint64_t)r] = b;
        }
    }
    if (same)
    {
        printf("  binding one worker, imported runtime  %9.2f us\n", snapshot_benchmark_median(bind_times, runs));
        printf("  binding one worker, loaded runtime    %9.2f us\n", snapshot_benchmark_median(bind_times + runs, runs));
    }
    else if (result)
    {
        fprintf(stderr, "benchmark-snapshot: the loaded runtime didn't bind as the imported one\n");
    }
    result = result && same;
    free(bind_times);
    if (referenced)
    {
        link_program_release(&reference);
    }
    if (loaded_runtime)
    {
        runtime_release(&loaded);
    }
    free(times);
    if (made)
    {
        unlink(path);
        rmdir(directory);
    }
    if (made && error == RUNTIME_ERROR_NONE)
    {
        runtime_release(&imported);
    }
    return result ? 0 : 1;
}
