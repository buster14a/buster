// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

//
// measure-helpers (#1059): the paired measurements of the direct forms against the helpers they replace. Each program is
// built by this compiler four ways: by Tier 0 and by --tier1 (with -fno-asynchronous-unwind-tables, as
// measure-tier1-programs' builds), each as it compiles it (its direct forms: the typed stream's fences, atomic accesses
// and read/modify/writes, compare-exchanges and fetch loops among them, and the asm statements placed in their callers)
// and with --helper-forms=all, which gives each of those operations its helper's call, as the compiler made them before
// #1059. An asm statement keeps its helper under --tier1 either way (CheckProgramOptions.asm_helpers), and an implicit
// _Atomic object's accesses, which have no helper, are the typed stream's either way.
//
//   build/compiler measure-helpers [--sqlite DIR --speedtest DIR] [--runs N] [--compiles N] [--warmups N]
//                                  [--directory DIR] [--keep] [--uncounted]
//
// The programs:
// - the primitives (helpers_measure_primitives, written into the directory), single-threaded kernels of the operations
//   a concurrent program's primitives are made of, each the whole run of the program's argument: `counts`, reference
//   counts' increments and decrements (fetch_add, sub_fetch: lock xadd); `lock`, an uncontended spin lock (an acquire
//   exchange, pause, a release store); `sequence`, a sequence lock's writer and reader (relaxed, acquire and release
//   loads and stores, release and acquire fences); `flags`, a flag word's fetch_or, fetch_and and xor_fetch (compare-
//   exchange loops); `maximum`, a running maximum by a weak compare-exchange; `swaps`, inline asm's bswap, roll and leaq,
//   whose statements are placed in their callers by Tier 0. Each prints its kernel's checksum.
// - speedtest1, with --sqlite's DIR/sqlite3.c and --speedtest's DIR/speedtest1.c, as measure-tier1-programs builds and
//   runs it (one translation unit, #136's flags and options, a fresh database for each run), whose SQLite has its
//   AtomicLoad and AtomicStore's relaxed accesses (its VDBE loop's interrupt test among them). Left out without them.
//
// For each source and build, its -c object's facts: --helper-report's direct forms and helper calls of the four families
// that have a direct form, the object's executable bytes, and --tier1-report's spill and call stores and loads, the
// save and reload traffic of Tier 1's code (a Tier 0 build's are the same code's every time: its values live across a
// call are saved around it, which its runs' instructions count). Then every build's output must be the first's (the
// primitives' checksums; speedtest1's --verify hash), before --compiles rounds (5) of every (source, build) pair's -c
// compile, then --warmups (1) and --runs rounds (11) of every (program, build) pair's run, each round in an order that
// is a Williams design's (profile_ab_tree's), as whole processes where the caller pins this one (taskset): wall time, user
// cycles and instructions each (profile_time_one), as measure-tier1-programs times them. Every compile and run must exit
// with status 0 and be counted (but with --uncounted, for a host without the counters). It prints a tab-separated line
// per compile and per run (`helpers-measure-compile` and `helpers-measure-run`), each pair's medians, and the tables:
// the objects' facts, the compiles' medians, and the runs', each helpers build against its direct one. DIR is a new
// directory under --directory ($TMPDIR, or /tmp), removed at the end unless --keep. docs/c-builtins-integration.md has
// the 9700X's numbers.

#define BUSTER_HELPERS_MEASURE_BUILDS 4
#define BUSTER_HELPERS_MEASURE_KERNELS 6
// The primitives, then speedtest1.
#define BUSTER_HELPERS_MEASURE_SOURCES 2
// The kernels, then speedtest1.
#define BUSTER_HELPERS_MEASURE_PROGRAMS (BUSTER_HELPERS_MEASURE_KERNELS + 1)
#define BUSTER_HELPERS_MEASURE_FAMILIES 4
#define BUSTER_HELPERS_MEASURE_ARGUMENTS 64
#define BUSTER_HELPERS_MEASURE_MAX_RUNS 1000

static const char* const helpers_measure_build_names[BUSTER_HELPERS_MEASURE_BUILDS] = { "Tier 0", "Tier 0, helpers", "--tier1", "--tier1, helpers" };
static const char* const helpers_measure_build_files[BUSTER_HELPERS_MEASURE_BUILDS] = { "tier0", "tier0-helpers", "tier1", "tier1-helpers" };
static const char* const helpers_measure_kernels[BUSTER_HELPERS_MEASURE_KERNELS] = { "counts", "lock", "sequence", "flags", "maximum", "swaps" };
static const char* const helpers_measure_source_names[BUSTER_HELPERS_MEASURE_SOURCES] = { "primitives", "speedtest1" };
// --helper-report's families of a direct form, in its order.
static const char* const helpers_measure_families[BUSTER_HELPERS_MEASURE_FAMILIES] = { "atomic-access", "atomic-rmw", "fence", "asm" };

