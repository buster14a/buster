// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// A discarded call's result (#1447, c_check_ignored_result.c): original fixtures whose whole diagnostic output is compared
// with clang's on both paths, at each warning policy and with the options that turn the warnings off. Include after
// c_check_warning_driver_test.c (check_test_driver_warning_fixtures).

static const char check_driver_unused_result_functions[] =
    "__attribute__((const)) int c(int);\n"
    "__attribute__((pure)) int p(int);\n"
    "__attribute__((warn_unused_result)) int w(int);\n"
    "[[nodiscard]] int d(int);\n"
    "[[nodiscard(\"because\")]] int dm(int);\n"
    "__attribute__((warn_unused_result(\"why\"))) int wm(int);\n"
    "[[gnu::warn_unused_result]] int gw(int);\n"
    "__attribute__((const, warn_unused_result)) int cw(int);\n"
    "int e(int);\n"
    "int g(int n) {\n"
    "  c(n); p(n); w(n); d(n); dm(n); wm(n); gw(n); cw(n); e(n);\n"
    "  (c)(n); (void)w(n); (void)c(n);\n"
    "  c(n), 0; n, c(n); c(n), p(n);\n"
    "  n ? c(n) : p(n); n ? c(n) : e(n); n ? e(n) : p(n); n ? c(n) : 0;\n"
    "  for (c(n); n; p(n)) break;\n"
    "  if (n) c(n); else w(n);\n"
    "  return c(n), w(n);\n"
    "}\n";

