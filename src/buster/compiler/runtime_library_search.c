// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The direct driver's ordered -l search, included by runtime.c after its library readers. Reuse the GCC installation
// that the runtime selected (and snapshots retain); never spawn a compiler or rescan GCC's versions per library.
// This is filesystem discovery, not a symbol/byte scan: keep the existing first-file-wins probing order. The native
// GCC-relative lib64 directories cover version-specific and target-specific runtime installations, before the
// existing system fallbacks. This is not a general cross-toolchain/sysroot search.
// Scripts may name shared libraries by absolute path, relative path or -l. Other scripts and archives remain refused,
// except the already-imported libgcc.a, identified by device/inode rather than its spelling: Debian's libgcc_s.so
// GROUP names it after libgcc_s.so.1. A different archive selected by -L must not silently disappear.

static bool runtime_libraries_in_directory(String8 directory, String8 name, bool exact, char* path, bool* archive)
{
    const bool valid = directory.pointer != 0 && directory.length != 0 && directory.length < BUSTER_RUNTIME_PATH_BYTES &&
                       name.pointer != 0 && name.length != 0 && name.length < BUSTER_RUNTIME_PATH_BYTES;
    bool found = false;
    *archive = false;
    for (uint32_t kind = 0; valid && !found && kind < (exact ? 1u : 2u); kind += 1)
    {
        const int length = snprintf(path, BUSTER_RUNTIME_PATH_BYTES, "%.*s/%s%.*s%s", (int)directory.length, directory.pointer,
                                    exact ? "" : "lib", (int)name.length, name.pointer, exact ? "" : kind == 0 ? ".so" : ".a");
        struct stat status;
        found = length > 0 && length < BUSTER_RUNTIME_PATH_BYTES && stat(path, &status) == 0 && S_ISREG(status.st_mode);
        const bool named_archive = exact && name.length > 2 && memcmp(name.pointer + name.length - 2, ".a", 2) == 0;
        *archive = found && (exact ? named_archive : kind == 1);
    }
    return found;
}

// With exact, search the filename unchanged (a script's libgcc_s.so.1), otherwise libNAME.so then libNAME.a in EACH
// directory. In particular, a user's archive wins over a shared library in a later user, GCC or system directory.
static bool runtime_libraries_search(const Runtime* runtime, String8 name, bool exact, const String8* directories, uint32_t directory_count,
                                     char* path, bool* archive)
{
    const String8 gcc = runtime->gcc_directory;
    const String8 versions = runtime->gcc_versions;
    const bool has_gcc = gcc.pointer != 0 && gcc.length != 0 && gcc.length < BUSTER_RUNTIME_PATH_BYTES;
    bool found = false;
    *archive = false;
    for (uint32_t d = 0; !found && d < directory_count; d += 1)
    {
        found = runtime_libraries_in_directory(directories[d], name, exact, path, archive);
    }
    if (!found && has_gcc)
    {
        found = runtime_libraries_in_directory(gcc, name, exact, path, archive);
    }
    String8 triple = { 0 };
    if (has_gcc && versions.pointer != 0 && versions.length != 0 && versions.length < BUSTER_RUNTIME_PATH_BYTES)
    {
        uint64_t begin = versions.length;
        while (begin != 0 && versions.pointer[begin - 1] != '/')
        {
            begin -= 1;
        }
        triple = (String8){ .pointer = versions.pointer + begin, .length = versions.length - begin };
    }
    static const struct
    {
        const char* prefix;
        bool triple;
        const char* suffix;
    } relatives[] = {
        { .prefix = "/../lib64", .triple = false, .suffix = "" },
        { .prefix = "/../../../../", .triple = true, .suffix = "/lib/../lib64" },
        { .prefix = "/../../../../lib64", .triple = false, .suffix = "" },
        { .prefix = "/../../../../", .triple = true, .suffix = "/lib" },
    };
    for (uint32_t d = 0; has_gcc && !found && d < BUSTER_ARRAY_LENGTH(relatives); d += 1)
    {
        char buffer[BUSTER_RUNTIME_PATH_BYTES];
        const bool target = relatives[d].triple;
        const int length = !target || triple.length != 0 ?
            snprintf(buffer, sizeof(buffer), "%.*s%s%.*s%s", (int)gcc.length, gcc.pointer, relatives[d].prefix,
                     target ? (int)triple.length : 0, target ? triple.pointer : "", relatives[d].suffix) : -1;
        if (length > 0 && (uint64_t)length < sizeof(buffer))
        {
            const String8 directory = { .pointer = buffer, .length = (uint64_t)length };
            found = runtime_libraries_in_directory(directory, name, exact, path, archive);
        }
    }
    for (uint32_t d = 0; !found && d < BUSTER_ARRAY_LENGTH(runtime_library_directories); d += 1)
    {
        const String8 directory = { .pointer = (char*)runtime_library_directories[d], .length = strlen(runtime_library_directories[d]) };
        found = runtime_libraries_in_directory(directory, name, exact, path, archive);
    }
    return found;
}

