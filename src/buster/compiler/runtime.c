// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The C runtime a dynamic PIE links (docs/object-writer-and-linker.md, "The runtime snapshot"): finding its files as
// GCC's driver does, and importing them with import.c.
//
// The files, for `cc main.c -o main` on this system:
//   - GCC's installation: <base>/<triple>/<version> under /usr/lib/gcc or /usr/lib64/gcc, for the triples Linux
//     distributions use, the highest version that has crtbeginS.o;
//   - the crt objects, in the order GCC's driver puts them around the program: Scrt1.o, crti.o and crtbeginS.o before
//     it, crtendS.o and crtn.o after it. Scrt1.o, crti.o and crtn.o are glibc's, found in the library directories
//     (Debian's multiarch ones, /usr/lib64 and /lib64, then /usr/lib and /lib), crtbeginS.o and crtendS.o GCC's;
//   - libc.so, found in the same directories: a linker script naming libc.so.6, libc_nonshared.a and, as needed, the
//     dynamic loader.
// Each path probed, found or absent, is recorded with what identifies a found file (st_dev, st_ino, st_size, st_mtim
// and st_ctim), so that the snapshot can tell with one stat per path whether it still describes the system
// (runtime_probes_current). Among them are the directory GCC's versions were chosen from, whose times change when a
// version's directory comes or goes, and each version's crtbeginS.o, found or not, so that one installed into a
// version's existing directory shows too.
//
// The import (runtime_import) reads every file once into the runtime's arena, which the atoms then point into, and
// fills a link of the runtime's own: the crt objects as units 0 to 4 (RuntimeObject), then libc_nonshared.a's members
// as units after them. libc.so.6 becomes an ImportLibrary: its exports with their default versions, its versions and
// the names it refers to. Then it binds each name the runtime refers to (runtime_bind): to a definition in the
// runtime's own objects (an index of them by name hash, as libc.so.6's exports have), to an export of libc.so.6, to
// nothing for a weak reference that neither defines, or else to the program (main): RuntimeBinding, by symbol.
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

#define BUSTER_RUNTIME_PATH_BYTES 4096

typedef struct Runtime Runtime;
struct Runtime
{
    // The runtime's link, its arenas, and the files' bytes, which its atoms point into.
    Link link;
    Arena tables;
    Arena names;
    Arena files;
    // GCC's installation, and the directory its version was chosen from.
    String8 gcc_directory;
    String8 gcc_versions;
    // The files found: the crt objects, libc.so's script and what it names.
    String8 object_paths[RUNTIME_OBJECTS];
    String8 libc_script;
    String8 libc_path;
    String8 nonshared_path;
    String8 loader_path;
    // Every path probed, in the order the search probed them, and whether a probe found no room among them.
    RuntimeProbe* probes;
    uint32_t probe_count;
    uint32_t probe_capacity;
    bool probes_full;
    // The imported files: the crt objects (and their bytes, which their atoms point into), libc_nonshared.a's and
    // libc.so.6's bytes, the archive and each of its members.
    String8 object_bytes[RUNTIME_OBJECTS];
    ImportObject objects[RUNTIME_OBJECTS];
    String8 nonshared_bytes;
    String8 libc_bytes;
    ImportArchive nonshared;
    ImportObject* member_objects;
    // The first unit of libc_nonshared.a's members, which follow it one per unit.
    uint32_t nonshared_unit;
    ImportLibrary libc;
    // Scrt1.o's .note.ABI-tag, which an executable repeats.
    String8 abi_note;
    // By symbol of the runtime's link.
    RuntimeBinding* bindings;
    // When it failed: the file that didn't import or couldn't be read, or the name that wasn't found, the importer's
    // result and errno.
    char failed_file[BUSTER_RUNTIME_PATH_BYTES];
    ImportResult import;
    int system_error;
};

