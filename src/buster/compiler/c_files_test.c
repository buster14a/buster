// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The per-file jobs' tests (c_files.c), for the test-files command:
//
//   build/compiler test-files [--lanes LIST] [--seed N] [--keep] [--flat] [options] [path...]
//
// Each check runs the same work with the file cache filled on each lane count of --lanes (1, 2, 3 and 8 by default),
// every time with a new intern table and cache, and requires the same results from every lane count, byte for byte:
// the shared table's ids and spellings, every record's tokens, values, leading-space bits, directive table and guard,
// what the walk did, and each translation unit's output stream (kinds, flags, values and locations) when it
// preprocesses with the cache. One lane refines straight into the shared table, so it is the merge's scalar twin, and
// the others must give its ids. Each unit's spellings must also be those it gives with a cache of its own (the
// preprocessor as it runs alone), with as many errors and warnings, and nothing may be written to stderr while the
// cache is filled.
//   - The tree: a directory of the test's own under $TMPDIR (or /tmp), with three translation units and the headers they
//     include: by the includer's directory and through -I and -isystem, an #include_next chain through three
//     directories, a header reached by four paths (a symbolic link and `..` among them), a hard link to a header that a
//     computed include names too (which the walk can't follow: the unit finds it prepared by its other name), a guarded
//     header and a #pragma once header included twice, a header under #if 0 that exists (prepared but never entered) and
//     one that doesn't (no diagnostic), and a missing header in an active line, which the unit reports. The counts are
//     exact: the files prepared (each identity once), the rounds, the unused files, the headers not found, and the system
//     calls each unit made after the walk (only the computed include's).
//   - Spellings: generated files of identifiers (a shared vocabulary drawn unevenly, names of their own, long ones, line
//     splices and UCNs) prepared without following includes, enough new spellings for the shared table to grow during a
//     merge; and again in two runs into one cache, whose second finds most of its spellings in the shared table, against
//     one run of all of them on one lane.
//   - Files: each file (or each .c and .h file under a directory) as a translation unit with the options (as for -E:
//     -I, -isystem, -D, --predefines, -std=, ...), all of them the walk's roots. With --flat, the files are only prepared,
//     without following their includes or preprocessing them (a corpus such as the C library's, which is files rather
//     than translation units).
// --seed N changes the generated spellings, and --keep leaves the directory.
//
// main.c includes this file after c_files.c and c_preprocessor_test.c, whose spellings and comparison it uses.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#define BUSTER_FILES_TEST_MAX_LANES 16
#define BUSTER_FILES_TEST_MAX_UNITS 4096

// A unit's results with a cache: its output stream (copied), spellings, errors and warnings, and its system calls.
typedef struct FilesTestUnit FilesTestUnit;
struct FilesTestUnit
{
    Arena arena;
    uint8_t* kinds;
    uint8_t* flags;
    uint32_t* values;
    uint32_t* locations;
    uint32_t count;
    uint32_t errors;
    uint32_t warnings;
    FileCacheCounts probes;
    bool ran;
};

// One lane count's run: the table and cache it filled, what the walk did, and its units' results.
typedef struct FilesTestRun FilesTestRun;
struct FilesTestRun
{
    InternTable table;
    FileCache cache;
    uint32_t lanes;
    // What the walk did (the run's counts), the bytes it wrote to stderr, and the records it prepared.
    uint32_t rounds;
    uint64_t requests;
    uint64_t searches;
    uint64_t absent;
    uint64_t prepared;
    uint64_t candidates;
    uint64_t shared_found;
    uint64_t stderr_bytes;
    bool failed;
    FilesTestUnit* units;
    uint32_t unit_count;
};

// The work a check runs: the units' options, the roots, whether the walk follows includes, and the units to
// preprocess after it (none for the spellings).
typedef struct FilesTestWork FilesTestWork;
struct FilesTestWork
{
    const char* name;
    PPOptions options;
    const char* const* roots;
    uint32_t root_count;
    bool follow;
    // The roots in two runs, the second from root `split` on (0 for one run).
    uint32_t split;
    // The runs' FilesRun.lane_bytes and split_bytes, if `thresholds`.
    bool thresholds;
    uint64_t lane_bytes;
    uint64_t split_bytes;
    const char* const* units;
    uint32_t unit_count;
};

static bool files_test_write(const char* path, const char* text)
{
    FILE* file = fopen(path, "w");
    const bool result = file != 0 && fputs(text, file) >= 0;
    if (file != 0)
    {
        fclose(file);
    }
    if (!result)
    {
        printf("can't write %s\n", path);
    }
    return result;
}

// Sends stderr to a file of the directory's until files_test_quiet_end, which returns how many bytes were written to it.
static int files_test_quiet_begin(const char* directory, char* path, uint32_t capacity)
{
    fflush(stderr);
    snprintf(path, capacity, "%s/stderr.txt", directory);
    const int file = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    const int saved = file >= 0 ? dup(2) : -1;
    if (saved >= 0)
    {
        dup2(file, 2);
    }
    if (file >= 0)
    {
        close(file);
    }
    return saved;
}

