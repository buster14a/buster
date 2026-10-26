// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The per-file jobs' tests (c_files.c), for the test-files command:
//
//   build/compiler test-files [--lanes LIST] [--seed N] [--id-order N] [--keep] [--flat] [options] [path...]
//
// Each check runs the same work with the file cache filled on each lane count of --lanes (1, 2, 3 and 8 by default),
// every time with a new intern table and cache, and requires the same results from every lane count, byte for byte:
// the shared table's ids and spellings, every record's tokens, values, leading-space bits, directive table and guard,
// what the walk did, and each translation unit's output stream (kinds, flags, values and locations), the text -E prints
// for it without and with line markers, and its diagnostics, when it preprocesses with the cache. One lane refines
// straight into the shared table, so it is the merge's scalar twin, and the others must give its ids. Each unit's
// spellings and diagnostics must also be those it gives with a cache of its own (the preprocessor as it runs alone), with
// as many errors and warnings, and nothing may be written to stderr while the cache is filled.
// Then every check runs again on each lane count with the shared table's ids handed out in another order (#153: ids are
// opaque, and nothing may depend on their order): the table is primed before the walk fills the cache, with names no
// file has, then every other name of the first run's table in another order (--id-order N picks it, 153 by default), so
// that the walk finds those in the table and gives the others ids after them; every lane prepares every round, so that on
// more than one lane the merge gives them. The fixed ids of the predefined names stay theirs, and the other names must
// actually change ids. Every unit must give the first run's -E text, diagnostics and stream, each id there (an
// identifier's or a keyword's value) spelled as the first run's, and every record and the walk must be the first run's,
// each identifier's value and guard spelled the same.
//   - The tree: a directory of the test's own under $TMPDIR (or /tmp), with three translation units and the headers they
//     include: by the includer's directory and through -I and -isystem, an #include_next chain through three
//     directories, a header reached by four paths (a symbolic link and `..` among them), a hard link to a header that a
//     computed include names too (which the walk can't follow: the unit finds it prepared by its other name), a guarded
//     header and a #pragma once header included twice, a header under #if 0 that exists (prepared but never entered) and
//     one that doesn't (no diagnostic), and a missing header in an active line, which the unit reports. Two warnings name
//     identifiers: a header that two units include warns in an #if with notes naming the macros it was expanded from, and
//     a unit redefines a macro. The counts are exact: the files prepared (each identity once), the rounds, the unused
//     files, the headers not found, the units' errors and warnings, and the system calls each unit made after the walk
//     (only the computed include's).
//   - Spellings: generated files of identifiers (a shared vocabulary drawn unevenly, names of their own, long ones, line
//     splices and UCNs) prepared without following includes, enough new spellings for the shared table to grow during a
//     merge, and a translation unit that includes them all: in parts, split once, and with two lanes working (in parts,
//     from the run's counter, and split once) and the others idle; and again in two runs into one cache, whose second
//     finds most of its spellings in the shared table, against one run of all of them on one lane.
//   - Contexts: a guarded header that includes itself through ./ and sub/.., which the walk must take once, and one
//     header in two directories (a hard link) whose own local.h differ, which it must walk in both.
//   - Rounds: the walk's trace (benchmark-files --rounds) on include chains of exactly the rounds it keeps apart (16) and
//     of more (21), a file a round: on every lane count it must keep each round, the last kept one with every later one,
//     and snapshots that go forward on every lane from the walk's start to its end, the one after the last kept round's.
//   - Special paths: a FIFO that an include under #if 0 names (the walk's probe mustn't block) and an active one
//     names, and a quoted name of 2,400 bytes in a directory of 2,539, whose three requests must share one search.
//   - Memory: runs short of the round's arena or of one lane's scratch memory, which may fail, but after which every
//     unit must give what it gives alone.
//   The checks that would hang if they regressed run under a 60-second alarm, which fails the test.
//   - Files: each file (or each .c and .h file under a directory) as a translation unit with the options (as for -E:
//     -I, -isystem, -D, --predefines, -std=, ...), all of them the walk's roots. With --flat, the files are only prepared,
//     without following their includes or preprocessing them (a corpus such as the C library's, which is files rather
//     than translation units).
// --seed N changes the generated spellings, --id-order N the order of the ids, and --keep leaves the directory.
//
// main.c includes this file after c_files.c and c_preprocessor_test.c, whose spellings, comparison and printing it uses.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>

#define BUSTER_FILES_TEST_MAX_LANES 16
#define BUSTER_FILES_TEST_MAX_UNITS 4096

// A unit's results with a cache: its output stream (copied), errors and warnings, its system calls, the text -E prints
// for it without and with line markers (text[0] and text[1]) and its diagnostics, as printed (each by a memory stream).
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
    char* text[2];
    size_t text_length[2];
    char* diagnostics;
    size_t diagnostic_length;
    bool ran;
};

// The shared table's ids in another order (#153): the names of a first run's table, primed by files_test_prime with
// `seed`.
typedef struct FilesTestOrder FilesTestOrder;
struct FilesTestOrder
{
    const InternTable* names;
    uint64_t seed;
};

// One lane count's run: the table and cache it filled, what the walk did, and its units' results.
typedef struct FilesTestRun FilesTestRun;
struct FilesTestRun
{
    InternTable table;
    FileCache cache;
    uint32_t lanes;
    // With the ids in another order: the table's count after priming (the ids below it were primed, the others added by
    // the walk and the units), and how many of the names primed first are no name of the first run's.
    IdentId primed_end;
    uint32_t padding;
    // What the walk did (the run's counts), the bytes it wrote to stderr, and the records it prepared, their tokens and the
    // table's ids after it (the units add their own).
    uint32_t rounds;
    uint64_t requests;
    uint64_t searches;
    uint64_t absent;
    uint64_t prepared;
    uint64_t tokens;
    IdentId ids;
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
    // The runs' FilesRun.lane_bytes and split_bytes, if `thresholds` (with shared_bytes 0, so that a round of any bytes
    // may take several lanes).
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
        FilesTestUnit* unit = &run->units[u];
        arena_release(&unit->arena);
        free(unit->text[0]);
        free(unit->text[1]);
        free(unit->diagnostics);
    }
    free(run->units);
    file_cache_release(&run->cache);
    intern_release(&run->table);
    *run = (FilesTestRun){ 0 };
}

// Primes a fresh shared table as test-preprocessor's check of the ids' order does (pp_order_prime): first with names
// that no file has (17 to 47 of them, by the seed), then with every other name of the first run's table past the
// predefined names, whose fixed ids are the table's contract, in reverse from a place the seed picks. The walk then
// finds those in the table, and gives the others ids after them. Returns false if a name doesn't fit.
static bool files_test_prime(InternTable* table, const FilesTestOrder* order, FilesTestRun* run)
{
    const InternTable* names = order->names;
    const uint32_t padding = 17 + (uint32_t)(order->seed % 31);
    bool result = true;
    for (uint32_t i = 0; result && i < padding; i += 1)
    {
        char text[80];
        const int length = snprintf(text, sizeof(text), "__buster_determinism_unused_%u", i);
        result = intern_find_or_add(table, (const uint8_t*)text, (uint32_t)length) != 0;
        run->padding += intern_find(names, (const uint8_t*)text, (uint32_t)length) == 0;
    }
    const uint32_t count = names->count - C_NAME_COUNT;
    for (uint32_t i = 0; result && i < count; i += 2)
    {
        const IdentId id = C_NAME_COUNT + (uint32_t)((order->seed % count + count - 1ull - i) % count);
        result = intern_find_or_add(table, intern_bytes(names, id), intern_length(names, id)) != 0;
    }
    run->primed_end = table->count;
    return result;
}

