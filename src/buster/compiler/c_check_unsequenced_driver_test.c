// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original sources for clang's -Wunsequenced (#1953, SequenceChecker, c_check_unsequenced.c): unsequenced modifications
// and reads of a variable, on by default under -Wsequence-point. The shared warning harness compares the pinned Clang's
// complete diagnostics through both kernels and requested lanes. Include after c_check_warning_driver_test.c
// (check_test_driver_warning_fixtures).

// Every operator's sequencing: assignments, increments, calls, commas, logical operators and conditionals with and
// without constant first operands, members and elements (no variables), GNU conditionals, and brace initializers, each
// list one full expression (a range's value once per element).
static const char check_driver_unsequenced_operators_source[] =
    "int g(int, int);\n"
    "struct S { int m; } s;\n"
    "void f(int c, int x, int y, int *p, int a[], struct S *ps) {\n"
    "    x = x++; x = ++x; x++ + x; x + (x = 1); g(x++, x); g(x++, x++); x = g(x++, 0);\n"
    "    (x++, x++); x++ && x++; y = (x, x++) + x; a[x] = x++; p[x++] = x; x += x++;\n"
    "    c ? x++ : x++; s.m = s.m++; ps->m = ps->m++; *p = (*p)++; (y = 1) + (y = 2);\n"
    "    x = (2, x++); x = 1 && x++; x = 0 && x++; x = 1 ? x++ : 0; x = 0 ? x++ : 0;\n"
    "    y = c ?: x++ + x; y = (c ? x : y) + x++; x = (x += 1) + x;\n"
    "}\n"
    "void h(int c, int x, int y, int z) {\n"
    "    int b[2] = { x++, x++ }; int d[2] = { x, x++ }; int e[3] = { [0 ... 2] = x++ };\n"
    "    int k[2][2] = { { y++ }, { y++ } }; int m = x++ + x++, n = z++;\n"
    "    (void)b; (void)d; (void)e; (void)k; (void)m; (void)n;\n"
    "}\n";

// Where it warns: a conditional's arm in an assignment's value (#1953's), a macro's argument, statement expressions'
// values and statements, unevaluated operands and builtins, loop headers and conditions, a pragma's region, beside
// constant conversions, and none in unreachable code.
static const char check_driver_unsequenced_contexts_source[] =
    "#define INC(v) v++\n"
    "int g(int);\n"
    "void f(int c, int x, int y, signed char sc, _Bool b) {\n"
    "    if (0) { x = x++; }\n"
    "    sc = c ? (sc = 300) : 400;\n"
    "    b = c ? (b = c ? 5 : 6) : 7;\n"
    "    sc = c ? (c ? (sc = 300) : 301) : (sc = 302, 303);\n"
    "    x = INC(x);\n"
    "    y = ({ x++ + x++; });\n"
    "    y = ({ x; }) + x++;\n"
    "    y = __builtin_constant_p(x++) + x++;\n"
    "    y = _Generic(x++, int: x) + x++;\n"
    "    y = sizeof(x++) + x++;\n"
    "    x = __builtin_expect(x++, 1) + x;\n"
    "    for (int i = 0; i < 10; i = i++) {}\n"
    "    while ((x = x++)) {}\n"
    "    #pragma clang diagnostic push\n"
    "    #pragma clang diagnostic ignored \"-Wunsequenced\"\n"
    "    y = y++;\n"
    "    #pragma clang diagnostic pop\n"
    "    y = y++ + (char)300;\n"
    "}\n"
    "void h(int y)\n"
    "{\n"
    "    y = y + 1;\n"
    "    return;\n"
    "    y = y++;\n"
    "}\n";

static const CheckDriverDefaultWarningFixture check_driver_unsequenced_fixtures[] =
{
    {
        .name = "unsequenced modifications and reads by operator",
        .source = check_driver_unsequenced_operators_source,
        .warnings = 27,
        // (-c can't compile a range designator.)
        .syntax_only = true,
    },
    {
        .name = "unsequenced modifications and reads by operator in C23 under -Wall -Wextra",
        .source = check_driver_unsequenced_operators_source,
        .options = { "-Wall", "-Wextra" },
        .warnings = 28,
        .c23 = true,
        .syntax_only = true,
    },
    {
        .name = "-Wno-sequence-point holds -Wunsequenced",
        .source = check_driver_unsequenced_operators_source,
        .options = { "-Wno-sequence-point" },
        .warnings = 5,
        .syntax_only = true,
    },
    {
        .name = "-Werror=unsequenced",
        .source = check_driver_unsequenced_operators_source,
        .options = { "-Werror=unsequenced" },
        .warnings = 5,
        .errors = 20,
        .syntax_only = true,
    },
    {
        .name = "unsequenced accesses by context",
        .source = check_driver_unsequenced_contexts_source,
        .warnings = 19,
        .notes = 1,
    },
    {
        .name = "unsequenced accesses by context in C23 under -Wall -Wextra",
        .source = check_driver_unsequenced_contexts_source,
        .options = { "-Wall", "-Wextra" },
        .warnings = 19,
        .notes = 1,
        .c23 = true,
    },
    {
        .name = "-Wunsequenced alone",
        .source = check_driver_unsequenced_contexts_source,
        .options = { "-Wno-everything", "-Wunsequenced" },
        .warnings = 10,
        .notes = 1,
    },
};

static void check_test_driver_unsequenced(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_unsequenced_fixtures,
                                      BUSTER_ARRAY_LENGTH(check_driver_unsequenced_fixtures));
}