static bool runtime_libraries_find(const Runtime* runtime, String8 name, const String8* directories, uint32_t directory_count, char* path, bool* archive)
{
    return runtime_libraries_search(runtime, name, false, directories, directory_count, path, archive);
}

// GNU ld's relative INPUT rule: the script's directory, the current directory, then the library search. A -l input
// instead uses the same ordered search as a command-line -l. The caller retains ownership of every input slice.
static RuntimeError runtime_libraries_script_path(const Runtime* runtime, const char* script, const ImportScriptInput* input,
                                                  const String8* directories, uint32_t directory_count, char* path, bool* archive)
{
    const String8 name = input->name;
    RuntimeError result = name.pointer != 0 && name.length != 0 && name.length < BUSTER_RUNTIME_PATH_BYTES ? RUNTIME_ERROR_NONE : RUNTIME_ERROR_IMPORT;
    bool found = false;
    *archive = false;
    path[0] = 0;
    if (result == RUNTIME_ERROR_NONE && input->library)
    {
        found = runtime_libraries_find(runtime, name, directories, directory_count, path, archive);
    }
    else if (result == RUNTIME_ERROR_NONE && name.pointer[0] == '/')
    {
        memcpy(path, name.pointer, name.length);
        path[name.length] = 0;
        *archive = name.length > 2 && memcmp(name.pointer + name.length - 2, ".a", 2) == 0;
        found = true;
    }
    else if (result == RUNTIME_ERROR_NONE)
    {
        const char* slash = strrchr(script, '/');
        const String8 directory = slash != 0 ? (String8){ .pointer = (char*)script, .length = slash == script ? 1 : (uint64_t)(slash - script) }
                                              : (String8){ .pointer = ".", .length = 1 };
        found = runtime_libraries_in_directory(directory, name, true, path, archive);
        found = found || runtime_libraries_in_directory((String8){ .pointer = ".", .length = 1 }, name, true, path, archive);
        found = found || runtime_libraries_search(runtime, name, true, directories, directory_count, path, archive);
    }
    result = result == RUNTIME_ERROR_NONE && !found ? RUNTIME_ERROR_MISSING : result;
    return result;
}

static bool runtime_libraries_selected_libgcc(const Runtime* runtime, const char* path)
{
    struct stat selected;
    struct stat input;
    const String8 gcc = runtime->archive_count != 0 ? runtime->archives[0].path : (String8){ 0 };
    const bool same = gcc.pointer != 0 && gcc.length != 0 && stat(gcc.pointer, &selected) == 0 && stat(path, &input) == 0 &&
                      S_ISREG(selected.st_mode) && S_ISREG(input.st_mode) && selected.st_dev == input.st_dev && selected.st_ino == input.st_ino;
    return same;
}

static RuntimeError runtime_libraries_script(RuntimeLibraries* libraries, const Runtime* runtime, const char* path, String8 bytes,
                                             const String8* directories, uint32_t directory_count, uint16_t* base)
{
    ImportScript script = { 0 };
    libraries->import = import_script(&script, bytes, &libraries->arena);
    RuntimeError result = libraries->import.error == IMPORT_ERROR_NONE ? RUNTIME_ERROR_NONE : RUNTIME_ERROR_IMPORT;
    if (result != RUNTIME_ERROR_NONE)
    {
        snprintf(libraries->failed, sizeof(libraries->failed), "%s", path);
    }
    const uint32_t count = script.input_count;
    const ImportScriptInput* const inputs = script.inputs;
    for (uint32_t i = 0; result == RUNTIME_ERROR_NONE && i < count; i += 1)
    {
        const ImportScriptInput input = inputs[i];
        char input_path[BUSTER_RUNTIME_PATH_BYTES];
        bool archive = false;
        result = runtime_libraries_script_path(runtime, path, &input, directories, directory_count, input_path, &archive);
        const bool provided = result == RUNTIME_ERROR_NONE && archive && runtime_libraries_selected_libgcc(runtime, input_path);
        result = result == RUNTIME_ERROR_NONE && archive && !provided ? RUNTIME_ERROR_ARCHIVE : result;
        if (result != RUNTIME_ERROR_NONE)
        {
            const int length = input.name.length < BUSTER_RUNTIME_PATH_BYTES ? (int)input.name.length : 0;
            snprintf(libraries->failed, sizeof(libraries->failed), "%s%.*s, which %s names", input.library ? "-l" : "", length,
                     input.name.pointer != 0 ? input.name.pointer : "", path);
        }
        if (result == RUNTIME_ERROR_NONE && !provided)
        {
            const String8 input_bytes = runtime_libraries_read(libraries, input_path, &result);
            if (result == RUNTIME_ERROR_NONE && !runtime_libraries_elf(input_bytes))
            {
                result = RUNTIME_ERROR_IMPORT;
                snprintf(libraries->failed, sizeof(libraries->failed), "%s, a script that %s names", input_path, path);
            }
            result = result == RUNTIME_ERROR_NONE ? runtime_libraries_add(libraries, input_path, input.library || input.name.pointer[0] != '/', input_bytes, base) : result;
        }
    }
    return result;
}
