// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The link benchmark (docs/object-writer-and-linker.md, step 5 of "Plan"): what linking a generated program that calls
// libc costs by the direct path, inside the compiler, against writing its objects and linking them with the linkers of
// the document's baselines, as its appendix links them. It takes over the appendix's harness.
//
// Usage: build/compiler benchmark-link [--functions LIST [--filler N]] [--seed N] [--directory PATH] [--cache PATH]
//                                      [--cpus LIST] [--linkers LIST] [--runs N] [--only direct|linkers]
//                                      [--kinds LIST] [--keep]
//
// Run it under the 9700X's lock without taskset: it pins the direct path's lanes, one to each CPU of --cpus (CPU 2 by
// default, so one lane), and the linkers' runs on one thread to the first of them, and leaves the linkers' other runs
// every CPU this process may use. The programs call libc, generated as benchmark-executable generates its programs but
// without SIZE32 and SIZE64, which wild 0.10 doesn't link: by default link_benchmark_default_programs, of hello world's
// and tcc's output sizes and of synth's shape, or with --functions a program of each count of workers, of up to
// --filler filler instructions each (8 by default). Each is linked as each kind of --kinds (dynamic, static and
// static-pie, a list; every kind by default): a dynamic PIE against libc.so.6, a static executable against libc.a, and
// a static PIE against libc.a. For each:
//   - The direct path, as a compiler runs it: runtime_open (runtime_open_static, runtime_open_static_pie), which must
//     load the kind's snapshot in --cache (the cache directory by default; a first open makes it this executable's),
//     and bind_begin; then, after the program's atoms (its code generation, outside the time), bind_end and
//     dynamic_inputs (dynamic_static_inputs, dynamic_static_pie_inputs) on lane 0 and executable_write of the
//     executable on every lane, into a directory of its own under --directory (TMPDIR or /tmp by default), over the
//     file of the run before. Each part is the median of its runs after a warm-up, each run from the link as it was
//     before the part (put back outside the time, as benchmark-bind puts its links back), and the link is the parts'
//     sum; every run must write the warm-up's file again (its build ID). Its work: the bytes written, the fixups
//     applied, the names looked up (the program's global names, each once in the runtime's), and over the warm-ups the
//     page faults (getrusage) and how much the peak resident set grew (VmHWM, which writing 5 to /proc/self/clear_refs
//     resets), and the first timed run's page faults in runtime_open and in bind_begin, whose arrays the warm-up has
//     touched: the snapshot's mapping's, which each run maps afresh (#490). The floor is one pwrite of the output's bytes
//     over it.
//   - The objects: each unit's, as -c writes them, by object_prepare and object_write_units on the same lanes, over the
//     files of the run before, with their bytes, symbols and relocations.
//   - Each linker of --linkers (ld.bfd, ld.lld, mold, wild and link-foreign by default) links the objects with the appendix's
//     command, the clang driver's with the matched options: --hash-style=gnu --build-id --eh-frame-hdr -m elf_x86_64
//     -z now --no-gc-sections, -pie and the loader, Scrt1.o, crti.o and crtbeginS.o, -L for GCC's directory and
//     libc.so's, the objects, -lgcc --as-needed -lgcc_s --no-as-needed -lc -lgcc --as-needed -lgcc_s --no-as-needed,
//     then crtendS.o and crtn.o. A static executable's is the driver's -static one: -static, crt1.o, crti.o and
//     crtbeginT.o, -L for GCC's directory and libc.a's, the objects, --start-group -lgcc -lgcc_eh -lc --end-group, then
//     crtend.o and crtn.o; and a static PIE's its -static-pie one: -static -pie --no-dynamic-linker -z text, rcrt1.o,
//     crti.o and crtbeginS.o, the same -L options, objects and group, then crtendS.o and crtn.o. Each figure is the
//     wall time from posix_spawn to the wait's return, as the caller sees it: the median of the runs after a warm-up,
//     with a pause before each, over the output of the run before. Each linker runs with its own threads, and on one
//     (lld's --threads=1, mold's and wild's --no-threads; GNU ld has one anyway). link-foreign, this executable's own
//     linker of other compilers' objects (#86, #394), takes the same command line as a process of its own, with
//     --lanes=8 and --lanes=1 for its threads and one, for every kind. Then one more run, with --no-fork for
//     mold and wild, which otherwise fork to leave their teardown to a child, through the helper
//     (link_benchmark_rusage): its page faults and its peak resident set. A program that does nothing, run the same way
//     first, shows the least the helper can count.
// One group of --cpus lanes runs every direct link and object write of the command. Its startup and shutdown are
// outside their phase timings; the external linkers' wall times include starting each linker process.
// The runs after each warm-up are the appendix's: BUSTER_LINK_BENCHMARK_RUNS with a pause of 20 ms, or for an output
// past BUSTER_LINK_BENCHMARK_LARGE_BYTES BUSTER_LINK_BENCHMARK_LARGE_RUNS with 200 ms, and GNU ld's
// BUSTER_LINK_BENCHMARK_SLOW_RUNS with 500 ms; --runs N sets every count. Every program written must run and print what
// the program's model predicts; wild doesn't link the static executable or the static PIE of a program whose .tbss is
// more aligned than its .tdata, whose TLS segment wild 0.10 may start off its alignment (link_benchmark_wild_tls).
// --only direct and --only linkers run one side, and --keep leaves the directory. The exit status is 1 if the arguments
// are invalid, a runtime doesn't load from its snapshot, the lanes don't start, a link fails, or a program doesn't
// print what it should.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// OS-dependent includes
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/syscall.h>

extern char** environ;

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
// The objects, and the import's relocation types, are the object writer's; the snapshot's hash is executable.c's.
#include "buster/compiler/object_writer.c"
#include "buster/compiler/import.c"
#include "buster/compiler/runtime.c"
#include "buster/compiler/bind.c"
#include "buster/compiler/executable.c"
#include "buster/compiler/dynamic.c"
#include "buster/compiler/snapshot.c"

#define BUSTER_LINK_BENCHMARK_PATH_BYTES 4096
#define BUSTER_LINK_BENCHMARK_MAX_PROGRAMS 16
#define BUSTER_LINK_BENCHMARK_MAX_RUNS 1001
// The appendix's runs after a warm-up and the pauses before them: for an output past BUSTER_LINK_BENCHMARK_LARGE_BYTES
// the large ones, and GNU ld's slow ones.
#define BUSTER_LINK_BENCHMARK_RUNS 41
#define BUSTER_LINK_BENCHMARK_LARGE_RUNS 11
#define BUSTER_LINK_BENCHMARK_SLOW_RUNS 5
#define BUSTER_LINK_BENCHMARK_LARGE_BYTES (4ull << 20)
#define BUSTER_LINK_BENCHMARK_PAUSE_MILLISECONDS 20
#define BUSTER_LINK_BENCHMARK_LARGE_PAUSE_MILLISECONDS 200
#define BUSTER_LINK_BENCHMARK_SLOW_PAUSE_MILLISECONDS 500
// Binding's arrays and dynamic_inputs', the marks' copies of a link's columns, and the objects' units.
#define BUSTER_LINK_BENCHMARK_ARENA_BYTES (1ull << 30)
// Workers per unit, as benchmark-executable's programs have them: synth's translation units have 200 functions.
#define BUSTER_LINK_BENCHMARK_UNIT_FUNCTIONS 200
// /proc/self/status, read whole.
#define BUSTER_LINK_BENCHMARK_STATUS_BYTES 8192
// The most arguments a linker's command has besides the objects, with the null that ends them, and the arguments before
// it that run it through the helper (link_benchmark_rusage).
#define BUSTER_LINK_BENCHMARK_ARGUMENTS 48
#define BUSTER_LINK_BENCHMARK_HELPER_ARGUMENTS 5
// How much of a failed link's standard error to show.
#define BUSTER_LINK_BENCHMARK_ERROR_BYTES 600

// A program to link: its workers, and the most filler instructions each has (LinkProgramOptions.filler).
typedef struct LinkBenchmarkProgram LinkBenchmarkProgram;
struct LinkBenchmarkProgram
{
    uint32_t functions;
    uint32_t filler;
};

