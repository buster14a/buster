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
//     dynamic loader.
// And for `cc -static main.c -o main` (RUNTIME_KIND_STATIC):
//   - the crt objects crt1.o, crti.o, crtbeginT.o, crtend.o and crtn.o, in the same order and places;
//   - the archives of the driver's group, --start-group -lgcc -lgcc_eh -latomic_asneeded -lc --end-group, in its order:
//     libgcc.a and libgcc_eh.a, GCC's; libatomic_asneeded.a, which GCC 16 installs as a link to libatomic.a, where -L
//     finds it if GCC has it; and libc.a, where -L finds it: GCC's installation, then the library directories.
// Each path probed, found or absent, is recorded with what identifies a found file (st_dev, st_ino, st_size, st_mtim
// and st_ctim), so that the snapshot can tell with one stat per path whether it still describes the system
// (runtime_probes_current). Among them are the directory GCC's versions were chosen from, whose times change when a
// version's directory comes or goes, and each version's crtbeginS.o, found or not, so that one installed into a
// version's existing directory shows too.
//
// The import (runtime_import, runtime_import_static) reads every file once into the runtime's arena, which the atoms
// then point into, and fills a link of the runtime's own: the crt objects as units 0 to 4 (RuntimeObject), then each
// archive's members, in the archives' order (RuntimeArchive), as units after them (Runtime.member_unit on), each
// object's records a piece. libc.so.6 becomes an ImportLibrary: its exports with their default versions, its versions
// and the names it refers to. Then it binds each name the runtime refers to (runtime_bind): to a definition in the
// runtime's own objects (an index of them by name hash, as libc.so.6's exports have; an absolute symbol is one), to the
// linker for a name it defines (_GLOBAL_OFFSET_TABLE_, and for a static executable the bounds of its sections and
// tables, runtime_linker_names), to an export of libc.so.6, to nothing for a weak reference that neither defines, or
// else to the program (main): RuntimeBinding, by symbol.
//
// What binding's closure over the members needs (bind.c), from the global names (RuntimeNames): the member whose
// definition each name has; each piece's strong references to names (Runtime.references, a range per piece); and its
// COMDAT groups (RuntimeGroup, a range per piece, each with its atoms and its signature's number among the runtime's
// distinct signatures), whose first in link order the link keeps.
//
// main.c includes this file after import.c. Layout, in this order:
//   - API: RuntimeError, RuntimeObject, RuntimeProbe, RuntimeBinding, Runtime and the entry points' prototypes.
//   - Files: probing and reading.
//   - The search: GCC's installation, the crt objects and libc's script.
//   - Importing and binding.

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
} RuntimeError;

// What the runtime is for: the files GCC's driver links around a program's objects (see the orientation comment).
typedef enum RuntimeKind
{
    // A dynamic PIE: Scrt1.o, crti.o, crtbeginS.o, crtendS.o and crtn.o; libc.so.6 and libc_nonshared.a.
    RUNTIME_KIND_DYNAMIC,
    // A static executable: crt1.o, crti.o, crtbeginT.o, crtend.o and crtn.o; libgcc.a, libgcc_eh.a, libatomic.a and
    // libc.a.
    RUNTIME_KIND_STATIC,
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
    // An export of libc.so.6: RuntimeBinding.export, its index in Runtime.libc.exports.
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
    // Its export of libc.so.6 plus 1, or 0.
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
    RuntimeArchive archives[BUSTER_RUNTIME_ARCHIVES];
    uint32_t archive_count;
    ImportMember* members;
    ImportObject* member_objects;
    uint32_t member_count;
    // The first member's unit (RUNTIME_OBJECTS): the members follow it one per unit.
    uint32_t member_unit;
    ImportLibrary libc;
    // Scrt1.o's (or crt1.o's) .note.ABI-tag, which an executable repeats.
    String8 abi_note;
    // By symbol of the runtime's link.
    RuntimeBinding* bindings;
    // Each object's records: the crt objects' in RuntimeObject order, then each member's.
    RuntimePiece* pieces;
    // By piece (RUNTIME_OBJECTS + member_count of them, and one more for the end): where its strong references to
    // global names start in `references` (each an entry of global_names), and its COMDAT groups in `groups`. The groups'
    // atoms are in `group_atoms`, and their signatures are `group_signatures` distinct ones.
    uint32_t* reference_begin;
    uint32_t* references;
    uint32_t* group_begin;
    RuntimeGroup* groups;
    AtomId* group_atoms;
    uint32_t group_signatures;
    // The names binding looks a program's up in, and by symbol of the runtime's link its name's entry among them plus 1
    // (0 for a local or unnamed symbol).
    RuntimeNames global_names;
    uint32_t* symbol_names;
    // For a runtime loaded from a snapshot (snapshot.c), the file, mapped, whose columns it uses where they are; 0 for one
    // runtime_import made. A loaded runtime has what binding and an executable read: none of the files' bytes, the
    // objects' and members' ImportObjects, the archives' indices, or libc.so.6's export names, sizes, references and
    // index, which only the import uses. Its arenas are empty, and `names` is a view of the mapped names run, which
    // bind.c copies from.
    uint8_t* snapshot;
    uint64_t snapshot_bytes;
    // When it failed: the file that didn't import or couldn't be read, or the name that wasn't found, the importer's
    // result and errno.
    char failed_file[BUSTER_RUNTIME_PATH_BYTES];
    ImportResult import;
    int system_error;
};

