// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The C runtime a dynamic PIE or a static executable links (docs/object-writer-and-linker.md, "The runtime
// snapshot"): finding its files as GCC's driver does, and importing them with import.c.
//
// The files, for `cc main.c -o main` on this system (RUNTIME_KIND_DYNAMIC):
//   - GCC's installation: <base>/<triple>/<version> under /usr/lib/gcc or /usr/lib64/gcc, for the triples Linux
//     distributions use, the highest version that has crtbeginS.o;
//   - the crt objects, in the order GCC's driver puts them around the program: Scrt1.o, crti.o and crtbeginS.o before
//     it, crtendS.o and crtn.o after it. Scrt1.o, crti.o and crtn.o are glibc's, found in the library directories
//     (Debian's multiarch ones, /usr/lib64 and /lib64, then /usr/lib and /lib), crtbeginS.o and crtendS.o GCC's;
//   - libc.so, found in the same directories: a linker script naming libc.so.6, libc_nonshared.a and, as needed, the
//     dynamic loader (ld-linux-x86-64.so.2), whose exports a link takes only for a name libc.so.6 doesn't export
//     (__tls_get_addr, _r_debug, __libc_stack_end), as GNU ld and lld take them, making the loader needed.
// And for `cc -static main.c -o main` (RUNTIME_KIND_STATIC):
//   - the crt objects crt1.o, crti.o, crtbeginT.o, crtend.o and crtn.o, in the same order and places;
//   - the archives of the driver's group, --start-group -lgcc -lgcc_eh -latomic_asneeded -lc --end-group, in its order:
//     libgcc.a and libgcc_eh.a, GCC's; libatomic_asneeded.a, which GCC 16 installs as a link to libatomic.a, where -L
//     finds it if GCC has it; and libc.a, where -L finds it: GCC's installation, then the library directories.
// And for `cc -static-pie main.c -o main` (RUNTIME_KIND_STATIC_PIE), the same archives with the crt objects rcrt1.o,
// crti.o, crtbeginS.o, crtendS.o and crtn.o. rcrt1.o doesn't define _dl_relocate_static_pie, as crt1.o does (an empty
// one), so libc.a's member that relocates the executable joins, which refers to _DYNAMIC.
// And for `cc -shared lib.c -o lib.so` (RUNTIME_KIND_SHARED), a dynamic PIE's but its start object: crti.o,
// crtbeginS.o, crtendS.o and crtn.o, with libc.so's libc.so.6, libc_nonshared.a and loader. The start object's unit and
// piece are there, empty, so that each object keeps its unit (RuntimeObject), and nothing refers to main.
// Each path probed, found or absent, is recorded with what identifies a found file (st_dev, st_ino, st_size, st_mtim
// and st_ctim), so that the snapshot can tell with one stat per path whether it still describes the system
// (runtime_probes_current). Among them are the directory GCC's versions were chosen from, whose times change when a
// version's directory comes or goes, and each version's crtbeginS.o, found or not, so that one installed into a
// version's existing directory shows too.
//
// The import (runtime_import, runtime_import_static) reads every file once into the runtime's arena, which the atoms
// then point into, and fills a link of the runtime's own: the crt objects as units 0 to 4 (RuntimeObject), then each
// archive's members, in the archives' order (RuntimeArchive), as units after them (Runtime.member_unit on), each
// object's records a piece. libc.so.6 and the loader become ImportLibraries: their exports with their default
// versions, their versions and the names they refer to. The loader's version indices are moved past libc.so.6's, so
// that an index, which a link's symbols hold (LinkSymbols.version), names one library's version. Then it binds each
// name the runtime refers to (runtime_bind): to a definition in the runtime's own objects (an index of them by name
// hash, as the libraries' exports have; an absolute symbol is one), to the linker for a name it defines
// (_GLOBAL_OFFSET_TABLE_, and for a static executable the bounds of its sections and tables, runtime_linker_names), to
// an export of libc.so.6 or else of the loader, to nothing for a weak reference that none defines, or else to the
// program (main): RuntimeBinding, by symbol.
//
// What binding's closure over the members needs (bind.c), from the global names (RuntimeNames): the member whose
// definition each name has; each piece's strong references to names (Runtime.references, a range per piece), with the
// member each loads (Runtime.reference_members); and its COMDAT groups (RuntimeGroup, a range per piece, each with its
// atoms and its signature's number among the runtime's distinct signatures), whose first in link order the link keeps.
//
// main.c includes this file after import.c. Layout, in this order:
//   - API: RuntimeError, RuntimeObject, RuntimeProbe, RuntimeBinding, Runtime and the entry points' prototypes.
//   - Files: probing and reading.
//   - The search: GCC's installation, the crt objects and libc's script.
//   - Importing and binding.
//   - Bytes and pieces: the accessors of a loaded runtime's bytes, and its pieces' hashes (runtime_piece_hash).

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

// OS-dependent includes
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

// CPU architecture dependent includes
#include <immintrin.h>

// ---------------------------------------------------------------------------------------------------------------------
// API

typedef enum RuntimeError
{
    RUNTIME_ERROR_NONE,
    // A file the runtime needs isn't anywhere the search looks (Runtime.failed_file says which).
    RUNTIME_ERROR_MISSING,
    // A file didn't import (Runtime.failed_file names it, Runtime.import says why).
    RUNTIME_ERROR_IMPORT,
    // Opening, reading or stat-ing a file failed (Runtime.failed_file and system_error).
    RUNTIME_ERROR_FILE,
    // An arena couldn't hold what the runtime needs.
    RUNTIME_ERROR_MEMORY,
    // A library a link names with -l (RuntimeLibraries) is an archive, which the direct path doesn't link yet
    // (RuntimeLibraries.failed says which).
    RUNTIME_ERROR_ARCHIVE,
} RuntimeError;

// What the runtime is for: the files GCC's driver links around a program's objects (see the orientation comment).
typedef enum RuntimeKind
{
    // A dynamic PIE: Scrt1.o, crti.o, crtbeginS.o, crtendS.o and crtn.o; libc.so.6 and libc_nonshared.a.
    RUNTIME_KIND_DYNAMIC,
    // A static executable: crt1.o, crti.o, crtbeginT.o, crtend.o and crtn.o; libgcc.a, libgcc_eh.a, libatomic.a and
    // libc.a.
    RUNTIME_KIND_STATIC,
    // A static PIE: rcrt1.o, crti.o, crtbeginS.o, crtendS.o and crtn.o; the static executable's archives.
    RUNTIME_KIND_STATIC_PIE,
    // A shared library: crti.o, crtbeginS.o, crtendS.o and crtn.o, without a start object; a dynamic PIE's libraries.
    RUNTIME_KIND_SHARED,
    RUNTIME_KINDS,
} RuntimeKind;

// The crt objects, in the order GCC's driver links them, which is their units' order in the runtime's link: the
// first three before the program's objects, the last two after.
typedef enum RuntimeObject
{
    RUNTIME_OBJECT_START,
    RUNTIME_OBJECT_INIT_PROLOGUE,
    RUNTIME_OBJECT_BEGIN,
    RUNTIME_OBJECT_END,
    RUNTIME_OBJECT_INIT_EPILOGUE,
    RUNTIME_OBJECTS,
} RuntimeObject;

// The most archives a runtime has: a static executable's group has four.
#define BUSTER_RUNTIME_ARCHIVES 4

// One of the runtime's archives (libc_nonshared.a; libgcc.a, libgcc_eh.a, libatomic.a, libc.a): its path, its bytes as
// import_archive read them, and its members' place among the runtime's (Runtime.members, whose units and pieces follow
// the crt objects').
typedef struct RuntimeArchive RuntimeArchive;
struct RuntimeArchive
{
    String8 path;
    String8 bytes;
    ImportArchive archive;
    uint32_t first_member;
};

// A COMDAT group of one of the runtime's objects: its signature's number among the runtime's distinct signatures, and
// its atoms, `count` from `first` in Runtime.group_atoms.
typedef struct RuntimeGroup RuntimeGroup;
struct RuntimeGroup
{
    uint32_t signature;
    uint32_t first;
    uint32_t count;
};

// A path the search probed, for a regular file or a directory: found, with what identifies it, or absent.
typedef struct RuntimeProbe RuntimeProbe;
struct RuntimeProbe
{
    String8 path;
    bool directory;
    bool found;
    uint64_t device;
    uint64_t inode;
    uint64_t size;
    int64_t modified_seconds;
    int64_t modified_nanoseconds;
    int64_t changed_seconds;
    int64_t changed_nanoseconds;
};

// What a name the runtime's objects refer to without defining it binds to.
typedef enum RuntimeBindingKind
{
    // The symbol is a definition (or none of the runtime's symbols refers to it).
    RUNTIME_BINDING_NONE,
    // A definition in another of the runtime's objects: RuntimeBinding.symbol.
    RUNTIME_BINDING_RUNTIME,
    // An export of libc.so.6 or of the loader: RuntimeBinding.export, its number among both (runtime_export_library).
    RUNTIME_BINDING_SHARED,
    // A weak reference that nothing defines: 0.
    RUNTIME_BINDING_NOWHERE,
    // A name the linker defines (runtime_linker_names): _GLOBAL_OFFSET_TABLE_, which the crt objects' GOT references
    // name.
    RUNTIME_BINDING_LINKER,
    // A name the program defines (main).
    RUNTIME_BINDING_PROGRAM,
} RuntimeBindingKind;

typedef struct RuntimeBinding RuntimeBinding;
struct RuntimeBinding
{
    RuntimeBindingKind kind;
    SymbolId symbol;
    uint32_t export;
};

// The records one of the runtime's objects (a crt object, or an archive's member) added to its link: the counts of
// each table before it and after it, the ranges a link the runtime joins copies (bind.c).
typedef struct RuntimePiece RuntimePiece;
struct RuntimePiece
{
    ImportMark begin;
    ImportMark end;
};

// The names a program's are bound against (bind.c): each distinct name of a global or weak symbol of the runtime's
// objects, of libc.so.6's exports and of its references, and of those the linker defines, once, as columns, with what
// binding needs to know of it, and indexed by hash. A name's bytes are `length` bytes at its offset in `strings`, so
// that the columns hold no pointers and a snapshot's can be mapped as they are (snapshot.c).
typedef struct RuntimeNames RuntimeNames;
struct RuntimeNames
{
    const uint8_t* strings;
    uint64_t strings_length;
    uint32_t* offset;
    uint32_t* length;
    uint64_t* hash;
    // The first of the runtime's symbols that defines the name, in symbol order, or 0; and if a member defines it there,
    // the member plus 1, or 0.
    SymbolId* definition;
    uint32_t* member;
    // Its export plus 1, or 0: libc.so.6's exports are numbered first, then the loader's, which a name that libc.so.6
    // exports doesn't take (runtime_export_library).
    uint32_t* export;
    // Whether libc.so.6 refers to it, and whether the linker defines it (runtime_linker_names).
    bool* referenced;
    bool* linker;
    uint32_t count;
    ImportNameIndex index;
    // A blocked Bloom filter of the names' hashes, which a lookup of a name that isn't among them mostly reads alone:
    // 2^filter_bits words of 64 bits, each name setting two bits (runtime_name_filter_bits) of the word its hash's top
    // bits choose. Its 8 KB for libc's names stay in L1 while a program's columns stream past, where the index's
    // buckets and hashes don't.
    uint64_t* filter;
    uint32_t filter_bits;
};

#define BUSTER_RUNTIME_PATH_BYTES 4096

typedef struct Runtime Runtime;
struct Runtime
{
    RuntimeKind kind;
    // The runtime's link, its arenas, and the files' bytes, which its atoms point into.
    Link link;
    Arena tables;
    Arena names;
    Arena files;
    // GCC's installation, and the directory its version was chosen from.
    String8 gcc_directory;
    String8 gcc_versions;
    // The files found: the crt objects, and for a dynamic PIE libc.so's script and the shared files it names.
    String8 object_paths[RUNTIME_OBJECTS];
    String8 libc_script;
    String8 libc_path;
    String8 loader_path;
    // Every path probed, in the order the search probed them, and whether a probe found no room among them.
    RuntimeProbe* probes;
    uint32_t probe_count;
    uint32_t probe_capacity;
    bool probes_full;
    // The imported files: the crt objects (and their bytes, which their atoms point into), libc.so.6's bytes, the
    // archives, and every archive's members, one after another in the archives' order, with each one's ImportObject.
    String8 object_bytes[RUNTIME_OBJECTS];
    ImportObject objects[RUNTIME_OBJECTS];
    String8 libc_bytes;
    String8 loader_bytes;
    RuntimeArchive archives[BUSTER_RUNTIME_ARCHIVES];
    uint32_t archive_count;
    ImportMember* members;
    ImportObject* member_objects;
    uint32_t member_count;
    // The first member's unit (RUNTIME_OBJECTS): the members follow it one per unit.
    uint32_t member_unit;
    ImportLibrary libc;
    // The loader that libc.so's script names as needed, whose version indices follow libc.so.6's (see the orientation
    // comment); empty when the script names none, and for a static runtime.
    ImportLibrary loader;
    // Scrt1.o's (or crt1.o's) .note.ABI-tag, which an executable repeats.
    String8 abi_note;
    // By symbol of the runtime's link.
    RuntimeBinding* bindings;
    // Each object's records: the crt objects' in RuntimeObject order, then each member's.
    RuntimePiece* pieces;
    // By piece (RUNTIME_OBJECTS + member_count of them, and one more for the end): where its strong references to
    // global names start in `references` (each an entry of global_names), and its COMDAT groups in `groups`. By
    // reference, the member plus 1 that it loads unless the link defines its name (the name's member, 0 for a name the
    // linker defines). The groups' atoms are in `group_atoms`, and their signatures are `group_signatures` distinct
    // ones.
    uint32_t* reference_begin;
    uint32_t* references;
    uint32_t* reference_members;
    uint32_t* group_begin;
    RuntimeGroup* groups;
    AtomId* group_atoms;
    uint32_t group_signatures;
    // The names binding looks a program's up in, and by symbol of the runtime's link its name's entry among them plus 1
    // (0 for a local or unnamed symbol).
    RuntimeNames global_names;
    uint32_t* symbol_names;
    // The room a link that binds against the runtime needs for the runtime's names' bytes, each with its NUL, and for
    // its atoms' bytes with their alignments (bind_capacity), which the import sums once.
    uint64_t name_room;
    uint64_t atom_room;
    // For a runtime loaded from a snapshot (snapshot.c), the file, mapped, whose columns it uses where they are; 0 for one
    // runtime_import made. A loaded runtime has what binding and an executable read: none of the files' bytes, the
    // objects' and members' ImportObjects, the archives' indices, or libc.so.6's export names, sizes, references and
    // index, which only the import uses. Its arenas are empty, and `names` is a view of the mapped names run, which
    // bind.c copies from.
    uint8_t* snapshot;
    uint64_t snapshot_bytes;
    // A loaded runtime's link holds its names' and atoms' bytes as offsets into the snapshot's data plus 1 (0 for none),
    // and its members their names as offsets into the snapshot's strings plus 1, which these deltas make pointers
    // (runtime_name_bytes, runtime_atom_bytes and runtime_member_name), so that loading writes none of those columns.
    // Both are 0 for an imported runtime, whose columns hold the pointers.
    uintptr_t byte_delta;
    uintptr_t string_delta;
    // A loaded runtime's pieces' records are trusted a piece at a time, when binding first reads them
    // (runtime_piece_current): each piece's hash, 16 bytes, and where each piece's atoms' bytes start in the snapshot's
    // data, the pieces' count plus 1 of them, the last the end.
    const uint8_t* snapshot_data;
    const uint8_t* piece_hashes;
    const uint64_t* piece_data;
    // When it failed: the file that didn't import or couldn't be read, or the name that wasn't found, the importer's
    // result and errno.
    char failed_file[BUSTER_RUNTIME_PATH_BYTES];
    ImportResult import;
    int system_error;
};

