// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original sources for clang's -Wunevaluated-expression (#1447, CheckUnaryExprOrTypeTraitOperand and
// CreateGenericSelectionExpr, c_check_warning_graph.c's check_warning_graph_operand): an unevaluated sizeof, alignof or
// _Generic operand with a side effect, on by default under -Wunused-value. The shared warning harness compares the
// pinned Clang's complete diagnostics through both kernels and requested lanes. Include after
// c_check_warning_driver_test.c (check_test_driver_warning_fixtures).

// The operators: assignments, increments, atomic builtins and a call's arguments, through every expression HasSideEffects
// reads, and the operands it doesn't (a type, a nested sizeof, a selection's other arms, a variable array's operand).
static const char check_driver_unevaluated_operators_source[] =
    "int g(int); int pf(void) __attribute__((pure)); int cf(int) __attribute__((const));\n"
    "struct S { int m; } s, *ps;\n"
    "void f(int i, int *p, volatile int v, __builtin_va_list ap, _Atomic int *ai) {\n"
    "    (void)sizeof(i++); (void)sizeof(i = 1); (void)sizeof(g(i)); (void)sizeof(pf()); (void)sizeof(cf(i++));\n"
    "    (void)sizeof(v); (void)sizeof(--i); (void)sizeof(i += 2); (void)sizeof i++; (void)sizeof (i++);\n"
    "    (void)_Alignof(i++); (void)__alignof__(i <<= 1); (void)_Generic(i++, int: 1); (void)_Generic(i, int: 1);\n"
    "    (void)sizeof(int[i++]); int a[i]; (void)sizeof(a[i++]); int b[i][i]; (void)sizeof(b[i++]);\n"
    "    (void)sizeof(*p = 2); (void)sizeof(i += 2, 3); (void)sizeof(__builtin_constant_p(i++));\n"
    "    (void)sizeof(i ? i++ : 0); (void)sizeof(i && i++); (void)sizeof(g(i = 1)); (void)sizeof(ps->m++); (void)sizeof(s.m = 1);\n"
    "    (void)sizeof(p[i++]); (void)sizeof(_Generic(0, int: i++)); (void)sizeof(_Generic(i++, int: 1));\n"
    "    (void)sizeof(__builtin_choose_expr(1, i++, 0)); (void)sizeof(__builtin_choose_expr(0, i++, 0));\n"
    "    (void)sizeof((char){i++}); (void)sizeof(__extension__ (i++)); (void)sizeof(-i++); (void)sizeof((long)i++);\n"
    "    (void)sizeof(__real__ i++); (void)sizeof(sizeof(i++));\n"
    "    (void)sizeof(__c11_atomic_load(ai, 5)); (void)sizeof(__atomic_load_n(p, 5));\n"
    "    (void)sizeof(__sync_fetch_and_add(p, 1)); (void)sizeof(__atomic_thread_fence(5), 0);\n"
    "}\n";

// GNU statement expressions: their statements as SideEffectFinder visits them.
static const char check_driver_unevaluated_statements_source[] =
    "int g(int);\n"
    "void f(int i) {\n"
    "    (void)sizeof(({ int x = 1; x; })); (void)sizeof(({ int x; x; })); (void)sizeof(({ g(i); 1; }));\n"
    "    (void)sizeof(({ __builtin_constant_p(i++); })); (void)sizeof(({ (void)__builtin_constant_p(i++); 1; }));\n"
    "    (void)sizeof(({ int y = i++; y; })); (void)sizeof(({ int y[i++]; 1; })); (void)sizeof(({ if (i) i = 2; 1; }));\n"
    "    (void)sizeof(({ sizeof(i++); })); (void)sizeof(({ return; 1; })); (void)sizeof(({ i++; }) + 1);\n"
    "    (void)sizeof(({ (void)({ i = 2; 1; }); 1; })); (void)_Alignof(({ i = 1; 1; }));\n"
    "    (void)sizeof(({ _Generic(0, int: 1, long: i++); })); (void)sizeof(({ _Generic(0, long: 1, int: i++); }));\n"
    "    (void)sizeof(({ struct { int m; } t = { i++ }; t.m; }));\n"
    "    (void)sizeof(({ if (__builtin_constant_p(i++)) {} 1; })); (void)sizeof(({ (__builtin_constant_p(i++)); 1; }));\n"
    "    typeof(i++) t1 = 0; (void)t1;\n"
    "}\n";

