// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The runtime snapshot (docs/object-writer-and-linker.md, "The runtime snapshot"): what runtime_import makes of the C
// runtime, kept in a file in the cache directory, so that a link maps one file instead of finding, reading and
// importing the runtime's. runtime_open loads the snapshot if it still describes the system, and otherwise imports the
// runtime and writes a new snapshot; runtime_open_static, runtime_open_static_pie and runtime_open_shared do the same for
// a static executable's runtime, a static PIE's and a shared library's (runtime_import_static, runtime_import_static_pie
// and runtime_import_shared), each in a file of its own. Each kind's file holds that kind, which the loader checks.
//
// The file holds the runtime's records as this executable lays them out in memory, the directory's regions each at a
// multiple of 64 bytes and the pieces' at their elements' alignment: a header, then the directory, which every link
// reads (from the second page: the probes, libc.so.6's, the loader's and libgcc_s.so.1's versions, the members, the
// strings, the pieces, the global names with their index and filter, the three libraries' exports, the pieces'
// references and COMDAT groups),
// then the pieces' records, of which a link reads only those of the pieces it joins. A piece is a crt object or a
// member, as Runtime.pieces has them. Every link joins the crt objects, and most join no member, so their records come
// first and together, in the pages after the directory's last (#490): each column binding reads (the link's and the
// symbols' global names) over the crt objects' records, then the data (the crt objects' names, every piece's atoms'
// bytes, then the members' names), then the same columns over the members' records (Runtime.member_link), then the
// columns nothing reads of a loaded runtime. With each column whole, the crt objects' records were the starts of
// columns that span every member, and their atoms' bytes came after every piece's names: with libgcc.a's 298 members,
// in pages of the mapping apart from the directory's and from each other, each a minor fault for a link that joins no
// member.
//   - Validation: the file must have this format's magic, version and header, and the size its header gives; the
//     directory's hash must be its bytes' (executable.c's chunk hash, over the file up to the pieces' records, with the
//     hash's field as zeros); this very executable must have written it (the identity of /proc/self/exe: st_dev,
//     st_ino, st_size, st_mtim and st_ctim, which a rebuild changes); its kind must be the one asked for; and every
//     non-deferred path the import probed must be as it was (runtime_probes_current). Binding checks libgcc_s's search
//     only when a strong reference or a version need can depend on it, or before reporting an undefined name. It also
//     checks permitted strong undefined names in shared libraries, which a newly installed library could define.
//     A piece's records are checked by binding,
//     when it first reads them: their hash must be the one the directory holds (runtime_piece_current). So the layouts
//     it holds are this executable's own, a link reads only what it wrote, and checks only what it reads: a static
//     hello world reads 442 of the static runtime's 2,622 pieces, and hashing all 8.6 MB took three quarters of the
//     load on the 9700X (docs/object-writer-and-linker.md, "The runtime snapshot"). A torn, truncated or corrupt file is
//     rebuilt rather than trusted, and none needs an fsync before its rename: a damaged directory makes the file
//     invalid, and a damaged piece fails the link that joins it (BIND_ERROR_SNAPSHOT), whose caller discards the file
//     (snapshot_discard) and opens the runtime again.
//   - Loading: the file is mapped privately, and the runtime's columns point into the mapping. The probes and the
//     libraries' versions hold offsets into the strings plus 1, 0 for none, and are rebased where they are, which
//     copies their page; the members' names and the link's names' and atoms' bytes hold such offsets too, into the
//     strings and the data, which the runtime's deltas make pointers where they are read (runtime_name_bytes,
//     runtime_atom_bytes and runtime_member_name), so that loading writes none of their pages. The rest is only read.
//   - Writing: into a new file beside the snapshot, under a random name, then renamed over it: a process that has the
//     old one mapped keeps it whole, and two compilers that rebuild it at once write the same bytes, the last rename
//     winning.
//
// main.c includes this file after executable.c, whose hash it uses. runtime_open and its other kinds'
// (runtime_open_static, runtime_open_static_pie, runtime_open_shared) take BUSTER_LINKER_ENTRY rather than static:
// main.c's units compile them in one unit alone (link.c). Layout, in this order:
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
// doesn't fail the import. runtime_release frees the runtime either way. runtime_open_static, runtime_open_static_pie
// and runtime_open_shared do the same for a static executable's runtime, a static PIE's and a shared library's.
BUSTER_LINKER_ENTRY __attribute__((unused)) SnapshotResult runtime_open(Runtime* runtime, const char* directory);
BUSTER_LINKER_ENTRY __attribute__((unused)) SnapshotResult runtime_open_static(Runtime* runtime, const char* directory);
BUSTER_LINKER_ENTRY __attribute__((unused)) SnapshotResult runtime_open_static_pie(Runtime* runtime, const char* directory);
BUSTER_LINKER_ENTRY __attribute__((unused)) SnapshotResult runtime_open_shared(Runtime* runtime, const char* directory);
// Removes a kind's snapshot from `directory`, as the caller of a link whose binding found a piece of a loaded runtime
// damaged does (BIND_ERROR_SNAPSHOT), before it releases the runtime and opens it again, which then imports it and
// writes a new snapshot: whether no snapshot of the kind is there now.
static __attribute__((unused)) bool snapshot_discard(const char* directory, RuntimeKind kind);

// ---------------------------------------------------------------------------------------------------------------------
// The file

#define BUSTER_SNAPSHOT_MAGIC 0x504e535254535542ull
#define BUSTER_SNAPSHOT_VERSION 8
// Each kind's file: a dynamic PIE's runtime's, a static executable's, a static PIE's and a shared library's.
#define BUSTER_SNAPSHOT_NAME "runtime.snapshot"
#define BUSTER_SNAPSHOT_STATIC_NAME "runtime-static.snapshot"
#define BUSTER_SNAPSHOT_STATIC_PIE_NAME "runtime-static-pie.snapshot"
#define BUSTER_SNAPSHOT_SHARED_NAME "runtime-shared.snapshot"
#define BUSTER_SNAPSHOT_ALIGNMENT 64
// The pieces' regions are aligned to their elements' alignment, 8 bytes at most, rather than to 64: binding reads a
// piece's records from whichever record it starts at, with masked loads, so a column's first record starting a cache
// line gains nothing, and 64 bytes would pad each of their 55 regions.
#define BUSTER_SNAPSHOT_PIECE_ALIGNMENT 8
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
    // BUSTER_RUNTIME_ARCHIVES of them, the archives' in their order.
    SNAPSHOT_PATH_ARCHIVES,
    SNAPSHOT_PATH_LOADER = SNAPSHOT_PATH_ARCHIVES + BUSTER_RUNTIME_ARCHIVES,
    SNAPSHOT_PATH_SONAME,
    SNAPSHOT_PATH_LOADER_SONAME,
    SNAPSHOT_PATH_GCC_S,
    SNAPSHOT_PATH_GCC_S_SONAME,
    SNAPSHOT_PATH_ABI_NOTE,
    SNAPSHOT_PATHS,
} SnapshotPath;