// Finds a dynamic PIE's runtime's files and imports them, with arenas of its own; runtime_release frees them (or a
// snapshot's mapping, for a runtime loaded from one). On failure, the error says what went wrong and the runtime's
// fields where. runtime_import_static does the same for a static executable's runtime, runtime_import_static_pie for a
// static PIE's and runtime_import_shared for a shared library's.
static __attribute__((unused)) RuntimeError runtime_import(Runtime* runtime);
static __attribute__((unused)) RuntimeError runtime_import_static(Runtime* runtime);
static __attribute__((unused)) RuntimeError runtime_import_static_pie(Runtime* runtime);
static __attribute__((unused)) RuntimeError runtime_import_shared(Runtime* runtime);
// runtime_import, runtime_import_static, runtime_import_static_pie or runtime_import_shared, by `kind`.
static __attribute__((unused)) RuntimeError runtime_import_kind(Runtime* runtime, RuntimeKind kind);
// Finds the default interpreter through libc.so's script without importing CRT objects, archives or library symbols.
// The path and probe records belong to runtime->files until runtime_release. This is path discovery, not ELF validation.
static __attribute__((unused)) RuntimeError runtime_discover_interpreter(Runtime* runtime);
static __attribute__((unused)) void runtime_release(Runtime* runtime);
// Whether a runtime of `kind` is libc.a's, with archives and without a shared library: a static executable's or a static
// PIE's.
static __attribute__((unused)) bool runtime_kind_is_static(RuntimeKind kind);

// The shared libraries a link names with -l, which it binds after the program and before the C runtime's libraries,
// where GCC's driver puts them on the link line (bind.c). -l NAME is lib<NAME>.so, or else lib<NAME>.a, in the first of
// the -L directories, then the library directories (runtime_library_directories), that has one, as GNU ld searches; an
// archive found first is refused, since the direct path doesn't link one yet. A file that is a linker script (glibc's
// libm.so, in libc.so's subset: import_script) stands for the shared libraries it names, each an absolute path. Each
// library is imported as libc.so.6 is (import_library), with an index of the names it refers to, and its version
// indices are moved past the runtime's and the libraries' before it, so that an index names one library's version, as
// runtime_move_loader_versions moves the loader's. A link needs a library only for a strong reference that binds to it,
// as GCC's driver links them on Debian and Ubuntu (--as-needed).
#define BUSTER_RUNTIME_LIBRARIES 32

// A library's exports at a version other than their name's default (name@VERSION, a .gnu.version entry with the hidden
// bit), which import_library leaves out of its exports: their names and their versions' names.
typedef struct RuntimeHiddenExports RuntimeHiddenExports;
struct RuntimeHiddenExports
{
    const uint8_t** name;
    uint32_t* length;
    uint64_t* hash;
    String8* version;
    uint32_t count;
};

typedef struct RuntimeLibraries RuntimeLibraries;
struct RuntimeLibraries
{
    ImportLibrary libraries[BUSTER_RUNTIME_LIBRARIES];
    // By library: the names it refers to, indexed by their hashes (ImportReferences.hash).
    ImportNameIndex references[BUSTER_RUNTIME_LIBRARIES];
    // By library: the names its DT_NEEDED entries give, dependency_count[l] of them at dependencies[l].
    String8* dependencies[BUSTER_RUNTIME_LIBRARIES];
    uint32_t dependency_count[BUSTER_RUNTIME_LIBRARIES];
    String8 paths[BUSTER_RUNTIME_LIBRARIES];
    // By library: whether the -l search found it, rather than a script naming its path (runtime_libraries_name).
    bool searched[BUSTER_RUNTIME_LIBRARIES];
    // By library: each reference's required version and the file its .gnu.version_r entry requires it of, beside
    // libraries[l].references (both empty for a reference without one); and its exports at other versions than their
    // names' defaults (runtime_libraries_versions).
    String8* reference_versions[BUSTER_RUNTIME_LIBRARIES];
    String8* reference_files[BUSTER_RUNTIME_LIBRARIES];
    RuntimeHiddenExports hidden[BUSTER_RUNTIME_LIBRARIES];
    uint32_t count;
    // The files' bytes, which the libraries' names point into, and their columns and indices.
    Arena arena;
    // When it failed: the -l name or the file, the importer's result, and errno.
    char failed[BUSTER_RUNTIME_PATH_BYTES];
    ImportResult import;
    int system_error;
};

// Imports the libraries that `name_count` -l names find, in their order, through `directory_count` -L directories, for
// a link against `runtime`, into an arena of their own that runtime_libraries_release frees. On failure,
// RUNTIME_ERROR_MISSING for a name no directory has, RUNTIME_ERROR_ARCHIVE for one found as an archive first,
// RUNTIME_ERROR_IMPORT for a file that is neither a shared library nor a script the reader takes, or for a script that
// names another script, an archive or a path that isn't absolute, RUNTIME_ERROR_FILE or RUNTIME_ERROR_MEMORY, with
// `failed` saying where.
static __attribute__((unused)) RuntimeError runtime_libraries_import(RuntimeLibraries* libraries, const Runtime* runtime, const String8* names,
                                                                    uint32_t name_count, const String8* directories, uint32_t directory_count);
static __attribute__((unused)) void runtime_libraries_release(RuntimeLibraries* libraries);
// The first library, in link order, that exports `name`: its index plus 1, and the export's index in *export; 0 for none
// (also with no libraries).
static __attribute__((unused)) uint32_t runtime_libraries_export(const RuntimeLibraries* libraries, const uint8_t* name, uint32_t length, uint64_t hash,
                                                                uint32_t* export);
// Whether library `l` refers to `name` without defining it.
static __attribute__((unused)) bool runtime_libraries_refer(const RuntimeLibraries* libraries, uint32_t l, const uint8_t* name, uint32_t length,
                                                           uint64_t hash);
// The name library `l` takes in a DT_NEEDED entry, as GNU ld and lld name it: its soname, or for a library without
// one, its file's name where the -l search found it, and its path as a script gives it otherwise.
static __attribute__((unused)) String8 runtime_libraries_name(const RuntimeLibraries* libraries, uint32_t l);
// Whether library `l`'s own DT_NEEDED entries name library `m`, which the loader then loads for it.
static __attribute__((unused)) bool runtime_libraries_lists(const RuntimeLibraries* libraries, uint32_t l, uint32_t m);
// The first library, in link order, that exports `name` at `version`, as the loader matches a reference that requires
// that version: a default export at it or without a version, or an export at it that isn't its name's default
// (RuntimeHiddenExports). Its index plus 1, or 0.
static __attribute__((unused)) uint32_t runtime_libraries_export_at(const RuntimeLibraries* libraries, const uint8_t* name, uint32_t length, uint64_t hash,
                                                                   String8 version);
// Whether every path the search probed is as it found it: absent still, or the same file or directory, by st_dev, st_ino,
// st_size, st_mtim and st_ctim.
static __attribute__((unused)) bool runtime_probes_current(const Runtime* runtime);
// The library of an export of the runtime's (RuntimeNames.export less 1, or RuntimeBinding.export): libc.so.6 for one
// below its exports' count, the loader for the others; and the export's index among that library's.
static __attribute__((unused)) const ImportLibrary* runtime_export_library(const Runtime* runtime, uint32_t export, uint32_t* index);
// Whether a version index that a link's symbol holds (LinkSymbols.version) is one of the loader's, which the import
// numbers after libc.so.6's.
static __attribute__((unused)) bool runtime_loader_version(const Runtime* runtime, uint16_t version);
// The entry of the runtime's global names (RuntimeNames) with these bytes, plus 1, or 0.
static __attribute__((unused)) uint32_t runtime_find_global_name(const RuntimeNames* names, const uint8_t* bytes, uint32_t length, uint64_t hash);
// The bytes of an entry of the runtime's global names.
static __attribute__((unused)) const uint8_t* runtime_global_name(const RuntimeNames* names, uint32_t entry);
// The two bits a name's hash sets in its word of the global names' filter: the hash's low six bits and the six above.
static __attribute__((unused)) uint64_t runtime_name_filter_bits(uint64_t hash);
// The bytes of a name and of an atom of the runtime's link (0 for an atom without bytes), and a member's name: what the
// link's columns and the members point to, for a loaded runtime through its deltas.
static __attribute__((unused)) const uint8_t* runtime_name_bytes(const Runtime* runtime, NameId name);
static __attribute__((unused)) const uint8_t* runtime_atom_bytes(const Runtime* runtime, AtomId atom);
static __attribute__((unused)) String8 runtime_member_name(const Runtime* runtime, uint32_t member);
// Whether binding may read a piece of the runtime (a crt object, or RUNTIME_OBJECTS + m for member m): always for one
// runtime_import made; for one loaded from a snapshot, whether the piece's records and bytes hash as when the snapshot
// was written (runtime_piece_hash).
static __attribute__((unused)) bool runtime_piece_current(const Runtime* runtime, uint32_t piece);

// ---------------------------------------------------------------------------------------------------------------------
// Files

#define BUSTER_RUNTIME_MAX_PROBES 256
#define BUSTER_RUNTIME_TABLES_BYTES (64ull << 20)
#define BUSTER_RUNTIME_NAMES_BYTES (16ull << 20)
#define BUSTER_RUNTIME_FILES_BYTES (1ull << 30)

// A copy of `path` in the runtime's arena, with a NUL for the system calls.
static String8 runtime_copy_path(Runtime* runtime, const char* path)
{
    const uint64_t length = strlen(path);
    char* copy = arena_push(&runtime->files, length + 1, 1);
    String8 result = { 0 };
    if (copy != 0)
    {
        memcpy(copy, path, length + 1);
        result.pointer = copy;
        result.length = length;
    }
    return result;
}

// Reserves the runtime's arenas and its probes: false if they can't be had.
static bool runtime_begin(Runtime* runtime)
{
    *runtime = (Runtime){ 0 };
    runtime->tables = arena_reserve(BUSTER_RUNTIME_TABLES_BYTES);
    runtime->names = arena_reserve(BUSTER_RUNTIME_NAMES_BYTES);
    runtime->files = arena_reserve(BUSTER_RUNTIME_FILES_BYTES);
    runtime->probe_capacity = BUSTER_RUNTIME_MAX_PROBES;
    runtime->probes = BUSTER_ARENA_PUSH_ARRAY(&runtime->files, RuntimeProbe, runtime->probe_capacity);
    return runtime->tables.base != 0 && runtime->names.base != 0 && runtime->probes != 0;
}

static void runtime_identify(RuntimeProbe* probe, const struct stat* status)
{
    probe->found = true;
    probe->device = (uint64_t)status->st_dev;
    probe->inode = (uint64_t)status->st_ino;
    probe->size = (uint64_t)status->st_size;
    probe->modified_seconds = (int64_t)status->st_mtim.tv_sec;
    probe->modified_nanoseconds = (int64_t)status->st_mtim.tv_nsec;
    probe->changed_seconds = (int64_t)status->st_ctim.tv_sec;
    probe->changed_nanoseconds = (int64_t)status->st_ctim.tv_nsec;
}

// Probes a path with stat and records it: whether it is a regular file (or with `directory`, a directory). A probe that
// finds no room is recorded as Runtime.probes_full, which fails the import, since the snapshot must check every path.
static bool runtime_probe(Runtime* runtime, const char* path, bool directory)
{
    struct stat status;
    const bool exists = stat(path, &status) == 0;
    const bool found = exists && (directory ? S_ISDIR(status.st_mode) : S_ISREG(status.st_mode));
    runtime->probes_full = runtime->probes_full || runtime->probe_count == runtime->probe_capacity;
    if (runtime->probe_count < runtime->probe_capacity)
    {
        RuntimeProbe* probe = &runtime->probes[runtime->probe_count];
        *probe = (RuntimeProbe){
            .path = runtime_copy_path(runtime, path),
            .directory = directory,
        };
        if (found)
        {
            runtime_identify(probe, &status);
        }
        runtime->probe_count += 1;
    }
    return found;
}

static bool runtime_probes_current(const Runtime* runtime)
{
    bool result = true;
    for (uint32_t p = 0; result && p < runtime->probe_count; p += 1)
    {
        const RuntimeProbe* recorded = &runtime->probes[p];
        struct stat status;
        RuntimeProbe now = {
            .directory = recorded->directory,
        };
        const bool exists = recorded->path.pointer != 0 && stat(recorded->path.pointer, &status) == 0;
        if (exists && (recorded->directory ? S_ISDIR(status.st_mode) : S_ISREG(status.st_mode)))
        {
            runtime_identify(&now, &status);
        }
        result = recorded->path.pointer != 0 && now.found == recorded->found && now.device == recorded->device && now.inode == recorded->inode &&
                 now.size == recorded->size && now.modified_seconds == recorded->modified_seconds && now.modified_nanoseconds == recorded->modified_nanoseconds &&
                 now.changed_seconds == recorded->changed_seconds && now.changed_nanoseconds == recorded->changed_nanoseconds;
    }
    return result;
}

