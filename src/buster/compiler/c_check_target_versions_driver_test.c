// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// GNU's function multiversioning by target strings (#1838, c_check_target_versions.c), by the driver against clang 23,
// on original fixtures:
//
// - Diagnostics: each unit's whole output (diagnostics, their places, their order, their notes) must be clang's by both
//   paths, in GNU C17: target_clones' strings (unsupported features and CPUs, empty versions, a trailing comma, the
//   duplicates, mixed specifiers, the missing default, trimmed versions, concatenated literals); then the warnings'
//   options.
// Include after the driver test's helpers (check_test_driver_spawn, check_test_diagnostic_lines).

static void check_test_target_versions_diagnostics(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char* const units[] = {
        // target_clones' strings: valid lists, trimmed, of every kind of version, concatenated.
        "__attribute__((target_clones(\"avx2\", \"sse4.2\", \"default\"))) int f(void) { return 1; }\n",
        "__attribute__((target_clones(\" avx2 , default \"))) int f(void) { return 1; }\n",
        "__attribute__((target_clones(\"arch=znver4\", \"arch=x86-64-v3\", \"avx10.2\", \"apxf\", \"avx512vp2intersect\", \"default\"))) int f(void) { return 1; }\n",
        "__attribute__((target_clones(\"av\" \"x2\", \"default\"))) int f(void) { return 1; }\n",
        "__attribute__((target_clones(\"default\"))) int f(void) { return 1; }\n",
        // The missing default: none, an empty list, and with a warning before.
        "__attribute__((target_clones(\"avx2\", \"sse4.2\"))) int f(void) { return 1; }\n",
        "__attribute__((target_clones())) int f(void) { return 1; }\n",
        "__attribute__((target_clones(\"avx2\", \"avx2\"))) int f(void) { return 1; }\n",
        // Unsupported versions: a feature of no priority, a feature unknown, misspelled or negated, tune=, a CPU unknown or
        // 32-bit, an empty string, a trailing comma, an empty item, and the first of several.
        "__attribute__((target_clones(\"adx\", \"default\"))) int f(void) { return 1; }\n",
        "__attribute__((target_clones(\"avx2\", \"foo\", \"default\"))) int f(void) { return 1; }\n",
        "__attribute__((target_clones(\"Avx2\", \"default\"))) int f(void) { return 1; }\n",
        "__attribute__((target_clones(\"no-avx2\", \"default\"))) int f(void) { return 1; }\n",
        "__attribute__((target_clones(\"crc32\", \"x87\", \"default\"))) int f(void) { return 1; }\n",
        "__attribute__((target_clones(\"tune=znver4\", \"default\"))) int f(void) { return 1; }\n",
        "__attribute__((target_clones(\"arch=znver4\", \"arch=foo\", \"default\"))) int f(void) { return 1; }\n",
        "__attribute__((target_clones(\"arch=i386\", \"default\"))) int f(void) { return 1; }\n",
        "__attribute__((target_clones(\"\", \"default\"))) int f(void) { return 1; }\n",
        "__attribute__((target_clones(\"avx2,\", \"default\"))) int f(void) { return 1; }\n",
        "__attribute__((target_clones(\"avx2,,default\"))) int f(void) { return 1; }\n",
        "__attribute__((target_clones(\"default, avx2,  foo\"))) int f(void) { return 1; }\n",
        "__attribute__((target_clones(\"avx2\", \"avx2\", \"foo\", \"default\"))) int f(void) { return 1; }\n",
        // Duplicates: two strings, two versions of one string, at an offset, and default twice.
        "__attribute__((target_clones(\"avx2\", \"avx2\", \"default\"))) int f(void) { return 1; }\n",
        "__attribute__((target_clones(\"avx2,avx2,default\"))) int f(void) { return 1; }\n",
        "__attribute__((target_clones(\"  avx2 ,   avx2 , default\"))) int f(void) { return 1; }\n",
        "__attribute__((target_clones(\"default\", \"default\"))) int f(void) { return 1; }\n",
        // Mixed specifiers: one string of several among others, and one alone.
        "__attribute__((target_clones(\"avx2,bmi\", \"default\"))) int f(void) { return 1; }\n",
        "__attribute__((target_clones(\"sse4.2\", \"avx2,bmi\", \"default\"))) int f(void) { return 1; }\n",
        "__attribute__((target_clones(\"avx2,bmi,default\"))) int f(void) { return 1; }\n",
        "__attribute__((target_clones(\"avx2,avx2\", \"sse4.2\"))) int f(void) { return 1; }\n",
        // Other subjects and arguments.
        "int x __attribute__((target_clones(\"avx2\", \"default\")));\n",
        "__attribute__((target_clones(1, \"default\"))) int f(void) { return 1; }\n",
        "__attribute__((target_clones(\"default\"), target_clones(\"avx2\", \"default\"))) int f(void) { return 1; }\n",
    };
    // Units whose warnings each option set below changes.
    static const char* const flagged[] = {
        "__attribute__((target_clones(\"avx2\", \"avx2\", \"default\"))) int f(void) { return 1; }\n",
        "__attribute__((target_clones(\"avx2,bmi\", \"default\"))) int f(void) { return 1; }\n",
        "__attribute__((target_clones(\"adx\", \"default\"))) int f(void) { return 1; }\n",
    };
    static const char* const options[] = {
        "-Werror",
        "-w",
        "-Wno-function-multiversion",
        "-Werror=function-multiversion",
        "-Wno-target-clones-mixed-specifiers",
        "-Werror=target-clones-mixed-specifiers",
        "-Wno-ignored-attributes",
    };
    enum { LINES = 16384 };
    char* expected_lines = malloc(LINES);
    char* actual_lines = malloc(LINES);
    char source[PATH_MAX];
    snprintf(source, sizeof(source), "%s/target-versions.c", directory);
    const uint32_t plain = BUSTER_ARRAY_LENGTH(units);
    const uint32_t total = plain + BUSTER_ARRAY_LENGTH(flagged) * BUSTER_ARRAY_LENGTH(options);
    for (uint32_t unit = 0; expected_lines != NULL && actual_lines != NULL && unit < total; unit += 1)
    {
        const uint32_t flag = unit - plain;
        const char* text = unit < plain ? units[unit] : flagged[flag % BUSTER_ARRAY_LENGTH(flagged)];
        char* option = unit < plain ? NULL : (char*)options[flag / BUSTER_ARRAY_LENGTH(flagged)];
        const bool made = parse_test_write(source, text, strlen(text));
        char* arguments[] = { (char*)clang, "-std=gnu17", "-fsyntax-only", "-fno-caret-diagnostics", source, option, NULL, NULL };
        char* expected = NULL;
        const int oracle = made ? check_test_driver_spawn(arguments, &expected) : -1;
        const bool lines = expected != NULL && check_test_diagnostic_lines(expected, expected_lines, LINES);
        char description[4096];
        snprintf(description, sizeof(description), "target versions %u: clang's lines read", unit);
        check_test_expect(test, made && lines, description);
        for (uint32_t scalar = 0; lines && scalar < 2; scalar += 1)
        {
            arguments[0] = (char*)executable;
            arguments[option != NULL ? 6 : 5] = scalar != 0 ? "--scalar" : NULL;
            char* actual = NULL;
            const int status = check_test_driver_spawn(arguments, &actual);
            const bool same = actual != NULL && check_test_diagnostic_lines(actual, actual_lines, LINES) && strcmp(expected_lines, actual_lines) == 0;
            snprintf(description, sizeof(description), "target versions %u (%s%s%s), %s: status %d (clang %d), lines\n%s(clang\n%s)", unit,
                     option != NULL ? option : "", option != NULL ? " " : "", text, scalar != 0 ? "scalar" : "vector", status, oracle, actual_lines, expected_lines);
            check_test_expect(test, same && (status != 0) == (oracle != 0), description);
            free(actual);
        }
        free(expected);
    }
    free(expected_lines);
    free(actual_lines);
    unlink(source);
}

static void check_test_driver_target_versions(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_target_versions_diagnostics(test, clang, executable, directory);
}
