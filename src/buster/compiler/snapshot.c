// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The runtime snapshot (docs/object-writer-and-linker.md, "The runtime snapshot"): what runtime_import makes of the C
// runtime, kept in a file in the cache directory, so that a link maps one file instead of finding, reading and
// importing the runtime's. runtime_open loads the snapshot if it still describes the system, and otherwise imports the
// runtime and writes a new snapshot.
//
// The file holds the runtime's records as this executable lays them out in memory: a header, the records that hold
// pointers (from the second page), the strings they point into, then the columns, each at a multiple of 64 bytes.
//   - Validation: the file must have this format's magic, version and header, and the size its header gives; its hash
//     must be its bytes' (executable.c's chunk hash, over the file with the hash's field as zeros); this very
//     executable must have written it (the identity of /proc/self/exe: st_dev, st_ino, st_size, st_mtim and st_ctim,
//     which a rebuild changes); and every path the import probed must be as it was (runtime_probes_current). So the
//     layouts it holds are this executable's own, and since the hash covers every byte, a file that passes is what it
//     wrote: a torn, truncated or corrupt one is rebuilt rather than trusted, and none needs an fsync before its rename.
//   - Loading: the file is mapped privately, and the runtime's columns point into the mapping. The records with pointers
//     (the probes, libc.so.6's versions, libc_nonshared.a's members, and the link's names' and atoms' bytes) hold
//     offsets into the strings plus 1, 0 for none, and are rebased where they are, which copies only their pages; the
//     rest is only read.
//   - Writing: into a new file beside the snapshot, under a random name, then renamed over it: a process that has the
//     old one mapped keeps it whole, and two compilers that rebuild it at once write the same bytes, the last rename
//     winning.
//
// main.c includes this file after executable.c, whose hash it uses. Layout, in this order:
//   - API: SnapshotStatus, SnapshotResult and the entry points' prototypes.
//   - The file: its header, regions and columns.
//   - Writing.
//   - Loading.
//   - runtime_open.

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// OS-dependent includes
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/random.h>
#include <sys/stat.h>

// ---------------------------------------------------------------------------------------------------------------------
// API

typedef enum SnapshotStatus
{
    // The snapshot described the system, and the runtime was loaded from it.
    SNAPSHOT_STATUS_LOADED,
    // There was none, it couldn't be opened or mapped, or there is no cache directory: the runtime was imported.
    SNAPSHOT_STATUS_MISSING,
    // It no longer described the system: another executable wrote it, or a path the import probed changed.
    SNAPSHOT_STATUS_STALE,
    // It wasn't a snapshot of this format: its magic, version, header or size, its regions or its hash were wrong.
    SNAPSHOT_STATUS_INVALID,
} SnapshotStatus;

// A load's phases, which snapshot_load can time (for benchmark-snapshot).
typedef enum SnapshotPhase
{
    // open, fstat, mmap and close.
    SNAPSHOT_PHASE_MAP,
    // The header's checks and the hash, which reads the whole file.
    SNAPSHOT_PHASE_HASH,
    // The columns pointed into the mapping and the records with pointers rebased.
    SNAPSHOT_PHASE_VIEW,
    // A stat of every path the import probed.
    SNAPSHOT_PHASE_PROBES,
    SNAPSHOT_PHASES,
} SnapshotPhase;

typedef struct SnapshotResult SnapshotResult;
struct SnapshotResult
{
    SnapshotStatus status;
    // For a runtime that was imported: the import's outcome, whether its snapshot was written, and if not, the call
    // that failed and errno.
    RuntimeError error;
    bool written;
    const char* call;
    int system_error;
};

// The cache directory, as the XDG base directory specification has it: $XDG_CACHE_HOME/buster, or without it
// $HOME/.cache/buster. False, with an empty path, if neither is set to an absolute path or the path doesn't fit.
static __attribute__((unused)) bool snapshot_directory(char* path, uint64_t capacity);
// The runtime: loaded from the snapshot in `directory` if it describes the system, otherwise imported and its snapshot
// written there, the directory made if it is missing. With no directory (0), imported. A snapshot that can't be written
// doesn't fail the import. runtime_release frees the runtime either way.
static __attribute__((unused)) SnapshotResult runtime_open(Runtime* runtime, const char* directory);

// ---------------------------------------------------------------------------------------------------------------------
// The file

#define BUSTER_SNAPSHOT_MAGIC 0x504e535254535542ull
#define BUSTER_SNAPSHOT_VERSION 1
#define BUSTER_SNAPSHOT_NAME "runtime.snapshot"
#define BUSTER_SNAPSHOT_ALIGNMENT 64
// The records with pointers start a page of their own, so that rebasing them copies a page (or as few as they need),
// and not the header's.
#define BUSTER_SNAPSHOT_PAGE_BYTES 4096
// The new file's name, in the snapshot's directory: the prefix, the process id, a dash and 16 random hexadecimal
// digits. And how many random names to try before giving up.
#define BUSTER_SNAPSHOT_TEMPORARY_PREFIX ".buster-snapshot-"
#define BUSTER_SNAPSHOT_NAME_ATTEMPTS 16
// The writer's arena: the strings and the file's image.
#define BUSTER_SNAPSHOT_WRITE_BYTES (256ull << 20)

// The runtime's strings that the header points to, by their place in it.
typedef enum SnapshotPath
{
    SNAPSHOT_PATH_GCC_DIRECTORY,
    SNAPSHOT_PATH_GCC_VERSIONS,
    // RUNTIME_OBJECTS of them, in RuntimeObject order.
    SNAPSHOT_PATH_OBJECTS,
    SNAPSHOT_PATH_LIBC_SCRIPT = SNAPSHOT_PATH_OBJECTS + RUNTIME_OBJECTS,
    SNAPSHOT_PATH_LIBC,
    SNAPSHOT_PATH_NONSHARED,
    SNAPSHOT_PATH_LOADER,
    SNAPSHOT_PATH_SONAME,
    SNAPSHOT_PATH_ABI_NOTE,
    SNAPSHOT_PATHS,
} SnapshotPath;