// Reads a whole file into the runtime's arena: its bytes, or an empty slice with the error recorded.
static String8 runtime_read(Runtime* runtime, String8 path, RuntimeError* error)
{
    String8 result = { 0 };
    const int file = open(path.pointer, O_RDONLY | O_CLOEXEC);
    struct stat status;
    const bool opened = file >= 0 && fstat(file, &status) == 0 && S_ISREG(status.st_mode);
    char* bytes = opened ? arena_push(&runtime->files, (uint64_t)status.st_size + 1, 64) : 0;
    uint64_t done = 0;
    int read_error = opened ? 0 : errno;
    while (bytes != 0 && read_error == 0 && done < (uint64_t)status.st_size)
    {
        const ssize_t count = read(file, bytes + done, (uint64_t)status.st_size - done);
        read_error = count > 0 || (count < 0 && errno == EINTR) ? 0 : count == 0 ? EIO : errno;
        done += count > 0 ? (uint64_t)count : 0;
    }
    if (file >= 0)
    {
        close(file);
    }
    if (opened && bytes == 0)
    {
        *error = RUNTIME_ERROR_MEMORY;
    }
    else if (!opened || read_error != 0)
    {
        *error = RUNTIME_ERROR_FILE;
        runtime->system_error = read_error != 0 ? read_error : ENOENT;
    }
    else
    {
        bytes[done] = 0;
        result.pointer = bytes;
        result.length = done;
    }
    if (*error != RUNTIME_ERROR_NONE)
    {
        snprintf(runtime->failed_file, sizeof(runtime->failed_file), "%.*s", (int)path.length, path.pointer);
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The search

static const char* const runtime_gcc_bases[] = {
    "/usr/lib/gcc",
    "/usr/lib64/gcc",
};

static const char* const runtime_triples[] = {
    "x86_64-pc-linux-gnu",
    "x86_64-linux-gnu",
    "x86_64-redhat-linux",
    "x86_64-suse-linux",
    "x86_64-unknown-linux-gnu",
};

// glibc's directories, in the order GCC's driver searches them on these systems.
static const char* const runtime_library_directories[] = {
    "/usr/lib/x86_64-linux-gnu",
    "/lib/x86_64-linux-gnu",
    "/usr/lib64",
    "/lib64",
    "/usr/lib",
    "/lib",
};

// Whether version `a` (dotted decimal numbers, "16" or "12.2.0") is greater than `b`: compared number by number, a
// missing number being smaller. A name that isn't a version is smaller than any version.
static bool runtime_version_greater(const char* a, const char* b)
{
    bool decided = false;
    bool result = false;
    const char* p = a;
    const char* q = b;
    while (!decided)
    {
        const bool p_digit = *p >= '0' && *p <= '9';
        const bool q_digit = *q >= '0' && *q <= '9';
        uint64_t x = 0;
        uint64_t y = 0;
        while (*p >= '0' && *p <= '9' && x < UINT32_MAX)
        {
            x = x * 10 + (uint64_t)(*p - '0');
            p += 1;
        }
        while (*q >= '0' && *q <= '9' && y < UINT32_MAX)
        {
            y = y * 10 + (uint64_t)(*q - '0');
            q += 1;
        }
        decided = !p_digit || !q_digit || x != y;
        result = decided ? (p_digit && (!q_digit || x > y)) : result;
        p += *p == '.' ? 1 : 0;
        q += *q == '.' ? 1 : 0;
    }
    return result;
}

// The longest version directory's name GCC's search considers.
#define BUSTER_RUNTIME_VERSION_BYTES 256

// Finds GCC's installation under `bases`: for the first base and triple whose directory exists, the highest version
// that has crtbeginS.o. Each version's crtbeginS.o is probed, found or not, so that the probes show one installed into a
// version's existing directory, and in the order of the versions' names, highest first, so that they don't depend on
// the order readdir gives them in.
static bool runtime_find_gcc(Runtime* runtime, const char* const* bases, uint32_t base_count)
{
    char path[BUSTER_RUNTIME_PATH_BYTES];
    char best[BUSTER_RUNTIME_PATH_BYTES] = { 0 };
    char best_triple[BUSTER_RUNTIME_PATH_BYTES] = { 0 };
    bool found = false;
    for (uint32_t b = 0; !found && b < base_count; b += 1)
    {
        for (uint64_t t = 0; !found && t < BUSTER_ARRAY_LENGTH(runtime_triples); t += 1)
        {
            snprintf(path, sizeof(path), "%s/%s", bases[b], runtime_triples[t]);
            DIR* versions = runtime_probe(runtime, path, true) ? opendir(path) : 0;
            // The names of the versions' directories, copied to the files arena one after another.
            const uint64_t names_begin = runtime->files.length;
            uint32_t name_count = 0;
            for (struct dirent* entry = versions != 0 ? readdir(versions) : 0; entry != 0; entry = readdir(versions))
            {
                const uint64_t length = strlen(entry->d_name);
                const bool version = entry->d_name[0] >= '0' && entry->d_name[0] <= '9' && length < BUSTER_RUNTIME_VERSION_BYTES;
                char* copy = version ? arena_push(&runtime->files, BUSTER_RUNTIME_VERSION_BYTES, 1) : 0;
                if (copy != 0)
                {
                    memcpy(copy, entry->d_name, length + 1);
                    name_count += 1;
                }
                runtime->probes_full = runtime->probes_full || (version && copy == 0);
            }
            if (versions != 0)
            {
                closedir(versions);
            }
            // Highest first: an insertion sort of the names in place (there are a few), by version, then by name.
            char* names = (char*)runtime->files.base + names_begin;
            for (uint32_t i = 1; i < name_count; i += 1)
            {
                char moving[BUSTER_RUNTIME_VERSION_BYTES];
                memcpy(moving, names + (uint64_t)i * BUSTER_RUNTIME_VERSION_BYTES, BUSTER_RUNTIME_VERSION_BYTES);
                uint32_t place = i;
                while (place > 0 && (runtime_version_greater(moving, names + (uint64_t)(place - 1) * BUSTER_RUNTIME_VERSION_BYTES) ||
                                     (!runtime_version_greater(names + (uint64_t)(place - 1) * BUSTER_RUNTIME_VERSION_BYTES, moving) &&
                                      strcmp(moving, names + (uint64_t)(place - 1) * BUSTER_RUNTIME_VERSION_BYTES) < 0)))
                {
                    memcpy(names + (uint64_t)place * BUSTER_RUNTIME_VERSION_BYTES, names + (uint64_t)(place - 1) * BUSTER_RUNTIME_VERSION_BYTES, BUSTER_RUNTIME_VERSION_BYTES);
                    place -= 1;
                }
                memcpy(names + (uint64_t)place * BUSTER_RUNTIME_VERSION_BYTES, moving, BUSTER_RUNTIME_VERSION_BYTES);
            }
            for (uint32_t i = 0; i < name_count; i += 1)
            {
                char crt[BUSTER_RUNTIME_PATH_BYTES];
                const char* name = names + (uint64_t)i * BUSTER_RUNTIME_VERSION_BYTES;
                const int length = snprintf(crt, sizeof(crt), "%s/%s/crtbeginS.o", path, name);
                const bool fits = length > 0 && (uint64_t)length < sizeof(crt);
                const bool has_crt = fits && runtime_probe(runtime, crt, false);
                if (has_crt && !found)
                {
                    snprintf(best, sizeof(best), "%s/%s", path, name);
                    snprintf(best_triple, sizeof(best_triple), "%s", path);
                    found = true;
                }
            }
        }
    }
    if (found)
    {
        runtime->gcc_directory = runtime_copy_path(runtime, best);
        runtime->gcc_versions = runtime_copy_path(runtime, best_triple);
    }
    return found && runtime->gcc_directory.length != 0;
}

// The first library directory (or with `gcc`, GCC's installation) that has `name`, probing each in order.
static String8 runtime_find_file(Runtime* runtime, const char* name, bool gcc)
{
    char path[BUSTER_RUNTIME_PATH_BYTES];
    String8 result = { 0 };
    if (gcc)
    {
        snprintf(path, sizeof(path), "%.*s/%s", (int)runtime->gcc_directory.length, runtime->gcc_directory.pointer, name);
        result = runtime_probe(runtime, path, false) ? runtime_copy_path(runtime, path) : result;
    }
    for (uint64_t d = 0; !gcc && result.length == 0 && d < BUSTER_ARRAY_LENGTH(runtime_library_directories); d += 1)
    {
        snprintf(path, sizeof(path), "%s/%s", runtime_library_directories[d], name);
        result = runtime_probe(runtime, path, false) ? runtime_copy_path(runtime, path) : result;
    }
    if (result.length == 0)
    {
        snprintf(runtime->failed_file, sizeof(runtime->failed_file), "%s", name);
    }
    return result;
}

// The first of GCC's installation and the library directories that has `name`, as -L finds a static executable's
// archive there, probing each in order; an empty path if none has it.
static String8 runtime_find_archive(Runtime* runtime, const char* name)
{
    char path[BUSTER_RUNTIME_PATH_BYTES];
    snprintf(path, sizeof(path), "%.*s/%s", (int)runtime->gcc_directory.length, runtime->gcc_directory.pointer, name);
    String8 result = runtime_probe(runtime, path, false) ? runtime_copy_path(runtime, path) : (String8){ 0 };
    for (uint64_t d = 0; result.length == 0 && d < BUSTER_ARRAY_LENGTH(runtime_library_directories); d += 1)
    {
        snprintf(path, sizeof(path), "%s/%s", runtime_library_directories[d], name);
        result = runtime_probe(runtime, path, false) ? runtime_copy_path(runtime, path) : result;
    }
    return result;
}

// A static executable's archives, in the order of the driver's group: GCC's libgcc.a and libgcc_eh.a, then
// libatomic_asneeded.a if GCC has it, and libc.a.
static RuntimeError runtime_search_archives(Runtime* runtime)
{
    static const char* const names[] = {
        "libgcc.a",
        "libgcc_eh.a",
        "libatomic_asneeded.a",
        "libc.a",
    };
    _Static_assert(BUSTER_ARRAY_LENGTH(names) <= BUSTER_RUNTIME_ARCHIVES, "the group's archives fit");
    RuntimeError result = RUNTIME_ERROR_NONE;
    for (uint32_t i = 0; result == RUNTIME_ERROR_NONE && i < BUSTER_ARRAY_LENGTH(names); i += 1)
    {
        const bool gcc = i < 2;
        const bool optional = i == 2;
        const String8 path = gcc ? runtime_find_file(runtime, names[i], true) : runtime_find_archive(runtime, names[i]);
        if (path.length != 0)
        {
            runtime->archives[runtime->archive_count].path = path;
            runtime->archive_count += 1;
        }
        else if (!optional)
        {
            result = RUNTIME_ERROR_MISSING;
            snprintf(runtime->failed_file, sizeof(runtime->failed_file), "%s", names[i]);
        }
    }
    return result;
}

static RuntimeError runtime_search(Runtime* runtime)
{
    static const char* const objects[RUNTIME_KINDS][RUNTIME_OBJECTS] = {
        [RUNTIME_KIND_DYNAMIC] = {
            [RUNTIME_OBJECT_START] = "Scrt1.o",
            [RUNTIME_OBJECT_INIT_PROLOGUE] = "crti.o",
            [RUNTIME_OBJECT_BEGIN] = "crtbeginS.o",
            [RUNTIME_OBJECT_END] = "crtendS.o",
            [RUNTIME_OBJECT_INIT_EPILOGUE] = "crtn.o",
        },
        [RUNTIME_KIND_STATIC] = {
            [RUNTIME_OBJECT_START] = "crt1.o",
            [RUNTIME_OBJECT_INIT_PROLOGUE] = "crti.o",
            [RUNTIME_OBJECT_BEGIN] = "crtbeginT.o",
            [RUNTIME_OBJECT_END] = "crtend.o",
            [RUNTIME_OBJECT_INIT_EPILOGUE] = "crtn.o",
        },
        // No start object: its path stays empty.
        [RUNTIME_KIND_SHARED] = {
            [RUNTIME_OBJECT_INIT_PROLOGUE] = "crti.o",
            [RUNTIME_OBJECT_BEGIN] = "crtbeginS.o",
            [RUNTIME_OBJECT_END] = "crtendS.o",
            [RUNTIME_OBJECT_INIT_EPILOGUE] = "crtn.o",
        },
        [RUNTIME_KIND_STATIC_PIE] = {
            [RUNTIME_OBJECT_START] = "rcrt1.o",
            [RUNTIME_OBJECT_INIT_PROLOGUE] = "crti.o",
            [RUNTIME_OBJECT_BEGIN] = "crtbeginS.o",
            [RUNTIME_OBJECT_END] = "crtendS.o",
            [RUNTIME_OBJECT_INIT_EPILOGUE] = "crtn.o",
        },
    };
    RuntimeError result = runtime_find_gcc(runtime, runtime_gcc_bases, BUSTER_ARRAY_LENGTH(runtime_gcc_bases)) ? RUNTIME_ERROR_NONE : RUNTIME_ERROR_MISSING;
    if (result != RUNTIME_ERROR_NONE)
    {
        snprintf(runtime->failed_file, sizeof(runtime->failed_file), "crtbeginS.o in a GCC installation");
    }
    for (uint32_t i = 0; result == RUNTIME_ERROR_NONE && i < RUNTIME_OBJECTS; i += 1)
    {
        const bool gcc = i == RUNTIME_OBJECT_BEGIN || i == RUNTIME_OBJECT_END;
        const char* name = objects[runtime->kind][i];
        runtime->object_paths[i] = name != 0 ? runtime_find_file(runtime, name, gcc) : (String8){ 0 };
        result = name == 0 || runtime->object_paths[i].length != 0 ? result : RUNTIME_ERROR_MISSING;
    }
    if (result == RUNTIME_ERROR_NONE && !runtime_kind_is_static(runtime->kind))
    {
        runtime->libc_script = runtime_find_file(runtime, "libc.so", false);
        result = runtime->libc_script.length != 0 ? result : RUNTIME_ERROR_MISSING;
    }
    else if (result == RUNTIME_ERROR_NONE)
    {
        result = runtime_search_archives(runtime);
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Importing and binding

static RuntimeError runtime_fail_import(Runtime* runtime, String8 path, ImportResult import)
{
    runtime->import = import;
    snprintf(runtime->failed_file, sizeof(runtime->failed_file), "%.*s", (int)path.length, path.pointer);
    return RUNTIME_ERROR_IMPORT;
}

// Reads libc.so's script: libc.so.6, libc_nonshared.a, and the loader as needed. Each must be an absolute path.
static RuntimeError runtime_read_script(Runtime* runtime)
{
    RuntimeError result = RUNTIME_ERROR_NONE;
    const String8 bytes = runtime_read(runtime, runtime->libc_script, &result);
    ImportScript script = { 0 };
    const ImportResult parsed = result == RUNTIME_ERROR_NONE ? import_script(&script, bytes, &runtime->files) : (ImportResult){ 0 };
    if (result == RUNTIME_ERROR_NONE && parsed.error != IMPORT_ERROR_NONE)
    {
        result = runtime_fail_import(runtime, runtime->libc_script, parsed);
    }
    for (uint32_t i = 0; result == RUNTIME_ERROR_NONE && i < script.input_count; i += 1)
    {
        const ImportScriptInput* input = &script.inputs[i];
        char path[BUSTER_RUNTIME_PATH_BYTES];
        snprintf(path, sizeof(path), "%.*s", (int)input->name.length, input->name.pointer);
        const uint64_t length = input->name.length;
        const bool absolute = !input->library && length > 0 && input->name.pointer[0] == '/';
        const bool archive = absolute && length > 2 && memcmp(input->name.pointer + length - 2, ".a", 2) == 0;
        const bool found = absolute && runtime_probe(runtime, path, false);
        if (!found)
        {
            result = RUNTIME_ERROR_MISSING;
            snprintf(runtime->failed_file, sizeof(runtime->failed_file), "%s", path);
        }
        else if (archive)
        {
            runtime->archives[0].path = runtime_copy_path(runtime, path);
            runtime->archive_count = 1;
        }
        else if (input->as_needed)
        {
            runtime->loader_path = runtime_copy_path(runtime, path);
        }
        else
        {
            runtime->libc_path = runtime_copy_path(runtime, path);
        }
    }
    if (result == RUNTIME_ERROR_NONE && (runtime->libc_path.length == 0 || runtime->archive_count == 0 || runtime->archives[0].path.length == 0))
    {
        result = RUNTIME_ERROR_MISSING;
        snprintf(runtime->failed_file, sizeof(runtime->failed_file), "libc.so.6 or libc_nonshared.a in %.*s", (int)runtime->libc_script.length,
                 runtime->libc_script.pointer);
    }
    return result;
}

// The script reader also serves callers that need its interpreter path alone. Keep its existing absolute-path and
// file-presence checks, and require an as-needed loader entry without reading or importing that library's contents.
static RuntimeError runtime_read_interpreter(Runtime* runtime)
{
    RuntimeError result = runtime_read_script(runtime);
    if (result == RUNTIME_ERROR_NONE && runtime->loader_path.length == 0)
    {
        result = RUNTIME_ERROR_MISSING;
        snprintf(runtime->failed_file, sizeof(runtime->failed_file), "dynamic loader in %.*s", (int)runtime->libc_script.length, runtime->libc_script.pointer);
    }
    result = result == RUNTIME_ERROR_NONE && runtime->probes_full ? RUNTIME_ERROR_MEMORY : result;
    if (result != RUNTIME_ERROR_NONE)
    {
        runtime->loader_path = (String8){ 0 };
    }
    return result;
}

static RuntimeError runtime_discover_interpreter(Runtime* runtime)
{
    *runtime = (Runtime){ 0 };
    runtime->files = arena_reserve(BUSTER_RUNTIME_FILES_BYTES);
    runtime->probe_capacity = BUSTER_RUNTIME_MAX_PROBES;
    runtime->probes = BUSTER_ARENA_PUSH_ARRAY(&runtime->files, RuntimeProbe, runtime->probe_capacity);
    RuntimeError result = runtime->probes != 0 ? RUNTIME_ERROR_NONE : RUNTIME_ERROR_MEMORY;
    if (result == RUNTIME_ERROR_NONE)
    {
        runtime->libc_script = runtime_find_file(runtime, "libc.so", false);
        result = runtime->libc_script.length != 0 ? runtime_read_interpreter(runtime) : RUNTIME_ERROR_MISSING;
    }
    return result;
}

// Adds an object's capacity to the runtime link's.
static RuntimeError runtime_count(Runtime* runtime, String8 path, String8 bytes, LinkCapacity* total)
{
    LinkCapacity capacity;
    const ImportResult counted = import_object_capacity(bytes, &capacity);
    RuntimeError result = RUNTIME_ERROR_NONE;
    if (counted.error != IMPORT_ERROR_NONE)
    {
        result = runtime_fail_import(runtime, path, counted);
    }
    else
    {
        total->names += capacity.names;
        total->atoms += capacity.atoms;
        total->symbols += capacity.symbols;
        total->fixups += capacity.fixups;
        total->wide_addends += capacity.wide_addends;
        total->priorities += capacity.priorities;
    }
    return result;
}

// The section's contents in an object, by name: Scrt1.o's .note.ABI-tag.
static String8 runtime_section(String8 bytes, const char* name)
{
    ImportElf elf;
    String8 result = { 0 };
    const bool valid = import_elf(&elf, bytes, ET_REL).error == IMPORT_ERROR_NONE;
    for (uint32_t index = 1; valid && index < elf.section_count && result.length == 0; index += 1)
    {
        const Elf64_Shdr header = import_section_header(&elf, index);
        String8 section_name = { 0 };
        if (import_section_name(&elf, &header, &section_name) && import_equal(section_name, name))
        {
            result = import_section_bytes(&elf, &header);
        }
    }
    return result;
}

// The names the direct path's linker defines (BUSTER_LINK_SOURCE_LINKER) when something refers to one, as lld does when
// no object defines it, each with the kinds whose linker defines it (a bit per RuntimeKind), as GNU ld's and lld's
// define them there:
//   - every kind: the bounds of the init and fini arrays, which step 4's executables define, and the GOT's
//     _GLOBAL_OFFSET_TABLE_, which GNU as names in an object with a GOT reference;
//   - the PIEs and a shared library: .dynamic's address (_DYNAMIC, through which libc.a's member that relocates a static
//     PIE finds it);
//   - a static executable and a static PIE: the ELF header's address (__ehdr_start, through which libc.a's startup finds
//     the program headers) and the image's start; the preinit array's bounds; and the ends of the text, the data and
//     the image;
//   - a static executable alone: its IRELATIVE relocations' bounds (__rela_iplt_start and __rela_iplt_end, which
//     libc.a's startup applies for its IFUNCs). A static PIE's are in .rela.dyn, which its startup applies with the
//     others, and libc.a's weak references to the bounds bind to nothing, as GNU ld and lld leave them.
#define BUSTER_RUNTIME_KIND_BIT(kind) (1u << (kind))
#define BUSTER_RUNTIME_EVERY_KIND ((1u << RUNTIME_KINDS) - 1)
#define BUSTER_RUNTIME_DYNAMIC_SECTION_KINDS (BUSTER_RUNTIME_KIND_BIT(RUNTIME_KIND_DYNAMIC) | BUSTER_RUNTIME_KIND_BIT(RUNTIME_KIND_STATIC_PIE) | BUSTER_RUNTIME_KIND_BIT(RUNTIME_KIND_SHARED))
#define BUSTER_RUNTIME_STATIC_KINDS (BUSTER_RUNTIME_KIND_BIT(RUNTIME_KIND_STATIC) | BUSTER_RUNTIME_KIND_BIT(RUNTIME_KIND_STATIC_PIE))

typedef struct RuntimeLinkerName RuntimeLinkerName;
struct RuntimeLinkerName
{
    const char* name;
    uint8_t kinds;
};

static const RuntimeLinkerName runtime_linker_names[] = {
    { "_GLOBAL_OFFSET_TABLE_", BUSTER_RUNTIME_EVERY_KIND },
    { "__init_array_start", BUSTER_RUNTIME_EVERY_KIND },
    { "__init_array_end", BUSTER_RUNTIME_EVERY_KIND },
    { "__fini_array_start", BUSTER_RUNTIME_EVERY_KIND },
    { "__fini_array_end", BUSTER_RUNTIME_EVERY_KIND },
    { "_DYNAMIC", BUSTER_RUNTIME_DYNAMIC_SECTION_KINDS },
    { "__ehdr_start", BUSTER_RUNTIME_STATIC_KINDS },
    { "__executable_start", BUSTER_RUNTIME_STATIC_KINDS },
    { "__preinit_array_start", BUSTER_RUNTIME_STATIC_KINDS },
    { "__preinit_array_end", BUSTER_RUNTIME_STATIC_KINDS },
    { "__rela_iplt_start", BUSTER_RUNTIME_KIND_BIT(RUNTIME_KIND_STATIC) },
    { "__rela_iplt_end", BUSTER_RUNTIME_KIND_BIT(RUNTIME_KIND_STATIC) },
    { "_etext", BUSTER_RUNTIME_STATIC_KINDS },
    { "etext", BUSTER_RUNTIME_STATIC_KINDS },
    { "__etext", BUSTER_RUNTIME_STATIC_KINDS },
    { "_edata", BUSTER_RUNTIME_STATIC_KINDS },
    { "edata", BUSTER_RUNTIME_STATIC_KINDS },
    { "__bss_start", BUSTER_RUNTIME_STATIC_KINDS },
    { "_end", BUSTER_RUNTIME_STATIC_KINDS },
    { "end", BUSTER_RUNTIME_STATIC_KINDS },
};

// Whether the name is one of the names the linker of a runtime of `kind` defines.
static bool runtime_linker_name(RuntimeKind kind, const uint8_t* bytes, uint32_t length)
{
    bool result = false;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(runtime_linker_names); i += 1)
    {
        const char* name = runtime_linker_names[i].name;
        const bool defined = (runtime_linker_names[i].kinds & BUSTER_RUNTIME_KIND_BIT(kind)) != 0;
        result = result || (defined && strlen(name) == length && memcmp(name, bytes, length) == 0);
    }
    return result;
}

// Whether a runtime of `kind` is libc.a's, with archives and without a shared library: a static executable's or a static
// PIE's.
static bool runtime_kind_is_static(RuntimeKind kind)
{
    return (BUSTER_RUNTIME_STATIC_KINDS & BUSTER_RUNTIME_KIND_BIT(kind)) != 0;
}

// The runtime's first symbol that defines the name (a global or weak definition in an atom), by the definitions' index:
// the name's bucket in symbol order, each hash compared, then the bytes of an equal one. 0 for none.
static SymbolId runtime_definition(const Link* link, const ImportNameIndex* index, const SymbolId* definitions, NameId name)
{
    const LinkNames* names = &link->names;
    const uint64_t hash = names->hash[name];
    SymbolId result = 0;
    uint32_t begin = 0;
    uint32_t end = 0;
    import_index_bucket(index, hash, &begin, &end);
    for (uint32_t place = begin; result == 0 && place < end; place += 1)
    {
        const SymbolId symbol = definitions[index->order[place]];
        const NameId other = link->symbols.name[symbol];
        const bool same = index->hash[place] == hash && names->length[other] == names->length[name] && import_same_bytes(names->bytes[other], names->bytes[name], names->length[name]);
        result = same ? symbol : 0;
    }
    return result;
}

// Binds each undefined symbol of the runtime's link (see the orientation comment). The definitions are found eight
// symbols at a time (a compress of the ids whose binding isn't local, with an atom or absolute, and with a name) and
// indexed by their names' hashes (import_index_build), rather than searched for each reference.
static RuntimeError runtime_bind(Runtime* runtime)
{
    Link* link = &runtime->link;
    const LinkSymbols* symbols = &link->symbols;
    const uint32_t count = symbols->count;
    RuntimeError result = RUNTIME_ERROR_NONE;
    runtime->bindings = BUSTER_ARENA_PUSH_ARRAY(&runtime->tables, RuntimeBinding, count);
    Scratch scratch = scratch_begin(&runtime->tables);
    SymbolId* definitions = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, SymbolId, count);
    uint64_t* hashes = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint64_t, count);
    ImportNameIndex index = { 0 };
    if (runtime->bindings == 0 || definitions == 0 || hashes == 0)
    {
        result = RUNTIME_ERROR_MEMORY;
    }
    const __m512i lane_index = _mm512_set_epi64(7, 6, 5, 4, 3, 2, 1, 0);
    uint32_t defined = 0;
    for (uint32_t first = 1; result == RUNTIME_ERROR_NONE && first < count; first += BUSTER_IMPORT_BATCH)
    {
        const uint32_t n = count - first < BUSTER_IMPORT_BATCH ? count - first : BUSTER_IMPORT_BATCH;
        const __mmask8 loaded = (__mmask8)import_lane_mask(n);
        const __m512i flags = _mm512_cvtepu8_epi64(_mm_maskz_loadu_epi8(loaded, symbols->flags + first));
        const __m512i atoms = _mm512_cvtepu32_epi64(_mm256_maskz_loadu_epi32(loaded, symbols->atom + first));
        const __m512i names = _mm512_cvtepu32_epi64(_mm256_maskz_loadu_epi32(loaded, symbols->name + first));
        const __m512i sources = _mm512_cvtepu8_epi64(_mm_maskz_loadu_epi8(loaded, symbols->source + first));
        const uint32_t placed = _mm512_test_epi64_mask(atoms, atoms) | import_lanes_equal(sources, BUSTER_LINK_SOURCE_ABSOLUTE);
        const uint32_t kept = _mm512_test_epi64_mask(flags, _mm512_set1_epi64(BUSTER_LINK_SYMBOL_BINDING)) & placed & _mm512_test_epi64_mask(names, names) & loaded;
        import_store32(definitions + defined, _mm512_maskz_compress_epi64((__mmask8)kept, _mm512_add_epi64(lane_index, _mm512_set1_epi64(first))),
                       (uint32_t)__builtin_popcount(kept));
        defined += (uint32_t)__builtin_popcount(kept);
    }
    for (uint32_t i = 0; result == RUNTIME_ERROR_NONE && i < defined; i += 1)
    {
        hashes[i] = link->names.hash[symbols->name[definitions[i]]];
    }
    if (result == RUNTIME_ERROR_NONE && !import_index_build(&index, hashes, defined, scratch.arena))
    {
        result = RUNTIME_ERROR_MEMORY;
    }
    for (SymbolId symbol = 1; result == RUNTIME_ERROR_NONE && symbol < count; symbol += 1)
    {
        const NameId name = symbols->name[symbol];
        RuntimeBinding binding = { 0 };
        if (symbols->atom[symbol] == 0 && symbols->source[symbol] != BUSTER_LINK_SOURCE_ABSOLUTE && name != 0)
        {
            const SymbolId definition = runtime_definition(link, &index, definitions, name);
            const bool linker = definition == 0 && runtime_linker_name(runtime->kind, link->names.bytes[name], link->names.length[name]);
            // A static executable's or a static PIE's runtime has no shared library.
            const bool shared = definition == 0 && !linker && !runtime_kind_is_static(runtime->kind);
            // libc.so.6's export, or else the loader's, numbered after libc.so.6's.
            const uint32_t libc = shared ? import_library_find(&runtime->libc, link->names.bytes[name], link->names.length[name], link->names.hash[name]) : 0;
            const uint32_t loader = shared && libc == 0 ? import_library_find(&runtime->loader, link->names.bytes[name], link->names.length[name], link->names.hash[name]) : 0;
            const uint32_t export = libc != 0 ? libc : loader != 0 ? runtime->libc.exports.count + loader : 0;
            const bool weak = (symbols->flags[symbol] & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_WEAK;
            const RuntimeBindingKind otherwise = export != 0 ? RUNTIME_BINDING_SHARED : weak ? RUNTIME_BINDING_NOWHERE : RUNTIME_BINDING_PROGRAM;
            binding.kind = definition != 0 ? RUNTIME_BINDING_RUNTIME : linker ? RUNTIME_BINDING_LINKER : otherwise;
            binding.symbol = definition;
            binding.export = export != 0 ? export - 1 : 0;
        }
        runtime->bindings[symbol] = binding;
    }
    scratch_end(scratch);
    return result;
}

// Builds the runtime's global names (RuntimeNames). The candidates are the names of its global and weak symbols (a
// compress of their ids, eight at a time, as runtime_bind's), of libc.so.6's exports and of its references, and the
// names the linker defines (runtime_linker_names), in that order, indexed by hash. A candidate that repeats an earlier one of its bucket, by hash and bytes, is folded into it;
// the first of each name is numbered by an exclusive prefix sum of the first ones, so that the entries keep the
// candidates' order, and the entries are indexed again.
static RuntimeError runtime_global_names(Runtime* runtime)
{
    const Link* link = &runtime->link;
    const LinkSymbols* symbols = &link->symbols;
    const ImportExports* exports = &runtime->libc.exports;
    const ImportReferences* references = &runtime->libc.references;
    const ImportExports* loader = &runtime->loader.exports;
    RuntimeNames* names = &runtime->global_names;
    Arena* arena = &runtime->tables;
    const uint32_t symbol_count = symbols->count;
    uint32_t linker_count = 0;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(runtime_linker_names); i += 1)
    {
        linker_count += (runtime_linker_names[i].kinds & BUSTER_RUNTIME_KIND_BIT(runtime->kind)) != 0 ? 1 : 0;
    }
    const uint64_t bound = (uint64_t)symbol_count + exports->count + references->count + loader->count + linker_count;
    runtime->symbol_names = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, symbol_count);
    Scratch scratch = scratch_begin(arena);
    SymbolId* named = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, SymbolId, symbol_count);
    const uint8_t** bytes = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, const uint8_t*, bound);
    uint32_t* lengths = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, bound);
    uint64_t* hashes = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint64_t, bound);
    uint32_t* first = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, bound);
    uint32_t* entries = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, bound + 1);
    RuntimeError result = runtime->symbol_names != 0 && named != 0 && bytes != 0 && lengths != 0 && hashes != 0 && first != 0 && entries != 0 ? RUNTIME_ERROR_NONE : RUNTIME_ERROR_MEMORY;
    const __m512i lane_index = _mm512_set_epi64(7, 6, 5, 4, 3, 2, 1, 0);
    uint32_t named_count = 0;
    for (uint32_t batch = 1; result == RUNTIME_ERROR_NONE && batch < symbol_count; batch += BUSTER_IMPORT_BATCH)
    {
        const uint32_t n = symbol_count - batch < BUSTER_IMPORT_BATCH ? symbol_count - batch : BUSTER_IMPORT_BATCH;
        const __mmask8 loaded = (__mmask8)import_lane_mask(n);
        const __m512i flags = _mm512_cvtepu8_epi64(_mm_maskz_loadu_epi8(loaded, symbols->flags + batch));
        const __m512i name = _mm512_cvtepu32_epi64(_mm256_maskz_loadu_epi32(loaded, symbols->name + batch));
        const uint32_t kept = _mm512_test_epi64_mask(flags, _mm512_set1_epi64(BUSTER_LINK_SYMBOL_BINDING)) & _mm512_test_epi64_mask(name, name) & loaded;
        import_store32(named + named_count, _mm512_maskz_compress_epi64((__mmask8)kept, _mm512_add_epi64(lane_index, _mm512_set1_epi64(batch))), (uint32_t)__builtin_popcount(kept));
        named_count += (uint32_t)__builtin_popcount(kept);
    }
    // The candidates: the objects' global names, libc's exports and references, the loader's exports, and the names the
    // linker defines, which a program can refer to whether or not the runtime does.
    const uint32_t references_end = named_count + exports->count + references->count;
    const uint32_t loader_end = references_end + loader->count;
    const uint32_t count = loader_end + linker_count;
    for (uint32_t i = 0; result == RUNTIME_ERROR_NONE && i < named_count; i += 1)
    {
        const NameId name = symbols->name[named[i]];
        bytes[i] = link->names.bytes[name];
        lengths[i] = link->names.length[name];
        hashes[i] = link->names.hash[name];
    }
    // A static executable's runtime has no exports or references.
    if (result == RUNTIME_ERROR_NONE && exports->count != 0)
    {
        memcpy(bytes + named_count, exports->name, exports->count * sizeof(*bytes));
        memcpy(lengths + named_count, exports->length, exports->count * sizeof(*lengths));
        memcpy(hashes + named_count, exports->hash, exports->count * sizeof(*hashes));
    }
    if (result == RUNTIME_ERROR_NONE && references->count != 0)
    {
        memcpy(bytes + named_count + exports->count, references->name, references->count * sizeof(*bytes));
        memcpy(lengths + named_count + exports->count, references->length, references->count * sizeof(*lengths));
        memcpy(hashes + named_count + exports->count, references->hash, references->count * sizeof(*hashes));
    }
    if (result == RUNTIME_ERROR_NONE && loader->count != 0)
    {
        memcpy(bytes + references_end, loader->name, loader->count * sizeof(*bytes));
        memcpy(lengths + references_end, loader->length, loader->count * sizeof(*lengths));
        memcpy(hashes + references_end, loader->hash, loader->count * sizeof(*hashes));
    }
    if (result == RUNTIME_ERROR_NONE)
    {
        uint32_t taken = 0;
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(runtime_linker_names); i += 1)
        {
            const uint32_t at = loader_end + taken;
            const bool defined = (runtime_linker_names[i].kinds & BUSTER_RUNTIME_KIND_BIT(runtime->kind)) != 0;
            bytes[at] = defined ? (const uint8_t*)runtime_linker_names[i].name : bytes[at];
            lengths[at] = defined ? (uint32_t)strlen(runtime_linker_names[i].name) : lengths[at];
            hashes[at] = defined ? link_hash(bytes[at], lengths[at]) : hashes[at];
            taken += defined ? 1 : 0;
        }
    }
    ImportNameIndex candidates = { 0 };
    if (result == RUNTIME_ERROR_NONE && !import_index_build(&candidates, hashes, count, scratch.arena))
    {
        result = RUNTIME_ERROR_MEMORY;
    }
    // Each candidate's first one with its name: itself, or an earlier one of its bucket.
    const uint64_t buckets = result == RUNTIME_ERROR_NONE ? (uint64_t)1 << candidates.bucket_bits : 0;
    for (uint64_t b = 0; b < buckets; b += 1)
    {
        for (uint32_t place = candidates.start[b]; place < candidates.start[b + 1]; place += 1)
        {
            const uint32_t candidate = candidates.order[place];
            uint32_t same = candidate;
            for (uint32_t other = candidates.start[b]; same == candidate && other < place; other += 1)
            {
                const uint32_t earlier = candidates.order[other];
                const bool equal = candidates.hash[other] == hashes[candidate] && lengths[earlier] == lengths[candidate] &&
                                   import_same_bytes(bytes[earlier], bytes[candidate], lengths[candidate]);
                same = equal ? first[earlier] : same;
            }
            first[candidate] = same;
        }
    }
    // The first ones numbered in candidate order: entries[c] is the number of first ones before candidate c.
    for (uint32_t c = 0; result == RUNTIME_ERROR_NONE && c < count; c += 1)
    {
        entries[c] = first[c] == c ? 1 : 0;
    }
    if (result == RUNTIME_ERROR_NONE)
    {
        entries[count] = 0;
        import_exclusive_scan(entries, count + 1);
    }
    const uint32_t entry_count = result == RUNTIME_ERROR_NONE ? entries[count] : 0;
    // The first ones' bytes, one after another in entry order.
    uint64_t strings_length = 0;
    for (uint32_t c = 0; result == RUNTIME_ERROR_NONE && c < count; c += 1)
    {
        strings_length += first[c] == c ? lengths[c] : 0;
    }
    if (result == RUNTIME_ERROR_NONE)
    {
        uint8_t* strings = strings_length <= UINT32_MAX ? arena_push(arena, strings_length, 64) : 0;
        names->strings = strings;
        names->strings_length = strings_length;
        names->offset = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, entry_count);
        names->length = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, entry_count);
        names->hash = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, entry_count);
        names->definition = BUSTER_ARENA_PUSH_ARRAY(arena, SymbolId, entry_count);
        names->member = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, entry_count);
        names->export = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, entry_count);
        names->referenced = BUSTER_ARENA_PUSH_ARRAY(arena, bool, entry_count);
        names->linker = BUSTER_ARENA_PUSH_ARRAY(arena, bool, entry_count);
        names->count = entry_count;
        const bool pushed = (strings != 0 || strings_length == 0) && names->offset != 0 && names->length != 0 && names->hash != 0 && names->definition != 0 &&
                            names->member != 0 && names->export != 0 && names->referenced != 0 && names->linker != 0;
        result = pushed ? result : RUNTIME_ERROR_MEMORY;
    }
    if (result == RUNTIME_ERROR_NONE)
    {
        memset(names->definition, 0, entry_count * sizeof(*names->definition));
        memset(names->member, 0, entry_count * sizeof(*names->member));
        memset(names->export, 0, entry_count * sizeof(*names->export));
        memset(names->referenced, 0, entry_count * sizeof(*names->referenced));
        memset(names->linker, 0, entry_count * sizeof(*names->linker));
        memset(runtime->symbol_names, 0, symbol_count * sizeof(*runtime->symbol_names));
    }
    // Each candidate's entry: a first one's spelling, and what each candidate says of its name. The symbols' candidates
    // come in symbol order, and so do the pieces, so the piece of each is the one the walk is in.
    uint64_t position = 0;
    uint32_t piece = 0;
    const uint32_t piece_count = RUNTIME_OBJECTS + runtime->member_count;
    for (uint32_t c = 0; result == RUNTIME_ERROR_NONE && c < count; c += 1)
    {
        const uint32_t entry = entries[first[c]];
        if (first[c] == c)
        {
            memcpy((uint8_t*)names->strings + position, bytes[c], lengths[c]);
            names->offset[entry] = (uint32_t)position;
            names->length[entry] = lengths[c];
            names->hash[entry] = hashes[c];
            position += lengths[c];
        }
        if (c < named_count)
        {
            const SymbolId symbol = named[c];
            while (piece + 1 < piece_count && runtime->pieces[piece].end.symbols <= symbol)
            {
                piece += 1;
            }
            const bool first_definition = names->definition[entry] == 0 && (symbols->atom[symbol] != 0 || symbols->source[symbol] == BUSTER_LINK_SOURCE_ABSOLUTE);
            runtime->symbol_names[symbol] = entry + 1;
            names->definition[entry] = first_definition ? symbol : names->definition[entry];
            names->member[entry] = first_definition && piece >= RUNTIME_OBJECTS ? piece - RUNTIME_OBJECTS + 1 : names->member[entry];
        }
        else if (c < named_count + exports->count)
        {
            names->export[entry] = c - named_count + 1;
        }
        else if (c < references_end)
        {
            names->referenced[entry] = true;
        }
        else if (c < loader_end)
        {
            // The loader's export, numbered after libc.so.6's, unless libc.so.6 exports the name too: GNU ld takes
            // libc.so.6's, which libc.so's script names first.
            names->export[entry] = names->export[entry] != 0 ? names->export[entry] : exports->count + (c - references_end) + 1;
        }
        else
        {
            names->linker[entry] = true;
        }
    }
    if (result == RUNTIME_ERROR_NONE && !import_index_build(&names->index, names->hash, entry_count, arena))
    {
        result = RUNTIME_ERROR_MEMORY;
    }
    // The filter: a word per four names at most, a power of two of them and 16 at least, so that a word holds about eight
    // bits at most and a name that isn't there passes for one at most about 1.6% of the time (a dynamic runtime's 2,700
    // or so names, libc.so.6's and the loader's among them, in 1,024 words, about 0.7%).
    uint32_t filter_bits = 4;
    while (((uint64_t)1 << filter_bits) * 4 < entry_count)
    {
        filter_bits += 1;
    }
    names->filter_bits = filter_bits;
    names->filter = result == RUNTIME_ERROR_NONE ? BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, (uint64_t)1 << filter_bits) : 0;
    result = result == RUNTIME_ERROR_NONE && names->filter == 0 ? RUNTIME_ERROR_MEMORY : result;
    if (result == RUNTIME_ERROR_NONE)
    {
        memset(names->filter, 0, ((uint64_t)1 << filter_bits) * sizeof(uint64_t));
        for (uint32_t e = 0; e < entry_count; e += 1)
        {
            names->filter[names->hash[e] >> (64 - filter_bits)] |= runtime_name_filter_bits(names->hash[e]);
        }
    }
    scratch_end(scratch);
    return result;
}