// The file's regions, in its order: the directory, which every link reads (the records with pointers, the strings, the
// pieces and their hashes, and the columns the closure and the lookups read), then the pieces' records, which a link
// reads only of the pieces it joins: each column binding reads (the link's and the symbols' global names) over the crt
// objects' records, from each table's record 0; the data (the crt objects' names, every piece's atoms' bytes, then the
// members' names, the names run in two parts); the same columns over the members' records; and the columns nothing
// reads of a loaded runtime, whole. The directory's hash covers the file up to the first of the pieces' regions
// (SnapshotHeader.directory_bytes), each piece's hash its records (runtime_piece_hash).
typedef enum SnapshotRegion
{
    SNAPSHOT_REGION_PROBES,
    SNAPSHOT_REGION_VERSIONS,
    SNAPSHOT_REGION_LOADER_VERSIONS,
    SNAPSHOT_REGION_GCC_S_VERSIONS,
    SNAPSHOT_REGION_MEMBERS,
    SNAPSHOT_REGION_STRINGS,
    SNAPSHOT_REGION_PIECES,
    SNAPSHOT_REGION_PIECE_HASHES,
    SNAPSHOT_REGION_PIECE_DATA,
    SNAPSHOT_REGION_GLOBAL_OFFSET,
    SNAPSHOT_REGION_GLOBAL_LENGTH,
    SNAPSHOT_REGION_GLOBAL_HASH,
    SNAPSHOT_REGION_GLOBAL_DEFINITION,
    SNAPSHOT_REGION_GLOBAL_MEMBER,
    SNAPSHOT_REGION_GLOBAL_EXPORT,
    SNAPSHOT_REGION_GLOBAL_REFERENCED,
    SNAPSHOT_REGION_GLOBAL_LINKER,
    SNAPSHOT_REGION_INDEX_START,
    SNAPSHOT_REGION_INDEX_ORDER,
    SNAPSHOT_REGION_INDEX_HASH,
    SNAPSHOT_REGION_FILTER,
    SNAPSHOT_REGION_EXPORT_VERSION,
    SNAPSHOT_REGION_EXPORT_FLAGS,
    SNAPSHOT_REGION_LOADER_EXPORT_VERSION,
    SNAPSHOT_REGION_LOADER_EXPORT_FLAGS,
    SNAPSHOT_REGION_GCC_S_EXPORT_VERSION,
    SNAPSHOT_REGION_GCC_S_EXPORT_FLAGS,
    SNAPSHOT_REGION_REFERENCE_BEGIN,
    SNAPSHOT_REGION_REFERENCES,
    SNAPSHOT_REGION_REFERENCE_MEMBERS,
    SNAPSHOT_REGION_GROUP_BEGIN,
    SNAPSHOT_REGION_GROUPS,
    SNAPSHOT_REGION_GROUP_ATOMS,
    // The crt objects' records of the columns binding reads.
    SNAPSHOT_REGION_NAME_BYTES,
    SNAPSHOT_REGION_NAME_LENGTH,
    SNAPSHOT_REGION_NAME_HASH,
    SNAPSHOT_REGION_ATOM_CLASS,
    SNAPSHOT_REGION_ATOM_ALIGNMENT,
    SNAPSHOT_REGION_ATOM_UNIT,
    SNAPSHOT_REGION_ATOM_SIZE,
    SNAPSHOT_REGION_ATOM_BYTES,
    SNAPSHOT_REGION_ATOM_FIXUP_BEGIN,
    SNAPSHOT_REGION_ATOM_FIXUP_COUNT,
    SNAPSHOT_REGION_SYMBOL_NAME,
    SNAPSHOT_REGION_SYMBOL_ATOM,
    SNAPSHOT_REGION_SYMBOL_VALUE,
    SNAPSHOT_REGION_SYMBOL_SIZE,
    SNAPSHOT_REGION_SYMBOL_FLAGS,
    SNAPSHOT_REGION_SYMBOL_SOURCE,
    SNAPSHOT_REGION_FIXUP_OFFSET,
    SNAPSHOT_REGION_FIXUP_TARGET,
    SNAPSHOT_REGION_FIXUP_ADDEND,
    SNAPSHOT_REGION_FIXUP_KIND,
    SNAPSHOT_REGION_WIDE_ADDENDS,
    SNAPSHOT_REGION_PRIORITY_ATOM,
    SNAPSHOT_REGION_PRIORITY_VALUE,
    SNAPSHOT_REGION_SYMBOL_NAMES,
    SNAPSHOT_REGION_DATA,
    // The members' records of the same columns, in the same order (snapshot_member_region).
    SNAPSHOT_REGION_MEMBER_NAME_BYTES,
    SNAPSHOT_REGION_MEMBER_NAME_LENGTH,
    SNAPSHOT_REGION_MEMBER_NAME_HASH,
    SNAPSHOT_REGION_MEMBER_ATOM_CLASS,
    SNAPSHOT_REGION_MEMBER_ATOM_ALIGNMENT,
    SNAPSHOT_REGION_MEMBER_ATOM_UNIT,
    SNAPSHOT_REGION_MEMBER_ATOM_SIZE,
    SNAPSHOT_REGION_MEMBER_ATOM_BYTES,
    SNAPSHOT_REGION_MEMBER_ATOM_FIXUP_BEGIN,
    SNAPSHOT_REGION_MEMBER_ATOM_FIXUP_COUNT,
    SNAPSHOT_REGION_MEMBER_SYMBOL_NAME,
    SNAPSHOT_REGION_MEMBER_SYMBOL_ATOM,
    SNAPSHOT_REGION_MEMBER_SYMBOL_VALUE,
    SNAPSHOT_REGION_MEMBER_SYMBOL_SIZE,
    SNAPSHOT_REGION_MEMBER_SYMBOL_FLAGS,
    SNAPSHOT_REGION_MEMBER_SYMBOL_SOURCE,
    SNAPSHOT_REGION_MEMBER_FIXUP_OFFSET,
    SNAPSHOT_REGION_MEMBER_FIXUP_TARGET,
    SNAPSHOT_REGION_MEMBER_FIXUP_ADDEND,
    SNAPSHOT_REGION_MEMBER_FIXUP_KIND,
    SNAPSHOT_REGION_MEMBER_WIDE_ADDENDS,
    SNAPSHOT_REGION_MEMBER_PRIORITY_ATOM,
    SNAPSHOT_REGION_MEMBER_PRIORITY_VALUE,
    SNAPSHOT_REGION_MEMBER_SYMBOL_NAMES,
    // The columns nothing reads of a loaded runtime, whole: the atoms' addresses and the symbols' GOT slots, dynamic
    // indices, versions and references, the linker's, which the runtime leaves 0, and the bindings, which only the
    // import uses.
    SNAPSHOT_REGION_ATOM_ADDRESS,
    SNAPSHOT_REGION_SYMBOL_GOT_SLOT,
    SNAPSHOT_REGION_SYMBOL_DYNAMIC_INDEX,
    SNAPSHOT_REGION_SYMBOL_VERSION,
    SNAPSHOT_REGION_SYMBOL_REFERENCE,
    SNAPSHOT_REGION_BINDINGS,
    SNAPSHOT_REGIONS,
} SnapshotRegion;

// The first of the pieces' regions: the directory is the file before it.
#define BUSTER_SNAPSHOT_PIECE_REGIONS SNAPSHOT_REGION_NAME_BYTES
// A column binding reads has the members' records in the region this many after the crt objects'.
#define BUSTER_SNAPSHOT_MEMBER_REGIONS (SNAPSHOT_REGION_MEMBER_NAME_BYTES - SNAPSHOT_REGION_NAME_BYTES)

_Static_assert(SNAPSHOT_REGION_MEMBER_SYMBOL_NAMES - SNAPSHOT_REGION_SYMBOL_NAMES == BUSTER_SNAPSHOT_MEMBER_REGIONS,
               "each column binding reads has a region of the members' records");

// A region's alignment in the file: the directory's are cache lines', the pieces' their elements' (the first, the
// probes, is a page's, which the writer gives it).
static uint64_t snapshot_region_alignment(uint32_t region)
{
    return region < BUSTER_SNAPSHOT_PIECE_REGIONS ? BUSTER_SNAPSHOT_ALIGNMENT : BUSTER_SNAPSHOT_PIECE_ALIGNMENT;
}

// The region of the members' records of a column binding reads whose crt objects' records are in `region` (from
// SNAPSHOT_REGION_NAME_BYTES to SNAPSHOT_REGION_SYMBOL_NAMES).
static SnapshotRegion snapshot_member_region(SnapshotRegion region)
{
    return (SnapshotRegion)(region + BUSTER_SNAPSHOT_MEMBER_REGIONS);
}

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

// How many records of each kind the runtime has, and of them the crt objects', where the names run's two parts are in
// the data and the global names' strings in the strings, and the room a link that binds against it needs
// (Runtime.name_room and atom_room).
typedef struct SnapshotCounts SnapshotCounts;
struct SnapshotCounts
{
    // The runtime's kind, and its archives' number and each one's first member.
    uint32_t kind;
    uint32_t archives;
    uint32_t first_members[BUSTER_RUNTIME_ARCHIVES];
    uint32_t probes;
    // libc.so.6's versions, the loader's and libgcc_s.so.1's.
    uint32_t versions;
    uint32_t loader_versions;
    uint32_t gcc_s_versions;
    uint32_t members;
    // Each of the link's tables, record 0 among them.
    uint32_t names;
    uint32_t atoms;
    uint32_t symbols;
    uint32_t fixups;
    uint32_t wide_addends;
    uint32_t priorities;
    uint32_t pieces;
    // The pieces' references, groups and the groups' atoms, and the groups' distinct signatures.
    uint32_t references;
    uint32_t groups;
    uint32_t group_atoms;
    uint32_t group_signatures;
    uint32_t globals;
    uint32_t bucket_bits;
    uint32_t filter_bits;
    // libc.so.6's exports, the loader's and libgcc_s.so.1's.
    uint32_t exports;
    uint32_t loader_exports;
    uint32_t gcc_s_exports;
    uint32_t member_unit;
    // The crt objects' records in each of the link's tables, from record 0: each table's records before the first
    // member's, at which the columns binding reads are split into two regions.
    uint32_t object_names;
    uint32_t object_atoms;
    uint32_t object_symbols;
    uint32_t object_fixups;
    uint32_t object_wide_addends;
    uint32_t object_priorities;
    // The names run's two parts: its first object_name_bytes bytes, the crt objects' names, at names_run in the data, and
    // the rest, the members', at member_names_run, after every piece's atoms' bytes.
    uint64_t names_run;
    uint64_t names_run_bytes;
    uint64_t object_name_bytes;
    uint64_t member_names_run;
    uint64_t global_strings;
    uint64_t global_strings_bytes;
    uint64_t name_room;
    uint64_t atom_room;
};