// The file's regions, in its order: the records with pointers, the strings, then the columns.
typedef enum SnapshotRegion
{
    SNAPSHOT_REGION_PROBES,
    SNAPSHOT_REGION_VERSIONS,
    SNAPSHOT_REGION_MEMBERS,
    SNAPSHOT_REGION_NAME_BYTES,
    SNAPSHOT_REGION_ATOM_BYTES,
    SNAPSHOT_REGION_STRINGS,
    SNAPSHOT_REGION_NAME_LENGTH,
    SNAPSHOT_REGION_NAME_HASH,
    SNAPSHOT_REGION_ATOM_CLASS,
    SNAPSHOT_REGION_ATOM_ALIGNMENT,
    SNAPSHOT_REGION_ATOM_UNIT,
    SNAPSHOT_REGION_ATOM_SIZE,
    SNAPSHOT_REGION_ATOM_FIXUP_BEGIN,
    SNAPSHOT_REGION_ATOM_FIXUP_COUNT,
    SNAPSHOT_REGION_ATOM_ADDRESS,
    SNAPSHOT_REGION_SYMBOL_NAME,
    SNAPSHOT_REGION_SYMBOL_ATOM,
    SNAPSHOT_REGION_SYMBOL_VALUE,
    SNAPSHOT_REGION_SYMBOL_SIZE,
    SNAPSHOT_REGION_SYMBOL_FLAGS,
    SNAPSHOT_REGION_SYMBOL_SOURCE,
    SNAPSHOT_REGION_SYMBOL_GOT_SLOT,
    SNAPSHOT_REGION_SYMBOL_DYNAMIC_INDEX,
    SNAPSHOT_REGION_SYMBOL_VERSION,
    SNAPSHOT_REGION_SYMBOL_REFERENCE,
    SNAPSHOT_REGION_FIXUP_OFFSET,
    SNAPSHOT_REGION_FIXUP_TARGET,
    SNAPSHOT_REGION_FIXUP_ADDEND,
    SNAPSHOT_REGION_FIXUP_KIND,
    SNAPSHOT_REGION_WIDE_ADDENDS,
    SNAPSHOT_REGION_PIECES,
    SNAPSHOT_REGION_BINDINGS,
    SNAPSHOT_REGION_SYMBOL_NAMES,
    SNAPSHOT_REGION_GLOBAL_OFFSET,
    SNAPSHOT_REGION_GLOBAL_LENGTH,
    SNAPSHOT_REGION_GLOBAL_HASH,
    SNAPSHOT_REGION_GLOBAL_DEFINITION,
    SNAPSHOT_REGION_GLOBAL_EXPORT,
    SNAPSHOT_REGION_GLOBAL_REFERENCED,
    SNAPSHOT_REGION_GLOBAL_LINKER,
    SNAPSHOT_REGION_INDEX_START,
    SNAPSHOT_REGION_INDEX_ORDER,
    SNAPSHOT_REGION_INDEX_HASH,
    SNAPSHOT_REGION_FILTER,
    SNAPSHOT_REGION_EXPORT_VERSION,
    SNAPSHOT_REGION_EXPORT_FLAGS,
    SNAPSHOT_REGIONS,
} SnapshotRegion;

// The records with pointers, which the loader rebases: the regions before the strings.
#define BUSTER_SNAPSHOT_POINTER_REGIONS SNAPSHOT_REGION_STRINGS

// A string, by its offset in the strings plus 1 (0 for none) and its length. Each is followed by a NUL there.
typedef struct SnapshotString SnapshotString;
struct SnapshotString
{
    uint64_t offset;
    uint64_t length;
};

typedef struct SnapshotPlace SnapshotPlace;
struct SnapshotPlace
{
    uint64_t offset;
    uint64_t bytes;
};

// How many records of each kind the runtime has, and where the names run and the global names' strings are in the
// strings.
typedef struct SnapshotCounts SnapshotCounts;
struct SnapshotCounts
{
    uint32_t probes;
    uint32_t versions;
    uint32_t members;
    // Each of the link's tables, record 0 among them.
    uint32_t names;
    uint32_t atoms;
    uint32_t symbols;
    uint32_t fixups;
    uint32_t wide_addends;
    uint32_t pieces;
    uint32_t globals;
    uint32_t bucket_bits;
    uint32_t filter_bits;
    uint32_t exports;
    uint32_t nonshared_unit;
    uint64_t names_run;
    uint64_t names_run_bytes;
    uint64_t global_strings;
    uint64_t global_strings_bytes;
};

typedef struct SnapshotHeader SnapshotHeader;
struct SnapshotHeader
{
    uint64_t magic;
    uint32_t version;
    uint32_t header_bytes;
    uint64_t file_bytes;
    uint8_t hash[16];
    // The executable that wrote it: an identity alone, without a path.
    RuntimeProbe compiler;
    SnapshotCounts counts;
    SnapshotString paths[SNAPSHOT_PATHS];
    SnapshotPlace regions[SNAPSHOT_REGIONS];
};

_Static_assert(sizeof(SnapshotHeader) <= BUSTER_SNAPSHOT_PAGE_BYTES, "the header fits in the first page");

// Where the runtime keeps a region's column (or array of records), and the size and number of its elements.
typedef struct SnapshotColumn SnapshotColumn;
struct SnapshotColumn
{
    void** column;
    uint64_t element;
    uint64_t count;
};