// The primitives' source (the measurement's own: no input of its own to pin).
static const char helpers_measure_primitives[] =
    "int printf(const char* format, ...);\n"
    "int strcmp(const char* a, const char* b);\n"
    "\n"
    "typedef struct Object Object;\n"
    "struct Object\n"
    "{\n"
    "    long references;\n"
    "    unsigned flags;\n"
    "    unsigned value;\n"
    "};\n"
    "\n"
    "static Object objects[64];\n"
    "static int lock;\n"
    "static unsigned sequence;\n"
    "static unsigned long shared[4];\n"
    "static long maximum;\n"
    "\n"
    "static unsigned long counts(unsigned long n)\n"
    "{\n"
    "    unsigned long freed = 0;\n"
    "    for (unsigned long i = 0; i < n; i += 1)\n"
    "    {\n"
    "        Object* o = &objects[i & 63];\n"
    "        __atomic_fetch_add(&o->references, 1, __ATOMIC_RELAXED);\n"
    "        __atomic_fetch_add(&o->references, 1, __ATOMIC_RELAXED);\n"
    "        freed += __atomic_sub_fetch(&o->references, 1, __ATOMIC_ACQ_REL) == 1;\n"
    "        freed += __atomic_sub_fetch(&o->references, 1, __ATOMIC_ACQ_REL) == 0;\n"
    "    }\n"
    "    return freed;\n"
    "}\n"
    "\n"
    "static unsigned long locked(unsigned long n)\n"
    "{\n"
    "    unsigned long total = 0;\n"
    "    for (unsigned long i = 0; i < n; i += 1)\n"
    "    {\n"
    "        while (__atomic_exchange_n(&lock, 1, __ATOMIC_ACQUIRE) != 0)\n"
    "        {\n"
    "            __asm__ volatile(\"pause\" ::: \"memory\");\n"
    "        }\n"
    "        total += objects[i & 63].value + i;\n"
    "        objects[i & 63].value = (unsigned)total;\n"
    "        __atomic_store_n(&lock, 0, __ATOMIC_RELEASE);\n"
    "    }\n"
    "    return total;\n"
    "}\n"
    "\n"
    "static unsigned long sequenced(unsigned long n)\n"
    "{\n"
    "    unsigned long total = 0;\n"
    "    for (unsigned long i = 0; i < n; i += 1)\n"
    "    {\n"
    "        // The writer's side.\n"
    "        const unsigned s = __atomic_load_n(&sequence, __ATOMIC_RELAXED);\n"
    "        __atomic_store_n(&sequence, s + 1, __ATOMIC_RELAXED);\n"
    "        __atomic_thread_fence(__ATOMIC_RELEASE);\n"
    "        __atomic_store_n(&shared[i & 3], i, __ATOMIC_RELAXED);\n"
    "        __atomic_store_n(&sequence, s + 2, __ATOMIC_RELEASE);\n"
    "        // The reader's side.\n"
    "        unsigned first = 0;\n"
    "        unsigned second = 0;\n"
    "        unsigned long read = 0;\n"
    "        do\n"
    "        {\n"
    "            first = __atomic_load_n(&sequence, __ATOMIC_ACQUIRE);\n"
    "            read = __atomic_load_n(&shared[i & 3], __ATOMIC_RELAXED);\n"
    "            __atomic_thread_fence(__ATOMIC_ACQUIRE);\n"
    "            second = __atomic_load_n(&sequence, __ATOMIC_RELAXED);\n"
    "        } while (first != second || (first & 1) != 0);\n"
    "        total += read;\n"
    "    }\n"
    "    return total;\n"
    "}\n"
    "\n"
    "static unsigned long flagged(unsigned long n)\n"
    "{\n"
    "    unsigned long total = 0;\n"
    "    for (unsigned long i = 0; i < n; i += 1)\n"
    "    {\n"
    "        Object* o = &objects[i & 63];\n"
    "        const unsigned bit = 1u << (i & 31);\n"
    "        total += __atomic_fetch_or(&o->flags, bit, __ATOMIC_RELAXED) & bit;\n"
    "        total += __atomic_fetch_and(&o->flags, ~bit, __ATOMIC_RELAXED) & 1;\n"
    "        total += __atomic_xor_fetch(&o->flags, bit, __ATOMIC_RELAXED) >> 31;\n"
    "    }\n"
    "    return total;\n"
    "}\n"
    "\n"
    "static unsigned long maximized(unsigned long n)\n"
    "{\n"
    "    unsigned long changes = 0;\n"
    "    unsigned long x = 12345;\n"
    "    for (unsigned long i = 0; i < n; i += 1)\n"
    "    {\n"
    "        x = x * 6364136223846793005ul + 1442695040888963407ul;\n"
    "        const long candidate = (long)(x >> 44);\n"
    "        long old = __atomic_load_n(&maximum, __ATOMIC_RELAXED);\n"
    "        while (candidate > old && !__atomic_compare_exchange_n(&maximum, &old, candidate, 1, __ATOMIC_RELAXED, __ATOMIC_RELAXED))\n"
    "        {\n"
    "        }\n"
    "        changes += candidate > old;\n"
    "        if ((i & 1023) == 0)\n"
    "        {\n"
    "            __atomic_store_n(&maximum, 0, __ATOMIC_RELAXED);\n"
    "        }\n"
    "    }\n"
    "    return changes;\n"
    "}\n"
    "\n"
    "static unsigned long swapped(unsigned long n)\n"
    "{\n"
    "    unsigned long total = 0;\n"
    "    unsigned x = 0x9e3779b9u;\n"
    "    for (unsigned long i = 0; i < n; i += 1)\n"
    "    {\n"
    "        unsigned y = x ^ (unsigned)i;\n"
    "        __asm__(\"bswap %0\" : \"+r\"(y));\n"
    "        __asm__(\"roll $7, %0\" : \"+r\"(y));\n"
    "        unsigned long z;\n"
    "        __asm__(\"leaq (%1,%2,2), %0\" : \"=r\"(z) : \"r\"((unsigned long)y), \"r\"(total));\n"
    "        total = z & 0xffffffffffful;\n"
    "        x = y;\n"
    "    }\n"
    "    return total;\n"
    "}\n"
    "\n"
    "int main(int argc, char** argv)\n"
    "{\n"
    "    const char* kernel = argc > 1 ? argv[1] : \"all\";\n"
    "    const unsigned long n = 5000000ul;\n"
    "    unsigned long result = 0;\n"
    "    const int all = strcmp(kernel, \"all\") == 0;\n"
    "    if (all || strcmp(kernel, \"counts\") == 0)\n"
    "    {\n"
    "        result ^= counts(n);\n"
    "    }\n"
    "    if (all || strcmp(kernel, \"lock\") == 0)\n"
    "    {\n"
    "        result ^= locked(n);\n"
    "    }\n"
    "    if (all || strcmp(kernel, \"sequence\") == 0)\n"
    "    {\n"
    "        result ^= sequenced(n);\n"
    "    }\n"
    "    if (all || strcmp(kernel, \"flags\") == 0)\n"
    "    {\n"
    "        result ^= flagged(n);\n"
    "    }\n"
    "    if (all || strcmp(kernel, \"maximum\") == 0)\n"
    "    {\n"
    "        result ^= maximized(n);\n"
    "    }\n"
    "    if (all || strcmp(kernel, \"swaps\") == 0)\n"
    "    {\n"
    "        result ^= swapped(n);\n"
    "    }\n"
    "    printf(\"%s %lu\\n\", kernel, result);\n"
    "    return 0;\n"
    "}\n"
    "\n";