static uint64_t runtime_name_filter_bits(uint64_t hash)
{
    return (1ull << (hash & 63)) | (1ull << ((hash >> 6) & 63));
}

static const ImportLibrary* runtime_export_library(const Runtime* runtime, uint32_t export, uint32_t* index)
{
    const bool libc = export < runtime->libc.exports.count;
    *index = libc ? export : export - runtime->libc.exports.count;
    return libc ? &runtime->libc : &runtime->loader;
}

static bool runtime_loader_version(const Runtime* runtime, uint16_t version)
{
    bool result = false;
    for (uint32_t v = 0; v < runtime->loader.version_count; v += 1)
    {
        result = result || runtime->loader.versions[v].index == version;
    }
    return result;
}

static uint32_t runtime_find_global_name(const RuntimeNames* names, const uint8_t* bytes, uint32_t length, uint64_t hash)
{
    const ImportNameIndex* index = &names->index;
    uint32_t result = 0;
    uint32_t begin = 0;
    uint32_t end = 0;
    import_index_bucket(index, hash, &begin, &end);
    for (uint32_t place = begin; result == 0 && place < end; place += 1)
    {
        const uint32_t entry = index->order[place];
        const bool same = index->hash[place] == hash && names->length[entry] == length && import_same_bytes(names->strings + names->offset[entry], bytes, length);
        result = same ? entry + 1 : 0;
    }
    return result;
}