// The cache filled on `lanes` lanes from the work's roots (in one run or two), then each of the work's units
// preprocessed with it. `directory` takes the walk's stderr while it runs. With `order`, the shared table is primed
// first, so that it gives the ids in another order.
static bool files_test_run(const FilesTestWork* work, uint32_t lanes, const char* directory, const FilesTestOrder* order, FilesTestRun* run)
{
    static Preprocessor search;
    static Preprocessor unit;
    *run = (FilesTestRun){ .lanes = lanes };
    PPOptions options = work->options;
    bool result = intern_initialize(&run->table) && (order == 0 || files_test_prime(&run->table, order, run)) &&
                  file_cache_initialize(&run->cache, &run->table, pp_prepare_options(&options));
    options.files = &run->cache;
    result = result && pp_initialize(&search, &options, &run->table);
    const uint32_t runs = work->split != 0 ? 2 : 1;
    for (uint32_t r = 0; result && r < runs; r += 1)
    {
        const uint32_t begin = r == 0 ? 0 : work->split;
        const uint32_t end = runs == 2 && r == 0 ? work->split : work->root_count;
        FilesRun files;
        result = files_run_initialize(&files, &run->cache, &search, work->roots + begin, end - begin, work->follow, lanes);
        files.shared_bytes = work->thresholds ? 0 : files.shared_bytes;
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
    run->tokens = run->cache.prepared_tokens;
    run->ids = run->table.count;
    run->units = result ? calloc(work->unit_count + 1, sizeof(FilesTestUnit)) : 0;
    run->unit_count = work->unit_count;
    result = result && run->units != 0;
    // The units keep their expansions' starts, which -E's line markers need, and print their diagnostics into memory.
    PPOptions unit_options = options;
    unit_options.expansion_starts = true;
    for (uint32_t u = 0; result && u < work->unit_count; u += 1)
    {
        FilesTestUnit* out = &run->units[u];
        FILE* diagnostics = open_memstream(&out->diagnostics, &out->diagnostic_length);
        unit_options.diagnostics = diagnostics;
        const bool initialized = diagnostics != 0 && pp_initialize(&unit, &unit_options, &run->table);
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
        for (uint32_t k = 0; result && k < 2; k += 1)
        {
            out->text[k] = pp_test_print(&unit, !options.scalar, k != 0, &out->text_length[k]);
            result = out->text[k] != 0;
        }
        pp_release(&unit);
        result = diagnostics != 0 && fclose(diagnostics) == 0 && result;
    }
    if (!result)
    {
        printf("%s: the run on %u lanes%s couldn't be set up\n", work->name, lanes, order != 0 ? " with the ids in another order" : "");
    }
    return result;
}

// Do ids a (of table x) and b (of table y) stand for the same name? With `opaque`, the tables give their ids in different
// orders, and the names' spellings must be the same; otherwise the ids must be.
static bool files_test_same_id(const InternTable* x, const InternTable* y, bool opaque, IdentId a, IdentId b)
{
    const bool result = opaque ? a < x->count && b < y->count && intern_length(x, a) == intern_length(y, b) &&
                                     memcmp(intern_bytes(x, a), intern_bytes(y, b), intern_length(x, a)) == 0
                               : a == b;
    return result;
}

// Do two output tokens of `kind` with values a and b say the same? An identifier's or a keyword's value is an id
// (files_test_same_id), and any other's must be the same.
static bool files_test_same_value(const InternTable* x, const InternTable* y, bool opaque, uint8_t kind, uint32_t a, uint32_t b)
{
    const bool named = kind == C_TOKEN_RAW_IDENTIFIER || (kind >= BUSTER_PP_KIND_KEYWORD && kind < BUSTER_PP_KIND_PRAGMA_END);
    return named ? files_test_same_id(x, y, opaque, a, b) : a == b;
}

// Are two records' prepared fields the same, byte for byte? With `opaque`, but for their ids: each identifier's value and
// the guard, ids of the records' tables (x and y), are compared by files_test_same_id.
static bool files_test_same_record(const PPFileData* a, const PPFileData* b, const InternTable* x, const InternTable* y, bool opaque)
{
    bool result = a->prepared == b->prepared && a->device == b->device && a->inode == b->inode && a->length == b->length;
    if (result && a->prepared)
    {
        const uint32_t count = a->token_count;
        result = b->token_count == count && memcmp(a->kinds, b->kinds, count) == 0 && memcmp(a->starts, b->starts, (uint64_t)count * sizeof(uint32_t)) == 0 &&
                 files_test_same_id(x, y, opaque, a->guard, b->guard) && a->directive_count == b->directive_count;
        result = result && a->raw_error_count == b->raw_error_count;
        for (uint32_t e = 0; result && e < a->raw_error_count; e += 1)
        {
            result = a->raw_errors[e].token == b->raw_errors[e].token && a->raw_errors[e].offset == b->raw_errors[e].offset && a->raw_errors[e].kind == b->raw_errors[e].kind;
        }
        result = result && (opaque || memcmp(a->values, b->values, (uint64_t)count * sizeof(uint32_t)) == 0);
        for (uint32_t t = 0; result && opaque && t < count; t += 1)
        {
            const bool identifier = (a->kinds[t] & (uint8_t)~BUSTER_C_TOKEN_START_OF_LINE) == C_TOKEN_RAW_IDENTIFIER;
            result = identifier ? files_test_same_id(x, y, true, a->values[t], b->values[t]) : a->values[t] == b->values[t];
        }
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
        // The line pages of a file prepared a window at a time (an entry per page, and one for the end).
        const uint64_t pages = (uint64_t)a->length / BUSTER_PP_LINE_PAGE_BYTES + 1;
        result = result && (a->line_pages != 0) == (b->line_pages != 0) && (a->line_pages == 0 || memcmp(a->line_pages, b->line_pages, pages * sizeof(uint32_t)) == 0);
    }
    return result;
}

// Are a unit's two texts (its -E text or its diagnostics) the same, byte for byte? If not, prints where they first differ:
// `what` names the text, and `how` the other run.
static bool files_test_same_text(const FilesTestWork* work, const char* unit, const char* what, const char* how, const char* a, size_t a_length, const char* b,
                                 size_t b_length)
{
    const bool result = a_length == b_length && (a_length == 0 || memcmp(a, b, a_length) == 0);
    if (!result)
    {
        size_t at = 0;
        while (at < a_length && at < b_length && a[at] == b[at])
        {
            at += 1;
        }
        printf("%s: %s's %s differs %s at byte %zu of %zu/%zu\n", work->name, unit, what, how, at, b_length, a_length);
    }
    return result;
}

// Does `other` have everything `reference` has: the same shared ids and spellings, records, walk, and units' streams, -E
// texts and diagnostics? With `opaque`, `other` gave the ids in another order (files_test_prime): an id in a record or a
// stream must stand for the same name rather than be the same id (files_test_same_id), and its table's names are
// files_test_same_names's to check.
static bool files_test_same(const FilesTestWork* work, const FilesTestRun* reference, const FilesTestRun* other, bool opaque)
{
    const InternTable* x = &reference->table;
    const InternTable* y = &other->table;
    char how[64];
    snprintf(how, sizeof(how), "on %u lanes%s", other->lanes, opaque ? " with the ids in another order" : "");
    bool result = opaque || x->count == y->count;
    IdentId id = 1;
    while (result && !opaque && id < x->count && intern_length(x, id) == intern_length(y, id) &&
           memcmp(intern_bytes(x, id), intern_bytes(y, id), intern_length(x, id)) == 0)
    {
        id += 1;
    }
    if (!result || (!opaque && id < x->count))
    {
        printf("%s: the shared tables differ on %u lanes (%u and %u ids; first different id %u)\n", work->name, other->lanes, x->count, y->count, id);
        result = false;
    }
    bool records = reference->cache.record_count == other->cache.record_count;
    for (uint32_t r = 0; records && r < reference->cache.record_count; r += 1)
    {
        records = files_test_same_record(&reference->cache.records[r], &other->cache.records[r], x, y, opaque);
        if (!records)
        {
            printf("%s: record %u differs %s\n", work->name, r, how);
        }
    }
    if (reference->cache.record_count != other->cache.record_count)
    {
        printf("%s: %u records %s, %u on %u\n", work->name, other->cache.record_count, how, reference->cache.record_count, reference->lanes);
    }
    const bool walks = reference->rounds == other->rounds && reference->requests == other->requests && reference->searches == other->searches &&
                       reference->absent == other->absent && reference->prepared == other->prepared;
    if (!walks)
    {
        printf("%s: the walk differs %s (rounds %u/%u, requests %lu/%lu, searches %lu/%lu, not found %lu/%lu, prepared %lu/%lu)\n", work->name, how, other->rounds,
               reference->rounds, (unsigned long)other->requests, (unsigned long)reference->requests, (unsigned long)other->searches,
               (unsigned long)reference->searches, (unsigned long)other->absent, (unsigned long)reference->absent, (unsigned long)other->prepared,
               (unsigned long)reference->prepared);
    }
    result = result && records && walks;
    for (uint32_t u = 0; u < work->unit_count; u += 1)
    {
        const FilesTestUnit* a = &reference->units[u];
        const FilesTestUnit* b = &other->units[u];
        uint32_t i = 0;
        while (i < a->count && i < b->count && a->kinds[i] == b->kinds[i] && a->flags[i] == b->flags[i] && a->locations[i] == b->locations[i] &&
               files_test_same_value(x, y, opaque, a->kinds[i], a->values[i], b->values[i]))
        {
            i += 1;
        }
        const bool streams = a->ran == b->ran && a->errors == b->errors && a->warnings == b->warnings && i == a->count && i == b->count;
        if (!streams)
        {
            printf("%s: %s's stream differs %s at token %u of %u/%u (errors %u/%u, warnings %u/%u)\n", work->name, work->units[u], how, i, b->count, a->count, b->errors,
                   a->errors, b->warnings, a->warnings);
        }
        const bool texts = files_test_same_text(work, work->units[u], "-E text", how, a->text[0], a->text_length[0], b->text[0], b->text_length[0]) &&
                           files_test_same_text(work, work->units[u], "-E text with line markers", how, a->text[1], a->text_length[1], b->text[1], b->text_length[1]) &&
                           files_test_same_text(work, work->units[u], "diagnostics", how, a->diagnostics, a->diagnostic_length, b->diagnostics, b->diagnostic_length);
        result = result && streams && texts;
    }
    return result;
}

// The first run's names in a run with the ids in another order: each must be in its table, which must have no others but
// the names primed first that no file has. Of the names primed, and of those the walk and the units added after them,
// some must have changed ids, if there are any; `changed` counts them all.
static bool files_test_same_names(const FilesTestWork* work, const FilesTestRun* reference, const FilesTestRun* other, uint32_t* changed)
{
    const InternTable* x = &reference->table;
    const InternTable* y = &other->table;
    uint32_t missing = 0;
    // By whether the name was primed: how many there are, and how many changed ids.
    uint32_t names[2] = { 0 };
    uint32_t moved[2] = { 0 };
    for (IdentId id = C_NAME_COUNT; id < x->count; id += 1)
    {
        const IdentId found = intern_find(y, intern_bytes(x, id), intern_length(x, id));
        const uint32_t primed = found != 0 && found < other->primed_end;
        missing += found == 0;
        names[primed] += found != 0;
        moved[primed] += found != 0 && found != id;
    }
    const bool result = missing == 0 && y->count == x->count + other->padding && (names[0] == 0 || moved[0] != 0) && (names[1] == 0 || moved[1] != 0);
    if (!result)
    {
        printf("%s: on %u lanes with the ids in another order, %u of the first run's %u names are missing, %u ids against %u and %u names that no file has, and "
               "%u of %u primed names and %u of %u others changed ids\n",
               work->name, other->lanes, missing, x->count - C_NAME_COUNT, y->count, x->count, other->padding, moved[1], names[1], moved[0], names[0]);
    }
    *changed = moved[0] + moved[1];
    return result;
}

// Each of the work's units with a cache of its own, against the same unit with the reference run's cache: the same
// spellings, errors, warnings and diagnostics.
static bool files_test_alone(const FilesTestWork* work, const FilesTestRun* reference)
{
    static Preprocessor alone;
    static Preprocessor cached;
    static InternTable alone_table;
    bool result = true;
    for (uint32_t u = 0; u < work->unit_count; u += 1)
    {
        char* texts[2] = { 0 };
        size_t lengths[2] = { 0 };
        FILE* streams[2] = { open_memstream(&texts[0], &lengths[0]), open_memstream(&texts[1], &lengths[1]) };
        PPOptions options = work->options;
        options.files = 0;
        options.diagnostics = streams[0];
        bool same = streams[0] != 0 && streams[1] != 0 && intern_initialize(&alone_table) && pp_initialize(&alone, &options, &alone_table);
        const bool alone_ran = same && pp_run(&alone, work->units[u]);
        options.files = (FileCache*)&reference->cache;
        options.diagnostics = streams[1];
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
        for (uint32_t k = 0; k < 2; k += 1)
        {
            same = streams[k] != 0 && fclose(streams[k]) == 0 && same;
        }
        same = same && files_test_same_text(work, work->units[u], "diagnostics", "from the unit run alone", texts[0], lengths[0], texts[1], lengths[1]);
        free(texts[0]);
        free(texts[1]);
        result = result && same;
    }
    return result;
}

// A run after the first must have written nothing to stderr and not run out of memory.
static bool files_test_quiet(const FilesTestWork* work, const FilesTestRun* other, const char* how)
{
    if (getenv("BUSTER_FILES_TEST_VERBOSE") != 0)
    {
        printf("%s: %u lanes%s: %lu candidates exchanged, %lu found in the shared table\n", work->name, other->lanes, how, (unsigned long)other->candidates,
               (unsigned long)other->shared_found);
    }
    const bool result = other->stderr_bytes == 0 && !other->failed;
    if (!result)
    {
        printf("%s: on %u lanes%s the walk %s\n", work->name, other->lanes, how, other->failed ? "ran out of memory" : "wrote to stderr");
    }
    return result;
}

// #153's check of the ids' order: the work on every lane count with the shared table primed by files_test_prime, a seed
// per lane count from `id_order` on, and every lane preparing every round (as the tree's rounds are prepared), so that on
// more than one lane the merge gives the walk's new spellings their ids after the primed ones. Each run must give what
// the first run (`reference`, whose table gives the names) gives, but for the ids.
static bool files_test_order(const FilesTestWork* work, const FilesTestRun* reference, const uint32_t* lanes, uint32_t lane_count, const char* directory,
                             uint64_t id_order)
{
    FilesTestWork every = *work;
    every.thresholds = true;
    every.lane_bytes = 0;
    every.split_bytes = work->thresholds ? work->split_bytes : BUSTER_FILES_SPLIT_BYTES;
    uint32_t fewest = UINT32_MAX;
    bool result = true;
    for (uint32_t l = 0; result && l < lane_count; l += 1)
    {
        const FilesTestOrder order = { .names = &reference->table, .seed = id_order + l };
        FilesTestRun other;
        uint32_t changed = 0;
        result = files_test_run(&every, lanes[l], directory, &order, &other) && files_test_same_names(work, reference, &other, &changed) &&
                 files_test_same(work, reference, &other, true);
        result = files_test_quiet(work, &other, " with the ids in another order") && result;
        fewest = changed < fewest ? changed : fewest;
        files_test_release_run(&other);
    }
    if (result)
    {
        printf("%s, with the ids in another order: the same -E text, diagnostics, locations and records on every lane count, at least %u of %u names with other ids "
               "(seeds %llu to %llu)\n",
               work->name, fewest, reference->table.count - C_NAME_COUNT, (unsigned long long)id_order, (unsigned long long)(id_order + lane_count - 1));
    }
    return result;
}

// Runs a work on every lane count, compares each run with the first's, and the units with themselves run alone; then
// again with the ids in another order (files_test_order). Keeps the first run in `reference` for the caller's own checks
// (it releases it).
static bool files_test_work(const FilesTestWork* work, const uint32_t* lanes, uint32_t lane_count, const char* directory, uint64_t id_order,
                            FilesTestRun* reference)
{
    bool result = files_test_run(work, lanes[0], directory, 0, reference);
    for (uint32_t l = 1; result && l < lane_count; l += 1)
    {
        FilesTestRun other;
        result = files_test_run(work, lanes[l], directory, 0, &other) && files_test_same(work, reference, &other, false);
        result = files_test_quiet(work, &other, "") && result;
        files_test_release_run(&other);
    }
    // Before files_test_alone, which preprocesses the units with the first run's table again: the order's names are those
    // the first run's walk and units interned.
    result = result && files_test_order(work, reference, lanes, lane_count, directory, id_order);
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
    // The units' diagnostics, which files_test_run compared, go to memory unread.
    char* text = 0;
    size_t length = 0;
    FILE* diagnostics = open_memstream(&text, &length);
    PPOptions options = work->options;
    options.files = &reference->cache;
    options.diagnostics = diagnostics;
    for (uint32_t u = 0; used != 0 && diagnostics != 0 && u < work->unit_count; u += 1)
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
    if (diagnostics != 0)
    {
        fclose(diagnostics);
    }
    free(text);
    free(used);
    return result;
}

// The tree's files, relative to its directory, and its directories (made first, removed last). The warnings name
// identifiers by their ids: b.h's #if, in main1 and main2, has notes naming the macros it was expanded from, and main2
// redefines NEXT2.
static const char* const files_test_tree_directories[] = { "sub", "I1", "I2", "I3", "sys" };
static const char* const files_test_tree_files[][2] = {
    { "main1.c", "#include \"a.h\"\n#include <sys1.h>\n#include \"sub/b.h\"\n#if 0\n#include \"missing.h\"\n#include \"unused.h\"\nR\"bad space\"\n#endif\n#define COMPUTED \"c.h\"\n"
                 "#include COMPUTED\n#include \"link.h\"\n#include \"hard.h\"\n#include \"once.h\"\n#include \"once.h\"\n#include \"guarded.h\"\n#include \"guarded.h\"\n"
                 "int main1 = A + B + SYS1 + C + ONCE + GUARDED;\n" },
    { "main2.c", "#include \"sub/../a.h\"\n#include <next.h>\n#include \"sub/b.h\"\n#define NEXT2 10\nint main2 = A + B + NEXT + NEXT2;\n" },
    { "main3.c", "#include \"a.h\"\n#include \"nope.h\"\nint main3;\n" },
    { "a.h", "#ifndef A_H\n#define A_H\n#define A 1\n#endif\n" },
    { "sub/b.h", "#include \"../a.h\"\n#define B 2\n#define BIG 18446744073709551615\n#define WIDE BIG\n#if WIDE\n#endif\n" },
    { "c.h", "#define C 3\n" },
    { "unused.h", "#define UNUSED 4\nR\"bad space\"\n" },
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

// The tree's check: its units, on every lane count and with the ids in another order, and its exact counts. Its rounds
// are small, so that lane 0 would prepare them alone: they are prepared on every lane instead, cut into parts (`split`
// false) or one range per lane.
static bool files_test_tree(const char* directory, const uint32_t* lanes, uint32_t lane_count, bool vectorized, bool split, uint64_t id_order)
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
    result = result && files_test_work(&work, lanes, lane_count, directory, id_order, &reference);
    if (result)
    {
        // 13 files: the units, then a.h (reached as a.h, sub/../a.h and link.h), sys1.h, b.h, unused.h, hard.h (c.h,
        // which main1's computed include names), once.h, guarded.h and I1's next.h, then I2's, then I3's.
        const uint32_t unused = files_test_unused(&work, &reference);
        const bool counts = reference.prepared == 13 && reference.rounds == 4 && reference.absent == 2 && unused == 1;
        // After the walk, main1 opens only c.h (its computed include), and the others open nothing; main3 reports nope.h,
        // main1 and main2 warn in b.h, and main2 about NEXT2.
        const FilesTestUnit* results = reference.units;
        const bool units_right = results[0].errors == 0 && results[0].warnings == 1 && results[0].probes.opens == 1 && results[1].errors == 0 &&
                                 results[1].warnings == 2 && results[1].probes.opens == 0 && results[2].errors == 1 && results[2].warnings == 0 &&
                                 results[2].probes.opens == 0;
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
            printf("%s: the units' errors %u, %u, %u (0, 0, 1), warnings %u, %u, %u (1, 2, 0) and opens %lu, %lu, %lu (1, 0, 0)\n", work.name, results[0].errors,
                   results[1].errors, results[2].errors, results[0].warnings, results[1].warnings, results[2].warnings, (unsigned long)results[0].probes.opens,
                   (unsigned long)results[1].probes.opens, (unsigned long)results[2].probes.opens);
        }
        if (result)
        {
            printf("%s: 13 files prepared once each in 4 rounds, the same on every lane count; 1 unused, 2 not found without a diagnostic\n", work.name);
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

// What would hang if it regressed (a probe that blocks on a FIFO, a walk whose paths never end) runs under an alarm,
// whose watchdog ends the test with status 1.
static void files_test_watchdog(int signal_number)
{
    (void)signal_number;
    static const char message[] = "test-files: a check didn't finish within 60 s (a probe blocked, or the walk didn't end)\n";
    const ssize_t written = write(1, message, sizeof(message) - 1);
    (void)written;
    _exit(1);
}

// The contexts' check: a guarded header that includes itself through ./ and through sub/.. (each path a new spelling
// of one file in one directory), which the walk must take once rather than make paths of without end; and one header
// in two directories (a hard link) whose own local.h differ, which it must walk in both.
static bool files_test_contexts(const char* directory, const uint32_t* lanes, uint32_t lane_count, uint64_t id_order)
{
    static const char* const directories[] = { "sub", "one", "two" };
    static const char* const files[][2] = {
        { "contexts.c", "#include \"cycle.h\"\n#include \"one/h.h\"\n#include \"two/h.h\"\nint contexts = CYCLE + ONE + TWO;\n" },
        { "cycle.h", "#ifndef CYCLE_H\n#define CYCLE_H\n#include \"./cycle.h\"\n#include \"sub/../cycle.h\"\n#define CYCLE 1\n#endif\n" },
        { "one/h.h", "#include \"local.h\"\n" },
        { "one/local.h", "#define ONE 2\n" },
        { "two/local.h", "#define TWO 3\n" },
    };
    char path[4200];
    char target[4200];
    bool result = true;
    for (uint32_t i = 0; result && i < BUSTER_ARRAY_LENGTH(directories); i += 1)
    {
        snprintf(path, sizeof(path), "%s/%s", directory, directories[i]);
        result = mkdir(path, 0700) == 0;
    }
    for (uint32_t i = 0; result && i < BUSTER_ARRAY_LENGTH(files); i += 1)
    {
        snprintf(path, sizeof(path), "%s/%s", directory, files[i][0]);
        result = files_test_write(path, files[i][1]);
    }
    snprintf(target, sizeof(target), "%s/one/h.h", directory);
    snprintf(path, sizeof(path), "%s/two/h.h", directory);
    result = result && link(target, path) == 0;
    char unit[4200];
    snprintf(unit, sizeof(unit), "%s/contexts.c", directory);
    const char* units[1] = { unit };
    FilesTestWork work = {
        .name = "contexts",
        .options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true },
        .roots = units,
        .root_count = 1,
        .follow = true,
        .units = units,
        .unit_count = 1,
    };
    FilesTestRun reference = { 0 };
    alarm(60);
    result = result && files_test_work(&work, lanes, lane_count, directory, id_order, &reference);
    alarm(0);
    if (result)
    {
        // 5 files in 3 rounds: the unit; cycle.h (by three spellings, walked once) and h.h; the two local.h. After the
        // walk the unit opens nothing.
        const bool counts = reference.prepared == 5 && reference.rounds == 3 && reference.absent == 0 && reference.units[0].errors == 0 &&
                            reference.units[0].probes.opens == 0;
        if (!counts)
        {
            printf("contexts: %lu files prepared (5) in %u rounds (3), %lu not found (0), the unit's errors %u (0) and opens %lu (0)\n",
                   (unsigned long)reference.prepared, reference.rounds, (unsigned long)reference.absent, reference.units[0].errors,
                   (unsigned long)reference.units[0].probes.opens);
        }
        else
        {
            printf("contexts: a header that includes itself through ./ and sub/.. walked once, and one in two directories in both\n");
        }
        result = counts;
    }
    files_test_release_run(&reference);
    unlink(path);
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(files); i += 1)
    {
        snprintf(path, sizeof(path), "%s/%s", directory, files[BUSTER_ARRAY_LENGTH(files) - 1 - i][0]);
        unlink(path);
    }
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(directories); i += 1)
    {
        snprintf(path, sizeof(path), "%s/%s", directory, directories[i]);
        rmdir(path);
    }
    return result;
}