static uint64_t files_test_quiet_end(int saved, const char* path)
{
    fflush(stderr);
    if (saved >= 0)
    {
        dup2(saved, 2);
        close(saved);
    }
    struct stat stats;
    const uint64_t result = stat(path, &stats) == 0 ? (uint64_t)stats.st_size : 0;
    unlink(path);
    return result;
}

static void files_test_release_run(FilesTestRun* run)
{
    for (uint32_t u = 0; run->units != 0 && u < run->unit_count; u += 1)
    {
        arena_release(&run->units[u].arena);
    }
    free(run->units);
    file_cache_release(&run->cache);
    intern_release(&run->table);
    *run = (FilesTestRun){ 0 };
}

// The cache filled on `lanes` lanes from the work's roots (in one run or two), then each of the work's units
// preprocessed with it. `directory` takes the walk's stderr while it runs.
static bool files_test_run(const FilesTestWork* work, uint32_t lanes, const char* directory, FilesTestRun* run)
{
    static Preprocessor search;
    static Preprocessor unit;
    *run = (FilesTestRun){ .lanes = lanes };
    PPOptions options = work->options;
    bool result = intern_initialize(&run->table) && file_cache_initialize(&run->cache, &run->table, pp_prepare_options(&options));
    options.files = &run->cache;
    result = result && pp_initialize(&search, &options, &run->table);
    const uint32_t runs = work->split != 0 ? 2 : 1;
    for (uint32_t r = 0; result && r < runs; r += 1)
    {
        const uint32_t begin = r == 0 ? 0 : work->split;
        const uint32_t end = runs == 2 && r == 0 ? work->split : work->root_count;
        FilesRun files;
        result = files_run_initialize(&files, &run->cache, &search, work->roots + begin, end - begin, work->follow, lanes);
        files.lane_bytes = work->thresholds ? work->lane_bytes : files.lane_bytes;
        files.split_bytes = work->thresholds ? work->split_bytes : files.split_bytes;
        char path[4200];
        const int saved = files_test_quiet_begin(directory, path, sizeof(path));
        result = result && lane_run(lanes, files_run_lane, &files, 0);
        run->stderr_bytes += files_test_quiet_end(saved, path);
        run->rounds += files.rounds;
        run->requests += files.requests_read;
        run->searches += files.searches_made;
        run->absent += files.absent;
        run->candidates += files.candidates_exchanged;
        run->shared_found += files.shared_found;
        run->failed |= files.failed;
        files_run_release(&files);
    }
    pp_release(&search);
    run->prepared = run->cache.prepared_records;
    run->units = result ? calloc(work->unit_count + 1, sizeof(FilesTestUnit)) : 0;
    run->unit_count = work->unit_count;
    result = result && run->units != 0;
    for (uint32_t u = 0; result && u < work->unit_count; u += 1)
    {
        FilesTestUnit* out = &run->units[u];
        const bool initialized = pp_initialize(&unit, &options, &run->table);
        out->ran = initialized && pp_run(&unit, work->units[u]);
        const PPTokens* tokens = &unit.output;
        out->arena = arena_reserve((uint64_t)tokens->count * 10 + 256);
        out->kinds = arena_push(&out->arena, (uint64_t)tokens->count + 1, 64);
        out->flags = arena_push(&out->arena, (uint64_t)tokens->count + 1, 64);
        out->values = arena_push_array(&out->arena, (uint64_t)tokens->count + 1, sizeof(uint32_t), 64);
        out->locations = arena_push_array(&out->arena, (uint64_t)tokens->count + 1, sizeof(uint32_t), 64);
        result = initialized && out->kinds != 0 && out->flags != 0 && out->values != 0 && out->locations != 0;
        if (result)
        {
            memcpy(out->kinds, tokens->kinds, tokens->count);
            memcpy(out->flags, tokens->flags, tokens->count);
            memcpy(out->values, tokens->values, (uint64_t)tokens->count * sizeof(uint32_t));
            memcpy(out->locations, tokens->locations, (uint64_t)tokens->count * sizeof(uint32_t));
            out->count = tokens->count;
            out->errors = unit.errors;
            out->warnings = unit.warnings;
            out->probes = unit.probes;
        }
        pp_release(&unit);
    }
    if (!result)
    {
        printf("%s: the run on %u lanes couldn't be set up\n", work->name, lanes);
    }
    return result;
}