static const uint8_t* runtime_global_name(const RuntimeNames* names, uint32_t entry)
{
    return names->strings + names->offset[entry];
}

// The strong references of each piece to global names (Runtime.reference_begin and references): eight symbols at a time,
// a compress of the global names' entries of those that have no atom, aren't absolute and are global rather than weak,
// a piece's after the last piece's. Then the member each loads (Runtime.reference_members), a scalar load of its name's
// member per reference, in a pass of its own.
static RuntimeError runtime_references(Runtime* runtime)
{
    const LinkSymbols* symbols = &runtime->link.symbols;
    const RuntimeNames* names = &runtime->global_names;
    const uint32_t piece_count = RUNTIME_OBJECTS + runtime->member_count;
    runtime->reference_begin = BUSTER_ARENA_PUSH_ARRAY(&runtime->tables, uint32_t, (uint64_t)piece_count + 1);
    runtime->references = BUSTER_ARENA_PUSH_ARRAY(&runtime->tables, uint32_t, (uint64_t)symbols->count + BUSTER_IMPORT_BATCH);
    runtime->reference_members = BUSTER_ARENA_PUSH_ARRAY(&runtime->tables, uint32_t, (uint64_t)symbols->count + BUSTER_IMPORT_BATCH);
    RuntimeError result = runtime->reference_begin != 0 && runtime->references != 0 && runtime->reference_members != 0 ? RUNTIME_ERROR_NONE : RUNTIME_ERROR_MEMORY;
    uint32_t total = 0;
    for (uint32_t p = 0; result == RUNTIME_ERROR_NONE && p < piece_count; p += 1)
    {
        const SymbolId begin = runtime->pieces[p].begin.symbols;
        const SymbolId end = runtime->pieces[p].end.symbols;
        runtime->reference_begin[p] = total;
        for (SymbolId first = begin; first < end; first += BUSTER_IMPORT_BATCH)
        {
            const uint32_t n = end - first < BUSTER_IMPORT_BATCH ? end - first : BUSTER_IMPORT_BATCH;
            const __mmask8 loaded = (__mmask8)import_lane_mask(n);
            const __m512i atoms = _mm512_cvtepu32_epi64(_mm256_maskz_loadu_epi32(loaded, symbols->atom + first));
            const __m512i sources = _mm512_cvtepu8_epi64(_mm_maskz_loadu_epi8(loaded, symbols->source + first));
            const __m512i bindings = _mm512_and_si512(_mm512_cvtepu8_epi64(_mm_maskz_loadu_epi8(loaded, symbols->flags + first)), _mm512_set1_epi64(BUSTER_LINK_SYMBOL_BINDING));
            const __m512i entries = _mm512_cvtepu32_epi64(_mm256_maskz_loadu_epi32(loaded, runtime->symbol_names + first));
            const uint32_t strong = _mm512_testn_epi64_mask(atoms, atoms) & import_lanes_equal(sources, BUSTER_LINK_SOURCE_NONE) &
                                    import_lanes_equal(bindings, BUSTER_LINK_SYMBOL_GLOBAL) & _mm512_test_epi64_mask(entries, entries) & loaded;
            const uint32_t count = (uint32_t)__builtin_popcount(strong);
            import_store32(runtime->references + total, _mm512_maskz_compress_epi64((__mmask8)strong, _mm512_sub_epi64(entries, _mm512_set1_epi64(1))), count);
            total += count;
        }
    }
    if (result == RUNTIME_ERROR_NONE)
    {
        runtime->reference_begin[piece_count] = total;
    }
    for (uint32_t r = 0; result == RUNTIME_ERROR_NONE && r < total; r += 1)
    {
        const uint32_t entry = runtime->references[r];
        runtime->reference_members[r] = names->linker[entry] ? 0 : names->member[entry];
    }
    return result;
}

// The pieces' COMDAT groups (RuntimeGroup, Runtime.group_begin and group_atoms), from each object's ImportObject: each
// group's atoms (its sections' that became atoms), and its signature's number, the same for groups of the same
// signature, numbered in the order the signatures first come. The signatures are indexed by their hashes, and a group
// takes the number of the first one in its bucket with its bytes.
static RuntimeError runtime_groups(Runtime* runtime)
{
    const uint32_t piece_count = RUNTIME_OBJECTS + runtime->member_count;
    uint32_t group_count = 0;
    uint64_t atom_count = 0;
    for (uint32_t p = 0; p < piece_count; p += 1)
    {
        const ImportObject* object = p < RUNTIME_OBJECTS ? &runtime->objects[p] : &runtime->member_objects[p - RUNTIME_OBJECTS];
        group_count += object->group_count;
        for (uint32_t g = 0; g < object->group_count; g += 1)
        {
            atom_count += object->groups[g].count;
        }
    }
    Arena* arena = &runtime->tables;
    runtime->group_begin = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)piece_count + 1);
    runtime->groups = BUSTER_ARENA_PUSH_ARRAY(arena, RuntimeGroup, (uint64_t)group_count + 1);
    runtime->group_atoms = BUSTER_ARENA_PUSH_ARRAY(arena, AtomId, atom_count + 1);
    Scratch scratch = scratch_begin(arena);
    String8* signatures = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, String8, (uint64_t)group_count + 1);
    uint64_t* hashes = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint64_t, (uint64_t)group_count + 1);
    uint32_t* firsts = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, (uint64_t)group_count + 1);
    RuntimeError result = runtime->group_begin != 0 && runtime->groups != 0 && runtime->group_atoms != 0 && signatures != 0 && hashes != 0 && firsts != 0
                              ? RUNTIME_ERROR_NONE
                              : RUNTIME_ERROR_MEMORY;
    uint32_t group = 0;
    uint32_t atoms = 0;
    for (uint32_t p = 0; result == RUNTIME_ERROR_NONE && p < piece_count; p += 1)
    {
        const ImportObject* object = p < RUNTIME_OBJECTS ? &runtime->objects[p] : &runtime->member_objects[p - RUNTIME_OBJECTS];
        runtime->group_begin[p] = group;
        for (uint32_t g = 0; g < object->group_count; g += 1)
        {
            const ImportGroup* imported = &object->groups[g];
            runtime->groups[group] = (RuntimeGroup){
                .first = atoms,
            };
            for (uint32_t i = 0; i < imported->count; i += 1)
            {
                const AtomId atom = object->section_atoms[object->group_sections[imported->first + i]];
                runtime->group_atoms[atoms] = atom;
                atoms += atom != 0 ? 1 : 0;
            }
            runtime->groups[group].count = atoms - runtime->groups[group].first;
            signatures[group] = imported->signature;
            hashes[group] = link_hash((const uint8_t*)imported->signature.pointer, imported->signature.length);
            group += 1;
        }
    }
    ImportNameIndex index = { 0 };
    if (result == RUNTIME_ERROR_NONE)
    {
        runtime->group_begin[piece_count] = group;
        result = import_index_build(&index, hashes, group_count, scratch.arena) ? result : RUNTIME_ERROR_MEMORY;
    }
    // Each group's first group of its signature: itself, or an earlier one of its bucket, whose places come in group order.
    const uint64_t buckets = result == RUNTIME_ERROR_NONE ? (uint64_t)1 << index.bucket_bits : 0;
    for (uint64_t b = 0; b < buckets; b += 1)
    {
        for (uint32_t place = index.start[b]; place < index.start[b + 1]; place += 1)
        {
            const uint32_t g = index.order[place];
            uint32_t same = g;
            for (uint32_t other = index.start[b]; same == g && other < place; other += 1)
            {
                const uint32_t earlier = index.order[other];
                const bool equal = index.hash[other] == hashes[g] && signatures[earlier].length == signatures[g].length &&
                                   import_same_bytes((const uint8_t*)signatures[earlier].pointer, (const uint8_t*)signatures[g].pointer, signatures[g].length);
                same = equal ? firsts[earlier] : same;
            }
            firsts[g] = same;
        }
    }
    // Numbered in group order: a first one takes the next number, the others their first one's.
    uint32_t distinct = 0;
    for (uint32_t g = 0; result == RUNTIME_ERROR_NONE && g < group_count; g += 1)
    {
        runtime->groups[g].signature = firsts[g] == g ? distinct : runtime->groups[firsts[g]].signature;
        distinct += firsts[g] == g ? 1 : 0;
    }
    runtime->group_signatures = distinct;
    scratch_end(scratch);
    return result;
}

