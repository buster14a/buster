// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Included by bind_test.c. The fake GCC tree makes search precedence independent of the host distribution; real
// imports cover its installed runtimes. --cross-check also compiles the original issue's shapes without -L, cold
// and with a cached runtime, through both driver paths and Clang, and checks the independently expected output.

static String8 bind_test_gcc_string(const char* text)
{
    return (String8){ .pointer = (char*)text, .length = strlen(text) };
}

static bool bind_test_gcc_same_file(const char* a, const char* b)
{
    struct stat first;
    struct stat second;
    return stat(a, &first) == 0 && stat(b, &second) == 0 && first.st_dev == second.st_dev && first.st_ino == second.st_ino;
}

static void bind_test_gcc_search(BindTestTally* tally, const Runtime* runtime, const char* root)
{
    static const char* const suffixes[] = {
        "/user1",
        "/user2",
        "/lib",
        "/lib/gcc",
        "/lib/gcc/x86_64-linux-gnu",
        "/lib/gcc/x86_64-linux-gnu/99",
        "/lib/gcc/x86_64-linux-gnu/lib64",
        "/lib64",
        "/x86_64-linux-gnu",
        "/x86_64-linux-gnu/lib",
        "/x86_64-linux-gnu/lib64",
    };
    char places[BUSTER_ARRAY_LENGTH(suffixes)][BUSTER_RUNTIME_PATH_BYTES];
    bool made = true;
    for (uint32_t d = 0; d < BUSTER_ARRAY_LENGTH(suffixes); d += 1)
    {
        const int length = snprintf(places[d], sizeof(places[d]), "%s%s", root, suffixes[d]);
        made = made && length > 0 && (uint64_t)length < sizeof(places[d]) && mkdir(places[d], 0700) == 0;
    }
    bind_test_expect(tally, made, "fake GCC installation created", 0);
    Runtime selected = *runtime;
    selected.gcc_directory = bind_test_gcc_string(places[5]);
    selected.gcc_versions = bind_test_gcc_string(places[4]);
    const String8 directories[] = { bind_test_gcc_string(places[0]), bind_test_gcc_string(places[1]) };
    // The independent expected order, including GCC's target and prefix lib64 directories.
    static const uint32_t order[] = { 0, 1, 5, 6, 10, 7, 9 };
    char files[BUSTER_ARRAY_LENGTH(order)][BUSTER_RUNTIME_PATH_BYTES];
    for (uint32_t f = 0; made && f < BUSTER_ARRAY_LENGTH(order); f += 1)
    {
        const int length = snprintf(files[f], sizeof(files[f]), "%s/libbuster_1119_order.so", places[order[f]]);
        made = length > 0 && (uint64_t)length < sizeof(files[f]) && bind_test_write_file(files[f], "", 0);
    }
    bind_test_expect(tally, made, "search candidates created", 0);
    const String8 name = bind_test_gcc_string("buster_1119_order");
    char path[BUSTER_RUNTIME_PATH_BYTES];
    char archive_path[BUSTER_RUNTIME_PATH_BYTES];
    snprintf(archive_path, sizeof(archive_path), "%s/libbuster_1119_order.a", places[0]);
    bool archive = false;
    bool ready = made && bind_test_write_file(archive_path, "!<arch>\n", 8);
    bool found = ready && runtime_libraries_find(&selected, name, directories, 2, path, &archive);
    bind_test_expect(tally, found && !archive && bind_test_gcc_same_file(path, files[0]), "same-directory shared library before archive", 0);
    ready = ready && unlink(files[0]) == 0;
    found = ready && runtime_libraries_find(&selected, name, directories, 2, path, &archive);
    bind_test_expect(tally, found && archive && bind_test_gcc_same_file(path, archive_path), "earlier -L archive before later shared libraries", 0);
    ready = ready && unlink(archive_path) == 0;
    for (uint32_t f = 1; ready && f < BUSTER_ARRAY_LENGTH(order); f += 1)
    {
        found = runtime_libraries_find(&selected, name, directories, 2, path, &archive);
        bind_test_expect(tally, found && !archive && bind_test_gcc_same_file(path, files[f]), "user, GCC and relative directories in order", f);
        ready = unlink(files[f]) == 0;
    }
    found = ready && runtime_libraries_find(&selected, name, directories, 2, path, &archive);
    bind_test_expect(tally, ready && !found && !archive, "missing name stays missing", 0);
    found = runtime_libraries_find(&selected, bind_test_gcc_string("m"), 0, 0, path, &archive);
    bind_test_expect(tally, found, "system library fallback retained", 0);
    selected.gcc_directory = (String8){ 0 };
    selected.gcc_versions = (String8){ 0 };
    found = runtime_libraries_find(&selected, name, 0, 0, path, &archive);
    bind_test_expect(tally, !found, "empty GCC installation is not a directory", 0);
    selected.gcc_directory = (String8){ .pointer = "x", .length = UINT64_MAX };
    selected.gcc_versions = selected.gcc_directory;
    found = runtime_libraries_find(&selected, (String8){ .pointer = "x", .length = UINT64_MAX }, 0, 0, path, &archive);
    bind_test_expect(tally, !found, "oversized slices are not truncated or read", 0);

    selected.gcc_directory = bind_test_gcc_string(places[5]);
    selected.gcc_versions = bind_test_gcc_string(places[4]);
    Arena arena = arena_reserve(1u << 20);
    const BindTestLibraryFile file = {
        .name = "buster_1119_only",
        .soname = "libbuster_1119_only.so.1",
        .export = "buster_gcc_marker",
    };
    ready = made && arena.base != 0 && bind_test_write_library(places[5], &file, &arena);
    bind_test_expect(tally, ready, "GCC-only ELF library created", 0);
    const String8 only = bind_test_gcc_string(file.name);
    RuntimeLibraries libraries = { 0 };
    RuntimeError error = ready ? runtime_libraries_import(&libraries, &selected, &only, 1, 0, 0) : RUNTIME_ERROR_FILE;
    bind_test_expect(tally, error == RUNTIME_ERROR_NONE && libraries.count == 1, "GCC-only library imported without -L", 0);
    runtime_libraries_release(&libraries);

    char script[BUSTER_RUNTIME_PATH_BYTES];
    snprintf(script, sizeof(script), "%s/libbuster_1119_script.so", places[5]);
    static const char body[] = "GROUP ( libbuster_1119_only.so -lgcc )\n";
    snprintf(archive_path, sizeof(archive_path), "%s/libgcc.a", places[5]);
    ready = ready && bind_test_write_file(script, body, sizeof(body) - 1) && symlink(runtime->archives[0].path.pointer, archive_path) == 0;
    bind_test_expect(tally, ready, "GCC-style relative shared library and -lgcc script created", 0);
    const String8 script_name = bind_test_gcc_string("buster_1119_script");
    libraries = (RuntimeLibraries){ 0 };
    error = ready ? runtime_libraries_import(&libraries, &selected, &script_name, 1, 0, 0) : RUNTIME_ERROR_FILE;
    bind_test_expect(tally, error == RUNTIME_ERROR_NONE && libraries.count == 1, "script reuses exactly the selected libgcc archive", 0);
    runtime_libraries_release(&libraries);

    snprintf(archive_path, sizeof(archive_path), "%s/libgcc.a", places[0]);
    ready = ready && bind_test_write_file(archive_path, "!<arch>\n", 8);
    libraries = (RuntimeLibraries){ 0 };
    error = ready ? runtime_libraries_import(&libraries, &selected, &script_name, 1, directories, 2) : RUNTIME_ERROR_FILE;
    bind_test_expect(tally, error == RUNTIME_ERROR_ARCHIVE, "-L override of script's libgcc is not silently discarded", 0);
    runtime_libraries_release(&libraries);
    unlink(archive_path);

    static const char nested[] = "GROUP ( libbuster_1119_script.so )\n";
    ready = ready && bind_test_write_file(script, nested, sizeof(nested) - 1);
    libraries = (RuntimeLibraries){ 0 };
    error = ready ? runtime_libraries_import(&libraries, &selected, &script_name, 1, 0, 0) : RUNTIME_ERROR_FILE;
    bind_test_expect(tally, error == RUNTIME_ERROR_IMPORT, "nested script refused without recursion", 0);
    runtime_libraries_release(&libraries);
    arena_release(&arena);
}