// '$' as punctuation (-fno-dollars-in-identifiers, #281) in a cache that units share: the files are prepared with the
// options' mode on every lane count, and the unit splits `aa$bb` into `aa`, `$` and `BB` as alone.
static bool files_test_dollars(const char* directory, const uint32_t* lanes, uint32_t lane_count, uint64_t id_order)
{
    static const char* const files[][2] = {
        { "dollars.c", "#include \"dollars.h\"\nint cc$bb = 1;\n" },
        { "dollars.h", "#define bb BB\nint aa$bb;\n" },
    };
    char path[4200];
    bool result = true;
    for (uint32_t i = 0; result && i < BUSTER_ARRAY_LENGTH(files); i += 1)
    {
        snprintf(path, sizeof(path), "%s/%s", directory, files[i][0]);
        result = files_test_write(path, files[i][1]);
    }
    char unit[4200];
    snprintf(unit, sizeof(unit), "%s/dollars.c", directory);
    const char* units[1] = { unit };
    FilesTestWork work = {
        .name = "dollars",
        .options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true, .dollars = PP_DOLLARS_PUNCTUATION },
        .roots = units,
        .root_count = 1,
        .follow = true,
        .units = units,
        .unit_count = 1,
    };
    FilesTestRun reference = { 0 };
    alarm(60);
    result = result && files_test_work(&work, lanes, lane_count, directory, id_order, &reference);
    alarm(0);
    if (result)
    {
        // `int aa $ BB ;` and `int cc $ BB = 1 ;`.
        result = reference.units[0].count == 12 && reference.units[0].errors == 0;
        printf("dollars: %s\n", result ? "'$' as punctuation in a shared cache, on every lane count" : "the unit's tokens aren't 12 without errors");
    }
    files_test_release_run(&reference);
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(files); i += 1)
    {
        snprintf(path, sizeof(path), "%s/%s", directory, files[i][0]);
        unlink(path);
    }
    return result;
}