// One (source, build) pair's object facts: each family's direct forms and helper calls, the object's executable bytes, and
// Tier 1's spill and call stores and loads (0 for Tier 0's builds).
typedef struct HelpersMeasureFacts HelpersMeasureFacts;
struct HelpersMeasureFacts
{
    uint64_t direct[BUSTER_HELPERS_MEASURE_FAMILIES];
    uint64_t helper[BUSTER_HELPERS_MEASURE_FAMILIES];
    uint64_t text;
    uint64_t spill_stores;
    uint64_t spill_loads;
    uint64_t call_stores;
    uint64_t call_loads;
};

typedef struct HelpersMeasure HelpersMeasure;
struct HelpersMeasure
{
    // Null without --sqlite and --speedtest: speedtest1 is left out.
    const char* sqlite;
    const char* speedtest;
    uint32_t runs;
    uint32_t compiles;
    uint32_t warmups;
    uint32_t source_count;
    uint32_t program_count;
    bool uncounted;
    char directory[PATH_MAX];
    char errors[PATH_MAX];
    char database[PATH_MAX];
    char temporary[PATH_MAX];
    char sources[BUSTER_HELPERS_MEASURE_SOURCES][PATH_MAX];
    char includes[2][PATH_MAX + 2];
    char objects[BUSTER_HELPERS_MEASURE_SOURCES][BUSTER_HELPERS_MEASURE_BUILDS][PATH_MAX];
    char executables[BUSTER_HELPERS_MEASURE_SOURCES][BUSTER_HELPERS_MEASURE_BUILDS][PATH_MAX];
    HelpersMeasureFacts facts[BUSTER_HELPERS_MEASURE_SOURCES][BUSTER_HELPERS_MEASURE_BUILDS];
};

// Formats into a buffer of PATH_MAX bytes; false if it doesn't fit.
static bool helpers_measure_path(char* path, const char* format, const char* a, const char* b)
{
    const int length = snprintf(path, PATH_MAX, format, a, b);
    return length > 0 && length < PATH_MAX;
}

// The arguments of a command, `count` of them, room for BUSTER_HELPERS_MEASURE_ARGUMENTS and the null after them.
typedef struct HelpersMeasureCommand HelpersMeasureCommand;
struct HelpersMeasureCommand
{
    char* arguments[BUSTER_HELPERS_MEASURE_ARGUMENTS + 1];
    uint32_t count;
};

static void helpers_measure_add(HelpersMeasureCommand* c, const char* argument)
{
    if (c->count < BUSTER_HELPERS_MEASURE_ARGUMENTS)
    {
        c->arguments[c->count] = (char*)argument;
        c->count += 1;
        c->arguments[c->count] = NULL;
    }
}