// Are two records' prepared fields the same, byte for byte?
static bool files_test_same_record(const PPFileData* a, const PPFileData* b)
{
    bool result = a->prepared == b->prepared && a->device == b->device && a->inode == b->inode && a->length == b->length;
    if (result && a->prepared)
    {
        const uint32_t count = a->token_count;
        result = b->token_count == count && memcmp(a->kinds, b->kinds, count) == 0 && memcmp(a->starts, b->starts, (uint64_t)count * sizeof(uint32_t)) == 0 &&
                 memcmp(a->values, b->values, (uint64_t)count * sizeof(uint32_t)) == 0 && a->guard == b->guard && a->directive_count == b->directive_count;
        for (uint32_t w = 0; result && w < (count + 63) / 64; w += 1)
        {
            // The last word's bits past the last token are refinement's, which only the tokens' bits are meant for.
            const uint64_t mask = w + 1 < (count + 63) / 64 || count % 64 == 0 ? ~0ull : _bzhi_u64(~0ull, count % 64);
            result = ((a->space[w] ^ b->space[w]) & mask) == 0;
        }
        const uint64_t directives = a->directive_count;
        result = result && memcmp(a->directive_hash, b->directive_hash, directives * sizeof(uint32_t)) == 0 &&
                 memcmp(a->directive_end, b->directive_end, directives * sizeof(uint32_t)) == 0 && memcmp(a->directive_kind, b->directive_kind, directives) == 0 &&
                 memcmp(a->directive_next, b->directive_next, directives * sizeof(uint32_t)) == 0;
    }
    return result;
}

// Does `other` have everything `reference` has: the same shared ids and spellings, records, walk and units' streams?
static bool files_test_same(const FilesTestWork* work, const FilesTestRun* reference, const FilesTestRun* other)
{
    const InternTable* x = &reference->table;
    const InternTable* y = &other->table;
    bool result = x->count == y->count;
    IdentId id = 1;
    while (result && id < x->count && intern_length(x, id) == intern_length(y, id) && memcmp(intern_bytes(x, id), intern_bytes(y, id), intern_length(x, id)) == 0)
    {
        id += 1;
    }
    if (!result || id < x->count)
    {
        printf("%s: the shared tables differ on %u lanes (%u and %u ids; first different id %u)\n", work->name, other->lanes, x->count, y->count, id);
        result = false;
    }
    bool records = reference->cache.record_count == other->cache.record_count;
    for (uint32_t r = 0; records && r < reference->cache.record_count; r += 1)
    {
        records = files_test_same_record(&reference->cache.records[r], &other->cache.records[r]);
        if (!records)
        {
            printf("%s: record %u differs on %u lanes\n", work->name, r, other->lanes);
        }
    }
    if (reference->cache.record_count != other->cache.record_count)
    {
        printf("%s: %u records on %u lanes, %u on %u\n", work->name, other->cache.record_count, other->lanes, reference->cache.record_count, reference->lanes);
    }
    const bool walks = reference->rounds == other->rounds && reference->requests == other->requests && reference->searches == other->searches &&
                       reference->absent == other->absent && reference->prepared == other->prepared;
    if (!walks)
    {
        printf("%s: the walk differs on %u lanes (rounds %u/%u, requests %lu/%lu, searches %lu/%lu, not found %lu/%lu, prepared %lu/%lu)\n", work->name, other->lanes,
               other->rounds, reference->rounds, (unsigned long)other->requests, (unsigned long)reference->requests, (unsigned long)other->searches,
               (unsigned long)reference->searches, (unsigned long)other->absent, (unsigned long)reference->absent, (unsigned long)other->prepared,
               (unsigned long)reference->prepared);
    }
    result = result && records && walks;
    for (uint32_t u = 0; u < work->unit_count; u += 1)
    {
        const FilesTestUnit* a = &reference->units[u];
        const FilesTestUnit* b = &other->units[u];
        uint32_t i = 0;
        while (i < a->count && i < b->count && a->kinds[i] == b->kinds[i] && a->flags[i] == b->flags[i] && a->values[i] == b->values[i] &&
               a->locations[i] == b->locations[i])
        {
            i += 1;
        }
        const bool same = a->ran == b->ran && a->errors == b->errors && a->warnings == b->warnings && i == a->count && i == b->count;
        if (!same)
        {
            printf("%s: %s's stream differs on %u lanes at token %u of %u/%u\n", work->name, work->units[u], other->lanes, i, b->count, a->count);
        }
        result = result && same;
    }
    return result;
}

