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

// Discarded conditionals, commas and selections (clang's isUnusedResultAWarning, check_common_unused_of).
static const char check_driver_unused_result_conditionals[] =
    "__attribute__((warn_unused_result)) int w(int);\n"
    "__attribute__((warn_unused_result)) long wl(int);\n"
    "__attribute__((const)) int c(int);\n"
    "int e(int);\n"
    "void f(void);\n"
    "void g(void);\n"
    "struct S { int a; } s;\n"
    "void h(int n) {\n"
    "  n ? (f(), 0) : (g(), 1); n ? (f(), 1) : (g(), (0)); n ? (n ? (f(), 0) : (g(), 0)) : (g(), 0);\n"
    "  (n ? (f(), 0) : (g(), 0)), n; (n ? c(n) : c(n)), 0; 0, (n ? c(n) : e(n));\n"
    "  n ? (0, w(n)) : (n, (0)); n ? (n, (n, w(n))) : w(n); n ? (n ? 1 : 2) : (n ? 3 : f());\n"
    "  n, (n, w(n)); (n, w(n)), n; n, (n ? (f(), 0) : (g(), 0));\n"
    "  n ?: w(n); e(n) ?: 0; w(n) ?: w(n);\n"
    "  n ? wl(n) : w(n); n ? w(n) : e(n); n ? w(n) : n + 1; n ? n + 1 : w(n); n ? c(n) : (n, 0); n ? s.a : s.a;\n"
    "  _Generic(n, int: (n ? w(n) : w(n))); _Generic(n, int: (f(), 0)); _Generic(n, int: (n ? 1 : 2)); n ? _Generic(n, int: w(n)) : c(n);\n"
    "  for (n ? w(n) : w(n); n; n ? c(n) : c(n)) { }\n"
    "}\n";

// A discarded call a macro's argument writes (#1447): clang highlights the call, which spans more than the argument's one
// token, so each macro whose argument it is gets an "expanded from macro" note, a nested one's in the order clang gives
// them (Preprocessor.diagnostic_spans).
static const char check_driver_unused_result_macro_arguments[] =
    "__attribute__((pure)) int p(int);\n"
    "__attribute__((const)) int c(int);\n"
    "__attribute__((warn_unused_result)) int w(int);\n"
    "struct [[nodiscard]] R { int x; };\n"
    "struct R r(int);\n"
    "#define ID(x) x\n"
    "#define STMT(x) do { x; } while (0)\n"
    "#define CALL(f) f(1)\n"
    "#define TWO(x) ID(x)\n"
    "#define ARG(x) p(x)\n"
    "void g(int n) {\n"
    "    ID(p(n)); ID(c(n)); ID(w(n)); ID(r(n));\n"
    "    STMT(p(n)); STMT(w(n));\n"
    "    CALL(p); CALL(w);\n"
    "    TWO(p(n)); TWO(w(n));\n"
    "    ARG(n);\n"
    "    ID((p)(n));\n"
    "    ID(n ? p(n) : c(n));\n"
    "}\n";