// Programs with outputs about the size of hello world's (6-16 KB) and tcc's (440 KB), and one of synth's shape: its
// 102,400 functions in 512 units, whose code with up to 64 filler instructions a worker comes to 44 MB of output where
// synth's comes to 46.
static const LinkBenchmarkProgram link_benchmark_default_programs[] = {
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

// The linkers the appendix measured.
typedef struct LinkBenchmarkLinker LinkBenchmarkLinker;
struct LinkBenchmarkLinker
{
    const char* name;
    // The option that makes it link on one thread, or 0 if it has only one.
    const char* one_thread;
    // The option that keeps it from forking, or 0 if it doesn't fork.
    const char* no_fork;
    // Whether its runs for an output past BUSTER_LINK_BENCHMARK_LARGE_BYTES are the slow ones.
    bool slow;
    // The option for its own threads, or 0 if it takes them by default.
    const char* own_threads;
};

static const LinkBenchmarkLinker link_benchmark_linkers[] = {
    {
        .name = "ld.bfd",
        .one_thread = 0,
        .no_fork = 0,
        .slow = true,
    },
    {
        .name = "ld.lld",
        .one_thread = "--threads=1",
        .no_fork = 0,
        .slow = false,
    },
    {
        .name = "mold",
        .one_thread = "--no-threads",
        .no_fork = "--no-fork",
        .slow = false,
    },
    {
        .name = "wild",
        .one_thread = "--no-threads",
        .no_fork = "--no-fork",
        .slow = false,
    },
    // This executable's own foreign linker (#86, #394), as a process of its own, on a lane per core of the 9700X or on
    // one.
    {
        .name = "link-foreign",
        .one_thread = "--lanes=1",
        .no_fork = 0,
        .slow = false,
        .own_threads = "--lanes=8",
    },
};

#define BUSTER_LINK_BENCHMARK_LINKERS BUSTER_ARRAY_LENGTH(link_benchmark_linkers)

// Each kind's name in --kinds, and what its links are.
static const char* const link_benchmark_kind_names[RUNTIME_KINDS] = {
    [RUNTIME_KIND_DYNAMIC] = "dynamic",
    [RUNTIME_KIND_STATIC] = "static",
    [RUNTIME_KIND_STATIC_PIE] = "static-pie",
    [RUNTIME_KIND_SHARED] = "shared",
};

static const char* const link_benchmark_kind_links[RUNTIME_KINDS] = {
    [RUNTIME_KIND_DYNAMIC] = "a dynamic PIE",
    [RUNTIME_KIND_STATIC] = "a static executable",
    [RUNTIME_KIND_STATIC_PIE] = "a static PIE",
    [RUNTIME_KIND_SHARED] = "a shared library",
};

typedef struct LinkBenchmarkSettings LinkBenchmarkSettings;
struct LinkBenchmarkSettings
{
    uint64_t seed;
    // The directory of the outputs and the objects, and the snapshot's.
    const char* directory;
    const char* cache;
    // The direct path's lanes' CPUs, the first of which the linkers' runs on one thread take, and how many.
    uint32_t cpus[BUSTER_LANE_MAX_COUNT];
    uint32_t lanes;
    // The CPUs this process may use, which the linkers' other runs have, and how many.
    uint64_t allowed[BUSTER_LANE_CPU_MASK_WORDS];
    uint32_t allowed_count;
    // The linkers of link_benchmark_linkers to run, a bit each.
    uint32_t linkers;
    // The runs after each warm-up, or 0 for the appendix's.
    uint64_t runs;
    bool direct;
    bool linking;
    // The kinds of link to measure, a dynamic PIE's, a static executable's, a static PIE's and a shared library's
    // (--kinds), and each one's runtime, opened once: the linkers' inputs come from its paths, and its records size the
    // programs' room.
    bool kinds[RUNTIME_KINDS];
    const Runtime* runtimes[RUNTIME_KINDS];
    // With a shared library's kind, the program that loads one and runs its buster_library_main (dlopen), which cc
    // builds in `work`.
    char harness[BUSTER_LINK_BENCHMARK_PATH_BYTES];
    // The directory made under `directory`, and its files: the direct path's output, and what a program run prints, its
    // errors and the helper's counts (link_benchmark_rusage). With `keep`, they stay.
    char work[BUSTER_LINK_BENCHMARK_PATH_BYTES];
    char direct_path[BUSTER_LINK_BENCHMARK_PATH_BYTES];
    char output_path[BUSTER_LINK_BENCHMARK_PATH_BYTES];
    char error_path[BUSTER_LINK_BENCHMARK_PATH_BYTES];
    char usage_path[BUSTER_LINK_BENCHMARK_PATH_BYTES];
    bool keep;
};

// ---------------------------------------------------------------------------------------------------------------------
// Measuring

static int link_benchmark_compare(const void* a, const void* b)
{
    const uint64_t x = *(const uint64_t*)a;
    const uint64_t y = *(const uint64_t*)b;
    return (x > y) - (x < y);
}

// The median of the values, in microseconds; sorts them, so that the first and the last are the least and the most.
static double link_benchmark_median(uint64_t* values, uint64_t count)
{
    qsort(values, count, sizeof(uint64_t), link_benchmark_compare);
    return (double)values[count / 2] / 1e3;
}

// The runs after a warm-up for an output of `bytes`: `runs` if it isn't 0, otherwise the appendix's.
static uint32_t link_benchmark_run_count(uint64_t runs, uint64_t bytes, bool slow)
{
    const bool large = bytes > BUSTER_LINK_BENCHMARK_LARGE_BYTES;
    const uint32_t appendix = !large ? BUSTER_LINK_BENCHMARK_RUNS : slow ? BUSTER_LINK_BENCHMARK_SLOW_RUNS : BUSTER_LINK_BENCHMARK_LARGE_RUNS;
    return runs != 0 ? (uint32_t)runs : appendix;
}

// The pause before each run for an output of `bytes`, in milliseconds.
static uint32_t link_benchmark_pause(uint64_t bytes, bool slow)
{
    const bool large = bytes > BUSTER_LINK_BENCHMARK_LARGE_BYTES;
    return !large ? BUSTER_LINK_BENCHMARK_PAUSE_MILLISECONDS : slow ? BUSTER_LINK_BENCHMARK_SLOW_PAUSE_MILLISECONDS : BUSTER_LINK_BENCHMARK_LARGE_PAUSE_MILLISECONDS;
}

static void link_benchmark_sleep(uint32_t milliseconds)
{
    const struct timespec pause = {
        .tv_sec = milliseconds / 1000,
        .tv_nsec = (long)(milliseconds % 1000) * 1000000,
    };
    nanosleep(&pause, 0);
}

// The page faults this process has taken, minor and major, on every thread.
static uint64_t link_benchmark_faults(void)
{
    struct rusage usage = { 0 };
    getrusage(RUSAGE_SELF, &usage);
    return (uint64_t)usage.ru_minflt + (uint64_t)usage.ru_majflt;
}

// A field of /proc/self/status in bytes: "VmRSS:", the resident set, or "VmHWM:", its peak. 0 if it can't be read.
static uint64_t link_benchmark_status(const char* field)
{
    char text[BUSTER_LINK_BENCHMARK_STATUS_BYTES];
    const int file = open("/proc/self/status", O_RDONLY | O_CLOEXEC);
    const ssize_t length = file >= 0 ? read(file, text, sizeof(text) - 1) : -1;
    if (file >= 0)
    {
        close(file);
    }
    text[length > 0 ? length : 0] = 0;
    const char* line = strstr(text, field);
    // The value is in kB, after blanks.
    const uint64_t kilobytes = line != 0 ? strtoull(line + strlen(field), 0, 10) : 0;
    return kilobytes * 1024;
}

// Resets the peak resident set to the resident set, as writing 5 to /proc/self/clear_refs does. False if it can't.
static bool link_benchmark_reset_peak(void)
{
    const int file = open("/proc/self/clear_refs", O_WRONLY | O_CLOEXEC);
    const bool result = file >= 0 && write(file, "5", 1) == 1;
    if (file >= 0)
    {
        close(file);
    }
    return result;
}

// What a warm-up took: page faults, and how much the peak resident set grew (unknown if it couldn't be reset).
typedef struct LinkBenchmarkCost LinkBenchmarkCost;
struct LinkBenchmarkCost
{
    uint64_t faults;
    uint64_t resident;
    bool reset;
};

static LinkBenchmarkCost link_benchmark_cost_begin(void)
{
    LinkBenchmarkCost result = { 0 };
    result.reset = link_benchmark_reset_peak();
    result.resident = result.reset ? link_benchmark_status("VmRSS:") : 0;
    result.faults = link_benchmark_faults();
    return result;
}

// Adds what the warm-up begun at `begun` took to `faults` and `growth`, and clears *known if the growth is unknown.
static void link_benchmark_cost_end(const LinkBenchmarkCost* begun, uint64_t* faults, uint64_t* growth, bool* known)
{
    *faults += link_benchmark_faults() - begun->faults;
    const uint64_t peak = begun->reset ? link_benchmark_status("VmHWM:") : 0;
    *growth += peak > begun->resident ? peak - begun->resident : 0;
    *known = *known && begun->reset && peak != 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// Running programs

// Runs `arguments` (a program, found on PATH unless its name has a slash, and its arguments, ending with a null) with
// its standard output and error in the files at `output_path` and `error_path`, and waits for it. Returns the
// nanoseconds from before the spawn to the wait's return. *status gets its exit status, or -1 if it didn't start or
// didn't exit; with `usage`, its rusage.
static uint64_t link_benchmark_spawn(char* const* arguments, const char* output_path, const char* error_path, int* status, struct rusage* usage)
{
    posix_spawn_file_actions_t actions;
    const bool initialized = posix_spawn_file_actions_init(&actions) == 0;
    const bool ready = initialized && posix_spawn_file_actions_addopen(&actions, 1, output_path, O_WRONLY | O_CREAT | O_TRUNC, 0644) == 0 &&
                       posix_spawn_file_actions_addopen(&actions, 2, error_path, O_WRONLY | O_CREAT | O_TRUNC, 0644) == 0;
    struct rusage ignored;
    int waited_status = 0;
    pid_t child = 0;
    const uint64_t start = executable_now();
    const bool spawned = ready && posix_spawnp(&child, arguments[0], &actions, 0, arguments, environ) == 0;
    const pid_t waited = spawned ? wait4(child, &waited_status, 0, usage != 0 ? usage : &ignored) : -1;
    const uint64_t end = executable_now();
    if (initialized)
    {
        posix_spawn_file_actions_destroy(&actions);
    }
    *status = spawned && waited == child && WIFEXITED(waited_status) ? WEXITSTATUS(waited_status) : -1;
    return end - start;
}

// Up to `capacity` - 1 bytes of the file at `path` into `text`, with a NUL after them. Returns how many, or -1.
static int64_t link_benchmark_read(const char* path, char* text, uint64_t capacity)
{
    const int file = open(path, O_RDONLY | O_CLOEXEC);
    const ssize_t length = file >= 0 ? read(file, text, capacity - 1) : -1;
    if (file >= 0)
    {
        close(file);
    }
    text[length > 0 ? length : 0] = 0;
    return length;
}

// Whether the program at `path` exits with status 0 having printed `expected` (BUSTER_LINK_PROGRAM_OUTPUT_BYTES): a
// shared library (`kind`) through the harness, which loads it and runs its buster_library_main.
static bool link_benchmark_runs_as_predicted(const LinkBenchmarkSettings* settings, RuntimeKind kind, const char* path, const uint8_t* expected)
{
    const bool library = kind == RUNTIME_KIND_SHARED;
    char* arguments[] = {
        library ? (char*)settings->harness : (char*)path,
        library ? (char*)path : 0,
        0,
    };
    int status = -1;
    link_benchmark_spawn(arguments, settings->output_path, settings->error_path, &status, 0);
    char printed[BUSTER_LINK_PROGRAM_OUTPUT_BYTES + 2];
    const int64_t length = status == 0 ? link_benchmark_read(settings->output_path, printed, sizeof(printed)) : -1;
    const bool result = length == BUSTER_LINK_PROGRAM_OUTPUT_BYTES && memcmp(printed, expected, BUSTER_LINK_PROGRAM_OUTPUT_BYTES) == 0;
    if (!result)
    {
        printf("  %s: status %d, printed %lld bytes, not the program's output\n", path, status, (long long)length);
    }
    return result;
}

// Sets the calling thread's CPUs, which a program it spawns starts with.
static bool link_benchmark_set_cpus(const uint64_t* mask)
{
    return syscall(SYS_sched_setaffinity, 0, BUSTER_LANE_CPU_MASK_WORDS * sizeof(uint64_t), mask) == 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// The programs

// A program's options, generated as a shared library for that kind (LinkProgramOptions.shared).
static LinkProgramOptions link_benchmark_options(LinkBenchmarkProgram program, uint64_t seed, RuntimeKind kind)
{
    LinkProgramOptions result = link_program_default_options();
    const uint32_t functions = program.functions;
    const uint32_t units = functions / BUSTER_LINK_BENCHMARK_UNIT_FUNCTIONS;
    result.seed = seed;
    result.filler = program.filler;
    result.functions = functions;
    result.units = units > 4 ? units : 4;
    result.globals = functions / 50 > 32 ? functions / 50 : 32;
    result.absolute = false;
    result.sizes = false;
    result.libc = true;
    result.shared = kind == RUNTIME_KIND_SHARED;
    return result;
}

// What binding changes in a link: the tables' counts, the arenas' lengths and the symbol columns it writes, as they
// were, copied (benchmark-bind puts its links back the same way).
typedef struct LinkBenchmarkMark LinkBenchmarkMark;
struct LinkBenchmarkMark
{
    uint32_t names;
    uint32_t atoms;
    uint32_t symbols;
    uint32_t fixups;
    uint32_t wide_addends;
    uint64_t name_bytes;
    uint64_t bytes;
    AtomId* atom;
    uint64_t* value;
    uint64_t* size;
    uint8_t* flags;
    uint8_t* source;
    uint16_t* version;
};

static bool link_benchmark_mark(LinkBenchmarkMark* mark, const Link* link, const Arena* bytes, Arena* arena)
{
    const LinkSymbols* symbols = &link->symbols;
    const uint32_t count = symbols->count;
    *mark = (LinkBenchmarkMark){
        .names = link->names.count,
        .atoms = link->atoms.count,
        .symbols = count,
        .fixups = link->fixups.count,
        .wide_addends = link->fixups.wide_count,
        .name_bytes = link->name_arena->length,
        .bytes = bytes->length,
        .atom = BUSTER_ARENA_PUSH_ARRAY(arena, AtomId, count),
        .value = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, count),
        .size = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, count),
        .flags = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, count),
        .source = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, count),
        .version = BUSTER_ARENA_PUSH_ARRAY(arena, uint16_t, count),
    };
    const bool result = mark->atom != 0 && mark->value != 0 && mark->size != 0 && mark->flags != 0 && mark->source != 0 && mark->version != 0;
    if (result)
    {
        memcpy(mark->atom, symbols->atom, count * sizeof(AtomId));
        memcpy(mark->value, symbols->value, count * sizeof(uint64_t));
        memcpy(mark->size, symbols->size, count * sizeof(uint64_t));
        memcpy(mark->flags, symbols->flags, count);
        memcpy(mark->source, symbols->source, count);
        memcpy(mark->version, symbols->version, count * sizeof(uint16_t));
    }
    return result;
}