// The rounds check's chain of headers, and its roots' first headers: walks of BUSTER_FILES_TRACE_ROUNDS rounds and of
// more, a round per file.
#define BUSTER_FILES_TEST_CHAIN 20
#define BUSTER_FILES_TEST_CHAIN_ROOTS 2

// One walk of `root` on `lanes` lanes that keeps its trace (FilesRun.trace) in `trace`, with the rounds it made and the
// files it prepared.
static bool files_test_traced_walk(const char* root, uint32_t lanes, const char* directory, FilesTrace* trace, uint32_t* rounds, uint64_t* prepared)
{
    static Preprocessor search;
    InternTable table = { 0 };
    FileCache cache = { 0 };
    PPOptions options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    bool result = intern_initialize(&table) && file_cache_initialize(&cache, &table, pp_prepare_options(&options));
    options.files = &cache;
    result = result && pp_initialize(&search, &options, &table);
    const char* roots[1] = { root };
    FilesRun run = { 0 };
    result = result && files_run_initialize(&run, &cache, &search, roots, 1, true, lanes);
    *trace = (FilesTrace){ 0 };
    run.trace = trace;
    char path[4200];
    const int saved = files_test_quiet_begin(directory, path, sizeof(path));
    result = result && lane_run(lanes, files_run_lane, &run, 0) && !run.failed;
    const uint64_t written = files_test_quiet_end(saved, path);
    *rounds = run.rounds;
    if (run.cache != 0)
    {
        files_run_release(&run);
    }
    *prepared = cache.prepared_records;
    pp_release(&search);
    file_cache_release(&cache);
    intern_release(&table);
    return result && written == 0;
}