typedef struct SnapshotHeader SnapshotHeader;
struct SnapshotHeader
{
    uint64_t magic;
    uint32_t version;
    uint32_t header_bytes;
    uint64_t file_bytes;
    // The directory's bytes, from the file's start: what its hash covers.
    uint64_t directory_bytes;
    uint8_t hash[16];
    // The executable that wrote it: an identity alone, without a path.
    RuntimeProbe compiler;
    SnapshotCounts counts;
    SnapshotString paths[SNAPSHOT_PATHS];
    SnapshotPlace regions[SNAPSHOT_REGIONS];
};

_Static_assert(sizeof(SnapshotHeader) <= BUSTER_SNAPSHOT_PAGE_BYTES, "the header fits in the first page");

// Where the runtime keeps a region's column (or array of records), the size of its elements, and which of them the
// region holds: `count` from element `first` (the members' records of a column binding reads, the column's elements from
// the first member's).
typedef struct SnapshotColumn SnapshotColumn;
struct SnapshotColumn
{
    void** column;
    uint64_t element;
    uint64_t first;
    uint64_t count;
};

// Each region's column in `runtime`, with `counts`' numbers of elements: of a column binding reads, the crt objects'
// records in `runtime->link`'s (from element 0) and the members' in `runtime->member_link`'s, which a loaded runtime
// points to the members' region placed as the column's elements from the first member's (snapshot_view). The strings
// and the data have no column of their own: their bytes come from several.
static void snapshot_columns(Runtime* runtime, const SnapshotCounts* counts, SnapshotColumn* columns)
{
    Link* link = &runtime->link;
    Link* members = &runtime->member_link;
    RuntimeNames* names = &runtime->global_names;
    // A header's counts may be anything until it is checked (snapshot_valid_header): a members' count that would be
    // negative is past any file's size.
    const uint64_t starts = counts->bucket_bits < 32 ? ((uint64_t)1 << counts->bucket_bits) + 1 : 0;
    const uint64_t filter = counts->filter_bits < 32 ? (uint64_t)1 << counts->filter_bits : 0;
    const uint64_t member_names = counts->names >= counts->object_names ? counts->names - counts->object_names : UINT64_MAX / 16;
    const uint64_t member_atoms = counts->atoms >= counts->object_atoms ? counts->atoms - counts->object_atoms : UINT64_MAX / 16;
    const uint64_t member_symbols = counts->symbols >= counts->object_symbols ? counts->symbols - counts->object_symbols : UINT64_MAX / 16;
    const uint64_t member_fixups = counts->fixups >= counts->object_fixups ? counts->fixups - counts->object_fixups : UINT64_MAX / 16;
    const uint64_t member_wide = counts->wide_addends >= counts->object_wide_addends ? counts->wide_addends - counts->object_wide_addends : UINT64_MAX / 16;
    const uint64_t member_priorities = counts->priorities >= counts->object_priorities ? counts->priorities - counts->object_priorities : UINT64_MAX / 16;
    columns[SNAPSHOT_REGION_PROBES] = (SnapshotColumn){ (void**)&runtime->probes, sizeof(RuntimeProbe), 0, counts->probes };
    columns[SNAPSHOT_REGION_VERSIONS] = (SnapshotColumn){ (void**)&runtime->libc.versions, sizeof(ImportVersion), 0, counts->versions };
    columns[SNAPSHOT_REGION_LOADER_VERSIONS] = (SnapshotColumn){ (void**)&runtime->loader.versions, sizeof(ImportVersion), 0, counts->loader_versions };
    columns[SNAPSHOT_REGION_GCC_S_VERSIONS] = (SnapshotColumn){ (void**)&runtime->gcc_s.versions, sizeof(ImportVersion), 0, counts->gcc_s_versions };
    columns[SNAPSHOT_REGION_MEMBERS] = (SnapshotColumn){ (void**)&runtime->members, sizeof(ImportMember), 0, counts->members };
    columns[SNAPSHOT_REGION_STRINGS] = (SnapshotColumn){ 0, 1, 0, 0 };
    columns[SNAPSHOT_REGION_PIECES] = (SnapshotColumn){ (void**)&runtime->pieces, sizeof(RuntimePiece), 0, counts->pieces };
    columns[SNAPSHOT_REGION_PIECE_HASHES] = (SnapshotColumn){ (void**)&runtime->piece_hashes, 16, 0, counts->pieces };
    columns[SNAPSHOT_REGION_PIECE_DATA] = (SnapshotColumn){ (void**)&runtime->piece_data, sizeof(*runtime->piece_data), 0, (uint64_t)counts->pieces + 1 };
    columns[SNAPSHOT_REGION_GLOBAL_OFFSET] = (SnapshotColumn){ (void**)&names->offset, sizeof(*names->offset), 0, counts->globals };
    columns[SNAPSHOT_REGION_GLOBAL_LENGTH] = (SnapshotColumn){ (void**)&names->length, sizeof(*names->length), 0, counts->globals };
    columns[SNAPSHOT_REGION_GLOBAL_HASH] = (SnapshotColumn){ (void**)&names->hash, sizeof(*names->hash), 0, counts->globals };
    columns[SNAPSHOT_REGION_GLOBAL_DEFINITION] = (SnapshotColumn){ (void**)&names->definition, sizeof(*names->definition), 0, counts->globals };
    columns[SNAPSHOT_REGION_GLOBAL_MEMBER] = (SnapshotColumn){ (void**)&names->member, sizeof(*names->member), 0, counts->globals };
    columns[SNAPSHOT_REGION_GLOBAL_EXPORT] = (SnapshotColumn){ (void**)&names->export, sizeof(*names->export), 0, counts->globals };
    columns[SNAPSHOT_REGION_GLOBAL_REFERENCED] = (SnapshotColumn){ (void**)&names->referenced, sizeof(*names->referenced), 0, counts->globals };
    columns[SNAPSHOT_REGION_GLOBAL_LINKER] = (SnapshotColumn){ (void**)&names->linker, sizeof(*names->linker), 0, counts->globals };
    columns[SNAPSHOT_REGION_INDEX_START] = (SnapshotColumn){ (void**)&names->index.start, sizeof(*names->index.start), 0, starts };
    columns[SNAPSHOT_REGION_INDEX_ORDER] = (SnapshotColumn){ (void**)&names->index.order, sizeof(*names->index.order), 0, counts->globals };
    columns[SNAPSHOT_REGION_INDEX_HASH] = (SnapshotColumn){ (void**)&names->index.hash, sizeof(*names->index.hash), 0, counts->globals };
    columns[SNAPSHOT_REGION_FILTER] = (SnapshotColumn){ (void**)&names->filter, sizeof(*names->filter), 0, filter };
    columns[SNAPSHOT_REGION_EXPORT_VERSION] = (SnapshotColumn){ (void**)&runtime->libc.exports.version, sizeof(*runtime->libc.exports.version), 0, counts->exports };
    columns[SNAPSHOT_REGION_EXPORT_FLAGS] = (SnapshotColumn){ (void**)&runtime->libc.exports.flags, sizeof(*runtime->libc.exports.flags), 0, counts->exports };
    columns[SNAPSHOT_REGION_LOADER_EXPORT_VERSION] =
        (SnapshotColumn){ (void**)&runtime->loader.exports.version, sizeof(*runtime->loader.exports.version), 0, counts->loader_exports };
    columns[SNAPSHOT_REGION_LOADER_EXPORT_FLAGS] = (SnapshotColumn){ (void**)&runtime->loader.exports.flags, sizeof(*runtime->loader.exports.flags), 0, counts->loader_exports };
    columns[SNAPSHOT_REGION_GCC_S_EXPORT_VERSION] =
        (SnapshotColumn){ (void**)&runtime->gcc_s.exports.version, sizeof(*runtime->gcc_s.exports.version), 0, counts->gcc_s_exports };
    columns[SNAPSHOT_REGION_GCC_S_EXPORT_FLAGS] = (SnapshotColumn){ (void**)&runtime->gcc_s.exports.flags, sizeof(*runtime->gcc_s.exports.flags), 0, counts->gcc_s_exports };
    columns[SNAPSHOT_REGION_REFERENCE_BEGIN] = (SnapshotColumn){ (void**)&runtime->reference_begin, sizeof(*runtime->reference_begin), 0, (uint64_t)counts->pieces + 1 };
    columns[SNAPSHOT_REGION_REFERENCES] = (SnapshotColumn){ (void**)&runtime->references, sizeof(*runtime->references), 0, counts->references };
    columns[SNAPSHOT_REGION_REFERENCE_MEMBERS] = (SnapshotColumn){ (void**)&runtime->reference_members, sizeof(*runtime->reference_members), 0, counts->references };
    columns[SNAPSHOT_REGION_GROUP_BEGIN] = (SnapshotColumn){ (void**)&runtime->group_begin, sizeof(*runtime->group_begin), 0, (uint64_t)counts->pieces + 1 };
    columns[SNAPSHOT_REGION_GROUPS] = (SnapshotColumn){ (void**)&runtime->groups, sizeof(RuntimeGroup), 0, counts->groups };
    columns[SNAPSHOT_REGION_GROUP_ATOMS] = (SnapshotColumn){ (void**)&runtime->group_atoms, sizeof(*runtime->group_atoms), 0, counts->group_atoms };
    columns[SNAPSHOT_REGION_NAME_BYTES] = (SnapshotColumn){ (void**)&link->names.bytes, sizeof(*link->names.bytes), 0, counts->object_names };
    columns[SNAPSHOT_REGION_NAME_LENGTH] = (SnapshotColumn){ (void**)&link->names.length, sizeof(*link->names.length), 0, counts->object_names };
    columns[SNAPSHOT_REGION_NAME_HASH] = (SnapshotColumn){ (void**)&link->names.hash, sizeof(*link->names.hash), 0, counts->object_names };
    columns[SNAPSHOT_REGION_ATOM_CLASS] = (SnapshotColumn){ (void**)&link->atoms.class, sizeof(*link->atoms.class), 0, counts->object_atoms };
    columns[SNAPSHOT_REGION_ATOM_ALIGNMENT] = (SnapshotColumn){ (void**)&link->atoms.alignment_log2, sizeof(*link->atoms.alignment_log2), 0, counts->object_atoms };
    columns[SNAPSHOT_REGION_ATOM_UNIT] = (SnapshotColumn){ (void**)&link->atoms.unit, sizeof(*link->atoms.unit), 0, counts->object_atoms };
    columns[SNAPSHOT_REGION_ATOM_SIZE] = (SnapshotColumn){ (void**)&link->atoms.size, sizeof(*link->atoms.size), 0, counts->object_atoms };
    columns[SNAPSHOT_REGION_ATOM_BYTES] = (SnapshotColumn){ (void**)&link->atoms.bytes, sizeof(*link->atoms.bytes), 0, counts->object_atoms };
    columns[SNAPSHOT_REGION_ATOM_FIXUP_BEGIN] = (SnapshotColumn){ (void**)&link->atoms.fixup_begin, sizeof(*link->atoms.fixup_begin), 0, counts->object_atoms };
    columns[SNAPSHOT_REGION_ATOM_FIXUP_COUNT] = (SnapshotColumn){ (void**)&link->atoms.fixup_count, sizeof(*link->atoms.fixup_count), 0, counts->object_atoms };
    columns[SNAPSHOT_REGION_SYMBOL_NAME] = (SnapshotColumn){ (void**)&link->symbols.name, sizeof(*link->symbols.name), 0, counts->object_symbols };
    columns[SNAPSHOT_REGION_SYMBOL_ATOM] = (SnapshotColumn){ (void**)&link->symbols.atom, sizeof(*link->symbols.atom), 0, counts->object_symbols };
    columns[SNAPSHOT_REGION_SYMBOL_VALUE] = (SnapshotColumn){ (void**)&link->symbols.value, sizeof(*link->symbols.value), 0, counts->object_symbols };
    columns[SNAPSHOT_REGION_SYMBOL_SIZE] = (SnapshotColumn){ (void**)&link->symbols.size, sizeof(*link->symbols.size), 0, counts->object_symbols };
    columns[SNAPSHOT_REGION_SYMBOL_FLAGS] = (SnapshotColumn){ (void**)&link->symbols.flags, sizeof(*link->symbols.flags), 0, counts->object_symbols };
    columns[SNAPSHOT_REGION_SYMBOL_SOURCE] = (SnapshotColumn){ (void**)&link->symbols.source, sizeof(*link->symbols.source), 0, counts->object_symbols };
    columns[SNAPSHOT_REGION_FIXUP_OFFSET] = (SnapshotColumn){ (void**)&link->fixups.offset, sizeof(*link->fixups.offset), 0, counts->object_fixups };
    columns[SNAPSHOT_REGION_FIXUP_TARGET] = (SnapshotColumn){ (void**)&link->fixups.target, sizeof(*link->fixups.target), 0, counts->object_fixups };
    columns[SNAPSHOT_REGION_FIXUP_ADDEND] = (SnapshotColumn){ (void**)&link->fixups.addend, sizeof(*link->fixups.addend), 0, counts->object_fixups };
    columns[SNAPSHOT_REGION_FIXUP_KIND] = (SnapshotColumn){ (void**)&link->fixups.kind, sizeof(*link->fixups.kind), 0, counts->object_fixups };
    columns[SNAPSHOT_REGION_WIDE_ADDENDS] = (SnapshotColumn){ (void**)&link->fixups.wide_addends, sizeof(*link->fixups.wide_addends), 0, counts->object_wide_addends };
    columns[SNAPSHOT_REGION_PRIORITY_ATOM] = (SnapshotColumn){ (void**)&link->priorities.atom, sizeof(*link->priorities.atom), 0, counts->object_priorities };
    columns[SNAPSHOT_REGION_PRIORITY_VALUE] = (SnapshotColumn){ (void**)&link->priorities.priority, sizeof(*link->priorities.priority), 0, counts->object_priorities };
    columns[SNAPSHOT_REGION_SYMBOL_NAMES] = (SnapshotColumn){ (void**)&runtime->symbol_names, sizeof(*runtime->symbol_names), 0, counts->object_symbols };
    columns[SNAPSHOT_REGION_DATA] = (SnapshotColumn){ 0, 1, 0, 0 };
    columns[SNAPSHOT_REGION_MEMBER_NAME_BYTES] = (SnapshotColumn){ (void**)&members->names.bytes, sizeof(*members->names.bytes), counts->object_names, member_names };
    columns[SNAPSHOT_REGION_MEMBER_NAME_LENGTH] = (SnapshotColumn){ (void**)&members->names.length, sizeof(*members->names.length), counts->object_names, member_names };
    columns[SNAPSHOT_REGION_MEMBER_NAME_HASH] = (SnapshotColumn){ (void**)&members->names.hash, sizeof(*members->names.hash), counts->object_names, member_names };
    columns[SNAPSHOT_REGION_MEMBER_ATOM_CLASS] = (SnapshotColumn){ (void**)&members->atoms.class, sizeof(*members->atoms.class), counts->object_atoms, member_atoms };
    columns[SNAPSHOT_REGION_MEMBER_ATOM_ALIGNMENT] =
        (SnapshotColumn){ (void**)&members->atoms.alignment_log2, sizeof(*members->atoms.alignment_log2), counts->object_atoms, member_atoms };
    columns[SNAPSHOT_REGION_MEMBER_ATOM_UNIT] = (SnapshotColumn){ (void**)&members->atoms.unit, sizeof(*members->atoms.unit), counts->object_atoms, member_atoms };
    columns[SNAPSHOT_REGION_MEMBER_ATOM_SIZE] = (SnapshotColumn){ (void**)&members->atoms.size, sizeof(*members->atoms.size), counts->object_atoms, member_atoms };
    columns[SNAPSHOT_REGION_MEMBER_ATOM_BYTES] = (SnapshotColumn){ (void**)&members->atoms.bytes, sizeof(*members->atoms.bytes), counts->object_atoms, member_atoms };
    columns[SNAPSHOT_REGION_MEMBER_ATOM_FIXUP_BEGIN] =
        (SnapshotColumn){ (void**)&members->atoms.fixup_begin, sizeof(*members->atoms.fixup_begin), counts->object_atoms, member_atoms };
    columns[SNAPSHOT_REGION_MEMBER_ATOM_FIXUP_COUNT] =
        (SnapshotColumn){ (void**)&members->atoms.fixup_count, sizeof(*members->atoms.fixup_count), counts->object_atoms, member_atoms };
    columns[SNAPSHOT_REGION_MEMBER_SYMBOL_NAME] = (SnapshotColumn){ (void**)&members->symbols.name, sizeof(*members->symbols.name), counts->object_symbols, member_symbols };
    columns[SNAPSHOT_REGION_MEMBER_SYMBOL_ATOM] = (SnapshotColumn){ (void**)&members->symbols.atom, sizeof(*members->symbols.atom), counts->object_symbols, member_symbols };
    columns[SNAPSHOT_REGION_MEMBER_SYMBOL_VALUE] = (SnapshotColumn){ (void**)&members->symbols.value, sizeof(*members->symbols.value), counts->object_symbols, member_symbols };
    columns[SNAPSHOT_REGION_MEMBER_SYMBOL_SIZE] = (SnapshotColumn){ (void**)&members->symbols.size, sizeof(*members->symbols.size), counts->object_symbols, member_symbols };
    columns[SNAPSHOT_REGION_MEMBER_SYMBOL_FLAGS] = (SnapshotColumn){ (void**)&members->symbols.flags, sizeof(*members->symbols.flags), counts->object_symbols, member_symbols };
    columns[SNAPSHOT_REGION_MEMBER_SYMBOL_SOURCE] =
        (SnapshotColumn){ (void**)&members->symbols.source, sizeof(*members->symbols.source), counts->object_symbols, member_symbols };
    columns[SNAPSHOT_REGION_MEMBER_FIXUP_OFFSET] = (SnapshotColumn){ (void**)&members->fixups.offset, sizeof(*members->fixups.offset), counts->object_fixups, member_fixups };
    columns[SNAPSHOT_REGION_MEMBER_FIXUP_TARGET] = (SnapshotColumn){ (void**)&members->fixups.target, sizeof(*members->fixups.target), counts->object_fixups, member_fixups };
    columns[SNAPSHOT_REGION_MEMBER_FIXUP_ADDEND] = (SnapshotColumn){ (void**)&members->fixups.addend, sizeof(*members->fixups.addend), counts->object_fixups, member_fixups };
    columns[SNAPSHOT_REGION_MEMBER_FIXUP_KIND] = (SnapshotColumn){ (void**)&members->fixups.kind, sizeof(*members->fixups.kind), counts->object_fixups, member_fixups };
    columns[SNAPSHOT_REGION_MEMBER_WIDE_ADDENDS] =
        (SnapshotColumn){ (void**)&members->fixups.wide_addends, sizeof(*members->fixups.wide_addends), counts->object_wide_addends, member_wide };
    columns[SNAPSHOT_REGION_MEMBER_PRIORITY_ATOM] =
        (SnapshotColumn){ (void**)&members->priorities.atom, sizeof(*members->priorities.atom), counts->object_priorities, member_priorities };
    columns[SNAPSHOT_REGION_MEMBER_PRIORITY_VALUE] =
        (SnapshotColumn){ (void**)&members->priorities.priority, sizeof(*members->priorities.priority), counts->object_priorities, member_priorities };
    columns[SNAPSHOT_REGION_MEMBER_SYMBOL_NAMES] =
        (SnapshotColumn){ (void**)&runtime->member_symbol_names, sizeof(*runtime->member_symbol_names), counts->object_symbols, member_symbols };
    columns[SNAPSHOT_REGION_ATOM_ADDRESS] = (SnapshotColumn){ (void**)&link->atoms.address, sizeof(*link->atoms.address), 0, counts->atoms };
    columns[SNAPSHOT_REGION_SYMBOL_GOT_SLOT] = (SnapshotColumn){ (void**)&link->symbols.got_slot, sizeof(*link->symbols.got_slot), 0, counts->symbols };
    columns[SNAPSHOT_REGION_SYMBOL_DYNAMIC_INDEX] = (SnapshotColumn){ (void**)&link->symbols.dynamic_index, sizeof(*link->symbols.dynamic_index), 0, counts->symbols };
    columns[SNAPSHOT_REGION_SYMBOL_VERSION] = (SnapshotColumn){ (void**)&link->symbols.version, sizeof(*link->symbols.version), 0, counts->symbols };
    columns[SNAPSHOT_REGION_SYMBOL_REFERENCE] = (SnapshotColumn){ (void**)&link->symbols.reference, sizeof(*link->symbols.reference), 0, counts->symbols };
    columns[SNAPSHOT_REGION_BINDINGS] = (SnapshotColumn){ (void**)&runtime->bindings, sizeof(RuntimeBinding), 0, counts->symbols };
}