// Each region's column in `runtime`, with `counts`' numbers of elements. The strings have no column of their own: their
// bytes come from several.
static void snapshot_columns(Runtime* runtime, const SnapshotCounts* counts, SnapshotColumn* columns)
{
    Link* link = &runtime->link;
    RuntimeNames* names = &runtime->global_names;
    // A header's counts may be anything until it is checked (snapshot_valid_header).
    const uint64_t starts = counts->bucket_bits < 32 ? ((uint64_t)1 << counts->bucket_bits) + 1 : 0;
    const uint64_t filter = counts->filter_bits < 32 ? (uint64_t)1 << counts->filter_bits : 0;
    columns[SNAPSHOT_REGION_PROBES] = (SnapshotColumn){ (void**)&runtime->probes, sizeof(RuntimeProbe), counts->probes };
    columns[SNAPSHOT_REGION_VERSIONS] = (SnapshotColumn){ (void**)&runtime->libc.versions, sizeof(ImportVersion), counts->versions };
    columns[SNAPSHOT_REGION_MEMBERS] = (SnapshotColumn){ (void**)&runtime->nonshared.members, sizeof(ImportMember), counts->members };
    columns[SNAPSHOT_REGION_NAME_BYTES] = (SnapshotColumn){ (void**)&link->names.bytes, sizeof(*link->names.bytes), counts->names };
    columns[SNAPSHOT_REGION_ATOM_BYTES] = (SnapshotColumn){ (void**)&link->atoms.bytes, sizeof(*link->atoms.bytes), counts->atoms };
    columns[SNAPSHOT_REGION_STRINGS] = (SnapshotColumn){ 0, 1, 0 };
    columns[SNAPSHOT_REGION_NAME_LENGTH] = (SnapshotColumn){ (void**)&link->names.length, sizeof(*link->names.length), counts->names };
    columns[SNAPSHOT_REGION_NAME_HASH] = (SnapshotColumn){ (void**)&link->names.hash, sizeof(*link->names.hash), counts->names };
    columns[SNAPSHOT_REGION_ATOM_CLASS] = (SnapshotColumn){ (void**)&link->atoms.class, sizeof(*link->atoms.class), counts->atoms };
    columns[SNAPSHOT_REGION_ATOM_ALIGNMENT] = (SnapshotColumn){ (void**)&link->atoms.alignment_log2, sizeof(*link->atoms.alignment_log2), counts->atoms };
    columns[SNAPSHOT_REGION_ATOM_UNIT] = (SnapshotColumn){ (void**)&link->atoms.unit, sizeof(*link->atoms.unit), counts->atoms };
    columns[SNAPSHOT_REGION_ATOM_SIZE] = (SnapshotColumn){ (void**)&link->atoms.size, sizeof(*link->atoms.size), counts->atoms };
    columns[SNAPSHOT_REGION_ATOM_FIXUP_BEGIN] = (SnapshotColumn){ (void**)&link->atoms.fixup_begin, sizeof(*link->atoms.fixup_begin), counts->atoms };
    columns[SNAPSHOT_REGION_ATOM_FIXUP_COUNT] = (SnapshotColumn){ (void**)&link->atoms.fixup_count, sizeof(*link->atoms.fixup_count), counts->atoms };
    columns[SNAPSHOT_REGION_ATOM_ADDRESS] = (SnapshotColumn){ (void**)&link->atoms.address, sizeof(*link->atoms.address), counts->atoms };
    columns[SNAPSHOT_REGION_SYMBOL_NAME] = (SnapshotColumn){ (void**)&link->symbols.name, sizeof(*link->symbols.name), counts->symbols };
    columns[SNAPSHOT_REGION_SYMBOL_ATOM] = (SnapshotColumn){ (void**)&link->symbols.atom, sizeof(*link->symbols.atom), counts->symbols };
    columns[SNAPSHOT_REGION_SYMBOL_VALUE] = (SnapshotColumn){ (void**)&link->symbols.value, sizeof(*link->symbols.value), counts->symbols };
    columns[SNAPSHOT_REGION_SYMBOL_SIZE] = (SnapshotColumn){ (void**)&link->symbols.size, sizeof(*link->symbols.size), counts->symbols };
    columns[SNAPSHOT_REGION_SYMBOL_FLAGS] = (SnapshotColumn){ (void**)&link->symbols.flags, sizeof(*link->symbols.flags), counts->symbols };
    columns[SNAPSHOT_REGION_SYMBOL_SOURCE] = (SnapshotColumn){ (void**)&link->symbols.source, sizeof(*link->symbols.source), counts->symbols };
    columns[SNAPSHOT_REGION_SYMBOL_GOT_SLOT] = (SnapshotColumn){ (void**)&link->symbols.got_slot, sizeof(*link->symbols.got_slot), counts->symbols };
    columns[SNAPSHOT_REGION_SYMBOL_DYNAMIC_INDEX] = (SnapshotColumn){ (void**)&link->symbols.dynamic_index, sizeof(*link->symbols.dynamic_index), counts->symbols };
    columns[SNAPSHOT_REGION_SYMBOL_VERSION] = (SnapshotColumn){ (void**)&link->symbols.version, sizeof(*link->symbols.version), counts->symbols };
    columns[SNAPSHOT_REGION_SYMBOL_REFERENCE] = (SnapshotColumn){ (void**)&link->symbols.reference, sizeof(*link->symbols.reference), counts->symbols };
    columns[SNAPSHOT_REGION_FIXUP_OFFSET] = (SnapshotColumn){ (void**)&link->fixups.offset, sizeof(*link->fixups.offset), counts->fixups };
    columns[SNAPSHOT_REGION_FIXUP_TARGET] = (SnapshotColumn){ (void**)&link->fixups.target, sizeof(*link->fixups.target), counts->fixups };
    columns[SNAPSHOT_REGION_FIXUP_ADDEND] = (SnapshotColumn){ (void**)&link->fixups.addend, sizeof(*link->fixups.addend), counts->fixups };
    columns[SNAPSHOT_REGION_FIXUP_KIND] = (SnapshotColumn){ (void**)&link->fixups.kind, sizeof(*link->fixups.kind), counts->fixups };
    columns[SNAPSHOT_REGION_WIDE_ADDENDS] = (SnapshotColumn){ (void**)&link->fixups.wide_addends, sizeof(*link->fixups.wide_addends), counts->wide_addends };
    columns[SNAPSHOT_REGION_PIECES] = (SnapshotColumn){ (void**)&runtime->pieces, sizeof(RuntimePiece), counts->pieces };
    columns[SNAPSHOT_REGION_BINDINGS] = (SnapshotColumn){ (void**)&runtime->bindings, sizeof(RuntimeBinding), counts->symbols };
    columns[SNAPSHOT_REGION_SYMBOL_NAMES] = (SnapshotColumn){ (void**)&runtime->symbol_names, sizeof(*runtime->symbol_names), counts->symbols };
    columns[SNAPSHOT_REGION_GLOBAL_OFFSET] = (SnapshotColumn){ (void**)&names->offset, sizeof(*names->offset), counts->globals };
    columns[SNAPSHOT_REGION_GLOBAL_LENGTH] = (SnapshotColumn){ (void**)&names->length, sizeof(*names->length), counts->globals };
    columns[SNAPSHOT_REGION_GLOBAL_HASH] = (SnapshotColumn){ (void**)&names->hash, sizeof(*names->hash), counts->globals };
    columns[SNAPSHOT_REGION_GLOBAL_DEFINITION] = (SnapshotColumn){ (void**)&names->definition, sizeof(*names->definition), counts->globals };
    columns[SNAPSHOT_REGION_GLOBAL_EXPORT] = (SnapshotColumn){ (void**)&names->export, sizeof(*names->export), counts->globals };
    columns[SNAPSHOT_REGION_GLOBAL_REFERENCED] = (SnapshotColumn){ (void**)&names->referenced, sizeof(*names->referenced), counts->globals };
    columns[SNAPSHOT_REGION_GLOBAL_LINKER] = (SnapshotColumn){ (void**)&names->linker, sizeof(*names->linker), counts->globals };
    columns[SNAPSHOT_REGION_INDEX_START] = (SnapshotColumn){ (void**)&names->index.start, sizeof(*names->index.start), starts };
    columns[SNAPSHOT_REGION_INDEX_ORDER] = (SnapshotColumn){ (void**)&names->index.order, sizeof(*names->index.order), counts->globals };
    columns[SNAPSHOT_REGION_INDEX_HASH] = (SnapshotColumn){ (void**)&names->index.hash, sizeof(*names->index.hash), counts->globals };
    columns[SNAPSHOT_REGION_FILTER] = (SnapshotColumn){ (void**)&names->filter, sizeof(*names->filter), filter };
    columns[SNAPSHOT_REGION_EXPORT_VERSION] = (SnapshotColumn){ (void**)&runtime->libc.exports.version, sizeof(*runtime->libc.exports.version), counts->exports };
    columns[SNAPSHOT_REGION_EXPORT_FLAGS] = (SnapshotColumn){ (void**)&runtime->libc.exports.flags, sizeof(*runtime->libc.exports.flags), counts->exports };
}