// Finds the runtime's files and imports them, with arenas of its own; runtime_release frees them. On failure, the
// error says what went wrong and the runtime's fields where.
static __attribute__((unused)) RuntimeError runtime_import(Runtime* runtime);
static __attribute__((unused)) void runtime_release(Runtime* runtime);
// Whether every path the search probed is as it found it: absent still, or the same file or directory, by st_dev, st_ino,
// st_size, st_mtim and st_ctim.
static __attribute__((unused)) bool runtime_probes_current(const Runtime* runtime);

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

static RuntimeError runtime_search(Runtime* runtime)
{
    static const char* const objects[RUNTIME_OBJECTS] = {
        [RUNTIME_OBJECT_START] = "Scrt1.o",
        [RUNTIME_OBJECT_INIT_PROLOGUE] = "crti.o",
        [RUNTIME_OBJECT_BEGIN] = "crtbeginS.o",
        [RUNTIME_OBJECT_END] = "crtendS.o",
        [RUNTIME_OBJECT_INIT_EPILOGUE] = "crtn.o",
    };
    RuntimeError result = runtime_find_gcc(runtime, runtime_gcc_bases, BUSTER_ARRAY_LENGTH(runtime_gcc_bases)) ? RUNTIME_ERROR_NONE : RUNTIME_ERROR_MISSING;
    if (result != RUNTIME_ERROR_NONE)
    {
        snprintf(runtime->failed_file, sizeof(runtime->failed_file), "crtbeginS.o in a GCC installation");
    }
    for (uint32_t i = 0; result == RUNTIME_ERROR_NONE && i < RUNTIME_OBJECTS; i += 1)
    {
        const bool gcc = i == RUNTIME_OBJECT_BEGIN || i == RUNTIME_OBJECT_END;
        runtime->object_paths[i] = runtime_find_file(runtime, objects[i], gcc);
        result = runtime->object_paths[i].length != 0 ? result : RUNTIME_ERROR_MISSING;
    }
    if (result == RUNTIME_ERROR_NONE)
    {
        runtime->libc_script = runtime_find_file(runtime, "libc.so", false);
        result = runtime->libc_script.length != 0 ? result : RUNTIME_ERROR_MISSING;
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
            runtime->nonshared_path = runtime_copy_path(runtime, path);
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
    if (result == RUNTIME_ERROR_NONE && (runtime->libc_path.length == 0 || runtime->nonshared_path.length == 0))
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
// symbols at a time (a compress of the ids whose binding isn't local, with an atom and a name) and indexed by their
// names' hashes (import_index_build), rather than searched for each reference.
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
        const uint32_t kept = _mm512_test_epi64_mask(flags, _mm512_set1_epi64(BUSTER_LINK_SYMBOL_BINDING)) & _mm512_test_epi64_mask(atoms, atoms) &
                              _mm512_test_epi64_mask(names, names) & loaded;
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
        if (symbols->atom[symbol] == 0 && name != 0)
        {
            const SymbolId definition = runtime_definition(link, &index, definitions, name);
            const uint32_t export = definition == 0 ? import_library_find(&runtime->libc, link->names.bytes[name], link->names.length[name], link->names.hash[name]) : 0;
            const bool weak = (symbols->flags[symbol] & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_WEAK;
            binding.kind = definition != 0 ? RUNTIME_BINDING_RUNTIME : export != 0 ? RUNTIME_BINDING_SHARED : weak ? RUNTIME_BINDING_NOWHERE : RUNTIME_BINDING_PROGRAM;
            binding.symbol = definition;
            binding.export = export != 0 ? export - 1 : 0;
        }
        runtime->bindings[symbol] = binding;
    }
    scratch_end(scratch);
    return result;
}

static RuntimeError runtime_import(Runtime* runtime)
{
    RuntimeError result = runtime_begin(runtime) ? RUNTIME_ERROR_NONE : RUNTIME_ERROR_MEMORY;
    if (result == RUNTIME_ERROR_NONE)
    {
        result = runtime_search(runtime);
    }
    if (result == RUNTIME_ERROR_NONE)
    {
        result = runtime_read_script(runtime);
    }
    // Read every file, then size the runtime's link for the objects and the archive's members.
    for (uint32_t i = 0; result == RUNTIME_ERROR_NONE && i < RUNTIME_OBJECTS; i += 1)
    {
        runtime->object_bytes[i] = runtime_read(runtime, runtime->object_paths[i], &result);
    }
    if (result == RUNTIME_ERROR_NONE)
    {
        runtime->nonshared_bytes = runtime_read(runtime, runtime->nonshared_path, &result);
    }
    if (result == RUNTIME_ERROR_NONE)
    {
        runtime->libc_bytes = runtime_read(runtime, runtime->libc_path, &result);
    }
    if (result == RUNTIME_ERROR_NONE)
    {
        const ImportResult archive = import_archive(&runtime->nonshared, runtime->nonshared_bytes, &runtime->files);
        result = archive.error == IMPORT_ERROR_NONE ? result : runtime_fail_import(runtime, runtime->nonshared_path, archive);
    }
    if (result == RUNTIME_ERROR_NONE)
    {
        const ImportResult library = import_library(&runtime->libc, runtime->libc_bytes, &runtime->files);
        result = library.error == IMPORT_ERROR_NONE ? result : runtime_fail_import(runtime, runtime->libc_path, library);
    }
    LinkCapacity capacity = { 0 };
    for (uint32_t i = 0; result == RUNTIME_ERROR_NONE && i < RUNTIME_OBJECTS; i += 1)
    {
        result = runtime_count(runtime, runtime->object_paths[i], runtime->object_bytes[i], &capacity);
    }
    for (uint32_t m = 0; result == RUNTIME_ERROR_NONE && m < runtime->nonshared.member_count; m += 1)
    {
        result = runtime_count(runtime, runtime->nonshared_path, runtime->nonshared.members[m].bytes, &capacity);
    }
    if (result == RUNTIME_ERROR_NONE && !link_initialize(&runtime->link, &runtime->tables, &runtime->names, capacity))
    {
        result = RUNTIME_ERROR_MEMORY;
    }
    // The crt objects as units 0 to 4, then each member of libc_nonshared.a as a unit of its own.
    for (uint32_t i = 0; result == RUNTIME_ERROR_NONE && i < RUNTIME_OBJECTS; i += 1)
    {
        const ImportResult imported =
            import_object(&runtime->link, runtime->object_bytes[i], i, BUSTER_LINK_SOURCE_RUNTIME_ARCHIVE, &runtime->files, &runtime->objects[i]);
        result = imported.error == IMPORT_ERROR_NONE ? result : runtime_fail_import(runtime, runtime->object_paths[i], imported);
    }
    runtime->nonshared_unit = RUNTIME_OBJECTS;
    if (result == RUNTIME_ERROR_NONE)
    {
        runtime->member_objects = BUSTER_ARENA_PUSH_ARRAY(&runtime->files, ImportObject, runtime->nonshared.member_count);
        result = runtime->member_objects != 0 ? result : RUNTIME_ERROR_MEMORY;
    }
    for (uint32_t m = 0; result == RUNTIME_ERROR_NONE && m < runtime->nonshared.member_count; m += 1)
    {
        const ImportResult imported = import_object(&runtime->link, runtime->nonshared.members[m].bytes, runtime->nonshared_unit + m, BUSTER_LINK_SOURCE_RUNTIME_ARCHIVE,
                                                    &runtime->files, &runtime->member_objects[m]);
        result = imported.error == IMPORT_ERROR_NONE ? result : runtime_fail_import(runtime, runtime->nonshared_path, imported);
    }
    if (result == RUNTIME_ERROR_NONE)
    {
        runtime->abi_note = runtime_section(runtime->object_bytes[RUNTIME_OBJECT_START], ".note.ABI-tag");
        result = runtime_bind(runtime);
    }
    result = result == RUNTIME_ERROR_NONE && runtime->probes_full ? RUNTIME_ERROR_MEMORY : result;
    return result;
}

static void runtime_release(Runtime* runtime)
{
    arena_release(&runtime->tables);
    arena_release(&runtime->names);
    arena_release(&runtime->files);
}