// Each of the work's units with a cache of its own, against the same unit with the reference run's cache: the same
// spellings, errors and warnings.
static bool files_test_alone(const FilesTestWork* work, const FilesTestRun* reference)
{
    static Preprocessor alone;
    static Preprocessor cached;
    static InternTable alone_table;
    bool result = true;
    for (uint32_t u = 0; u < work->unit_count; u += 1)
    {
        PPOptions options = work->options;
        options.files = 0;
        bool same = intern_initialize(&alone_table) && pp_initialize(&alone, &options, &alone_table);
        const bool alone_ran = same && pp_run(&alone, work->units[u]);
        options.files = (FileCache*)&reference->cache;
        same = same && pp_initialize(&cached, &options, (InternTable*)&reference->table);
        const bool cached_ran = same && pp_run(&cached, work->units[u]);
        PPSpellings ours = { 0 };
        PPSpellings theirs = { 0 };
        same = same && alone_ran == cached_ran && alone.errors == cached.errors && alone.warnings == cached.warnings && pp_stream_spellings(&cached, &ours) &&
               pp_stream_spellings(&alone, &theirs) && pp_compare_spellings(&cached, &ours, &theirs, work->units[u], "alone");
        if (!same)
        {
            printf("%s: %s differs from the unit run alone (errors %u/%u, warnings %u/%u)\n", work->name, work->units[u], cached.errors, alone.errors, cached.warnings,
                   alone.warnings);
        }
        arena_release(&ours.arena);
        arena_release(&theirs.arena);
        pp_release(&alone);
        pp_release(&cached);
        intern_release(&alone_table);
        result = result && same;
    }
    return result;
}

// Runs a work on every lane count, compares each run with the first's, and the units with themselves run alone. Keeps the
// first run in `reference` for the caller's own checks (it releases it).
static bool files_test_work(const FilesTestWork* work, const uint32_t* lanes, uint32_t lane_count, const char* directory, FilesTestRun* reference)
{
    bool result = files_test_run(work, lanes[0], directory, reference);
    for (uint32_t l = 1; result && l < lane_count; l += 1)
    {
        FilesTestRun other;
        result = files_test_run(work, lanes[l], directory, &other) && files_test_same(work, reference, &other);
        if (getenv("BUSTER_FILES_TEST_VERBOSE") != 0)
        {
            printf("%s: %u lanes: %lu candidates exchanged, %lu found in the shared table\n", work->name, lanes[l], (unsigned long)other.candidates,
                   (unsigned long)other.shared_found);
        }
        result = result && other.stderr_bytes == 0 && !other.failed;
        if (other.stderr_bytes != 0 || other.failed)
        {
            printf("%s: on %u lanes the walk %s\n", work->name, lanes[l], other.failed ? "ran out of memory" : "wrote to stderr");
        }
        files_test_release_run(&other);
    }
    result = result && reference->stderr_bytes == 0 && !reference->failed && files_test_alone(work, reference);
    if (reference->stderr_bytes != 0)
    {
        printf("%s: the walk wrote %lu bytes to stderr\n", work->name, (unsigned long)reference->stderr_bytes);
    }
    return result;
}

// The records the walk prepared that no unit entered: counted through units run again with the reference's cache, each
// record a unit prepares marked. The walk's records come first: a unit may add others after them (the predefines'
// file, a computed include's), which aren't counted.
static uint32_t files_test_unused(const FilesTestWork* work, FilesTestRun* reference)
{
    static Preprocessor unit;
    const uint32_t walked = reference->cache.record_count;
    uint8_t* used = calloc((size_t)walked + 1, 1);
    uint32_t result = 0;
    PPOptions options = work->options;
    options.files = &reference->cache;
    for (uint32_t u = 0; used != 0 && u < work->unit_count; u += 1)
    {
        if (pp_initialize(&unit, &options, &reference->table))
        {
            pp_run(&unit, work->units[u]);
            for (uint32_t d = 0; d < unit.data_count; d += 1)
            {
                const uint32_t record = unit.datas[d].record;
                if (record < walked && unit.datas[d].prepared)
                {
                    used[record] = 1;
                }
            }
        }
        pp_release(&unit);
    }
    for (uint32_t r = 0; used != 0 && r < walked; r += 1)
    {
        result += reference->cache.records[r].prepared && used[r] == 0;
    }
    free(used);
    return result;
}

// The tree's files, relative to its directory, and its directories (made first, removed last).
static const char* const files_test_tree_directories[] = { "sub", "I1", "I2", "I3", "sys" };
static const char* const files_test_tree_files[][2] = {
    { "main1.c", "#include \"a.h\"\n#include <sys1.h>\n#include \"sub/b.h\"\n#if 0\n#include \"missing.h\"\n#include \"unused.h\"\n#endif\n#define COMPUTED \"c.h\"\n"
                 "#include COMPUTED\n#include \"link.h\"\n#include \"hard.h\"\n#include \"once.h\"\n#include \"once.h\"\n#include \"guarded.h\"\n#include \"guarded.h\"\n"
                 "int main1 = A + B + SYS1 + C + ONCE + GUARDED;\n" },
    { "main2.c", "#include \"sub/../a.h\"\n#include <next.h>\n#include \"sub/b.h\"\nint main2 = A + B + NEXT + NEXT2;\n" },
    { "main3.c", "#include \"a.h\"\n#include \"nope.h\"\nint main3;\n" },
    { "a.h", "#ifndef A_H\n#define A_H\n#define A 1\n#endif\n" },
    { "sub/b.h", "#include \"../a.h\"\n#define B 2\n" },
    { "c.h", "#define C 3\n" },
    { "unused.h", "#define UNUSED 4\n" },
    { "once.h", "#pragma once\n#define ONCE 5\n" },
    { "guarded.h", "#ifndef GUARDED_H\n#define GUARDED_H\n#define GUARDED 6\n#endif\n" },
    { "I1/next.h", "#include_next <next.h>\n" },
    { "I2/next.h", "#include_next <next.h>\n#define NEXT2 8\n" },
    { "I3/next.h", "#define NEXT 7\n" },
    { "sys/sys1.h", "#define SYS1 9\n" },
};
// The links the tree makes beside its files: a symbolic link to a.h and a hard link to c.h.
static const char* const files_test_tree_links[] = { "link.h", "hard.h" };