// The runtime's strings that the header points to, by SnapshotPath.
static void snapshot_paths(Runtime* runtime, String8** paths)
{
    paths[SNAPSHOT_PATH_GCC_DIRECTORY] = &runtime->gcc_directory;
    paths[SNAPSHOT_PATH_GCC_VERSIONS] = &runtime->gcc_versions;
    for (uint32_t i = 0; i < RUNTIME_OBJECTS; i += 1)
    {
        paths[SNAPSHOT_PATH_OBJECTS + i] = &runtime->object_paths[i];
    }
    paths[SNAPSHOT_PATH_LIBC_SCRIPT] = &runtime->libc_script;
    paths[SNAPSHOT_PATH_LIBC] = &runtime->libc_path;
    paths[SNAPSHOT_PATH_NONSHARED] = &runtime->nonshared_path;
    paths[SNAPSHOT_PATH_LOADER] = &runtime->loader_path;
    paths[SNAPSHOT_PATH_SONAME] = &runtime->libc.soname;
    paths[SNAPSHOT_PATH_ABI_NOTE] = &runtime->abi_note;
}

static bool snapshot_same_identity(const RuntimeProbe* a, const RuntimeProbe* b)
{
    return a->found == b->found && a->device == b->device && a->inode == b->inode && a->size == b->size && a->modified_seconds == b->modified_seconds &&
           a->modified_nanoseconds == b->modified_nanoseconds && a->changed_seconds == b->changed_seconds && a->changed_nanoseconds == b->changed_nanoseconds;
}

// This executable's identity: what stat gives of /proc/self/exe, the file it was run from. False if it can't be had.
static bool snapshot_compiler(RuntimeProbe* compiler)
{
    struct stat status;
    const bool found = stat("/proc/self/exe", &status) == 0 && S_ISREG(status.st_mode);
    *compiler = (RuntimeProbe){ 0 };
    if (found)
    {
        runtime_identify(compiler, &status);
    }
    return found;
}

// The hash of a snapshot's bytes, with its header's hash as zeros.
static void snapshot_hash(const uint8_t* file, uint64_t bytes, uint8_t* hash)
{
    static const uint8_t zeros[sizeof(((SnapshotHeader*)0)->hash)] = { 0 };
    const uint64_t field = offsetof(SnapshotHeader, hash);
    ExecutableHash state;
    executable_hash_begin(&state);
    executable_hash_bytes(&state, file, field);
    executable_hash_bytes(&state, zeros, sizeof(zeros));
    executable_hash_bytes(&state, file + field + sizeof(zeros), bytes - field - sizeof(zeros));
    executable_hash_end(&state, hash);
}

// ---------------------------------------------------------------------------------------------------------------------
// Writing

static void snapshot_fail(SnapshotResult* result, const char* call, int error)
{
    if (result->call == 0)
    {
        result->call = call;
        result->system_error = error;
    }
}

static SnapshotCounts snapshot_counts(const Runtime* runtime)
{
    const Link* link = &runtime->link;
    SnapshotCounts result = {
        .probes = runtime->probe_count,
        .versions = runtime->libc.version_count,
        .members = runtime->nonshared.member_count,
        .names = link->names.count,
        .atoms = link->atoms.count,
        .symbols = link->symbols.count,
        .fixups = link->fixups.count,
        .wide_addends = link->fixups.wide_count,
        .pieces = RUNTIME_OBJECTS + runtime->nonshared.member_count,
        .globals = runtime->global_names.count,
        .bucket_bits = runtime->global_names.index.bucket_bits,
        .filter_bits = runtime->global_names.filter_bits,
        .exports = runtime->libc.exports.count,
        .nonshared_unit = runtime->nonshared_unit,
        .names_run_bytes = runtime->names.length,
        .global_strings_bytes = runtime->global_names.strings_length,
    };
    return result;
}

