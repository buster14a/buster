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
//     file it maps. The loaded runtime must use the file's mapping and no arena. Then the same for a static
//     executable's runtime (runtime_import_static), with its archives' members, their references and COMDAT groups, and
//     for a static PIE's (runtime_import_static_pie).
//   - Links: test-link's programs that call libc, and one of --functions workers (2,000 by default), bound by both of
//     binding's paths against the imported runtime and against the loaded one: the same links and bindings, written as
//     dynamic PIEs by 1 and 8 lanes into the same bytes, which run as their models predict. Then the same against each
//     static kind's runtime, written as static executables and as static PIEs.
//   - Opening: runtime_open in a directory that doesn't exist yet, which it makes (private), then in it again: imported
//     and written, then loaded. With no directory, or as an executable without an identity, the runtime is imported and
//     no file written; and where the directory can't be made, the link still gets the imported runtime.
//   - Kinds: runtime_open_static and runtime_open_static_pie, then runtime_open, in one directory, each kind's snapshot
//     a file of its own (snapshot_name) beside the others'; each loaded as another kind, stale; and each renamed over
//     another kind's, stale there, and written over.
//   - Staleness: snapshots with probes of the test's own files and directories, each then changed one way, must be stale:
//     a file rewritten in place to the same size, one whose mode changed, one replaced by a rename, one removed, a file
//     created where a probe found none, one added to a probed directory, and a file written to with its times put back
//     (st_ctim still changes). So must one written by another executable (another identity). runtime_open must then
//     import the runtime and write a new snapshot, which loads.
//   - Invalid files, of the dynamic PIE's runtime's snapshot and the static PIE's: copies of a snapshot cut short at
//     sampled lengths, one byte longer, with a byte changed at sampled offsets and in each of the header's bytes, must
//     be invalid, and so must ones whose header was changed and the hash made right again (another version or magic, a
//     region misaligned, past the end or of the wrong size, counts out of range); runtime_open must replace them. A
//     directory and a FIFO at the snapshot's path must neither block nor fail the link.
//   - Damaged pieces, of each kind's snapshot: a byte of the pieces' records changed, which only the piece that holds
//     it shows, and a link that reads that piece fails (snapshot_test_damaged).
//   - Concurrency: eight processes opening the runtime at once in an empty directory must each get it, imported or
//     loaded, and leave one snapshot, which loads, and no other file.
//   - The cache directory: snapshot_directory from XDG_CACHE_HOME and HOME.
// The exit status is 1 if the arguments are invalid or a check fails.
//
// Layout, in this order: the tally, the comparison, the round trip, the links, opening, staleness, invalid files,
// damaged pieces, the kinds, concurrency, the cache directory, and run_snapshot_tests.

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
// data, a region starting past the end, and the directory's end before its last region's.
#define BUSTER_SNAPSHOT_TEST_HEADER_CHANGES 10

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

// Whether two runtimes' links have the same records, as bind_test_same_links compares two links, the names' and atoms'
// bytes through each runtime's accessors (a loaded runtime's columns hold offsets).
static bool snapshot_test_same_link(const Runtime* x, const Runtime* y)
{
    const Link* a = &x->link;
    const Link* b = &y->link;
    bool result = a->names.count == b->names.count && a->atoms.count == b->atoms.count && a->symbols.count == b->symbols.count && a->fixups.count == b->fixups.count &&
                  a->fixups.wide_count == b->fixups.wide_count && a->priorities.count == b->priorities.count;
    for (NameId name = 1; result && name < a->names.count; name += 1)
    {
        result = a->names.length[name] == b->names.length[name] && a->names.hash[name] == b->names.hash[name] &&
                 memcmp(runtime_name_bytes(x, name), runtime_name_bytes(y, name), (uint64_t)a->names.length[name] + 1) == 0;
    }
    for (AtomId atom = 1; result && atom < a->atoms.count; atom += 1)
    {
        const uint8_t* p = runtime_atom_bytes(x, atom);
        const uint8_t* q = runtime_atom_bytes(y, atom);
        result = a->atoms.class[atom] == b->atoms.class[atom] && a->atoms.alignment_log2[atom] == b->atoms.alignment_log2[atom] && a->atoms.unit[atom] == b->atoms.unit[atom] &&
                 a->atoms.size[atom] == b->atoms.size[atom] && a->atoms.fixup_begin[atom] == b->atoms.fixup_begin[atom] &&
                 a->atoms.fixup_count[atom] == b->atoms.fixup_count[atom] && a->atoms.address[atom] == b->atoms.address[atom] && (p != 0) == (q != 0) &&
                 (p == 0 || memcmp(p, q, a->atoms.size[atom]) == 0);
    }
    const LinkSymbols* s = &a->symbols;
    const LinkSymbols* t = &b->symbols;
    for (SymbolId symbol = 1; result && symbol < s->count; symbol += 1)
    {
        result = s->name[symbol] == t->name[symbol] && s->atom[symbol] == t->atom[symbol] && s->value[symbol] == t->value[symbol] && s->size[symbol] == t->size[symbol] &&
                 s->flags[symbol] == t->flags[symbol] && s->source[symbol] == t->source[symbol] && s->got_slot[symbol] == t->got_slot[symbol] &&
                 s->dynamic_index[symbol] == t->dynamic_index[symbol] && s->version[symbol] == t->version[symbol] && s->reference[symbol] == t->reference[symbol];
    }
    for (FixupId fixup = 1; result && fixup < a->fixups.count; fixup += 1)
    {
        result = a->fixups.offset[fixup] == b->fixups.offset[fixup] && a->fixups.target[fixup] == b->fixups.target[fixup] && a->fixups.kind[fixup] == b->fixups.kind[fixup] &&
                 link_fixup_addend(a, fixup) == link_fixup_addend(b, fixup);
    }
    for (uint32_t entry = 1; result && entry < a->priorities.count; entry += 1)
    {
        result = a->priorities.atom[entry] == b->priorities.atom[entry] && a->priorities.priority[entry] == b->priorities.priority[entry];
    }
    return result;
}

