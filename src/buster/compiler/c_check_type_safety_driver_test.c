// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// clang's type tags (#1447, c_check_type_safety_input.c): argument_with_type_tag, pointer_with_type_tag and
// type_tag_for_datatype, by the driver against clang, on original fixtures. First -Wtype-safety's warnings at calls
// (check_common_type_safety), through the shared warning harness at each policy, both paths and lanes: tags named by
// variables and by registered values, through casts, & and *, commas and constant conditions; mismatched, matching,
// qualified, aliased, character, layout-compatible and null pointer tags; inherited and replaced rules; registrations
// before and after calls; and a pragma. Then each declaration unit's whole output, by both paths, in GNU C17 and C23: the
// attributes' parser's and handlers' errors, their subjects and counts, and their C23 spellings.

static void check_test_driver_type_safety(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_suite(test, clang, executable, directory, "driver/warnings/type-safety", false);
    static const char* const units[] = {
        "void f(int *p, int t) __attribute__((pointer_with_type_tag(mpi, 1, 2)));\n",
        "void f(int p, int t) __attribute__((pointer_with_type_tag(mpi, 1, 2)));\n",
        "void f(int *p, int t) __attribute__((pointer_with_type_tag(mpi, 3, 2)));\n",
        "void f(int *p, int t) __attribute__((pointer_with_type_tag(mpi, 1, 3)));\n",
        "void f(int *p, int t) __attribute__((pointer_with_type_tag(1, 1, 2)));\n",
        "void f(int *p, int t) __attribute__((pointer_with_type_tag(mpi, 1)));\n",
        "void f(int *p, int t) __attribute__((pointer_with_type_tag(mpi, 0, 2)));\n",
        "void f(int *p, int t) __attribute__((pointer_with_type_tag(mpi, x, 2)));\n",
        "void f(int *p, int t) __attribute__((pointer_with_type_tag(mpi, 1.0, 2)));\n",
        "void f(int *p, int t) __attribute__((pointer_with_type_tag(mpi, 1, 2, 3)));\n",
        "void f(int *p, int t) __attribute__((pointer_with_type_tag));\n",
        "void f(int *p, int t) __attribute__((pointer_with_type_tag(mpi, -1, 2)));\n",
        "void f(int *p, int t) __attribute__((pointer_with_type_tag(mpi, 4294967297, 2)));\n",
        "void f(int *p, int t) __attribute__((argument_with_type_tag(mpi, 1, 2)));\n",
        "void f(int p, int t) __attribute__((argument_with_type_tag(mpi, 1, 2)));\n",
        "int v __attribute__((argument_with_type_tag(mpi, 1, 2)));\n",
        "void (*fp)(int *p, int t) __attribute__((pointer_with_type_tag(mpi, 1, 2)));\n",
        "void f() __attribute__((pointer_with_type_tag(mpi, 1, 2)));\n",
        "typedef void T(int *p, int t) __attribute__((pointer_with_type_tag(mpi, 1, 2)));\n",
        "void f(int *p, int t) __attribute__((argument_with_type_tag(mpi, 1, 2), pointer_with_type_tag(other, 1, 2)));\n",
        "void f(int *p, int t) __attribute__((__pointer_with_type_tag__(mpi, 1, 2)));\n",
        "struct S { int a; } __attribute__((pointer_with_type_tag(mpi, 1, 2)));\n",
        "void f(int *p __attribute__((pointer_with_type_tag(mpi, 1, 2))), int t);\n",
        "void f(int *p, int t) __attribute__((pointer_with_type_tag(mpi, 2 - 1, 1 + 1)));\n",
        "int t __attribute__((type_tag_for_datatype(mpi, int)));\n",
        "int t __attribute__((type_tag_for_datatype(mpi, int, layout_compatible)));\n",
        "int t __attribute__((type_tag_for_datatype(mpi, int, must_be_null)));\n",
        "int t __attribute__((type_tag_for_datatype(mpi, int, layout_compatible, must_be_null, layout_compatible)));\n",
        "int t __attribute__((type_tag_for_datatype(mpi, int, foo)));\n",
        "int t __attribute__((type_tag_for_datatype(1, int)));\n",
        "int t __attribute__((type_tag_for_datatype(mpi int)));\n",
        "int t __attribute__((type_tag_for_datatype(mpi, 1)));\n",
        "int t __attribute__((type_tag_for_datatype(mpi, int, 1)));\n",
        "int t __attribute__((type_tag_for_datatype));\n",
        "int t __attribute__((type_tag_for_datatype()));\n",
        "int t __attribute__((type_tag_for_datatype(mpi)));\n",
        "int t __attribute__((type_tag_for_datatype(mpi,)));\n",
        "int t __attribute__((type_tag_for_datatype(mpi, foo)));\n",
        "int t __attribute__((type_tag_for_datatype(mpi, int,)));\n",
        "int t __attribute__((type_tag_for_datatype(mpi, int *)));\n",
        "int t __attribute__((type_tag_for_datatype(mpi, struct S)));\n",
        "typedef long L;\n" "int t __attribute__((type_tag_for_datatype(mpi, L)));\n",
        "int t __attribute__((type_tag_for_datatype(mpi, __layout_compatible__)));\n",
        "int t __attribute__((type_tag_for_datatype(mpi, int, __layout_compatible__)));\n",
        "void f(void) __attribute__((type_tag_for_datatype(mpi, int)));\n",
        "typedef int T __attribute__((type_tag_for_datatype(mpi, int)));\n",
        "struct S { int m __attribute__((type_tag_for_datatype(mpi, int))); };\n",
        "struct __attribute__((type_tag_for_datatype(mpi, int))) S { int m; };\n",
        "void f(void) { int l __attribute__((type_tag_for_datatype(mpi, int))); }\n",
        "void f(void) { static int l __attribute__((type_tag_for_datatype(mpi, int))) = 3; }\n",
        "static const int m __attribute__((type_tag_for_datatype(mpi, int))) = 3;\n",
        "int k;\n" "const int m __attribute__((type_tag_for_datatype(mpi, int))) = k;\n",
        "const double m __attribute__((type_tag_for_datatype(mpi, int))) = 1.5;\n",
        "const int *m __attribute__((type_tag_for_datatype(mpi, int))) = 0;\n",
        "static const int m __attribute__((type_tag_for_datatype(mpi, int), type_tag_for_datatype(mpi, long))) = 3;\n",
        "extern const int m __attribute__((type_tag_for_datatype(mpi, int)));\n" "const int m = 4;\n",
        "int t __attribute__((__type_tag_for_datatype__(mpi, int)));\n",
        "[[clang::type_tag_for_datatype(mpi, int)]] int t;\n",
        "int t [[clang::type_tag_for_datatype(mpi, int, must_be_null)]];\n",
        "[[clang::pointer_with_type_tag(mpi, 1, 2)]] void f(int *p, int t);\n",
        "void f(int *p, int t) [[clang::argument_with_type_tag(mpi, 1, 2)]];\n",
        "void f(int *p, int t, ...) __attribute__((pointer_with_type_tag(mpi, 3, 2)));\n",
        "void f(int k) { const int m __attribute__((type_tag_for_datatype(mpi, int))) = k; }\n",
        "void f(int k) { int m __attribute__((type_tag_for_datatype(mpi, int))) = k + 1, n = 2; }\n",
        "void f(int k) { int m __attribute__((type_tag_for_datatype(mpi, int))) = 1, n __attribute__((type_tag_for_datatype(mpi, int))) = k; }\n",
        "int m __attribute__((type_tag_for_datatype(mpi, int))) = 1;\n",
        "enum E { A = 3 } e __attribute__((type_tag_for_datatype(mpi, int))) = A;\n",
        "_Bool b __attribute__((type_tag_for_datatype(mpi, int))) = 1;\n",
        "long long x __attribute__((type_tag_for_datatype(mpi, int))) = -1;\n",
        "int y __attribute__((type_tag_for_datatype(mpi, int))) = { 1 };\n",
    };
    static const char* const standards[] = { "-std=gnu17", "-std=gnu23" };
    enum { LINES = 16384 };
    char* expected_lines = malloc(LINES);
    char* actual_lines = malloc(LINES);
    char source[PATH_MAX];
    snprintf(source, sizeof(source), "%s/type-tags.c", directory);
    const uint32_t total = BUSTER_ARRAY_LENGTH(units) * BUSTER_ARRAY_LENGTH(standards);
    for (uint32_t unit = 0; expected_lines != NULL && actual_lines != NULL && unit < total; unit += 1)
    {
        const char* text = units[unit % BUSTER_ARRAY_LENGTH(units)];
        char* standard = (char*)standards[unit / BUSTER_ARRAY_LENGTH(units)];
        const bool made = parse_test_write(source, text, strlen(text));
        char* arguments[] = { (char*)clang, standard, "-fsyntax-only", "-fno-caret-diagnostics", source, NULL, NULL };
        char* expected = NULL;
        const int oracle = made ? check_test_driver_spawn(arguments, &expected) : -1;
        const bool lines = expected != NULL && check_test_diagnostic_lines(expected, expected_lines, LINES);
        char description[4096];
        snprintf(description, sizeof(description), "type tags %u: clang's lines read", unit);
        check_test_expect(test, made && lines, description);
        for (uint32_t scalar = 0; lines && scalar < 2; scalar += 1)
        {
            arguments[0] = (char*)executable;
            arguments[5] = scalar != 0 ? "--scalar" : NULL;
            char* actual = NULL;
            const int status = check_test_driver_spawn(arguments, &actual);
            const bool same = actual != NULL && check_test_diagnostic_lines(actual, actual_lines, LINES) && strcmp(expected_lines, actual_lines) == 0;
            snprintf(description, sizeof(description), "type tags %u (%s %s), %s: status %d (clang %d), lines\n%s(clang\n%s)", unit, standard, text,
                     scalar != 0 ? "scalar" : "vector", status, oracle, actual_lines, expected_lines);
            check_test_expect(test, same && (status != 0) == (oracle != 0), description);
            free(actual);
        }
        free(expected);
    }
    free(expected_lines);
    free(actual_lines);
    unlink(source);
}
