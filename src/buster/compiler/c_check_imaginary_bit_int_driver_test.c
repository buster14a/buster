// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #2219's imaginary bit-precise constants (5wbi, clang's _Complex _BitInt(N)) through the real driver against Clang:
// tests/driver/imaginary-bit-int's diagnostics sources, each in the standards cases.txt names, by the shared
// default-warning harness (both readers, one and three lanes, -Werror and -w, -fsyntax-only and -c), and its program,
// built by Clang -O0 as the oracle and by buster at Tier 0 and strict Tier 1, by both readers, as an object Clang links
// and directly, whose output must be Clang's.

static void check_test_driver_imaginary_bit_int_diagnostics(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    CheckTestFixtures files = { .storage = arena_reserve(UINT64_C(2) * CHECK_TEST_FIXTURE_BYTES + 4096) };
    uint64_t length = 0;
    char* catalog = files.storage.base != NULL ? check_test_fixture_read(&files, "tests/driver/imaginary-bit-int/cases.txt", &length) : NULL;
    check_test_expect(test, catalog != NULL, "imaginary bit-precise diagnostics case list is read (run from the repository root)");
    const uint64_t mark = files.storage.length;
    char* cursor = NULL;
    char* line = catalog != NULL ? strtok_r(catalog, "\n", &cursor) : NULL;
    uint32_t count = 0;
    bool valid = catalog != NULL;
    for (; valid && line != NULL; line = strtok_r(NULL, "\n", &cursor))
    {
        if (line[0] == '#')
        {
            continue;
        }
        char name[128];
        char standard[16];
        char extra;
        CheckDriverDefaultWarningFixture fixture = { 0 };
        valid = sscanf(line, "%127s %15s %u %u %u %c", name, standard, &fixture.warnings, &fixture.errors, &fixture.notes, &extra) == 5 &&
                check_test_fixture_word(name, sizeof(name) - 1) && (strcmp(standard, "c23") == 0 || strcmp(standard, "gnu17") == 0);
        check_test_expect(test, valid, "imaginary bit-precise diagnostics case metadata is valid");
        if (valid)
        {
            char path[PATH_MAX];
            char description[192];
            snprintf(path, sizeof(path), "tests/driver/imaginary-bit-int/%s.c", name);
            snprintf(description, sizeof(description), "imaginary bit-precise %s, -std=%s", name, standard);
            fixture.name = description;
            fixture.c23 = strcmp(standard, "c23") == 0;
            fixture.source = check_test_fixture_read(&files, path, &length);
            check_test_expect(test, fixture.source != NULL, path);
            if (fixture.source != NULL)
            {
                check_test_driver_warning_fixtures(test, clang, executable, directory, &fixture, 1);
            }
            count += 1;
            arena_pop_to(&files.storage, mark);
        }
    }
    check_test_expect(test, count != 0, "imaginary bit-precise diagnostics case list is nonempty");
    arena_release(&files.storage);
}

static void check_test_driver_imaginary_bit_int_program(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    const char* const buster = executable != NULL ? executable : "/proc/self/exe";
    const char* const source = "tests/driver/imaginary-bit-int/program.c";
    const bool available = access(source, R_OK) == 0;
    check_test_expect(test, available, "imaginary bit-precise program is available (run from the repository root)");
    char object[PATH_MAX];
    char binary[PATH_MAX];
    char reference[PATH_MAX];
    snprintf(object, sizeof(object), "%s/imaginary-bit-int.o", directory);
    snprintf(binary, sizeof(binary), "%s/imaginary-bit-int-buster", directory);
    snprintf(reference, sizeof(reference), "%s/imaginary-bit-int-clang", directory);
    char* oracle_arguments[] = { (char*)clang, "-O0", "-std=c23", "-w", (char*)source, "-o", reference, NULL };
    char* diagnostic = NULL;
    const int oracle_status = available ? check_test_driver_spawn(oracle_arguments, &diagnostic) : -1;
    check_test_expect(test, oracle_status == 0, "imaginary bit-precise program: Clang -O0 builds the oracle");
    if (oracle_status != 0 && diagnostic != NULL)
    {
        fprintf(stderr, "%s", diagnostic);
    }
    free(diagnostic);
    diagnostic = NULL;
    char* expected = NULL;
    char* oracle_run[] = { reference, NULL };
    const bool referenced = oracle_status == 0 && check_test_driver_spawn_stream(oracle_run, &expected, true) == 0 && expected != NULL &&
                            strncmp(expected, "statics ", 8) == 0;
    check_test_expect(test, referenced, "imaginary bit-precise program: Clang's build runs");
    for (uint32_t route = 0; referenced && route < 8; route += 1)
    {
        const bool direct = (route & 1) != 0;
        const bool scalar = (route & 2) != 0;
        const bool tier1 = (route & 4) != 0;
        char* arguments[24] = { (char*)buster, "-std=c23", "-w" };
        uint32_t count = 3;
        if (scalar)
        {
            arguments[count++] = "--scalar";
            arguments[count++] = "--deferred-bodies";
        }
        arguments[count++] = "--lanes";
        arguments[count++] = scalar ? "3" : "1";
        if (tier1)
        {
            arguments[count++] = "--tier1";
            arguments[count++] = "--tier1-strict";
            arguments[count++] = "--tier1-report";
            arguments[count++] = "--verify";
        }
        if (!direct)
        {
            arguments[count++] = "-c";
        }
        arguments[count++] = (char*)source;
        arguments[count++] = "-o";
        arguments[count++] = direct ? binary : object;
        int status = check_test_driver_spawn(arguments, &diagnostic);
        if (tier1)
        {
            // Every function Tier 1's: none left to Tier 0.
            const bool taken = status == 0 && diagnostic != NULL && strstr(diagnostic, "fallbacks=0\t") != NULL;
            check_test_expect(test, taken, "imaginary bit-precise program is compiled by Tier 1 with no fallback");
        }
        if (status == 0 && !direct)
        {
            free(diagnostic);
            diagnostic = NULL;
            char* link[] = { (char*)clang, object, "-o", binary, NULL };
            status = check_test_driver_spawn(link, &diagnostic);
        }
        char description[256];
        snprintf(description, sizeof(description), "imaginary bit-precise program, Tier %u, %s reader, %s: builds and links", tier1 ? 1 : 0,
                 scalar ? "scalar/deferred" : "vector", direct ? "direct" : "object");
        check_test_expect(test, status == 0, description);
        if (status != 0 && diagnostic != NULL)
        {
            fprintf(stderr, "%s", diagnostic);
        }
        free(diagnostic);
        diagnostic = NULL;
        if (status == 0)
        {
            char* actual = NULL;
            char* run[] = { binary, NULL };
            const int ran = check_test_driver_spawn_stream(run, &actual, true);
            const bool same = ran == 0 && actual != NULL && strcmp(actual, expected) == 0;
            snprintf(description, sizeof(description), "imaginary bit-precise program, Tier %u, %s reader, %s: prints Clang's values", tier1 ? 1 : 0,
                     scalar ? "scalar/deferred" : "vector", direct ? "direct" : "object");
            check_test_expect(test, same, description);
            if (!same)
            {
                fprintf(stderr, "Clang:\n%sBuster:\n%s", expected, actual != NULL ? actual : "<no output>\n");
            }
            free(actual);
        }
        unlink(object);
        unlink(binary);
    }
    free(expected);
    unlink(reference);
}

static void check_test_driver_imaginary_bit_int(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_imaginary_bit_int_diagnostics(test, clang, executable, directory);
    check_test_driver_imaginary_bit_int_program(test, clang, executable, directory);
}