// Macros: an expression whose location a macro wrote, its argument's too, is no side effect.
static const char check_driver_unevaluated_macros_source[] =
    "#define ID(x) x\n"
    "#define SZ(x) sizeof(x)\n"
    "#define INC(x) x++\n"
    "#define PLUS +\n"
    "int g(int);\n"
    "void f(int i, int *p) {\n"
    "    (void)sizeof(ID(i++)); (void)sizeof(INC(i)); (void)SZ(i++); (void)sizeof(ID((i++)));\n"
    "    (void)sizeof(i = ID(1)); (void)sizeof(ID(i) = 1); (void)sizeof(i ID(=) 1); (void)sizeof(ID(i)++); (void)sizeof(++ID(i));\n"
    "    (void)sizeof(ID(++)i); (void)sizeof(i ID(++)); (void)sizeof(ID(g)(i++)); (void)sizeof(g(ID(i)++));\n"
    "    (void)sizeof(i++ PLUS 1); (void)sizeof(1 PLUS i++); (void)sizeof(ID(1) + i++); (void)sizeof(i ? ID(1) : i++);\n"
    "    (void)sizeof(ID(i) ? 1 : i++); (void)sizeof((int)ID(i++)); (void)sizeof(ID((int))i++);\n"
    "    (void)_Generic(ID(i++), int: 1); (void)sizeof(({ ID(i++); 1; }));\n"
    "}\n";

static const CheckDriverDefaultWarningFixture check_driver_unevaluated_fixtures[] =
{
    {
        .name = "side effects in unevaluated operands",
        .source = check_driver_unevaluated_operators_source,
        .warnings = 32,
    },
    {
        .name = "side effects in unevaluated operands in C23",
        .source = check_driver_unevaluated_operators_source,
        .warnings = 32,
        .c23 = true,
    },
    {
        .name = "side effects in unevaluated operands under -Wall -Wextra",
        .source = check_driver_unevaluated_operators_source,
        .options = { "-Wall", "-Wextra" },
        .warnings = 33,
    },
    {
        .name = "-Wno-unevaluated-expression",
        .source = check_driver_unevaluated_operators_source,
        .options = { "-Wno-unevaluated-expression" },
        .warnings = 1,
    },
    {
        .name = "-Wno-unused-value holds -Wunevaluated-expression",
        .source = check_driver_unevaluated_operators_source,
        .options = { "-Wno-unused-value" },
        .warnings = 1,
    },
    {
        .name = "side effects in statement expressions in unevaluated operands",
        .source = check_driver_unevaluated_statements_source,
        .warnings = 12,
    },
    {
        .name = "side effects in statement expressions under -Wall",
        .source = check_driver_unevaluated_statements_source,
        .options = { "-Wall" },
        .warnings = 13,
    },
    {
        .name = "side effects in statement expressions under -Wall -Wextra in C23",
        .source = check_driver_unevaluated_statements_source,
        .options = { "-Wall", "-Wextra" },
        .warnings = 13,
        .c23 = true,
    },
    {
        .name = "side effects macros write",
        .source = check_driver_unevaluated_macros_source,
        .warnings = 7,
    },
    {
        .name = "side effects macros write under -Wall -Wextra",
        .source = check_driver_unevaluated_macros_source,
        .options = { "-Wall", "-Wextra" },
        .warnings = 8,
    },
    {
        .name = "-Werror=unevaluated-expression",
        .source = check_driver_unevaluated_macros_source,
        .options = { "-Werror=unevaluated-expression" },
        .errors = 7,
    },
};

static void check_test_driver_unevaluated(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_unevaluated_fixtures,
                                      BUSTER_ARRAY_LENGTH(check_driver_unevaluated_fixtures));
}
