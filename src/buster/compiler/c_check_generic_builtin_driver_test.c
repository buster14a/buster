// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #148's generic builtins' own errors by the driver against clang, on original fixtures: __builtin_verbose_trap's
// arguments, each a constant string without a `$` (check_input_verbose_trap_arguments), a literal's under parentheses
// and casts, __func__'s, a variable's, a null pointer's and an integer's; and a FILE builtin's call in a unit without a
// file-scope FILE (check_input_file_type), clang's "use of unknown builtin". The errors are compared, their places and
// messages, in order, with the exit status, by both paths of -fsyntax-only (each function's first error alone, as the
// checker reports them). Warnings aren't compared. Include after c_check_label_attribute_driver_test.c.

static void check_test_driver_generic_builtins(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const struct
    {
        const char* name;
        const char* source;
    } fixtures[] = {
        { "verbose_trap's literals", "void g(int x) {\nif (x) __builtin_verbose_trap(\"c\", \"m\" \"n\");\n__builtin_verbose_trap(((\"c\")), (const char*)\"m\");\n}\n" },
        { "verbose_trap of __func__", "void g(void) {\n__builtin_verbose_trap(__func__, __FUNCTION__);\n}\n" },
        { "verbose_trap of a dollar", "void g(void) {\n__builtin_verbose_trap(\"c\", \"a$b\");\n}\n" },
        { "verbose_trap of a dollar under a cast", "void g(void) {\n__builtin_verbose_trap((const char*)(\"$\"), \"m\");\n}\n" },
        { "verbose_trap of __func__ with a dollar", "void g$h(void) {\n__builtin_verbose_trap(\"c\", __func__);\n}\n" },
        { "verbose_trap of a variable", "void g(const char* p) {\n__builtin_verbose_trap(p, \"m\");\n}\n" },
        { "verbose_trap of a variable under a cast", "void g(char* p) {\n__builtin_verbose_trap(\"c\", (const char*)p);\n}\n" },
        { "verbose_trap of a null pointer", "void g(void) {\n__builtin_verbose_trap((const char*)0, \"m\");\n}\n" },
        { "verbose_trap of a call", "const char* f(void);\nvoid g(void) {\n__builtin_verbose_trap(\"c\", f());\n}\n" },
        { "verbose_trap in two functions", "void g(const char* p) {\n__builtin_verbose_trap(p, \"m\");\n}\nvoid h(void) {\n__builtin_verbose_trap(\"$\", \"m\");\n}\n" },
        { "fprintf without FILE", "int g(void* p) {\nreturn 1 + __builtin_fprintf(p, \"x\");\n}\n" },
        { "vfscanf with a FILE of block scope", "void h(void) {\ntypedef int FILE;\n}\nint g(void* p, __builtin_va_list a) {\nreturn __builtin_vfscanf(p, \"x\", a);\n}\n" },
    };
    char source[PATH_MAX];
    snprintf(source, sizeof(source), "%s/generic-builtins.c", directory);
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        const bool made = parse_test_write(source, fixtures[i].source, strlen(fixtures[i].source));
        char* arguments[] = { (char*)clang, "-std=gnu17", "-fsyntax-only", "-fno-color-diagnostics", source, NULL };
        char* reference = NULL;
        const int oracle = made ? check_test_driver_spawn(arguments, &reference) : -1;
        char expected[8192];
        const bool expected_valid = check_test_cleanup_errors(reference, 0, expected, sizeof(expected));
        free(reference);
        char description[512];
        snprintf(description, sizeof(description), "generic builtins, %s: clang runs it", fixtures[i].name);
        check_test_expect(test, made && (oracle == 0 || oracle == 1) && expected_valid && (oracle == 0) == (expected[0] == 0), description);
        for (uint32_t scalar = 0; made && scalar < 2; scalar += 1)
        {
            char* checked[] = { (char*)(executable != NULL ? executable : "/proc/self/exe"), "-std=gnu17", "-fsyntax-only", scalar != 0 ? "--scalar" : source,
                                scalar != 0 ? source : NULL, NULL };
            char* text = NULL;
            const int status = check_test_driver_spawn(checked, &text);
            char actual[8192];
            const bool actual_valid = check_test_cleanup_errors(text, 0, actual, sizeof(actual));
            free(text);
            snprintf(description, sizeof(description), "generic builtins, %s, by the %s path: clang's\n%sagainst\n%s", fixtures[i].name,
                     scalar != 0 ? "scalar" : "vector", expected, actual);
            check_test_expect(test, status == oracle && expected_valid && actual_valid && strcmp(expected, actual) == 0, description);
        }
    }
    unlink(source);
}