// The place in a snapshot's file of record `record` of a column binding reads, whose crt objects' records are in
// `region` (from SNAPSHOT_REGION_NAME_BYTES to SNAPSHOT_REGION_SYMBOL_NAMES): in that region, or in the members'
// (snapshot_member_region). For tests that change a piece's records.
static __attribute__((unused)) uint64_t snapshot_record_offset(const SnapshotHeader* header, SnapshotRegion region, uint64_t record)
{
    // snapshot_columns only takes its columns' addresses.
    Runtime places;
    SnapshotColumn columns[SNAPSHOT_REGIONS];
    snapshot_columns(&places, &header->counts, columns);
    const SnapshotRegion holder = record < columns[region].count ? region : snapshot_member_region(region);
    return header->regions[holder].offset + (record - columns[holder].first) * columns[holder].element;
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
    for (uint32_t a = 0; a < BUSTER_RUNTIME_ARCHIVES; a += 1)
    {
        paths[SNAPSHOT_PATH_ARCHIVES + a] = &runtime->archives[a].path;
    }
    paths[SNAPSHOT_PATH_LOADER] = &runtime->loader_path;
    paths[SNAPSHOT_PATH_SONAME] = &runtime->libc.soname;
    paths[SNAPSHOT_PATH_LOADER_SONAME] = &runtime->loader.soname;
    paths[SNAPSHOT_PATH_GCC_S] = &runtime->gcc_s_path;
    paths[SNAPSHOT_PATH_GCC_S_SONAME] = &runtime->gcc_s.soname;
    paths[SNAPSHOT_PATH_ABI_NOTE] = &runtime->abi_note;
}