// Is a trace of a walk of `rounds` rounds, a file each, on `lanes` lanes what it must be (FilesTrace)? It keeps
// min(rounds, BUSTER_FILES_TRACE_ROUNDS) rounds and the walk's count, each a file on one working lane but the last kept
// one, which counts every later round's file too; and its snapshots from the walk's start to its end, the one after the
// last kept round's, go forward on every traced lane: its TSC and each phase's ticks, and the waves and probes. Prints
// what isn't.
static bool files_test_trace(const FilesTrace* trace, uint32_t rounds, uint32_t lanes, const char* root)
{
    const uint32_t kept = rounds < BUSTER_FILES_TRACE_ROUNDS ? rounds : BUSTER_FILES_TRACE_ROUNDS;
    const uint32_t traced = lanes < BUSTER_FILES_TRACE_LANES ? lanes : BUSTER_FILES_TRACE_LANES;
    bool result = trace->rounds == kept && trace->walked == rounds;
    if (!result)
    {
        printf("rounds: %s on %u lanes: the trace keeps %u rounds (%u) of %u (%u)\n", root, lanes, trace->rounds, kept, trace->walked, rounds);
    }
    for (uint32_t r = 0; result && r < kept; r += 1)
    {
        const uint32_t files = r + 1 < kept ? 1 : rounds - r;
        result = trace->files[r] == files && trace->bytes[r] != 0 && trace->working[r] == 1;
        if (!result)
        {
            printf("rounds: %s on %u lanes: round %u has %u files (%u), %lu bytes and %u working lanes (1)\n", root, lanes, r, trace->files[r], files,
                   (unsigned long)trace->bytes[r], trace->working[r]);
        }
    }
    for (uint32_t at = 0; result && at <= kept; at += 1)
    {
        result = trace->waves[at + 1] >= trace->waves[at] && trace->probes[at + 1] >= trace->probes[at];
        if (!result)
        {
            printf("rounds: %s on %u lanes: the waves or the probes go back from snapshot %u to %u\n", root, lanes, at, at + 1);
        }
        for (uint32_t l = 0; result && l < traced; l += 1)
        {
            result = trace->clock[at][l] != 0 && trace->clock[at + 1][l] >= trace->clock[at][l];
            for (uint32_t p = 0; result && p < FILES_PHASE_COUNT; p += 1)
            {
                result = trace->ticks[at + 1][l][p] >= trace->ticks[at][l][p];
            }
            if (!result)
            {
                printf("rounds: %s on %u lanes: lane %u's snapshot %u doesn't go on to snapshot %u\n", root, lanes, l, at, at + 1);
            }
        }
    }
    return result;
}