// __builtin_available (#148, c_check_availability_input.c), by the driver against clang, by both paths of -fsyntax-only:
// the whole output, line for line, with the exit status. First a unit of valid checks, warned about where they guard
// nothing (not an if's condition, through parentheses and `!`, nor a case label's value), at file scope never, under
// -Wunsupported-availability-guard's and -Wavailability's options; then units of clang's errors of the specs: a version
// that isn't one, a zero one, an unknown platform, an anyAppleOS below 26, a repeated platform or `*`, no `*`.
static void check_test_driver_availability(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char guards[] =
        "int s = sizeof(__builtin_available(*));\n"
        "typeof(__builtin_available(macos 10.12, *)) t;\n"
        "int u = _Generic(__builtin_available(*), _Bool: 1, default: 2);\n"
        "int f(int x) {\n"
        "    int n = 0;\n"
        "    if (__builtin_available(macos 10.12, ios 10, tvos 10_1, watchos 3.1.2, *)) n += 1;\n"
        "    if (!(__builtin_available(macOS 10, iOSApplicationExtension 1, *))) n += 2; else if ((!!__builtin_available(*))) n += 3;\n"
        "    if (__builtin_available(*) && x) n += 4;\n"
        "    while (__builtin_available(visionOS 1, ShaderModel 6, macosx 10, *) && x--) n += 5;\n"
        "    for (; __builtin_available(android 21, ohos 3, fuchsia 1, swift 5, *) && n < 9;) n += 1;\n"
        "    _Bool b = __builtin_available(anyAppleOS 26, driverkit 19, maccatalyst 13.1, *);\n"
        "    n += (int)sizeof(__builtin_available(*)) + _Generic(__builtin_available(*), _Bool: 1, default: 0);\n"
        "    typeof(__builtin_available(*)) c = 1;\n"
        "    int a[__builtin_available(*) + 1];\n"
        "    switch (x) { case sizeof(__builtin_available(*)): n += 1; break; default: break; }\n"
        "    n += ({ __builtin_available(*); }) + (x ? __builtin_available(macos 1_2_3, *) : 0) + b + c + (int)sizeof a;\n"
        "    return n + __builtin_available(macos 1.2_3, ios 1_2.3, *);\n"
        "}\n";
    // A unit each, as the checker stops a unit at its first error: a spec's errors, in order, then the list's, which leave
    // clang's expression, and so its warning.
    static const char* const errors[] = {
        "int f(void) { return __builtin_available(macos true, *); }\n",
        "int f(void) { return __builtin_available(macos 0x10, ios 1.2.3.4, *); }\n",
        "int f(void) { return __builtin_available(macos 1e5, tvos 10.); }\n",
        "int f(void) { return __builtin_available(macos 10.2_3x, *); }\n",
        "int f(void) { return __builtin_available(macos 0, *); }\n",
        "int f(void) { return __builtin_available(ios 0.0); }\n",
        "int f(void) { return __builtin_available(foo 1, xros 1, *); }\n",
        "int f(void) { return __builtin_available(xros_app_extension 2, *); }\n",
        "int f(void) { return __builtin_available(anyAppleOS 25.3, anyAppleOS 25_9_9, *); }\n",
        "int f(void) { return __builtin_available(foo 0x1, bar 1, *); }\n",
        "int f(void) { return __builtin_available(macos 10, macOS 11, *, *); }\n",
        "int f(void) { return __builtin_available(visionOS 1, visionos 2); }\n",
        "int f(void) { return __builtin_available(macos 10); }\n",
        "int f(void) { if (__builtin_available(tvos 4294967296, *)) return 1; return 0; }\n",
        "int f(void) { if (__builtin_available(ios 1, tvos 2_3)) return 1; return 0; }\n",
    };
    static const char* const options[] = { NULL, "-Wno-unsupported-availability-guard", "-Werror=unsupported-availability-guard", "-Wno-availability",
                                            "-Werror=availability", "-w" };
    enum { LINES = 16384 };
    char* expected_lines = malloc(LINES);
    char* actual_lines = malloc(LINES);
    char source[PATH_MAX];
    snprintf(source, sizeof(source), "%s/availability.c", directory);
    const uint32_t units = 1 + BUSTER_ARRAY_LENGTH(errors);
    for (uint32_t unit = 0; expected_lines != NULL && actual_lines != NULL && unit < units; unit += 1)
    {
        const char* text = unit == 0 ? guards : errors[unit - 1];
        const bool made = parse_test_write(source, text, strlen(text));
        for (uint32_t o = 0; made && o < (unit == 0 ? BUSTER_ARRAY_LENGTH(options) : 1); o += 1)
        {
            char* arguments[] = { (char*)clang, "-std=gnu23", "-fsyntax-only", "-fno-caret-diagnostics", source, (char*)options[o], NULL, NULL };
            char* expected = NULL;
            const int oracle = check_test_driver_spawn(arguments, &expected);
            const bool lines = expected != NULL && check_test_diagnostic_lines(expected, expected_lines, LINES);
            char description[4096];
            snprintf(description, sizeof(description), "availability %u %s: clang's lines read", unit, options[o] != NULL ? options[o] : "(no option)");
            check_test_expect(test, lines && (expected_lines[0] == 0) == (unit == 0 && o == 5), description);
            for (uint32_t scalar = 0; lines && scalar < 2; scalar += 1)
            {
                const uint32_t last = options[o] != NULL ? 6 : 5;
                arguments[0] = (char*)executable;
                arguments[last] = scalar != 0 ? "--scalar" : NULL;
                char* actual = NULL;
                const int status = check_test_driver_spawn(arguments, &actual);
                const bool same = actual != NULL && check_test_diagnostic_lines(actual, actual_lines, LINES) && strcmp(expected_lines, actual_lines) == 0;
                snprintf(description, sizeof(description), "availability %u %s, %s: status %d (clang %d), lines\n%s(clang\n%s)", unit,
                         options[o] != NULL ? options[o] : "(no option)", scalar != 0 ? "scalar" : "vector", status, oracle, actual_lines, expected_lines);
                check_test_expect(test, same && (status != 0) == (oracle != 0), description);
                free(actual);
            }
            free(expected);
        }
        unlink(source);
    }
    free(expected_lines);
    free(actual_lines);
}