// Finds a dynamic PIE's runtime's files and imports them, with arenas of its own; runtime_release frees them (or a
// snapshot's mapping, for a runtime loaded from one). On failure, the error says what went wrong and the runtime's
// fields where. runtime_import_static does the same for a static executable's runtime.
static __attribute__((unused)) RuntimeError runtime_import(Runtime* runtime);
static __attribute__((unused)) RuntimeError runtime_import_static(Runtime* runtime);
static __attribute__((unused)) void runtime_release(Runtime* runtime);
// Whether every path the search probed is as it found it: absent still, or the same file or directory, by st_dev, st_ino,
// st_size, st_mtim and st_ctim.
static __attribute__((unused)) bool runtime_probes_current(const Runtime* runtime);
// The entry of the runtime's global names (RuntimeNames) with these bytes, plus 1, or 0.
static __attribute__((unused)) uint32_t runtime_find_global_name(const RuntimeNames* names, const uint8_t* bytes, uint32_t length, uint64_t hash);
// The bytes of an entry of the runtime's global names.
static __attribute__((unused)) const uint8_t* runtime_global_name(const RuntimeNames* names, uint32_t entry);
// The two bits a name's hash sets in its word of the global names' filter: the hash's low six bits and the six above.
static __attribute__((unused)) uint64_t runtime_name_filter_bits(uint64_t hash);

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
    };
    RuntimeError result = runtime_find_gcc(runtime, runtime_gcc_bases, BUSTER_ARRAY_LENGTH(runtime_gcc_bases)) ? RUNTIME_ERROR_NONE : RUNTIME_ERROR_MISSING;
    if (result != RUNTIME_ERROR_NONE)
    {
        snprintf(runtime->failed_file, sizeof(runtime->failed_file), "crtbeginS.o in a GCC installation");
    }
    for (uint32_t i = 0; result == RUNTIME_ERROR_NONE && i < RUNTIME_OBJECTS; i += 1)
    {
        const bool gcc = i == RUNTIME_OBJECT_BEGIN || i == RUNTIME_OBJECT_END;
        runtime->object_paths[i] = runtime_find_file(runtime, objects[runtime->kind][i], gcc);
        result = runtime->object_paths[i].length != 0 ? result : RUNTIME_ERROR_MISSING;
    }
    if (result == RUNTIME_ERROR_NONE && runtime->kind == RUNTIME_KIND_DYNAMIC)
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
// no object defines it. For both kinds: the bounds of the init and fini arrays, which step 4's executables define, and
// the GOT's _GLOBAL_OFFSET_TABLE_, which GNU as names in an object with a GOT reference and the dynamic PIE (step 5)
// defines. For a static executable, besides, as GNU ld's and lld's define them there: the ELF header's address
// (__ehdr_start, through which libc.a's startup finds the program headers) and the image's start; the preinit array's
// bounds; the IRELATIVE relocations' (__rela_iplt_start and __rela_iplt_end, which libc.a's startup applies for its
// IFUNCs); and the ends of the text, the data and the image.
static const char* const runtime_linker_names[] = {
    "_GLOBAL_OFFSET_TABLE_",
    "__init_array_start",
    "__init_array_end",
    "__fini_array_start",
    "__fini_array_end",
    "__ehdr_start",
    "__executable_start",
    "__preinit_array_start",
    "__preinit_array_end",
    "__rela_iplt_start",
    "__rela_iplt_end",
    "_etext",
    "etext",
    "__etext",
    "_edata",
    "edata",
    "__bss_start",
    "_end",
    "end",
};