// The walk's trace (FilesRun.trace, benchmark-files --rounds) on include chains of exactly BUSTER_FILES_TRACE_ROUNDS
// rounds and of more (#422's review): headers h1.h to h20.h, each but the last including the next, and two roots that
// include h6.h and h1.h, whose walks take a round per file, 16 and 21, on every lane count (files_test_trace).
static bool files_test_rounds(const char* directory, const uint32_t* lanes, uint32_t lane_count)
{
    static const uint32_t firsts[BUSTER_FILES_TEST_CHAIN_ROOTS] = { BUSTER_FILES_TEST_CHAIN + 2 - BUSTER_FILES_TRACE_ROUNDS, 1 };
    static FilesTrace trace;
    char path[4200];
    char text[64];
    bool result = true;
    for (uint32_t h = 1; result && h <= BUSTER_FILES_TEST_CHAIN; h += 1)
    {
        if (h < BUSTER_FILES_TEST_CHAIN)
        {
            snprintf(text, sizeof(text), "#include \"h%u.h\"\nint h%u;\n", h + 1, h);
        }
        else
        {
            snprintf(text, sizeof(text), "int h%u;\n", h);
        }
        snprintf(path, sizeof(path), "%s/h%u.h", directory, h);
        result = files_test_write(path, text);
    }
    char roots[BUSTER_FILES_TEST_CHAIN_ROOTS][4200];
    for (uint32_t i = 0; result && i < BUSTER_FILES_TEST_CHAIN_ROOTS; i += 1)
    {
        snprintf(roots[i], sizeof(roots[i]), "%s/rounds%u.c", directory, i);
        snprintf(text, sizeof(text), "#include \"h%u.h\"\n", firsts[i]);
        result = files_test_write(roots[i], text);
    }
    for (uint32_t i = 0; result && i < BUSTER_FILES_TEST_CHAIN_ROOTS; i += 1)
    {
        // The root and the headers from its first on.
        const uint32_t rounds = BUSTER_FILES_TEST_CHAIN - firsts[i] + 2;
        for (uint32_t l = 0; result && l < lane_count; l += 1)
        {
            uint32_t walked = 0;
            uint64_t prepared = 0;
            result = files_test_traced_walk(roots[i], lanes[l], directory, &trace, &walked, &prepared);
            if (result && (walked != rounds || prepared != rounds))
            {
                printf("rounds: %s on %u lanes: %u rounds (%u) and %lu files prepared (%u)\n", roots[i], lanes[l], walked, rounds, (unsigned long)prepared, rounds);
                result = false;
            }
            else if (!result)
            {
                printf("rounds: %s on %u lanes: the walk failed or wrote to stderr\n", roots[i], lanes[l]);
            }
            result = result && files_test_trace(&trace, rounds, lanes[l], roots[i]);
        }
    }
    if (result)
    {
        printf("rounds: walks of %u and %u rounds traced on every lane count, the last kept round with the later ones, to the walk's end\n",
               BUSTER_FILES_TEST_CHAIN - firsts[0] + 2, BUSTER_FILES_TEST_CHAIN - firsts[1] + 2);
    }
    for (uint32_t i = 0; i < BUSTER_FILES_TEST_CHAIN_ROOTS; i += 1)
    {
        snprintf(path, sizeof(path), "%s/rounds%u.c", directory, i);
        unlink(path);
    }
    for (uint32_t h = 1; h <= BUSTER_FILES_TEST_CHAIN; h += 1)
    {
        snprintf(path, sizeof(path), "%s/h%u.h", directory, h);
        unlink(path);
    }
    return result;
}

#define BUSTER_FILES_TEST_DEEP_LEVELS 10
#define BUSTER_FILES_TEST_DEEP_NAME 250
#define BUSTER_FILES_TEST_LONG_NAME 2400

// The special paths' check: a FIFO, which an #include under #if 0 names (the walk mustn't block opening it, and leaves
// it to the unit, which never reaches it) and an active one names (an error, as the unit gives alone); and a quoted
// name of 2,400 bytes under #if 0, twice in a file and once in another of the same directory, which is more than 2,500
// bytes long: the three requests' key holds both whole, and they must share one search.
static bool files_test_special(const char* directory, const uint32_t* lanes, uint32_t lane_count, uint64_t id_order)
{
    char path[4200];
    char deep[4200];
    char units_paths[4][4200];
    bool result = true;
    uint32_t length = (uint32_t)snprintf(deep, sizeof(deep), "%s", directory);
    for (uint32_t level = 0; result && level < BUSTER_FILES_TEST_DEEP_LEVELS; level += 1)
    {
        deep[length] = '/';
        memset(deep + length + 1, 'd', BUSTER_FILES_TEST_DEEP_NAME);
        length += 1 + BUSTER_FILES_TEST_DEEP_NAME;
        deep[length] = 0;
        result = mkdir(deep, 0700) == 0;
    }
    // The long name, twice in long.c and once in long2.c.
    const uint32_t capacity = 2 * BUSTER_FILES_TEST_LONG_NAME + 128;
    char* name = malloc(BUSTER_FILES_TEST_LONG_NAME + 1);
    char* text = malloc(capacity);
    char* text2 = malloc(capacity);
    result = result && name != 0 && text != 0 && text2 != 0;
    if (result)
    {
        memset(name, 'n', BUSTER_FILES_TEST_LONG_NAME);
        name[BUSTER_FILES_TEST_LONG_NAME] = 0;
        snprintf(text, capacity, "#if 0\n#include \"%s\"\n#include \"%s\"\n#endif\nint special_long;\n", name, name);
        snprintf(text2, capacity, "#if 0\n#include \"%s\"\n#endif\nint special_long2;\n", name);
    }
    snprintf(units_paths[0], sizeof(units_paths[0]), "%s/fifo.c", directory);
    snprintf(units_paths[1], sizeof(units_paths[1]), "%s/fifo2.c", directory);
    snprintf(units_paths[2], sizeof(units_paths[2]), "%s/long.c", deep);
    snprintf(units_paths[3], sizeof(units_paths[3]), "%s/long2.c", deep);
    snprintf(path, sizeof(path), "%s/never.h", directory);
    result = result && mkfifo(path, 0600) == 0 && files_test_write(units_paths[0], "#if 0\n#include \"never.h\"\n#endif\nint special_fifo;\n") &&
             files_test_write(units_paths[1], "#include \"never.h\"\nint special_fifo2;\n") && files_test_write(units_paths[2], text) &&
             files_test_write(units_paths[3], text2);
    const char* units[4] = { units_paths[0], units_paths[1], units_paths[2], units_paths[3] };
    FilesTestWork work = {
        .name = "special paths",
        .options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true },
        .roots = units,
        .root_count = 4,
        .follow = true,
        .units = units,
        .unit_count = 4,
    };
    FilesTestRun reference = { 0 };
    alarm(60);
    result = result && files_test_work(&work, lanes, lane_count, directory, id_order, &reference);
    alarm(0);
    if (result)
    {
        // The units alone; 9 requests (the 4 roots, never.h twice, the long name three times) in 6 searches (the roots',
        // never.h's and the long name's), none of never.h's and the long name's found; and fifo2.c's include an error.
        const bool counts = reference.prepared == 4 && reference.requests == 9 && reference.searches == 6 && reference.absent == 5 &&
                            reference.units[0].errors == 0 && reference.units[1].errors == 1 && reference.units[2].errors == 0 && reference.units[3].errors == 0;
        if (!counts)
        {
            printf("special paths: %lu files prepared (4), %lu requests (9) in %lu searches (6), %lu not found (5), the units' errors %u, %u, %u, %u (0, 1, 0, 0)\n",
                   (unsigned long)reference.prepared, (unsigned long)reference.requests, (unsigned long)reference.searches, (unsigned long)reference.absent,
                   reference.units[0].errors, reference.units[1].errors, reference.units[2].errors, reference.units[3].errors);
        }
        else
        {
            printf("special paths: a FIFO under #if 0 and active, and a name of %u bytes in a directory of %u, as each unit alone\n", BUSTER_FILES_TEST_LONG_NAME,
                   length);
        }
        result = counts;
    }
    files_test_release_run(&reference);
    for (uint32_t u = 0; u < 4; u += 1)
    {
        unlink(units_paths[u]);
    }
    unlink(path);
    for (uint32_t level = 0; level < BUSTER_FILES_TEST_DEEP_LEVELS && length > (uint32_t)strlen(directory); level += 1)
    {
        rmdir(deep);
        length -= 1 + BUSTER_FILES_TEST_DEEP_NAME;
        deep[length] = 0;
    }
    free(name);
    free(text);
    free(text2);
    return result;
}