// The command that compiles source `s` by build `b`: an object (-c) where `object`, with the reports where `reported`,
// or else its executable.
static void helpers_measure_compile_command(const HelpersMeasure* m, uint32_t s, uint32_t b, bool object, bool reported, HelpersMeasureCommand* c)
{
    c->count = 0;
    c->arguments[0] = NULL;
    helpers_measure_add(c, "/proc/self/exe");
    if (b >= 2)
    {
        helpers_measure_add(c, "--tier1");
        helpers_measure_add(c, "-fno-asynchronous-unwind-tables");
    }
    if (b == 1 || b == 3)
    {
        helpers_measure_add(c, "--helper-forms=all");
    }
    if (reported)
    {
        helpers_measure_add(c, "--helper-report");
        if (b >= 2)
        {
            helpers_measure_add(c, "--tier1-report");
        }
    }
    if (s == 1)
    {
        for (uint32_t f = 0; f < BUSTER_TIER1_PROGRAMS_SQLITE_FLAGS; f += 1)
        {
            helpers_measure_add(c, tier1_programs_sqlite_flags[f]);
        }
        helpers_measure_add(c, m->includes[0]);
        helpers_measure_add(c, m->includes[1]);
    }
    if (object)
    {
        helpers_measure_add(c, "-c");
    }
    helpers_measure_add(c, "-o");
    helpers_measure_add(c, object ? m->objects[s][b] : m->executables[s][b]);
    helpers_measure_add(c, m->sources[s]);
    if (!object)
    {
        helpers_measure_add(c, "-lm");
    }
}

// The number after `key` in `text` (as "direct=12"), or 0 where it has none.
static uint64_t helpers_measure_field(const char* text, const char* key)
{
    const char* at = text != NULL ? strstr(text, key) : NULL;
    return at != NULL ? strtoull(at + strlen(key), NULL, 10) : 0;
}

// The executable bytes of an ELF64 object at `path`: its sections' with SHF_EXECINSTR. 0 for a file that isn't one.
static uint64_t helpers_measure_text(Arena* arena, const char* path)
{
    const uint64_t mark = arena->length;
    const int file = open(path, O_RDONLY | O_CLOEXEC);
    struct stat information;
    const bool sized = file != -1 && fstat(file, &information) == 0 && information.st_size >= 64;
    uint8_t* bytes = sized ? arena_push(arena, (uint64_t)information.st_size, 8) : NULL;
    const bool read_all = bytes != NULL && read(file, bytes, (size_t)information.st_size) == information.st_size;
    if (file != -1)
    {
        close(file);
    }
    uint64_t text = 0;
    if (read_all && memcmp(bytes, "\177ELF", 4) == 0 && bytes[4] == 2)
    {
        uint64_t offset = 0;
        uint16_t entry = 0;
        uint16_t count = 0;
        memcpy(&offset, bytes + 0x28, 8);
        memcpy(&entry, bytes + 0x3a, 2);
        memcpy(&count, bytes + 0x3c, 2);
        for (uint32_t k = 0; entry >= 64 && k < count && offset + (uint64_t)(k + 1) * entry <= (uint64_t)information.st_size; k += 1)
        {
            uint64_t flags = 0;
            uint64_t size = 0;
            memcpy(&flags, bytes + offset + (uint64_t)k * entry + 8, 8);
            memcpy(&size, bytes + offset + (uint64_t)k * entry + 32, 8);
            text += (flags & 4) != 0 ? size : 0;
        }
    }
    arena_pop_to(arena, mark);
    return text;
}

// Source `s`'s object by build `b`, with its reports read into its facts, then its executable. Returns whether both built.
static bool helpers_measure_build(Arena* arena, HelpersMeasure* m, uint32_t s, uint32_t b)
{
    char report[PATH_MAX];
    HelpersMeasureCommand c = { 0 };
    helpers_measure_compile_command(m, s, b, true, true, &c);
    bool ok = helpers_measure_path(report, "%s/report-%s", m->directory, m->objects[s][b] + strlen(m->directory) + 1) && c.count < BUSTER_HELPERS_MEASURE_ARGUMENTS;
    // The reports are on standard error: the command's errors there, and a copy for the report.
    unlink(report);
    ok = ok && tier1_programs_spawn(c.arguments, NULL, report);
    const uint64_t mark = arena->length;
    const char* text = ok ? measure_compile_read(arena, report) : NULL;
    HelpersMeasureFacts* facts = &m->facts[s][b];
    for (uint32_t f = 0; text != NULL && f < BUSTER_HELPERS_MEASURE_FAMILIES; f += 1)
    {
        char key[64];
        snprintf(key, sizeof(key), "helpers.%s\t", helpers_measure_families[f]);
        const char* line = strstr(text, key);
        facts->direct[f] = helpers_measure_field(line, "direct=");
        facts->helper[f] = helpers_measure_field(line, "helper=");
        ok = ok && line != NULL;
    }
    const char* tier1 = text != NULL ? strstr(text, "tier1\tfunctions=") : NULL;
    facts->spill_stores = helpers_measure_field(tier1, "\tspill_stores=");
    facts->spill_loads = helpers_measure_field(tier1, "\tspill_loads=");
    facts->call_stores = helpers_measure_field(tier1, "\tcall_stores=");
    facts->call_loads = helpers_measure_field(tier1, "\tcall_loads=");
    ok = ok && (b < 2 || tier1 != NULL);
    arena_pop_to(arena, mark);
    facts->text = ok ? helpers_measure_text(arena, m->objects[s][b]) : 0;
    ok = ok && facts->text != 0;
    helpers_measure_compile_command(m, s, b, false, false, &c);
    ok = ok && c.count < BUSTER_HELPERS_MEASURE_ARGUMENTS && tier1_programs_spawn(c.arguments, NULL, m->errors);
    printf("measure-helpers: %s by %s %s\n", helpers_measure_source_names[s], helpers_measure_build_names[b], ok ? "built" : "FAILED");
    return ok;
}