static const CheckDriverDefaultWarningFixture check_driver_unused_result_fixtures[] =
{
    {
        .name = "ignored results of calls macros' arguments write",
        .source = check_driver_unused_result_macro_arguments,
        .warnings = 12,
        .notes = 14,
        .c23 = true,
    },
    {
        .name = "ignored results of calls macros' arguments write under -Wall -Wextra",
        .source = check_driver_unused_result_macro_arguments,
        .options = { "-Wall", "-Wextra" },
        .warnings = 12,
        .notes = 14,
        .c23 = true,
    },
    {
        .name = "ignored results of calls macros' arguments write without -Wunused-result",
        .source = check_driver_unused_result_macro_arguments,
        .options = { "-Wno-unused-result" },
        .warnings = 8,
        .notes = 9,
        .c23 = true,
    },
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
        // An initializer's values (scalar and braced, designated, nested and a compound literal's) are read, not discarded,
        // though a declaration's `;` casts each to void as a root of its own; a statement's call after them still warns.
        .name = "initializers' calls keep their results",
        .source = "typedef struct { const char* p; unsigned long n; } S;\n"
                  "extern unsigned long strlen(const char*) __attribute__((__pure__));\n"
                  "__attribute__((const)) int c(int);\n"
                  "__attribute__((warn_unused_result)) int w(int);\n"
                  "unsigned long f(const char* x) { S s = { .p = x, .n = __builtin_strlen(x) }; return s.n; }\n"
                  "unsigned long g(const char* x) { const S s = { .p = x, .n = strlen(x), }; return s.n; }\n"
                  "unsigned long h(const char* x) { S s = { x, strlen(x) }; S t[1] = { { x, strlen(x) } }; return s.n + t[0].n; }\n"
                  "int k(int n) { int a = c(n); int b[2] = { c(n), w(n) }; int d = { c(n) }; return a + b[0] + d; }\n"
                  "unsigned long m(const char* x) { return ((S){ .p = x, .n = strlen(x) }).n; }\n"
                  "unsigned long q(const char* x) { S s; s = (S){ x, strlen(x) }; return s.n; }\n"
                  "static int z[2] = { sizeof(int), 1 };\n"
                  "int y(int n) { c(n); strlen(\"\"); return z[0]; }\n",
        .warnings = 2,
    },
    {
        // A conditional whose arms hide their results behind the `(x, 0)` idiom is quiet (clang's isUnusedResultAWarning).
        .name = "ignored results: a conditional of hidden results",
        .source = "void f(void); void g(void);\n"
                  "void h(int n) { n ? (f(), 0) : (g(), 0); }\n",
    },
    {
        // A comma's left operand or a discarded comma's value warns of the call the conditional leaves, the else arm's.
        .name = "ignored results: a comma's conditional operand",
        .source = "__attribute__((warn_unused_result)) int w(int);\n"
                  "void f(int n) { (n ? w(n) : w(n)), 0; }\n"
                  "void f2(int n) { 0, (n ? w(n) : w(n)); }\n",
        .warnings = 3,
    },
    {
        // A conditional's arms warn through their commas and their own conditionals.
        .name = "ignored results: a conditional's comma and conditional arms",
        .source = "__attribute__((warn_unused_result)) int w(int);\n"
                  "void f(int n) { n ? (n, w(n)) : (n, w(n)); }\n"
                  "void f2(int n) { n ? (n ? w(n) : w(n)) : w(n); }\n",
        .warnings = 4,
    },
    {
        // A call through a pointer warns of the typedef its declarator spells as the return type.
        .name = "ignored results: a call through a pointer",
        .source = "typedef int __attribute__((warn_unused_result)) WI;\n"
                  "WI (*fp)(void);\n"
                  "void f(void) { fp(); }\n",
        .warnings = 1,
    },
    {
        // The `(x, 0)` idiom in one arm, parenthesized, nested; commas of commas; GNU `?:`, whose else arm alone counts;
        // an arm of no effect, of a plain call, of another type; a member; selections of conditionals and commas; a for
        // clause's conditionals.
        .name = "ignored results through conditionals, commas and selections",
        .source = check_driver_unused_result_conditionals,
        .warnings = 28,
    },
    {
        .name = "ignored results through conditionals without -Wunused-result",
        .source = check_driver_unused_result_conditionals,
        .options = { "-Wno-unused-result" },
        .warnings = 19,
    },
    {
        // A pointer object's and a parameter's declarator, a pointer typedef, a function typedef's pointer, a typedef of the
        // typedef; not a pointer the call returns; a tag; a callee through dereferences.
        .name = "ignored results of calls through pointers",
        .source = "typedef int __attribute__((warn_unused_result)) WI;\n"
                  "typedef WI WJ;\n"
                  "typedef WI (*FP)(void);\n"
                  "typedef WI FN(void);\n"
                  "struct __attribute__((warn_unused_result)) S { int a; };\n"
                  "__attribute__((warn_unused_result)) int w(int);\n"
                  "__attribute__((const)) int c(int);\n"
                  "WI (*fp)(void);\n"
                  "WJ (*fj)(void);\n"
                  "WI *(*fpp)(void);\n"
                  "WI *pw(void);\n"
                  "FP fq;\n"
                  "FN *fn;\n"
                  "struct S (*sp)(void);\n"
                  "int (*plain)(void);\n"
                  "void f(WI (*cb)(void), FP cb2) {\n"
                  "  fp(); (*fp)(); (fp)(); fj(); fpp(); pw(); fq(); fn(); sp(); plain(); cb(); cb2(); (*w)(1); (**c)(1);\n"
                  "}\n",
        .warnings = 11,
    },
    {
        // A pure or const call is quiet where the discarded expression itself is a macro body's: a call's callee, a comma's
        // operator, a conditional's first token; a warn_unused_result call warns anyway.
        .name = "ignored results of conditionals and commas through macros",
        .source = "__attribute__((const)) int c(int);\n"
                  "__attribute__((warn_unused_result)) int w(int);\n"
                  "#define C c\n"
                  "#define W w\n"
                  "#define PICK(n) ((n) ? c(n) : c(n))\n"
                  "#define COMMA(n) ((void)0, c(n))\n"
                  "#define CO ,\n"
                  "#define N n\n"
                  "#define Q ?\n"
                  "void f(int n) {\n"
                  "  n ? C(n) : C(n); n, C(n); C(n), n; C(n);\n"
                  "  PICK(n); COMMA(n); n CO c(n); N ? c(n) : c(n); n Q c(n) : c(n);\n"
                  "  n ? W(n) : W(n); N ? w(n) : w(n);\n"
                  "}\n",
        .warnings = 8,
        .notes = 3,
    },
    {
        // A conditional in a comma's result chain warns only where both arms do; a conditional's comma, conditional and
        // selection arms; a selection's comma and conditional; a statement's comma, conditional and selection in
        // unreachable code, a dead for clause's, a constant condition's other arm's and a short-circuited side's comma;
        // a call's report at its callee, through the parentheses of the callee alone, a dereference and an address.
        .name = "ignored results through nested values, unreachable code and callee groups",
        .source = "int e(int);\n"
                  "__attribute__((warn_unused_result)) int w(int);\n"
                  "__attribute__((pure)) int q(int);\n"
                  "int a, x, m;\n"
                  "void f(int n) {\n"
                  "  (a, n ? e(n) : w(n)), x; x, (a, n ? e(n) : w(n));\n"
                  "  n ? w(n) : (x, w(n)); n ? (x, w(n)) : w(n); n ? w(n) : (m ? w(n) : w(n)); n ? w(n) : _Generic(n, int: w(n));\n"
                  "  n ? w(n) : (x, q(n)); n ? q(n) : _Generic(n, int: q(n));\n"
                  "  _Generic(n, int: (x, w(n)), default: 0); __builtin_choose_expr(1, (x, w(n)), 0); _Generic(n, int: (n ? w(n) : w(n)), default: 0);\n"
                  "}\n"
                  "int g(int n) {\n"
                  "  if (n) { return 0; n ? w(n) : w(n); n, w(n); (n ? w(n) : w(n)), 0; w(n), 0; _Generic(n, int: w(n)); (n, w(n)), n; }\n"
                  "  for (;; n ? w(n) : w(n)) break;\n"
                  "  if (0) { n ? w(n) : w(n); }\n"
                  "  return 1 ? 0 : (w(n), 1);\n"
                  "}\n"
                  "int h(int n) { return 0 && (w(n), 1); }\n"
                  "void k(int n) {\n"
                  "  (w(n)); x = (w(n), 5); (w)(n); ((w))(n); ((w)(n)); (*w)(n); (**w)(n); (&w)(n);\n"
                  "}\n",
        .warnings = 36,
    },
    {
        // A place's and a type query's builtin constant, discarded, reports at its keyword, in a conditional's arm, a
        // comma's operand and a selection too.
        .name = "unused values of keyword builtins",
        .source = "typedef int v4 __attribute__((vector_size(16)));\n"
                  "struct S { int a; };\n"
                  "void f(int n) {\n"
                  "  __builtin_FILE(); __builtin_LINE(); __builtin_FUNCTION(); __builtin_COLUMN(); __builtin_FILE_NAME();\n"
                  "  __builtin_vectorelements(v4); __is_destructible(int); __is_bitwise_cloneable(int); __is_nothrow_destructible(int);\n"
                  "  n ? __builtin_LINE() : __builtin_LINE(); n, __builtin_FILE(); __builtin_LINE(), n;\n"
                  "  n ? __builtin_offsetof(struct S, a) : __builtin_offsetof(struct S, a); _Generic(n, int: __builtin_LINE()); _Generic(n, int: (n, __builtin_LINE()));\n"
                  "}\n",
        .warnings = 18,
    },
    {
        // An unevaluated operand's (sizeof's, typeof's, _Alignof's, a _Generic's controlling expression) commas and
        // conditionals are quiet; a _Generic's other association, a short-circuited side, a statement expression's
        // unreachable statement and a constant condition's other arm are clang's to warn of.
        .name = "ignored results in unevaluated operands and dead code",
        .source = "__attribute__((warn_unused_result)) int w(int);\n"
                  "__attribute__((const)) int c(int);\n"
                  "void f(int n) {\n"
                  "  (void)sizeof((w(1), 1)); __typeof__((w(1), 1)) y = 0; (void)_Alignof(__typeof__((w(1), 1))); (void)y;\n"
                  "  (void)_Generic(n, int: 0, default: (w(1), 1)); (void)_Generic((w(1), n), int: 0);\n"
                  "  (void)sizeof((n ? w(1) : w(2), 1));\n"
                  "  (void)sizeof(({ n ? w(1) : w(2); 1; }));\n"
                  "  (void)(0 && (c(1), 1)); (void)(1 || (n, c(1)));\n"
                  "  ({ return; n ? w(1) : w(2); });\n"
                  "}\n"
                  "static int g(int n) { return 0 ? (c(n), 1) : 0; }\n"
                  "int h(int n) { return g(n); }\n",
        .warnings = 4,
    },
    {
        // Logical operators among conditionals and commas (#1390): a logical operator's value warns where both operands
        // do, at the operator, and a conditional's where both arms' do; a call whose result warns makes its logical operand
        // warn (a tag's contract, a g row's const), a plain one, an enum's, an e row's or a pointer's doesn't; a statement
        // expression's final value; a volatile object's read accepts the discard everywhere.
        .name = "ignored results among logical operators",
        .source = "__attribute__((warn_unused_result)) int w(int);\n"
                  "__attribute__((const)) int c(int);\n"
                  "int e(int);\n"
                  "enum E { A, B };\n"
                  "enum E g(void);\n"
                  "enum __attribute__((warn_unused_result)) F { C };\n"
                  "enum F h(void);\n"
                  "double sin(double);\n"
                  "double rint(double);\n"
                  "int (*fp)(int);\n"
                  "void f(int n, int a, int b, int x, double d, volatile int *p) {\n"
                  "  n ? (a && w(n)) : (b || w(n)); n ? (a && e(n)) : (b || w(n)); (x && 0), w(n); (x && w(n)), 0; x, (a && b);\n"
                  "  (a && b), x; a && (n ? w(n) : w(n)); a && (n ? w(n) : e(n)); a || (x, (b && c(n))); (a && b) || (n ? a : b);\n"
                  "  a && ({ e(n); }); n ? ({ 1; }) : ({ 2; }); ({ e(n); 3; }), x;\n"
                  "  a && g(); a && h(); a && sin(d); a && rint(d); a && fp(1);\n"
                  "  *p; a ? *p : *p; *p, a; a, *p; a && *p;\n"
                  "}\n",
        .warnings = 19,
    },
    {
        // A statement expression's value is its final expression's, and one that ends without an expression is quiet; a
        // void dereference is no cast to void.
        .name = "unused values of statement expressions",
        .source = "void *p;\n"
                  "int e(int);\n"
                  "void f(int n, int a) {\n"
                  "  n ? ({ int z = 1; (void)z; }) : ({ int y = 2; (void)y; });\n"
                  "  n ? ({ e(n); }) : ({ 3; });\n"
                  "  n ? ({ (void)0; }) : ({ (void)1; });\n"
                  "  ({ int q = 1; (void)q; }), a;\n"
                  "  a, ({ int q = 1; (void)q; });\n"
                  "  n ? ({ *p; }) : ({ *p; });\n"
                  "}\n",
        .options = { "-Wno-void-ptr-dereference" },
        .warnings = 3,
    },
    {
        // Discarded values among runtime shift-count warnings (#1394): each keeps clang's order, the counts' before the
        // unused-value markers they precede, in commas, conditionals, logical operators and selections.
        .name = "ignored results among shift-count warnings",
        .source = "__attribute__((warn_unused_result)) int w(int);\n"
                  "void f(int x, int n, int a) {\n"
                  "  x << 40, w(1); n ? x << 40 : x >> -1; (x << 33) && a; a || (x >> 40); (w(x << 40), 0);\n"
                  "  _Generic(n, int: x, default: x << 40); n ? w(x << 33) : w(x >> 33);\n"
                  "}\n",
        .warnings = 17,
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
    {
        // An initialized pointer declarator's return-type typedef (review on #1732): the declarator hangs from its `=`,
        // at file scope and in a block, static, a later declarator's, a pointer typedef's object, `_Nonnull`.
        .name = "ignored results through initialized pointer declarators",
        .source = "typedef __attribute__((warn_unused_result)) int wur_int;\n"
                  "int plain_fn(void) { return 0; }\n"
                  "wur_int (*gp)(void) = plain_fn;\n"
                  "static wur_int (*sp)(void) = &plain_fn, (*sq)(void) = 0;\n"
                  "typedef wur_int (*FP)(void);\n"
                  "FP tp = plain_fn;\n"
                  "void test(int n)\n"
                  "{\n"
                  "    wur_int (*fp)(void) = plain_fn;\n"
                  "    fp();\n"
                  "    gp();\n"
                  "    sp();\n"
                  "    sq();\n"
                  "    tp();\n"
                  "    wur_int (*_Nonnull np)(void) = (plain_fn);\n"
                  "    np();\n"
                  "    wur_int (*cp)(void) = n ? plain_fn : plain_fn;\n"
                  "    cp();\n"
                  "    wur_int x = 1, (*dp)(void) = plain_fn;\n"
                  "    dp();\n"
                  "    (void)x;\n"
                  "}\n",
        .warnings = 8,
    },
};

static void check_test_driver_unused_results(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_unused_result_fixtures,
                                       BUSTER_ARRAY_LENGTH(check_driver_unused_result_fixtures));
    check_test_driver_warning_catalog(test, clang, executable, directory, "checker/initializer-comma");
}
