// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// test-foreign's check of the shared libraries' versions (#1590, ForeignCommand.library_versions, "Shared libraries'
// versions" in foreign.c), on the resolution alone, by both paths and on 1, 2, 3 and 8 lanes, which must decide alike.
// cc builds the libraries with version scripts and without the C runtime: libA, whose foo is foo@V1 of libB.so (built
// against a libB of foo@@V1); libBs of foo@V1 alone (not its name's default) and of foo@@V2 alone; a libC of foo@@V1;
// and a library of another name, copied 40 times. Each line is fv_main.o, which calls libA's a_func, then the libraries
// under --as-needed, libA first and libB last:
//   - libA, libB of foo@V1: linked, libA needed, libB not (libA's DT_NEEDED names it, and the loader loads it);
//   - libA, libC, libB of foo@V1: linked, libC needed too, for libA's foo@V1, which binds to libC's;
//   - libA, the 40 copies, libC, libB of foo@V1: the same, libC the 42nd library on the line (the direct path takes 32);
//   - libA, libB of foo@@V2: refused, libA's foo@V1, which no library exports (FOREIGN_ERROR_LIBRARY_UNDEFINED);
//   - libA, libC, libB of foo@@V2: refused, libA's need of V1 of libB.so, which that libB doesn't define
//     (FOREIGN_ERROR_LIBRARY_VERSION);
//   - the last again without library_versions: linked, libC not needed, as link-foreign links it (GNU ld and lld too);
//   - libA alone: linked, its references unchecked, since its DT_NEEDED names a libB.so the link doesn't have.

#define BUSTER_FOREIGN_VERSIONS_TEST_COPIES 40

// Builds the fixtures in `directory`: false, with the tally told, if one doesn't build.
static bool foreign_versions_test_build(const char* directory, ForeignTestTally* tally)
{
    // A source, a version script ("" for none), a soname ("" for none), what to link against ("" for nothing) and the
    // output, each under the directory.
    static const char* const fixtures[][5] = {
        { "int foo(void) { return 1; }\n", "V1 { global: foo; local: *; };\n", "libB.so", "", "fv_old/libB.so" },
        { "int foo_v1(void) { return 11; }\n__asm__(\".symver foo_v1,foo@V1\");\n", "V1 { global: foo; local: *; };\n", "libB.so", "", "fv_v1only/libB.so" },
        { "int foo(void) { return 2; }\n", "V2 { global: foo; local: *; };\n", "libB.so", "", "fv_v2only/libB.so" },
        { "int foo(void) { return 11; }\n", "V1 { global: foo; local: *; };\n", "libC.so", "", "fv_c/libC.so" },
        { "int foo(void);\nint a_func(void) { return foo(); }\n", "", "libA.so", "fv_old/libB.so", "libfv_a.so" },
        { "int fv_filler(void) { return 3; }\n", "", "", "", "fv_filler.so" },
    };
    static const char* const directories[] = { "fv_old", "fv_v1only", "fv_v2only", "fv_c" };
    ForeignTestPath path;
    ForeignTestPath output;
    ForeignTestPath errors;
    snprintf(output, sizeof(output), "%s/fv_build.out", directory);
    snprintf(errors, sizeof(errors), "%s/fv_build.err", directory);
    bool result = true;
    for (uint32_t d = 0; d < BUSTER_ARRAY_LENGTH(directories); d += 1)
    {
        snprintf(path, sizeof(path), "%s/%s", directory, directories[d]);
        result = mkdir(path, 0755) == 0 && result;
    }
    for (uint32_t f = 0; result && f < BUSTER_ARRAY_LENGTH(fixtures); f += 1)
    {
        ForeignTestPath source;
        ForeignTestPath script;
        ForeignTestPath library;
        ForeignTestPath soname;
        ForeignTestPath against;
        snprintf(source, sizeof(source), "%s/fv_%u.c", directory, f);
        snprintf(script, sizeof(script), "%s/fv_%u.map", directory, f);
        snprintf(library, sizeof(library), "%s/%s", directory, fixtures[f][4]);
        snprintf(soname, sizeof(soname), "-Wl,-soname,%s", fixtures[f][2]);
        snprintf(against, sizeof(against), "%s/%s", directory, fixtures[f][3]);
        FILE* file = fopen(source, "w");
        bool written = file != 0 && fputs(fixtures[f][0], file) >= 0;
        written = file != 0 && fclose(file) == 0 && written;
        const bool scripted = fixtures[f][1][0] != 0;
        if (scripted)
        {
            file = fopen(script, "w");
            written = file != 0 && fputs(fixtures[f][1], file) >= 0 && written;
            written = file != 0 && fclose(file) == 0 && written;
        }
        snprintf(path, sizeof(path), "-Wl,--version-script=%s", script);
        char* arguments[16];
        uint32_t count = 0;
        arguments[count++] = (char*)"cc";
        arguments[count++] = (char*)"-shared";
        arguments[count++] = (char*)"-fPIC";
        arguments[count++] = (char*)"-nostdlib";
        arguments[count++] = (char*)"-o";
        arguments[count++] = library;
        arguments[count++] = source;
        if (fixtures[f][2][0] != 0)
        {
            arguments[count++] = soname;
        }
        if (scripted)
        {
            arguments[count++] = path;
        }
        if (fixtures[f][3][0] != 0)
        {
            arguments[count++] = against;
        }
        arguments[count] = 0;
        const bool built = written && object_test_run(arguments, output, errors) == 0;
        foreign_test_expect(tally, built, "build a versioned library fixture", library);
        result = built && result;
    }
    for (uint32_t c = 0; result && c < BUSTER_FOREIGN_VERSIONS_TEST_COPIES; c += 1)
    {
        ForeignTestPath from;
        snprintf(from, sizeof(from), "%s/fv_filler.so", directory);
        snprintf(path, sizeof(path), "%s/fv_filler_%u.so", directory, c);
        char* arguments[] = { (char*)"cp", from, path, 0 };
        result = object_test_run(arguments, output, errors) == 0;
        foreign_test_expect(tally, result, "copy the library of another name", path);
    }
    ForeignTestPath source;
    ForeignTestPath object;
    snprintf(source, sizeof(source), "%s/fv_main.c", directory);
    snprintf(object, sizeof(object), "%s/fv_main.o", directory);
    FILE* file = result ? fopen(source, "w") : 0;
    bool written = file != 0 && fputs("int a_func(void);\nint fv_main(void) { return a_func(); }\n", file) >= 0;
    written = file != 0 && fclose(file) == 0 && written;
    char* arguments[] = { (char*)"cc", (char*)"-c", (char*)"-fPIC", source, (char*)"-o", object, 0 };
    const bool compiled = written && object_test_run(arguments, output, errors) == 0;
    foreign_test_expect(tally, !result || compiled, "compile the program's object", source);
    return result && compiled;
}

