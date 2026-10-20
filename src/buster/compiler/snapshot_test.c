// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The runtime snapshot's tests (snapshot.c), for the test-snapshot command:
//
//   build/compiler test-snapshot [--seed N] [--functions N] [--directory PATH] [--keep]
//
// main.c includes this file after snapshot.c and dynamic_test.c, and calls run_snapshot_tests for the command. The
// snapshots and executables go in a directory of their own under --directory (TMPDIR, or /tmp, by default), which the
// tests remove unless --keep says to leave it. The checks, in this order:
//   - Round trip: the system's runtime, imported, written as a snapshot and loaded back, must have the same records as
//     the import's (snapshot_test_same_runtime), and so must the loaded one after a new snapshot was renamed over the
//     file it maps. The loaded runtime must use the file's mapping and no arena.
//   - Links: test-link's programs that call libc, and one of --functions workers (2,000 by default), bound by both of
//     binding's paths against the imported runtime and against the loaded one: the same links and bindings, written as
//     dynamic PIEs by 1 and 8 lanes into the same bytes, which run as their models predict.
//   - Opening: runtime_open in a directory that doesn't exist yet, which it makes (private), then in it again: imported
//     and written, then loaded. With no directory, or as an executable without an identity, the runtime is imported and
//     no file written; and where the directory can't be made, the link still gets the imported runtime.
//   - Staleness: snapshots with probes of the test's own files and directories, each then changed one way, must be stale:
//     a file rewritten in place to the same size, one whose mode changed, one replaced by a rename, one removed, a file
//     created where a probe found none, one added to a probed directory, and a file written to with its times put back
//     (st_ctim still changes). So must one written by another executable (another identity). runtime_open must then
//     import the runtime and write a new snapshot, which loads.
//   - Invalid files: copies of a snapshot cut short at sampled lengths, one byte longer, with a byte changed at sampled
//     offsets and in each of the header's bytes, must be invalid, and so must ones whose header was changed and the hash
//     made right again (another version or magic, a region misaligned, past the end or of the wrong size, counts out of
//     range); runtime_open must replace them. A directory and a FIFO at the snapshot's path must neither block nor
//     fail the link.
//   - Concurrency: eight processes opening the runtime at once in an empty directory must each get it, imported or
//     loaded, and leave one snapshot, which loads, and no other file.
//   - The cache directory: snapshot_directory from XDG_CACHE_HOME and HOME.
// The exit status is 1 if the arguments are invalid or a check fails.
//
// Layout, in this order: the tally, the comparison, the round trip, the links, opening, staleness, invalid files,
// concurrency, the cache directory, and run_snapshot_tests.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// OS-dependent includes
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define BUSTER_SNAPSHOT_TEST_CHILDREN 8
// How long a child that opens the runtime may take before an alarm ends it.
#define BUSTER_SNAPSHOT_TEST_SECONDS 30
// The headers the invalid files' check changes and rehashes: the version, the magic, the header's size, a region
// misaligned, one running past the end, one shorter than its column, the index's bits past 31, the names run past the
// strings, and a region starting past the end.
#define BUSTER_SNAPSHOT_TEST_HEADER_CHANGES 9

// ---------------------------------------------------------------------------------------------------------------------
// The tally

typedef struct SnapshotTestTally SnapshotTestTally;
struct SnapshotTestTally
{
    const char* check;
    uint64_t checked;
    uint64_t failures;
};

static void snapshot_test_expect(SnapshotTestTally* tally, bool condition, const char* what, uint64_t index)
{
    tally->checked += 1;
    if (!condition)
    {
        if (tally->failures < 10)
        {
            printf("  %s: %s (%llu)\n", tally->check, what, (unsigned long long)index);
        }
        tally->failures += 1;
    }
}

static bool snapshot_test_report(const SnapshotTestTally* tally)
{
    printf("%s: %llu checks, %llu failed\n", tally->check, (unsigned long long)tally->checked, (unsigned long long)tally->failures);
    return tally->failures == 0;
}

static const char* const snapshot_test_status_names[] = {
    [SNAPSHOT_STATUS_LOADED] = "loaded",
    [SNAPSHOT_STATUS_MISSING] = "missing",
    [SNAPSHOT_STATUS_STALE] = "stale",
    [SNAPSHOT_STATUS_INVALID] = "invalid",
};

// ---------------------------------------------------------------------------------------------------------------------
// The comparison

static bool snapshot_test_same_string(String8 a, String8 b)
{
    return a.length == b.length && (a.pointer != 0) == (b.pointer != 0) && (a.length == 0 || memcmp(a.pointer, b.pointer, a.length) == 0);
}

static bool snapshot_test_same_column(const void* a, const void* b, uint64_t bytes)
{
    return bytes == 0 || (a != 0 && b != 0 && memcmp(a, b, bytes) == 0);
}