// What failed to import: the file's path, or for a member, the archive's and the member's name.
static RuntimeError runtime_fail_member(Runtime* runtime, const RuntimeArchive* archive, const ImportMember* member, ImportResult import)
{
    runtime->import = import;
    snprintf(runtime->failed_file, sizeof(runtime->failed_file), "%.*s(%.*s)", (int)archive->path.length, archive->path.pointer, (int)member->name.length,
             member->name.pointer);
    return RUNTIME_ERROR_IMPORT;
}

// Moves the loader's version indices, its versions' and its exports', past libc.so.6's largest, so that an index names
// one library's version (see the orientation comment). VER_NDX_LOCAL and VER_NDX_GLOBAL, which no export of a library
// with versions has, stay as they are.
static void runtime_move_loader_versions(Runtime* runtime)
{
    uint16_t base = 0;
    for (uint32_t v = 0; v < runtime->libc.version_count; v += 1)
    {
        base = runtime->libc.versions[v].index > base ? runtime->libc.versions[v].index : base;
    }
    ImportLibrary* loader = &runtime->loader;
    for (uint32_t v = 0; v < loader->version_count; v += 1)
    {
        loader->versions[v].index = (uint16_t)(loader->versions[v].index + base);
    }
    for (uint32_t e = 0; e < loader->exports.count; e += 1)
    {
        const uint16_t version = loader->exports.version[e];
        loader->exports.version[e] = version > VER_NDX_GLOBAL ? (uint16_t)(version + base) : version;
    }
}

// Imports the runtime of `kind` (see the orientation comment).
static RuntimeError runtime_import_kind(Runtime* runtime, RuntimeKind kind)
{
    RuntimeError result = runtime_begin(runtime) ? RUNTIME_ERROR_NONE : RUNTIME_ERROR_MEMORY;
    runtime->kind = kind;
    // A dynamic PIE's runtime and a shared library's have libc.so's libraries.
    const bool dynamic = !runtime_kind_is_static(kind);
    if (result == RUNTIME_ERROR_NONE)
    {
        result = runtime_search(runtime);
    }
    if (result == RUNTIME_ERROR_NONE && dynamic)
    {
        result = runtime_read_script(runtime);
    }
    // Read every file, then size the runtime's link for the objects and the archives' members. A shared library's runtime
    // has no start object: no file, and an empty piece.
    for (uint32_t i = 0; result == RUNTIME_ERROR_NONE && i < RUNTIME_OBJECTS; i += 1)
    {
        runtime->object_bytes[i] = runtime->object_paths[i].length != 0 ? runtime_read(runtime, runtime->object_paths[i], &result) : (String8){ 0 };
    }
    for (uint32_t a = 0; result == RUNTIME_ERROR_NONE && a < runtime->archive_count; a += 1)
    {
        RuntimeArchive* archive = &runtime->archives[a];
        archive->bytes = runtime_read(runtime, archive->path, &result);
        const ImportResult read = result == RUNTIME_ERROR_NONE ? import_archive(&archive->archive, archive->bytes, &runtime->files) : (ImportResult){ 0 };
        result = read.error == IMPORT_ERROR_NONE ? result : runtime_fail_import(runtime, archive->path, read);
        archive->first_member = runtime->member_count;
        runtime->member_count += result == RUNTIME_ERROR_NONE ? archive->archive.member_count : 0;
    }
    if (result == RUNTIME_ERROR_NONE && dynamic)
    {
        runtime->libc_bytes = runtime_read(runtime, runtime->libc_path, &result);
    }
    if (result == RUNTIME_ERROR_NONE && dynamic)
    {
        const ImportResult library = import_library(&runtime->libc, runtime->libc_bytes, &runtime->files);
        result = library.error == IMPORT_ERROR_NONE ? result : runtime_fail_import(runtime, runtime->libc_path, library);
    }
    // The loader, when libc.so's script names one, its version indices moved past libc.so.6's.
    const bool loader = dynamic && runtime->loader_path.length != 0;
    if (result == RUNTIME_ERROR_NONE && loader)
    {
        runtime->loader_bytes = runtime_read(runtime, runtime->loader_path, &result);
    }
    if (result == RUNTIME_ERROR_NONE && loader)
    {
        const ImportResult library = import_library(&runtime->loader, runtime->loader_bytes, &runtime->files);
        result = library.error == IMPORT_ERROR_NONE ? result : runtime_fail_import(runtime, runtime->loader_path, library);
    }
    if (result == RUNTIME_ERROR_NONE && loader)
    {
        runtime_move_loader_versions(runtime);
    }
    // Every archive's members, one after another.
    if (result == RUNTIME_ERROR_NONE)
    {
        runtime->members = BUSTER_ARENA_PUSH_ARRAY(&runtime->files, ImportMember, (uint64_t)runtime->member_count + 1);
        result = runtime->members != 0 ? result : RUNTIME_ERROR_MEMORY;
    }
    for (uint32_t a = 0; result == RUNTIME_ERROR_NONE && a < runtime->archive_count; a += 1)
    {
        const RuntimeArchive* archive = &runtime->archives[a];
        memcpy(runtime->members + archive->first_member, archive->archive.members, (uint64_t)archive->archive.member_count * sizeof(ImportMember));
    }
    LinkCapacity capacity = { 0 };
    for (uint32_t i = 0; result == RUNTIME_ERROR_NONE && i < RUNTIME_OBJECTS; i += 1)
    {
        result = runtime->object_paths[i].length != 0 ? runtime_count(runtime, runtime->object_paths[i], runtime->object_bytes[i], &capacity) : result;
    }
    for (uint32_t a = 0; result == RUNTIME_ERROR_NONE && a < runtime->archive_count; a += 1)
    {
        const RuntimeArchive* archive = &runtime->archives[a];
        for (uint32_t m = 0; result == RUNTIME_ERROR_NONE && m < archive->archive.member_count; m += 1)
        {
            result = runtime_count(runtime, archive->path, archive->archive.members[m].bytes, &capacity);
        }
    }
    if (result == RUNTIME_ERROR_NONE && !link_initialize(&runtime->link, &runtime->tables, &runtime->names, capacity))
    {
        result = RUNTIME_ERROR_MEMORY;
    }
    // The crt objects as units 0 to 4, then each archive's members, each a unit of its own, each object's records a
    // piece.
    runtime->member_unit = RUNTIME_OBJECTS;
    if (result == RUNTIME_ERROR_NONE)
    {
        runtime->member_objects = BUSTER_ARENA_PUSH_ARRAY(&runtime->files, ImportObject, (uint64_t)runtime->member_count + 1);
        runtime->pieces = BUSTER_ARENA_PUSH_ARRAY(&runtime->files, RuntimePiece, (uint64_t)RUNTIME_OBJECTS + runtime->member_count);
        result = runtime->member_objects != 0 && runtime->pieces != 0 ? result : RUNTIME_ERROR_MEMORY;
    }
    for (uint32_t i = 0; result == RUNTIME_ERROR_NONE && i < RUNTIME_OBJECTS; i += 1)
    {
        const bool present = runtime->object_paths[i].length != 0;
        runtime->pieces[i].begin = import_mark(&runtime->link, &runtime->files);
        const ImportResult imported = present ? import_object(&runtime->link, runtime->object_bytes[i], i, BUSTER_LINK_SOURCE_RUNTIME_ARCHIVE, &runtime->files,
                                                              &runtime->objects[i])
                                              : (ImportResult){ 0 };
        runtime->pieces[i].end = import_mark(&runtime->link, &runtime->files);
        result = imported.error == IMPORT_ERROR_NONE ? result : runtime_fail_import(runtime, runtime->object_paths[i], imported);
    }
    for (uint32_t a = 0; result == RUNTIME_ERROR_NONE && a < runtime->archive_count; a += 1)
    {
        const RuntimeArchive* archive = &runtime->archives[a];
        for (uint32_t i = 0; result == RUNTIME_ERROR_NONE && i < archive->archive.member_count; i += 1)
        {
            const uint32_t m = archive->first_member + i;
            RuntimePiece* piece = &runtime->pieces[RUNTIME_OBJECTS + m];
            piece->begin = import_mark(&runtime->link, &runtime->files);
            const ImportResult imported = import_object(&runtime->link, runtime->members[m].bytes, runtime->member_unit + m, BUSTER_LINK_SOURCE_RUNTIME_ARCHIVE,
                                                        &runtime->files, &runtime->member_objects[m]);
            piece->end = import_mark(&runtime->link, &runtime->files);
            result = imported.error == IMPORT_ERROR_NONE ? result : runtime_fail_member(runtime, archive, &runtime->members[m], imported);
        }
    }
    if (result == RUNTIME_ERROR_NONE)
    {
        runtime->abi_note = runtime_section(runtime->object_bytes[RUNTIME_OBJECT_START], ".note.ABI-tag");
        result = runtime_bind(runtime);
    }
    if (result == RUNTIME_ERROR_NONE)
    {
        result = runtime_global_names(runtime);
    }
    if (result == RUNTIME_ERROR_NONE)
    {
        result = runtime_references(runtime);
    }
    if (result == RUNTIME_ERROR_NONE)
    {
        result = runtime_groups(runtime);
    }
    // The room a link that binds against the runtime needs for its names' and atoms' bytes, summed once here rather than
    // by each link (bind_capacity).
    for (NameId name = 1; result == RUNTIME_ERROR_NONE && name < runtime->link.names.count; name += 1)
    {
        runtime->name_room += (uint64_t)runtime->link.names.length[name] + 1;
    }
    for (AtomId atom = 1; result == RUNTIME_ERROR_NONE && atom < runtime->link.atoms.count; atom += 1)
    {
        const LinkAtoms* atoms = &runtime->link.atoms;
        runtime->atom_room += atoms->bytes[atom] != 0 ? atoms->size[atom] + ((uint64_t)1 << atoms->alignment_log2[atom]) - 1 : 0;
    }
    result = result == RUNTIME_ERROR_NONE && runtime->probes_full ? RUNTIME_ERROR_MEMORY : result;
    return result;
}

static RuntimeError runtime_import(Runtime* runtime)
{
    return runtime_import_kind(runtime, RUNTIME_KIND_DYNAMIC);
}

static RuntimeError runtime_import_static(Runtime* runtime)
{
    return runtime_import_kind(runtime, RUNTIME_KIND_STATIC);
}

static RuntimeError runtime_import_static_pie(Runtime* runtime)
{
    return runtime_import_kind(runtime, RUNTIME_KIND_STATIC_PIE);
}

static RuntimeError runtime_import_shared(Runtime* runtime)
{
    return runtime_import_kind(runtime, RUNTIME_KIND_SHARED);
}