// What a resolution decided that the cases check: its failure, and the libraries needed, a character each ('1' or
// '0'), from the first.
typedef struct ForeignVersionsTestOutcome ForeignVersionsTestOutcome;
struct ForeignVersionsTestOutcome
{
    ForeignError error;
    uint32_t library;
    uint32_t provider;
    char reference[64];
    char needed[BUSTER_FOREIGN_VERSIONS_TEST_COPIES + 8];
};

static ForeignVersionsTestOutcome foreign_versions_test_outcome(const Foreign* foreign)
{
    ForeignVersionsTestOutcome result = {
        .error = foreign->failure.error,
        .library = foreign->failure.error == FOREIGN_ERROR_NONE ? 0 : foreign->failure.library,
        .provider = foreign->failure.error == FOREIGN_ERROR_LIBRARY_VERSION ? foreign->failure.provider : 0,
    };
    if (foreign->failure.error == FOREIGN_ERROR_LIBRARY_UNDEFINED)
    {
        const ForeignLibrary* library = &foreign->libraries[foreign->failure.library];
        const ImportReferences* references = &library->library.references;
        const String8 version = library->versions.reference_versions[foreign->failure.reference];
        snprintf(result.reference, sizeof(result.reference), "%.*s@%.*s", (int)references->length[foreign->failure.reference],
                 (const char*)references->name[foreign->failure.reference], (int)version.length, version.pointer);
    }
    else if (foreign->failure.error == FOREIGN_ERROR_LIBRARY_VERSION)
    {
        const String8 version = foreign->libraries[foreign->failure.library].versions.needs.name[foreign->failure.reference];
        snprintf(result.reference, sizeof(result.reference), "%.*s", (int)version.length, version.pointer);
    }
    for (uint32_t l = 0; foreign->failure.error == FOREIGN_ERROR_NONE && l < foreign->library_count && l + 1 < sizeof(result.needed); l += 1)
    {
        result.needed[l] = foreign->libraries[l].needed ? '1' : '0';
    }
    return result;
}