// Whether a loaded runtime has the imported one's records: everything binding and a dynamic PIE read (see Runtime's
// snapshot field), compared by value, the strings by their bytes, and every piece as it was written
// (runtime_piece_current). Reports each part that differs.
static void snapshot_test_same_runtime(SnapshotTestTally* tally, const Runtime* imported, const Runtime* loaded, uint64_t index)
{
    snapshot_test_expect(tally, loaded->snapshot != 0 && imported->snapshot == 0, "a loaded runtime maps its snapshot", index);
    snapshot_test_expect(tally, loaded->tables.base == 0 && loaded->files.base == 0, "a loaded runtime has no arenas", index);
    snapshot_test_expect(tally, snapshot_test_same_link(imported, loaded), "the runtime's link", index);
    snapshot_test_expect(tally, loaded->name_room == imported->name_room && loaded->atom_room == imported->atom_room, "the room for its names' and atoms' bytes", index);
    bool current = true;
    for (uint32_t piece = 0; piece < RUNTIME_OBJECTS + loaded->member_count; piece += 1)
    {
        current = current && runtime_piece_current(loaded, piece);
    }
    snapshot_test_expect(tally, current, "every piece as it was written", index);
    const Link* link = &loaded->link;
    bool names_run = loaded->names.length == imported->names.length && memcmp(loaded->names.base, imported->names.base, imported->names.length) == 0;
    for (NameId name = 1; names_run && name < link->names.count; name += 1)
    {
        names_run = runtime_name_bytes(loaded, name) - loaded->names.base == imported->link.names.bytes[name] - imported->names.base;
    }
    snapshot_test_expect(tally, names_run, "the names run, each name where the import put it", index);
    const uint64_t pieces = RUNTIME_OBJECTS + (uint64_t)imported->member_count;
    const uint64_t symbols = imported->link.symbols.count;
    snapshot_test_expect(tally, loaded->member_count == imported->member_count && loaded->member_unit == imported->member_unit &&
                                    snapshot_test_same_column(loaded->pieces, imported->pieces, pieces * sizeof(RuntimePiece)),
                         "the pieces", index);
    bool archives = loaded->kind == imported->kind && loaded->archive_count == imported->archive_count;
    for (uint32_t a = 0; archives && a < imported->archive_count; a += 1)
    {
        archives = snapshot_test_same_string(loaded->archives[a].path, imported->archives[a].path) && loaded->archives[a].first_member == imported->archives[a].first_member &&
                   loaded->archives[a].archive.member_count == imported->archives[a].archive.member_count &&
                   loaded->archives[a].archive.members == loaded->members + imported->archives[a].first_member;
    }
    snapshot_test_expect(tally, archives, "the kind and the archives", index);
    // The pieces' references and COMDAT groups, which a static runtime's closure reads.
    const uint64_t references = imported->reference_begin[pieces];
    const uint64_t groups = imported->group_begin[pieces];
    const uint64_t group_atoms = groups != 0 ? (uint64_t)imported->groups[groups - 1].first + imported->groups[groups - 1].count : 0;
    snapshot_test_expect(tally, snapshot_test_same_column(loaded->reference_begin, imported->reference_begin, (pieces + 1) * sizeof(uint32_t)) &&
                                    snapshot_test_same_column(loaded->references, imported->references, references * sizeof(uint32_t)) &&
                                    snapshot_test_same_column(loaded->reference_members, imported->reference_members, references * sizeof(uint32_t)),
                         "the references and the members they load", index);
    snapshot_test_expect(tally, loaded->group_signatures == imported->group_signatures &&
                                    snapshot_test_same_column(loaded->group_begin, imported->group_begin, (pieces + 1) * sizeof(uint32_t)) &&
                                    snapshot_test_same_column(loaded->groups, imported->groups, groups * sizeof(RuntimeGroup)) &&
                                    snapshot_test_same_column(loaded->group_atoms, imported->group_atoms, group_atoms * sizeof(AtomId)),
                         "the groups", index);
    snapshot_test_expect(tally, snapshot_test_same_column(loaded->bindings, imported->bindings, symbols * sizeof(RuntimeBinding)) &&
                                    snapshot_test_same_column(loaded->symbol_names, imported->symbol_names, symbols * sizeof(uint32_t)),
                         "the bindings and the symbols' global names", index);
    bool members = true;
    for (uint32_t m = 0; m < imported->member_count; m += 1)
    {
        members = members && snapshot_test_same_string(runtime_member_name(loaded, m), runtime_member_name(imported, m)) &&
                  loaded->members[m].offset == imported->members[m].offset;
    }
    snapshot_test_expect(tally, members, "the members' names", index);
    const RuntimeNames* a = &imported->global_names;
    const RuntimeNames* b = &loaded->global_names;
    const uint64_t count = a->count;
    bool globals = a->count == b->count && a->strings_length == b->strings_length && memcmp(a->strings, b->strings, a->strings_length) == 0 &&
                   snapshot_test_same_column(a->offset, b->offset, count * sizeof(uint32_t)) && snapshot_test_same_column(a->length, b->length, count * sizeof(uint32_t)) &&
                   snapshot_test_same_column(a->hash, b->hash, count * sizeof(uint64_t)) && snapshot_test_same_column(a->definition, b->definition, count * sizeof(SymbolId)) &&
                   snapshot_test_same_column(a->member, b->member, count * sizeof(uint32_t)) && snapshot_test_same_column(a->export, b->export, count * sizeof(uint32_t)) &&
                   snapshot_test_same_column(a->referenced, b->referenced, count) &&
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
    // libc.so.6's, then the loader's.
    for (uint32_t library = 0; library < 2; library += 1)
    {
        const ImportLibrary* x = library == 0 ? &imported->libc : &imported->loader;
        const ImportLibrary* y = library == 0 ? &loaded->libc : &loaded->loader;
        bool versions = x->version_count == y->version_count && snapshot_test_same_string(x->soname, y->soname);
        for (uint32_t v = 0; versions && v < x->version_count; v += 1)
        {
            versions = snapshot_test_same_string(x->versions[v].name, y->versions[v].name) && x->versions[v].hash == y->versions[v].hash &&
                       x->versions[v].index == y->versions[v].index && x->versions[v].base == y->versions[v].base;
        }
        snapshot_test_expect(tally, versions, library == 0 ? "libc.so.6's soname and versions" : "the loader's soname and versions", index);
        snapshot_test_expect(tally,
                             x->exports.count == y->exports.count && snapshot_test_same_column(x->exports.version, y->exports.version, x->exports.count * sizeof(uint16_t)) &&
                                 snapshot_test_same_column(x->exports.flags, y->exports.flags, x->exports.count),
                             library == 0 ? "libc.so.6's exports' versions and types" : "the loader's exports' versions and types", index);
    }
    bool paths = snapshot_test_same_string(imported->gcc_directory, loaded->gcc_directory) && snapshot_test_same_string(imported->gcc_versions, loaded->gcc_versions) &&
                 snapshot_test_same_string(imported->libc_script, loaded->libc_script) && snapshot_test_same_string(imported->libc_path, loaded->libc_path) &&
                 snapshot_test_same_string(imported->loader_path, loaded->loader_path) && snapshot_test_same_string(imported->abi_note, loaded->abi_note);
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

// Loads the snapshot of a kind's runtime in `directory` as `compiler`, into `runtime`.
static SnapshotStatus snapshot_test_load(Runtime* runtime, const char* directory, RuntimeKind kind, const RuntimeProbe* compiler)
{
    char path[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    snapshot_test_path(path, directory, snapshot_name(kind));
    return snapshot_load(runtime, path, kind, compiler, 0);
}

// The runtime written as a snapshot in a directory of its own, `name`, and loaded back, as the check `check`.
static bool snapshot_test_round_trip(const ExecutableTestDirectory* directory, const Runtime* imported, const RuntimeProbe* compiler, const char* check, const char* name)
{
    SnapshotTestTally tally = {
        .check = check,
    };
    char place[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    snapshot_test_path(place, directory->path, name);
    const bool written = snapshot_test_write(imported, place, compiler);
    snapshot_test_expect(&tally, written, "the snapshot written", 0);
    Runtime loaded;
    const SnapshotStatus status = written ? snapshot_test_load(&loaded, place, imported->kind, compiler) : SNAPSHOT_STATUS_MISSING;
    snapshot_test_expect(&tally, status == SNAPSHOT_STATUS_LOADED, "the snapshot loaded", status);
    if (status == SNAPSHOT_STATUS_LOADED)
    {
        snapshot_test_same_runtime(&tally, imported, &loaded, 0);
        printf("  %u probes, %u global names, %u exports, %u members, %u symbols: a %llu-byte snapshot\n", loaded.probe_count, loaded.global_names.count,
               loaded.libc.exports.count, loaded.member_count, loaded.link.symbols.count - 1, (unsigned long long)loaded.snapshot_bytes);
        // Another snapshot renamed over the file: the loaded runtime keeps the one it maps.
        snapshot_test_expect(&tally, snapshot_test_write(imported, place, compiler), "a second snapshot written over the first", 0);
        snapshot_test_same_runtime(&tally, imported, &loaded, 1);
        Runtime again;
        const SnapshotStatus second = snapshot_test_load(&again, place, imported->kind, compiler);
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
    snapshot_test_path(path, place, snapshot_name(imported->kind));
    if (!directory->keep)
    {
        unlink(path);
        rmdir(place);
    }
    return snapshot_test_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// The links

// Writes a bound link with a group of `lanes` lanes, as a dynamic PIE or, against a static runtime, as a static
// executable or a static PIE by its kind (static_test_write_with), from its atoms' bytes as binding left them (`saved`:
// the TLS relaxation and a static PIE's relaxed loads through the GOT rewrite them).
static bool snapshot_test_write_link(Link* link, const Bind* bind, const Runtime* runtime, const uint8_t* saved, char* path, uint32_t lanes, ExecutableResult* result)
{
    DynamicTestWrite write = { 0 };
    const bool reset = executable_test_reset(link, path);
    bool written = false;
    if (reset && runtime_kind_is_static(runtime->kind))
    {
        executable_test_restore(link, saved);
        written = static_test_write_with(&write, link, bind, runtime, snapshot_test_string(path), lanes, result);
    }
    else if (reset)
    {
        written = dynamic_test_write_with(&write, link, bind, runtime, snapshot_test_string(path), lanes, result);
    }
    return written;
}

// Binds a program against each runtime by `path` and writes it with 1 and 8 lanes, as a dynamic PIE, a static
// executable or a static PIE by the runtimes' kind: the same links, bindings and files.
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
    Scratch saved = scratch_begin(0);
    const uint8_t* saved_a = bound ? executable_test_save(&a.program.link, saved.arena) : 0;
    const uint8_t* saved_b = bound ? executable_test_save(&b.program.link, saved.arena) : 0;
    snapshot_test_expect(tally, !bound || (saved_a != 0 && saved_b != 0), "room for the atoms' bytes", index);
    const uint32_t lane_counts[] = { 1, 8 };
    for (uint32_t l = 0; bound && saved_a != 0 && saved_b != 0 && l < BUSTER_ARRAY_LENGTH(lane_counts); l += 1)
    {
        Scratch scratch = scratch_begin(saved.arena);
        ExecutableResult written_a;
        ExecutableResult written_b;
        const bool written = snapshot_test_write_link(&a.program.link, &a.bind, imported, saved_a, path_a, lane_counts[l], &written_a) &&
                             snapshot_test_write_link(&b.program.link, &b.bind, loaded, saved_b, path_b, lane_counts[l], &written_b);
        snapshot_test_expect(tally, written, name, lane_counts[l]);
        const String8 file_a = written ? executable_test_read(scratch.arena, path_a) : (String8){ 0 };
        const String8 file_b = written ? executable_test_read(scratch.arena, path_b) : (String8){ 0 };
        snapshot_test_expect(tally, written && file_a.pointer != 0 && file_a.length == file_b.length && memcmp(file_a.pointer, file_b.pointer, file_a.length) == 0,
                             "the same executable", index * 2 + l);
        snapshot_test_expect(tally, written && executable_test_runs(path_b, b.program.output, BUSTER_LINK_PROGRAM_OUTPUT_BYTES), "the loaded runtime's executable runs",
                             index * 2 + l);
        scratch_end(scratch);
    }
    scratch_end(saved);
    if (!directory->keep)
    {
        unlink(path_a);
        unlink(path_b);
    }
    bind_test_release(&a);
    bind_test_release(&b);
}

// test-bind's loader case (main reads _r_debug and __libc_stack_end, which only the loader exports) bound against a
// dynamic PIE's imported runtime and its loaded one by both paths: the same links and bindings, and the loaded
// runtime's executable runs with the case's status.
static void snapshot_test_loader_case(SnapshotTestTally* tally, const ExecutableTestDirectory* directory, const Runtime* imported, const Runtime* loaded)
{
    for (uint32_t path = 0; path < 2; path += 1)
    {
        const BindPath bind_path = path == 0 ? BIND_PATH_VECTOR : BIND_PATH_EXACT;
        BindTestCaseProgram a;
        BindTestCaseProgram b;
        bind_test_case_program(&a, imported, BIND_TEST_CASE_LOADER, bind_path, true);
        bind_test_case_program(&b, loaded, BIND_TEST_CASE_LOADER, bind_path, true);
        const bool bound = a.bound && b.bound;
        snapshot_test_expect(tally, bound && bind_test_same_links(&a.test.link, &b.test.link) && bind_test_same_binds(&a.test.bind, &b.test.bind),
                             "the loader's case: the same link and binding", path);
        char program[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
        snapshot_test_path(program, directory->path, "loader");
        Scratch scratch = scratch_begin(0);
        const uint8_t* saved = bound ? executable_test_save(&b.test.link, scratch.arena) : 0;
        ExecutableResult result;
        const bool written = saved != 0 && snapshot_test_write_link(&b.test.link, &b.test.bind, loaded, saved, program, 1, &result);
        uint8_t output[BUSTER_EXECUTABLE_TEST_OUTPUT_BYTES];
        uint64_t length = 0;
        int status = -1;
        const bool ran = written && executable_test_run(program, output, &length, &status);
        snapshot_test_expect(tally, ran && WIFEXITED(status) && WEXITSTATUS(status) == bind_test_cases[BIND_TEST_CASE_LOADER].status,
                             "the loader's case runs with its status", path);
        scratch_end(scratch);
        if (!directory->keep)
        {
            unlink(program);
        }
        bind_test_link_end(&a.test);
        bind_test_link_end(&b.test);
    }
}

// The links of programs bound against the imported runtime and against its snapshot loaded, as the check `check`, in a
// directory of its own, `name`.
static bool snapshot_test_links(const ExecutableTestDirectory* directory, const Runtime* imported, const RuntimeProbe* compiler, uint64_t seed, uint32_t functions,
                                const char* check, const char* name)
{
    SnapshotTestTally tally = {
        .check = check,
    };
    char place[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    snapshot_test_path(place, directory->path, name);
    Runtime loaded;
    const bool written = snapshot_test_write(imported, place, compiler);
    const SnapshotStatus status = written ? snapshot_test_load(&loaded, place, imported->kind, compiler) : SNAPSHOT_STATUS_MISSING;
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
    if (status == SNAPSHOT_STATUS_LOADED && imported->kind == RUNTIME_KIND_DYNAMIC)
    {
        snapshot_test_loader_case(&tally, directory, imported, &loaded);
    }
    printf("  %u programs bound and written against both runtimes\n", linked / 2);
    if (status == SNAPSHOT_STATUS_LOADED)
    {
        runtime_release(&loaded);
    }
    char path[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    snapshot_test_path(path, place, snapshot_name(imported->kind));
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
        const SnapshotResult result = runtime_open_as(&runtime, place, RUNTIME_KIND_DYNAMIC, compiler);
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
        const SnapshotResult result = runtime_open_as(&runtime, way == 0 ? 0 : none, RUNTIME_KIND_DYNAMIC, way == 0 ? compiler : 0);
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
    const SnapshotResult result = runtime_open_as(&runtime, under, RUNTIME_KIND_DYNAMIC, compiler);
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
        const SnapshotStatus first = written ? snapshot_test_load(&loaded, place, RUNTIME_KIND_DYNAMIC, &writer) : SNAPSHOT_STATUS_MISSING;
        snapshot_test_expect(&tally, first == SNAPSHOT_STATUS_LOADED, "loaded before the change", change);
        if (first == SNAPSHOT_STATUS_LOADED)
        {
            snapshot_test_expect(&tally, loaded.probe_count == runtime.probe_count && runtime_probes_current(&loaded), "the case's probes in it", change);
            runtime_release(&loaded);
        }
        snapshot_test_expect(&tally, snapshot_test_change(change, place, file, absent, probed), snapshot_test_change_names[change], change);
        const SnapshotStatus second = written ? snapshot_test_load(&loaded, place, RUNTIME_KIND_DYNAMIC, compiler) : SNAPSHOT_STATUS_MISSING;
        snapshot_test_expect(&tally, second == SNAPSHOT_STATUS_STALE, snapshot_test_change_names[change], second);
        if (second == SNAPSHOT_STATUS_LOADED)
        {
            runtime_release(&loaded);
        }
        // runtime_open imports the runtime and writes a snapshot of its own probes, which loads.
        Runtime opened;
        const SnapshotResult result = runtime_open_as(&opened, place, RUNTIME_KIND_DYNAMIC, compiler);
        snapshot_test_expect(&tally, result.status == SNAPSHOT_STATUS_STALE && result.error == RUNTIME_ERROR_NONE && result.written, "imported again, and written", result.status);
        if (result.error == RUNTIME_ERROR_NONE)
        {
            runtime_release(&opened);
        }
        const SnapshotStatus third = snapshot_test_load(&loaded, place, RUNTIME_KIND_DYNAMIC, compiler);
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

// Writes `count` bytes at `offset` of the file, all of them.
static bool snapshot_test_pwrite(int file, const uint8_t* bytes, uint64_t count, uint64_t offset)
{
    uint64_t done = 0;
    bool result = true;
    while (result && done < count)
    {
        const ssize_t written = pwrite(file, bytes + done, count - done, (off_t)(offset + done));
        result = written > 0;
        done += result ? (uint64_t)written : 0;
    }
    return result;
}

// Changes the file, which holds the snapshot `good`, in place into a damaged copy of it: its first `length` bytes and
// `extra` zeros after them, with the byte at `flip` (if it is below length) changed; or with `repair`, that copy back
// into `good`. Each writes only what differs, so that a case of a static runtime's 9 MB snapshot costs its damage, not
// the file.
static bool snapshot_test_edit(int file, String8 good, uint64_t length, uint64_t extra, uint64_t flip, bool repair)
{
    static const uint8_t zeros[64] = { 0 };
    const uint8_t* bytes = (const uint8_t*)good.pointer;
    const uint8_t changed = flip < length ? (uint8_t)(bytes[flip] ^ 0x5a) : 0;
    bool result = flip >= length || snapshot_test_pwrite(file, repair ? bytes + flip : &changed, 1, flip);
    if (length < good.length)
    {
        result = result && (repair ? snapshot_test_pwrite(file, bytes + length, good.length - length, length) : ftruncate(file, (off_t)length) == 0);
    }
    if (extra != 0)
    {
        result = result && (repair ? ftruncate(file, (off_t)good.length) == 0 : snapshot_test_pwrite(file, zeros, extra, good.length));
    }
    return result;
}

// runtime_open of a kind's runtime in a child, which an alarm ends if it blocks: whether it ended in time, having
// imported the runtime and, with `refused`, failed to rename the snapshot over what is at its path, or otherwise
// written it.
static bool snapshot_test_open_child(const char* place, RuntimeKind kind, const RuntimeProbe* compiler, bool refused)
{
    fflush(stdout);
    const pid_t child = fork();
    if (child == 0)
    {
        alarm(BUSTER_SNAPSHOT_TEST_SECONDS);
        Runtime runtime;
        const SnapshotResult result = runtime_open_as(&runtime, place, kind, compiler);
        const bool imported = result.status == SNAPSHOT_STATUS_MISSING && result.error == RUNTIME_ERROR_NONE;
        const bool written = refused ? !result.written && result.call != 0 && strcmp(result.call, "rename") == 0 : result.written;
        _exit(imported && written ? 0 : 1);
    }
    int status = 0;
    return child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

// The imported runtime's snapshot, of its kind, damaged in each way below, as the check `check` in a directory of its
// own, `name`. The damaged copies are made in place over the snapshot (snapshot_test_edit) and repaired after each
// load.
static bool snapshot_test_invalid(const ExecutableTestDirectory* directory, const Runtime* imported, const RuntimeProbe* compiler, const char* check, const char* name)
{
    SnapshotTestTally tally = {
        .check = check,
    };
    const RuntimeKind kind = imported->kind;
    char place[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    char path[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    snapshot_test_path(place, directory->path, name);
    snapshot_test_path(path, place, snapshot_name(kind));
    Scratch scratch = scratch_begin(0);
    const bool written = snapshot_test_write(imported, place, compiler);
    const String8 good = written ? executable_test_read(scratch.arena, path) : (String8){ 0 };
    snapshot_test_expect(&tally, good.pointer != 0 && good.length > sizeof(SnapshotHeader), "the snapshot written", 0);
    const uint8_t* bytes = (const uint8_t*)good.pointer;
    const uint64_t directory_bytes = good.pointer != 0 ? ((const SnapshotHeader*)bytes)->directory_bytes : 0;
    const int file = good.pointer != 0 ? open(path, O_RDWR | O_CLOEXEC) : -1;
    bool repaired = file >= 0;
    uint64_t cases = 0;
    // Cut short (a length for each 1/61 of the file, and around the header), one byte longer, and a byte of the directory
    // changed (at each 1/61 of it, and in each of the header's first bytes): the pieces' records are checked as binding
    // reads them (snapshot_test_damaged).
    for (uint64_t i = 0; file >= 0 && directory_bytes > 61 && i < 61 + 3 + 1 + 61 + sizeof(SnapshotHeader); i += 1)
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
            flip = directory_bytes * (i - 65) / 61 + (i - 65) % 61;
        }
        else
        {
            flip = i - 65 - 61;
        }
        const uint64_t cut = length < good.length ? length : good.length;
        const uint64_t changed = flip < good.length ? flip : UINT64_MAX;
        const bool damaged = snapshot_test_edit(file, good, cut, extra, changed, false);
        Runtime loaded;
        const SnapshotStatus status = damaged ? snapshot_load(&loaded, path, kind, compiler, 0) : SNAPSHOT_STATUS_LOADED;
        snapshot_test_expect(&tally, status == SNAPSHOT_STATUS_INVALID, "a damaged snapshot invalid", i);
        if (status == SNAPSHOT_STATUS_LOADED)
        {
            runtime_release(&loaded);
        }
        repaired = repaired && snapshot_test_edit(file, good, cut, extra, changed, true);
        cases += 1;
    }
    snapshot_test_expect(&tally, repaired, "each damaged copy made in place, and repaired", cases);
    // Headers that the hash passes (rehashed after the change) but that aren't this format's, or whose regions don't fit
    // the file: each checked by snapshot_valid_header alone. Only the header changes, so only it is written over the
    // snapshot, and the last stays there.
    uint8_t* edited = file >= 0 ? arena_push(scratch.arena, good.length, 64) : 0;
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
            header->counts.names_run = header->regions[SNAPSHOT_REGION_DATA].bytes;
            break;
        case 8:
            // Starting past the end.
            header->regions[SNAPSHOT_REGION_FILTER].offset = (good.length + 2 * BUSTER_SNAPSHOT_ALIGNMENT) & ~(uint64_t)(BUSTER_SNAPSHOT_ALIGNMENT - 1);
            break;
        default:
            // Ending before the directory's last region does, which its hash would then leave out.
            header->directory_bytes -= BUSTER_SNAPSHOT_ALIGNMENT;
            header->regions[BUSTER_SNAPSHOT_PIECE_REGIONS].offset -= BUSTER_SNAPSHOT_ALIGNMENT;
            break;
        }
        snapshot_hash(edited, header->directory_bytes, header->hash);
        const bool copied = snapshot_test_pwrite(file, edited, sizeof(SnapshotHeader), 0);
        Runtime loaded;
        const SnapshotStatus status = copied ? snapshot_load(&loaded, path, kind, compiler, 0) : SNAPSHOT_STATUS_LOADED;
        snapshot_test_expect(&tally, status == SNAPSHOT_STATUS_INVALID, "a header the hash passes, invalid", change);
        if (status == SNAPSHOT_STATUS_LOADED)
        {
            runtime_release(&loaded);
        }
        cases += 1;
    }
    if (file >= 0)
    {
        close(file);
    }
    // runtime_open replaces the last one, which then loads.
    Runtime opened;
    const SnapshotResult result = runtime_open_as(&opened, place, kind, compiler);
    snapshot_test_expect(&tally, result.status == SNAPSHOT_STATUS_INVALID && result.error == RUNTIME_ERROR_NONE && result.written, "an invalid snapshot replaced", result.status);
    if (result.error == RUNTIME_ERROR_NONE)
    {
        runtime_release(&opened);
    }
    Runtime loaded;
    const SnapshotStatus replaced = snapshot_load(&loaded, path, kind, compiler, 0);
    snapshot_test_expect(&tally, replaced == SNAPSHOT_STATUS_LOADED, "its replacement loaded", replaced);
    if (replaced == SNAPSHOT_STATUS_LOADED)
    {
        snapshot_test_same_runtime(&tally, imported, &loaded, 0);
        runtime_release(&loaded);
    }
    // A directory, then a FIFO, at the snapshot's path: imported without blocking (in a child, which an alarm ends if it
    // does), the directory left in place with nothing beside it, and the FIFO replaced by a snapshot.
    unlink(path);
    for (uint32_t way = 0; way < 2; way += 1)
    {
        const bool placed = way == 0 ? mkdir(path, 0700) == 0 : mkfifo(path, 0600) == 0;
        snapshot_test_expect(&tally, placed, way == 0 ? "a directory at the path" : "a FIFO at the path", way);
        snapshot_test_expect(&tally, snapshot_test_open_child(place, kind, compiler, way == 0), way == 0 ? "imported, not renamed over a directory" : "imported, a FIFO replaced",
                             way);
        bool named = false;
        struct stat status;
        snapshot_test_expect(&tally, snapshot_test_entries(place, snapshot_name(kind), &named) == 1 && named && stat(path, &status) == 0 &&
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
// Damaged pieces
//
// A snapshot whose directory is whole but one of whose pieces' records isn't loads: binding checks each piece when it
// first reads it (runtime_piece_current), so that a link reads only what this executable wrote.

// The table a piece region's column is a column of, by the mark of its records in `mark`.
static uint64_t snapshot_test_mark(ImportMark mark, uint32_t region)
{
    uint64_t result = mark.symbols;
    result = region >= SNAPSHOT_REGION_NAME_BYTES && region <= SNAPSHOT_REGION_NAME_HASH ? mark.names : result;
    result = region >= SNAPSHOT_REGION_ATOM_CLASS && region <= SNAPSHOT_REGION_ATOM_ADDRESS ? mark.atoms : result;
    result = region >= SNAPSHOT_REGION_FIXUP_OFFSET && region <= SNAPSHOT_REGION_FIXUP_KIND ? mark.fixups : result;
    result = region == SNAPSHOT_REGION_WIDE_ADDENDS ? mark.wide_addends : result;
    result = region == SNAPSHOT_REGION_PRIORITY_ATOM || region == SNAPSHOT_REGION_PRIORITY_VALUE ? mark.priorities : result;
    return result;
}

// Whether a piece's hash covers a region: what binding reads, not the atoms' addresses, the symbols' GOT slots, dynamic
// indices, versions and references (the linker's columns), or the bindings, which nothing reads of a loaded runtime.
static bool snapshot_test_hashed(uint32_t region)
{
    return region != SNAPSHOT_REGION_ATOM_ADDRESS && region != SNAPSHOT_REGION_SYMBOL_GOT_SLOT && region != SNAPSHOT_REGION_SYMBOL_DYNAMIC_INDEX &&
           region != SNAPSHOT_REGION_SYMBOL_VERSION && region != SNAPSHOT_REGION_SYMBOL_REFERENCE && region != SNAPSHOT_REGION_BINDINGS;
}

// The piece whose records hold byte `offset` of a snapshot (`header`, viewed as loaded in `view`), by the test's own
// reading of the layout: in a column its hash covers (snapshot_test_hashed), the piece of the record (piece 0 from
// record 0); in the data, the piece of the names run's byte or of the atoms' bytes. UINT32_MAX for a byte of the
// directory, of a column no hash covers, or of no piece's records (padding).
static uint32_t snapshot_test_piece_at(const SnapshotHeader* header, Runtime* view, uint64_t offset)
{
    SnapshotColumn columns[SNAPSHOT_REGIONS];
    snapshot_columns(view, &header->counts, columns);
    uint32_t result = UINT32_MAX;
    for (uint32_t r = BUSTER_SNAPSHOT_PIECE_REGIONS; r < SNAPSHOT_REGIONS; r += 1)
    {
        const SnapshotPlace place = header->regions[r];
        const bool inside = snapshot_test_hashed(r) && offset >= place.offset && offset - place.offset < place.bytes;
        const uint64_t within = inside ? offset - place.offset : 0;
        for (uint32_t p = 0; inside && p < header->counts.pieces; p += 1)
        {
            const ImportMark begin = view->pieces[p].begin;
            const ImportMark end = view->pieces[p].end;
            bool held = false;
            if (r == SNAPSHOT_REGION_DATA && within < header->counts.names_run_bytes)
            {
                held = within >= (p == 0 ? 0 : begin.name_bytes) && within < end.name_bytes;
            }
            else if (r == SNAPSHOT_REGION_DATA)
            {
                held = within >= view->piece_data[p] && within < view->piece_data[p + 1];
            }
            else
            {
                const uint64_t record = within / columns[r].element;
                held = record >= (p == 0 ? 0 : snapshot_test_mark(begin, r)) && record < snapshot_test_mark(end, r);
            }
            result = held ? p : result;
        }
    }
    return result;
}

// A byte of a piece's records: the first of its symbols' values, or else of its names' lengths, of its atoms' bytes or
// of its atoms' sizes.
static uint64_t snapshot_test_piece_byte(const SnapshotHeader* header, const Runtime* view, uint32_t piece)
{
    const ImportMark begin = view->pieces[piece].begin;
    const ImportMark end = view->pieces[piece].end;
    uint64_t result = header->regions[SNAPSHOT_REGION_ATOM_SIZE].offset + sizeof(uint64_t) * begin.atoms;
    result = view->piece_data[piece] != view->piece_data[piece + 1] ? header->regions[SNAPSHOT_REGION_DATA].offset + view->piece_data[piece] : result;
    result = begin.names != end.names ? header->regions[SNAPSHOT_REGION_NAME_LENGTH].offset + sizeof(uint32_t) * begin.names : result;
    result = begin.symbols != end.symbols ? header->regions[SNAPSHOT_REGION_SYMBOL_VALUE].offset + sizeof(uint64_t) * begin.symbols : result;
    return result;
}

// The snapshot at `path`, whose file (`file`) holds `good`, with the byte at `flip` changed in place (none for
// UINT64_MAX), loaded as `compiler`: its status. The caller changes the byte back (snapshot_test_edit's `repair`) once it
// has released the runtime, whose private mapping would see the repair in the pages it hasn't copied.
static SnapshotStatus snapshot_test_damage(Runtime* loaded, int file, const char* path, String8 good, uint64_t flip, RuntimeKind kind, const RuntimeProbe* compiler)
{
    const bool damaged = file >= 0 && snapshot_test_edit(file, good, good.length, 0, flip, false);
    return damaged ? snapshot_load(loaded, path, kind, compiler, 0) : SNAPSHOT_STATUS_MISSING;
}

// A runtime's snapshot with a byte of its pieces' records changed, one at a time: at each 1/61 of them, and the first
// and last of each piece region. Each loads, and every piece is current but the one whose records hold the byte
// (snapshot_test_piece_at), if any. Then binding a program by each path: a program that joins a damaged piece
// (Scrt1.o's, crt1.o's or rcrt1.o's, which bind_begin joins; crtendS.o's or crtend.o's, which it checks first; or a
// member the program joins) must fail with BIND_ERROR_SNAPSHOT, and one that doesn't (a member it doesn't join) must
// bind as against the snapshot whole. After a failure, snapshot_discard and runtime_open give a runtime imported again,
// whose new snapshot loads.
static bool snapshot_test_damaged(const ExecutableTestDirectory* directory, const Runtime* imported, const RuntimeProbe* compiler, uint64_t seed, const char* check,
                                  const char* name)
{
    SnapshotTestTally tally = {
        .check = check,
    };
    const RuntimeKind kind = imported->kind;
    char place[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    char path[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    snapshot_test_path(place, directory->path, name);
    snapshot_test_path(path, place, snapshot_name(kind));
    Scratch scratch = scratch_begin(0);
    const bool written = snapshot_test_write(imported, place, compiler);
    const String8 good = written ? executable_test_read(scratch.arena, path) : (String8){ 0 };
    snapshot_test_expect(&tally, good.pointer != 0 && good.length > sizeof(SnapshotHeader), "the snapshot written", 0);
    // A copy of the good snapshot viewed as loaded, for the test's own reading of its layout.
    uint8_t* copy = good.pointer != 0 ? arena_push(scratch.arena, good.length, 64) : 0;
    Runtime view = { 0 };
    if (copy != 0)
    {
        memcpy(copy, good.pointer, good.length);
        snapshot_view(&view, copy, good.length);
    }
    const SnapshotHeader* header = (const SnapshotHeader*)copy;
    const uint32_t pieces = copy != 0 ? header->counts.pieces : 0;
    const uint32_t regions = SNAPSHOT_REGIONS - BUSTER_SNAPSHOT_PIECE_REGIONS;
    // Each damaged copy is made in place over the snapshot and repaired after its runtime is released
    // (snapshot_test_damage).
    const int file = good.pointer != 0 ? open(path, O_RDWR | O_CLOEXEC) : -1;
    bool repaired = file >= 0;
    uint64_t cases = 0;
    uint64_t harmless = 0;
    for (uint64_t i = 0; copy != 0 && i < 61 + 2 * (uint64_t)regions; i += 1)
    {
        const uint64_t first = header->directory_bytes;
        const SnapshotPlace region = header->regions[BUSTER_SNAPSHOT_PIECE_REGIONS + (i - 61) / 2 % regions];
        const uint64_t edge = region.bytes == 0 ? first : (i - 61) % 2 == 0 ? region.offset : region.offset + region.bytes - 1;
        const uint64_t flip = i < 61 ? first + (good.length - first) * i / 61 : edge;
        const uint32_t expected = snapshot_test_piece_at(header, &view, flip);
        Runtime loaded;
        const SnapshotStatus status = snapshot_test_damage(&loaded, file, path, good, flip, kind, compiler);
        snapshot_test_expect(&tally, status == SNAPSHOT_STATUS_LOADED, "a snapshot with a piece damaged loads", i);
        uint32_t wrong = UINT32_MAX;
        for (uint32_t p = 0; status == SNAPSHOT_STATUS_LOADED && p < pieces; p += 1)
        {
            wrong = runtime_piece_current(&loaded, p) == (p == expected) && wrong == UINT32_MAX ? p : wrong;
        }
        snapshot_test_expect(&tally, wrong == UINT32_MAX, "only the damaged piece not current", i * 1000000 + wrong);
        if (status == SNAPSHOT_STATUS_LOADED)
        {
            runtime_release(&loaded);
        }
        repaired = repaired && snapshot_test_edit(file, good, good.length, 0, flip, true);
        cases += 1;
        harmless += expected == UINT32_MAX ? 1 : 0;
    }
    // Binding: a program bound against the whole snapshot, which says which members join. Binding copies what it reads
    // into the program's link, so that the whole runtime isn't read again while the file under its mapping is damaged.
    LinkProgramOptions all[BUSTER_LINK_TEST_PROGRAMS];
    link_test_program_options(all, seed, 1);
    const LinkProgramOptions options = all[5];
    Runtime whole;
    const SnapshotStatus whole_status = snapshot_test_damage(&whole, file, path, good, UINT64_MAX, kind, compiler);
    BindTestProgram reference = { 0 };
    const bool referenced = whole_status == SNAPSHOT_STATUS_LOADED && bind_test_program(&reference, &whole, options, BIND_PATH_VECTOR);
    snapshot_test_expect(&tally, referenced, "the program bound against the whole snapshot", 0);
    uint32_t joined = UINT32_MAX;
    uint32_t unjoined = UINT32_MAX;
    for (uint32_t m = 0; referenced && m < imported->member_count; m += 1)
    {
        const bool in = (reference.bind.members_joined[m / 64] >> (m % 64) & 1) != 0;
        joined = in && joined == UINT32_MAX ? m : joined;
        unjoined = !in && unjoined == UINT32_MAX ? m : unjoined;
    }
    // The damaged pieces: the first crt object's, the last but one's (crtendS.o's or crtend.o's), a member that joins and
    // one that doesn't (where there is one).
    const uint32_t damaged[4] = {
        RUNTIME_OBJECT_START,
        RUNTIME_OBJECT_END,
        joined != UINT32_MAX ? RUNTIME_OBJECTS + joined : UINT32_MAX,
        unjoined != UINT32_MAX ? RUNTIME_OBJECTS + unjoined : UINT32_MAX,
    };
    for (uint32_t d = 0; referenced && copy != 0 && d < BUSTER_ARRAY_LENGTH(damaged); d += 1)
    {
        const uint32_t piece = damaged[d];
        const uint64_t flip = piece != UINT32_MAX ? snapshot_test_piece_byte(header, &view, piece) : 0;
        snapshot_test_expect(&tally, piece == UINT32_MAX || snapshot_test_piece_at(header, &view, flip) == piece, "the byte changed is the piece's", d);
        for (uint32_t way = 0; piece != UINT32_MAX && way < 2; way += 1)
        {
            Runtime loaded;
            const SnapshotStatus status = snapshot_test_damage(&loaded, file, path, good, flip, kind, compiler);
            BindTestProgram bound = { 0 };
            const bool succeeded = status == SNAPSHOT_STATUS_LOADED && bind_test_program(&bound, &loaded, options, way == 0 ? BIND_PATH_VECTOR : BIND_PATH_EXACT);
            const bool reads = d < 3;
            const bool right = reads ? !succeeded && bound.bind.error == BIND_ERROR_SNAPSHOT
                                     : succeeded && bind_test_same_links(&bound.program.link, &reference.program.link) && bind_test_same_binds(&bound.bind, &reference.bind);
            snapshot_test_expect(&tally, status == SNAPSHOT_STATUS_LOADED && right, reads ? "a damaged piece the link reads fails the binding" : "a damaged piece it doesn't, binds",
                                 d * 2 + way);
            if (status == SNAPSHOT_STATUS_LOADED)
            {
                bind_test_release(&bound);
                runtime_release(&loaded);
            }
            repaired = repaired && snapshot_test_edit(file, good, good.length, 0, flip, true);
        }
        printf("  %s damaged: %s\n", d == 0 ? "the first crt object" : d == 1 ? "crtend's" : d == 2 ? "a member the program joins" : "a member it doesn't", piece == UINT32_MAX ? "none to damage" : "checked");
    }
    snapshot_test_expect(&tally, repaired, "each damaged copy made in place, and repaired", cases);
    // After a binding found a damaged piece: the snapshot discarded, and the runtime opened again, imported, written.
    const uint64_t first_piece = copy != 0 ? snapshot_test_piece_byte(header, &view, 0) : UINT64_MAX;
    snapshot_test_expect(&tally, file >= 0 && snapshot_test_edit(file, good, good.length, 0, first_piece, false) && snapshot_discard(place, kind) && access(path, F_OK) != 0,
                         "the damaged snapshot discarded", 0);
    if (file >= 0)
    {
        close(file);
    }
    Runtime opened;
    const SnapshotResult result = runtime_open_as(&opened, place, kind, compiler);
    snapshot_test_expect(&tally, result.status == SNAPSHOT_STATUS_MISSING && result.error == RUNTIME_ERROR_NONE && result.written, "imported again, and written", result.status);
    if (result.error == RUNTIME_ERROR_NONE)
    {
        runtime_release(&opened);
    }
    Runtime again;
    const SnapshotStatus status = snapshot_test_load(&again, place, kind, compiler);
    snapshot_test_expect(&tally, status == SNAPSHOT_STATUS_LOADED, "its new snapshot loaded", status);
    if (status == SNAPSHOT_STATUS_LOADED)
    {
        snapshot_test_same_runtime(&tally, imported, &again, 0);
        runtime_release(&again);
    }
    if (referenced)
    {
        bind_test_release(&reference);
    }
    if (whole_status == SNAPSHOT_STATUS_LOADED)
    {
        runtime_release(&whole);
    }
    printf("  %llu snapshots with a byte of the pieces' records changed, %llu of them in no piece's\n", (unsigned long long)cases, (unsigned long long)harmless);
    scratch_end(scratch);
    if (!directory->keep)
    {
        unlink(path);
        rmdir(place);
    }
    return snapshot_test_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// The kinds
//
// Each kind's runtime has a snapshot of its own beside the others' (snapshot_name), and the loader checks the kind.

// runtime_open_static and runtime_open_static_pie in a directory of their own: each imported and written, then loaded,
// with its kind's records; runtime_open beside them, the three snapshots side by side; each kind's snapshot loaded as
// each other kind, stale; and each kind's snapshot renamed to the next kind's name in turn (a dynamic PIE's to a static
// executable's, that to a static PIE's, that to a dynamic PIE's), stale there, imported again and written over.
// `runtimes` holds each kind's imported runtime, by RuntimeKind.
static bool snapshot_test_kinds(const ExecutableTestDirectory* directory, const Runtime* const* runtimes, const RuntimeProbe* compiler)
{
    SnapshotTestTally tally = {
        .check = "kinds",
    };
    char place[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    char paths[RUNTIME_KINDS][BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    snapshot_test_path(place, directory->path, "kinds");
    for (uint32_t kind = 0; kind < RUNTIME_KINDS; kind += 1)
    {
        snapshot_test_path(paths[kind], place, snapshot_name((RuntimeKind)kind));
    }
    // The kinds whose entry points open them here; the dynamic PIE's after them.
    const RuntimeKind others[] = {
        RUNTIME_KIND_STATIC,
        RUNTIME_KIND_STATIC_PIE,
        RUNTIME_KIND_SHARED,
    };
    for (uint32_t s = 0; s < BUSTER_ARRAY_LENGTH(others); s += 1)
    {
        const RuntimeKind kind = others[s];
        for (uint32_t round = 0; round < 2; round += 1)
        {
            // The entry points, which take this executable's identity (`compiler`) themselves.
            Runtime runtime;
            const SnapshotResult result = kind == RUNTIME_KIND_STATIC       ? runtime_open_static(&runtime, place)
                                          : kind == RUNTIME_KIND_STATIC_PIE ? runtime_open_static_pie(&runtime, place)
                                                                            : runtime_open_shared(&runtime, place);
            const SnapshotStatus expected = round == 0 ? SNAPSHOT_STATUS_MISSING : SNAPSHOT_STATUS_LOADED;
            snapshot_test_expect(&tally, result.status == expected && result.error == RUNTIME_ERROR_NONE && (round == 1 || result.written),
                                 round == 0 ? "a kind's runtime imported, and written" : "loaded, the second time", s * 2 + round);
            if (result.error == RUNTIME_ERROR_NONE)
            {
                snapshot_test_expect(&tally, runtime.kind == kind, "the kind's runtime", s * 2 + round);
                if (round == 1)
                {
                    snapshot_test_same_runtime(&tally, runtimes[kind], &runtime, s * 2 + round);
                }
                runtime_release(&runtime);
            }
        }
    }
    Runtime runtime;
    SnapshotResult result = runtime_open_as(&runtime, place, RUNTIME_KIND_DYNAMIC, compiler);
    snapshot_test_expect(&tally, result.status == SNAPSHOT_STATUS_MISSING && result.error == RUNTIME_ERROR_NONE && result.written, "the dynamic runtime's beside them", result.status);
    if (result.error == RUNTIME_ERROR_NONE)
    {
        runtime_release(&runtime);
    }
    bool named = false;
    bool every = snapshot_test_entries(place, snapshot_name(RUNTIME_KIND_STATIC_PIE), &named) == RUNTIME_KINDS && named;
    for (uint32_t kind = 0; kind < RUNTIME_KINDS; kind += 1)
    {
        every = every && access(paths[kind], F_OK) == 0;
    }
    snapshot_test_expect(&tally, every, "a snapshot of each kind, and no other file", 0);
    // Each kind's snapshot loaded as each other kind: stale.
    for (uint32_t written = 0; written < RUNTIME_KINDS; written += 1)
    {
        for (uint32_t asked = 0; asked < RUNTIME_KINDS; asked += 1)
        {
            if (asked != written)
            {
                Runtime loaded;
                const SnapshotStatus status = snapshot_load(&loaded, paths[written], (RuntimeKind)asked, compiler, 0);
                snapshot_test_expect(&tally, status == SNAPSHOT_STATUS_STALE, "another kind's snapshot stale", written * RUNTIME_KINDS + asked);
                if (status == SNAPSHOT_STATUS_LOADED)
                {
                    runtime_release(&loaded);
                }
            }
        }
    }
    // Each kind's snapshot at the next kind's name: stale, imported again, and written over.
    const RuntimeKind order[RUNTIME_KINDS] = {
        RUNTIME_KIND_DYNAMIC,
        RUNTIME_KIND_STATIC,
        RUNTIME_KIND_STATIC_PIE,
        RUNTIME_KIND_SHARED,
    };
    for (uint32_t way = 0; way < RUNTIME_KINDS; way += 1)
    {
        const RuntimeKind from = order[way];
        const RuntimeKind kind = order[(way + 1) % RUNTIME_KINDS];
        const bool moved = rename(paths[from], paths[kind]) == 0;
        result = runtime_open_as(&runtime, place, kind, compiler);
        snapshot_test_expect(&tally, moved && result.status == SNAPSHOT_STATUS_STALE && result.error == RUNTIME_ERROR_NONE && result.written,
                             "another kind's snapshot stale, and written over", way);
        if (result.error == RUNTIME_ERROR_NONE)
        {
            runtime_release(&runtime);
        }
        Runtime loaded;
        const SnapshotStatus status = snapshot_test_load(&loaded, place, kind, compiler);
        snapshot_test_expect(&tally, status == SNAPSHOT_STATUS_LOADED, "then loaded", way);
        if (status == SNAPSHOT_STATUS_LOADED)
        {
            snapshot_test_same_runtime(&tally, runtimes[kind], &loaded, way);
            runtime_release(&loaded);
        }
    }
    if (!directory->keep)
    {
        for (uint32_t kind = 0; kind < RUNTIME_KINDS; kind += 1)
        {
            unlink(paths[kind]);
        }
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
            const SnapshotResult result = runtime_open_as(&runtime, place, RUNTIME_KIND_DYNAMIC, compiler);
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
    const SnapshotStatus status = snapshot_test_load(&loaded, place, RUNTIME_KIND_DYNAMIC, compiler);
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
        bool passed = snapshot_test_round_trip(&directory, &runtime, &compiler, "round trip", "round-trip");
        // A static executable's runtime, whose snapshot has the archives' members, references and groups, a static
        // PIE's, with the same archives behind its own crt objects, and a shared library's, a dynamic PIE's without its
        // start object.
        Runtime static_runtime;
        Runtime static_pie_runtime;
        Runtime shared_runtime;
        const RuntimeError static_imported = runtime_import_static(&static_runtime);
        const RuntimeError static_pie_imported = runtime_import_static_pie(&static_pie_runtime);
        const RuntimeError shared_imported = runtime_import_shared(&shared_runtime);
        const bool statics = static_imported == RUNTIME_ERROR_NONE;
        const bool static_pies = static_pie_imported == RUNTIME_ERROR_NONE;
        const bool shareds = shared_imported == RUNTIME_ERROR_NONE;
        printf("%s", !statics ? "static round trip: the static runtime didn't import\n" : "");
        printf("%s", !static_pies ? "static PIE round trip: the static PIE's runtime didn't import\n" : "");
        printf("%s", !shareds ? "shared round trip: the shared library's runtime didn't import\n" : "");
        passed = statics && snapshot_test_round_trip(&directory, &static_runtime, &compiler, "static round trip", "static-round-trip") && passed;
        passed = static_pies && snapshot_test_round_trip(&directory, &static_pie_runtime, &compiler, "static PIE round trip", "static-pie-round-trip") && passed;
        passed = shareds && snapshot_test_round_trip(&directory, &shared_runtime, &compiler, "shared round trip", "shared-round-trip") && passed;
        passed = snapshot_test_links(&directory, &runtime, &compiler, seed, (uint32_t)functions, "links", "links") && passed;
        passed = statics && snapshot_test_links(&directory, &static_runtime, &compiler, seed, (uint32_t)functions, "static links", "static-links") && passed;
        passed = static_pies && snapshot_test_links(&directory, &static_pie_runtime, &compiler, seed, (uint32_t)functions, "static PIE links", "static-pie-links") && passed;
        passed = snapshot_test_opening(&directory, &runtime, &compiler) && passed;
        const Runtime* const runtimes[RUNTIME_KINDS] = {
            [RUNTIME_KIND_DYNAMIC] = &runtime,
            [RUNTIME_KIND_STATIC] = &static_runtime,
            [RUNTIME_KIND_STATIC_PIE] = &static_pie_runtime,
            [RUNTIME_KIND_SHARED] = &shared_runtime,
        };
        passed = statics && static_pies && shareds && snapshot_test_kinds(&directory, runtimes, &compiler) && passed;
        passed = snapshot_test_staleness(&directory, &runtime, &compiler) && passed;
        passed = snapshot_test_invalid(&directory, &runtime, &compiler, "invalid files", "invalid") && passed;
        passed = static_pies && snapshot_test_invalid(&directory, &static_pie_runtime, &compiler, "static PIE invalid files", "static-pie-invalid") && passed;
        passed = snapshot_test_damaged(&directory, &runtime, &compiler, seed, "damaged pieces", "damaged") && passed;
        passed = statics && snapshot_test_damaged(&directory, &static_runtime, &compiler, seed, "static damaged pieces", "static-damaged") && passed;
        passed = static_pies && snapshot_test_damaged(&directory, &static_pie_runtime, &compiler, seed, "static PIE damaged pieces", "static-pie-damaged") && passed;
        passed = snapshot_test_concurrency(&directory, &runtime, &compiler) && passed;
        passed = snapshot_test_directories() && passed;
        if (statics)
        {
            runtime_release(&static_runtime);
        }
        if (static_pies)
        {
            runtime_release(&static_pie_runtime);
        }
        if (shareds)
        {
            runtime_release(&shared_runtime);
        }
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