// How many of runtime_linker_names, from the first, a kind's linker defines.
static const uint32_t runtime_linker_name_counts[RUNTIME_KINDS] = {
    [RUNTIME_KIND_DYNAMIC] = 5,
    [RUNTIME_KIND_STATIC] = BUSTER_ARRAY_LENGTH(runtime_linker_names),
};

// Whether the name is one of the names the linker of a runtime of `kind` defines.
static bool runtime_linker_name(RuntimeKind kind, const uint8_t* bytes, uint32_t length)
{
    bool result = false;
    for (uint32_t i = 0; i < runtime_linker_name_counts[kind]; i += 1)
    {
        result = result || (strlen(runtime_linker_names[i]) == length && memcmp(runtime_linker_names[i], bytes, length) == 0);
    }
    return result;
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
            // A static executable's runtime has no shared library.
            const bool shared = definition == 0 && !linker && runtime->kind == RUNTIME_KIND_DYNAMIC;
            const uint32_t export = shared ? import_library_find(&runtime->libc, link->names.bytes[name], link->names.length[name], link->names.hash[name]) : 0;
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
    RuntimeNames* names = &runtime->global_names;
    Arena* arena = &runtime->tables;
    const uint32_t symbol_count = symbols->count;
    const uint32_t linker_count = runtime_linker_name_counts[runtime->kind];
    const uint64_t bound = (uint64_t)symbol_count + exports->count + references->count + linker_count;
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
    // The candidates: the objects' global names, libc's exports and references, and the names the linker defines, which
    // a program can refer to whether or not the runtime does.
    const uint32_t references_end = named_count + exports->count + references->count;
    const uint32_t count = references_end + linker_count;
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
    if (result == RUNTIME_ERROR_NONE)
    {
        for (uint32_t i = 0; i < linker_count; i += 1)
        {
            bytes[references_end + i] = (const uint8_t*)runtime_linker_names[i];
            lengths[references_end + i] = (uint32_t)strlen(runtime_linker_names[i]);
            hashes[references_end + i] = link_hash(bytes[references_end + i], lengths[references_end + i]);
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
    // bits at most and a name that isn't there passes for one at most about 1.6% of the time (libc's 2,651 names, in
    // 1,024 words, about 0.7%).
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
// a piece's after the last piece's.
static RuntimeError runtime_references(Runtime* runtime)
{
    const LinkSymbols* symbols = &runtime->link.symbols;
    const uint32_t piece_count = RUNTIME_OBJECTS + runtime->member_count;
    runtime->reference_begin = BUSTER_ARENA_PUSH_ARRAY(&runtime->tables, uint32_t, (uint64_t)piece_count + 1);
    runtime->references = BUSTER_ARENA_PUSH_ARRAY(&runtime->tables, uint32_t, (uint64_t)symbols->count + BUSTER_IMPORT_BATCH);
    RuntimeError result = runtime->reference_begin != 0 && runtime->references != 0 ? RUNTIME_ERROR_NONE : RUNTIME_ERROR_MEMORY;
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

// Imports the runtime of `kind` (see the orientation comment).
static RuntimeError runtime_import_kind(Runtime* runtime, RuntimeKind kind)
{
    RuntimeError result = runtime_begin(runtime) ? RUNTIME_ERROR_NONE : RUNTIME_ERROR_MEMORY;
    runtime->kind = kind;
    const bool dynamic = kind == RUNTIME_KIND_DYNAMIC;
    if (result == RUNTIME_ERROR_NONE)
    {
        result = runtime_search(runtime);
    }
    if (result == RUNTIME_ERROR_NONE && dynamic)
    {
        result = runtime_read_script(runtime);
    }
    // Read every file, then size the runtime's link for the objects and the archives' members.
    for (uint32_t i = 0; result == RUNTIME_ERROR_NONE && i < RUNTIME_OBJECTS; i += 1)
    {
        runtime->object_bytes[i] = runtime_read(runtime, runtime->object_paths[i], &result);
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
        result = runtime_count(runtime, runtime->object_paths[i], runtime->object_bytes[i], &capacity);
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
        runtime->pieces[i].begin = import_mark(&runtime->link, &runtime->files);
        const ImportResult imported =
            import_object(&runtime->link, runtime->object_bytes[i], i, BUSTER_LINK_SOURCE_RUNTIME_ARCHIVE, &runtime->files, &runtime->objects[i]);
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