static void files_test_remove_tree(const char* directory)
{
    char path[4200];
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(files_test_tree_files); i += 1)
    {
        snprintf(path, sizeof(path), "%s/%s", directory, files_test_tree_files[i][0]);
        unlink(path);
    }
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(files_test_tree_links); i += 1)
    {
        snprintf(path, sizeof(path), "%s/%s", directory, files_test_tree_links[i]);
        unlink(path);
    }
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(files_test_tree_directories); i += 1)
    {
        snprintf(path, sizeof(path), "%s/%s", directory, files_test_tree_directories[i]);
        rmdir(path);
    }
}

// The tree's check: its units, on every lane count, and its exact counts. Its rounds are small, so that lane 0 would
// prepare them alone: they are prepared on every lane instead, cut into parts (`split` false) or one range per lane.
static bool files_test_tree(const char* directory, const uint32_t* lanes, uint32_t lane_count, bool vectorized, bool split)
{
    char path[4200];
    bool result = true;
    for (uint32_t i = 0; result && i < BUSTER_ARRAY_LENGTH(files_test_tree_directories); i += 1)
    {
        snprintf(path, sizeof(path), "%s/%s", directory, files_test_tree_directories[i]);
        result = mkdir(path, 0700) == 0;
    }
    for (uint32_t i = 0; result && i < BUSTER_ARRAY_LENGTH(files_test_tree_files); i += 1)
    {
        snprintf(path, sizeof(path), "%s/%s", directory, files_test_tree_files[i][0]);
        result = files_test_write(path, files_test_tree_files[i][1]);
    }
    char target[4200];
    snprintf(path, sizeof(path), "%s/link.h", directory);
    result = result && symlink("a.h", path) == 0;
    snprintf(path, sizeof(path), "%s/hard.h", directory);
    snprintf(target, sizeof(target), "%s/c.h", directory);
    result = result && link(target, path) == 0;
    char mains[3][4200];
    char directories[4][4200];
    const char* units[3];
    for (uint32_t i = 0; i < 3; i += 1)
    {
        snprintf(mains[i], sizeof(mains[i]), "%s/main%u.c", directory, i + 1);
        units[i] = mains[i];
    }
    for (uint32_t i = 0; i < 4; i += 1)
    {
        snprintf(directories[i], sizeof(directories[i]), "%s/%s", directory, files_test_tree_directories[i + 1]);
    }
    const char* includes[3] = { directories[0], directories[1], directories[2] };
    const char* systems[1] = { directories[3] };
    FilesTestWork work = {
        .name = vectorized && split ? "tree, split once" : vectorized ? "tree" : split ? "tree, split once (scalar kernels)" : "tree (scalar kernels)",
        .thresholds = true,
        .lane_bytes = 0,
        .split_bytes = split ? UINT64_MAX : 0,
        .options = {
            .language = { .standard = 2017, .gnu = true },
            .scalar = !vectorized,
            .no_standard_includes = true,
            .include_directories = includes,
            .include_directory_count = 3,
            .system_directories = systems,
            .system_directory_count = 1,
        },
        .roots = units,
        .root_count = 3,
        .follow = true,
        .units = units,
        .unit_count = 3,
    };
    FilesTestRun reference = { 0 };
    result = result && files_test_work(&work, lanes, lane_count, directory, &reference);
    if (result)
    {
        // 13 files: the units, then a.h (reached as a.h, sub/../a.h and link.h), sys1.h, b.h, unused.h, hard.h (c.h,
        // which main1's computed include names), once.h, guarded.h and I1's next.h, then I2's, then I3's.
        const uint32_t unused = files_test_unused(&work, &reference);
        const bool counts = reference.prepared == 13 && reference.rounds == 4 && reference.absent == 2 && unused == 1;
        // After the walk, main1 opens only c.h (its computed include), and the others open nothing; main3 reports nope.h.
        const bool units_right = reference.units[0].errors == 0 && reference.units[0].probes.opens == 1 && reference.units[1].errors == 0 &&
                                 reference.units[1].probes.opens == 0 && reference.units[2].errors == 1 && reference.units[2].probes.opens == 0;
        uint32_t a_records = 0;
        struct stat stats;
        snprintf(path, sizeof(path), "%s/a.h", directory);
        a_records = stat(path, &stats) == 0 && file_cache_find_identity(&reference.cache, (uint64_t)stats.st_dev, (uint64_t)stats.st_ino) != BUSTER_PP_NONE;
        result = counts && units_right && a_records == 1;
        if (!counts)
        {
            printf("%s: %lu files prepared (13), %u rounds (4), %lu not found (2), %u unused (1)\n", work.name, (unsigned long)reference.prepared, reference.rounds,
                   (unsigned long)reference.absent, unused);
        }
        if (!units_right)
        {
            printf("%s: the units' errors %u, %u, %u (0, 0, 1) and opens %lu, %lu, %lu (1, 0, 0)\n", work.name, reference.units[0].errors, reference.units[1].errors,
                   reference.units[2].errors, (unsigned long)reference.units[0].probes.opens, (unsigned long)reference.units[1].probes.opens,
                   (unsigned long)reference.units[2].probes.opens);
        }
        if (result)
        {
            printf("%s: 13 files prepared once each in 4 rounds, the same on every lane count; 1 unused, 2 not found, no diagnostic\n", work.name);
        }
    }
    files_test_release_run(&reference);
    return result;
}