// Whether a loaded runtime has the imported one's records: everything binding and a dynamic PIE read (see Runtime's
// snapshot field), compared by value, the strings by their bytes. Reports each part that differs.
static void snapshot_test_same_runtime(SnapshotTestTally* tally, const Runtime* imported, const Runtime* loaded, uint64_t index)
{
    snapshot_test_expect(tally, loaded->snapshot != 0 && imported->snapshot == 0, "a loaded runtime maps its snapshot", index);
    snapshot_test_expect(tally, loaded->tables.base == 0 && loaded->files.base == 0, "a loaded runtime has no arenas", index);
    snapshot_test_expect(tally, bind_test_same_links(&imported->link, &loaded->link), "the runtime's link", index);
    const Link* link = &loaded->link;
    bool names_run = loaded->names.length == imported->names.length && memcmp(loaded->names.base, imported->names.base, imported->names.length) == 0;
    for (NameId name = 1; names_run && name < link->names.count; name += 1)
    {
        names_run = link->names.bytes[name] - loaded->names.base == imported->link.names.bytes[name] - imported->names.base;
    }
    snapshot_test_expect(tally, names_run, "the names run, each name where the import put it", index);
    const uint64_t pieces = RUNTIME_OBJECTS + (uint64_t)imported->nonshared.member_count;
    const uint64_t symbols = imported->link.symbols.count;
    snapshot_test_expect(tally, loaded->nonshared.member_count == imported->nonshared.member_count && loaded->nonshared_unit == imported->nonshared_unit &&
                                    snapshot_test_same_column(loaded->pieces, imported->pieces, pieces * sizeof(RuntimePiece)),
                         "the pieces", index);
    snapshot_test_expect(tally, snapshot_test_same_column(loaded->bindings, imported->bindings, symbols * sizeof(RuntimeBinding)) &&
                                    snapshot_test_same_column(loaded->symbol_names, imported->symbol_names, symbols * sizeof(uint32_t)),
                         "the bindings and the symbols' global names", index);
    bool members = true;
    for (uint32_t m = 0; m < imported->nonshared.member_count; m += 1)
    {
        members = members && snapshot_test_same_string(loaded->nonshared.members[m].name, imported->nonshared.members[m].name) &&
                  loaded->nonshared.members[m].offset == imported->nonshared.members[m].offset;
    }
    snapshot_test_expect(tally, members, "the members' names", index);
    const RuntimeNames* a = &imported->global_names;
    const RuntimeNames* b = &loaded->global_names;
    const uint64_t count = a->count;
    bool globals = a->count == b->count && a->strings_length == b->strings_length && memcmp(a->strings, b->strings, a->strings_length) == 0 &&
                   snapshot_test_same_column(a->offset, b->offset, count * sizeof(uint32_t)) && snapshot_test_same_column(a->length, b->length, count * sizeof(uint32_t)) &&
                   snapshot_test_same_column(a->hash, b->hash, count * sizeof(uint64_t)) && snapshot_test_same_column(a->definition, b->definition, count * sizeof(SymbolId)) &&
                   snapshot_test_same_column(a->export, b->export, count * sizeof(uint32_t)) && snapshot_test_same_column(a->referenced, b->referenced, count) &&
                   snapshot_test_same_column(a->linker, b->linker, count);
    snapshot_test_expect(tally, globals, "the global names", index);
    const uint64_t starts = ((uint64_t)1 << a->index.bucket_bits) + 1;
    snapshot_test_expect(tally, a->index.bucket_bits == b->index.bucket_bits && snapshot_test_same_column(a->index.start, b->index.start, starts * sizeof(uint32_t)) &&
                                    snapshot_test_same_column(a->index.order, b->index.order, count * sizeof(uint32_t)) &&
                                    snapshot_test_same_column(a->index.hash, b->index.hash, count * sizeof(uint64_t)),
                         "their index", index);
    snapshot_test_expect(tally, a->filter_bits == b->filter_bits && snapshot_test_same_column(a->filter, b->filter, ((uint64_t)1 << a->filter_bits) * sizeof(uint64_t)),
                         "their filter", index);
    bool found = true;
    for (uint32_t e = 0; found && e < count; e += 1)
    {
        found = runtime_find_global_name(b, runtime_global_name(a, e), a->length[e], a->hash[e]) == e + 1;
    }
    snapshot_test_expect(tally, found, "each global name found in the loaded runtime", index);
    const ImportLibrary* x = &imported->libc;
    const ImportLibrary* y = &loaded->libc;
    bool versions = x->version_count == y->version_count && snapshot_test_same_string(x->soname, y->soname);
    for (uint32_t v = 0; versions && v < x->version_count; v += 1)
    {
        versions = snapshot_test_same_string(x->versions[v].name, y->versions[v].name) && x->versions[v].hash == y->versions[v].hash &&
                   x->versions[v].index == y->versions[v].index && x->versions[v].base == y->versions[v].base;
    }
    snapshot_test_expect(tally, versions, "libc.so.6's soname and versions", index);
    snapshot_test_expect(tally, x->exports.count == y->exports.count && snapshot_test_same_column(x->exports.version, y->exports.version, x->exports.count * sizeof(uint16_t)) &&
                                    snapshot_test_same_column(x->exports.flags, y->exports.flags, x->exports.count),
                         "libc.so.6's exports' versions and types", index);
    bool paths = snapshot_test_same_string(imported->gcc_directory, loaded->gcc_directory) && snapshot_test_same_string(imported->gcc_versions, loaded->gcc_versions) &&
                 snapshot_test_same_string(imported->libc_script, loaded->libc_script) && snapshot_test_same_string(imported->libc_path, loaded->libc_path) &&
                 snapshot_test_same_string(imported->nonshared_path, loaded->nonshared_path) && snapshot_test_same_string(imported->loader_path, loaded->loader_path) &&
                 snapshot_test_same_string(imported->abi_note, loaded->abi_note);
    for (uint32_t i = 0; i < RUNTIME_OBJECTS; i += 1)
    {
        paths = paths && snapshot_test_same_string(imported->object_paths[i], loaded->object_paths[i]);
    }
    snapshot_test_expect(tally, paths, "the paths and the ABI note", index);
    bool probes = imported->probe_count == loaded->probe_count;
    for (uint32_t p = 0; probes && p < imported->probe_count; p += 1)
    {
        const RuntimeProbe* i = &imported->probes[p];
        const RuntimeProbe* l = &loaded->probes[p];
        probes = snapshot_test_same_string(i->path, l->path) && l->path.pointer[l->path.length] == 0 && i->directory == l->directory && snapshot_same_identity(i, l);
    }
    snapshot_test_expect(tally, probes, "the probes", index);
}

// ---------------------------------------------------------------------------------------------------------------------
// The round trip

// The file at `path` in `directory`'s path.
static void snapshot_test_path(char* path, const char* directory, const char* name)
{
    snprintf(path, BUSTER_EXECUTABLE_TEST_PATH_BYTES, "%s/%s", directory, name);
}

static String8 snapshot_test_string(char* path)
{
    String8 result = {
        .pointer = path,
        .length = strlen(path),
    };
    return result;
}

// Writes the runtime's snapshot into `directory` as `compiler`.
static bool snapshot_test_write(const Runtime* runtime, const char* directory, const RuntimeProbe* compiler)
{
    SnapshotResult result = { 0 };
    return snapshot_write(runtime, directory, compiler, &result);
}