// The strings, as the writer builds them in the file's image: room for `capacity` bytes, of which `length` are used.
typedef struct SnapshotStrings SnapshotStrings;
struct SnapshotStrings
{
    uint8_t* bytes;
    uint64_t length;
    uint64_t capacity;
};

// Appends `length` bytes and a NUL: their offset plus 1, or 0 for no bytes (or no room, which `fits` records).
static uint64_t snapshot_add_string(SnapshotStrings* strings, const void* bytes, uint64_t length, bool* fits)
{
    uint64_t result = 0;
    const bool room = strings->capacity - strings->length > length;
    *fits = *fits && (bytes == 0 || room);
    if (bytes != 0 && room)
    {
        memcpy(strings->bytes + strings->length, bytes, length);
        strings->bytes[strings->length + length] = 0;
        result = strings->length + 1;
        strings->length += length + 1;
    }
    return result;
}

// The bytes the strings need: each string and its NUL, as snapshot_add_string adds them (none for no bytes).
static uint64_t snapshot_strings_bytes(const Runtime* runtime)
{
    String8* paths[SNAPSHOT_PATHS];
    snapshot_paths((Runtime*)runtime, paths);
    uint64_t result = runtime->names.length + 1 + runtime->global_names.strings_length + 1;
    for (uint32_t i = 0; i < SNAPSHOT_PATHS; i += 1)
    {
        result += paths[i]->pointer != 0 ? paths[i]->length + 1 : 0;
    }
    for (uint32_t p = 0; p < runtime->probe_count; p += 1)
    {
        result += runtime->probes[p].path.length + 1;
    }
    for (uint32_t v = 0; v < runtime->libc.version_count; v += 1)
    {
        result += runtime->libc.versions[v].name.length + 1;
    }
    for (uint32_t m = 0; m < runtime->nonshared.member_count; m += 1)
    {
        result += runtime->nonshared.members[m].name.length + 1;
    }
    for (AtomId atom = 1; atom < runtime->link.atoms.count; atom += 1)
    {
        result += runtime->link.atoms.bytes[atom] != 0 ? runtime->link.atoms.size[atom] + 1 : 0;
    }
    return result;
}

// The file's image of the runtime, from `arena`: its bytes and size, or 0 if the arena has no room or a pointer of the
// runtime's isn't in what it should point into.
static uint8_t* snapshot_image(const Runtime* runtime, const RuntimeProbe* compiler, Arena* arena, uint64_t* file_bytes)
{
    Runtime source = *runtime;
    SnapshotColumn columns[SNAPSHOT_REGIONS];
    SnapshotHeader header = {
        .magic = BUSTER_SNAPSHOT_MAGIC,
        .version = BUSTER_SNAPSHOT_VERSION,
        .header_bytes = sizeof(SnapshotHeader),
        .counts = snapshot_counts(runtime),
    };
    snapshot_columns(&source, &header.counts, columns);
    const uint64_t strings_bytes = snapshot_strings_bytes(runtime);
    uint64_t end = sizeof(SnapshotHeader);
    for (uint32_t r = 0; r < SNAPSHOT_REGIONS; r += 1)
    {
        const uint64_t alignment = r == 0 ? BUSTER_SNAPSHOT_PAGE_BYTES : BUSTER_SNAPSHOT_ALIGNMENT;
        const uint64_t offset = (end + alignment - 1) & ~(alignment - 1);
        const uint64_t bytes = r == SNAPSHOT_REGION_STRINGS ? strings_bytes : columns[r].element * columns[r].count;
        header.regions[r] = (SnapshotPlace){ offset, bytes };
        end = offset + bytes;
    }
    uint8_t* image = arena_push(arena, end, BUSTER_SNAPSHOT_ALIGNMENT);
    bool fits = image != 0;
    if (fits)
    {
        memset(image, 0, end);
        // The columns without pointers, as they are.
        for (uint32_t r = SNAPSHOT_REGION_STRINGS + 1; r < SNAPSHOT_REGIONS; r += 1)
        {
            if (header.regions[r].bytes != 0)
            {
                memcpy(image + header.regions[r].offset, *columns[r].column, header.regions[r].bytes);
            }
        }
        // The strings: the names run and the global names' strings first, so that their offsets are the counts', then the
        // paths, then the records' strings, each written in its record as it goes in.
        SnapshotStrings strings = {
            .bytes = image + header.regions[SNAPSHOT_REGION_STRINGS].offset,
            .capacity = strings_bytes,
        };
        header.counts.names_run = snapshot_add_string(&strings, runtime->names.base, runtime->names.length, &fits) - 1;
        header.counts.global_strings = snapshot_add_string(&strings, runtime->global_names.strings, runtime->global_names.strings_length, &fits) - 1;
        String8* paths[SNAPSHOT_PATHS];
        snapshot_paths(&source, paths);
        for (uint32_t i = 0; i < SNAPSHOT_PATHS; i += 1)
        {
            header.paths[i] = (SnapshotString){
                .offset = snapshot_add_string(&strings, paths[i]->pointer, paths[i]->length, &fits),
                .length = paths[i]->length,
            };
        }
        RuntimeProbe* probes = (RuntimeProbe*)(image + header.regions[SNAPSHOT_REGION_PROBES].offset);
        for (uint32_t p = 0; p < header.counts.probes; p += 1)
        {
            const RuntimeProbe* probe = &runtime->probes[p];
            probes[p].path.pointer = (char*)(uintptr_t)snapshot_add_string(&strings, probe->path.pointer, probe->path.length, &fits);
            probes[p].path.length = probe->path.length;
            probes[p].directory = probe->directory;
            probes[p].found = probe->found;
            probes[p].device = probe->device;
            probes[p].inode = probe->inode;
            probes[p].size = probe->size;
            probes[p].modified_seconds = probe->modified_seconds;
            probes[p].modified_nanoseconds = probe->modified_nanoseconds;
            probes[p].changed_seconds = probe->changed_seconds;
            probes[p].changed_nanoseconds = probe->changed_nanoseconds;
        }
        ImportVersion* versions = (ImportVersion*)(image + header.regions[SNAPSHOT_REGION_VERSIONS].offset);
        for (uint32_t v = 0; v < header.counts.versions; v += 1)
        {
            const ImportVersion* version = &runtime->libc.versions[v];
            versions[v].name.pointer = (char*)(uintptr_t)snapshot_add_string(&strings, version->name.pointer, version->name.length, &fits);
            versions[v].name.length = version->name.length;
            versions[v].hash = version->hash;
            versions[v].index = version->index;
            versions[v].base = version->base;
        }
        ImportMember* members = (ImportMember*)(image + header.regions[SNAPSHOT_REGION_MEMBERS].offset);
        for (uint32_t m = 0; m < header.counts.members; m += 1)
        {
            const ImportMember* member = &runtime->nonshared.members[m];
            members[m].name.pointer = (char*)(uintptr_t)snapshot_add_string(&strings, member->name.pointer, member->name.length, &fits);
            members[m].name.length = member->name.length;
            members[m].offset = member->offset;
        }
        // The names' bytes: in the names run, which the strings start with.
        const Link* link = &runtime->link;
        uint64_t* name_bytes = (uint64_t*)(image + header.regions[SNAPSHOT_REGION_NAME_BYTES].offset);
        for (NameId name = 1; name < header.counts.names; name += 1)
        {
            const uintptr_t place = (uintptr_t)link->names.bytes[name] - (uintptr_t)runtime->names.base;
            const bool inside = link->names.bytes[name] != 0 && place <= runtime->names.length && runtime->names.length - place >= link->names.length[name];
            fits = fits && inside;
            name_bytes[name] = inside ? header.counts.names_run + place + 1 : 0;
        }
        uint64_t* atom_bytes = (uint64_t*)(image + header.regions[SNAPSHOT_REGION_ATOM_BYTES].offset);
        for (AtomId atom = 1; atom < header.counts.atoms; atom += 1)
        {
            atom_bytes[atom] = snapshot_add_string(&strings, link->atoms.bytes[atom], link->atoms.size[atom], &fits);
        }
        fits = fits && strings.length == strings_bytes;
    }
    if (fits)
    {
        header.file_bytes = end;
        header.compiler = *compiler;
        header.compiler.path = (String8){ 0 };
        memcpy(image, &header, sizeof(header));
        snapshot_hash(image, end, header.hash);
        memcpy(image + offsetof(SnapshotHeader, hash), header.hash, sizeof(header.hash));
    }
    *file_bytes = fits ? end : 0;
    return fits ? image : 0;
}

