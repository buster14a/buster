// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original runtime sources for rejected record and atomic operands (#2441 item 1). The shared fixture runner compares
// complete Clang diagnostics, severity policies, both readers and lane requests, and preserves old object outputs.

static void check_test_driver_operand_deferred(CheckTest* test, const char* clang, const char* executable, const char* directory,
                                                const CheckDriverDefaultWarningFixture* fixture)
{
    char object[PATH_MAX];
    snprintf(object, sizeof(object), "%s/operand-deferred.o", directory);
    char* oracle_arguments[] = { (char*)clang, fixture->c23 ? "-std=c23" : "-std=gnu17", "-fsyntax-only", "-fno-caret-diagnostics",
                                (char*)fixture->name, (char*)fixture->options[0], NULL };
    char* expected = NULL;
    const int oracle = check_test_driver_spawn(oracle_arguments, &expected);
    static const char* const lanes[] = { "1", "3" };
    for (uint32_t lane = 0; lane < BUSTER_ARRAY_LENGTH(lanes); lane += 1)
    {
        char* arguments[] = { (char*)(executable != NULL ? executable : "/proc/self/exe"), fixture->c23 ? "-std=c23" : "-std=gnu17",
                              "-c", "-o", object, "-fno-caret-diagnostics", (char*)fixture->name, "--deferred-bodies", "--lanes",
                              (char*)lanes[lane], (char*)fixture->options[0], NULL };
        unlink(object);
        char* actual = NULL;
        const int status = check_test_driver_spawn(arguments, &actual);
        const bool same = expected != NULL && actual != NULL && strcmp(expected, actual) == 0 && status == oracle;
        char description[256];
        snprintf(description, sizeof(description), "%s %s deferred lanes=%s: complete Clang diagnostics and object publication",
                 fixture->name, fixture->c23 ? "c23" : "gnu17", lanes[lane]);
        check_test_expect(test, same && (access(object, F_OK) == 0) == (status == 0), description);
        if (!same)
        {
            fprintf(stderr, "%s: status %d (clang %d)\nBuster:\n%s\nClang:\n%s\n", description, status, oracle,
                    actual != NULL ? actual : "<no output>\n", expected != NULL ? expected : "<no output>\n");
        }
        free(actual);
    }
    free(expected);
    unlink(object);
}

// Compatible atomic conditional arms must preserve the selected value as well as their type. Compile the original
// source with each reader and the deferred path, link it with the same Clang driver, and run its two-arm controls.
static void check_test_driver_atomic_conditional(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    char object[PATH_MAX];
    char binary[PATH_MAX];
    snprintf(object, sizeof(object), "%s/atomic-conditional.o", directory);
    snprintf(binary, sizeof(binary), "%s/atomic-conditional", directory);
    for (uint32_t route = 0; route < 4; route += 1)
    {
        const char* compiler = route == 0 ? clang : executable != NULL ? executable : "/proc/self/exe";
        char* arguments[12] = { (char*)compiler, "-std=gnu17", "-c", "tests/driver/operand_diagnostics/atomic_conditional.c", "-o", object };
        uint32_t count = 6;
        if (route == 2)
        {
            arguments[count++] = "--scalar";
        }
        else if (route == 3)
        {
            arguments[count++] = "--deferred-bodies";
            arguments[count++] = "--lanes";
            arguments[count++] = "3";
        }
        char* text = NULL;
        unlink(object);
        unlink(binary);
        const int compiled = check_test_driver_spawn(arguments, &text);
        free(text);
        char* link[] = { (char*)clang, object, "-o", binary, NULL };
        text = NULL;
        const int linked = compiled == 0 ? check_test_driver_spawn(link, &text) : -1;
        free(text);
        char* run[] = { binary, NULL };
        text = NULL;
        const int status = linked == 0 ? check_test_driver_spawn(run, &text) : -1;
        free(text);
        char description[256];
        snprintf(description, sizeof(description), "compatible atomic conditional arms route=%u: integer, float and pointer values select either arm", route);
        check_test_expect(test, compiled == 0 && linked == 0 && status == 0, description);
    }
    unlink(object);
    unlink(binary);
}

static void check_test_driver_operand_diagnostics(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const struct
    {
        const char* name;
        const char* option;
        uint32_t errors;
        uint32_t warnings;
        uint32_t notes;
    } files[] =
    {
        { .name = "unary", .errors = 9 },
        { .name = "binary", .errors = 12 },
        { .name = "conditional", .errors = 13 },
        { .name = "pointers", .errors = 17 },
        { .name = "qualifiers", .errors = 14 },
        { .name = "statements", .errors = 14 },
        { .name = "assertions", .option = "-pedantic", .errors = 15, .warnings = 3 },
        { .name = "enums", .errors = 14, .warnings = 1 },
        { .name = "enums_recovery", .errors = 6, .notes = 1 },
        { .name = "valid", .option = "-Wno-literal-conversion", .errors = 0 },
        { .name = "atomic_conditional", .errors = 0 },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(files); i += 1)
    {
        char path[256];
        snprintf(path, sizeof(path), "tests/driver/operand_diagnostics/%s.c", files[i].name);
        const int descriptor = open(path, O_RDONLY);
        char* source = NULL;
        const bool read = descriptor >= 0 && check_test_driver_read_file(descriptor, &source);
        check_test_expect(test, read, "invalid-operand runtime fixture is available (run from the repository root)");
        if (descriptor >= 0)
        {
            close(descriptor);
        }
        if (read)
        {
            CheckDriverDefaultWarningFixture fixture =
            {
                .name = path,
                .source = source,
                .options = { files[i].option },
                .errors = files[i].errors,
                .warnings = files[i].warnings,
                .notes = files[i].notes,
            };
            for (uint32_t language = 0; language < 2; language += 1)
            {
                fixture.c23 = language != 0;
                check_test_driver_warning_fixtures(test, clang, executable, directory, &fixture, 1);
                check_test_driver_operand_deferred(test, clang, executable, directory, &fixture);
            }
        }
        free(source);
    }
    check_test_driver_atomic_conditional(test, clang, executable, directory);
}