static void bind_test_gcc_installed(BindTestTally* tally, const Runtime* runtime)
{
    static const char* const names[] = { "atomic", "gcc_s", "stdc++", "quadmath", "gomp", "asan" };
    for (uint32_t n = 0; n < BUSTER_ARRAY_LENGTH(names); n += 1)
    {
        const String8 name = bind_test_gcc_string(names[n]);
        char path[BUSTER_RUNTIME_PATH_BYTES];
        bool archive = false;
        const bool found = runtime_libraries_find(runtime, name, 0, 0, path, &archive);
        if (found || n < 2)
        {
            RuntimeLibraries libraries = { 0 };
            const RuntimeError error = runtime_libraries_import(&libraries, runtime, &name, 1, 0, 0);
            if (error != RUNTIME_ERROR_NONE)
            {
                printf("  -l%s: error %u, %s\n", names[n], (unsigned)error, libraries.failed);
            }
            bind_test_expect(tally, found && error == RUNTIME_ERROR_NONE && libraries.count != 0, names[n], n);
            runtime_libraries_release(&libraries);
        }
        else
        {
            printf("  -l%s: optional development library not installed\n", names[n]);
        }
    }
}

static void bind_test_gcc_atomics(BindTestTally* tally, const char* root)
{
    static const char program[] =
        "#include <stdatomic.h>\n#include <stdio.h>\n"
        "struct three { char a[3]; }; struct big { long x[4]; };\n"
        "int main(void) {\n"
        "struct three initial = {{1, 2, 3}}; _Atomic struct three small; atomic_init(&small, initial);\n"
        "struct three v = atomic_load(&small); _Atomic struct big large; struct big init = {{1, 2, 3, 4}};\n"
        "atomic_store(&large, init); struct big r = atomic_load(&large);\n"
        "_Atomic __int128 q = 5; __int128 old = atomic_fetch_add(&q, 3);\n"
        "_Atomic long double ld = 1.5L; long double l2 = atomic_load(&ld);\n"
        "printf(\"%d %ld %d %d %g\\n\", v.a[2], r.x[3], (int)old, (int)atomic_load(&q), (double)l2); return 0; }\n";
    char source[BUSTER_RUNTIME_PATH_BYTES];
    char output[BUSTER_RUNTIME_PATH_BYTES];
    char errors[BUSTER_RUNTIME_PATH_BYTES];
    char executable[BUSTER_RUNTIME_PATH_BYTES];
    char compiler[BUSTER_RUNTIME_PATH_BYTES];
    char cache[BUSTER_RUNTIME_PATH_BYTES];
    char cache_directory[BUSTER_RUNTIME_PATH_BYTES];
    snprintf(source, sizeof(source), "%s/atomic.c", root);
    snprintf(output, sizeof(output), "%s/stdout", root);
    snprintf(errors, sizeof(errors), "%s/stderr", root);
    snprintf(executable, sizeof(executable), "%s/atomic", root);
    snprintf(cache, sizeof(cache), "XDG_CACHE_HOME=%s/cache", root);
    snprintf(cache_directory, sizeof(cache_directory), "%s/cache", root);
    const ssize_t length = readlink("/proc/self/exe", compiler, sizeof(compiler) - 1);
    const bool ready = length > 0 && (uint64_t)length < sizeof(compiler) - 1 && mkdir(cache_directory, 0700) == 0 &&
                       bind_test_write_file(source, program, sizeof(program) - 1);
    if (ready)
    {
        compiler[length] = 0;
    }
    bind_test_expect(tally, ready, "atomic driver fixture prepared", 0);
    Arena arena = arena_reserve(1u << 20);
    for (uint32_t run = 0; ready && run < 4; run += 1)
    {
        // Clang, then Buster with a new cache, the same cache and its scalar path. No -L on any command line.
        char* arguments[16] = { 0 };
        uint32_t count = 0;
        arguments[count++] = "env";
        arguments[count++] = cache;
        arguments[count++] = run == 0 ? "clang" : compiler;
        if (run == 0)
        {
            arguments[count++] = "-O0";
        }
        if (run == 3)
        {
            arguments[count++] = "--scalar";
        }
        arguments[count++] = "-std=gnu17";
        arguments[count++] = "-march=x86-64";
        arguments[count++] = "-o";
        arguments[count++] = executable;
        arguments[count++] = source;
        arguments[count++] = "-latomic";
        arguments[count++] = "-lgcc_s";
        const int status = object_test_run(arguments, output, errors);
        if (status != 0)
        {
            const String8 diagnostics = object_test_read_file(&arena, errors);
            printf("  atomic compile %u exited %d: %.*s\n", run, status, (int)diagnostics.length, diagnostics.pointer != 0 ? diagnostics.pointer : "");
        }
        bind_test_expect(tally, status == 0, "atomic fixture links without -L", run);
        if (run == 1)
        {
            snprintf(cache_directory, sizeof(cache_directory), "%s/cache/buster", root);
            DIR* cache_files = opendir(cache_directory);
            bool saved = false;
            for (struct dirent* entry = cache_files != 0 ? readdir(cache_files) : 0; entry != 0; entry = readdir(cache_files))
            {
                saved = saved || entry->d_name[0] != '.';
            }
            if (cache_files != 0)
            {
                closedir(cache_files);
            }
            bind_test_expect(tally, saved, "cold link publishes its runtime snapshot", run);
        }
        char* execute[] = { executable, 0 };
        const int ran = status == 0 ? object_test_run(execute, output, errors) : -1;
        const String8 printed = ran == 0 ? object_test_read_file(&arena, output) : (String8){ 0 };
        static const char expected[] = "3 4 5 8 1.5\n";
        bind_test_expect(tally, ran == 0 && printed.length == sizeof(expected) - 1 && memcmp(printed.pointer, expected, sizeof(expected) - 1) == 0,
                         "atomic fixture returns independently expected values", run);
    }
    if (ready)
    {
        char* arguments[] = { "env", cache, compiler, "-std=gnu17", "-march=x86-64", "-o", executable, source, 0 };
        const int status = object_test_run(arguments, output, errors);
        const String8 diagnostics = object_test_read_file(&arena, errors);
        const bool named = diagnostics.pointer != 0 && memmem(diagnostics.pointer, diagnostics.length, "__atomic_", 9) != 0;
        bind_test_expect(tally, status == 1 && named, "libatomic remains explicit, not automatically linked", 0);
    }
    arena_release(&arena);
}

static bool bind_test_gcc_libraries(const Runtime* runtime, const char* directory, bool cross_check)
{
    BindTestTally tally = { .check = "GCC library search" };
    char root[BUSTER_RUNTIME_PATH_BYTES];
    const int length = snprintf(root, sizeof(root), "%s/gcc-libraries-XXXXXX", directory);
    const bool created = length > 0 && (uint64_t)length < sizeof(root) && mkdtemp(root) != 0;
    bind_test_expect(&tally, created, "private fixture directory created", 0);
    if (created)
    {
        bind_test_gcc_search(&tally, runtime, root);
        bind_test_gcc_installed(&tally, runtime);
        if (cross_check)
        {
            bind_test_gcc_atomics(&tally, root);
        }
        char* remove[] = { "rm", "-rf", "--", root, 0 };
        bind_test_expect(&tally, object_test_run(remove, "/dev/null", "/dev/null") == 0, "private fixtures removed", 0);
    }
    return bind_test_report(&tally);
}