static void link_benchmark_restore(const LinkBenchmarkMark* mark, Link* link, Arena* bytes)
{
    LinkSymbols* symbols = &link->symbols;
    const uint32_t count = mark->symbols;
    link->names.count = mark->names;
    link->atoms.count = mark->atoms;
    symbols->count = count;
    link->fixups.count = mark->fixups;
    link->fixups.wide_count = mark->wide_addends;
    arena_pop_to(link->name_arena, mark->name_bytes);
    arena_pop_to(bytes, mark->bytes);
    memcpy(symbols->atom, mark->atom, count * sizeof(AtomId));
    memcpy(symbols->value, mark->value, count * sizeof(uint64_t));
    memcpy(symbols->size, mark->size, count * sizeof(uint64_t));
    memcpy(symbols->flags, mark->flags, count);
    memcpy(symbols->source, mark->source, count);
    memcpy(symbols->version, mark->version, count * sizeof(uint16_t));
}

// Binding's state after bind_begin and the program's atoms: the struct and the arrays bind_end writes.
typedef struct LinkBenchmarkBinding LinkBenchmarkBinding;
struct LinkBenchmarkBinding
{
    Bind bind;
    SymbolId* name_symbols;
    uint32_t* named;
    SymbolId* symbols;
    uint64_t* members;
    // The COMDAT signatures that have joined: a static link's bind_end sets the bit of each group it keeps, and a run
    // that started with them set would discard the first group of each signature too.
    uint64_t* signatures;
};

static bool link_benchmark_save(LinkBenchmarkBinding* saved, const Bind* bind, Arena* arena)
{
    const Runtime* runtime = bind->runtime;
    const uint32_t words = (runtime->member_count + 63) / 64;
    const uint32_t signature_words = (runtime->group_signatures + 63) / 64;
    *saved = (LinkBenchmarkBinding){
        .bind = *bind,
        .name_symbols = BUSTER_ARENA_PUSH_ARRAY(arena, SymbolId, runtime->global_names.count),
        .named = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, runtime->global_names.count),
        .symbols = BUSTER_ARENA_PUSH_ARRAY(arena, SymbolId, runtime->link.symbols.count),
        .members = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, 2 * words + 1),
        .signatures = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, signature_words + 1),
    };
    const bool result = saved->name_symbols != 0 && saved->named != 0 && saved->symbols != 0 && saved->members != 0 && saved->signatures != 0;
    if (result)
    {
        memcpy(saved->name_symbols, bind->name_symbols, runtime->global_names.count * sizeof(SymbolId));
        memcpy(saved->named, bind->named, runtime->global_names.count * sizeof(uint32_t));
        memcpy(saved->symbols, bind->symbols, runtime->link.symbols.count * sizeof(SymbolId));
        memcpy(saved->members, bind->members_needed, words * sizeof(uint64_t));
        memcpy(saved->members + words, bind->members_joined, words * sizeof(uint64_t));
        memcpy(saved->signatures, bind->signatures, signature_words * sizeof(uint64_t));
    }
    return result;
}

static void link_benchmark_load(const LinkBenchmarkBinding* saved, Bind* bind)
{
    const Runtime* runtime = saved->bind.runtime;
    const uint32_t words = (runtime->member_count + 63) / 64;
    *bind = saved->bind;
    memcpy(bind->name_symbols, saved->name_symbols, runtime->global_names.count * sizeof(SymbolId));
    memcpy(bind->named, saved->named, runtime->global_names.count * sizeof(uint32_t));
    memcpy(bind->symbols, saved->symbols, runtime->link.symbols.count * sizeof(SymbolId));
    memcpy(bind->members_needed, saved->members, words * sizeof(uint64_t));
    memcpy(bind->members_joined, saved->members + words, words * sizeof(uint64_t));
    memcpy(bind->signatures, saved->signatures, (runtime->group_signatures + 63) / 64 * sizeof(uint64_t));
}

// ---------------------------------------------------------------------------------------------------------------------
// The direct path

// The parts of the direct path's link, each timed on lane 0.
typedef enum LinkBenchmarkPart
{
    // runtime_open, from the snapshot.
    LINK_BENCHMARK_PART_OPEN,
    // bind_begin, before the program's atoms.
    LINK_BENCHMARK_PART_BEGIN,
    // bind_end and dynamic_inputs, after them.
    LINK_BENCHMARK_PART_END,
    // executable_write.
    LINK_BENCHMARK_PART_WRITE,
    LINK_BENCHMARK_PARTS,
} LinkBenchmarkPart;

static const char* const link_benchmark_part_names[LINK_BENCHMARK_PARTS] = {
    [LINK_BENCHMARK_PART_OPEN] = "open",
    [LINK_BENCHMARK_PART_BEGIN] = "begin",
    [LINK_BENCHMARK_PART_END] = "end",
    [LINK_BENCHMARK_PART_WRITE] = "write",
};

static const char* const link_benchmark_phase_names[EXECUTABLE_PHASES] = {
    [EXECUTABLE_PHASE_COUNT] = "count",
    [EXECUTABLE_PHASE_LAYOUT] = "layout",
    [EXECUTABLE_PHASE_SYMBOLS] = "symbols",
    [EXECUTABLE_PHASE_DYNAMIC] = "dynamic",
    [EXECUTABLE_PHASE_OPEN] = "open",
    [EXECUTABLE_PHASE_CHUNKS] = "chunks",
    [EXECUTABLE_PHASE_FINISH] = "finish",
};

typedef struct LinkBenchmarkDirect LinkBenchmarkDirect;
struct LinkBenchmarkDirect
{
    // The kind of link: a dynamic PIE against libc.so.6, or a static executable or a static PIE against libc.a.
    RuntimeKind kind;
    LinkProgram* program;
    // The program's symbols as they were before binding.
    const LinkBenchmarkMark* begun;
    // The snapshot's directory, and the output.
    const char* cache;
    const char* path;
    // The runs after each warm-up, or 0 for the appendix's.
    uint64_t runs;
    // Binding's arrays and dynamic_inputs', which each run pops back; and where the marks keep their copies.
    Arena* arena;
    Arena* marks;
    // The runtime and binding that the runs of bind_end start from, as they were after the program's atoms.
    Runtime runtime;
    bool opened;
    Bind bind;
    LinkBenchmarkMark atoms_added;
    LinkBenchmarkBinding binding;
    uint64_t arena_length;
    ExecutableDynamic dynamic;
    ExecutableRuntime static_inputs;
    ExecutableOptions options;
    // What lane 0 measured: each part's runs and executable_write's phases in its runs, and over the warm-ups the page
    // faults and how much the peak resident set grew.
    uint64_t times[LINK_BENCHMARK_PARTS][BUSTER_LINK_BENCHMARK_MAX_RUNS];
    uint64_t phases[EXECUTABLE_PHASES][BUSTER_LINK_BENCHMARK_MAX_RUNS];
    uint32_t begin_runs;
    uint32_t end_runs;
    uint64_t faults;
    // The first timed run's page faults in runtime_open and in bind_begin.
    uint64_t open_faults;
    uint64_t begin_faults;
    uint64_t growth;
    bool growth_known;
    // The program's global names, which bind_begin looks up, and the bound link's fixups; and the atoms and symbols of the
    // first binding, fresh, which every run's must make again.
    uint32_t globals;
    uint64_t fixups;
    uint32_t bound_atoms;
    uint32_t bound_symbols;
    // Whether a binding failed with BIND_ERROR_SNAPSHOT: a piece of the runtime's snapshot is damaged.
    bool damaged;
    ExecutableResult result;
    // The warm-up's build ID, which every run's must repeat: each writes the same link.
    uint8_t build_id[BUSTER_EXECUTABLE_BUILD_ID_BYTES];
    bool same_files;
    const char* problem;
    bool succeeded;
};

// The kind's runtime from its snapshot in `cache`: runtime_open, runtime_open_static, runtime_open_static_pie or
// runtime_open_shared.
static SnapshotResult link_benchmark_open(Runtime* runtime, const char* cache, RuntimeKind kind)
{
    SnapshotResult result;
    if (kind == RUNTIME_KIND_STATIC)
    {
        result = runtime_open_static(runtime, cache);
    }
    else if (kind == RUNTIME_KIND_STATIC_PIE)
    {
        result = runtime_open_static_pie(runtime, cache);
    }
    else if (kind == RUNTIME_KIND_SHARED)
    {
        result = runtime_open_shared(runtime, cache);
    }
    else
    {
        result = runtime_open(runtime, cache);
    }
    return result;
}