// A run in which lane `lane` (BUSTER_PP_NONE for none) has only `scratch` bytes of scratch memory.
typedef struct FilesTestShort FilesTestShort;
struct FilesTestShort
{
    FilesRun* run;
    uint32_t lane;
    uint64_t scratch;
};

static void files_test_short_lane(void* argument)
{
    FilesTestShort* test = argument;
    const bool shortened = lane_index() == test->lane;
    if (shortened)
    {
        arena_release(&thread_context.scratch[0]);
        thread_context.scratch[0] = arena_reserve(test->scratch);
    }
    files_run_ahead(test->run);
    if (shortened)
    {
        // The thread's next scratch_begin reserves its usual arena again.
        arena_release(&thread_context.scratch[0]);
    }
}

#define BUSTER_FILES_TEST_MEMORY_FILES 4

// The memory check: runs on 2 and 3 lanes that all work, short of the round's arena at sizes up to what the merge
// needs, or of one lane's scratch memory (the first's, then the last's): a run may fail, but every unit must give what it
// gives alone, since what the run couldn't finish is left unprepared for the unit to prepare.
static bool files_test_memory(const char* directory, uint64_t seed)
{
    static const uint32_t lane_counts[] = { 2, 3 };
    static Preprocessor alone;
    static Preprocessor cached;
    static InternTable alone_table;
    char paths[BUSTER_FILES_TEST_MEMORY_FILES][4200];
    const char* units[BUSTER_FILES_TEST_MEMORY_FILES];
    PPSpellings alone_spellings[BUSTER_FILES_TEST_MEMORY_FILES] = { 0 };
    uint32_t alone_errors[BUSTER_FILES_TEST_MEMORY_FILES] = { 0 };
    char* text = malloc(1u << 16);
    uint64_t state = seed;
    bool result = text != 0;
    for (uint32_t f = 0; result && f < BUSTER_FILES_TEST_MEMORY_FILES; f += 1)
    {
        // Names of the file's own, and of a vocabulary the files share.
        uint32_t length = 0;
        for (uint32_t i = 0; i < 100; i += 1)
        {
            char name[32];
            files_test_identifier(&state, name, 3 + (uint32_t)(files_test_random(&state) % 10));
            length += (uint32_t)snprintf(text + length, (1u << 16) - length, "int %s%s_%u;\n", files_test_random(&state) % 2 == 0 ? "shared_" : "own_", name,
                                         files_test_random(&state) % 2 == 0 ? 0 : f);
        }
        snprintf(paths[f], sizeof(paths[f]), "%s/memory%u.c", directory, f);
        units[f] = paths[f];
        result = files_test_write(paths[f], text);
    }
    const PPOptions base = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    // Each unit alone, once.
    for (uint32_t u = 0; result && u < BUSTER_FILES_TEST_MEMORY_FILES; u += 1)
    {
        result = intern_initialize(&alone_table) && pp_initialize(&alone, &base, &alone_table) && pp_run(&alone, units[u]) &&
                 pp_stream_spellings(&alone, &alone_spellings[u]);
        alone_errors[u] = alone.errors;
        pp_release(&alone);
        intern_release(&alone_table);
    }
    uint32_t runs = 0;
    uint32_t failed = 0;
    for (uint32_t c = 0; result && c < BUSTER_ARRAY_LENGTH(lane_counts); c += 1)
    {
        const uint32_t lanes = lane_counts[c];
        // Way 0: the round's arena, from 64 bytes to 32 KB, half again each time (the walk's arrays, then the merge's,
        // fail at some of them); ways 1 and 2: the first or the last lane's scratch memory, from 8 bytes to 512, four
        // times as much each time (a file's line words, then a partition's table of first occurrences, fail).
        for (uint32_t way = 0; result && way < 3; way += 1)
        {
            const uint64_t first = way == 0 ? 64 : 8;
            const uint64_t last = way == 0 ? 32768 : 512;
            for (uint64_t size = first; result && size <= last; size += way == 0 ? size / 2 : 3 * size)
            {
                static Preprocessor search;
                InternTable table;
                FileCache cache;
                PPOptions options = base;
                result = intern_initialize(&table) && file_cache_initialize(&cache, &table, pp_prepare_options(&options));
                options.files = &cache;
                result = result && pp_initialize(&search, &options, &table);
                FilesRun files;
                result = result && files_run_initialize(&files, &cache, &search, units, BUSTER_FILES_TEST_MEMORY_FILES, true, lanes);
                if (result)
                {
                    files.lane_bytes = 0;
                    files.split_bytes = 0;
                    if (way == 0)
                    {
                        arena_release(&files.round);
                        files.round = arena_reserve(size);
                    }
                    FilesTestShort test = { .run = &files, .lane = way == 1 ? 0 : way == 2 ? lanes - 1 : BUSTER_PP_NONE, .scratch = size };
                    alarm(60);
                    result = lane_run(lanes, files_test_short_lane, &test, 0);
                    alarm(0);
                    failed += files.failed;
                    runs += 1;
                    files_run_release(&files);
                }
                pp_release(&search);
                // Each unit with what the run left in the cache, against the unit alone.
                for (uint32_t u = 0; result && u < BUSTER_FILES_TEST_MEMORY_FILES; u += 1)
                {
                    PPSpellings ours = { 0 };
                    result = pp_initialize(&cached, &options, &table) && pp_run(&cached, units[u]) && cached.errors == alone_errors[u] &&
                             pp_stream_spellings(&cached, &ours) && pp_compare_spellings(&cached, &ours, &alone_spellings[u], units[u], "alone");
                    arena_release(&ours.arena);
                    pp_release(&cached);
                }
                if (!result)
                {
                    printf("memory: on %u lanes, with %lu bytes of %s\n", lanes, (unsigned long)size,
                           way == 0 ? "the round's arena" : way == 1 ? "lane 0's scratch memory" : "the last lane's scratch memory");
                }
                file_cache_release(&cache);
                intern_release(&table);
            }
        }
    }
    // The sizes must reach both: runs that fail, and runs with room enough.
    const bool both = failed != 0 && failed < runs;
    if (result && !both)
    {
        printf("memory: %u of %u runs failed: the sizes don't reach both outcomes\n", failed, runs);
    }
    result = result && both;
    if (result)
    {
        printf("memory: %u runs short of the round's arena or a lane's scratch memory, %u of them failed; every unit as alone\n", runs, failed);
    }
    for (uint32_t f = 0; f < BUSTER_FILES_TEST_MEMORY_FILES; f += 1)
    {
        unlink(paths[f]);
        arena_release(&alone_spellings[f].arena);
    }
    free(text);
    return result;
}