// speedtest1's database's files and SQLite's temporary directory removed, and the directory made again (as
// tier1_programs_fresh's). Returns whether the directory is there.
static bool helpers_measure_fresh(const HelpersMeasure* m)
{
    Tier1Programs p = { 0 };
    memcpy(p.database, m->database, sizeof(p.database));
    memcpy(p.temporary, m->temporary, sizeof(p.temporary));
    return tier1_programs_fresh(&p);
}

// The arguments of program `l` (a kernel, or speedtest1) by build `b` into argv, with --verify before speedtest1's
// database when `verify`, or the primitives' every kernel at once (`all`) when `every`.
static void helpers_measure_list(const HelpersMeasure* m, uint32_t l, uint32_t b, bool verify, bool every, char** argv)
{
    uint32_t count = 0;
    const bool speedtest = l == BUSTER_HELPERS_MEASURE_KERNELS;
    argv[count++] = (char*)m->executables[speedtest ? 1 : 0][b];
    for (uint32_t o = 0; speedtest && o < BUSTER_TIER1_PROGRAMS_SPEEDTEST_OPTIONS; o += 1)
    {
        argv[count++] = (char*)tier1_programs_speedtest_options[o];
    }
    if (speedtest && verify)
    {
        argv[count++] = "--verify";
    }
    argv[count++] = speedtest ? (char*)m->database : every ? "all" : (char*)helpers_measure_kernels[l];
    argv[count] = NULL;
}

// Every build's output against the first's: the primitives' checksums of every kernel, and speedtest1's verification
// hash. Returns whether they all agree.
static bool helpers_measure_check(Arena* arena, HelpersMeasure* m)
{
    char* argv[BUSTER_TIER1_PROGRAMS_SPEEDTEST_OPTIONS + 4];
    char outputs[BUSTER_HELPERS_MEASURE_BUILDS][PATH_MAX];
    bool ok = true;
    for (uint32_t b = 0; ok && b < BUSTER_HELPERS_MEASURE_BUILDS; b += 1)
    {
        helpers_measure_list(m, 0, b, false, true, argv);
        ok = helpers_measure_path(outputs[b], "%s/primitives-%s.out", m->directory, helpers_measure_build_files[b]) && tier1_programs_spawn(argv, outputs[b], m->errors);
        ok = ok && measure_compile_same(arena, outputs[0], outputs[b]);
        printf("measure-helpers: the primitives' checksums by %s: %s\n", helpers_measure_build_names[b], ok ? "the first build's" : "DIFFER (FAILED)");
    }
    char reference[256] = { 0 };
    for (uint32_t b = 0; ok && m->source_count == 2 && b < BUSTER_HELPERS_MEASURE_BUILDS; b += 1)
    {
        helpers_measure_list(m, BUSTER_HELPERS_MEASURE_KERNELS, b, true, false, argv);
        ok = helpers_measure_path(outputs[b], "%s/verify-%s.txt", m->directory, helpers_measure_build_files[b]) && helpers_measure_fresh(m) &&
             tier1_programs_spawn(argv, outputs[b], m->errors);
        const uint64_t mark = arena->length;
        const char* text = ok ? measure_compile_read(arena, outputs[b]) : NULL;
        const char* line = text != NULL ? strstr(text, "Verification Hash:") : NULL;
        const size_t length = line != NULL ? strcspn(line, "\n") : 0;
        ok = line != NULL && length < sizeof(reference);
        if (ok && b == 0)
        {
            memcpy(reference, line, length);
        }
        ok = ok && strlen(reference) == length && memcmp(reference, line, length) == 0;
        printf("measure-helpers: speedtest1 --verify by %s: %.*s%s\n", helpers_measure_build_names[b], (int)length, line != NULL ? line : "", ok ? "" : " (FAILED)");
        arena_pop_to(arena, mark);
    }
    return ok;
}

// One pair's medians over its rounds: wall time with its range, user cycles and instructions.
typedef struct HelpersMeasureMedians HelpersMeasureMedians;
struct HelpersMeasureMedians
{
    uint64_t wall_ns;
    uint64_t wall_low;
    uint64_t wall_high;
    uint64_t cycles;
    uint64_t instructions;
};

// The medians of `slots` pairs over `rounds` rounds of `timed` (round by round, each round's slots in their order).
static void helpers_measure_medians(const ProfileTimed* timed, uint64_t rounds, uint64_t slots, uint64_t* values, HelpersMeasureMedians* medians)
{
    for (uint64_t s = 0; s < slots; s += 1)
    {
        HelpersMeasureMedians* median = &medians[s];
        for (uint64_t r = 0; r < rounds; r += 1)
        {
            values[r] = timed[r * slots + s].cycles;
        }
        median->cycles = profile_median(values, rounds);
        for (uint64_t r = 0; r < rounds; r += 1)
        {
            values[r] = timed[r * slots + s].instructions;
        }
        median->instructions = profile_median(values, rounds);
        for (uint64_t r = 0; r < rounds; r += 1)
        {
            values[r] = timed[r * slots + s].wall_ns;
        }
        median->wall_ns = profile_median(values, rounds);
        median->wall_low = values[0];
        median->wall_high = values[rounds - 1];
    }
}