// Lane 0's part before the program's atoms: the runs of runtime_open (or the static kinds') and bind_begin, each from
// the program's symbols as they were before binding; then the runtime and binding the runs of bind_end start from, after
// the program's atoms (its code generation), and the link checked once, by link_program_generate_end after a bind_end
// that those runs put back. Returns whether all of it succeeded.
static bool link_benchmark_direct_begin(LinkBenchmarkDirect* direct)
{
    LinkProgram* program = direct->program;
    Link* link = &program->link;
    const uint32_t units = program->options.units;
    const uint64_t length = direct->arena->length;
    // These grow with the program's symbols, not with its output.
    direct->begin_runs = link_benchmark_run_count(direct->runs, 0, false);
    bool result = true;
    for (uint32_t run = 0; result && run <= direct->begin_runs; run += 1)
    {
        link_benchmark_restore(direct->begun, link, &program->bytes);
        arena_pop_to(direct->arena, length);
        const LinkBenchmarkCost cost = run == 0 ? link_benchmark_cost_begin() : (LinkBenchmarkCost){ 0 };
        Runtime runtime;
        Bind bind = { 0 };
        // The first timed run counts each part's page faults, outside its time.
        const bool counted = run == 1;
        const uint64_t faulted = counted ? link_benchmark_faults() : 0;
        const uint64_t start = executable_now();
        const SnapshotResult opened = link_benchmark_open(&runtime, direct->cache, direct->kind);
        const uint64_t opened_at = executable_now();
        const uint64_t opened_faults = counted ? link_benchmark_faults() : 0;
        const bool loaded = opened.status == SNAPSHOT_STATUS_LOADED;
        const uint64_t begun_at = counted ? executable_now() : opened_at;
        result = loaded && bind_begin(&bind, link, &runtime, BIND_PATH_VECTOR, &program->bytes, units, direct->arena);
        const uint64_t end = executable_now();
        if (counted)
        {
            direct->open_faults = opened_faults - faulted;
            direct->begin_faults = link_benchmark_faults() - opened_faults;
        }
        direct->damaged = direct->damaged || bind.error == BIND_ERROR_SNAPSHOT;
        if (run == 0)
        {
            link_benchmark_cost_end(&cost, &direct->faults, &direct->growth, &direct->growth_known);
        }
        else
        {
            direct->times[LINK_BENCHMARK_PART_OPEN][run - 1] = opened_at - start;
            direct->times[LINK_BENCHMARK_PART_BEGIN][run - 1] = end - begun_at;
        }
        direct->problem = !loaded ? "the runtime didn't load from its snapshot" : !result ? "bind_begin failed" : 0;
        runtime_release(&runtime);
    }
    link_benchmark_restore(direct->begun, link, &program->bytes);
    arena_pop_to(direct->arena, length);
    direct->opened = result;
    const SnapshotStatus status = result ? link_benchmark_open(&direct->runtime, direct->cache, direct->kind).status : SNAPSHOT_STATUS_MISSING;
    result = result && status == SNAPSHOT_STATUS_LOADED;
    result = result && bind_begin(&direct->bind, link, &direct->runtime, BIND_PATH_VECTOR, &program->bytes, units, direct->arena);
    result = result && link_program_generate_atoms(program);
    result = result && link_benchmark_mark(&direct->atoms_added, link, &program->bytes, direct->marks);
    result = result && link_benchmark_save(&direct->binding, &direct->bind, direct->marks);
    direct->arena_length = direct->arena->length;
    result = result && bind_end(&direct->bind, link) && link_program_generate_end(program);
    direct->fixups = link->fixups.count - 1;
    direct->bound_atoms = link->atoms.count;
    direct->bound_symbols = link->symbols.count;
    direct->damaged = direct->damaged || direct->bind.error == BIND_ERROR_SNAPSHOT;
    if (!result && direct->problem == 0)
    {
        direct->problem = program->problem != 0 ? program->problem : "the program didn't bind";
    }
    direct->problem = direct->damaged ? "binding found a piece of the runtime's snapshot damaged" : direct->problem;
    return result;
}

// The lanes' entry point: lane 0's part before the program's atoms, then the runs of bind_end and dynamic_inputs (or
// dynamic_static_inputs, dynamic_static_pie_inputs or dynamic_shared_inputs) on lane 0 and executable_write on every
// lane, each from the link and binding as they were after the atoms, whose members' bytes bind_end copies again (the
// static runtime's TLS relaxation rewrites them, and a static PIE's GOT relaxation the program's too, which the writer
// relaxes alike when it finds them relaxed). The warm-up's output decides how many runs follow it.
static void link_benchmark_direct_main(void* argument)
{
    LinkBenchmarkDirect* direct = argument;
    LinkProgram* program = direct->program;
    Link* link = &program->link;
    const bool first = lane_index() == 0;
    bool succeeded = lane_broadcast(first && link_benchmark_direct_begin(direct) ? 1 : 0, 0) != 0;
    uint32_t runs = 0;
    for (uint32_t run = 0; succeeded && run <= runs; run += 1)
    {
        LinkBenchmarkCost cost = { 0 };
        uint64_t start = 0;
        uint64_t bound_at = 0;
        bool bound = false;
        if (first)
        {
            link_benchmark_restore(&direct->atoms_added, link, &program->bytes);
            link_benchmark_load(&direct->binding, &direct->bind);
            arena_pop_to(direct->arena, direct->arena_length);
            cost = run == 0 ? link_benchmark_cost_begin() : cost;
            start = executable_now();
            bound = bind_end(&direct->bind, link);
            direct->damaged = direct->damaged || direct->bind.error == BIND_ERROR_SNAPSHOT;
            if (bound && direct->kind == RUNTIME_KIND_STATIC)
            {
                dynamic_static_inputs(&direct->static_inputs, &direct->runtime, &direct->bind);
            }
            else if (bound && direct->kind == RUNTIME_KIND_STATIC_PIE)
            {
                dynamic_static_pie_inputs(&direct->dynamic, &direct->static_inputs, &direct->runtime, &direct->bind);
            }
            else if (direct->kind == RUNTIME_KIND_SHARED)
            {
                // Without a soname, as the linkers' -shared links are.
                bound = bound && dynamic_shared_inputs(&direct->dynamic, &direct->runtime, &direct->bind, (String8){ 0 }, direct->arena);
            }
            else
            {
                bound = bound && dynamic_inputs(&direct->dynamic, &direct->runtime, &direct->bind, direct->arena);
            }
            // A shared library has no entry point.
            direct->options.entry = bound && direct->kind != RUNTIME_KIND_SHARED ? dynamic_global(&direct->runtime, &direct->bind, "_start") : 0;
            bound_at = executable_now();
            // The link the first binding made, fresh: a run that restored less than binding writes would make another.
            const bool same = link->atoms.count == direct->bound_atoms && link->symbols.count == direct->bound_symbols && link->fixups.count - 1 == direct->fixups;
            direct->problem = bound && !same ? "a run's binding made another link than the first" : direct->problem;
            bound = bound && same;
        }
        bound = lane_broadcast(bound ? 1 : 0, 0) != 0;
        succeeded = bound && executable_write(link, &direct->options, &direct->result);
        const uint64_t end = first ? executable_now() : 0;
        if (first && run == 0)
        {
            link_benchmark_cost_end(&cost, &direct->faults, &direct->growth, &direct->growth_known);
            memcpy(direct->build_id, direct->result.build_id, sizeof(direct->build_id));
        }
        else if (first)
        {
            direct->same_files = direct->same_files && memcmp(direct->build_id, direct->result.build_id, sizeof(direct->build_id)) == 0;
            direct->times[LINK_BENCHMARK_PART_END][run - 1] = bound_at - start;
            direct->times[LINK_BENCHMARK_PART_WRITE][run - 1] = end - bound_at;
            uint64_t previous = direct->result.start_time;
            for (uint32_t phase = 0; phase < EXECUTABLE_PHASES; phase += 1)
            {
                direct->phases[phase][run - 1] = direct->result.phase_times[phase] - previous;
                previous = direct->result.phase_times[phase];
            }
        }
        if (run == 0)
        {
            runs = (uint32_t)lane_broadcast(first ? link_benchmark_run_count(direct->runs, direct->result.file_size, false) : 0, 0);
        }
    }
    if (first)
    {
        direct->end_runs = runs;
        direct->succeeded = succeeded;
        direct->problem = !succeeded && direct->problem == 0 ? "bind_end or executable_write failed" : direct->problem;
        direct->problem = direct->damaged ? "binding found a piece of the runtime's snapshot damaged" : direct->problem;
    }
}

// The floor: the output's `size` bytes, from memory, written over it as executable_write writes it in place, opened and
// closed: the nanoseconds one pwrite takes, or 0 if it failed.
static uint64_t link_benchmark_floor(const char* path, const uint8_t* bytes, uint64_t size)
{
    const uint64_t start = executable_now();
    const int file = open(path, O_RDWR | O_CLOEXEC);
    const struct iovec piece = {
        .iov_base = (void*)bytes,
        .iov_len = size,
    };
    const int error = file >= 0 ? executable_write_all(file, &piece, 1, 0, false) : errno;
    if (file >= 0)
    {
        close(file);
    }
    const uint64_t end = executable_now();
    return error == 0 ? end - start : 0;
}

