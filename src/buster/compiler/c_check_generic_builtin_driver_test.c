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