// The timed rounds: `compile` the (source, build) pairs' -c compiles (--compiles rounds, after --warmups), or else the
// (program, build) pairs' runs (--runs rounds, after --warmups), every pair's medians into `medians` (by source or
// program, then build). Returns the failed ones.
static uint64_t helpers_measure_time(Arena* arena, HelpersMeasure* m, bool compile, HelpersMeasureMedians* medians)
{
    const uint64_t lists = compile ? m->source_count : m->program_count;
    const uint64_t slots = lists * BUSTER_HELPERS_MEASURE_BUILDS;
    const uint64_t rounds = compile ? m->compiles : m->runs;
    const uint64_t mark = arena->length;
    ProfileTimed* timed = BUSTER_ARENA_PUSH_ARRAY(arena, ProfileTimed, rounds * slots + 1);
    uint64_t* values = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, rounds + 1);
    const char* kind = compile ? "helpers-measure-compile" : "helpers-measure-run";
    uint64_t failures = timed == NULL || values == NULL;
    printf("%s\tphase\tround\tposition\t%s\tbuild\twall_ns\tuser_cycles\tinstructions\tstatus\tcounted\n", kind, compile ? "source" : "program");
    for (uint64_t run = 0; failures == 0 && run < (uint64_t)m->warmups + rounds; run += 1)
    {
        const bool warmup = run < m->warmups;
        for (uint64_t position = 0; position < slots; position += 1)
        {
            const uint64_t s = warmup ? position : profile_ab_tree(run - m->warmups, position, slots);
            const uint32_t b = (uint32_t)(s % BUSTER_HELPERS_MEASURE_BUILDS);
            const uint32_t l = (uint32_t)(s / BUSTER_HELPERS_MEASURE_BUILDS);
            HelpersMeasureCommand c = { 0 };
            char* argv[BUSTER_TIER1_PROGRAMS_SPEEDTEST_OPTIONS + 4];
            char* const* arguments = argv;
            bool fresh = true;
            if (compile)
            {
                helpers_measure_compile_command(m, l, b, true, false, &c);
                arguments = c.arguments;
            }
            else
            {
                helpers_measure_list(m, l, b, false, false, argv);
                fresh = l != BUSTER_HELPERS_MEASURE_KERNELS || helpers_measure_fresh(m);
            }
            const ProfileTimed one = fresh ? profile_time_one(arguments[0], arguments) : (ProfileTimed){ .status = -1 };
            const int status = one.status != -1 && WIFEXITED(one.status) ? WEXITSTATUS(one.status) : -1;
            const bool passed = status == 0 && (one.counted || m->uncounted);
            const char* name = compile ? helpers_measure_source_names[l] : l == BUSTER_HELPERS_MEASURE_KERNELS ? "speedtest1" : helpers_measure_kernels[l];
            failures += !passed;
            printf("%s\t%s\t%llu\t%llu\t%s\t%s\t%llu\t%llu\t%llu\t%d\t%d\n", kind, warmup ? "warm-up" : "measured", (unsigned long long)(warmup ? run : run - m->warmups) + 1,
                   (unsigned long long)position + 1, name, helpers_measure_build_files[b], (unsigned long long)one.wall_ns, (unsigned long long)one.cycles,
                   (unsigned long long)one.instructions, status, one.counted);
            if (!passed)
            {
                printf("measure-helpers: %s by %s: FAILED (status %d, counted %d)\n", name, helpers_measure_build_names[b], status, one.counted);
            }
            if (!warmup)
            {
                timed[(run - m->warmups) * slots + s] = one;
            }
        }
    }
    if (failures == 0)
    {
        helpers_measure_medians(timed, rounds, slots, values, medians);
    }
    arena_pop_to(arena, mark);
    return failures;
}

// A helpers build's median against its direct build's (the one before it), or "none" where the direct one's is 0.
static void helpers_measure_ratio(double helpers, double direct)
{
    if (direct > 0)
    {
        printf(" %.2f |", helpers / direct);
    }
    else
    {
        printf(" none |");
    }
}