// Loads the snapshot in `directory` as `compiler`, into `runtime`.
static SnapshotStatus snapshot_test_load(Runtime* runtime, const char* directory, const RuntimeProbe* compiler)
{
    char path[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    snapshot_test_path(path, directory, BUSTER_SNAPSHOT_NAME);
    return snapshot_load(runtime, path, compiler, 0);
}

static bool snapshot_test_round_trip(const ExecutableTestDirectory* directory, const Runtime* imported, const RuntimeProbe* compiler)
{
    SnapshotTestTally tally = {
        .check = "round trip",
    };
    char place[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    snapshot_test_path(place, directory->path, "round-trip");
    const bool written = snapshot_test_write(imported, place, compiler);
    snapshot_test_expect(&tally, written, "the snapshot written", 0);
    Runtime loaded;
    const SnapshotStatus status = written ? snapshot_test_load(&loaded, place, compiler) : SNAPSHOT_STATUS_MISSING;
    snapshot_test_expect(&tally, status == SNAPSHOT_STATUS_LOADED, "the snapshot loaded", status);
    if (status == SNAPSHOT_STATUS_LOADED)
    {
        snapshot_test_same_runtime(&tally, imported, &loaded, 0);
        printf("  %u probes, %u global names, %u exports, %u symbols: a %llu-byte snapshot\n", loaded.probe_count, loaded.global_names.count, loaded.libc.exports.count,
               loaded.link.symbols.count - 1, (unsigned long long)loaded.snapshot_bytes);
        // Another snapshot renamed over the file: the loaded runtime keeps the one it maps.
        snapshot_test_expect(&tally, snapshot_test_write(imported, place, compiler), "a second snapshot written over the first", 0);
        snapshot_test_same_runtime(&tally, imported, &loaded, 1);
        Runtime again;
        const SnapshotStatus second = snapshot_test_load(&again, place, compiler);
        snapshot_test_expect(&tally, second == SNAPSHOT_STATUS_LOADED, "the second loaded", second);
        if (second == SNAPSHOT_STATUS_LOADED)
        {
            snapshot_test_same_runtime(&tally, imported, &again, 2);
            snapshot_test_expect(&tally, again.snapshot_bytes == loaded.snapshot_bytes && memcmp(again.snapshot, loaded.snapshot, sizeof(SnapshotHeader)) == 0,
                                 "the same bytes written twice", 0);
            runtime_release(&again);
        }
        runtime_release(&loaded);
    }
    char path[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    snapshot_test_path(path, place, BUSTER_SNAPSHOT_NAME);
    if (!directory->keep)
    {
        unlink(path);
        rmdir(place);
    }
    return snapshot_test_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// The links

// Binds a program against each runtime by `path` and writes it with 1 and 8 lanes: the same links, bindings and files.
static void snapshot_test_link(SnapshotTestTally* tally, const ExecutableTestDirectory* directory, const Runtime* imported, const Runtime* loaded, LinkProgramOptions options,
                               BindPath path, const char* name, uint64_t index)
{
    BindTestProgram a;
    BindTestProgram b;
    // Both bound, so that both can be released whatever happens.
    const bool bound_imported = bind_test_program(&a, imported, options, path);
    const bool bound_loaded = bind_test_program(&b, loaded, options, path);
    const bool bound = bound_imported && bound_loaded;
    snapshot_test_expect(tally, bound, "the program bound against both", index);
    snapshot_test_expect(tally, bound && bind_test_same_links(&a.program.link, &b.program.link) && bind_test_same_binds(&a.bind, &b.bind), "the same link and binding", index);
    char path_a[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    char path_b[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    snapshot_test_path(path_a, directory->path, "imported");
    snapshot_test_path(path_b, directory->path, "loaded");
    const uint32_t lane_counts[] = { 1, 8 };
    for (uint32_t l = 0; bound && l < BUSTER_ARRAY_LENGTH(lane_counts); l += 1)
    {
        Scratch scratch = scratch_begin(0);
        ExecutableResult written_a;
        ExecutableResult written_b;
        DynamicTestWrite write_a = { 0 };
        DynamicTestWrite write_b = { 0 };
        const bool written = executable_test_reset(&a.program.link, path_a) &&
                             dynamic_test_write_with(&write_a, &a.program.link, &a.bind, imported, snapshot_test_string(path_a), lane_counts[l], &written_a) &&
                             executable_test_reset(&b.program.link, path_b) &&
                             dynamic_test_write_with(&write_b, &b.program.link, &b.bind, loaded, snapshot_test_string(path_b), lane_counts[l], &written_b);
        snapshot_test_expect(tally, written, name, lane_counts[l]);
        const String8 file_a = written ? executable_test_read(scratch.arena, path_a) : (String8){ 0 };
        const String8 file_b = written ? executable_test_read(scratch.arena, path_b) : (String8){ 0 };
        snapshot_test_expect(tally, written && file_a.pointer != 0 && file_a.length == file_b.length && memcmp(file_a.pointer, file_b.pointer, file_a.length) == 0,
                             "the same executable", index * 2 + l);
        snapshot_test_expect(tally, written && executable_test_runs(path_b, b.program.output, BUSTER_LINK_PROGRAM_OUTPUT_BYTES), "the loaded runtime's executable runs",
                             index * 2 + l);
        scratch_end(scratch);
    }
    if (!directory->keep)
    {
        unlink(path_a);
        unlink(path_b);
    }
    bind_test_release(&a);
    bind_test_release(&b);
}

static bool snapshot_test_links(const ExecutableTestDirectory* directory, const Runtime* imported, const RuntimeProbe* compiler, uint64_t seed, uint32_t functions)
{
    SnapshotTestTally tally = {
        .check = "links",
    };
    char place[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    snapshot_test_path(place, directory->path, "links");
    Runtime loaded;
    const bool written = snapshot_test_write(imported, place, compiler);
    const SnapshotStatus status = written ? snapshot_test_load(&loaded, place, compiler) : SNAPSHOT_STATUS_MISSING;
    snapshot_test_expect(&tally, status == SNAPSHOT_STATUS_LOADED, "the snapshot loaded", status);
    LinkProgramOptions options[BUSTER_LINK_TEST_PROGRAMS + 1];
    link_test_program_options(options, seed, functions);
    options[BUSTER_LINK_TEST_PROGRAMS] = options[5];
    options[BUSTER_LINK_TEST_PROGRAMS].seed = seed + BUSTER_LINK_TEST_PROGRAMS;
    options[BUSTER_LINK_TEST_PROGRAMS].functions = functions;
    uint32_t linked = 0;
    for (uint32_t i = 0; status == SNAPSHOT_STATUS_LOADED && i <= BUSTER_LINK_TEST_PROGRAMS; i += 1)
    {
        const char* name = i == BUSTER_LINK_TEST_PROGRAMS ? "calling libc, --functions workers" : link_test_program_names[i];
        for (uint32_t path = 0; options[i].libc && path < 2; path += 1)
        {
            snapshot_test_link(&tally, directory, imported, &loaded, options[i], path == 0 ? BIND_PATH_VECTOR : BIND_PATH_EXACT, name, (uint64_t)i * 2 + path);
            linked += 1;
        }
    }
    printf("  %u programs bound and written against both runtimes\n", linked / 2);
    if (status == SNAPSHOT_STATUS_LOADED)
    {
        runtime_release(&loaded);
    }
    char path[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    snapshot_test_path(path, place, BUSTER_SNAPSHOT_NAME);
    if (!directory->keep)
    {
        unlink(path);
        rmdir(place);
    }
    return snapshot_test_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// Opening

// The number of entries in a directory, but . and ..; and whether one of them is named `name`.
static uint32_t snapshot_test_entries(const char* directory, const char* name, bool* found)
{
    uint32_t result = 0;
    DIR* entries = opendir(directory);
    *found = false;
    for (struct dirent* entry = entries != 0 ? readdir(entries) : 0; entry != 0; entry = readdir(entries))
    {
        const bool dots = strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0;
        result += dots ? 0 : 1;
        *found = *found || strcmp(entry->d_name, name) == 0;
    }
    if (entries != 0)
    {
        closedir(entries);
    }
    return result;
}

static bool snapshot_test_opening(const ExecutableTestDirectory* directory, const Runtime* imported, const RuntimeProbe* compiler)
{
    SnapshotTestTally tally = {
        .check = "opening",
    };
    // A directory two levels below one that exists: runtime_open makes both.
    char parent[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    char place[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    snapshot_test_path(parent, directory->path, "opening");
    snapshot_test_path(place, parent, "cache");
    for (uint32_t round = 0; round < 2; round += 1)
    {
        Runtime runtime;
        const SnapshotResult result = runtime_open_as(&runtime, place, compiler);
        const SnapshotStatus expected = round == 0 ? SNAPSHOT_STATUS_MISSING : SNAPSHOT_STATUS_LOADED;
        snapshot_test_expect(&tally, result.status == expected && result.error == RUNTIME_ERROR_NONE, round == 0 ? "imported, with no snapshot" : "loaded, the second time",
                             result.status);
        snapshot_test_expect(&tally, round == 1 || result.written, "the snapshot written", round);
        if (result.error == RUNTIME_ERROR_NONE)
        {
            if (round == 1)
            {
                snapshot_test_same_runtime(&tally, imported, &runtime, round);
            }
            runtime_release(&runtime);
        }
    }
    struct stat status;
    snapshot_test_expect(&tally, stat(place, &status) == 0 && S_ISDIR(status.st_mode) && (status.st_mode & 0777) == 0700, "the directory made, private", 0);
    snapshot_test_expect(&tally, stat(parent, &status) == 0 && S_ISDIR(status.st_mode) && (status.st_mode & 0777) == 0700, "the one above it made, private", 0);
    bool named = false;
    snapshot_test_expect(&tally, snapshot_test_entries(place, BUSTER_SNAPSHOT_NAME, &named) == 1 && named, "only the snapshot in it", 0);
    // Without a directory or an identity: imported, and nothing written.
    char none[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    snapshot_test_path(none, directory->path, "opening-none");
    for (uint32_t way = 0; way < 2; way += 1)
    {
        Runtime runtime;
        const SnapshotResult result = runtime_open_as(&runtime, way == 0 ? 0 : none, way == 0 ? compiler : 0);
        snapshot_test_expect(&tally, result.status == SNAPSHOT_STATUS_MISSING && result.error == RUNTIME_ERROR_NONE && !result.written, "imported, nothing written", way);
        if (result.error == RUNTIME_ERROR_NONE)
        {
            runtime_release(&runtime);
        }
    }
    snapshot_test_expect(&tally, stat(none, &status) != 0, "no directory made without an identity", 0);
    // A directory under a regular file can't be made: the runtime is still imported, and the failure named.
    char file[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    char under[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    snapshot_test_path(file, directory->path, "opening-file");
    snapshot_test_path(under, file, "cache");
    const int created = open(file, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (created >= 0)
    {
        close(created);
    }
    Runtime runtime;
    const SnapshotResult result = runtime_open_as(&runtime, under, compiler);
    snapshot_test_expect(&tally, result.error == RUNTIME_ERROR_NONE && !result.written && result.call != 0 && strcmp(result.call, "mkdir") == 0 && result.system_error == ENOTDIR,
                         "imported, the directory not made", 0);
    if (result.error == RUNTIME_ERROR_NONE)
    {
        runtime_release(&runtime);
    }
    if (!directory->keep)
    {
        char path[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
        snapshot_test_path(path, place, BUSTER_SNAPSHOT_NAME);
        unlink(path);
        rmdir(place);
        rmdir(parent);
        unlink(file);
    }
    return snapshot_test_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// Staleness
//
// The import's probes are of the system's files, which the test can't change; so each case copies the imported runtime
// with probes of the test's own files added, writes its snapshot, changes one of the files and loads it again.

typedef enum SnapshotTestChange
{
    SNAPSHOT_TEST_REWRITTEN,
    SNAPSHOT_TEST_MODE,
    SNAPSHOT_TEST_RENAMED,
    SNAPSHOT_TEST_REMOVED,
    SNAPSHOT_TEST_CREATED,
    SNAPSHOT_TEST_ADDED,
    SNAPSHOT_TEST_TIMES,
    SNAPSHOT_TEST_COMPILER,
    SNAPSHOT_TEST_CHANGES,
} SnapshotTestChange;

static const char* const snapshot_test_change_names[SNAPSHOT_TEST_CHANGES] = {
    [SNAPSHOT_TEST_REWRITTEN] = "a file rewritten in place to the same size",
    [SNAPSHOT_TEST_MODE] = "a file's mode changed",
    [SNAPSHOT_TEST_RENAMED] = "a file replaced by a rename",
    [SNAPSHOT_TEST_REMOVED] = "a file removed",
    [SNAPSHOT_TEST_CREATED] = "a file created where a probe found none",
    [SNAPSHOT_TEST_ADDED] = "a file added to a probed directory",
    [SNAPSHOT_TEST_TIMES] = "a file written to with its times put back",
    [SNAPSHOT_TEST_COMPILER] = "another executable's snapshot",
};

static bool snapshot_test_put(const char* path, const char* text)
{
    const int file = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    const uint64_t length = strlen(text);
    const bool result = file >= 0 && write(file, text, length) == (ssize_t)length;
    if (file >= 0)
    {
        close(file);
    }
    return result;
}

// The test's files for a case: `file` (found), `absent` (not there) and `directory` (found, empty), under `place`.
static bool snapshot_test_files(const char* place, char* file, char* absent, char* directory)
{
    snapshot_test_path(file, place, "file");
    snapshot_test_path(absent, place, "absent");
    snapshot_test_path(directory, place, "directory");
    return mkdir(place, 0700) == 0 && snapshot_test_put(file, "the probed file") && mkdir(directory, 0700) == 0;
}

static bool snapshot_test_change(SnapshotTestChange change, const char* place, const char* file, const char* absent, const char* directory)
{
    bool result = true;
    char other[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    snapshot_test_path(other, place, "other");
    struct stat before;
    switch (change)
    {
    case SNAPSHOT_TEST_REWRITTEN:
        result = snapshot_test_put(file, "the changed file");
        break;
    case SNAPSHOT_TEST_MODE:
        result = chmod(file, 0600) == 0;
        break;
    case SNAPSHOT_TEST_RENAMED:
        result = snapshot_test_put(other, "the probed file") && rename(other, file) == 0;
        break;
    case SNAPSHOT_TEST_REMOVED:
        result = unlink(file) == 0;
        break;
    case SNAPSHOT_TEST_CREATED:
        result = snapshot_test_put(absent, "a new file");
        break;
    case SNAPSHOT_TEST_ADDED:
        snapshot_test_path(other, directory, "added");
        result = snapshot_test_put(other, "a new file");
        break;
    case SNAPSHOT_TEST_TIMES:
    {
        result = stat(file, &before) == 0 && snapshot_test_put(file, "the probed file");
        const struct timespec times[2] = { before.st_atim, before.st_mtim };
        result = result && utimensat(AT_FDCWD, file, times, 0) == 0;
        break;
    }
    case SNAPSHOT_TEST_COMPILER:
    case SNAPSHOT_TEST_CHANGES:
        break;
    }
    return result;
}

// Removes a case's files, whichever are there.
static void snapshot_test_clean(const char* place, const char* file, const char* absent, const char* directory)
{
    char path[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    snapshot_test_path(path, directory, "added");
    unlink(path);
    rmdir(directory);
    unlink(file);
    unlink(absent);
    snapshot_test_path(path, place, "other");
    unlink(path);
    snapshot_test_path(path, place, BUSTER_SNAPSHOT_NAME);
    unlink(path);
    rmdir(place);
}

static bool snapshot_test_staleness(const ExecutableTestDirectory* directory, const Runtime* imported, const RuntimeProbe* compiler)
{
    SnapshotTestTally tally = {
        .check = "staleness",
    };
    for (uint32_t change = 0; change < SNAPSHOT_TEST_CHANGES; change += 1)
    {
        char place[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
        char file[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
        char absent[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
        char probed[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
        char name[64];
        snprintf(name, sizeof(name), "stale-%u", change);
        snapshot_test_path(place, directory->path, name);
        const bool made = snapshot_test_files(place, file, absent, probed);
        snapshot_test_expect(&tally, made, "the case's files", change);
        // The imported runtime with three probes of the case's files after its own, in a copy of its probes.
        Runtime runtime = *imported;
        RuntimeProbe probes[BUSTER_RUNTIME_MAX_PROBES];
        memcpy(probes, imported->probes, imported->probe_count * sizeof(RuntimeProbe));
        runtime.probes = probes;
        runtime.probe_capacity = BUSTER_RUNTIME_MAX_PROBES;
        const String8 paths[3] = { { file, strlen(file) }, { absent, strlen(absent) }, { probed, strlen(probed) } };
        const bool directories[3] = { false, false, true };
        for (uint32_t p = 0; p < 3 && runtime.probe_count < runtime.probe_capacity; p += 1)
        {
            struct stat status;
            RuntimeProbe* probe = &runtime.probes[runtime.probe_count];
            *probe = (RuntimeProbe){
                .path = paths[p],
                .directory = directories[p],
            };
            if (stat(paths[p].pointer, &status) == 0 && (directories[p] ? S_ISDIR(status.st_mode) : S_ISREG(status.st_mode)))
            {
                runtime_identify(probe, &status);
            }
            runtime.probe_count += 1;
        }
        // Another executable's snapshot: its identity's inode one more than this one's.
        RuntimeProbe writer = *compiler;
        writer.inode += change == SNAPSHOT_TEST_COMPILER ? 1 : 0;
        const bool written = made && snapshot_test_write(&runtime, place, &writer);
        Runtime loaded;
        const SnapshotStatus first = written ? snapshot_test_load(&loaded, place, &writer) : SNAPSHOT_STATUS_MISSING;
        snapshot_test_expect(&tally, first == SNAPSHOT_STATUS_LOADED, "loaded before the change", change);
        if (first == SNAPSHOT_STATUS_LOADED)
        {
            snapshot_test_expect(&tally, loaded.probe_count == runtime.probe_count && runtime_probes_current(&loaded), "the case's probes in it", change);
            runtime_release(&loaded);
        }
        snapshot_test_expect(&tally, snapshot_test_change(change, place, file, absent, probed), snapshot_test_change_names[change], change);
        const SnapshotStatus second = written ? snapshot_test_load(&loaded, place, compiler) : SNAPSHOT_STATUS_MISSING;
        snapshot_test_expect(&tally, second == SNAPSHOT_STATUS_STALE, snapshot_test_change_names[change], second);
        if (second == SNAPSHOT_STATUS_LOADED)
        {
            runtime_release(&loaded);
        }
        // runtime_open imports the runtime and writes a snapshot of its own probes, which loads.
        Runtime opened;
        const SnapshotResult result = runtime_open_as(&opened, place, compiler);
        snapshot_test_expect(&tally, result.status == SNAPSHOT_STATUS_STALE && result.error == RUNTIME_ERROR_NONE && result.written, "imported again, and written", result.status);
        if (result.error == RUNTIME_ERROR_NONE)
        {
            runtime_release(&opened);
        }
        const SnapshotStatus third = snapshot_test_load(&loaded, place, compiler);
        snapshot_test_expect(&tally, third == SNAPSHOT_STATUS_LOADED, "the new snapshot loaded", third);
        if (third == SNAPSHOT_STATUS_LOADED)
        {
            runtime_release(&loaded);
        }
        if (!directory->keep)
        {
            snapshot_test_clean(place, file, absent, probed);
        }
    }
    return snapshot_test_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// Invalid files

// Writes `length` bytes of `bytes` to `path`, then `extra` bytes of zeros, with the byte at `flip` (if it is below
// length) changed.
static bool snapshot_test_copy(const char* path, const uint8_t* bytes, uint64_t length, uint64_t extra, uint64_t flip)
{
    const int file = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    bool result = file >= 0;
    uint64_t done = 0;
    while (result && done < length)
    {
        const uint64_t piece = flip >= done && flip < length ? flip - done : length - done;
        const ssize_t count = piece != 0 ? write(file, bytes + done, piece) : 0;
        const uint8_t changed = (uint8_t)(bytes[done] ^ 0x5a);
        const ssize_t flipped = piece == 0 ? write(file, &changed, 1) : 0;
        result = (piece != 0 && count > 0) || (piece == 0 && flipped == 1);
        done += piece != 0 ? (count > 0 ? (uint64_t)count : 0) : 1;
    }
    static const uint8_t zeros[64] = { 0 };
    result = result && (extra == 0 || write(file, zeros, extra) == (ssize_t)extra);
    if (file >= 0)
    {
        close(file);
    }
    return result;
}

// runtime_open in a child, which an alarm ends if it blocks: whether it ended in time, having imported the runtime and,
// with `refused`, failed to rename the snapshot over what is at its path, or otherwise written it.
static bool snapshot_test_open_child(const char* place, const RuntimeProbe* compiler, bool refused)
{
    fflush(stdout);
    const pid_t child = fork();
    if (child == 0)
    {
        alarm(BUSTER_SNAPSHOT_TEST_SECONDS);
        Runtime runtime;
        const SnapshotResult result = runtime_open_as(&runtime, place, compiler);
        const bool imported = result.status == SNAPSHOT_STATUS_MISSING && result.error == RUNTIME_ERROR_NONE;
        const bool written = refused ? !result.written && result.call != 0 && strcmp(result.call, "rename") == 0 : result.written;
        _exit(imported && written ? 0 : 1);
    }
    int status = 0;
    return child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static bool snapshot_test_invalid(const ExecutableTestDirectory* directory, const Runtime* imported, const RuntimeProbe* compiler)
{
    SnapshotTestTally tally = {
        .check = "invalid files",
    };
    char place[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    char path[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    snapshot_test_path(place, directory->path, "invalid");
    snapshot_test_path(path, place, BUSTER_SNAPSHOT_NAME);
    Scratch scratch = scratch_begin(0);
    const bool written = snapshot_test_write(imported, place, compiler);
    const String8 good = written ? executable_test_read(scratch.arena, path) : (String8){ 0 };
    snapshot_test_expect(&tally, good.pointer != 0 && good.length > sizeof(SnapshotHeader), "the snapshot written", 0);
    const uint8_t* bytes = (const uint8_t*)good.pointer;
    uint64_t cases = 0;
    // Cut short (a length for each 1/61 of the file, and around the header), one byte longer, and a byte changed (at each
    // 1/61 of the file, and in each of the header's first bytes).
    for (uint64_t i = 0; good.pointer != 0 && i < 61 + 3 + 1 + 61 + sizeof(SnapshotHeader); i += 1)
    {
        uint64_t length = good.length;
        uint64_t extra = 0;
        uint64_t flip = UINT64_MAX;
        if (i < 61)
        {
            length = good.length * i / 61;
        }
        else if (i < 64)
        {
            length = sizeof(SnapshotHeader) - 1 + (i - 61);
        }
        else if (i == 64)
        {
            extra = 1;
        }
        else if (i < 65 + 61)
        {
            flip = good.length * (i - 65) / 61 + (i - 65);
        }
        else
        {
            flip = i - 65 - 61;
        }
        const bool copied = snapshot_test_copy(path, bytes, length < good.length ? length : good.length, extra, flip < good.length ? flip : UINT64_MAX);
        Runtime loaded;
        const SnapshotStatus status = copied ? snapshot_load(&loaded, path, compiler, 0) : SNAPSHOT_STATUS_LOADED;
        snapshot_test_expect(&tally, status == SNAPSHOT_STATUS_INVALID, "a damaged snapshot invalid", i);
        if (status == SNAPSHOT_STATUS_LOADED)
        {
            runtime_release(&loaded);
        }
        cases += 1;
    }
    // Headers that the hash passes (rehashed after the change) but that aren't this format's, or whose regions don't fit
    // the file: each checked by snapshot_valid_header alone.
    uint8_t* edited = good.pointer != 0 ? arena_push(scratch.arena, good.length, 64) : 0;
    for (uint32_t change = 0; edited != 0 && change < BUSTER_SNAPSHOT_TEST_HEADER_CHANGES; change += 1)
    {
        memcpy(edited, bytes, good.length);
        SnapshotHeader* header = (SnapshotHeader*)edited;
        switch (change)
        {
        case 0:
            header->version += 1;
            break;
        case 1:
            header->magic ^= 1;
            break;
        case 2:
            header->header_bytes -= 8;
            break;
        case 3:
            header->regions[SNAPSHOT_REGION_GLOBAL_HASH].offset += 8;
            break;
        case 4:
            // Its size its column's, but past the end from inside the file.
            header->regions[SNAPSHOT_REGION_FILTER].offset = (good.length - BUSTER_SNAPSHOT_ALIGNMENT) & ~(uint64_t)(BUSTER_SNAPSHOT_ALIGNMENT - 1);
            break;
        case 5:
            header->regions[SNAPSHOT_REGION_SYMBOL_NAMES].bytes -= 4;
            break;
        case 6:
            header->counts.bucket_bits = 40;
            break;
        case 7:
            header->counts.names_run = header->regions[SNAPSHOT_REGION_STRINGS].bytes;
            break;
        default:
            // Starting past the end.
            header->regions[SNAPSHOT_REGION_FILTER].offset = (good.length + 2 * BUSTER_SNAPSHOT_ALIGNMENT) & ~(uint64_t)(BUSTER_SNAPSHOT_ALIGNMENT - 1);
            break;
        }
        snapshot_hash(edited, good.length, header->hash);
        const bool copied = snapshot_test_copy(path, edited, good.length, 0, UINT64_MAX);
        Runtime loaded;
        const SnapshotStatus status = copied ? snapshot_load(&loaded, path, compiler, 0) : SNAPSHOT_STATUS_LOADED;
        snapshot_test_expect(&tally, status == SNAPSHOT_STATUS_INVALID, "a header the hash passes, invalid", change);
        if (status == SNAPSHOT_STATUS_LOADED)
        {
            runtime_release(&loaded);
        }
        cases += 1;
    }
    // runtime_open replaces the last one, which then loads.
    Runtime opened;
    const SnapshotResult result = runtime_open_as(&opened, place, compiler);
    snapshot_test_expect(&tally, result.status == SNAPSHOT_STATUS_INVALID && result.error == RUNTIME_ERROR_NONE && result.written, "an invalid snapshot replaced", result.status);
    if (result.error == RUNTIME_ERROR_NONE)
    {
        runtime_release(&opened);
    }
    Runtime loaded;
    const SnapshotStatus replaced = snapshot_load(&loaded, path, compiler, 0);
    snapshot_test_expect(&tally, replaced == SNAPSHOT_STATUS_LOADED, "its replacement loaded", replaced);
    if (replaced == SNAPSHOT_STATUS_LOADED)
    {
        runtime_release(&loaded);
    }
    // A directory, then a FIFO, at the snapshot's path: imported without blocking (in a child, which an alarm ends if it
    // does), the directory left in place with nothing beside it, and the FIFO replaced by a snapshot.
    unlink(path);
    for (uint32_t way = 0; way < 2; way += 1)
    {
        const bool placed = way == 0 ? mkdir(path, 0700) == 0 : mkfifo(path, 0600) == 0;
        snapshot_test_expect(&tally, placed, way == 0 ? "a directory at the path" : "a FIFO at the path", way);
        snapshot_test_expect(&tally, snapshot_test_open_child(place, compiler, way == 0), way == 0 ? "imported, not renamed over a directory" : "imported, a FIFO replaced",
                             way);
        bool named = false;
        struct stat status;
        snapshot_test_expect(&tally, snapshot_test_entries(place, BUSTER_SNAPSHOT_NAME, &named) == 1 && named && stat(path, &status) == 0 &&
                                         (way == 0 ? S_ISDIR(status.st_mode) : S_ISREG(status.st_mode)),
                             "nothing beside it", way);
        if (way == 0)
        {
            rmdir(path);
        }
    }
    printf("  %llu damaged snapshots\n", (unsigned long long)cases);
    scratch_end(scratch);
    if (!directory->keep)
    {
        unlink(path);
        rmdir(place);
    }
    return snapshot_test_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// Concurrency

static bool snapshot_test_concurrency(const ExecutableTestDirectory* directory, const Runtime* imported, const RuntimeProbe* compiler)
{
    SnapshotTestTally tally = {
        .check = "concurrency",
    };
    char place[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    snapshot_test_path(place, directory->path, "concurrency");
    // The children wait on a pipe until every one is forked, then open the runtime at once.
    int gate[2];
    const bool piped = pipe(gate) == 0;
    pid_t children[BUSTER_SNAPSHOT_TEST_CHILDREN];
    uint32_t forked = 0;
    fflush(stdout);
    for (uint32_t c = 0; piped && c < BUSTER_SNAPSHOT_TEST_CHILDREN; c += 1)
    {
        children[c] = fork();
        if (children[c] == 0)
        {
            char token = 0;
            close(gate[1]);
            const bool opened = read(gate[0], &token, 1) == 0;
            Runtime runtime;
            const SnapshotResult result = runtime_open_as(&runtime, place, compiler);
            const bool fine = opened && result.error == RUNTIME_ERROR_NONE && (result.status == SNAPSHOT_STATUS_LOADED || result.written);
            _exit(fine ? 0 : 1);
        }
        forked += children[c] > 0 ? 1 : 0;
    }
    if (piped)
    {
        close(gate[0]);
        close(gate[1]);
    }
    uint32_t succeeded = 0;
    for (uint32_t c = 0; c < forked; c += 1)
    {
        int status = 0;
        succeeded += waitpid(children[c], &status, 0) == children[c] && WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 1 : 0;
    }
    snapshot_test_expect(&tally, forked == BUSTER_SNAPSHOT_TEST_CHILDREN && succeeded == forked, "each process got the runtime", succeeded);
    bool named = false;
    snapshot_test_expect(&tally, snapshot_test_entries(place, BUSTER_SNAPSHOT_NAME, &named) == 1 && named, "one snapshot left, and no other file", 0);
    Runtime loaded;
    const SnapshotStatus status = snapshot_test_load(&loaded, place, compiler);
    snapshot_test_expect(&tally, status == SNAPSHOT_STATUS_LOADED, "it loads", status);
    if (status == SNAPSHOT_STATUS_LOADED)
    {
        snapshot_test_same_runtime(&tally, imported, &loaded, 0);
        runtime_release(&loaded);
    }
    if (!directory->keep)
    {
        char path[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
        snapshot_test_path(path, place, BUSTER_SNAPSHOT_NAME);
        unlink(path);
        rmdir(place);
    }
    return snapshot_test_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// The cache directory

// Sets an environment variable, or with a null value removes it.
static void snapshot_test_environment(const char* name, const char* value)
{
    if (value != 0)
    {
        setenv(name, value, 1);
    }
    else
    {
        unsetenv(name);
    }
}

static bool snapshot_test_directories(void)
{
    SnapshotTestTally tally = {
        .check = "cache directory",
    };
    char* saved_cache = getenv("XDG_CACHE_HOME") != 0 ? strdup(getenv("XDG_CACHE_HOME")) : 0;
    char* saved_home = getenv("HOME") != 0 ? strdup(getenv("HOME")) : 0;
    typedef struct SnapshotTestEnvironment SnapshotTestEnvironment;
    struct SnapshotTestEnvironment
    {
        const char* cache;
        const char* home;
        const char* expected;
    };
    static const SnapshotTestEnvironment cases[] = {
        { "/cache", "/home/user", "/cache/buster" },
        { 0, "/home/user", "/home/user/.cache/buster" },
        { "relative", "/home/user", "/home/user/.cache/buster" },
        { "", "/home/user", "/home/user/.cache/buster" },
        { 0, 0, 0 },
        { "relative", "relative", 0 },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        snapshot_test_environment("XDG_CACHE_HOME", cases[i].cache);
        snapshot_test_environment("HOME", cases[i].home);
        char path[BUSTER_RUNTIME_PATH_BYTES];
        const bool found = snapshot_directory(path, sizeof(path));
        snapshot_test_expect(&tally, cases[i].expected != 0 ? found && strcmp(path, cases[i].expected) == 0 : !found && path[0] == 0, "the cache directory", i);
    }
    // A path that doesn't fit.
    snapshot_test_environment("XDG_CACHE_HOME", "/cache");
    char small[8];
    snapshot_test_expect(&tally, !snapshot_directory(small, sizeof(small)) && small[0] == 0, "a path that doesn't fit", 0);
    snapshot_test_environment("XDG_CACHE_HOME", saved_cache);
    snapshot_test_environment("HOME", saved_home);
    free(saved_cache);
    free(saved_home);
    return snapshot_test_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// run_snapshot_tests

static int run_snapshot_tests(int argc, char** argv)
{
    uint64_t seed = 1;
    uint64_t functions = BUSTER_EXECUTABLE_TEST_DEFAULT_FUNCTIONS;
    const char* parent = getenv("TMPDIR") != 0 ? getenv("TMPDIR") : "/tmp";
    bool keep = false;
    bool valid = true;
    for (int i = 0; i < argc; i += 1)
    {
        if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
        {
            valid = valid && executable_test_parse_u64(argv[i + 1], &seed);
            i += 1;
        }
        else if (strcmp(argv[i], "--functions") == 0 && i + 1 < argc)
        {
            valid = valid && executable_test_parse_u64(argv[i + 1], &functions) && functions >= 1 && functions <= BUSTER_LINK_PROGRAM_MAX_COUNT;
            i += 1;
        }
        else if (strcmp(argv[i], "--directory") == 0 && i + 1 < argc)
        {
            parent = argv[i + 1];
            i += 1;
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
    ExecutableTestDirectory directory = {
        .keep = keep,
    };
    const int length = snprintf(directory.path, sizeof(directory.path), "%s/buster-snapshot-XXXXXX", parent);
    int result = 1;
    Runtime runtime;
    RuntimeProbe compiler;
    const RuntimeError imported = valid ? runtime_import(&runtime) : RUNTIME_ERROR_NONE;
    if (!valid)
    {
        fprintf(stderr, "usage: test-snapshot [--seed N] [--functions N] [--directory PATH] [--keep]\n");
    }
    else if (imported != RUNTIME_ERROR_NONE)
    {
        fprintf(stderr, "the runtime didn't import: error %d, %s\n", (int)imported, runtime.failed_file);
    }
    else if (!snapshot_compiler(&compiler))
    {
        fprintf(stderr, "test-snapshot: this executable has no identity (/proc/self/exe)\n");
    }
    else if (length <= 0 || (uint64_t)length + 64 >= sizeof(directory.path) || mkdtemp(directory.path) == 0)
    {
        fprintf(stderr, "test-snapshot: can't make a directory in %s\n", parent);
    }
    else if (!x86_encoder_initialized && !x86_encoder_initialize())
    {
        fprintf(stderr, "the encoder's templates can't be built\n");
    }
    else
    {
        bool passed = snapshot_test_round_trip(&directory, &runtime, &compiler);
        passed = snapshot_test_links(&directory, &runtime, &compiler, seed, (uint32_t)functions) && passed;
        passed = snapshot_test_opening(&directory, &runtime, &compiler) && passed;
        passed = snapshot_test_staleness(&directory, &runtime, &compiler) && passed;
        passed = snapshot_test_invalid(&directory, &runtime, &compiler) && passed;
        passed = snapshot_test_concurrency(&directory, &runtime, &compiler) && passed;
        passed = snapshot_test_directories() && passed;
        result = passed ? 0 : 1;
        if (keep)
        {
            printf("kept %s\n", directory.path);
        }
        else
        {
            rmdir(directory.path);
        }
    }
    if (valid)
    {
        runtime_release(&runtime);
    }
    return result;
}