static bool foreign_versions_test(const char* directory, Arena* arena)
{
    ForeignTestTally tally = { .check = "foreign shared libraries' versions" };
    // The line's libraries after libA: the copies (BUSTER_FOREIGN_VERSIONS_TEST_COPIES of them, or none), libC or not,
    // and the libB; whether the versions are checked; and the outcome expected: the failure, its library, its
    // reference (name@version) or need (version) and provider, or the libraries needed.
    static const struct
    {
        const char* name;
        bool copies;
        bool c;
        // The libB's directory, or 0 for none on the line.
        const char* b;
        bool checked;
        ForeignError error;
        const char* reference;
        const char* needed;
    } cases[] = {
        { "libA's foo@V1, a libB of foo@V1 alone: linked, libB not kept", false, false, "fv_v1only", true, FOREIGN_ERROR_NONE, "", "10" },
        { "libA's foo@V1, libC's foo@@V1, a libB of foo@V1 alone: libC kept for it", false, true, "fv_v1only", true, FOREIGN_ERROR_NONE, "", "110" },
        { "libA's foo@V1, 40 libraries, libC's foo@@V1, a libB of foo@V1 alone: libC kept", true, true, "fv_v1only", true, FOREIGN_ERROR_NONE, "", 0 },
        { "libA's foo@V1, a libB of foo@@V2 alone: refused", false, false, "fv_v2only", true, FOREIGN_ERROR_LIBRARY_UNDEFINED, "foo@V1", 0 },
        { "libA's foo@V1, libC's foo@@V1, a libB of foo@@V2 alone, which lacks V1: refused", false, true, "fv_v2only", true, FOREIGN_ERROR_LIBRARY_VERSION, "V1", 0 },
        { "the same without library_versions: linked, as link-foreign links it", false, true, "fv_v2only", false, FOREIGN_ERROR_NONE, "", "100" },
        { "libA's foo@V1 without a libB on the line: linked, libA's references unchecked", false, false, 0, true, FOREIGN_ERROR_NONE, "", "1" },
    };
    ForeignTestPath detail;
    char copies_needed[BUSTER_FOREIGN_VERSIONS_TEST_COPIES + 8] = { 0 };
    copies_needed[0] = '1';
    for (uint32_t c = 0; c < BUSTER_FOREIGN_VERSIONS_TEST_COPIES; c += 1)
    {
        copies_needed[c + 1] = '0';
    }
    copies_needed[BUSTER_FOREIGN_VERSIONS_TEST_COPIES + 1] = '1';
    copies_needed[BUSTER_FOREIGN_VERSIONS_TEST_COPIES + 2] = '0';
    const bool built = foreign_versions_test_build(directory, &tally);
    for (uint32_t k = 0; built && k < BUSTER_ARRAY_LENGTH(cases); k += 1)
    {
        const uint64_t mark = arena->length;
        char* line[BUSTER_FOREIGN_VERSIONS_TEST_COPIES + 8];
        uint32_t count = 0;
        line[count++] = foreign_test_path(directory, "fv_main.o", arena);
        line[count++] = (char*)"--as-needed";
        line[count++] = foreign_test_path(directory, "libfv_a.so", arena);
        for (uint32_t c = 0; cases[k].copies && c < BUSTER_FOREIGN_VERSIONS_TEST_COPIES; c += 1)
        {
            char name[64];
            snprintf(name, sizeof(name), "fv_filler_%u.so", c);
            line[count++] = foreign_test_path(directory, name, arena);
        }
        if (cases[k].c)
        {
            line[count++] = foreign_test_path(directory, "fv_c/libC.so", arena);
        }
        if (cases[k].b != 0)
        {
            char b[64];
            snprintf(b, sizeof(b), "%s/libB.so", cases[k].b);
            line[count++] = foreign_test_path(directory, b, arena);
        }
        bool ready = true;
        for (uint32_t a = 0; a < count; a += 1)
        {
            ready = ready && line[a] != 0;
        }
        ForeignCommand command = { 0 };
        ready = ready && foreign_test_command(&command, line, count, arena);
        foreign_test_expect(&tally, ready, "prepare the command", cases[k].name);
        command.library_versions = cases[k].checked;
        // The library a failure is about: libA, first; the provider, the libB, last.
        const uint32_t libraries = count - 2;
        const char* needed = cases[k].needed != 0 ? cases[k].needed : copies_needed;
        for (uint32_t path = 0; ready && path < 2; path += 1)
        {
            for (uint32_t l = 0; l < BUSTER_ARRAY_LENGTH(foreign_test_lanes); l += 1)
            {
                Foreign foreign;
                foreign_run(&foreign, &command, path == 0 ? FOREIGN_PATH_EXACT : FOREIGN_PATH_VECTOR, foreign_test_lanes[l], 0);
                const ForeignVersionsTestOutcome outcome = foreign_versions_test_outcome(&foreign);
                foreign_release(&foreign);
                const bool failed = cases[k].error != FOREIGN_ERROR_NONE;
                const bool right = outcome.error == cases[k].error && (failed ? outcome.library == 0 && strcmp(outcome.reference, cases[k].reference) == 0 &&
                                                                                    (cases[k].error != FOREIGN_ERROR_LIBRARY_VERSION || outcome.provider == libraries - 1)
                                                                              : strcmp(outcome.needed, needed) == 0);
                snprintf(detail, sizeof(detail), "%s, %s, %u lanes: error %u, library %u, provider %u, reference \"%s\", needed \"%s\"", cases[k].name,
                         path == 0 ? "exact" : "vector", foreign_test_lanes[l], (unsigned)outcome.error, outcome.library, outcome.provider, outcome.reference, outcome.needed);
                foreign_test_expect(&tally, right, "the resolution's versions", detail);
            }
        }
        arena_pop_to(arena, mark);
    }
    return foreign_test_report(&tally);
}