// Makes the directory and the ones it is in that are missing, as private ones (mode 0700), as the XDG specification
// asks of a cache directory.
static bool snapshot_make_directory(const char* directory, SnapshotResult* result)
{
    char path[BUSTER_RUNTIME_PATH_BYTES];
    const uint64_t length = strlen(directory);
    bool made = length != 0 && length < sizeof(path);
    if (made)
    {
        memcpy(path, directory, length + 1);
    }
    for (uint64_t i = 1; made && i <= length; i += 1)
    {
        if (path[i] == '/' || path[i] == 0)
        {
            const char end = path[i];
            path[i] = 0;
            const int error = mkdir(path, 0700) == 0 ? 0 : errno;
            made = error == 0 || error == EEXIST;
            if (!made)
            {
                snapshot_fail(result, "mkdir", error);
            }
            path[i] = end;
        }
    }
    if (length == 0 || length >= sizeof(path))
    {
        snapshot_fail(result, "the directory", ENAMETOOLONG);
    }
    return made;
}

// Writes the bytes into a new file in `directory` under a random name, then renames it to `path`.
static bool snapshot_write_file(const char* directory, const char* path, const uint8_t* image, uint64_t bytes, SnapshotResult* result)
{
    char written[BUSTER_RUNTIME_PATH_BYTES];
    int descriptor = -1;
    int create_error = EEXIST;
    for (uint32_t attempt = 0; descriptor < 0 && create_error == EEXIST && attempt < BUSTER_SNAPSHOT_NAME_ATTEMPTS; attempt += 1)
    {
        uint64_t random = 0;
        if (getrandom(&random, sizeof(random), GRND_NONBLOCK) != (ssize_t)sizeof(random))
        {
            random = __rdtsc();
        }
        const int length = snprintf(written, sizeof(written), "%s/" BUSTER_SNAPSHOT_TEMPORARY_PREFIX "%d-%016llx", directory, (int)getpid(), (unsigned long long)random);
        descriptor = length > 0 && (uint64_t)length < sizeof(written) ? open(written, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644) : -1;
        create_error = descriptor >= 0 ? 0 : length > 0 && (uint64_t)length < sizeof(written) ? errno : ENAMETOOLONG;
    }
    bool done = descriptor >= 0;
    if (!done)
    {
        snapshot_fail(result, "open", create_error);
    }
    uint64_t sent = 0;
    while (done && sent < bytes)
    {
        const ssize_t count = write(descriptor, image + sent, bytes - sent);
        const int error = count > 0 || (count < 0 && errno == EINTR) ? 0 : count == 0 ? EIO : errno;
        sent += count > 0 ? (uint64_t)count : 0;
        done = error == 0;
        if (!done)
        {
            snapshot_fail(result, "write", error);
        }
    }
    if (descriptor >= 0 && close(descriptor) != 0 && done)
    {
        snapshot_fail(result, "close", errno);
        done = false;
    }
    if (done && rename(written, path) != 0)
    {
        snapshot_fail(result, "rename", errno);
        done = false;
    }
    if (descriptor >= 0 && !done)
    {
        unlink(written);
    }
    return done;
}