static const CheckDriverDefaultWarningFixture check_driver_unused_result_fixtures[] =
{
    {
        // Each spelling, its message (a standard nodiscard's alone) and its flag; warn_unused_result before const; a void
        // cast; a statement's, a comma's operands', a conditional's arms' (both must warn, the else arm's reported), a for
        // clause's, an if's and a return's comma's.
        .name = "ignored results of attributed functions",
        .source = check_driver_unused_result_functions,
        .warnings = 21,
        .c23 = true,
    },
    {
        .name = "ignored results without -Wunused-value",
        .source = check_driver_unused_result_functions,
        .options = { "-Wno-unused-value" },
        .c23 = true,
    },
    {
        .name = "ignored results without -Wunused-result",
        .source = check_driver_unused_result_functions,
        .options = { "-Wno-unused-result" },
        .warnings = 14,
        .c23 = true,
    },
    {
        .name = "ignored results without -Wunused",
        .source = check_driver_unused_result_functions,
        .options = { "-Wno-unused" },
        .c23 = true,
    },
    {
        .name = "ignored results without -Wall",
        .source = check_driver_unused_result_functions,
        .options = { "-Wno-all" },
        .c23 = true,
    },
    {
        .name = "ignored results with -Wunused-result alone",
        .source = check_driver_unused_result_functions,
        .options = { "-Wno-unused", "-Wunused-result" },
        .warnings = 7,
        .c23 = true,
    },
    {
        // A pure or const call written in a macro's body is quiet, a warn_unused_result one's isn't and notes the
        // expansion; a call whose argument a macro writes is the file's.
        .name = "ignored results through macros",
        .source = "__attribute__((const)) int c(int);\n"
                  "__attribute__((pure)) int p(int);\n"
                  "__attribute__((warn_unused_result)) int w(int);\n"
                  "#define CALLC(x) c(x)\n"
                  "#define CALLP(x) p(x)\n"
                  "#define CALLW(x) w(x)\n"
                  "#define ID(x) x\n"
                  "#define STATEMENT(x) c(x);\n"
                  "void f(int n) {\n"
                  "  CALLC(n); CALLP(n); CALLW(n); STATEMENT(n)\n"
                  "  c(ID(n)); w(ID(n));\n"
                  "}\n",
        .warnings = 3,
        .notes = 1,
    },
    {
        // A return type's struct, union, enum or typedef (through another typedef), the tag before the typedef; not a
        // pointer to one; the function's own attribute first.
        .name = "ignored results of nodiscard types",
        .source = "struct [[nodiscard]] S { int a; };\n"
                  "struct [[nodiscard(\"keep it\")]] M { int a; };\n"
                  "union __attribute__((warn_unused_result)) U { int a; };\n"
                  "enum [[nodiscard]] E { A };\n"
                  "typedef int __attribute__((warn_unused_result)) WI;\n"
                  "typedef WI WJ;\n"
                  "typedef struct S TS;\n"
                  "struct S mk(void);\n"
                  "struct M mm(void);\n"
                  "union U mu(void);\n"
                  "enum E me(void);\n"
                  "WI mwi(void);\n"
                  "WJ mwj(void);\n"
                  "TS mts(void);\n"
                  "struct S *ps(void);\n"
                  "[[nodiscard]] struct S both(void);\n"
                  "void f(void) {\n"
                  "  mk(); mm(); mu(); me(); mwi(); mwj(); mts(); ps(); both(); (void)mk();\n"
                  "}\n",
        .warnings = 8,
        .c23 = true,
    },
    {
        // A redeclaration inherits the attributes before it; a call reads those its lookup sees.
        .name = "ignored results through redeclarations",
        .source = "int h(int);\n"
                  "int h(int) __attribute__((const));\n"
                  "int k(int) __attribute__((pure));\n"
                  "int k(int);\n"
                  "int m(int);\n"
                  "void before(int n) { m(n); }\n"
                  "int m(int) __attribute__((warn_unused_result));\n"
                  "void after(int n) { h(n); k(n); m(n); }\n",
        .warnings = 3,
    },
    {
        // Clang's builtin table's const and pure (a g row's const too, an e row's not under -fmath-errno), a library
        // function's where the unit keeps it a builtin and declares it external; a query builtin's constant and a
        // selection's chosen operand.
        .name = "ignored results of builtins",
        .source = "int abs(int);\n"
                  "double rint(double);\n"
                  "double sin(double);\n"
                  "static long labs(long x) { return x; }\n"
                  "struct S { int a; };\n"
                  "void f(int n, double d, const char *s) {\n"
                  "  __builtin_expect(n, 1); __builtin_clz(n); __builtin_popcount(n); __builtin_constant_p(n); __builtin_classify_type(n);\n"
                  "  __builtin_abs(n); __builtin_fabs(d); __builtin_rint(d); __builtin_sin(d); __builtin_nan(\"\"); __builtin_isnan(d);\n"
                  "  __builtin_strlen(s); __builtin_bswap32(n);\n"
                  "  abs(n); rint(d); sin(d); labs(n);\n"
                  "  __builtin_offsetof(struct S, a); __builtin_types_compatible_p(int, long);\n"
                  "  _Generic(n, int: n, default: 0); _Generic(n, int: abs(n), default: 0); __builtin_choose_expr(1, abs(n), 0);\n"
                  "}\n",
        .warnings = 18,
    },
    {
        // A statement expression in sizeof's operand is unevaluated: its calls' results are clang's to drop quietly.
        .name = "ignored results in an unevaluated statement expression",
        .source = "__attribute__((const)) int c(int);\n"
                  "__attribute__((warn_unused_result)) int w(int);\n"
                  "void f(int n) {\n"
                  "  (void)sizeof(({ c(n); w(n); 1; }));\n"
                  "  int a[n];\n"
                  "  (void)sizeof(a[c(n)]);\n"
                  "  ({ c(n); 1; });\n"
                  "}\n",
        .warnings = 2,
    },
    {
        .name = "ignored results of library functions without builtins",
        .source = "int abs(int);\n"
                  "double rint(double);\n"
                  "void f(int n, double d) {\n"
                  "  abs(n); rint(d); __builtin_rint(d);\n"
                  "}\n",
        .options = { "-fno-builtin" },
        .warnings = 1,
    },
};

static void check_test_driver_unused_results(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_unused_result_fixtures,
                                       BUSTER_ARRAY_LENGTH(check_driver_unused_result_fixtures));
}