static void runtime_release(Runtime* runtime)
{
    if (runtime->snapshot != 0)
    {
        munmap(runtime->snapshot, runtime->snapshot_bytes);
        runtime->snapshot = 0;
    }
    else
    {
        arena_release(&runtime->tables);
        arena_release(&runtime->names);
        arena_release(&runtime->files);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Libraries a link names (-l)

// The address space the libraries' arena reserves: their files' bytes and columns.
#define BUSTER_RUNTIME_LIBRARIES_BYTES (UINT64_C(1) << 34)

// Reads the file at `path` into the libraries' arena: its bytes, NUL-terminated, or empty with *error set.
static String8 runtime_libraries_read(RuntimeLibraries* libraries, const char* path, RuntimeError* error)
{
    String8 result = { 0 };
    const int file = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    struct stat status;
    const bool opened = file >= 0 && fstat(file, &status) == 0 && S_ISREG(status.st_mode);
    char* bytes = opened ? arena_push(&libraries->arena, (uint64_t)status.st_size + 1, 64) : 0;
    uint64_t done = 0;
    int read_error = opened ? 0 : file >= 0 ? EINVAL : errno;
    while (bytes != 0 && read_error == 0 && done < (uint64_t)status.st_size)
    {
        const ssize_t count = read(file, bytes + done, (uint64_t)status.st_size - done);
        read_error = count > 0 || (count < 0 && errno == EINTR) ? 0 : count == 0 ? EIO : errno;
        done += count > 0 ? (uint64_t)count : 0;
    }
    if (file >= 0)
    {
        close(file);
    }
    if (opened && bytes == 0)
    {
        *error = RUNTIME_ERROR_MEMORY;
    }
    else if (!opened || read_error != 0)
    {
        *error = RUNTIME_ERROR_FILE;
        libraries->system_error = read_error;
    }
    else
    {
        bytes[done] = 0;
        result.pointer = bytes;
        result.length = done;
    }
    if (*error != RUNTIME_ERROR_NONE)
    {
        snprintf(libraries->failed, sizeof(libraries->failed), "%s", path);
    }
    return result;
}

// Finds -l `name` into `path`: lib<name>.so, or else lib<name>.a, in the first directory that has one, the -L ones in
// order, then the library directories. Whether one was found, and in *archive whether it is the archive.
static bool runtime_libraries_find(String8 name, const String8* directories, uint32_t directory_count, char* path, bool* archive)
{
    const uint32_t total = directory_count + (uint32_t)BUSTER_ARRAY_LENGTH(runtime_library_directories);
    bool found = false;
    *archive = false;
    for (uint32_t d = 0; !found && d < total; d += 1)
    {
        const String8 directory = d < directory_count ? directories[d] : (String8){
            .pointer = (char*)runtime_library_directories[d - directory_count],
            .length = strlen(runtime_library_directories[d - directory_count]),
        };
        for (uint32_t kind = 0; !found && kind < 2; kind += 1)
        {
            const int length = snprintf(path, BUSTER_RUNTIME_PATH_BYTES, "%.*s/lib%.*s%s", (int)directory.length, directory.pointer, (int)name.length,
                                        name.pointer, kind == 0 ? ".so" : ".a");
            struct stat status;
            found = length > 0 && length < BUSTER_RUNTIME_PATH_BYTES && stat(path, &status) == 0 && S_ISREG(status.st_mode);
            *archive = found && kind == 1;
        }
    }
    return found;
}

// The largest version index of `library`'s definitions (0 for a library without versions).
static uint16_t runtime_libraries_top(const ImportLibrary* library)
{
    uint16_t top = 0;
    for (uint32_t v = 0; v < library->version_count; v += 1)
    {
        top = library->versions[v].index > top ? library->versions[v].index : top;
    }
    return top;
}

// Reads the names library `bytes`' DT_NEEDED entries give, before its first DT_NULL, into slot `l`, the array pushed from
// the libraries' arena: RUNTIME_ERROR_IMPORT if they can't be read (import_library has checked its .dynamic), or
// RUNTIME_ERROR_MEMORY. A library without .dynamic has none. A pass over a few entries per library, beside its import.
static RuntimeError runtime_libraries_dependencies(RuntimeLibraries* libraries, uint32_t l, String8 bytes)
{
    ImportElf elf;
    RuntimeError result = import_elf(&elf, bytes, ET_DYN).error == IMPORT_ERROR_NONE ? RUNTIME_ERROR_NONE : RUNTIME_ERROR_IMPORT;
    uint32_t dynamic = 0;
    for (uint32_t index = 1; result == RUNTIME_ERROR_NONE && dynamic == 0 && index < elf.section_count; index += 1)
    {
        dynamic = import_section_header(&elf, index).sh_type == SHT_DYNAMIC ? index : 0;
    }
    const Elf64_Shdr header = dynamic != 0 ? import_section_header(&elf, dynamic) : (Elf64_Shdr){ 0 };
    uint64_t count = 0;
    const bool table = dynamic != 0 && import_table(&header, sizeof(Elf64_Dyn), &count);
    const bool linked = table && header.sh_link != SHN_UNDEF && header.sh_link < elf.section_count &&
                        import_section_header(&elf, header.sh_link).sh_type == SHT_STRTAB;
    result = dynamic != 0 && !linked ? RUNTIME_ERROR_IMPORT : result;
    const Elf64_Shdr strings_header = linked ? import_section_header(&elf, header.sh_link) : header;
    const String8 strings = linked ? import_section_bytes(&elf, &strings_header) : (String8){ 0 };
    const String8 entries = linked ? import_section_bytes(&elf, &header) : (String8){ 0 };
    String8* names = result == RUNTIME_ERROR_NONE && linked ? BUSTER_ARENA_PUSH_ARRAY(&libraries->arena, String8, count + 1) : 0;
    result = result == RUNTIME_ERROR_NONE && linked && names == 0 ? RUNTIME_ERROR_MEMORY : result;
    uint32_t found = 0;
    bool ended = false;
    for (uint64_t i = 0; result == RUNTIME_ERROR_NONE && names != 0 && !ended && i < count; i += 1)
    {
        Elf64_Dyn entry;
        memcpy(&entry, entries.pointer + i * sizeof(entry), sizeof(entry));
        ended = entry.d_tag == DT_NULL;
        if (entry.d_tag == DT_NEEDED)
        {
            result = import_string(strings, entry.d_un.d_val, &names[found]) ? result : RUNTIME_ERROR_IMPORT;
            found += result == RUNTIME_ERROR_NONE ? 1 : 0;
        }
    }
    libraries->dependencies[l] = names;
    libraries->dependency_count[l] = found;
    return result;
}

// The name of library `l`'s version definition that `index` (import_library's, moved past the libraries' before it)
// names, or nothing for VER_NDX_LOCAL and VER_NDX_GLOBAL, which are no version.
static String8 runtime_libraries_version_name(const RuntimeLibraries* libraries, uint32_t l, uint16_t index)
{
    const ImportLibrary* library = &libraries->libraries[l];
    String8 result = { 0 };
    for (uint32_t v = 0; index > VER_NDX_GLOBAL && result.length == 0 && v < library->version_count; v += 1)
    {
        result = library->versions[v].index == index && !library->versions[v].base ? library->versions[v].name : result;
    }
    return result;
}

// Reads what import_library leaves out of library `l`'s versions, from `bytes`: each reference's required version and
// the file it requires it of (its .gnu.version entry's index is a vna_other of the .gnu.version_r entry that names
// them), in the references' order, and its exports at a version other than their name's default (a .gnu.version entry
// with the hidden bit), with their versions' names (its definitions', whose indices moved by `shift`). RUNTIME_ERROR_IMPORT
// if they can't be read, or RUNTIME_ERROR_MEMORY. A library without .gnu.version has neither. A pass over the library's
// dynamic symbols and its few version needs, beside its import.
static RuntimeError runtime_libraries_versions(RuntimeLibraries* libraries, uint32_t l, String8 bytes, uint16_t shift)
{
    const ImportLibrary* library = &libraries->libraries[l];
    ImportElf elf;
    ImportLibrarySections sections = { 0 };
    ImportDynamicSymbols symbols = { 0 };
    RuntimeError result = import_elf(&elf, bytes, ET_DYN).error == IMPORT_ERROR_NONE ? RUNTIME_ERROR_NONE : RUNTIME_ERROR_IMPORT;
    uint32_t verneed = 0;
    for (uint32_t index = 1; result == RUNTIME_ERROR_NONE && index < elf.section_count; index += 1)
    {
        const uint32_t type = import_section_header(&elf, index).sh_type;
        sections.dynsym = type == SHT_DYNSYM && sections.dynsym == 0 ? index : sections.dynsym;
        sections.versym = type == SHT_GNU_versym && sections.versym == 0 ? index : sections.versym;
        verneed = type == SHT_GNU_verneed && verneed == 0 ? index : verneed;
    }
    result = result == RUNTIME_ERROR_NONE && import_library_symbols(&elf, &sections, &symbols).error != IMPORT_ERROR_NONE ? RUNTIME_ERROR_IMPORT : result;
    const uint32_t reference_count = library->references.count;
    String8* versions = result == RUNTIME_ERROR_NONE ? BUSTER_ARENA_PUSH_ARRAY(&libraries->arena, String8, (uint64_t)reference_count + 1) : 0;
    String8* files = result == RUNTIME_ERROR_NONE ? BUSTER_ARENA_PUSH_ARRAY(&libraries->arena, String8, (uint64_t)reference_count + 1) : 0;
    RuntimeHiddenExports hidden = {
        .name = result == RUNTIME_ERROR_NONE ? BUSTER_ARENA_PUSH_ARRAY(&libraries->arena, const uint8_t*, symbols.count + 1) : 0,
        .length = result == RUNTIME_ERROR_NONE ? BUSTER_ARENA_PUSH_ARRAY(&libraries->arena, uint32_t, symbols.count + 1) : 0,
        .hash = result == RUNTIME_ERROR_NONE ? BUSTER_ARENA_PUSH_ARRAY(&libraries->arena, uint64_t, symbols.count + 1) : 0,
        .version = result == RUNTIME_ERROR_NONE ? BUSTER_ARENA_PUSH_ARRAY(&libraries->arena, String8, symbols.count + 1) : 0,
    };
    const bool room = versions != 0 && files != 0 && hidden.name != 0 && hidden.length != 0 && hidden.hash != 0 && hidden.version != 0;
    result = result == RUNTIME_ERROR_NONE && !room ? RUNTIME_ERROR_MEMORY : result;
    // The version needs: each file's entries, and each entry's index (vna_other), name and file, in place.
    const Elf64_Shdr needs_header = verneed != 0 ? import_section_header(&elf, verneed) : (Elf64_Shdr){ 0 };
    const bool needs_linked = verneed != 0 && needs_header.sh_link != SHN_UNDEF && needs_header.sh_link < elf.section_count &&
                              import_section_header(&elf, needs_header.sh_link).sh_type == SHT_STRTAB;
    result = result == RUNTIME_ERROR_NONE && verneed != 0 && !needs_linked ? RUNTIME_ERROR_IMPORT : result;
    const Elf64_Shdr needs_strings_header = needs_linked ? import_section_header(&elf, needs_header.sh_link) : needs_header;
    const String8 needs = needs_linked ? import_section_bytes(&elf, &needs_header) : (String8){ 0 };
    const String8 needs_strings = needs_linked ? import_section_bytes(&elf, &needs_strings_header) : (String8){ 0 };
    uint32_t reference = 0;
    for (uint64_t i = 1; result == RUNTIME_ERROR_NONE && i < symbols.count; i += 1)
    {
        Elf64_Sym symbol;
        memcpy(&symbol, symbols.entries.pointer + i * sizeof(symbol), sizeof(symbol));
        uint16_t version = VER_NDX_GLOBAL;
        if (symbols.versions.length != 0)
        {
            memcpy(&version, symbols.versions.pointer + i * sizeof(version), sizeof(version));
        }
        String8 name = { 0 };
        const uint32_t binding = ELF64_ST_BIND(symbol.st_info);
        const uint32_t visibility = ELF64_ST_VISIBILITY(symbol.st_other);
        const bool global = binding == STB_GLOBAL || binding == STB_WEAK || binding == STB_GNU_UNIQUE;
        const uint16_t index = (uint16_t)(version & 0x7fff);
        result = import_string(symbols.names, symbol.st_name, &name) ? result : RUNTIME_ERROR_IMPORT;
        // A reference, as import_library takes them, in its order: the version need its index names, if any.
        const bool referred = result == RUNTIME_ERROR_NONE && symbol.st_shndx == SHN_UNDEF && global && name.length != 0;
        String8 required = { 0 };
        String8 file = { 0 };
        uint64_t offset = 0;
        bool walking = referred && index > VER_NDX_GLOBAL && needs.length != 0;
        for (uint64_t n = 0; walking && n < needs_header.sh_info; n += 1)
        {
            Elf64_Verneed need = { 0 };
            walking = import_within(needs.length, offset, sizeof(need));
            if (walking)
            {
                memcpy(&need, needs.pointer + offset, sizeof(need));
            }
            uint64_t at = offset + (walking ? need.vn_aux : 0);
            for (uint32_t a = 0; walking && required.length == 0 && a < need.vn_cnt; a += 1)
            {
                Elf64_Vernaux auxiliary = { 0 };
                walking = import_within(needs.length, at, sizeof(auxiliary));
                if (walking)
                {
                    memcpy(&auxiliary, needs.pointer + at, sizeof(auxiliary));
                }
                const bool match = walking && auxiliary.vna_other == index;
                walking = !match || (import_string(needs_strings, auxiliary.vna_name, &required) && import_string(needs_strings, need.vn_file, &file));
                at += walking ? auxiliary.vna_next : 0;
            }
            walking = walking && required.length == 0 && need.vn_next != 0;
            offset += walking ? need.vn_next : 0;
        }
        if (referred && reference < reference_count)
        {
            versions[reference] = required;
            files[reference] = file;
        }
        reference += referred ? 1 : 0;
        // An export at a version that isn't its name's default.
        const bool hidden_export = result == RUNTIME_ERROR_NONE && symbol.st_shndx != SHN_UNDEF && global && name.length != 0 && (version & 0x8000) != 0 &&
                                   index > VER_NDX_GLOBAL && (visibility == STV_DEFAULT || visibility == STV_PROTECTED);
        const String8 defined = hidden_export ? runtime_libraries_version_name(libraries, l, (uint16_t)(index + shift)) : (String8){ 0 };
        if (defined.length != 0)
        {
            hidden.name[hidden.count] = (const uint8_t*)name.pointer;
            hidden.length[hidden.count] = (uint32_t)name.length;
            hidden.hash[hidden.count] = link_hash((const uint8_t*)name.pointer, name.length);
            hidden.version[hidden.count] = defined;
            hidden.count += 1;
        }
    }
    // The references import_library took, one for one.
    result = result == RUNTIME_ERROR_NONE && reference != reference_count ? RUNTIME_ERROR_IMPORT : result;
    libraries->reference_versions[l] = versions;
    libraries->reference_files[l] = files;
    libraries->hidden[l] = hidden;
    return result;
}

// Imports the shared library `bytes` (at `path`, which the -l search found when `searched`) as the next library, its
// version indices moved past `*base`, which moves past its own; with an index of the names it refers to and the names
// its DT_NEEDED entries give. A library named twice (by its soname, whatever the
// path) is linked once, at its first place, as GNU ld loads it once.
static RuntimeError runtime_libraries_add(RuntimeLibraries* libraries, const char* path, bool searched, String8 bytes, uint16_t* base)
{
    RuntimeError result = libraries->count < BUSTER_RUNTIME_LIBRARIES ? RUNTIME_ERROR_NONE : RUNTIME_ERROR_MEMORY;
    ImportLibrary* library = &libraries->libraries[libraries->count];
    if (result == RUNTIME_ERROR_NONE)
    {
        libraries->import = import_library(library, bytes, &libraries->arena);
        result = libraries->import.error == IMPORT_ERROR_NONE ? result : RUNTIME_ERROR_IMPORT;
    }
    bool repeated = false;
    for (uint32_t l = 0; result == RUNTIME_ERROR_NONE && l < libraries->count; l += 1)
    {
        const String8 soname = libraries->libraries[l].soname;
        repeated = repeated || (soname.length != 0 && soname.length == library->soname.length && memcmp(soname.pointer, library->soname.pointer, soname.length) == 0);
    }
    const uint16_t top = result == RUNTIME_ERROR_NONE ? runtime_libraries_top(library) : 0;
    if (result == RUNTIME_ERROR_NONE && (uint32_t)*base + top >= 0x8000)
    {
        // Version indices are 15 bits.
        result = RUNTIME_ERROR_IMPORT;
    }
    if (result == RUNTIME_ERROR_NONE && !repeated)
    {
        for (uint32_t v = 0; v < library->version_count; v += 1)
        {
            library->versions[v].index = (uint16_t)(library->versions[v].index + *base);
        }
        for (uint32_t e = 0; e < library->exports.count; e += 1)
        {
            const uint16_t version = library->exports.version[e];
            library->exports.version[e] = version > VER_NDX_GLOBAL ? (uint16_t)(version + *base) : version;
        }
        const uint16_t shift = *base;
        *base = (uint16_t)(*base + top);
        const ImportReferences* references = &library->references;
        result = import_index_build(&libraries->references[libraries->count], references->hash, references->count, &libraries->arena) ? result : RUNTIME_ERROR_MEMORY;
        result = result == RUNTIME_ERROR_NONE ? runtime_libraries_dependencies(libraries, libraries->count, bytes) : result;
        result = result == RUNTIME_ERROR_NONE ? runtime_libraries_versions(libraries, libraries->count, bytes, shift) : result;
    }
    if (result == RUNTIME_ERROR_NONE && !repeated)
    {
        const uint64_t length = strlen(path);
        char* copy = arena_push(&libraries->arena, length + 1, 1);
        result = copy != 0 ? result : RUNTIME_ERROR_MEMORY;
        if (copy != 0)
        {
            memcpy(copy, path, length + 1);
            libraries->paths[libraries->count] = (String8){
                .pointer = copy,
                .length = length,
            };
            libraries->searched[libraries->count] = searched;
            libraries->count += 1;
        }
    }
    if (result != RUNTIME_ERROR_NONE)
    {
        snprintf(libraries->failed, sizeof(libraries->failed), "%s", path);
    }
    return result;
}

// Whether `bytes` are an ELF file: a library to import, where anything else is read as a script.
static bool runtime_libraries_elf(String8 bytes)
{
    return bytes.length >= SELFMAG && memcmp(bytes.pointer, ELFMAG, SELFMAG) == 0;
}

static RuntimeError runtime_libraries_import(RuntimeLibraries* libraries, const Runtime* runtime, const String8* names, uint32_t name_count,
                                             const String8* directories, uint32_t directory_count)
{
    *libraries = (RuntimeLibraries){ 0 };
    libraries->arena = arena_reserve(BUSTER_RUNTIME_LIBRARIES_BYTES);
    RuntimeError result = libraries->arena.base != 0 ? RUNTIME_ERROR_NONE : RUNTIME_ERROR_MEMORY;
    // Past libc.so.6's and the loader's indices, which runtime_move_loader_versions made apart.
    const uint16_t libc_top = runtime_libraries_top(&runtime->libc);
    const uint16_t loader_top = runtime_libraries_top(&runtime->loader);
    uint16_t base = libc_top > loader_top ? libc_top : loader_top;
    for (uint32_t n = 0; result == RUNTIME_ERROR_NONE && n < name_count; n += 1)
    {
        char path[BUSTER_RUNTIME_PATH_BYTES];
        bool archive = false;
        if (!runtime_libraries_find(names[n], directories, directory_count, path, &archive))
        {
            result = RUNTIME_ERROR_MISSING;
            snprintf(libraries->failed, sizeof(libraries->failed), "-l%.*s", (int)names[n].length, names[n].pointer);
        }
        const String8 bytes = result == RUNTIME_ERROR_NONE ? runtime_libraries_read(libraries, path, &result) : (String8){ 0 };
        // An archive without members (glibc's libpthread.a, libdl.a and librt.a since 2.34) adds nothing, as GNU ld links
        // it; one with members is refused.
        const uint64_t magic = sizeof(BUSTER_IMPORT_ARCHIVE_MAGIC) - 1;
        const bool empty = archive && bytes.length == magic && memcmp(bytes.pointer, BUSTER_IMPORT_ARCHIVE_MAGIC, magic) == 0;
        if (result == RUNTIME_ERROR_NONE && archive && !empty)
        {
            result = RUNTIME_ERROR_ARCHIVE;
            snprintf(libraries->failed, sizeof(libraries->failed), "%s", path);
        }
        if (result == RUNTIME_ERROR_NONE && archive)
        {
            // The empty archive.
        }
        else if (result == RUNTIME_ERROR_NONE && runtime_libraries_elf(bytes))
        {
            result = runtime_libraries_add(libraries, path, true, bytes, &base);
        }
        else if (result == RUNTIME_ERROR_NONE)
        {
            // A script's inputs, each an absolute path to a shared library (glibc's name them so).
            ImportScript script = { 0 };
            libraries->import = import_script(&script, bytes, &libraries->arena);
            result = libraries->import.error == IMPORT_ERROR_NONE ? result : RUNTIME_ERROR_IMPORT;
            if (result != RUNTIME_ERROR_NONE)
            {
                snprintf(libraries->failed, sizeof(libraries->failed), "%s", path);
            }
            for (uint32_t i = 0; result == RUNTIME_ERROR_NONE && i < script.input_count; i += 1)
            {
                const ImportScriptInput* input = &script.inputs[i];
                const uint64_t length = input->name.length;
                char input_path[BUSTER_RUNTIME_PATH_BYTES];
                snprintf(input_path, sizeof(input_path), "%.*s", (int)length, input->name.pointer);
                const bool absolute = !input->library && length > 0 && length < sizeof(input_path) && input->name.pointer[0] == '/';
                const bool input_archive = length > 2 && memcmp(input->name.pointer + length - 2, ".a", 2) == 0;
                if (!absolute || input_archive)
                {
                    result = input_archive ? RUNTIME_ERROR_ARCHIVE : RUNTIME_ERROR_IMPORT;
                    snprintf(libraries->failed, sizeof(libraries->failed), "%s, which %s names", input_path, path);
                }
                const String8 input_bytes = result == RUNTIME_ERROR_NONE ? runtime_libraries_read(libraries, input_path, &result) : (String8){ 0 };
                if (result == RUNTIME_ERROR_NONE && !runtime_libraries_elf(input_bytes))
                {
                    result = RUNTIME_ERROR_IMPORT;
                    snprintf(libraries->failed, sizeof(libraries->failed), "%s, a script that %s names", input_path, path);
                }
                result = result == RUNTIME_ERROR_NONE ? runtime_libraries_add(libraries, input_path, false, input_bytes, &base) : result;
            }
        }
    }
    return result;
}

static void runtime_libraries_release(RuntimeLibraries* libraries)
{
    arena_release(&libraries->arena);
    libraries->count = 0;
}

static uint32_t runtime_libraries_export(const RuntimeLibraries* libraries, const uint8_t* name, uint32_t length, uint64_t hash, uint32_t* export)
{
    uint32_t result = 0;
    for (uint32_t l = 0; libraries != 0 && result == 0 && l < libraries->count; l += 1)
    {
        const uint32_t found = import_library_find(&libraries->libraries[l], name, length, hash);
        *export = found != 0 ? found - 1 : *export;
        result = found != 0 ? l + 1 : 0;
    }
    return result;
}

static String8 runtime_libraries_name(const RuntimeLibraries* libraries, uint32_t l)
{
    const String8 path = libraries->paths[l];
    const String8 soname = libraries->libraries[l].soname;
    String8 result = soname.length != 0 || libraries->searched[l] ? soname : path;
    for (uint64_t i = 0; result.length == 0 && i < path.length; i += 1)
    {
        const uint64_t at = path.length - 1 - i;
        result = path.pointer[at] == '/' || at == 0 ? (String8){
            .pointer = path.pointer + at + (path.pointer[at] == '/' ? 1 : 0),
            .length = path.length - at - (path.pointer[at] == '/' ? 1 : 0),
        } : result;
    }
    return result;
}

static uint32_t runtime_libraries_export_at(const RuntimeLibraries* libraries, const uint8_t* name, uint32_t length, uint64_t hash, String8 version)
{
    uint32_t result = 0;
    for (uint32_t m = 0; libraries != 0 && result == 0 && m < libraries->count; m += 1)
    {
        const ImportLibrary* library = &libraries->libraries[m];
        const uint32_t found = import_library_find(library, name, length, hash);
        const String8 at = found != 0 ? runtime_libraries_version_name(libraries, m, library->exports.version[found - 1]) : (String8){ 0 };
        // A default export without a version matches any, as the loader takes it.
        bool exported = found != 0 && (at.length == 0 || (at.length == version.length && memcmp(at.pointer, version.pointer, version.length) == 0));
        const RuntimeHiddenExports* hidden = &libraries->hidden[m];
        for (uint32_t h = 0; !exported && h < hidden->count; h += 1)
        {
            exported = hidden->hash[h] == hash && hidden->length[h] == length && memcmp(hidden->name[h], name, length) == 0 &&
                       hidden->version[h].length == version.length && memcmp(hidden->version[h].pointer, version.pointer, version.length) == 0;
        }
        result = exported ? m + 1 : 0;
    }
    return result;
}

static bool runtime_libraries_lists(const RuntimeLibraries* libraries, uint32_t l, uint32_t m)
{
    const String8 name = runtime_libraries_name(libraries, m);
    bool listed = false;
    for (uint32_t d = 0; !listed && d < libraries->dependency_count[l]; d += 1)
    {
        const String8 dependency = libraries->dependencies[l][d];
        listed = dependency.length == name.length && memcmp(dependency.pointer, name.pointer, name.length) == 0;
    }
    return listed;
}

static bool runtime_libraries_refer(const RuntimeLibraries* libraries, uint32_t l, const uint8_t* name, uint32_t length, uint64_t hash)
{
    const ImportReferences* references = &libraries->libraries[l].references;
    const ImportNameIndex* index = &libraries->references[l];
    uint32_t begin = 0;
    uint32_t end = 0;
    import_index_bucket(index, hash, &begin, &end);
    bool found = false;
    for (uint32_t place = begin; !found && place < end; place += 1)
    {
        const uint32_t reference = index->order[place];
        found = index->hash[place] == hash && references->length[reference] == length && import_same_bytes(references->name[reference], name, length);
    }
    return found;
}

// ---------------------------------------------------------------------------------------------------------------------
// Bytes and pieces

static const uint8_t* runtime_name_bytes(const Runtime* runtime, NameId name)
{
    const uintptr_t value = (uintptr_t)runtime->link.names.bytes[name];
    return value != 0 ? (const uint8_t*)(value + runtime->byte_delta) : 0;
}

static const uint8_t* runtime_atom_bytes(const Runtime* runtime, AtomId atom)
{
    const uintptr_t value = (uintptr_t)runtime->link.atoms.bytes[atom];
    return value != 0 ? (const uint8_t*)(value + runtime->byte_delta) : 0;
}

static String8 runtime_member_name(const Runtime* runtime, uint32_t member)
{
    const uintptr_t value = (uintptr_t)runtime->members[member].name.pointer;
    const String8 result = {
        .pointer = value != 0 ? (char*)(value + runtime->string_delta) : 0,
        .length = runtime->members[member].name.length,
    };
    return result;
}

// A run of a piece's bytes that its hash takes: one table's column over the piece's records, or the piece's names' or
// atoms' bytes.
typedef struct RuntimeSlice RuntimeSlice;
struct RuntimeSlice
{
    const uint8_t* bytes;
    uint64_t size;
};

#define BUSTER_RUNTIME_PIECE_SLICES 26

static RuntimeSlice runtime_slice(const void* column, uint64_t element, uint64_t from, uint64_t to)
{
    const RuntimeSlice result = {
        .bytes = (const uint8_t*)column + element * from,
        .size = element * (to - from),
    };
    return result;
}

// A loaded runtime's piece as its hash takes it: what binding reads of it (bind.c's joins), each column of its link over
// the piece's records (piece 0's from each table's record 0, which no piece has), its symbols' global names, then its
// names' bytes in the names run and its atoms' bytes in the snapshot's data. Not the columns nothing reads of a loaded
// runtime: the atoms' addresses and the symbols' GOT slots, dynamic indices, versions and references, the linker's,
// which the runtime leaves 0, and the bindings, which only the import uses (hashing them too took a fifth more on the
// 9700X, and faulted their pages in). Returns how many slices.
static uint32_t runtime_piece_slices(const Runtime* runtime, uint32_t piece, RuntimeSlice* slices)
{
    const Link* link = &runtime->link;
    const ImportMark begin = runtime->pieces[piece].begin;
    const ImportMark end = runtime->pieces[piece].end;
    const bool first = piece == 0;
    const uint64_t names = first ? 0 : begin.names;
    const uint64_t atoms = first ? 0 : begin.atoms;
    const uint64_t symbols = first ? 0 : begin.symbols;
    const uint64_t fixups = first ? 0 : begin.fixups;
    const uint64_t wide = first ? 0 : begin.wide_addends;
    const uint64_t priorities = first ? 0 : begin.priorities;
    const uint64_t name_bytes = first ? 0 : begin.name_bytes;
    const RuntimeSlice all[BUSTER_RUNTIME_PIECE_SLICES] = {
        runtime_slice(link->names.bytes, sizeof(*link->names.bytes), names, end.names),
        runtime_slice(link->names.length, sizeof(*link->names.length), names, end.names),
        runtime_slice(link->names.hash, sizeof(*link->names.hash), names, end.names),
        runtime_slice(link->atoms.class, sizeof(*link->atoms.class), atoms, end.atoms),
        runtime_slice(link->atoms.alignment_log2, sizeof(*link->atoms.alignment_log2), atoms, end.atoms),
        runtime_slice(link->atoms.unit, sizeof(*link->atoms.unit), atoms, end.atoms),
        runtime_slice(link->atoms.size, sizeof(*link->atoms.size), atoms, end.atoms),
        runtime_slice(link->atoms.bytes, sizeof(*link->atoms.bytes), atoms, end.atoms),
        runtime_slice(link->atoms.fixup_begin, sizeof(*link->atoms.fixup_begin), atoms, end.atoms),
        runtime_slice(link->atoms.fixup_count, sizeof(*link->atoms.fixup_count), atoms, end.atoms),
        runtime_slice(link->symbols.name, sizeof(*link->symbols.name), symbols, end.symbols),
        runtime_slice(link->symbols.atom, sizeof(*link->symbols.atom), symbols, end.symbols),
        runtime_slice(link->symbols.value, sizeof(*link->symbols.value), symbols, end.symbols),
        runtime_slice(link->symbols.size, sizeof(*link->symbols.size), symbols, end.symbols),
        runtime_slice(link->symbols.flags, sizeof(*link->symbols.flags), symbols, end.symbols),
        runtime_slice(link->symbols.source, sizeof(*link->symbols.source), symbols, end.symbols),
        runtime_slice(link->fixups.offset, sizeof(*link->fixups.offset), fixups, end.fixups),
        runtime_slice(link->fixups.target, sizeof(*link->fixups.target), fixups, end.fixups),
        runtime_slice(link->fixups.addend, sizeof(*link->fixups.addend), fixups, end.fixups),
        runtime_slice(link->fixups.kind, sizeof(*link->fixups.kind), fixups, end.fixups),
        runtime_slice(link->fixups.wide_addends, sizeof(*link->fixups.wide_addends), wide, end.wide_addends),
        runtime_slice(link->priorities.atom, sizeof(*link->priorities.atom), priorities, end.priorities),
        runtime_slice(link->priorities.priority, sizeof(*link->priorities.priority), priorities, end.priorities),
        runtime_slice(runtime->symbol_names, sizeof(*runtime->symbol_names), symbols, end.symbols),
        runtime_slice(runtime->names.base, 1, name_bytes, end.name_bytes),
        runtime_slice(runtime->snapshot_data, 1, runtime->piece_data[piece], runtime->piece_data[piece + 1]),
    };
    memcpy(slices, all, sizeof(all));
    return BUSTER_RUNTIME_PIECE_SLICES;
}

// The piece hash's accumulators' starting values, its round key and its length's key: splitmix64's first 24 outputs
// from state 0x7069656365 ("piece").
static const uint64_t runtime_piece_constants[24] __attribute__((aligned(64))) = {
    0x3b6c2a1e0f287982ull, 0x9883bd502459f28bull, 0x0e440393a1363654ull, 0xec7f8f82ad4b274aull,
    0xdb044812eec1d14full, 0xf2678ff4d3cf820full, 0x71c427ba2cca1a52ull, 0xdd507d22f8cf497full,
    0xff1551593eea8aeaull, 0xd5b0fd0b8bbdc348ull, 0xcb9fbba7df47918dull, 0x44f5b75efd54f847ull,
    0x72e4a321d8d873daull, 0x6acce6e0f7f2d408ull, 0xf128baa029a7e49dull, 0xe97bfbb96d982cf1ull,
    0x4c93ee5447e895e2ull, 0x70e7cb8ac6de2be8ull, 0xb134236ffb5f0309ull, 0x9b62ff6d62ebf85eull,
    0x301611dd774bb168ull, 0x0d11f78933ee35f6ull, 0xf445a4a9f0eeb728ull, 0x0e75a7bade90190cull,
};

// A loaded runtime's piece hash: 128 bits of its slices (runtime_piece_slices), each slice's 64-byte blocks in turn,
// the last padded with zeros (masked loads, which read nothing past the slice), as executable.c's chunk hash takes a
// stream's: each block the round key of one AES round of an accumulator and then one more round with a constant key.
// The eight accumulators take the piece's blocks in turn, rotating after each, so that a slice of a few bytes takes
// one block rather than a stride; then they are folded with the length into 128 bits. A piece's slices are many and
// small (a hello world's link reads 442 of the static runtime's 2,622 pieces), so this takes no stream with a carry, as
// the chunk hash does, which would copy every slice's tail. The snapshot's writer hashes each piece so over its image,
// and runtime_piece_current again over the mapping, before binding reads the piece.
static void runtime_piece_hash(const Runtime* runtime, uint32_t piece, uint8_t* hash)
{
    RuntimeSlice slices[BUSTER_RUNTIME_PIECE_SLICES];
    const uint32_t count = runtime_piece_slices(runtime, piece, slices);
    const __m512i start = _mm512_load_si512(runtime_piece_constants);
    const __m512i key = _mm512_load_si512(runtime_piece_constants + 8);
    __m512i a0 = _mm512_xor_si512(start, _mm512_set1_epi64(1));
    __m512i a1 = _mm512_xor_si512(start, _mm512_set1_epi64(2));
    __m512i a2 = _mm512_xor_si512(start, _mm512_set1_epi64(3));
    __m512i a3 = _mm512_xor_si512(start, _mm512_set1_epi64(4));
    __m512i a4 = _mm512_xor_si512(start, _mm512_set1_epi64(5));
    __m512i a5 = _mm512_xor_si512(start, _mm512_set1_epi64(6));
    __m512i a6 = _mm512_xor_si512(start, _mm512_set1_epi64(7));
    __m512i a7 = _mm512_xor_si512(start, _mm512_set1_epi64(8));
    uint64_t length = 0;
    for (uint32_t s = 0; s < count; s += 1)
    {
        const uint8_t* bytes = slices[s].bytes;
        const uint64_t size = slices[s].size;
        for (uint64_t at = 0; at < size; at += 64)
        {
            const uint64_t taken = size - at < 64 ? size - at : 64;
            const __m512i block = _mm512_maskz_loadu_epi8(_bzhi_u64(~0ull, (uint32_t)taken), bytes + at);
            const __m512i next = _mm512_aesenc_epi128(_mm512_aesenc_epi128(a0, block), key);
            a0 = a1;
            a1 = a2;
            a2 = a3;
            a3 = a4;
            a4 = a5;
            a5 = a6;
            a6 = a7;
            a7 = next;
        }
        length += size;
    }
    __m512i x = a0;
    x = _mm512_aesenc_epi128(x, a1);
    x = _mm512_aesenc_epi128(x, a2);
    x = _mm512_aesenc_epi128(x, a3);
    x = _mm512_aesenc_epi128(x, a4);
    x = _mm512_aesenc_epi128(x, a5);
    x = _mm512_aesenc_epi128(x, a6);
    x = _mm512_aesenc_epi128(x, a7);
    const __m512i length_key = _mm512_xor_si512(_mm512_load_si512(runtime_piece_constants + 16), _mm512_set1_epi64((long long)length));
    x = _mm512_aesenc_epi128(_mm512_aesenc_epi128(x, length_key), key);
    const __m128i lane_key = _mm512_castsi512_si128(key);
    __m128i folded = _mm512_castsi512_si128(x);
    folded = _mm_aesenc_si128(folded, _mm512_extracti32x4_epi32(x, 1));
    folded = _mm_aesenc_si128(folded, _mm512_extracti32x4_epi32(x, 2));
    folded = _mm_aesenc_si128(folded, _mm512_extracti32x4_epi32(x, 3));
    folded = _mm_aesenclast_si128(_mm_aesenc_si128(folded, lane_key), lane_key);
    _mm_storeu_si128((__m128i*)hash, folded);
}

static bool runtime_piece_current(const Runtime* runtime, uint32_t piece)
{
    uint8_t hash[16];
    bool result = runtime->snapshot == 0;
    if (!result)
    {
        runtime_piece_hash(runtime, piece, hash);
        result = memcmp(hash, runtime->piece_hashes + 16 * (uint64_t)piece, sizeof(hash)) == 0;
    }
    return result;
}