// A random number generator of the test's own (splitmix64).
static uint64_t files_test_random(uint64_t* state)
{
    *state += 0x9e3779b97f4a7c15ull;
    uint64_t z = *state;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

// Writes a random identifier of `length` letters and digits (a letter first) to `out`.
static void files_test_identifier(uint64_t* state, char* out, uint32_t length)
{
    static const char letters[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_0123456789";
    for (uint32_t i = 0; i < length; i += 1)
    {
        out[i] = letters[files_test_random(state) % (i == 0 ? 53 : 63)];
    }
    out[length] = 0;
}

#define BUSTER_FILES_TEST_SPELLING_FILES 240
#define BUSTER_FILES_TEST_VOCABULARY 30000

// The spellings' check: generated files of identifiers, on every lane count without following includes, then in two
// runs into one cache.
static bool files_test_spellings(const char* directory, const uint32_t* lanes, uint32_t lane_count, uint64_t seed)
{
    uint64_t state = seed;
    char* vocabulary = calloc(BUSTER_FILES_TEST_VOCABULARY, 96);
    char (*paths)[4200] = calloc(BUSTER_FILES_TEST_SPELLING_FILES, sizeof(*paths));
    const char** roots = calloc(BUSTER_FILES_TEST_SPELLING_FILES, sizeof(char*));
    char* text = malloc(1u << 20);
    uint64_t written = 0;
    bool result = vocabulary != 0 && paths != 0 && roots != 0 && text != 0;
    for (uint32_t v = 0; result && v < BUSTER_FILES_TEST_VOCABULARY; v += 1)
    {
        // Mostly short names, as in C, some past 16 bytes (the merge compares their records) and past 63 (refinement's
        // scalar path).
        const uint64_t pick = files_test_random(&state) % 100;
        const uint32_t length = pick < 70 ? 1 + (uint32_t)(files_test_random(&state) % 12) : pick < 95 ? 13 + (uint32_t)(files_test_random(&state) % 30)
                                                                                                    : 60 + (uint32_t)(files_test_random(&state) % 30);
        files_test_identifier(&state, vocabulary + (uint64_t)v * 96, length);
    }
    for (uint32_t f = 0; result && f < BUSTER_FILES_TEST_SPELLING_FILES; f += 1)
    {
        uint64_t length = 0;
        const uint32_t count = 500 + (uint32_t)(files_test_random(&state) % 3000);
        for (uint32_t i = 0; i < count && length + 256 < (1u << 20); i += 1)
        {
            const uint64_t pick = files_test_random(&state) % 1000;
            char name[128];
            if (pick < 700)
            {
                // The shared vocabulary, drawn unevenly (the cube of a uniform number favors its first names).
                const double u = (double)(files_test_random(&state) >> 11) / 9007199254740992.0;
                const uint32_t v = (uint32_t)((double)BUSTER_FILES_TEST_VOCABULARY * u * u * u);
                snprintf(name, sizeof(name), "%s", vocabulary + (uint64_t)v * 96);
            }
            else if (pick < 990)
            {
                // A name of the file's own.
                char tail[16];
                files_test_identifier(&state, tail, 4);
                snprintf(name, sizeof(name), "f%u_%u%s", f, i, tail);
            }
            else if (pick < 995)
            {
                // A shared name with a line splice inside, which interns as the name without it.
                const char* word = vocabulary + (uint64_t)(files_test_random(&state) % 100) * 96;
                const uint32_t word_length = (uint32_t)strlen(word);
                const uint32_t cut = word_length > 1 ? 1 + (uint32_t)(files_test_random(&state) % (word_length - 1)) : word_length;
                snprintf(name, sizeof(name), "%.*s\\\n%s", (int)cut, word, word + cut);
            }
            else
            {
                // A UCN, which interns as its UTF-8 (é).
                snprintf(name, sizeof(name), "caf\\u00e9%u", (uint32_t)(files_test_random(&state) % 50));
            }
            length += (uint64_t)snprintf(text + length, (1u << 20) - length, "%s%s", name, files_test_random(&state) % 8 == 0 ? "\n" : " ");
        }
        snprintf(paths[f], sizeof(paths[f]), "%s/s%u.c", directory, f);
        roots[f] = paths[f];
        result = files_test_write(paths[f], text);
        written += length;
    }
    FilesTestWork work = {
        .name = "spellings",
        .options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true },
        .roots = roots,
        .root_count = BUSTER_FILES_TEST_SPELLING_FILES,
        .follow = false,
    };
    FilesTestRun reference = { 0 };
    result = result && files_test_work(&work, lanes, lane_count, directory, &reference);
    // And with one range per lane, then with two lanes working (one range each) and the others idle, on every lane
    // count, against the same reference.
    FilesTestWork once = work;
    once.name = "spellings, split once";
    once.thresholds = true;
    once.lane_bytes = 0;
    once.split_bytes = UINT64_MAX;
    FilesTestWork two = work;
    two.name = "spellings, two lanes working";
    two.thresholds = true;
    two.lane_bytes = written / 2;
    two.split_bytes = 0;
    for (uint32_t l = 1; result && l < lane_count; l += 1)
    {
        FilesTestRun other;
        result = files_test_run(&once, lanes[l], directory, &other) && files_test_same(&once, &reference, &other);
        files_test_release_run(&other);
        result = result && files_test_run(&two, lanes[l], directory, &other) && files_test_same(&two, &reference, &other);
        files_test_release_run(&other);
    }
    const bool grew = result && reference.table.count > 40960;
    if (result)
    {
        printf("spellings: %u files, %u ids, %lu tokens, the same on every lane count, in parts, split once and on two lanes\n",
               BUSTER_FILES_TEST_SPELLING_FILES, reference.table.count, (unsigned long)reference.cache.prepared_tokens);
    }
    if (result && !grew)
    {
        printf("spellings: only %u ids, too few for the shared table to grow in a merge\n", reference.table.count);
    }
    result = result && grew;
    // Two runs into one cache, on each lane count, against the reference (one run on the first lane count).
    FilesTestWork split = work;
    split.name = "spellings in two runs";
    split.split = BUSTER_FILES_TEST_SPELLING_FILES / 2;
    for (uint32_t l = 0; result && l < lane_count; l += 1)
    {
        FilesTestRun other;
        result = files_test_run(&split, lanes[l], directory, &other);
        // The two runs' walks add up to the one's but in their rounds; the tables and records must be the same.
        other.rounds = reference.rounds;
        other.requests = reference.requests;
        other.searches = reference.searches;
        result = result && files_test_same(&split, &reference, &other);
        files_test_release_run(&other);
    }
    if (result)
    {
        printf("spellings in two runs: the same as one run, on every lane count\n");
    }
    files_test_release_run(&reference);
    for (uint32_t f = 0; paths != 0 && f < BUSTER_FILES_TEST_SPELLING_FILES; f += 1)
    {
        unlink(paths[f]);
    }
    free(vocabulary);
    free(paths);
    free(roots);
    free(text);
    return result;
}

// The files' check: each a unit with the options, all the walk's roots; or with `flat`, only prepared.
static bool files_test_files(const PPOptions* options, const char* const* files, uint32_t count, bool flat, const char* directory, const uint32_t* lanes,
                             uint32_t lane_count)
{
    FilesTestWork work = {
        .name = flat ? "flat files" : "files",
        .options = *options,
        .roots = files,
        .root_count = count,
        .follow = !flat,
        .units = files,
        .unit_count = flat ? 0 : count,
    };
    FilesTestRun reference = { 0 };
    bool result = files_test_work(&work, lanes, lane_count, directory, &reference);
    if (result && flat)
    {
        printf("flat files: %u files (%lu tokens, %u ids), the same on every lane count\n", reference.cache.record_count, (unsigned long)reference.cache.prepared_tokens,
               reference.table.count);
    }
    else if (result)
    {
        const uint32_t unused = files_test_unused(&work, &reference);
        uint64_t opens = 0;
        uint64_t tokens = 0;
        for (uint32_t u = 0; u < count; u += 1)
        {
            opens += reference.units[u].probes.opens;
            tokens += reference.units[u].count;
        }
        printf("files: %u units (%lu output tokens), the same on every lane count and as each unit alone: %lu files prepared in %u rounds, %u of them unused; "
               "%lu requests, %lu searches, %lu not found; the units then opened %lu paths\n",
               count, (unsigned long)tokens, (unsigned long)reference.prepared, reference.rounds, unused, (unsigned long)reference.requests,
               (unsigned long)reference.searches, (unsigned long)reference.absent, (unsigned long)opens);
    }
    files_test_release_run(&reference);
    return result;
}

// Parses a list of lane counts ("1,2,3,8").
static uint32_t files_test_lanes(const char* text, uint32_t* lanes)
{
    uint32_t result = 0;
    const char* p = text;
    bool valid = true;
    while (valid && *p != 0 && result < BUSTER_FILES_TEST_MAX_LANES)
    {
        char* end;
        const unsigned long value = strtoul(p, &end, 10);
        valid = end != p && value >= 1 && value <= BUSTER_LANE_MAX_COUNT && (*end == ',' || *end == 0);
        lanes[result] = (uint32_t)value;
        result += valid;
        p = *end == ',' ? end + 1 : end;
    }
    return valid ? result : 0;
}

static int run_files_tests(int argument_count, char** arguments)
{
    uint32_t lanes[BUSTER_FILES_TEST_MAX_LANES] = { 1, 2, 3, 8 };
    uint32_t lane_count = 4;
    uint64_t seed = 1;
    bool keep = false;
    bool flat = false;
    FilesSources sources = { 0 };
    char** passed = calloc((size_t)argument_count + 2, sizeof(char*));
    const char** files = calloc(BUSTER_FILES_MAX_FILES, sizeof(char*));
    int passed_count = 0;
    uint32_t file_count = 0;
    bool valid = passed != 0 && files != 0;
    for (int i = 0; valid && i < argument_count; i += 1)
    {
        const char* argument = arguments[i];
        const bool takes_value = strcmp(argument, "-I") == 0 || strcmp(argument, "-D") == 0 || strcmp(argument, "-U") == 0 || strcmp(argument, "-isystem") == 0 ||
                                 strcmp(argument, "-iquote") == 0 || strcmp(argument, "-idirafter") == 0 || strcmp(argument, "-include") == 0 ||
                                 strcmp(argument, "--predefines") == 0 || strcmp(argument, "-x") == 0;
        if (strcmp(argument, "--lanes") == 0 && i + 1 < argument_count)
        {
            lane_count = files_test_lanes(arguments[i + 1], lanes);
            valid = lane_count != 0;
            i += 1;
        }
        else if (strcmp(argument, "--seed") == 0 && i + 1 < argument_count)
        {
            seed = strtoull(arguments[i + 1], 0, 10);
            i += 1;
        }
        else if (strcmp(argument, "--keep") == 0)
        {
            keep = true;
        }
        else if (strcmp(argument, "--flat") == 0)
        {
            flat = true;
        }
        else if (argument[0] == '-')
        {
            passed[passed_count++] = arguments[i];
            if (takes_value && i + 1 < argument_count)
            {
                i += 1;
                passed[passed_count++] = arguments[i];
            }
        }
        else
        {
            valid = files_list_sources(&sources, argument);
            if (!valid)
            {
                printf("can't read %s\n", argument);
            }
        }
    }
    file_count = sources.count;
    for (uint32_t f = 0; valid && f < file_count; f += 1)
    {
        files[f] = sources.paths[f];
    }
    char directory[4096];
    const char* temporary = getenv("TMPDIR");
    snprintf(directory, sizeof(directory), "%s/buster-files-test-XXXXXX", temporary != 0 ? temporary : "/tmp");
    bool passed_all = valid && mkdtemp(directory) != 0;
    if (!valid)
    {
        printf("usage: test-files [--lanes LIST] [--seed N] [--keep] [--flat] [options] [path...]\n");
    }
    if (passed_all && file_count == 0)
    {
        for (uint32_t way = 0; way < 4; way += 1)
        {
            passed_all &= files_test_tree(directory, lanes, lane_count, way < 2, (way & 1) != 0);
            if (!keep)
            {
                files_test_remove_tree(directory);
            }
        }
        passed_all &= files_test_spellings(directory, lanes, lane_count, seed);
    }
    else if (passed_all)
    {
        Arena arena = arena_reserve(1ull << 30);
        PPOptions options;
        const char* input;
        const char* output;
        bool markers;
        // pp_parse_options wants one input: give it the first file, and use its options for every file.
        passed[passed_count] = (char*)files[0];
        passed_all = pp_parse_options(passed_count + 1, passed, &options, &arena, &input, &output, &markers) &&
                     files_test_files(&options, files, file_count, flat, directory, lanes, lane_count);
        arena_release(&arena);
    }
    if (!keep)
    {
        rmdir(directory);
    }
    printf("test-files: %s\n", passed_all ? "passed" : "FAILED");
    arena_release(&sources.arena);
    free(passed);
    free(files);
    return passed_all ? 0 : 1;
}