// The file name of a kind's snapshot, in the cache directory.
static const char* snapshot_name(RuntimeKind kind)
{
    static const char* const names[RUNTIME_KINDS] = {
        [RUNTIME_KIND_DYNAMIC] = BUSTER_SNAPSHOT_NAME,
        [RUNTIME_KIND_STATIC] = BUSTER_SNAPSHOT_STATIC_NAME,
        [RUNTIME_KIND_STATIC_PIE] = BUSTER_SNAPSHOT_STATIC_PIE_NAME,
        [RUNTIME_KIND_SHARED] = BUSTER_SNAPSHOT_SHARED_NAME,
    };
    return names[kind];
}

static bool snapshot_same_identity(const RuntimeProbe* a, const RuntimeProbe* b)
{
    return a->found == b->found && a->device == b->device && a->inode == b->inode && a->size == b->size && a->modified_seconds == b->modified_seconds &&
           a->modified_nanoseconds == b->modified_nanoseconds && a->changed_seconds == b->changed_seconds && a->changed_nanoseconds == b->changed_nanoseconds;
}

// On-disk probes are byte records until their bool representations are checked.
static bool snapshot_valid_probe(const RuntimeProbe* probe)
{
    const uint8_t* bytes = (const uint8_t*)probe;
    return bytes[offsetof(RuntimeProbe, directory)] <= 1 && bytes[offsetof(RuntimeProbe, found)] <= 1 && bytes[offsetof(RuntimeProbe, deferred)] <= 1;
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

// The hash of a snapshot's directory, its first `bytes` bytes, with its header's hash as zeros.
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

// A string of the strings, from its offset plus 1.
static String8 snapshot_string(uint8_t* strings, SnapshotString string)
{
    String8 result = {
        .pointer = string.offset != 0 ? (char*)strings + string.offset - 1 : 0,
        .length = string.length,
    };
    return result;
}

// Points the runtime's columns into a snapshot's bytes (a mapped file, or the writer's image) and fills what its header
// counts, as a loaded runtime has them, writing nothing there: the members' names and the link's names' and atoms' bytes
// stay offsets, which the runtime's deltas make pointers (Runtime.byte_delta and string_delta). The probes and versions
// keep theirs until snapshot_rebase. The members' records of a column binding reads go into Runtime.member_link's,
// placed so that the runtime's ids index it as they index the crt objects' in Runtime.link's: the region less the
// first member's elements, which the crt objects' region and the data before it hold (snapshot_valid_header).
// `columns` are the header's columns of `runtime` (snapshot_columns), which loading has from its checks.
static void snapshot_view_columns(Runtime* runtime, uint8_t* file, uint64_t bytes, const SnapshotColumn* columns)
{
    const SnapshotHeader* header = (const SnapshotHeader*)file;
    const SnapshotCounts counts = header->counts;
    *runtime = (Runtime){ 0 };
    for (uint32_t r = 0; r < SNAPSHOT_REGIONS; r += 1)
    {
        if (r != SNAPSHOT_REGION_STRINGS && r != SNAPSHOT_REGION_DATA)
        {
            *columns[r].column = file + header->regions[r].offset - columns[r].first * columns[r].element;
        }
    }
    uint8_t* strings = file + header->regions[SNAPSHOT_REGION_STRINGS].offset;
    uint8_t* data = file + header->regions[SNAPSHOT_REGION_DATA].offset;
    runtime->snapshot_data = data;
    runtime->byte_delta = (uintptr_t)data - 1;
    runtime->string_delta = (uintptr_t)strings - 1;
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
    // The names run's first part, the crt objects' names; and its second, the members', placed as its offsets in the run
    // index it (Runtime.member_names).
    runtime->names = (Arena){
        .base = data + counts.names_run,
        .length = counts.object_name_bytes,
        .capacity = counts.object_name_bytes,
    };
    runtime->member_names = data + counts.member_names_run - counts.object_name_bytes;
    runtime->probe_count = counts.probes;
    runtime->probe_capacity = counts.probes;
    runtime->libc.version_count = counts.versions;
    runtime->libc.exports.count = counts.exports;
    runtime->loader.version_count = counts.loader_versions;
    runtime->loader.exports.count = counts.loader_exports;
    runtime->gcc_s.version_count = counts.gcc_s_versions;
    runtime->gcc_s.exports.count = counts.gcc_s_exports;
    runtime->member_count = counts.members;
    runtime->member_unit = counts.member_unit;
    runtime->kind = (RuntimeKind)counts.kind;
    runtime->archive_count = counts.archives;
    for (uint32_t a = 0; a < counts.archives; a += 1)
    {
        RuntimeArchive* archive = &runtime->archives[a];
        const uint32_t end = a + 1 < counts.archives ? counts.first_members[a + 1] : counts.members;
        archive->first_member = counts.first_members[a];
        archive->archive.members = runtime->members + counts.first_members[a];
        archive->archive.member_count = end - counts.first_members[a];
    }
    runtime->group_signatures = counts.group_signatures;
    link->priorities.count = counts.priorities;
    link->priorities.capacity = counts.priorities;
    runtime->global_names.strings = strings + counts.global_strings;
    runtime->global_names.strings_length = counts.global_strings_bytes;
    runtime->global_names.count = counts.globals;
    runtime->global_names.index.bucket_bits = counts.bucket_bits;
    runtime->global_names.filter_bits = counts.filter_bits;
    runtime->name_room = counts.name_room;
    runtime->atom_room = counts.atom_room;
    // The members' link: the counts and the whole columns are the crt objects' link's.
    Link* members = &runtime->member_link;
    members->names.count = link->names.count;
    members->names.capacity = link->names.capacity;
    members->atoms.count = link->atoms.count;
    members->atoms.capacity = link->atoms.capacity;
    members->atoms.address = link->atoms.address;
    members->symbols.count = link->symbols.count;
    members->symbols.capacity = link->symbols.capacity;
    members->symbols.got_slot = link->symbols.got_slot;
    members->symbols.dynamic_index = link->symbols.dynamic_index;
    members->symbols.version = link->symbols.version;
    members->symbols.reference = link->symbols.reference;
    members->fixups.count = link->fixups.count;
    members->fixups.capacity = link->fixups.capacity;
    members->fixups.wide_count = link->fixups.wide_count;
    members->fixups.wide_capacity = link->fixups.wide_capacity;
    members->priorities.count = link->priorities.count;
    members->priorities.capacity = link->priorities.capacity;
    members->name_arena = link->name_arena;
    runtime->snapshot = file;
    runtime->snapshot_bytes = bytes;
}

static void snapshot_view(Runtime* runtime, uint8_t* file, uint64_t bytes)
{
    SnapshotColumn columns[SNAPSHOT_REGIONS];
    snapshot_columns(runtime, &((const SnapshotHeader*)file)->counts, columns);
    snapshot_view_columns(runtime, file, bytes, columns);
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

// The counts of a runtime whose pieces are one after another (snapshot_pieces_contiguous): the crt objects' records end
// where the first member's begin. The names run's places in the data come with the data (snapshot_image).
static SnapshotCounts snapshot_counts(const Runtime* runtime)
{
    const Link* link = &runtime->link;
    const uint32_t pieces = RUNTIME_OBJECTS + runtime->member_count;
    const ImportMark objects = runtime->pieces[RUNTIME_OBJECTS - 1].end;
    SnapshotCounts result = {
        .kind = runtime->kind,
        .archives = runtime->archive_count,
        .probes = runtime->probe_count,
        .versions = runtime->libc.version_count,
        .loader_versions = runtime->loader.version_count,
        .gcc_s_versions = runtime->gcc_s.version_count,
        .members = runtime->member_count,
        .names = link->names.count,
        .atoms = link->atoms.count,
        .symbols = link->symbols.count,
        .fixups = link->fixups.count,
        .wide_addends = link->fixups.wide_count,
        .priorities = link->priorities.count,
        .pieces = pieces,
        .references = runtime->reference_begin[pieces],
        .groups = runtime->group_begin[pieces],
        .group_atoms = runtime->group_begin[pieces] != 0 ? runtime->groups[runtime->group_begin[pieces] - 1].first + runtime->groups[runtime->group_begin[pieces] - 1].count : 0,
        .group_signatures = runtime->group_signatures,
        .globals = runtime->global_names.count,
        .bucket_bits = runtime->global_names.index.bucket_bits,
        .filter_bits = runtime->global_names.filter_bits,
        .exports = runtime->libc.exports.count,
        .loader_exports = runtime->loader.exports.count,
        .gcc_s_exports = runtime->gcc_s.exports.count,
        .member_unit = runtime->member_unit,
        .object_names = objects.names,
        .object_atoms = objects.atoms,
        .object_symbols = objects.symbols,
        .object_fixups = objects.fixups,
        .object_wide_addends = objects.wide_addends,
        .object_priorities = objects.priorities,
        .names_run_bytes = runtime->names.length,
        .object_name_bytes = objects.name_bytes,
        .global_strings_bytes = runtime->global_names.strings_length,
        .name_room = runtime->name_room,
        .atom_room = runtime->atom_room,
    };
    for (uint32_t a = 0; a < runtime->archive_count; a += 1)
    {
        result.first_members[a] = runtime->archives[a].first_member;
    }
    return result;
}

// Whether each table's records from record 1 on, and the names run, are the pieces' one after another, in piece order
// and none left over: a piece's records are then ranges of the columns, and its atoms' bytes a range of the data.
static bool snapshot_pieces_contiguous(const Runtime* runtime)
{
    const Link* link = &runtime->link;
    const uint32_t pieces = RUNTIME_OBJECTS + runtime->member_count;
    const ImportMark first = runtime->pieces[0].begin;
    const ImportMark last = runtime->pieces[pieces - 1].end;
    bool result = first.names == 1 && first.atoms == 1 && first.symbols == 1 && first.fixups == 1 && first.wide_addends == 1 && first.priorities == 1 &&
                  last.names == link->names.count && last.atoms == link->atoms.count && last.symbols == link->symbols.count && last.fixups == link->fixups.count &&
                  last.wide_addends == link->fixups.wide_count && last.priorities == link->priorities.count && last.name_bytes == runtime->names.length;
    for (uint32_t p = 0; result && p + 1 < pieces; p += 1)
    {
        const ImportMark end = runtime->pieces[p].end;
        const ImportMark next = runtime->pieces[p + 1].begin;
        result = end.names == next.names && end.atoms == next.atoms && end.symbols == next.symbols && end.fixups == next.fixups && end.wide_addends == next.wide_addends &&
                 end.priorities == next.priorities && end.name_bytes == next.name_bytes;
    }
    return result;
}

// Whether the writer copies a region from the runtime's column as it is: every one but the records with pointers, the
// strings, the pieces' hashes and data, the names' and atoms' bytes (both regions of each) and the data, which it fills
// itself.
static bool snapshot_region_copied(uint32_t region)
{
    return region != SNAPSHOT_REGION_PROBES && region != SNAPSHOT_REGION_VERSIONS && region != SNAPSHOT_REGION_MEMBERS && region != SNAPSHOT_REGION_STRINGS &&
           region != SNAPSHOT_REGION_PIECE_HASHES && region != SNAPSHOT_REGION_PIECE_DATA && region != SNAPSHOT_REGION_NAME_BYTES &&
           region != SNAPSHOT_REGION_ATOM_BYTES && region != SNAPSHOT_REGION_MEMBER_NAME_BYTES && region != SNAPSHOT_REGION_MEMBER_ATOM_BYTES &&
           region != SNAPSHOT_REGION_DATA;
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
    uint64_t result = runtime->global_names.strings_length + 1;
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
    for (uint32_t v = 0; v < runtime->loader.version_count; v += 1)
    {
        result += runtime->loader.versions[v].name.length + 1;
    }
    for (uint32_t v = 0; v < runtime->gcc_s.version_count; v += 1)
    {
        result += runtime->gcc_s.versions[v].name.length + 1;
    }
    for (uint32_t m = 0; m < runtime->member_count; m += 1)
    {
        result += runtime->members[m].name.length + 1;
    }
    return result;
}

// The bytes the data needs: the names run, then every atom's bytes, without NULs.
static uint64_t snapshot_data_bytes(const Runtime* runtime)
{
    uint64_t result = runtime->names.length;
    for (AtomId atom = 1; atom < runtime->link.atoms.count; atom += 1)
    {
        result += runtime->link.atoms.bytes[atom] != 0 ? runtime->link.atoms.size[atom] : 0;
    }
    return result;
}

// The file's image of the runtime, from `arena`: its bytes and size, or 0 if the arena has no room, a pointer of the
// runtime's isn't in what it should point into, or its pieces' records aren't one after another
// (snapshot_pieces_contiguous). The pieces' hashes come last, over the image as a loaded runtime sees it
// (snapshot_view), then the directory's.
static uint8_t* snapshot_image(const Runtime* runtime, const RuntimeProbe* compiler, Arena* arena, uint64_t* file_bytes)
{
    // An imported runtime's link holds every piece's records: the members' regions copy its columns too.
    Runtime source = *runtime;
    source.member_link = source.link;
    source.member_symbol_names = source.symbol_names;
    SnapshotColumn columns[SNAPSHOT_REGIONS];
    SnapshotHeader header = {
        .magic = BUSTER_SNAPSHOT_MAGIC,
        .version = BUSTER_SNAPSHOT_VERSION,
        .header_bytes = sizeof(SnapshotHeader),
        .counts = snapshot_counts(runtime),
    };
    snapshot_columns(&source, &header.counts, columns);
    const uint64_t strings_bytes = snapshot_strings_bytes(runtime);
    const uint64_t data_bytes = snapshot_data_bytes(runtime);
    uint64_t end = sizeof(SnapshotHeader);
    for (uint32_t r = 0; r < SNAPSHOT_REGIONS; r += 1)
    {
        const uint64_t alignment = r == 0 ? BUSTER_SNAPSHOT_PAGE_BYTES : snapshot_region_alignment(r);
        const uint64_t offset = (end + alignment - 1) & ~(alignment - 1);
        const uint64_t bytes = r == SNAPSHOT_REGION_STRINGS ? strings_bytes : r == SNAPSHOT_REGION_DATA ? data_bytes : columns[r].element * columns[r].count;
        header.regions[r] = (SnapshotPlace){ offset, bytes };
        end = offset + bytes;
    }
    header.directory_bytes = header.regions[BUSTER_SNAPSHOT_PIECE_REGIONS].offset;
    uint8_t* image = arena_push(arena, end, BUSTER_SNAPSHOT_ALIGNMENT);
    bool fits = image != 0 && snapshot_pieces_contiguous(runtime);
    if (fits)
    {
        memset(image, 0, end);
        for (uint32_t r = 0; r < SNAPSHOT_REGIONS; r += 1)
        {
            if (snapshot_region_copied(r) && header.regions[r].bytes != 0)
            {
                memcpy(image + header.regions[r].offset, (const uint8_t*)*columns[r].column + columns[r].first * columns[r].element, header.regions[r].bytes);
            }
        }
        // The strings: the global names' first, so that their offset is the counts', then the paths, then the records'
        // strings, each written in its record as it goes in.
        SnapshotStrings strings = {
            .bytes = image + header.regions[SNAPSHOT_REGION_STRINGS].offset,
            .capacity = strings_bytes,
        };
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
            probes[p].deferred = probe->deferred;
            probes[p].device = probe->device;
            probes[p].inode = probe->inode;
            probes[p].size = probe->size;
            probes[p].modified_seconds = probe->modified_seconds;
            probes[p].modified_nanoseconds = probe->modified_nanoseconds;
            probes[p].changed_seconds = probe->changed_seconds;
            probes[p].changed_nanoseconds = probe->changed_nanoseconds;
        }
        // libc.so.6's versions, then the loader's and libgcc_s.so.1's, each with its name in the strings.
        const ImportLibrary* const libraries[] = { &runtime->libc, &runtime->loader, &runtime->gcc_s };
        static const SnapshotRegion regions[] = { SNAPSHOT_REGION_VERSIONS, SNAPSHOT_REGION_LOADER_VERSIONS, SNAPSHOT_REGION_GCC_S_VERSIONS };
        for (uint32_t library = 0; library < BUSTER_ARRAY_LENGTH(libraries); library += 1)
        {
            const ImportLibrary* source = libraries[library];
            const SnapshotRegion region = regions[library];
            ImportVersion* versions = (ImportVersion*)(image + header.regions[region].offset);
            for (uint32_t v = 0; v < source->version_count; v += 1)
            {
                const ImportVersion* version = &source->versions[v];
                versions[v].name.pointer = (char*)(uintptr_t)snapshot_add_string(&strings, version->name.pointer, version->name.length, &fits);
                versions[v].name.length = version->name.length;
                versions[v].hash = version->hash;
                versions[v].index = version->index;
                versions[v].base = version->base;
            }
        }
        ImportMember* members = (ImportMember*)(image + header.regions[SNAPSHOT_REGION_MEMBERS].offset);
        for (uint32_t m = 0; m < header.counts.members; m += 1)
        {
            const String8 name = runtime_member_name(runtime, m);
            members[m].name.pointer = (char*)(uintptr_t)snapshot_add_string(&strings, name.pointer, name.length, &fits);
            members[m].name.length = name.length;
            members[m].offset = runtime->members[m].offset;
        }
        fits = fits && strings.length == strings_bytes;
        // The data: the crt objects' names (the names run's first object_name_bytes bytes), then each piece's atoms' bytes
        // in atom order, where each piece's start is recorded, then the members' names (the rest of the run); the names'
        // and atoms' columns hold their offsets there plus 1, each in its piece's region.
        uint8_t* data = image + header.regions[SNAPSHOT_REGION_DATA].offset;
        const uint64_t object_name_bytes = header.counts.object_name_bytes;
        const uint64_t names_length = runtime->names.length;
        fits = fits && object_name_bytes <= names_length;
        header.counts.names_run = 0;
        header.counts.member_names_run = object_name_bytes + (data_bytes - names_length);
        if (fits)
        {
            memcpy(data, runtime->names.base, object_name_bytes);
            memcpy(data + header.counts.member_names_run, runtime->names.base + object_name_bytes, names_length - object_name_bytes);
        }
        const Link* link = &runtime->link;
        const uint32_t object_names = header.counts.object_names;
        uint64_t* object_name_column = (uint64_t*)(image + header.regions[SNAPSHOT_REGION_NAME_BYTES].offset);
        uint64_t* member_name_column = (uint64_t*)(image + header.regions[SNAPSHOT_REGION_MEMBER_NAME_BYTES].offset);
        for (NameId name = 1; fits && name < header.counts.names; name += 1)
        {
            // A crt object's name is in the run's first part, a member's in the rest.
            const bool object = name < object_names;
            const uint8_t* bytes = runtime_name_bytes(runtime, name);
            const uintptr_t place = (uintptr_t)bytes - (uintptr_t)runtime->names.base;
            const uint64_t from = object ? 0 : object_name_bytes;
            const uint64_t to = object ? object_name_bytes : names_length;
            const bool inside = bytes != 0 && place >= from && place <= to && to - place >= link->names.length[name];
            const uint64_t offset = object ? place : header.counts.member_names_run + (place - object_name_bytes);
            fits = inside;
            if (object)
            {
                object_name_column[name] = offset + 1;
            }
            else
            {
                member_name_column[name - object_names] = offset + 1;
            }
        }
        const uint32_t object_atoms = header.counts.object_atoms;
        uint64_t* object_atom_column = (uint64_t*)(image + header.regions[SNAPSHOT_REGION_ATOM_BYTES].offset);
        uint64_t* member_atom_column = (uint64_t*)(image + header.regions[SNAPSHOT_REGION_MEMBER_ATOM_BYTES].offset);
        uint64_t* piece_data = (uint64_t*)(image + header.regions[SNAPSHOT_REGION_PIECE_DATA].offset);
        uint64_t at = object_name_bytes;
        for (uint32_t p = 0; fits && p < header.counts.pieces; p += 1)
        {
            piece_data[p] = at;
            for (AtomId atom = runtime->pieces[p].begin.atoms; atom < runtime->pieces[p].end.atoms; atom += 1)
            {
                const uint8_t* bytes = runtime_atom_bytes(runtime, atom);
                const uint64_t size = bytes != 0 ? link->atoms.size[atom] : 0;
                const uint64_t value = bytes != 0 ? at + 1 : 0;
                if (atom < object_atoms)
                {
                    object_atom_column[atom] = value;
                }
                else
                {
                    member_atom_column[atom - object_atoms] = value;
                }
                if (bytes != 0)
                {
                    memcpy(data + at, bytes, size);
                }
                at += size;
            }
        }
        piece_data[header.counts.pieces] = at;
        fits = fits && at == header.counts.member_names_run;
    }
    if (fits)
    {
        header.file_bytes = end;
        header.compiler = *compiler;
        header.compiler.path = (String8){ 0 };
        memcpy(image, &header, sizeof(header));
        Runtime view;
        snapshot_view(&view, image, end);
        uint8_t* hashes = image + header.regions[SNAPSHOT_REGION_PIECE_HASHES].offset;
        for (uint32_t p = 0; p < header.counts.pieces; p += 1)
        {
            runtime_piece_hash(&view, p, hashes + 16 * (uint64_t)p);
        }
        snapshot_hash(image, header.directory_bytes, header.hash);
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

// Writes the runtime's snapshot into `directory` as `compiler` wrote it, under its kind's name (snapshot_name).
static bool snapshot_write(const Runtime* runtime, const char* directory, const RuntimeProbe* compiler, SnapshotResult* result)
{
    char path[BUSTER_RUNTIME_PATH_BYTES];
    const int length = snprintf(path, sizeof(path), "%s/%s", directory, snapshot_name(runtime->kind));
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

// The records with pointers the loader writes, a few dozen: the probes' paths and the libraries' versions' names,
// offsets into the strings plus 1 until now.
static void snapshot_rebase(Runtime* runtime)
{
    const SnapshotHeader* header = (const SnapshotHeader*)runtime->snapshot;
    uint8_t* strings = runtime->snapshot + header->regions[SNAPSHOT_REGION_STRINGS].offset;
    for (uint32_t p = 0; p < runtime->probe_count; p += 1)
    {
        runtime->probes[p].path = snapshot_string(strings, (SnapshotString){ (uint64_t)(uintptr_t)runtime->probes[p].path.pointer, runtime->probes[p].path.length });
    }
    for (uint32_t v = 0; v < runtime->libc.version_count; v += 1)
    {
        ImportVersion* version = &runtime->libc.versions[v];
        version->name = snapshot_string(strings, (SnapshotString){ (uint64_t)(uintptr_t)version->name.pointer, version->name.length });
    }
    for (uint32_t v = 0; v < runtime->loader.version_count; v += 1)
    {
        ImportVersion* version = &runtime->loader.versions[v];
        version->name = snapshot_string(strings, (SnapshotString){ (uint64_t)(uintptr_t)version->name.pointer, version->name.length });
    }
    for (uint32_t v = 0; v < runtime->gcc_s.version_count; v += 1)
    {
        ImportVersion* version = &runtime->gcc_s.versions[v];
        version->name = snapshot_string(strings, (SnapshotString){ (uint64_t)(uintptr_t)version->name.pointer, version->name.length });
    }
}

// Whether the header describes a file of this format and of `bytes` bytes, with each region inside it, aligned and of
// its column's size, the directory's before its end and the pieces' after it, the members' records of a column after
// as many bytes as the crt objects' (where snapshot_view places the column), and the names run's two parts inside the
// data. `columns` gets the header's columns of `runtime`, whose fields this doesn't touch (snapshot_columns), for the
// view.
static bool snapshot_valid_header(const SnapshotHeader* header, uint64_t bytes, Runtime* runtime, SnapshotColumn* columns)
{
    const SnapshotCounts* counts = &header->counts;
    bool result = snapshot_valid_probe(&header->compiler) && header->magic == BUSTER_SNAPSHOT_MAGIC && header->version == BUSTER_SNAPSHOT_VERSION && header->header_bytes == sizeof(SnapshotHeader) &&
                  header->file_bytes == bytes && counts->bucket_bits < 32 && counts->filter_bits < 32 && counts->kind < RUNTIME_KINDS &&
                  counts->archives <= BUSTER_RUNTIME_ARCHIVES && counts->pieces < UINT32_MAX && counts->pieces >= RUNTIME_OBJECTS &&
                  header->directory_bytes == header->regions[BUSTER_SNAPSHOT_PIECE_REGIONS].offset && header->directory_bytes <= bytes &&
                  header->directory_bytes >= sizeof(SnapshotHeader) && counts->object_names <= counts->names && counts->object_atoms <= counts->atoms &&
                  counts->object_symbols <= counts->symbols && counts->object_fixups <= counts->fixups && counts->object_wide_addends <= counts->wide_addends &&
                  counts->object_priorities <= counts->priorities;
    // The archives' members, one after another.
    for (uint32_t a = 0; result && a < counts->archives; a += 1)
    {
        result = counts->first_members[a] <= counts->members && (a == 0 || counts->first_members[a - 1] <= counts->first_members[a]);
    }
    snapshot_columns(runtime, &header->counts, columns);
    for (uint32_t r = 0; result && r < SNAPSHOT_REGIONS; r += 1)
    {
        const SnapshotPlace place = header->regions[r];
        const bool sized = r == SNAPSHOT_REGION_STRINGS || r == SNAPSHOT_REGION_DATA || place.bytes == columns[r].element * columns[r].count;
        const bool side = r < BUSTER_SNAPSHOT_PIECE_REGIONS ? place.offset + place.bytes <= header->directory_bytes : place.offset >= header->directory_bytes;
        result = place.offset % snapshot_region_alignment(r) == 0 && place.offset >= sizeof(SnapshotHeader) && place.offset <= bytes && place.bytes <= bytes - place.offset &&
                 sized &&
                 side && columns[r].first * columns[r].element <= place.offset;
    }
    const SnapshotPlace data = header->regions[SNAPSHOT_REGION_DATA];
    const SnapshotPlace strings = header->regions[SNAPSHOT_REGION_STRINGS];
    const uint64_t member_name_bytes = counts->names_run_bytes - counts->object_name_bytes;
    result = result && counts->object_name_bytes <= counts->names_run_bytes && counts->names_run <= data.bytes && counts->object_name_bytes <= data.bytes - counts->names_run &&
             counts->member_names_run >= counts->object_name_bytes && counts->member_names_run <= data.bytes && member_name_bytes <= data.bytes - counts->member_names_run &&
             counts->global_strings_bytes <= strings.bytes && counts->global_strings <= strings.bytes - counts->global_strings_bytes;
    return result;
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

// Loads the runtime from the snapshot at `path` if `compiler` wrote it, it is of `kind` and it still describes the
// system: its header and its directory's hash checked, its columns pointed into the mapping and its probes compared.
// Its pieces are checked later, each when binding first reads it (runtime_piece_current). The runtime is left empty
// otherwise. With `times`, each phase's nanoseconds are added to it, by SnapshotPhase.
static SnapshotStatus snapshot_load(Runtime* runtime, const char* path, RuntimeKind kind, const RuntimeProbe* compiler, uint64_t* times)
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
        SnapshotColumn columns[SNAPSHOT_REGIONS];
        bool valid = snapshot_valid_header(header, bytes, runtime, columns);
        if (valid)
        {
            snapshot_hash(file, header->directory_bytes, hash);
            valid = memcmp(hash, header->hash, sizeof(hash)) == 0;
        }
        for (uint32_t p = 0; valid && p < header->counts.probes; p += 1)
        {
            const RuntimeProbe* probes = (const RuntimeProbe*)(file + header->regions[SNAPSHOT_REGION_PROBES].offset);
            valid = snapshot_valid_probe(&probes[p]);
        }
        // A damaged directory may contain an invalid bool representation.
        // Do not inspect its typed identity before validating its bytes/hash.
        const bool current = valid && snapshot_same_identity(&header->compiler, compiler) && header->counts.kind == (uint32_t)kind;
        result = !valid ? SNAPSHOT_STATUS_INVALID : current ? SNAPSHOT_STATUS_LOADED : SNAPSHOT_STATUS_STALE;
        snapshot_time(times, SNAPSHOT_PHASE_HASH, &start);
        if (result == SNAPSHOT_STATUS_LOADED)
        {
            snapshot_view_columns(runtime, file, bytes, columns);
            snapshot_rebase(runtime);
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

// runtime_open of a kind's runtime, as `compiler` (0 for an executable without an identity, which never loads or writes
// a snapshot).
static SnapshotResult runtime_open_as(Runtime* runtime, const char* directory, RuntimeKind kind, const RuntimeProbe* compiler)
{
    SnapshotResult result = { .status = SNAPSHOT_STATUS_MISSING };
    char path[BUSTER_RUNTIME_PATH_BYTES];
    const int length = directory != 0 ? snprintf(path, sizeof(path), "%s/%s", directory, snapshot_name(kind)) : -1;
    const bool cached = compiler != 0 && length > 0 && (uint64_t)length < sizeof(path);
    if (cached)
    {
        result.status = snapshot_load(runtime, path, kind, compiler, 0);
    }
    if (result.status != SNAPSHOT_STATUS_LOADED)
    {
        result.error = runtime_import_kind(runtime, kind);
        result.written = result.error == RUNTIME_ERROR_NONE && cached && snapshot_write(runtime, directory, compiler, &result);
    }
    return result;
}

BUSTER_LINKER_ENTRY SnapshotResult runtime_open(Runtime* runtime, const char* directory)
{
    RuntimeProbe compiler;
    const bool identified = snapshot_compiler(&compiler);
    return runtime_open_as(runtime, directory, RUNTIME_KIND_DYNAMIC, identified ? &compiler : 0);
}

BUSTER_LINKER_ENTRY SnapshotResult runtime_open_static(Runtime* runtime, const char* directory)
{
    RuntimeProbe compiler;
    const bool identified = snapshot_compiler(&compiler);
    return runtime_open_as(runtime, directory, RUNTIME_KIND_STATIC, identified ? &compiler : 0);
}

BUSTER_LINKER_ENTRY SnapshotResult runtime_open_static_pie(Runtime* runtime, const char* directory)
{
    RuntimeProbe compiler;
    const bool identified = snapshot_compiler(&compiler);
    return runtime_open_as(runtime, directory, RUNTIME_KIND_STATIC_PIE, identified ? &compiler : 0);
}

BUSTER_LINKER_ENTRY SnapshotResult runtime_open_shared(Runtime* runtime, const char* directory)
{
    RuntimeProbe compiler;
    const bool identified = snapshot_compiler(&compiler);
    return runtime_open_as(runtime, directory, RUNTIME_KIND_SHARED, identified ? &compiler : 0);
}

static bool snapshot_discard(const char* directory, RuntimeKind kind)
{
    char path[BUSTER_RUNTIME_PATH_BYTES];
    const int length = snprintf(path, sizeof(path), "%s/%s", directory, snapshot_name(kind));
    return length > 0 && (uint64_t)length < sizeof(path) && (unlink(path) == 0 || errno == ENOENT);
}