// The tables: each object's facts; the compiles' medians, by source; the runs', by program; each helpers build against
// its direct build.
static void helpers_measure_report(const HelpersMeasure* m, const HelpersMeasureMedians* compiles, const HelpersMeasureMedians* runs)
{
    for (uint32_t l = 0; l < m->program_count; l += 1)
    {
        for (uint32_t b = 0; b < BUSTER_HELPERS_MEASURE_BUILDS; b += 1)
        {
            const HelpersMeasureMedians* median = &runs[l * BUSTER_HELPERS_MEASURE_BUILDS + b];
            printf("%s by %s: %u runs, median %.3f ms wall (%.3f to %.3f), %llu user cycles, %llu instructions\n",
                   l == BUSTER_HELPERS_MEASURE_KERNELS ? "speedtest1" : helpers_measure_kernels[l], helpers_measure_build_names[b], m->runs, (double)median->wall_ns / 1e6,
                   (double)median->wall_low / 1e6, (double)median->wall_high / 1e6, (unsigned long long)median->cycles, (unsigned long long)median->instructions);
        }
    }
    printf("\n| Source | Build | Direct forms | Helper calls | atomic-access | atomic-rmw | fence | asm | Executable bytes | Spill stores, loads | Call stores, loads |\n");
    printf("| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n");
    for (uint32_t s = 0; s < m->source_count; s += 1)
    {
        for (uint32_t b = 0; b < BUSTER_HELPERS_MEASURE_BUILDS; b += 1)
        {
            const HelpersMeasureFacts* facts = &m->facts[s][b];
            uint64_t direct = 0;
            uint64_t helper = 0;
            for (uint32_t f = 0; f < BUSTER_HELPERS_MEASURE_FAMILIES; f += 1)
            {
                direct += facts->direct[f];
                helper += facts->helper[f];
            }
            printf("| %s | %s | %llu | %llu |", helpers_measure_source_names[s], helpers_measure_build_names[b], (unsigned long long)direct, (unsigned long long)helper);
            for (uint32_t f = 0; f < BUSTER_HELPERS_MEASURE_FAMILIES; f += 1)
            {
                printf(" %llu/%llu |", (unsigned long long)facts->direct[f], (unsigned long long)facts->helper[f]);
            }
            if (b >= 2)
            {
                printf(" %llu | %llu, %llu | %llu, %llu |\n", (unsigned long long)facts->text, (unsigned long long)facts->spill_stores, (unsigned long long)facts->spill_loads,
                       (unsigned long long)facts->call_stores, (unsigned long long)facts->call_loads);
            }
            else
            {
                printf(" %llu | | |\n", (unsigned long long)facts->text);
            }
        }
    }
    // The compiles' and the runs' medians: rows by source or program and metric, columns by build, each helpers build
    // against its direct one.
    static const char* const metrics[3] = { "ms (median)", "user cycles, M", "instructions, M" };
    for (uint32_t table = 0; table < 2; table += 1)
    {
        const bool compile = table == 0;
        const uint32_t lists = compile ? m->source_count : m->program_count;
        const HelpersMeasureMedians* medians = compile ? compiles : runs;
        printf("\n| %s | Tier 0 | Tier 0, helpers | helpers / direct | `--tier1` | `--tier1`, helpers | helpers / direct |\n| --- | ---: | ---: | ---: | ---: | ---: | ---: |\n",
               compile ? "Compile (-c)" : "Run");
        for (uint32_t l = 0; l < lists; l += 1)
        {
            const char* name = compile ? helpers_measure_source_names[l] : l == BUSTER_HELPERS_MEASURE_KERNELS ? "speedtest1" : helpers_measure_kernels[l];
            for (uint32_t metric = 0; metric < 3; metric += 1)
            {
                double values[BUSTER_HELPERS_MEASURE_BUILDS];
                for (uint32_t b = 0; b < BUSTER_HELPERS_MEASURE_BUILDS; b += 1)
                {
                    const HelpersMeasureMedians* median = &medians[l * BUSTER_HELPERS_MEASURE_BUILDS + b];
                    values[b] = metric == 0 ? (double)median->wall_ns / 1e6 : metric == 1 ? (double)median->cycles / 1e6 : (double)median->instructions / 1e6;
                }
                printf("| %s, %s | %.1f | %.1f |", name, metrics[metric], values[0], values[1]);
                helpers_measure_ratio(values[1], values[0]);
                printf(" %.1f | %.1f |", values[2], values[3]);
                helpers_measure_ratio(values[3], values[2]);
                printf("\n");
            }
        }
    }
}

