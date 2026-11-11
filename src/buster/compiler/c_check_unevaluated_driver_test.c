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

// The operands the constant reader reads (#1447, check_warning_graph_constant_queries): array bounds at file scope, in a
// record and in a block, an enumerator's value, a `_Generic` in a bound, beside a static assertion's and an initializer's.
static const char check_driver_unevaluated_constant_source[] =
    "int x;\n"
                  "int arr[sizeof(x++)];\n"
                  "enum { E = sizeof(x = 1) };\n"
                  "_Static_assert(sizeof(x++) == 4, \"s\");\n"
                  "int y = sizeof(x--);\n"
                  "struct S { int a[sizeof(++x)]; };\n"
                  "int f(void) { int v[sizeof(x++)]; return sizeof v + _Alignof(int) + (int)sizeof(typeof(x++)); }\n"
                  "int g = _Generic(x++, int: 1);\n"
                  "char c[_Generic(x = 2, int: 3)];\n";

// Its forms: type names and a designator's `=` (none), sizeof without parentheses, alignof, a nested sizeof (none), a cast,
// a comma, a call (none), a macro's increment (none), a subscript, a bit-field's width, a case label and _Alignas.
static const char check_driver_unevaluated_constant_forms_source[] =
    "int x; int arr2[4];\n"
                  "typedef int T;\n"
                  "struct S { int a; int b[2]; };\n"
                  "int f(void);\n"
                  "#define INC(v) v++\n"
                  "int a1[sizeof(int)];\n"
                  "int a2[sizeof(T)];\n"
                  "int a3[sizeof((struct S){ .a = 1, .b[1] = 2 })];\n"
                  "int a4[sizeof x++];\n"
                  "int a5[__alignof__(x = 3)];\n"
                  "int a6[sizeof(sizeof(x++))];\n"
                  "int a7[sizeof((int)(x = 1))];\n"
                  "int a8[sizeof(x, x++)];\n"
                  "int a9[sizeof(f())];\n"
                  "int a10[sizeof(INC(x))];\n"
                  "int a11[sizeof(arr2[x++])];\n"
                  "struct B { int w : sizeof(x++); };\n"
                  "enum { E1 = _Generic(x++, int: 1), E2 = sizeof(x += 2) };\n"
                  "int a12[sizeof(x ? x-- : 0)];\n"
                  "int a13[sizeof(T[2])];\n"
                  "void g(void) { switch (x) { case sizeof(x++): break; } _Alignas(sizeof(x++)) int z; (void)z; }\n";

static const CheckDriverDefaultWarningFixture check_driver_unevaluated_fixtures[] =
{
    {
        .name = "side effects in the constant reader's unevaluated operands",
        .source = check_driver_unevaluated_constant_source,
        .warnings = 8,
    },
    {
        .name = "side effects in the constant reader's unevaluated operands: forms",
        .source = check_driver_unevaluated_constant_forms_source,
        .warnings = 12,
    },
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