// The floor's median over `runs` runs after a warm-up, from the file at `path` read into memory, in microseconds, or 0
// if it can't be had.
static double link_benchmark_floor_median(const char* path, uint64_t size, uint32_t runs)
{
    uint8_t* bytes = size != 0 ? mmap(0, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    const int file = bytes != MAP_FAILED ? open(path, O_RDONLY | O_CLOEXEC) : -1;
    bool read_whole = file >= 0 && pread(file, bytes, size, 0) == (ssize_t)size;
    if (file >= 0)
    {
        close(file);
    }
    uint64_t floors[BUSTER_LINK_BENCHMARK_MAX_RUNS];
    for (uint32_t run = 0; read_whole && run <= runs; run += 1)
    {
        const uint64_t time = link_benchmark_floor(path, bytes, size);
        read_whole = time != 0;
        floors[run == 0 ? 0 : run - 1] = time;
    }
    if (bytes != MAP_FAILED)
    {
        munmap(bytes, size);
    }
    return read_whole ? link_benchmark_median(floors, runs) : 0.0;
}

// The direct path's link of the program, as a dynamic PIE, a static executable or a static PIE by `kind`: its parts'
// medians, its work and the floor, and its output run, which must print the program's output. *total gets the link's
// time in microseconds, and `expected` the program's output. *damaged says whether a binding failed because a piece of
// the kind's snapshot is damaged (BIND_ERROR_SNAPSHOT), which the measurement can't go on from.
static bool link_benchmark_direct_attempt(const LinkBenchmarkSettings* settings, RuntimeKind kind, LinkBenchmarkProgram linked, uint8_t* expected, double* total,
                                          bool* damaged)
{
    const char* path = settings->direct_path;
    const LinkProgramOptions options = link_benchmark_options(linked, settings->seed, kind);
    uint64_t name_bytes = 0;
    uint64_t atom_bytes = 0;
    const LinkCapacity extra = bind_capacity(settings->runtimes[kind], &name_bytes, &atom_bytes);
    Arena arena = arena_reserve(BUSTER_LINK_BENCHMARK_ARENA_BYTES);
    Arena marks = arena_reserve(BUSTER_LINK_BENCHMARK_ARENA_BYTES);
    LinkBenchmarkDirect* direct = calloc(1, sizeof(LinkBenchmarkDirect));
    LinkProgram program = { 0 };
    bool result = direct != 0 && arena.base != 0 && marks.base != 0 && link_program_generate_begin(&program, options, extra, name_bytes, atom_bytes);
    LinkBenchmarkMark begun;
    result = result && link_benchmark_mark(&begun, &program.link, &program.bytes, &marks);
    if (result)
    {
        const LinkSymbols* symbols = &program.link.symbols;
        for (SymbolId symbol = 1; symbol < symbols->count; symbol += 1)
        {
            direct->globals += symbols->name[symbol] != 0 && (symbols->flags[symbol] & BUSTER_LINK_SYMBOL_BINDING) != BUSTER_LINK_SYMBOL_LOCAL ? 1 : 0;
        }
        direct->kind = kind;
        direct->program = &program;
        direct->begun = &begun;
        direct->cache = settings->cache;
        direct->path = path;
        direct->runs = settings->runs;
        direct->arena = &arena;
        direct->marks = &marks;
        direct->growth_known = true;
        direct->same_files = true;
        direct->options.path = (String8){
            .pointer = (char*)path,
            .length = strlen(path),
        };
        // A static PIE has both.
        direct->options.dynamic = kind != RUNTIME_KIND_STATIC ? &direct->dynamic : 0;
        direct->options.runtime = runtime_kind_is_static(kind) ? &direct->static_inputs : 0;
        const bool ran = lane_run(settings->lanes, link_benchmark_direct_main, direct, settings->cpus);
        result = ran && direct->succeeded && direct->same_files;
        if (!result)
        {
            const char* problem = !ran ? "its lanes couldn't run on --cpus" : !direct->succeeded ? direct->problem : "its runs wrote different files";
            printf("  the direct path: %s (error %d)\n", problem, (int)direct->result.error);
        }
    }
    else
    {
        printf("  the direct path: %s\n", program.problem != 0 ? program.problem : "out of memory");
    }
    *damaged = direct != 0 && direct->damaged;
    if (result)
    {
        const ExecutableResult* written = &direct->result;
        double parts[LINK_BENCHMARK_PARTS];
        double sum = 0.0;
        for (uint32_t part = 0; part < LINK_BENCHMARK_PARTS; part += 1)
        {
            const uint32_t runs = part < LINK_BENCHMARK_PART_END ? direct->begin_runs : direct->end_runs;
            parts[part] = link_benchmark_median(direct->times[part], runs);
            sum += parts[part];
        }
        *total = sum;
        memcpy(expected, program.output, BUSTER_LINK_PROGRAM_OUTPUT_BYTES);
        printf("  %-24s %12.1f us:", "direct path", sum);
        for (uint32_t part = 0; part < LINK_BENCHMARK_PARTS; part += 1)
        {
            printf(" %s %.1f", link_benchmark_part_names[part], parts[part]);
        }
        printf(" (");
        for (uint32_t phase = 0; phase < EXECUTABLE_PHASES; phase += 1)
        {
            printf("%s%s %.1f", phase == 0 ? "" : ", ", link_benchmark_phase_names[phase], link_benchmark_median(direct->phases[phase], direct->end_runs));
        }
        printf(")\n");
        printf("  %-24s %12.1f us: one pwrite of the output in place\n", "floor", link_benchmark_floor_median(path, written->file_size, direct->end_runs));
        printf("  %-24s %llu bytes written, %llu fixups applied, %u names looked up, %llu page faults (a run's open %llu, begin %llu), ", "work",
               (unsigned long long)written->file_size, (unsigned long long)direct->fixups, direct->globals, (unsigned long long)direct->faults,
               (unsigned long long)direct->open_faults, (unsigned long long)direct->begin_faults);
        if (direct->growth_known)
        {
            printf("%.2f MB more resident\n", (double)direct->growth / 1e6);
        }
        else
        {
            printf("the resident set's growth unknown\n");
        }
        result = link_benchmark_runs_as_predicted(settings, kind, path, expected);
    }
    if (direct != 0 && direct->opened)
    {
        runtime_release(&direct->runtime);
    }
    link_program_release(&program);
    arena_release(&marks);
    arena_release(&arena);
    free(direct);
    return result;
}

// link_benchmark_direct_attempt, again once if a binding found a piece of the kind's snapshot damaged
// (BIND_ERROR_SNAPSHOT), which runtime_open's checks of the directory don't see, as a link's caller recovers from it:
// the measurement is thrown away, the snapshot discarded (snapshot_discard) and written again by an open outside any
// measurement, and the program generated and measured again from the start, its runs loading the new snapshot. Without
// it, the damaged snapshot would stay in the cache and fail every later run.
static bool link_benchmark_direct(const LinkBenchmarkSettings* settings, RuntimeKind kind, LinkBenchmarkProgram linked, uint8_t* expected, double* total)
{
    bool damaged = false;
    bool result = link_benchmark_direct_attempt(settings, kind, linked, expected, total, &damaged);
    if (!result && damaged)
    {
        Runtime runtime;
        const bool discarded = snapshot_discard(settings->cache, kind);
        const SnapshotResult reopened = discarded ? link_benchmark_open(&runtime, settings->cache, kind) : (SnapshotResult){ .status = SNAPSHOT_STATUS_MISSING };
        if (discarded)
        {
            runtime_release(&runtime);
        }
        const bool written = discarded && reopened.error == RUNTIME_ERROR_NONE && reopened.written;
        printf("  the direct path: %s\n", written ? "the snapshot discarded and written again, and the program measured again" : "the snapshot couldn't be written again");
        result = written && link_benchmark_direct_attempt(settings, kind, linked, expected, total, &damaged);
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The objects

typedef struct LinkBenchmarkObjects LinkBenchmarkObjects;
struct LinkBenchmarkObjects
{
    const Link* link;
    uint32_t units;
    // Each unit's path and STT_FILE name, and each object's error.
    const char* const* paths;
    const String8* source_names;
    ObjectError* errors;
    // object_prepare's units, from `arena`, which each run pops back.
    Arena* arena;
    ObjectUnits prepared;
    // The runs after the warm-up, or 0 for the appendix's.
    uint64_t runs;
    // What lane 0 measured: the runs' times and how many, and the objects' bytes after the warm-up.
    uint64_t times[BUSTER_LINK_BENCHMARK_MAX_RUNS];
    uint32_t run_count;
    uint64_t bytes;
    bool succeeded;
};

// The objects' bytes, from their files' sizes.
static uint64_t link_benchmark_object_bytes(const LinkBenchmarkObjects* objects)
{
    uint64_t result = 0;
    for (uint32_t unit = 0; unit < objects->units; unit += 1)
    {
        struct stat status;
        result += stat(objects->paths[unit], &status) == 0 ? (uint64_t)status.st_size : 0;
    }
    return result;
}

// The lanes' entry point: the runs of object_prepare on lane 0 and object_write_units on every lane, over the files of
// the run before. The warm-up's objects decide how many runs follow it.
static void link_benchmark_objects_main(void* argument)
{
    LinkBenchmarkObjects* objects = argument;
    const bool first = lane_index() == 0;
    const uint64_t length = first ? objects->arena->length : 0;
    uint32_t runs = 0;
    bool succeeded = true;
    for (uint32_t run = 0; succeeded && run <= runs; run += 1)
    {
        uint64_t start = 0;
        bool prepared = false;
        if (first)
        {
            arena_pop_to(objects->arena, length);
            start = executable_now();
            prepared = object_prepare(&objects->prepared, objects->link, objects->units, objects->arena);
        }
        prepared = lane_broadcast(prepared ? 1 : 0, 0) != 0;
        const ObjectOptions options = { 0 };
        succeeded = prepared && object_write_units(objects->link, &objects->prepared, options, objects->paths, objects->source_names, objects->errors);
        const uint64_t end = first ? executable_now() : 0;
        if (first && run > 0)
        {
            objects->times[run - 1] = end - start;
        }
        if (first && run == 0)
        {
            objects->bytes = link_benchmark_object_bytes(objects);
        }
        if (run == 0)
        {
            runs = (uint32_t)lane_broadcast(first ? link_benchmark_run_count(objects->runs, objects->bytes, false) : 0, 0);
        }
    }
    if (first)
    {
        objects->run_count = runs;
        objects->succeeded = succeeded;
    }
}

// The objects' symbols and relocations, each object built once more on the calling thread.
static bool link_benchmark_object_counts(LinkBenchmarkObjects* objects, uint64_t* symbols, uint64_t* relocations)
{
    const uint64_t length = objects->arena->length;
    ObjectSymbols mapped;
    bool result = object_map_symbols(&mapped, objects->link, objects->arena);
    for (uint32_t unit = 0; result && unit < objects->units; unit += 1)
    {
        const uint64_t before = objects->arena->length;
        const ObjectOptions options = { 0 };
        const Object object = object_build(objects->link, &objects->prepared, &mapped, unit, options, objects->source_names[unit], objects->arena);
        result = object.error == OBJECT_ERROR_NONE;
        *symbols += object.symbol_count;
        *relocations += object.relocation_count;
        arena_pop_to(objects->arena, before);
    }
    arena_pop_to(objects->arena, length);
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The linkers

// The arguments of the objects' link by `linker` into `output`, the appendix's command for a dynamic PIE, or for a
// static executable or a static PIE with the static runtime `runtime` (the clang driver's for -static and -static-pie),
// with `one_thread` and `no_fork` (each 0 for none) after the matched options; `search` holds the two -L options.
// `arguments` has room for BUSTER_LINK_BENCHMARK_ARGUMENTS and the objects. Returns how many, without the null that ends
// them.
static uint32_t link_benchmark_arguments(char** arguments, const char* linker, const char* one_thread, const char* no_fork, const Runtime* runtime, char* const* search,
                                         const char* const* objects, uint32_t object_count, const char* output)
{
    // The clang driver's group for -static and -static-pie, which links libc.a without libatomic.a: the programs need
    // none of it.
    static const char* const group[] = {
        "--start-group",
        "-lgcc",
        "-lgcc_eh",
        "-lc",
        "--end-group",
    };
    // The driver's options for -static-pie.
    static const char* const static_pie[] = {
        "-static",
        "-pie",
        "--no-dynamic-linker",
        "-z",
        "text",
    };
    const bool static_link = runtime_kind_is_static(runtime->kind);
    static const char* const matched[] = {
        "--hash-style=gnu",
        "--build-id",
        "--eh-frame-hdr",
        "-m",
        "elf_x86_64",
        "-z",
        "now",
        "--no-gc-sections",
    };
    static const char* const libraries[] = {
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
    uint32_t count = 0;
    const bool ours = strcmp(linker, "link-foreign") == 0;
    arguments[count++] = ours ? (char*)"/proc/self/exe" : (char*)linker;
    if (ours)
    {
        arguments[count++] = (char*)"link-foreign";
    }
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(matched); i += 1)
    {
        arguments[count++] = (char*)matched[i];
    }
    if (one_thread != 0)
    {
        arguments[count++] = (char*)one_thread;
    }
    if (no_fork != 0)
    {
        arguments[count++] = (char*)no_fork;
    }
    if (runtime->kind == RUNTIME_KIND_STATIC)
    {
        arguments[count++] = "-static";
    }
    else if (runtime->kind == RUNTIME_KIND_STATIC_PIE)
    {
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(static_pie); i += 1)
        {
            arguments[count++] = (char*)static_pie[i];
        }
    }
    else if (runtime->kind == RUNTIME_KIND_SHARED)
    {
        arguments[count++] = "-shared";
    }
    else
    {
        arguments[count++] = "-pie";
        arguments[count++] = "-dynamic-linker";
        arguments[count++] = runtime->loader_path.pointer;
    }
    arguments[count++] = "-o";
    arguments[count++] = (char*)output;
    // A shared library's runtime has no start object.
    if (runtime->kind != RUNTIME_KIND_SHARED)
    {
        arguments[count++] = runtime->object_paths[RUNTIME_OBJECT_START].pointer;
    }
    arguments[count++] = runtime->object_paths[RUNTIME_OBJECT_INIT_PROLOGUE].pointer;
    arguments[count++] = runtime->object_paths[RUNTIME_OBJECT_BEGIN].pointer;
    arguments[count++] = search[0];
    arguments[count++] = search[1];
    for (uint32_t i = 0; i < object_count; i += 1)
    {
        arguments[count++] = (char*)objects[i];
    }
    for (uint32_t i = 0; !static_link && i < BUSTER_ARRAY_LENGTH(libraries); i += 1)
    {
        arguments[count++] = (char*)libraries[i];
    }
    for (uint32_t i = 0; static_link && i < BUSTER_ARRAY_LENGTH(group); i += 1)
    {
        arguments[count++] = (char*)group[i];
    }
    arguments[count++] = runtime->object_paths[RUNTIME_OBJECT_END].pointer;
    arguments[count++] = runtime->object_paths[RUNTIME_OBJECT_INIT_EPILOGUE].pointer;
    arguments[count] = 0;
    return count;
}

// What a linker's runs on its own threads or on one took.
typedef struct LinkBenchmarkLinked LinkBenchmarkLinked;
struct LinkBenchmarkLinked
{
    uint64_t times[BUSTER_LINK_BENCHMARK_MAX_RUNS];
    uint32_t runs;
    uint64_t output_bytes;
    uint64_t faults;
    uint64_t peak;
};

// Shows the start of what a failed run of `name` wrote to its standard error.
static void link_benchmark_show_errors(const LinkBenchmarkSettings* settings, const char* name, int status)
{
    char errors[BUSTER_LINK_BENCHMARK_ERROR_BYTES + 1];
    link_benchmark_read(settings->error_path, errors, sizeof(errors));
    printf("  %s: status %d: %s\n", name, status, errors);
}

// The helper's mode, `benchmark-link --rusage OUTPUT ERRORS COMMAND...`: it runs COMMAND with its standard output and
// error in the files OUTPUT and ERRORS, and prints its exit status, its page faults and its peak resident set in KB.
// The linkers' counts come from it: posix_spawn's child shares the spawning process's memory until it execs, and Linux
// then records that memory's peak resident set as the child's own, so a linker spawned by the benchmark would report
// the benchmark's peak whenever its own was lower. The helper is this executable run again, whose peak is a few
// megabytes.
static int link_benchmark_rusage(int argc, char** argv)
{
    int status = -1;
    struct rusage usage = { 0 };
    const bool valid = argc >= 5;
    if (valid)
    {
        link_benchmark_spawn(argv + 4, argv[2], argv[3], &status, &usage);
        printf("%d %llu %llu\n", status, (unsigned long long)usage.ru_minflt + (unsigned long long)usage.ru_majflt, (unsigned long long)usage.ru_maxrss);
    }
    return valid ? 0 : 1;
}

// Runs `command` (with room for BUSTER_LINK_BENCHMARK_HELPER_ARGUMENTS before it) through the helper, which puts its
// standard output and error in the settings' files: whether the helper counted it, with its exit status, its page
// faults and its peak resident set in bytes.
static bool link_benchmark_count(const LinkBenchmarkSettings* settings, char** command, int* status, uint64_t* faults, uint64_t* peak)
{
    char** arguments = command - BUSTER_LINK_BENCHMARK_HELPER_ARGUMENTS;
    arguments[0] = "/proc/self/exe";
    arguments[1] = "benchmark-link";
    arguments[2] = "--rusage";
    arguments[3] = (char*)settings->output_path;
    arguments[4] = (char*)settings->error_path;
    int helper_status = -1;
    link_benchmark_spawn(arguments, settings->usage_path, "/dev/null", &helper_status, 0);
    char counts[128];
    unsigned long long fault_count = 0;
    unsigned long long kilobytes = 0;
    const bool result = helper_status == 0 && link_benchmark_read(settings->usage_path, counts, sizeof(counts)) > 0 &&
                        sscanf(counts, "%d %llu %llu", status, &fault_count, &kilobytes) == 3;
    *faults = fault_count;
    *peak = kilobytes * 1024;
    return result;
}

// One linker's links of the objects, with its own threads or on one: a warm-up, the runs with a pause before each, and
// one more without forking, through the helper, which counts its page faults and its peak resident set; then its
// output, which must print the program's output. The calling thread's CPUs are the process's, or with `one_thread` the
// first of --cpus, while it runs them. `arguments` has room for BUSTER_LINK_BENCHMARK_HELPER_ARGUMENTS before the
// linker's.
static bool link_benchmark_linker(const LinkBenchmarkSettings* settings, const Runtime* runtime, const LinkBenchmarkLinker* linker, bool one_thread, char** arguments,
                                  char* const* search, const char* const* objects, uint32_t object_count, const char* output, const uint8_t* expected,
                                  LinkBenchmarkLinked* linked)
{
    uint64_t pinned[BUSTER_LANE_CPU_MASK_WORDS] = { 0 };
    pinned[settings->cpus[0] / 64] = 1ull << (settings->cpus[0] % 64);
    bool result = link_benchmark_set_cpus(one_thread ? pinned : settings->allowed);
    const char* thread_option = one_thread ? linker->one_thread : linker->own_threads;
    char** command = arguments + BUSTER_LINK_BENCHMARK_HELPER_ARGUMENTS;
    link_benchmark_arguments(command, linker->name, thread_option, 0, runtime, search, objects, object_count, output);
    int status = -1;
    link_benchmark_spawn(command, settings->output_path, settings->error_path, &status, 0);
    if (result && status != 0)
    {
        link_benchmark_show_errors(settings, linker->name, status);
    }
    result = result && status == 0;
    struct stat written;
    linked->output_bytes = result && stat(output, &written) == 0 ? (uint64_t)written.st_size : 0;
    linked->runs = link_benchmark_run_count(settings->runs, linked->output_bytes, linker->slow);
    const uint32_t pause = link_benchmark_pause(linked->output_bytes, linker->slow);
    for (uint32_t run = 0; result && run < linked->runs; run += 1)
    {
        link_benchmark_sleep(pause);
        linked->times[run] = link_benchmark_spawn(command, "/dev/null", "/dev/null", &status, 0);
        result = status == 0;
        if (!result)
        {
            printf("  %s: run %u of %u exited with status %d\n", linker->name, run + 1, linked->runs, status);
        }
    }
    // The counts' run, through the helper.
    link_benchmark_arguments(command, linker->name, thread_option, linker->no_fork, runtime, search, objects, object_count, output);
    if (result)
    {
        link_benchmark_sleep(pause);
    }
    const bool counted = result && link_benchmark_count(settings, command, &status, &linked->faults, &linked->peak);
    if (counted && status != 0)
    {
        link_benchmark_show_errors(settings, linker->name, status);
    }
    else if (result && !counted)
    {
        printf("  %s: the helper didn't count its run\n", linker->name);
    }
    result = counted && status == 0;
    result = result && link_benchmark_runs_as_predicted(settings, runtime->kind, output, expected);
    result = link_benchmark_set_cpus(settings->allowed) && result;
    return result;
}

// Whether wild 0.10 may link a program's static executable or static PIE wrong: when .tbss is more aligned than .tdata,
// it starts the TLS segment at .tdata's alignment, past a multiple of the segment's wherever the layout puts it there,
// and glibc's static startup (a static PIE's too), which takes the image to start on the alignment, then faults (a C
// program with a 16-aligned zero thread-local beside an 8-aligned one shows it; GNU ld, lld, mold and the direct path
// start the segment aligned). libc.a's .tdata is aligned to 8.
static bool link_benchmark_wild_tls(const Link* link)
{
    uint32_t tdata = 3;
    uint32_t tbss = 0;
    for (AtomId atom = 1; atom < link->atoms.count; atom += 1)
    {
        const uint8_t class = link->atoms.class[atom];
        const uint32_t log2 = link->atoms.alignment_log2[atom];
        tdata = class == BUSTER_LINK_CLASS_TDATA && log2 > tdata ? log2 : tdata;
        tbss = class == BUSTER_LINK_CLASS_TBSS && log2 > tbss ? log2 : tbss;
    }
    return tbss > tdata;
}

// The program's objects written into the work directory's objects, and linked there by each linker of the settings as
// a dynamic PIE, a static executable or a static PIE by `kind`, with their figures against the direct path's
// `direct_total` microseconds (0 if it didn't run). With `known`, the program's output must be `expected`, which
// otherwise it sets. A static executable or static PIE that wild may link wrong (link_benchmark_wild_tls) isn't linked
// by it.
static bool link_benchmark_linkers_on(const LinkBenchmarkSettings* settings, RuntimeKind kind, LinkBenchmarkProgram linked_program, uint8_t* expected, bool known,
                                      double direct_total)
{
    const char* directory = settings->work;
    const bool keep = settings->keep;
    const LinkProgramOptions options = link_benchmark_options(linked_program, settings->seed, kind);
    const uint32_t units = options.units;
    LinkProgram program = { 0 };
    Arena arena = arena_reserve(BUSTER_LINK_BENCHMARK_ARENA_BYTES);
    const uint64_t path_room = (uint64_t)units * BUSTER_LINK_BENCHMARK_PATH_BYTES;
    char* path_bytes = calloc(path_room + 2 * BUSTER_LINK_BENCHMARK_PATH_BYTES, 1);
    char** paths = calloc(units, sizeof(char*));
    String8* source_names = calloc(units, sizeof(String8));
    ObjectError* errors = calloc(units, sizeof(ObjectError));
    char** arguments = calloc((uint64_t)units + BUSTER_LINK_BENCHMARK_ARGUMENTS + BUSTER_LINK_BENCHMARK_HELPER_ARGUMENTS, sizeof(char*));
    LinkBenchmarkObjects* objects = calloc(1, sizeof(LinkBenchmarkObjects));
    LinkBenchmarkLinked* linked = calloc(1, sizeof(LinkBenchmarkLinked));
    char objects_directory[BUSTER_LINK_BENCHMARK_PATH_BYTES];
    snprintf(objects_directory, sizeof(objects_directory), "%s/objects", directory);
    bool result = arena.base != 0 && path_bytes != 0 && paths != 0 && source_names != 0 && errors != 0 && arguments != 0 && objects != 0 && linked != 0;
    result = result && (mkdir(objects_directory, 0777) == 0 || errno == EEXIST);
    result = result && link_program_generate(&program, options);
    if (!result)
    {
        printf("  the objects: %s\n", program.problem != 0 ? program.problem : "out of memory, or no directory for them");
    }
    else if (known && memcmp(program.output, expected, BUSTER_LINK_PROGRAM_OUTPUT_BYTES) != 0)
    {
        printf("  the objects: the program generated again prints something else\n");
        result = false;
    }
    else
    {
        memcpy(expected, program.output, BUSTER_LINK_PROGRAM_OUTPUT_BYTES);
    }
    char* names = path_bytes != 0 ? path_bytes + path_room : 0;
    for (uint32_t unit = 0; result && unit < units; unit += 1)
    {
        paths[unit] = path_bytes + (uint64_t)unit * BUSTER_LINK_BENCHMARK_PATH_BYTES;
        snprintf(paths[unit], BUSTER_LINK_BENCHMARK_PATH_BYTES, "%s/unit_%u.o", objects_directory, unit);
        // The STT_FILE names point into the paths: "unit_N", which the source file would be named after.
        char* name = strrchr(paths[unit], '/') + 1;
        source_names[unit] = (String8){
            .pointer = name,
            .length = strlen(name) - 2,
        };
    }
    if (result)
    {
        *objects = (LinkBenchmarkObjects){
            .link = &program.link,
            .units = units,
            .paths = (const char* const*)paths,
            .source_names = source_names,
            .errors = errors,
            .arena = &arena,
            .runs = settings->runs,
        };
        result = lane_run(settings->lanes, link_benchmark_objects_main, objects, settings->cpus) && objects->succeeded;
        uint64_t symbols = 0;
        uint64_t relocations = 0;
        result = result && link_benchmark_object_counts(objects, &symbols, &relocations);
        if (result)
        {
            printf("  %-24s %12.1f us: %u files of %llu bytes, %llu symbols, %llu relocations\n", "objects", link_benchmark_median(objects->times, objects->run_count), units,
                   (unsigned long long)objects->bytes, (unsigned long long)symbols, (unsigned long long)relocations);
        }
        else
        {
            printf("  the objects: unit 0's error %d\n", (int)errors[0]);
        }
    }
    const bool wild_wrong = result && runtime_kind_is_static(kind) && link_benchmark_wild_tls(&program.link);
    // The linkers get the memory the program had.
    link_program_release(&program);
    // The -L options: GCC's directory and libc.so's, or for a static executable or a static PIE libc.a's.
    const Runtime* runtime = settings->runtimes[kind];
    char* search[2] = {
        names,
        names + BUSTER_LINK_BENCHMARK_PATH_BYTES,
    };
    const String8 script = runtime_kind_is_static(kind) ? runtime->archives[runtime->archive_count - 1].path : runtime->libc_script;
    uint64_t directory_length = script.length;
    while (directory_length > 0 && script.pointer[directory_length - 1] != '/')
    {
        directory_length -= 1;
    }
    if (result)
    {
        snprintf(search[0], BUSTER_LINK_BENCHMARK_PATH_BYTES, "-L%.*s", (int)runtime->gcc_directory.length, runtime->gcc_directory.pointer);
        snprintf(search[1], BUSTER_LINK_BENCHMARK_PATH_BYTES, "-L%.*s", (int)(directory_length > 1 ? directory_length - 1 : directory_length), script.pointer);
    }
    for (uint32_t l = 0; result && l < BUSTER_LINK_BENCHMARK_LINKERS; l += 1)
    {
        const LinkBenchmarkLinker* linker = &link_benchmark_linkers[l];
        char output[BUSTER_LINK_BENCHMARK_PATH_BYTES];
        snprintf(output, sizeof(output), "%s/%s", directory, linker->name);
        // GNU ld links on one thread either way.
        const uint32_t ways = linker->one_thread != 0 ? 2 : 1;
        const bool wrong = wild_wrong && strcmp(linker->name, "wild") == 0;
        if (result && (settings->linkers >> l & 1) != 0 && wrong)
        {
            printf("  %-24s not run: its %s of this program may fault, its TLS segment off its alignment\n", "wild", kind == RUNTIME_KIND_STATIC ? "static executable" : "static PIE");
        }
        for (uint32_t way = 0; result && (settings->linkers >> l & 1) != 0 && !wrong && way < ways; way += 1)
        {
            const bool one_thread = way == 1 || linker->one_thread == 0;
            result = link_benchmark_linker(settings, runtime, linker, one_thread, arguments, search, (const char* const*)paths, units, output, expected, linked);
            if (result)
            {
                const double median = link_benchmark_median(linked->times, linked->runs);
                char label[64];
                snprintf(label, sizeof(label), "%s, %s", linker->name, one_thread ? "1 thread" : "its threads");
                printf("  %-24s %12.1f us: min %.1f, max %.1f of %u runs; %llu bytes, %llu page faults, %.2f MB peak resident", label, median,
                       (double)linked->times[0] / 1e3, (double)linked->times[linked->runs - 1] / 1e3, linked->runs, (unsigned long long)linked->output_bytes,
                       (unsigned long long)linked->faults, (double)linked->peak / 1e6);
                if (direct_total > 0.0)
                {
                    printf("; %.1fx the direct path", median / direct_total);
                }
                printf("\n");
            }
        }
        if (!keep)
        {
            unlink(output);
        }
    }
    for (uint32_t unit = 0; !keep && paths != 0 && unit < units; unit += 1)
    {
        if (paths[unit] != 0)
        {
            unlink(paths[unit]);
        }
    }
    if (!keep)
    {
        rmdir(objects_directory);
    }
    free(linked);
    free(objects);
    free(arguments);
    free(errors);
    free(source_names);
    free(paths);
    free(path_bytes);
    arena_release(&arena);
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The command

static bool link_benchmark_parse_u64(const char* text, uint64_t* value)
{
    char* end = 0;
    const unsigned long long parsed = strtoull(text, &end, 10);
    const bool result = text[0] >= '0' && text[0] <= '9' && end != text && *end == 0;
    *value = result ? (uint64_t)parsed : *value;
    return result;
}

// Numbers and ranges separated by commas, such as "0-3,8", each from `minimum` to `maximum`, into `values` (room for
// `capacity`). Returns how many, or 0 if the text isn't such a list or names more.
static uint32_t link_benchmark_parse_list(const char* text, uint32_t* values, uint32_t capacity, uint64_t minimum, uint64_t maximum)
{
    uint32_t count = 0;
    bool valid = *text != 0;
    const char* cursor = text;
    while (valid && *cursor != 0)
    {
        char* end = 0;
        const unsigned long long first = strtoull(cursor, &end, 10);
        unsigned long long last = first;
        valid = *cursor >= '0' && *cursor <= '9' && end != cursor;
        cursor = end;
        if (valid && *cursor == '-')
        {
            last = strtoull(cursor + 1, &end, 10);
            valid = cursor[1] >= '0' && cursor[1] <= '9' && end != cursor + 1 && last >= first;
            cursor = end;
        }
        valid = valid && first >= minimum && last <= maximum && last - first < capacity;
        for (unsigned long long value = first; valid && value <= last; value += 1)
        {
            valid = count < capacity;
            values[valid ? count : 0] = (uint32_t)value;
            count += valid ? 1 : 0;
        }
        const bool separated = valid && *cursor == ',';
        cursor += separated ? 1 : 0;
        valid = valid && (separated ? *cursor != 0 : *cursor == 0);
    }
    return valid ? count : 0;
}

// The kinds named in a list separated by commas (link_benchmark_kind_names), each kind's entry of `kinds` set if it is
// named and cleared if it isn't: whether every name is one.
static bool link_benchmark_parse_kinds(const char* text, bool* kinds)
{
    bool valid = *text != 0;
    const char* cursor = text;
    for (uint32_t kind = 0; kind < RUNTIME_KINDS; kind += 1)
    {
        kinds[kind] = false;
    }
    while (valid && *cursor != 0)
    {
        const char* comma = strchr(cursor, ',');
        const uint64_t length = comma != 0 ? (uint64_t)(comma - cursor) : strlen(cursor);
        bool found = false;
        for (uint32_t kind = 0; kind < RUNTIME_KINDS; kind += 1)
        {
            // A kind it links: a shared library's, which has no name, waits for the writer.
            const char* name = link_benchmark_kind_names[kind];
            const bool named = name != 0 && strlen(name) == length && memcmp(name, cursor, length) == 0;
            kinds[kind] = kinds[kind] || named;
            found = found || named;
        }
        valid = found;
        cursor += length + (comma != 0 ? 1 : 0);
        valid = valid && (comma == 0 || *cursor != 0);
    }
    return valid;
}

// The linkers named in a list separated by commas, a bit each in link_benchmark_linkers' order, or 0 if a name isn't
// one of them.
static uint32_t link_benchmark_parse_linkers(const char* text)
{
    uint32_t result = 0;
    bool valid = *text != 0;
    const char* cursor = text;
    while (valid && *cursor != 0)
    {
        const char* comma = strchr(cursor, ',');
        const uint64_t length = comma != 0 ? (uint64_t)(comma - cursor) : strlen(cursor);
        uint32_t found = 0;
        for (uint32_t l = 0; l < BUSTER_LINK_BENCHMARK_LINKERS; l += 1)
        {
            const char* name = link_benchmark_linkers[l].name;
            found |= strlen(name) == length && memcmp(name, cursor, length) == 0 ? 1u << l : 0;
        }
        valid = found != 0;
        result |= found;
        cursor += length + (comma != 0 ? 1 : 0);
        valid = valid && (comma == 0 || *cursor != 0);
    }
    return valid ? result : 0;
}

// Prints the first line `name --version` prints. False if it doesn't run.
static bool link_benchmark_version(const LinkBenchmarkSettings* settings, const char* name)
{
    char* arguments[] = {
        (char*)name,
        "--version",
        0,
    };
    int status = -1;
    link_benchmark_spawn(arguments, settings->output_path, settings->error_path, &status, 0);
    char line[256];
    const int64_t length = status == 0 ? link_benchmark_read(settings->output_path, line, sizeof(line)) : -1;
    char* newline = length > 0 ? strchr(line, '\n') : 0;
    if (newline != 0)
    {
        *newline = 0;
    }
    if (status == 0)
    {
        printf("  %s: %s\n", name, line);
    }
    else
    {
        printf("  %s: doesn't run (status %d); is it on PATH?\n", name, status);
    }
    return status == 0;
}

// Builds the harness that loads a shared library (LinkBenchmarkSettings.harness) with cc: whether it did.
static bool link_benchmark_build_harness(const LinkBenchmarkSettings* settings)
{
    static const char source[] = "#include <dlfcn.h>\n"
                                 "#include <stdio.h>\n"
                                 "int main(int argc, char** argv, char** envp)\n"
                                 "{\n"
                                 "    void* library = argc >= 2 ? dlopen(argv[1], RTLD_NOW) : 0;\n"
                                 "    int (*entry)(int, char**, char**) = library != 0 ? (int (*)(int, char**, char**))dlsym(library, \"buster_library_main\") : 0;\n"
                                 "    if (entry == 0)\n"
                                 "    {\n"
                                 "        fprintf(stderr, \"%s\\n\", dlerror());\n"
                                 "    }\n"
                                 "    return entry != 0 ? entry(argc, argv, envp) : 99;\n"
                                 "}\n";
    char source_path[BUSTER_LINK_BENCHMARK_PATH_BYTES];
    snprintf(source_path, sizeof(source_path), "%s.c", settings->harness);
    FILE* file = fopen(source_path, "w");
    const bool written = file != 0 && fputs(source, file) >= 0;
    if (file != 0)
    {
        fclose(file);
    }
    char* arguments[] = { "cc", "-O1", "-o", (char*)settings->harness, source_path, 0 };
    int status = -1;
    if (written)
    {
        link_benchmark_spawn(arguments, settings->output_path, settings->error_path, &status, 0);
    }
    unlink(source_path);
    return status == 0;
}

int run_link_benchmark(int argc, char** argv);

int run_link_benchmark(int argc, char** argv)
{
    LinkBenchmarkSettings settings = {
        .seed = 1,
        .directory = getenv("TMPDIR") != 0 ? getenv("TMPDIR") : "/tmp",
        .lanes = 1,
        .linkers = (1u << BUSTER_LINK_BENCHMARK_LINKERS) - 1,
        .direct = true,
        .linking = true,
        .kinds = {
            [RUNTIME_KIND_DYNAMIC] = true,
            [RUNTIME_KIND_STATIC] = true,
            [RUNTIME_KIND_STATIC_PIE] = true,
            [RUNTIME_KIND_SHARED] = true,
        },
    };
    settings.cpus[0] = 2;
    LinkBenchmarkProgram programs[BUSTER_LINK_BENCHMARK_MAX_PROGRAMS];
    uint32_t program_count = BUSTER_ARRAY_LENGTH(link_benchmark_default_programs);
    memcpy(programs, link_benchmark_default_programs, sizeof(link_benchmark_default_programs));
    uint32_t functions[BUSTER_LINK_BENCHMARK_MAX_PROGRAMS];
    uint32_t function_count = 0;
    uint64_t filler = link_program_default_options().filler;
    char cache[BUSTER_LINK_BENCHMARK_PATH_BYTES];
    const bool cached = snapshot_directory(cache, sizeof(cache));
    settings.cache = cached ? cache : 0;
    const bool helper = argc >= 2 && strcmp(argv[1], "--rusage") == 0;
    bool valid = !helper;
    for (int i = 1; valid && i < argc; i += 1)
    {
        const bool has_value = i + 1 < argc;
        if (strcmp(argv[i], "--functions") == 0 && has_value)
        {
            function_count = link_benchmark_parse_list(argv[i + 1], functions, BUSTER_LINK_BENCHMARK_MAX_PROGRAMS, 1, BUSTER_LINK_PROGRAM_MAX_COUNT);
            valid = function_count != 0;
            i += 1;
        }
        else if (strcmp(argv[i], "--filler") == 0 && has_value)
        {
            valid = link_benchmark_parse_u64(argv[i + 1], &filler) && filler <= BUSTER_LINK_PROGRAM_MAX_FILLER;
            i += 1;
        }
        else if (strcmp(argv[i], "--seed") == 0 && has_value)
        {
            valid = link_benchmark_parse_u64(argv[i + 1], &settings.seed);
            i += 1;
        }
        else if (strcmp(argv[i], "--directory") == 0 && has_value)
        {
            settings.directory = argv[i + 1];
            i += 1;
        }
        else if (strcmp(argv[i], "--cache") == 0 && has_value)
        {
            settings.cache = argv[i + 1];
            i += 1;
        }
        else if (strcmp(argv[i], "--cpus") == 0 && has_value)
        {
            settings.lanes = link_benchmark_parse_list(argv[i + 1], settings.cpus, BUSTER_LANE_MAX_COUNT, 0, BUSTER_LANE_CPU_MASK_WORDS * 64 - 1);
            valid = settings.lanes != 0;
            i += 1;
        }
        else if (strcmp(argv[i], "--linkers") == 0 && has_value)
        {
            settings.linkers = link_benchmark_parse_linkers(argv[i + 1]);
            valid = settings.linkers != 0;
            i += 1;
        }
        else if (strcmp(argv[i], "--runs") == 0 && has_value)
        {
            valid = link_benchmark_parse_u64(argv[i + 1], &settings.runs) && settings.runs >= 1 && settings.runs < BUSTER_LINK_BENCHMARK_MAX_RUNS;
            i += 1;
        }
        else if (strcmp(argv[i], "--only") == 0 && has_value)
        {
            settings.direct = strcmp(argv[i + 1], "direct") == 0;
            settings.linking = strcmp(argv[i + 1], "linkers") == 0;
            valid = settings.direct || settings.linking;
            i += 1;
        }
        else if (strcmp(argv[i], "--kinds") == 0 && has_value)
        {
            valid = link_benchmark_parse_kinds(argv[i + 1], settings.kinds);
            i += 1;
        }
        else
        {
            settings.keep = settings.keep || strcmp(argv[i], "--keep") == 0;
            valid = strcmp(argv[i], "--keep") == 0;
        }
    }
    // With --functions, each program has --filler's.
    for (uint32_t i = 0; i < function_count; i += 1)
    {
        programs[i] = (LinkBenchmarkProgram){
            .functions = functions[i],
            .filler = (uint32_t)filler,
        };
    }
    program_count = function_count != 0 ? function_count : program_count;
    const bool known_cpus = valid && syscall(SYS_sched_getaffinity, 0, sizeof(settings.allowed), settings.allowed) > 0;
    for (uint32_t word = 0; known_cpus && word < BUSTER_LANE_CPU_MASK_WORDS; word += 1)
    {
        settings.allowed_count += (uint32_t)__builtin_popcountll(settings.allowed[word]);
    }
    // Every CPU of --cpus must be one this process may use.
    uint32_t unusable = BUSTER_LANE_CPU_MASK_WORDS * 64;
    for (uint32_t lane = 0; known_cpus && lane < settings.lanes; lane += 1)
    {
        const uint32_t cpu = settings.cpus[lane];
        unusable = unusable == BUSTER_LANE_CPU_MASK_WORDS * 64 && (settings.allowed[cpu / 64] >> (cpu % 64) & 1) == 0 ? cpu : unusable;
    }
    const bool usable = unusable == BUSTER_LANE_CPU_MASK_WORDS * 64;
    const int length = snprintf(settings.work, sizeof(settings.work), "%s/buster-link-benchmark-XXXXXX", settings.directory);
    const bool made = known_cpus && usable && settings.cache != 0 && length > 0 && (uint64_t)length + 64 < sizeof(settings.work) && mkdtemp(settings.work) != 0;
    snprintf(settings.direct_path, sizeof(settings.direct_path), "%s/direct", settings.work);
    snprintf(settings.output_path, sizeof(settings.output_path), "%s/output", settings.work);
    snprintf(settings.error_path, sizeof(settings.error_path), "%s/errors", settings.work);
    snprintf(settings.usage_path, sizeof(settings.usage_path), "%s/usage", settings.work);
    snprintf(settings.harness, sizeof(settings.harness), "%s/harness", settings.work);
    const bool harnessed = !made || !settings.kinds[RUNTIME_KIND_SHARED] || link_benchmark_build_harness(&settings);
    // A first open of each kind's runtime makes its snapshot this executable's, so that the direct path's runs load it.
    Runtime runtimes[RUNTIME_KINDS];
    SnapshotResult opened[RUNTIME_KINDS];
    bool result = made;
    // The first kind whose runtime didn't import, if one didn't.
    uint32_t failed = RUNTIME_KINDS;
    for (uint32_t kind = 0; kind < RUNTIME_KINDS; kind += 1)
    {
        const bool wanted = made && settings.kinds[kind];
        opened[kind] = wanted ? link_benchmark_open(&runtimes[kind], settings.cache, (RuntimeKind)kind) : (SnapshotResult){ .error = RUNTIME_ERROR_NONE };
        result = result && opened[kind].error == RUNTIME_ERROR_NONE;
        failed = failed == RUNTIME_KINDS && opened[kind].error != RUNTIME_ERROR_NONE ? kind : failed;
        settings.runtimes[kind] = wanted ? &runtimes[kind] : 0;
    }
    if (helper)
    {
        result = link_benchmark_rusage(argc, argv) == 0;
    }
    else if (!harnessed)
    {
        fprintf(stderr, "benchmark-link: cc didn't build the harness that loads a shared library\n");
        result = false;
    }
    else if (!valid)
    {
        fprintf(stderr, "usage: benchmark-link [--functions LIST] [--filler N] [--seed N] [--directory PATH] [--cache PATH] [--cpus LIST] [--linkers LIST] [--runs N] "
                        "[--only direct|linkers] [--kinds LIST of dynamic, static, static-pie and shared] [--keep]\n");
    }
    else if (!known_cpus || settings.cache == 0)
    {
        fprintf(stderr, "benchmark-link: %s\n", !known_cpus ? "can't read this process's CPUs" : "no cache directory: set XDG_CACHE_HOME or HOME, or give --cache");
    }
    else if (!usable)
    {
        fprintf(stderr, "benchmark-link: this process may not run on CPU %u\n", unusable);
    }
    else if (!made)
    {
        fprintf(stderr, "benchmark-link: can't make a directory in %s\n", settings.directory);
    }
    else if (!result)
    {
        fprintf(stderr, "benchmark-link: the %s runtime didn't import: error %d, %s\n", link_benchmark_kind_names[failed], (int)opened[failed].error, runtimes[failed].failed_file);
    }
    LaneTeam team = { 0 };
    const bool started = made && result && lane_team_start(&team, settings.lanes, settings.cpus);
    if (made && result && !started)
    {
        fprintf(stderr, "benchmark-link: can't start lanes on --cpus\n");
        result = false;
    }
    if (made && result)
    {
        printf("benchmark-link: the direct path on %u lane%s from CPU %u, the linkers on %u CPUs or CPU %u; outputs in %s; the snapshots in %s", settings.lanes,
               settings.lanes == 1 ? "" : "s", settings.cpus[0], settings.allowed_count, settings.cpus[0], settings.work, settings.cache);
        for (uint32_t kind = 0; kind < RUNTIME_KINDS; kind += 1)
        {
            const SnapshotResult* open = &opened[kind];
            if (settings.kinds[kind])
            {
                printf(" (%s: %s)", link_benchmark_kind_names[kind], open->status == SNAPSHOT_STATUS_LOADED ? "loaded" : open->written ? "written" : "not written");
            }
        }
        printf("\n  one lane group for the command; startup and shutdown are outside the direct and object timings\n");
    }
    for (uint32_t l = 0; made && result && settings.linking && l < BUSTER_LINK_BENCHMARK_LINKERS; l += 1)
    {
        if ((settings.linkers >> l & 1) != 0 && strcmp(link_benchmark_linkers[l].name, "link-foreign") != 0)
        {
            result = link_benchmark_version(&settings, link_benchmark_linkers[l].name);
        }
    }
    // What the helper counts of a program that does nothing: the least it can, its own peak resident set among it.
    char* nothing[BUSTER_LINK_BENCHMARK_HELPER_ARGUMENTS + 2] = {
        [BUSTER_LINK_BENCHMARK_HELPER_ARGUMENTS] = "true",
    };
    int nothing_status = -1;
    uint64_t nothing_faults = 0;
    uint64_t nothing_peak = 0;
    if (made && result && settings.linking)
    {
        result = link_benchmark_count(&settings, nothing + BUSTER_LINK_BENCHMARK_HELPER_ARGUMENTS, &nothing_status, &nothing_faults, &nothing_peak) && nothing_status == 0;
        printf("  the counts' floor: true, through the helper, takes %llu page faults and %.2f MB peak resident\n", (unsigned long long)nothing_faults,
               (double)nothing_peak / 1e6);
    }
    for (uint32_t i = 0; made && result && i < program_count; i += 1)
    {
        for (uint32_t kind = 0; result && kind < RUNTIME_KINDS; kind += 1)
        {
            const LinkProgramOptions options = link_benchmark_options(programs[i], settings.seed, (RuntimeKind)kind);
            if (settings.kinds[kind])
            {
                printf("%u worker%s in %u units, up to %u filler instructions each, linked as %s:\n", options.functions, options.functions == 1 ? "" : "s", options.units,
                       options.filler, link_benchmark_kind_links[kind]);
                uint8_t expected[BUSTER_LINK_PROGRAM_OUTPUT_BYTES];
                double direct_total = 0.0;
                result = !settings.direct || link_benchmark_direct(&settings, (RuntimeKind)kind, programs[i], expected, &direct_total);
                result = result && (!settings.linking || link_benchmark_linkers_on(&settings, (RuntimeKind)kind, programs[i], expected, settings.direct, direct_total));
            }
        }
    }
    if (started)
    {
        lane_team_stop(&team);
    }
    for (uint32_t kind = 0; made && kind < RUNTIME_KINDS; kind += 1)
    {
        if (settings.kinds[kind] && opened[kind].error == RUNTIME_ERROR_NONE)
        {
            runtime_release(&runtimes[kind]);
        }
    }
    if (made && !settings.keep)
    {
        unlink(settings.harness);
        unlink(settings.direct_path);
        unlink(settings.output_path);
        unlink(settings.error_path);
        unlink(settings.usage_path);
        rmdir(settings.work);
    }
    return result ? 0 : 1;
}