static int run_helpers_measure(int argc, char** argv)
{
    Arena arena = arena_reserve(UINT64_C(1) << 32);
    HelpersMeasure m = { .runs = 11, .compiles = 5, .warmups = 1 };
    const char* parent = oracle_temporary_directory();
    bool keep = false;
    bool valid = arena.base != 0;
    int i = 1;
    while (valid && i < argc)
    {
        uint64_t value = 0;
        const bool pair = i + 1 < argc;
        const bool flag = strcmp(argv[i], "--keep") == 0 || strcmp(argv[i], "--uncounted") == 0;
        if (strcmp(argv[i], "--runs") == 0 && pair && profile_count_option(argv[i + 1], &value) && value <= BUSTER_HELPERS_MEASURE_MAX_RUNS)
        {
            m.runs = (uint32_t)value;
        }
        else if (strcmp(argv[i], "--compiles") == 0 && pair && profile_count_option(argv[i + 1], &value) && value <= BUSTER_HELPERS_MEASURE_MAX_RUNS)
        {
            m.compiles = (uint32_t)value;
        }
        else if (strcmp(argv[i], "--warmups") == 0 && pair && profile_decimal(profile_string(argv[i + 1]), &value) && value <= 1000)
        {
            m.warmups = (uint32_t)value;
        }
        else if (strcmp(argv[i], "--sqlite") == 0 && pair)
        {
            m.sqlite = argv[i + 1];
        }
        else if (strcmp(argv[i], "--speedtest") == 0 && pair)
        {
            m.speedtest = argv[i + 1];
        }
        else if (strcmp(argv[i], "--directory") == 0 && pair)
        {
            parent = argv[i + 1];
        }
        else if (strcmp(argv[i], "--keep") == 0)
        {
            keep = true;
        }
        else if (strcmp(argv[i], "--uncounted") == 0)
        {
            m.uncounted = true;
        }
        else
        {
            valid = false;
        }
        i += flag ? 1 : 2;
    }
    // speedtest1 takes both of its directories, or neither.
    valid = valid && (m.sqlite == NULL) == (m.speedtest == NULL);
    m.source_count = m.sqlite != NULL ? BUSTER_HELPERS_MEASURE_SOURCES : 1;
    m.program_count = m.sqlite != NULL ? BUSTER_HELPERS_MEASURE_PROGRAMS : BUSTER_HELPERS_MEASURE_KERNELS;
    const int length = valid ? snprintf(m.directory, sizeof(m.directory), "%s/measure-helpers-XXXXXX", parent) : -1;
    valid = valid && length > 0 && (size_t)length + 64 < sizeof(m.directory);
    const bool made = valid && mkdtemp(m.directory) != NULL;
    int result = 1;
    if (!valid)
    {
        fprintf(stderr, "usage: measure-helpers [--sqlite DIR --speedtest DIR] [--runs N] [--compiles N] [--warmups N] [--directory DIR] [--keep] [--uncounted]\n");
    }
    else if (!made)
    {
        fprintf(stderr, "error: can't make a directory under %s: %s\n", parent, strerror(errno));
    }
    else
    {
        bool ok = helpers_measure_path(m.errors, "%s/%s", m.directory, "errors.txt") && helpers_measure_path(m.database, "%s/%s", m.directory, "speedtest1.db") &&
                  helpers_measure_path(m.temporary, "%s/%s", m.directory, "tmp") && helpers_measure_path(m.sources[0], "%s/%s", m.directory, "primitives.c") &&
                  helpers_measure_path(m.sources[1], "%s/%s", m.directory, "speedtest1-unity.c");
        ok = ok && (m.sqlite == NULL || (snprintf(m.includes[0], sizeof(m.includes[0]), "-I%s", m.sqlite) < (int)sizeof(m.includes[0]) &&
                                         snprintf(m.includes[1], sizeof(m.includes[1]), "-I%s", m.speedtest) < (int)sizeof(m.includes[1])));
        for (uint32_t s = 0; ok && s < m.source_count; s += 1)
        {
            for (uint32_t b = 0; ok && b < BUSTER_HELPERS_MEASURE_BUILDS; b += 1)
            {
                char name[64];
                snprintf(name, sizeof(name), "%s-%s", helpers_measure_source_names[s], helpers_measure_build_files[b]);
                ok = helpers_measure_path(m.objects[s][b], "%s/%s.o", m.directory, name) && helpers_measure_path(m.executables[s][b], "%s/%s", m.directory, name);
            }
        }
        // The sources: the primitives, and speedtest1's one file (#136's speedtest1-unity.c, as measure-tier1-programs').
        static const char unity_text[] = "#include \"sqlite3.c\"\n#define randomFunc speedtest1_randomFunc\n#include \"speedtest1.c\"\n";
        for (uint32_t s = 0; ok && s < m.source_count; s += 1)
        {
            const char* text = s == 0 ? helpers_measure_primitives : unity_text;
            const size_t size = s == 0 ? sizeof(helpers_measure_primitives) - 1 : sizeof(unity_text) - 1;
            const int file = open(m.sources[s], O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
            ok = file != -1 && write(file, text, size) == (ssize_t)size;
            if (file != -1)
            {
                close(file);
            }
        }
        ok = ok && setenv("SQLITE_TMPDIR", m.temporary, 1) == 0;
        printf("measure-helpers: %u compile rounds, %u warm-ups, %u runs, %u sources, %u builds, in %s\n", m.compiles, m.warmups, m.runs, m.source_count,
               BUSTER_HELPERS_MEASURE_BUILDS, m.directory);
        for (uint32_t s = 0; ok && s < m.source_count; s += 1)
        {
            for (uint32_t b = 0; ok && b < BUSTER_HELPERS_MEASURE_BUILDS; b += 1)
            {
                ok = helpers_measure_build(&arena, &m, s, b);
            }
        }
        ok = ok && helpers_measure_check(&arena, &m);
        HelpersMeasureMedians compiles[BUSTER_HELPERS_MEASURE_SOURCES * BUSTER_HELPERS_MEASURE_BUILDS] = { 0 };
        HelpersMeasureMedians runs[BUSTER_HELPERS_MEASURE_PROGRAMS * BUSTER_HELPERS_MEASURE_BUILDS] = { 0 };
        ok = ok && helpers_measure_time(&arena, &m, true, compiles) == 0 && helpers_measure_time(&arena, &m, false, runs) == 0;
        if (ok)
        {
            helpers_measure_report(&m, compiles, runs);
        }
        else
        {
            const char* errors = measure_compile_read(&arena, m.errors);
            fprintf(stderr, "measure-helpers: FAILED; the commands' errors:\n%s\n", errors != NULL ? errors : "");
        }
        result = ok ? 0 : 1;
        if (!keep)
        {
            char* remove[] = { "rm", "-rf", m.directory, NULL };
            tier1_programs_spawn(remove, NULL, "/dev/null");
        }
    }
    arena_release(&arena);
    return result;
}