// Writes the runtime's snapshot into `directory` as `compiler` wrote it.
static bool snapshot_write(const Runtime* runtime, const char* directory, const RuntimeProbe* compiler, SnapshotResult* result)
{
    char path[BUSTER_RUNTIME_PATH_BYTES];
    const int length = snprintf(path, sizeof(path), "%s/" BUSTER_SNAPSHOT_NAME, directory);
    const bool named = length > 0 && (uint64_t)length < sizeof(path);
    if (!named)
    {
        snapshot_fail(result, "the directory", ENAMETOOLONG);
    }
    Arena arena = named ? arena_reserve(BUSTER_SNAPSHOT_WRITE_BYTES) : (Arena){ 0 };
    uint64_t bytes = 0;
    const uint8_t* image = arena.base != 0 ? snapshot_image(runtime, compiler, &arena, &bytes) : 0;
    if (named && image == 0)
    {
        snapshot_fail(result, "the image", ENOMEM);
    }
    const bool written = image != 0 && snapshot_make_directory(directory, result) && snapshot_write_file(directory, path, image, bytes, result);
    arena_release(&arena);
    return written;
}

// ---------------------------------------------------------------------------------------------------------------------
// Loading

// A string of the strings, from its offset plus 1.
static String8 snapshot_string(uint8_t* strings, SnapshotString string)
{
    String8 result = {
        .pointer = string.offset != 0 ? (char*)strings + string.offset - 1 : 0,
        .length = string.length,
    };
    return result;
}

// A column of pointers held as offsets into the strings plus 1, rebased where it is, eight at a time.
static void snapshot_rebase(uint64_t* words, uint64_t count, const uint8_t* strings)
{
    const __m512i base = _mm512_set1_epi64((long long)((uintptr_t)strings - 1));
    for (uint64_t i = 0; i < count; i += 8)
    {
        const __mmask8 lanes = (__mmask8)_bzhi_u32(0xff, count - i < 8 ? (uint32_t)(count - i) : 8);
        const __m512i word = _mm512_maskz_loadu_epi64(lanes, words + i);
        _mm512_mask_storeu_epi64(words + i, lanes, _mm512_mask_add_epi64(word, _mm512_test_epi64_mask(word, word), word, base));
    }
}

// Whether the header describes a file of this format and of `bytes` bytes, with each region inside it, aligned and of
// its column's size (`runtime` only gives the columns' places, which this doesn't touch).
static bool snapshot_valid_header(const SnapshotHeader* header, uint64_t bytes, Runtime* runtime)
{
    bool result = header->magic == BUSTER_SNAPSHOT_MAGIC && header->version == BUSTER_SNAPSHOT_VERSION && header->header_bytes == sizeof(SnapshotHeader) &&
                  header->file_bytes == bytes && header->counts.bucket_bits < 32 && header->counts.filter_bits < 32;
    SnapshotColumn columns[SNAPSHOT_REGIONS];
    snapshot_columns(runtime, &header->counts, columns);
    for (uint32_t r = 0; result && r < SNAPSHOT_REGIONS; r += 1)
    {
        const SnapshotPlace place = header->regions[r];
        const bool sized = r == SNAPSHOT_REGION_STRINGS || place.bytes == columns[r].element * columns[r].count;
        result = place.offset % BUSTER_SNAPSHOT_ALIGNMENT == 0 && place.offset >= sizeof(SnapshotHeader) && place.offset <= bytes && place.bytes <= bytes - place.offset && sized;
    }
    const SnapshotPlace strings = header->regions[SNAPSHOT_REGION_STRINGS];
    result = result && header->counts.names_run_bytes <= strings.bytes && header->counts.names_run <= strings.bytes - header->counts.names_run_bytes &&
             header->counts.global_strings_bytes <= strings.bytes && header->counts.global_strings <= strings.bytes - header->counts.global_strings_bytes;
    return result;
}

// Points the runtime's columns into the mapped snapshot and rebases its records with pointers.
static void snapshot_view(Runtime* runtime, uint8_t* file, uint64_t bytes)
{
    const SnapshotHeader* header = (const SnapshotHeader*)file;
    const SnapshotCounts counts = header->counts;
    SnapshotColumn columns[SNAPSHOT_REGIONS];
    snapshot_columns(runtime, &counts, columns);
    for (uint32_t r = 0; r < SNAPSHOT_REGIONS; r += 1)
    {
        if (r != SNAPSHOT_REGION_STRINGS)
        {
            *columns[r].column = file + header->regions[r].offset;
        }
    }
    uint8_t* strings = file + header->regions[SNAPSHOT_REGION_STRINGS].offset;
    // The records with pointers: the columns' eight at a time, and one field of each of the others' records, of which
    // there are a few dozen.
    snapshot_rebase((uint64_t*)runtime->link.names.bytes, counts.names, strings);
    snapshot_rebase((uint64_t*)runtime->link.atoms.bytes, counts.atoms, strings);
    for (uint32_t p = 0; p < counts.probes; p += 1)
    {
        runtime->probes[p].path = snapshot_string(strings, (SnapshotString){ (uint64_t)(uintptr_t)runtime->probes[p].path.pointer, runtime->probes[p].path.length });
    }
    for (uint32_t v = 0; v < counts.versions; v += 1)
    {
        ImportVersion* version = &runtime->libc.versions[v];
        version->name = snapshot_string(strings, (SnapshotString){ (uint64_t)(uintptr_t)version->name.pointer, version->name.length });
    }
    for (uint32_t m = 0; m < counts.members; m += 1)
    {
        ImportMember* member = &runtime->nonshared.members[m];
        member->name = snapshot_string(strings, (SnapshotString){ (uint64_t)(uintptr_t)member->name.pointer, member->name.length });
    }
    String8* paths[SNAPSHOT_PATHS];
    snapshot_paths(runtime, paths);
    for (uint32_t i = 0; i < SNAPSHOT_PATHS; i += 1)
    {
        *paths[i] = snapshot_string(strings, header->paths[i]);
    }
    Link* link = &runtime->link;
    link->names.count = counts.names;
    link->names.capacity = counts.names;
    link->atoms.count = counts.atoms;
    link->atoms.capacity = counts.atoms;
    link->symbols.count = counts.symbols;
    link->symbols.capacity = counts.symbols;
    link->fixups.count = counts.fixups;
    link->fixups.capacity = counts.fixups;
    link->fixups.wide_count = counts.wide_addends;
    link->fixups.wide_capacity = counts.wide_addends;
    link->name_arena = &runtime->names;
    runtime->names = (Arena){
        .base = strings + counts.names_run,
        .length = counts.names_run_bytes,
        .capacity = counts.names_run_bytes,
    };
    runtime->probe_count = counts.probes;
    runtime->probe_capacity = counts.probes;
    runtime->libc.version_count = counts.versions;
    runtime->libc.exports.count = counts.exports;
    runtime->nonshared.member_count = counts.members;
    runtime->nonshared_unit = counts.nonshared_unit;
    runtime->global_names.strings = strings + counts.global_strings;
    runtime->global_names.strings_length = counts.global_strings_bytes;
    runtime->global_names.count = counts.globals;
    runtime->global_names.index.bucket_bits = counts.bucket_bits;
    runtime->global_names.filter_bits = counts.filter_bits;
    runtime->snapshot = file;
    runtime->snapshot_bytes = bytes;
}

