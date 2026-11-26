// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #2219's imaginary bit-precise constants (5wbi, clang's _Complex _BitInt(N)) through the real driver against Clang:
// tests/driver/imaginary-bit-int/diagnostics sources, each in the catalog's standards, by the shared
// default-warning harness (both readers, one and three lanes, -Werror and -w, -fsyntax-only and -c), and its program,
// built by Clang -O0 as the oracle and by buster at Tier 0 and strict Tier 1, by both readers, as an object Clang links
// and directly, whose output must be Clang's.

static void check_test_driver_imaginary_bit_int_diagnostics(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_suite(test, clang, executable, directory, "driver/imaginary-bit-int/diagnostics", false);
}

static void check_test_driver_imaginary_bit_int_program(CheckTest* test, const char* clang, const char* executable, const char* directory, const CheckDriverProgramInput* inputs)
{
    const char* const buster = executable != NULL ? executable : "/proc/self/exe";
    char source[PATH_MAX];
    snprintf(source, sizeof(source), "%s/imaginary-bit-int.c", directory);
    const bool available = parse_test_write(source, inputs[0].bytes, inputs[0].length);
    check_test_expect(test, available, "imaginary bit-precise program is staged from the selected fixture root");
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
        const bool deferred = (route & 2) != 0;
        const bool tier1 = (route & 4) != 0;
        char* arguments[24] = { (char*)buster, "-std=c23", "-w" };
        uint32_t count = 3;
        if (deferred)
        {
            arguments[count++] = "--deferred-bodies";
        }
        arguments[count++] = "--lanes";
        arguments[count++] = deferred ? "3" : "1";
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
                 deferred ? "deferred, 3 lanes" : "1 lane", direct ? "direct" : "object");
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
                     deferred ? "deferred, 3 lanes" : "1 lane", direct ? "direct" : "object");
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
    unlink(source);
}

static void check_test_driver_imaginary_bit_int(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_imaginary_bit_int_diagnostics(test, clang, executable, directory);
    check_test_driver_programs(test, clang, executable, directory, "imaginary-bit-int", "program.c", check_test_driver_imaginary_bit_int_program);
}