#define BUSTER_FILES_TEST_SPELLING_FILES 240
#define BUSTER_FILES_TEST_VOCABULARY 30000

// The spellings' check: generated files of identifiers, on every lane count without following includes, then in two
// runs into one cache. A translation unit includes every file, so that it reads them all from the cache.
static bool files_test_spellings(const char* directory, const uint32_t* lanes, uint32_t lane_count, uint64_t seed, uint64_t id_order)
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
    char unit[4200];
    snprintf(unit, sizeof(unit), "%s/spellings.c", directory);
    uint64_t includes = 0;
    for (uint32_t f = 0; result && f < BUSTER_FILES_TEST_SPELLING_FILES; f += 1)
    {
        includes += (uint64_t)snprintf(text + includes, (1u << 20) - includes, "#include \"s%u.c\"\n", f);
    }
    result = result && files_test_write(unit, text);
    const char* units[1] = { unit };
    FilesTestWork work = {
        .name = "spellings",
        .options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true },
        .roots = roots,
        .root_count = BUSTER_FILES_TEST_SPELLING_FILES,
        .follow = false,
        .units = units,
        .unit_count = 1,
    };
    FilesTestRun reference = { 0 };
    result = result && files_test_work(&work, lanes, lane_count, directory, id_order, &reference);
    // And with one range per lane, then with two lanes working and the others idle, taking parts from the run's
    // counter and with one range each, on every lane count, against the same reference.
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
    FilesTestWork two_once = two;
    two_once.name = "spellings, two lanes working, split once";
    two_once.split_bytes = UINT64_MAX;
    for (uint32_t l = 1; result && l < lane_count; l += 1)
    {
        FilesTestRun other;
        result = files_test_run(&once, lanes[l], directory, 0, &other) && files_test_same(&once, &reference, &other, false);
        files_test_release_run(&other);
        result = result && files_test_run(&two, lanes[l], directory, 0, &other) && files_test_same(&two, &reference, &other, false);
        files_test_release_run(&other);
        result = result && files_test_run(&two_once, lanes[l], directory, 0, &other) && files_test_same(&two_once, &reference, &other, false);
        files_test_release_run(&other);
    }
    const bool grew = result && reference.ids > 40960;
    if (result)
    {
        printf("spellings: %u files, %u ids, %lu tokens, the same on every lane count, in parts, split once and on two lanes (in parts and split once)\n",
               BUSTER_FILES_TEST_SPELLING_FILES, reference.ids, (unsigned long)reference.tokens);
    }
    if (result && !grew)
    {
        printf("spellings: only %u ids, too few for the shared table to grow in a merge\n", reference.ids);
    }
    result = result && grew;
    // Two runs into one cache, on each lane count, against the reference (one run on the first lane count).
    FilesTestWork split = work;
    split.name = "spellings in two runs";
    split.split = BUSTER_FILES_TEST_SPELLING_FILES / 2;
    for (uint32_t l = 0; result && l < lane_count; l += 1)
    {
        FilesTestRun other;
        result = files_test_run(&split, lanes[l], directory, 0, &other);
        // The two runs' walks add up to the one's but in their rounds; the tables and records must be the same.
        other.rounds = reference.rounds;
        other.requests = reference.requests;
        other.searches = reference.searches;
        result = result && files_test_same(&split, &reference, &other, false);
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
    unlink(unit);
    free(vocabulary);
    free(paths);
    free(roots);
    free(text);
    return result;
}

// The files' check: each a unit with the options, all the walk's roots; or with `flat`, only prepared.
static bool files_test_files(const PPOptions* options, const char* const* files, uint32_t count, bool flat, const char* directory, const uint32_t* lanes,
                             uint32_t lane_count, uint64_t id_order)
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
    bool result = files_test_work(&work, lanes, lane_count, directory, id_order, &reference);
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
    uint64_t id_order = 153;
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
                                 pp_own_option_with_value(argument) || strcmp(argument, "-x") == 0 || strcmp(argument, "-target") == 0;
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
        else if (strcmp(argument, "--id-order") == 0 && i + 1 < argument_count)
        {
            char* end;
            errno = 0;
            i += 1;
            id_order = strtoull(arguments[i], &end, 10);
            valid = errno == 0 && end != arguments[i] && *end == 0 && arguments[i][0] != '-';
        }
        else if (strcmp(argument, "--id-order") == 0)
        {
            valid = false;
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
        printf("usage: test-files [--lanes LIST] [--seed N] [--id-order N] [--keep] [--flat] [options] [path...]\n");
    }
    Arena options_arena = { 0 };
    PPOptions options = { 0 };
    if (passed_all && file_count != 0)
    {
        options_arena = arena_reserve(1ull << 30);
        const char* input;
        const char* output;
        bool markers;
        // Validate the corpus options before starting workers. pp_parse_options wants one input: give it the first
        // file, and use its options for every file.
        passed[passed_count] = (char*)files[0];
        passed_all = pp_parse_options(passed_count + 1, passed, &options, &options_arena, &input, &output, &markers);
    }
    // The built-in short-read cases also use two and three lanes. Keep those workers and every requested lane alive
    // across the walks, so the existing scalar/corpus/determinism checks exercise command-owned thread reuse.
    uint32_t capacity = file_count == 0 ? 3 : 1;
    for (uint32_t l = 0; valid && l < lane_count; l += 1)
    {
        capacity = lanes[l] > capacity ? lanes[l] : capacity;
    }
    LaneTeam team = { 0 };
    if (passed_all)
    {
        passed_all = lane_team_start(&team, capacity, 0);
        if (!passed_all)
        {
            fprintf(stderr, "test-files: can't start the command's %u lanes\n", capacity);
        }
    }
    if (passed_all && file_count == 0)
    {
        for (uint32_t way = 0; way < 4; way += 1)
        {
            passed_all &= files_test_tree(directory, lanes, lane_count, way < 2, (way & 1) != 0, id_order);
            if (!keep)
            {
                files_test_remove_tree(directory);
            }
        }
        passed_all &= files_test_spellings(directory, lanes, lane_count, seed, id_order);
        signal(SIGALRM, files_test_watchdog);
        passed_all &= files_test_contexts(directory, lanes, lane_count, id_order);
        passed_all &= files_test_dollars(directory, lanes, lane_count, id_order);
        passed_all &= files_test_rounds(directory, lanes, lane_count);
        passed_all &= files_test_special(directory, lanes, lane_count, id_order);
        passed_all &= files_test_memory(directory, seed);
        signal(SIGALRM, SIG_DFL);
    }
    else if (passed_all)
    {
        passed_all = files_test_files(&options, files, file_count, flat, directory, lanes, lane_count, id_order);
    }
    lane_team_stop(&team);
    arena_release(&options_arena);
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