// The time from `*start` to now, added to `times[phase]` if there are times, and now as the next phase's start.
static void snapshot_time(uint64_t* times, SnapshotPhase phase, uint64_t* start)
{
    if (times != 0)
    {
        const uint64_t now = executable_now();
        times[phase] += now - *start;
        *start = now;
    }
}

// Loads the runtime from the snapshot at `path` if `compiler` wrote it and it still describes the system. The runtime is
// left empty otherwise. With `times`, each phase's nanoseconds are added to it, by SnapshotPhase.
static SnapshotStatus snapshot_load(Runtime* runtime, const char* path, const RuntimeProbe* compiler, uint64_t* times)
{
    uint64_t start = times != 0 ? executable_now() : 0;
    *runtime = (Runtime){ 0 };
    // O_NONBLOCK, so that a FIFO in its place doesn't wait for a writer.
    const int descriptor = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    struct stat status;
    const bool regular = descriptor >= 0 && fstat(descriptor, &status) == 0 && S_ISREG(status.st_mode);
    const uint64_t bytes = regular ? (uint64_t)status.st_size : 0;
    const bool sized = regular && bytes >= sizeof(SnapshotHeader);
    uint8_t* file = sized ? mmap(0, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE, descriptor, 0) : MAP_FAILED;
    if (descriptor >= 0)
    {
        close(descriptor);
    }
    SnapshotStatus result = regular && !sized ? SNAPSHOT_STATUS_INVALID : SNAPSHOT_STATUS_MISSING;
    snapshot_time(times, SNAPSHOT_PHASE_MAP, &start);
    if (file != MAP_FAILED)
    {
        const SnapshotHeader* header = (const SnapshotHeader*)file;
        uint8_t hash[sizeof(header->hash)];
        bool valid = snapshot_valid_header(header, bytes, runtime);
        if (valid)
        {
            snapshot_hash(file, bytes, hash);
            valid = memcmp(hash, header->hash, sizeof(hash)) == 0;
        }
        result = !valid ? SNAPSHOT_STATUS_INVALID : snapshot_same_identity(&header->compiler, compiler) ? SNAPSHOT_STATUS_LOADED : SNAPSHOT_STATUS_STALE;
        snapshot_time(times, SNAPSHOT_PHASE_HASH, &start);
        if (result == SNAPSHOT_STATUS_LOADED)
        {
            snapshot_view(runtime, file, bytes);
            snapshot_time(times, SNAPSHOT_PHASE_VIEW, &start);
            result = runtime_probes_current(runtime) ? result : SNAPSHOT_STATUS_STALE;
            snapshot_time(times, SNAPSHOT_PHASE_PROBES, &start);
        }
        if (result != SNAPSHOT_STATUS_LOADED)
        {
            munmap(file, bytes);
            *runtime = (Runtime){ 0 };
        }
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// runtime_open

static bool snapshot_directory(char* path, uint64_t capacity)
{
    const char* cache = getenv("XDG_CACHE_HOME");
    const char* home = getenv("HOME");
    const bool xdg = cache != 0 && cache[0] == '/';
    const int length = xdg ? snprintf(path, capacity, "%s/buster", cache) : home != 0 && home[0] == '/' ? snprintf(path, capacity, "%s/.cache/buster", home) : -1;
    const bool result = length > 0 && (uint64_t)length < capacity;
    if (!result && capacity != 0)
    {
        path[0] = 0;
    }
    return result;
}

// runtime_open, as `compiler` (0 for an executable without an identity, which never loads or writes a snapshot).
static SnapshotResult runtime_open_as(Runtime* runtime, const char* directory, const RuntimeProbe* compiler)
{
    SnapshotResult result = { .status = SNAPSHOT_STATUS_MISSING };
    char path[BUSTER_RUNTIME_PATH_BYTES];
    const int length = directory != 0 ? snprintf(path, sizeof(path), "%s/" BUSTER_SNAPSHOT_NAME, directory) : -1;
    const bool cached = compiler != 0 && length > 0 && (uint64_t)length < sizeof(path);
    if (cached)
    {
        result.status = snapshot_load(runtime, path, compiler, 0);
    }
    if (result.status != SNAPSHOT_STATUS_LOADED)
    {
        result.error = runtime_import(runtime);
        result.written = result.error == RUNTIME_ERROR_NONE && cached && snapshot_write(runtime, directory, compiler, &result);
    }
    return result;
}

static SnapshotResult runtime_open(Runtime* runtime, const char* directory)
{
    RuntimeProbe compiler;
    const bool identified = snapshot_compiler(&compiler);
    return runtime_open_as(runtime, directory, identified ? &compiler : 0);
}
