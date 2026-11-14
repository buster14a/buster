// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// GNU's function multiversioning by target strings (#1838, c_check_target_versions.c), by the driver against clang 23,
// on original fixtures:
//
// - Diagnostics: each unit's whole output (diagnostics, their places, their order, their notes) must be clang's by both
//   paths, in GNU C17: target_clones' strings (unsupported features and CPUs, empty versions, a trailing comma, the
//   duplicates, mixed specifiers, the missing default, trimmed versions, concatenated literals); multiversioning's rules
//   (versions apart, redeclarations of one, a call no default reaches, a use before the function became
//   multiversioned, duplicates, missing attributes, mixed kinds, unsupported versions, prototypes, return types, inline
//   specifications, linkage, main); then the warnings' options. Each unit also runs with its bodies deferred.
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
        "__attribute__((target_clones(\"default\"), target_clones(\"avx2\", \"default\"))) int f(void) { return 1; }\n",        // Multiversioning by target strings: versions of several strings, a default, its forward declaration, redeclarations of a version, calls after the default.
        "__attribute__((target(\"avx2\"))) int f(void) { return 1; }\n"
        "__attribute__((target(\"default\"))) int f(void) { return 2; }\n"
        "__attribute__((target(\"arch=znver4\"))) int f(void) { return 4; }\n"
        "__attribute__((target(\"sse4.2,popcnt\"))) int f(void) { return 42; }\n"
        "int main(void) { return f(); }\n",
        "__attribute__((target(\"default\"))) int f(void);\n"
        "int g(void) { return f(); }\n"
        "__attribute__((target(\"avx2\"))) int f(void) { return 1; }\n"
        "__attribute__((target(\"default\"))) int f(void) { return 2; }\n",
        "__attribute__((target(\"avx2\"))) int f(void);\n"
        "__attribute__((target(\"avx2\"))) int f(void);\n"
        "__attribute__((target(\"avx2\"))) int f(void) { return 1; }\n"
        "__attribute__((target(\"default\"))) int f(void);\n"
        "__attribute__((target(\"default\"))) int f(void) { return 0; }\n"
        "__attribute__((target(\"avx2\"))) int f(void) { return 2; }\n",
        "int f(void);\n"
        "int x = sizeof(f());\n"
        "__attribute__((target(\"default\"))) int f(void) { return 1; }\n"
        "__attribute__((target(\"avx2\"))) int f(void) { return 2; }\n"
        "int main(void) { return f(); }\n",
        "__attribute__((target(\"avx2\"))) int f(void) { return f(); }\n"
        "__attribute__((target(\"default\"))) int f(void) { return 1; }\n",
        "static __attribute__((target(\"default\"))) int f(void) { return 0; }\n"
        "static __attribute__((target(\"avx2\"))) int f(void) { return 1; }\n"
        "int g(void) { return f(); }\n",
        "__attribute__((target(\"default\"))) int f(void) { return 0; }\n"
        "__attribute__((target(\"avx2\"))) int f(void);\n"
        "int g(void) { int f(void); return f(); }\n",
        "__attribute__((target(\"default\"))) int f(void) { return 1; }\n"
        "int main(void) { return f(); }\n",
        "__attribute__((target(\"avx2\"))) int main(void) { return 1; }\n",
        "__attribute__((target(\"tune=znver4\"))) int f(void) { return 1; }\n"
        "__attribute__((target(\"default\"))) int f(void) { return 0; }\n"
        "__attribute__((target(\"tune=znver5\"))) int f(void) { return 2; }\n",
        "__attribute__((target(\"avx2\"), used)) int f(void) { return 1; }\n"
        "__attribute__((target(\"default\"), noinline)) int f(void) { return 0; }\n",
        "void h(void) { __attribute__((target(\"default\"))) int f(void); __attribute__((target(\"avx2\"))) int f(void); }\n",
        // A visible target overload set diagnoses the default candidate (#2424), including parenthesized and
        // address-of callees. A compatible function pointer and call remain valid.
        "__attribute__((target(\"default\"))) int f(int a) { return a; }\n"
        "__attribute__((target(\"avx2\"))) int f(int b) { return b; }\n"
        "void calls(void) { f(); f(1, 2); (f)(); (&f)(); }\n"
        "int (*bad)(void) = f;\nint (*good)(int) = &f;\nint valid(void) { return f(1); }\n",
        // Unnamed and multiple parameters use the count wording; a zero-parameter candidate has no argument name.
        "__attribute__((target(\"default\"))) int f(int, long);\n"
        "__attribute__((target(\"avx2\"))) int f(int a, long b) { return a + b; }\n"
        "__attribute__((target(\"default\"))) int z(void) { return 0; }\n"
        "__attribute__((target(\"avx2\"))) int z(void) { return 1; }\n"
        "void calls(void) { f(); f(1); f(1, 2, 3); z(1); }\n",
        // Nonviable conversions use overload notes; C's viable bad-pointer conversions and qualifier warnings still
        // name the default's parameter, rather than the latest version's parameter.
        "struct S { int x; };\n"
        "__attribute__((target(\"default\"))) int f(int *a) { return *a; }\n"
        "__attribute__((target(\"avx2\"))) int f(int *b) { return *b; }\n"
        "int calls(struct S s, double *p, const int *q) { f(1); f(1.0); f(p); f(q); (&f)(p); (&f)(q); f(s); return f(0); }\n",
        // Incompatible function pointers are not viable for target candidates; ordinary overloadable calls retain
        // their configurable conversion. Matching function designators, pointers and compatible old-style types work.
        "int plain(int);\nint matching(double);\n"
        "__attribute__((target(\"default\"))) int f(int a) { return a; }\n"
        "__attribute__((target(\"avx2\"))) int f(int b) { return b; }\n"
        "__attribute__((target(\"default\"))) int take(int (*a)(double)) { return a(1); }\n"
        "__attribute__((target(\"avx2\"))) int take(int (*b)(double)) { return b(2); }\n"
        "__attribute__((overloadable)) int ordinary(int (*p)(double));\n"
        "void calls(int (*p)(int), int (**pp)(int)) { take(plain); take(f); take(&f); take(p); take(*pp); take((*pp)); take((int (*)(int))p); ordinary(plain); ordinary(f); ordinary(&f); }\n"
        "int valid(int (*p)(double), int (*old)()) { return take(matching) + take(p) + take(old); }\n",
        // An ordinary overload set with distinct functions also resolves pointer signatures strictly. Target and
        // plain function arguments, pointer lvalues and compatible controls retain their respective diagnostics.
        "int plain(int);\n"
        "__attribute__((target(\"default\"))) int f(int a) { return a; }\n"
        "__attribute__((target(\"avx2\"))) int f(int b) { return b; }\n"
        "__attribute__((overloadable)) int h(int (*cb)(double));\n__attribute__((overloadable)) int h(void);\n"
        "__attribute__((overloadable)) int accept(int (*cb)(int));\n__attribute__((overloadable)) int accept(void);\n"
        "void rejected(int (*p)(int)) { h(f); h(&f); h(plain); h(p); }\n"
        "int valid(int (*p)(int)) { return accept(f) + accept(&f) + accept(plain) + accept(p); }\n",
        // A plain callee's unresolved ordinary overloaded argument remains an argument conversion diagnostic.
        "__attribute__((overloadable)) int f(int);\n__attribute__((overloadable)) int f(double);\n"
        "void take(int (*cb)(char));\nvoid rejected(void) { take(f); take(&f); }\n",
        // Address mismatches keep the overload-set source type, with the default's arity, parameter or return-type
        // note for initialization; assignment omits that note, and passing keeps its normal parameter note.
        "__attribute__((target(\"default\"))) int f(int a) { return a; }\n"
        "__attribute__((target(\"avx2\"))) int f(int b) { return b; }\n"
        "int (*arity)(void) = f;\nint (*parameter)(double) = &f;\ndouble (*result)(int) = f;\n"
        "typedef int (*FP)(double);\nvoid take(FP p);\n"
        "void assign(void) { arity = f; parameter = &f; take(f); take(&f); (&take)(f); }\nFP returned(void) { return f; }\n",
        // The candidate is retained at each use: a later version cannot change an earlier ordinary call, and a
        // default redeclaration changes only later notes.
        "__attribute__((target(\"default\"))) int f(int early);\nvoid before(void) { f(); }\n"
        "__attribute__((target(\"avx2\"))) int f(int other) { return other; }\nvoid middle(void) { f(); }\n"
        "__attribute__((target(\"default\"))) int f(int late) { return late; }\nvoid after(void) { f(); }\n"
        "int (*p)(void) = f;\n",
        // A block declaration or an object hides the outer set. A lone default and target_clones are ordinary calls.
        "__attribute__((target(\"default\"))) int f(int a) { return a; }\n"
        "__attribute__((target(\"avx2\"))) int f(int b) { return b; }\n"
        "void local(void) { __attribute__((target(\"default\"))) int f(int c); f(); }\n"
        "void pointer(void) { int (*f)(int) = 0; f(); }\nvoid outer(void) { f(); }\n"
        "__attribute__((target(\"default\"))) int d(int x) { return x; }\n"
        "__attribute__((target_clones(\"avx2\", \"default\"))) int c(int x) { return x; }\n"
        "void plain(void) { d(); c(); }\n",
        // Nested calls are visited in source order but their graph nodes put the inner call first. Their sparse
        // overload receipts coexist with ordinary overloadable calls and preserve valid dispatch and pointer uses.
        "__attribute__((target(\"default\"))) int f(int a) { return a; }\n"
        "__attribute__((target(\"avx2\"))) int f(int a) { return a + 1; }\n"
        "__attribute__((target(\"default\"))) int g(int a) { return a; }\n"
        "__attribute__((target(\"avx2\"))) int g(int a) { return a + 2; }\n"
        "__attribute__((overloadable)) int h(int a) { return a; }\n"
        "__attribute__((overloadable)) int h(double a) { return (int)a; }\n"
        "int (*p)(int) = f;\nint valid(void) { return f(g(1)) + h(f(2)) + f(h(3)) + (&f)(4) + p(5); }\n",
        // target_clones: redeclarations with and without the attribute, the same list spelled otherwise, after a plain declaration.
        "__attribute__((target_clones(\"avx2\",\"default\"))) int f(void);\n"
        "int f(void);\n"
        "int f(void) { return 1; }\n"
        "int g(void) { return f(); }\n",
        "__attribute__((target_clones(\"avx2\",\"default\"))) int f(void);\n"
        "__attribute__((target_clones(\"avx2\",\"default\"))) int f(void) { return 1; }\n",
        "__attribute__((target_clones(\"avx2\",\"default\"))) int f(void) { return 1; }\n"
        "__attribute__((target_clones(\"avx2, default\"))) int f(void);\n",
        "__attribute__((target_clones(\"avx2\",\"default\"))) int f(void) { return 1; }\n"
        "__attribute__((target_clones(\"avx2\",\"default\"))) int f(void) { return 2; }\n",
        "__attribute__((target_clones(\"avx2\",\"default\"))) int f(void);\n"
        "__attribute__((target_clones(\"sse4.2\",\"default\"))) int f(void) { return 1; }\n",
        "int f(void);\n"
        "__attribute__((target_clones(\"avx2\",\"default\"))) int f(void) { return 1; }\n"
        "int main(void) { return f(); }\n",
        "__attribute__((target_clones(\"avx2\",\"default\"))) int main(void) { return 1; }\n",
        "__attribute__((target_clones(\"default\"))) int f();\n",
        // Their errors: a call no default reaches, a use before the function became multiversioned, duplicates, missing attributes, mixed kinds, unsupported versions, prototypes, return types, inline specifications, linkage, main.
        "__attribute__((target(\"avx2\"))) int f(void) { return 1; }\n"
        "__attribute__((target(\"sse4.2\"))) int f(void) { return 2; }\n"
        "int main(void) { return f() + (f)(); }\n",
        "__attribute__((target(\"avx2\"))) int f(void) { return 1; }\n"
        "__attribute__((target(\"sse4.2\"))) int f(void) { return 2; }\n"
        "int g(void) { return f(); }\n"
        "__attribute__((target(\"default\"))) int f(void) { return 0; }\n"
        "int h(void) { return f(); }\n",
        "int f(void);\n"
        "int g(void) { return f(); }\n"
        "__attribute__((target(\"avx2\"))) int f(void) { return 1; }\n"
        "__attribute__((target(\"default\"))) int f(void) { return 2; }\n",
        "__attribute__((target(\"avx2\"))) int f(void) { return 1; }\n"
        "int g(void) { return f(); }\n"
        "__attribute__((target(\"default\"))) int f(void) { return 2; }\n",
        "int f(void);\n"
        "int g(void) { return f(); }\n"
        "__attribute__((target_clones(\"sse4.2\",\"default\"))) int f(void) { return 1; }\n",
        "__attribute__((target(\"avx2\"))) int f(void) { return 1; }\n"
        "__attribute__((target(\"avx2\"))) int f(void) { return 2; }\n"
        "__attribute__((target(\"default\"))) int f(void) { return 2; }\n",
        "__attribute__((target(\"avx2,sse4.2\"))) int f(void) { return 1; }\n"
        "__attribute__((target(\"sse4.2,avx2\"))) int f(void) { return 2; }\n"
        "__attribute__((target(\"default\"))) int f(void) { return 2; }\n",
        "__attribute__((target(\"avx2,sse4.2\"))) int f(void);\n"
        "__attribute__((target(\"default\"))) int f(void);\n"
        "__attribute__((target(\" sse4.2 , avx2 \"))) int f(void);\n",
        "__attribute__((target(\"arch=znver4\"))) int f(void);\n"
        "__attribute__((target(\"default\"))) int f(void);\n"
        "__attribute__((target(\"arch=znver4,tune=znver5\"))) int f(void);\n"
        "__attribute__((target(\"arch=znver3\"))) int f(void);\n",
        "__attribute__((target(\"default\"))) int f(void) { return 0; }\n"
        "__attribute__((target(\"avx2\"))) int f(void) { return 1; }\n"
        "__attribute__((target(\"avx2\"))) int f(void) { return 1; }\n",
        "__attribute__((target(\"no-avx2\"))) int f(void) { return 1; }\n"
        "__attribute__((target(\"default\"))) int f(void) { return 2; }\n",
        "__attribute__((target(\"crc32\"))) int f(void) { return 1; }\n"
        "__attribute__((target(\"default\"))) int f(void) { return 2; }\n",
        "__attribute__((target(\"arch=x86-64-v3\"))) int f(void) { return 1; }\n"
        "__attribute__((target(\"default\"))) int f(void) { return 2; }\n",
        "__attribute__((target(\"default\"))) int f(void);\n"
        "__attribute__((target(\"adx\"))) int f(void);\n"
        "__attribute__((target(\"no-sse\"))) int f(void);\n"
        "__attribute__((target(\"arch=x86-64-v2\"))) int f(void);\n"
        "__attribute__((target(\"arch=amdfam19h\"))) int f(void);\n"
        "__attribute__((target(\"avx2,crc32\"))) int f(void);\n",
        "__attribute__((target(\"avx2\"))) int f(void) { return 1; }\n"
        "__attribute__((target(\"default\"))) int f(void) { return 2; }\n"
        "static int f(void);\n",
        "__attribute__((target(\"avx2\"))) int f(void) { return 1; }\n"
        "__attribute__((target(\"default\"))) int f(void) { return 2; }\n"
        "int f(void);\n",
        "__attribute__((target(\"avx2\"))) int f(void);\n"
        "int f(void);\n"
        "__attribute__((target(\"default\"))) int f(void);\n",
        "__attribute__((target(\"avx2\"))) int f(void);\n"
        "int f(void);\n"
        "__attribute__((target(\"sse4.2\"))) int f(void);\n",
        "__attribute__((target(\"avx2\"))) int f(void);\n"
        "__attribute__((target(\"default\"))) int f(void);\n"
        "int g(void) { extern int f(void); return f(); }\n",
        "__attribute__((target(\"avx2\"))) static int f(void) { return 1; }\n"
        "__attribute__((target(\"default\"))) int f(void) { return 2; }\n",
        "__attribute__((target(\"avx2\"))) int f(void) { return 1; }\n"
        "__attribute__((target(\"default\"))) long f(void) { return 2; }\n",
        "__attribute__((target(\"avx2\"))) int f(void) { return 1; }\n"
        "__attribute__((target(\"default\"))) inline int f(void) { return 2; }\n",
        "__attribute__((target(\"default\"))) int f(int);\n"
        "__attribute__((target(\"avx2\"))) int f(long);\n",
        "__attribute__((target(\"avx2\"))) int f() { return 1; }\n"
        "__attribute__((target(\"default\"))) int f() { return 2; }\n",
        "__attribute__((target(\"default\"))) int f(void);\n"
        "__attribute__((target(\"avx2\"))) int f();\n",
        "__attribute__((target(\"default\"))) int f();\n",
        "__attribute__((target_clones(\"avx2\",\"default\"))) int f(void) { return 1; }\n"
        "__attribute__((target(\"avx2\"))) int f(void) { return 1; }\n",
        "__attribute__((target_clones(\"avx2\",\"default\"))) int f(void);\n"
        "__attribute__((target(\"default\"))) int f(void);\n",
        "__attribute__((target(\"default\"))) int f(void);\n"
        "__attribute__((target_clones(\"avx2\",\"default\"))) int f(void);\n",
        "__attribute__((target(\"default\"))) int main(void) { return 1; }\n",
        "__attribute__((target(\"default\"))) int main(void);\n"
        "int main(void) { return 0; }\n",
        "__attribute__((target(\"avx2\"))) inline int f(void) { return 1; }\n"
        "__attribute__((target(\"default\"))) inline int f(void) { return 0; }\n",
        // apxf (#1919): a plain target, versions of it (on and off), and a version of it with others.
        "__attribute__((target(\"apxf\"))) int f(void) { return 1; }\n"
        "__attribute__((target(\"no-apxf,avx2\"))) int g(void) { return 1; }\n",
        "__attribute__((target(\"default\"))) int f(void) { return 0; }\n"
        "__attribute__((target(\"apxf\"))) int f(void) { return 1; }\n"
        "__attribute__((target(\"apxf,avx2\"))) int f(void) { return 2; }\n"
        "int main(void) { return f(); }\n",
        "__attribute__((target(\"default\"))) int f(void);\n"
        "__attribute__((target(\"no-apxf\"))) int f(void);\n",
        // Attributes after a definition (#1920): a redefinition's, warned before its error, of target's, a plain one's and
        // an object's; a redeclaration's of target, target_clones, cpu_specific and cpu_dispatch; a version's redefinition
        // and redeclaration.
        "int f(void) { return 1; }\n"
        "__attribute__((target(\"avx2\"))) int f(void) { return 0; }\n"
        "int g(void) { return 1; }\n"
        "__attribute__((noinline)) int g(void) { return 0; }\n"
        "int h(void) { return 1; }\n"
        "__attribute__((aligned(16), cold)) int h(void) { return 0; }\n"
        "__attribute__((noinline)) int w(void) { return 1; }\n"
        "__attribute__((noinline)) int w(void) { return 2; }\n"
        "int x = 1;\n"
        "__attribute__((aligned(16))) int x = 2;\n"
        "int y = 1;\n"
        "__attribute__((weak)) int y = 2;\n"
        "int v(void) { return 1; }\n"
        "__attribute__((target(\"default\"))) int v(void) { return 2; }\n"
        "int t(void) { return 1; }\n"
        "long t(void) { return 2; }\n",
        "int u(void) { return 1; }\n"
        "__attribute__((target_clones(\"avx2\",\"default\"))) int u(void) { return 2; }\n",
        "int a(void) { return 1; }\n"
        "__attribute__((cpu_specific(generic))) int a(void);\n"
        "int b(void) { return 1; }\n"
        "__attribute__((target_clones(\"avx2\",\"default\"))) int b(void);\n"
        "int c(void) { return 1; }\n"
        "__attribute__((target(\"default\"))) int c(void);\n"
        "__attribute__((target(\"avx2\"))) int d(void) { return 1; }\n"
        "__attribute__((target(\"avx2\"))) int d(void);\n"
        "__attribute__((target(\"sse4.2\"))) int e(void) { return 1; }\n"
        "__attribute__((target(\"avx2\"))) int e(void);\n"
        "int g(void) { return 1; }\n"
        "__attribute__((cpu_dispatch(generic))) int g(void);\n",
        "__attribute__((target(\"default\"))) int f(void) { return 0; }\n"
        "__attribute__((target(\"avx2\"))) int f(void) { return 1; }\n"
        "__attribute__((target(\"avx2\"), used)) int f(void);\n"
        "__attribute__((target(\"avx2\"), used)) int f(void) { return 2; }\n",
        // A redeclaration of an existing version isn't checked against multiversioning's attributes, but a new version
        // is, and so is the declaration before one that makes the function multiversioned, with what earlier ones gave it
        // (#2135).
        "__attribute__((target(\"default\"))) int f(void);\n"
        "__attribute__((target(\"default\"), cold)) int f(void);\n"
        "__attribute__((target(\"avx2\"))) int f(void);\n"
        "__attribute__((target(\"avx2\"), noinline)) int f(void);\n"
        "__attribute__((target(\"sse4.2\"), cold)) int f(void);\n"
        "__attribute__((target_clones(\"avx2\",\"default\"))) int g(void);\n"
        "__attribute__((target_clones(\"avx2\",\"default\"), cold)) int g(void);\n"
        "__attribute__((target(\"avx2\"))) int h(void);\n"
        "__attribute__((target(\"avx2\"), cold)) int h(void);\n"
        "__attribute__((target(\"default\"))) int h(void);\n",
        "__attribute__((target(\"default\"))) int f(void) { return 0; }\n"
        "__attribute__((target(\"avx2\"))) int f(void) { return 1; }\n"
        "__attribute__((target(\"avx2\"), cold)) int f(void);\n"
        "__attribute__((target(\"avx2\"), noinline)) int f(void) { return 2; }\n",
        // A declaration refused as the one that caused multiversioning doesn't make the function multiversioned: the
        // next version causes it again, with the error repeated.
        "__attribute__((target(\"avx2\"), cold)) int f(void);\n"
        "__attribute__((target(\"default\"))) int f(void);\n"
        "__attribute__((target(\"sse4.2\"))) int f(void);\n"
        "__attribute__((cold, target(\"avx2\"))) int g(void);\n"
        "__attribute__((target(\"sse4.2\"))) int g(void);\n"
        "__attribute__((target(\"bmi\"))) int g(void);\n"
        "__attribute__((target(\"avx2\"))) int h(void);\n"
        "__attribute__((target(\"avx2\"), cold)) int h(void);\n"
        "__attribute__((target(\"sse4.2\"))) int h(void);\n"
        "__attribute__((target(\"bmi\"))) int h(void);\n",
        "__attribute__((target_clones(\"sse4.2\",\"default\"))) int k(void) { return 0; }\n"
        "__attribute__((target_clones(\"sse4.2\",\"default\"), used)) int k(void);\n"
        "__attribute__((target_clones(\"sse4.2\",\"default\"), used)) int k(void) { return 1; }\n",
        // A version refused for its attribute isn't merged: no warning (#1920).
        "__attribute__((target(\"default\"))) int f(void) { return 0; }\n"
        "__attribute__((target(\"avx2\"), cold)) int f(void) { return 1; }\n",
        // After a version's redefinition, the rejected unit's next reading leaves out an earlier redefinition whose
        // attribute names a word another declaration's attribute names too (`noinline`): no use of a name it declares, so
        // the errors after both are still clang's (#2137).
        "__attribute__((noinline)) int w(void) { return 1; }\n"
        "__attribute__((noinline)) int w(void) { return 2; }\n"
        "int u(void) { return 1; }\n"
        "__attribute__((target_clones(\"avx2\",\"default\"))) int u(void) { return 2; }\n"
        "int t(void) { return 1; }\n"
        "long __attribute__((cold)) t(void) { return 2; }\n",
        "[[gnu::noinline]] int w(void) { return 1; }\n"
        "[[gnu::noinline]] int w(void) { return 2; }\n"
        "int u(void) { return 1; }\n"
        "__attribute__((target_clones(\"avx2\",\"default\"))) int u(void) { return 2; }\n"
        "int t(void) { return 1; }\n"
        "long t(void) { return 2; }\n",
        // A version's redefinition is recovered and invalid (#2362): a later definition of its version is quiet, another
        // version's redefinition is still clang's error, and so are the errors after them; the call goes on to the
        // function's other versions.
        "__attribute__((target(\"default\"))) int f(void) { return 0; }\n"
        "__attribute__((target(\"avx2\"))) int f(void) { return 1; }\n"
        "__attribute__((target(\"avx2\"))) int f(void) { return 2; }\n"
        "__attribute__((target(\"avx2\"))) int f(void) { return 3; }\n"
        "__attribute__((target(\"sse4.2\"))) int f(void) { return 4; }\n"
        "__attribute__((target(\"avx2\"))) int f(void) { return 5; }\n"
        "__attribute__((target(\"sse4.2\"))) int f(void) { return 6; }\n"
        "int main(void) { return f() + undeclared; }\n",
        "__attribute__((target_clones(\"avx2\",\"default\"))) int f(int a) { return 0; }\n"
        "__attribute__((target_clones(\"avx2\",\"default\"))) int f(int a) { return 1; }\n"
        "__attribute__((target_clones(\"avx2\",\"default\"))) int f(int a) { return 2; }\n"
        "int main(void) { return f(1) + undeclared; }\n",
        // An ordinary redefinition's earlier, then a version's: the version's later attributes are its own, no late
        // warning (#2362).
        "__attribute__((noinline)) int w(void) { return 1; }\n"
        "__attribute__((noinline)) int w(void) { return 2; }\n"
        "__attribute__((target(\"default\"))) int u(void) { return 1; }\n"
        "__attribute__((target(\"avx2\"))) int u(void) { return 2; }\n"
        "__attribute__((target(\"avx2\"))) int u(void) { return 3; }\n"
        "__attribute__((target(\"avx2\"), used)) int u(void);\n",
    };
    // Units whose warnings each option set below changes.
    static const char* const flagged[] = {
        "__attribute__((target_clones(\"avx2\", \"avx2\", \"default\"))) int f(void) { return 1; }\n",
        "__attribute__((target_clones(\"avx2,bmi\", \"default\"))) int f(void) { return 1; }\n",
        "__attribute__((target_clones(\"adx\", \"default\"))) int f(void) { return 1; }\n",
        // A target overload's nonviable integer conversion stays a hard no-match even when -Wint-conversion is off.
        "__attribute__((target(\"default\"))) int f(int *a) { return *a; }\n"
        "__attribute__((target(\"avx2\"))) int f(int *b) { return *b; }\nvoid calls(void) { f(1); f(1.0); }\n",
        "__attribute__((target(\"default\"))) int f(int *a) { return *a; }\n"
        "__attribute__((target(\"avx2\"))) int f(int *b) { return *b; }\nvoid calls(double *p, const int *q) { f(p); f(q); (&f)(p); (&f)(q); }\n",
        "__attribute__((target(\"default\"))) int f(int a) { return a; }\n"
        "__attribute__((target(\"avx2\"))) int f(int b) { return b; }\nint (*p)(void) = f;\n"
        "typedef int (*FP)(double);\nvoid take(FP p);\nvoid assign(void) { p = &f; take(f); (&take)(f); }\nFP returned(void) { return f; }\n"
        "void sink(int (*ok)(int), int bad);\nvoid mixed(void) { sink(f, \"x\"); }\n"
        "void take_pair(int *a, FP b);\nvoid ordered(const int *q) { take_pair(q, f); }\n",
        // Suppressing incompatible-function-pointer diagnostics never makes a target candidate viable, including
        // an argument that is itself a target set. The ordinary overloadable control follows the warning option.
        "int plain(int);\n"
        "__attribute__((target(\"default\"))) int f(int a) { return a; }\n"
        "__attribute__((target(\"avx2\"))) int f(int b) { return b; }\n"
        "__attribute__((target(\"default\"))) int take(int (*a)(double)) { return a(1); }\n"
        "__attribute__((target(\"avx2\"))) int take(int (*b)(double)) { return b(2); }\n"
        "__attribute__((overloadable)) int ordinary(int (*p)(double));\n"
        "void calls(void) { take(plain); take(f); take(&f); ordinary(plain); ordinary(f); ordinary(&f); }\n",
        "int plain(int);\n"
        "__attribute__((target(\"default\"))) int f(int a) { return a; }\n"
        "__attribute__((target(\"avx2\"))) int f(int b) { return b; }\n"
        "__attribute__((overloadable)) int h(int (*cb)(double));\n__attribute__((overloadable)) int h(void);\n"
        "void rejected(int (*p)(int)) { h(f); h(&f); h(plain); h(p); }\n",
    };
    static const char* const options[] = {
        "-Werror",
        "-w",
        "-Wno-function-multiversion",
        "-Werror=function-multiversion",
        "-Wno-target-clones-mixed-specifiers",
        "-Werror=target-clones-mixed-specifiers",
        "-Wno-ignored-attributes",
        "-Wno-int-conversion",
        "-Wno-error=int-conversion",
        "-Wno-incompatible-pointer-types",
        "-Wno-error=incompatible-pointer-types",
        "-Wno-incompatible-function-pointer-types",
        "-Wno-error=incompatible-function-pointer-types",
    };
    enum { LINES = 65536, LONG = 8192 };
    // Long lists, built here: past 64 versions (a version first seen there and repeated), past an offset of 1024 bytes in
    // one string, and past 64 strings; then a declaration of 70 versions and its redeclaration, with the same versions and
    // with its last one different. Then target strings past 64 items: a feature multiversioning doesn't support after 70 of
    // them, and one the target attribute doesn't (its warning's piece past 256 bytes), two strings of
    // 71 features the same once sorted, and 91 versions (each a pair of features) with the first one's redeclaration spelled
    // in the other order.
    enum { CLONES_UNITS = 6, LONG_UNITS = 10 };
    char* long_units[LONG_UNITS] = { malloc(LONG), malloc(LONG), malloc(LONG), malloc(LONG), malloc(LONG),
                                     malloc(LONG), malloc(LONG), malloc(LONG), malloc(LONG), malloc(LONG) };
    const uint32_t long_versions[CLONES_UNITS] = { 70, 140, 70, 140, 70, 70 };
    static const char* const tails[CLONES_UNITS][2] = {
        { "avx2,avx2\"", NULL },
        { "avx2,  avx2\"", NULL },
        { "\"sse4.2\", \"sse4.2\"", NULL },
        { "foo\"", NULL },
        { "avx2\"", "avx2\"" },
        { "avx2\"", "sse4.2\"" },
    };
    static const char* const ends[2] = { "))) int f(void) { return 1; }\n", "))) int f(void);\n" };
    bool built = true;
    for (uint32_t l = 0; l < CLONES_UNITS; l += 1)
    {
        int length = long_units[l] != NULL ? 0 : -1;
        const uint32_t copies = tails[l][1] != NULL ? 2 : 1;
        for (uint32_t copy = 0; length >= 0 && length < LONG && copy < copies; copy += 1)
        {
            length += snprintf(long_units[l] + length, LONG - (size_t)length, "__attribute__((target_clones(%s", l == 2 ? "" : "\"");
            for (uint32_t v = 0; length > 0 && length < LONG && v < long_versions[l]; v += 1)
            {
                length += snprintf(long_units[l] + length, LONG - (size_t)length, l == 2 ? "\"default\", " : "default,");
            }
            if (length > 0 && length < LONG)
            {
                length += snprintf(long_units[l] + length, LONG - (size_t)length, "%s%s", tails[l][copy], ends[copies - 1]);
            }
        }
        built = built && length > 0 && length < LONG;
    }
    static const char* const pair_features[] = { "sse", "sse2", "sse3", "ssse3", "sse4.1", "sse4.2", "avx", "avx2", "fma", "bmi", "bmi2", "popcnt", "aes", "pclmul" };
    for (uint32_t l = CLONES_UNITS; l < LONG_UNITS; l += 1)
    {
        int length = long_units[l] != NULL ? snprintf(long_units[l], LONG, "__attribute__((target(\"default\"))) int f(void);\n") : -1;
        // The first two units' one string and the third's two: their prefixes, 70 avx2s, and their suffixes.
        static const char* const around[3][2][2] = {
            { { "", ",adx" }, { NULL, NULL } },
            { { "", ",foo" }, { NULL, NULL } },
            { { "", ",sse4.2" }, { "sse4.2,", "" } },
        };
        for (uint32_t copy = 0; l < LONG_UNITS - 1 && length > 0 && length < LONG && copy < 2 && around[l - CLONES_UNITS][copy][0] != NULL; copy += 1)
        {
            length += snprintf(long_units[l] + length, LONG - (size_t)length, "__attribute__((target(\"%s", around[l - CLONES_UNITS][copy][0]);
            for (uint32_t v = 0; length > 0 && length < LONG && v < 70; v += 1)
            {
                length += snprintf(long_units[l] + length, LONG - (size_t)length, v == 0 ? "avx2" : ",avx2");
            }
            if (length > 0 && length < LONG)
            {
                length += snprintf(long_units[l] + length, LONG - (size_t)length, "%s\"))) int f(void);\n", around[l - CLONES_UNITS][copy][1]);
            }
        }
        for (uint32_t a = 0; l == LONG_UNITS - 1 && length > 0 && length < LONG && a < BUSTER_ARRAY_LENGTH(pair_features); a += 1)
        {
            for (uint32_t b = a + 1; length > 0 && length < LONG && b < BUSTER_ARRAY_LENGTH(pair_features); b += 1)
            {
                length += snprintf(long_units[l] + length, LONG - (size_t)length, "__attribute__((target(\"%s,%s\"))) int f(void);\n", pair_features[a], pair_features[b]);
            }
        }
        if (l == LONG_UNITS - 1 && length > 0 && length < LONG)
        {
            length += snprintf(long_units[l] + length, LONG - (size_t)length, "__attribute__((target(\"%s,%s\"))) int f(void);\n", pair_features[1], pair_features[0]);
        }
        built = built && length > 0 && length < LONG;
    }
    char* expected_lines = malloc(LINES);
    char* actual_lines = malloc(LINES);
    char source[PATH_MAX];
    snprintf(source, sizeof(source), "%s/target-versions.c", directory);
    const uint32_t plain = BUSTER_ARRAY_LENGTH(units) + LONG_UNITS;
    const uint32_t total = plain + BUSTER_ARRAY_LENGTH(flagged) * BUSTER_ARRAY_LENGTH(options);
    for (uint32_t unit = 0; expected_lines != NULL && actual_lines != NULL && built && unit < total; unit += 1)
    {
        const uint32_t flag = unit - plain;
        const char* text = unit < BUSTER_ARRAY_LENGTH(units) ? units[unit] : unit < plain ? long_units[unit - BUSTER_ARRAY_LENGTH(units)] :
                           flagged[flag % BUSTER_ARRAY_LENGTH(flagged)];
        char* option = unit < plain ? NULL : (char*)options[flag / BUSTER_ARRAY_LENGTH(flagged)];
        const bool made = parse_test_write(source, text, strlen(text));
        char* arguments[] = { (char*)clang, "-std=gnu17", "-fsyntax-only", "-fno-caret-diagnostics", source, option, NULL, NULL };
        char* expected = NULL;
        const int oracle = made ? check_test_driver_spawn(arguments, &expected) : -1;
        const bool lines = expected != NULL && check_test_diagnostic_lines(expected, expected_lines, LINES);
        char description[4096];
        snprintf(description, sizeof(description), "target versions %u: clang's lines read", unit);
        check_test_expect(test, made && lines, description);
        // The vector path, the scalar one, and the vector one with the bodies deferred (a unit with a multiversioned
        // function is read in source order all the same).
        for (uint32_t scalar = 0; lines && scalar < 3; scalar += 1)
        {
            arguments[0] = (char*)executable;
            arguments[option != NULL ? 6 : 5] = scalar == 1 ? "--scalar" : scalar == 2 ? "--deferred-bodies" : NULL;
            char* actual = NULL;
            const int status = check_test_driver_spawn(arguments, &actual);
            const bool same = actual != NULL && check_test_diagnostic_lines(actual, actual_lines, LINES) && strcmp(expected_lines, actual_lines) == 0;
            snprintf(description, sizeof(description), "target versions %u (%s%s%s), %s: status %d (clang %d), lines\n%s(clang\n%s)", unit,
                     option != NULL ? option : "", option != NULL ? " " : "", text, scalar == 1 ? "scalar" : scalar == 2 ? "deferred" : "vector", status, oracle, actual_lines, expected_lines);
            check_test_expect(test, same && (status != 0) == (oracle != 0), description);
            free(actual);
        }
        free(expected);
    }
    free(expected_lines);
    free(actual_lines);
    for (uint32_t l = 0; l < LONG_UNITS; l += 1)
    {
        free(long_units[l]);
    }
    unlink(source);
}


// The object at `path`'s named symbols that aren't sections, files or .L labels, each "name binding type defined" (a
// line each, in symbol-table order) into `text` (room for `capacity` bytes). Whether it was read.
static bool check_test_target_versions_symbols(const char* path, char* text, size_t capacity)
{
    Arena arena = arena_reserve(UINT64_C(1) << 24);
    CheckTestElf elf = { 0 };
    bool valid = arena.base != 0 && check_test_elf_open(&elf, object_test_read_file(&arena, path));
    size_t length = 0;
    text[0] = 0;
    for (uint64_t i = 1; valid && i < elf.symbol_count; i += 1)
    {
        const char* name = check_test_elf_string(elf.symbol_names, elf.symbol_names_size, elf.symbols[i].st_name);
        const uint32_t type = ELF64_ST_TYPE(elf.symbols[i].st_info);
        if (name != NULL && name[0] != 0 && type != STT_SECTION && type != STT_FILE && strncmp(name, ".L", 2) != 0)
        {
            const int written = snprintf(text + length, capacity - length, "%s %u %u %u\n", name, (uint32_t)ELF64_ST_BIND(elf.symbols[i].st_info), type,
                                         elf.symbols[i].st_shndx != SHN_UNDEF ? 1u : 0u);
            valid = written > 0 && (size_t)written < capacity - length;
            length += valid ? (size_t)written : 0;
        }
    }
    arena_release(&arena);
    return valid;
}

// The number of distinct places (section and value) of the object at `path`'s defined functions, or -1: each version
// of a target_clones body has code of its own, as clang compiles one per version (#1838).
static int64_t check_test_target_versions_places(const char* path)
{
    Arena arena = arena_reserve(UINT64_C(1) << 24);
    CheckTestElf elf = { 0 };
    bool valid = arena.base != 0 && check_test_elf_open(&elf, object_test_read_file(&arena, path));
    int64_t places = 0;
    for (uint64_t i = 1; valid && i < elf.symbol_count; i += 1)
    {
        const Elf64_Sym* symbol = elf.symbols + i;
        bool seen = ELF64_ST_TYPE(symbol->st_info) != STT_FUNC || symbol->st_shndx == SHN_UNDEF;
        for (uint64_t j = 1; !seen && j < i; j += 1)
        {
            const Elf64_Sym* other = elf.symbols + j;
            seen = ELF64_ST_TYPE(other->st_info) == STT_FUNC && other->st_shndx == symbol->st_shndx && other->st_value == symbol->st_value;
        }
        places += seen ? 0 : 1;
    }
    arena_release(&arena);
    return valid ? places : -1;
}

// Whether every line of `expected` is a line of `actual` and the other way round (the symbols, in any order).
static bool check_test_target_versions_same_lines(const char* expected, const char* actual)
{
    bool same = true;
    for (uint32_t side = 0; same && side < 2; side += 1)
    {
        const char* from = side == 0 ? expected : actual;
        const char* in = side == 0 ? actual : expected;
        for (const char* line = from; same && *line != 0;)
        {
            const char* end = strchr(line, '\n');
            const size_t length = end != NULL ? (size_t)(end - line) + 1 : strlen(line);
            bool found = false;
            for (const char* other = in; !found && *other != 0;)
            {
                const char* other_end = strchr(other, '\n');
                const size_t other_length = other_end != NULL ? (size_t)(other_end - other) + 1 : strlen(other);
                found = other_length == length && memcmp(other, line, length) == 0;
                other += other_length;
            }
            same = found;
            line += length;
        }
    }
    return same;
}

// Objects: a unit of versions (target's, external and static with a call, and target_clones', external and static
// with a call) and a unit of their callers (declarations, calls and addresses), each by clang and both paths: the
// objects' symbols must be clang's, with clang's bindings, types and definedness; the scalar and vector objects the same
// bytes; every mix of the objects linked by cc must run as clang's; and so must the direct path's executable of both,
// and -g's.
static void check_test_target_versions_objects(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char versions[] =
        "__attribute__((target(\"avx2\"))) int f(int x) { return x + 2; }\n"
        "__attribute__((target(\"default\"))) int f(int x) { return x + 1; }\n"
        "__attribute__((target(\"arch=znver4\"))) int f(int x) { return x + 4; }\n"
        "__attribute__((target(\"popcnt,sse4.2\"))) int f(int x) { return x + 42; }\n"
        "__attribute__((target(\"apxf\"))) int f(int x) { return x + 36; }\n"
        "static __attribute__((target(\"default\"))) int s(void) { static int n = 10; return n; }\n"
        "static __attribute__((target(\"avx512f\"))) int s(void) { static int n = 20; return n; }\n"
        "int call_s(void) { return s(); }\n"
        "__attribute__((target_clones(\"avx2\", \"arch=x86-64-v3\", \"sse4.1\", \"default\"))) int c(int x) { return x * 3; }\n"
        "static __attribute__((target_clones(\"gfni\", \"default\"))) int t(void) { return 7; }\n"
        "int call_t(void) { return t(); }\n"
        // A repeated version (kept for its index, without a symbol of its own) and a default that isn't last.
        "__attribute__((target_clones(\"default\", \"avx2\", \"sse4.2\", \"avx2\"))) int d(int x) { static int m = 5; return x * m; }\n"
        // A body of vector code, compiled once per version: the avx2, sse4.2 and default ones are called directly too. Its
        // AVX-512 version is chosen by the feature, not by arch=znver4: a hosted runner's virtual Zen 4 is znver4 to
        // __builtin_cpu_is but exposes no AVX-512, and that version's copy has EVEX code.
        "typedef float w8 __attribute__((vector_size(32)));\n"
        "__attribute__((target_clones(\"avx2\", \"avx512f\", \"sse4.2\", \"default\"))) int w(int x) { w8 a = { 1, 2, 3, 4, 5, 6, 7, 8 }; "
        "a = a * (w8){ x, x, x, x, x, x, x, x }; int y = 0; for (int i = 0; i < 8; i += 1) { y += (int)a[i]; } return y; }\n"
        // A body that takes its labels' addresses (#2247): each copy's code its own labels, the static table the last
        // copy's, as clang's static data reaches the last clone it emits (the objects' places of code are clang's).
        "__attribute__((target_clones(\"avx2\", \"sse4.2\", \"default\"))) long j(int x) { static void* table[] = { &&a, &&b }; "
        "void* local[] = { &&b, &&a }; goto *(x < 0 ? table[-x & 1] : local[x & 1]); a: return 1; b: return 2; }\n"
        // A body with a switch whose jump table each copy has of its own (#2247), and one in the next function.
        "__attribute__((target_clones(\"avx2\", \"sse4.2\", \"default\"))) int k(int x) { switch (x) { case 0: return 3; case 1: return 5; "
        "case 2: return 7; case 3: return 11; case 5: return 13; default: return x; } }\n"
        "int call_k(int x) { switch (x) { case 1: return k(0); case 2: return k(1); case 3: return k(5); case 4: return k(9); default: return k(x); } }\n"
        // A body with inline assembly (#2247): each copy's statements planned for its own instruction set, an asm goto's
        // exits among them.
        "__attribute__((target_clones(\"avx2\", \"sse4.2\", \"default\"))) int m(int x) { int y; __asm__(\"imull $3, %1, %0\" : \"=r\"(y) : \"r\"(x)); "
        "__asm__ goto(\"testl %0, %0\\n\\tjs %l[negative]\" : : \"r\"(x) : \"cc\" : negative); return y; negative: return -y; }\n"
        // A version of 41 features, its name past 200 bytes.
        "__attribute__((target(\"default\"))) int g(int x) { return x - 1; }\n"
        "__attribute__((target(\"popcnt,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2\"))) int g(int x) { return x - 2; }\n";
    static const char callers[] =
        "int printf(const char*, ...);\n"
        "__attribute__((target(\"avx2\"))) int f(int x);\n"
        "__attribute__((target(\"default\"))) int f(int x);\n"
        "__attribute__((target(\"arch=znver4\"))) int f(int x);\n"
        "__attribute__((target(\"popcnt,sse4.2\"))) int f(int x);\n"
        "__attribute__((target(\"apxf\"))) int f(int x);\n"
        "__attribute__((target_clones(\"avx2\", \"arch=x86-64-v3\", \"sse4.1\", \"default\"))) int c(int x);\n"
        "__attribute__((target_clones(\"default\", \"avx2\", \"sse4.2\", \"avx2\"))) int d(int x);\n"
        "__attribute__((target(\"default\"))) int g(int x);\n"
        "__attribute__((target(\"popcnt,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2,avx2\"))) int g(int x);\n"
        "int call_s(void);\nint call_t(void);\n"
        "__attribute__((target_clones(\"avx2\", \"avx512f\", \"sse4.2\", \"default\"))) int w(int x);\n"
        // Labels of the versions' names: in one unit with the definition, clang's module has one global per name, so they are
        // the versions' own symbols (#2204, check_program_label_forwards).
        "int w0(int) __asm__(\"w.avx2.0\");\nint w2(int) __asm__(\"w.sse4.2.2\");\nint w3(int) __asm__(\"w.default.3\");\n"
        "long j1(int) __asm__(\"j.sse4.2.1\");\nlong j2(int) __asm__(\"j.default.2\");\n"
        "int k1(int) __asm__(\"k.sse4.2.1\");\nint k2(int) __asm__(\"k.default.2\");\n"
        "int m1(int) __asm__(\"m.sse4.2.1\");\nint m2(int) __asm__(\"m.default.2\");\n"
        "__attribute__((target_clones(\"avx2\", \"sse4.2\", \"default\"))) long j(int x);\n"
        "__attribute__((target_clones(\"avx2\", \"sse4.2\", \"default\"))) int k(int x);\nint call_k(int x);\n"
        "__attribute__((target_clones(\"avx2\", \"sse4.2\", \"default\"))) int m(int x);\n"
        "int (*address_f)(int) = f;\nint (*address_c)(int) = c;\nint (*address_d)(int) = d;\n"
        "int main(void) { printf(\"%d %d %d %d %d %d %d %d %d %d %d %d %d\\n\", f(5), call_s(), address_f(1), c(2), address_c(3), call_t(), d(4), address_d(6), g(9),\n"
        "    w(2), w0(3), w2(4), w3(5));\n"
        "    for (int x = -2; x < 3; x += 1) { printf(\" %ld%ld%ld\", j(x), j1(x), j2(x)); }\n"
        "    for (int x = -1; x < 7; x += 1) { printf(\" %d:%d,%d,%d,%d\", x, k(x), k1(x), k2(x), call_k(x)); }\n"
        "    for (int x = -3; x < 4; x += 1) { printf(\" %d,%d,%d\", m(x), m1(x), m2(x)); }\n    printf(\"\\n\");\n"
        "    return 0; }\n";
    char sources[2][PATH_MAX];
    char objects[2][3][PATH_MAX];
    snprintf(sources[0], sizeof(sources[0]), "%s/target-versions.c", directory);
    snprintf(sources[1], sizeof(sources[1]), "%s/target-callers.c", directory);
    const bool written = parse_test_write(sources[0], versions, sizeof(versions) - 1) && parse_test_write(sources[1], callers, sizeof(callers) - 1);
    check_test_expect(test, written, "target versions: the objects' fixtures are written");
    bool built[2][3] = { { false } };
    enum { SYMBOLS = 8192 };
    char* symbols[3] = { malloc(SYMBOLS), malloc(SYMBOLS), malloc(SYMBOLS) };
    for (uint32_t u = 0; written && u < 2; u += 1)
    {
        for (uint32_t p = 0; p < 3; p += 1)
        {
            snprintf(objects[u][p], sizeof(objects[u][p]), "%s/target-%s-%u.o", directory, u == 0 ? "versions" : "callers", p);
            built[u][p] = check_test_cpu_dispatch_build(p == 0 ? clang : executable, sources[u], objects[u][p], true, p == 2, NULL);
            char description[256];
            snprintf(description, sizeof(description), "target versions: the %s unit compiles through %s", u == 0 ? "versions'" : "callers'",
                     p == 0 ? "clang" : p == 1 ? "the vector path" : "the scalar path");
            check_test_expect(test, built[u][p], description);
        }
        const bool read = symbols[0] != NULL && symbols[1] != NULL && symbols[2] != NULL && built[u][0] && built[u][1] && built[u][2] &&
                          check_test_target_versions_symbols(objects[u][0], symbols[0], SYMBOLS) && check_test_target_versions_symbols(objects[u][1], symbols[1], SYMBOLS) &&
                          check_test_target_versions_symbols(objects[u][2], symbols[2], SYMBOLS);
        for (uint32_t p = 1; p < 3; p += 1)
        {
            char description[SYMBOLS * 2 + 256];
            snprintf(description, sizeof(description), "target versions: the %s object's symbols are clang's, by the %s path:\n%s(clang\n%s)", u == 0 ? "versions'" : "callers'",
                     p == 1 ? "vector" : "scalar", read ? symbols[p] : "", read ? symbols[0] : "");
            check_test_expect(test, read && check_test_target_versions_same_lines(symbols[0], symbols[p]), description);
        }
        check_test_expect(test, built[u][1] && built[u][2] && check_test_driver_same_files(objects[u][1], objects[u][2]),
                          "target versions: the scalar and vector objects match byte for byte");
        const int64_t places = built[u][0] ? check_test_target_versions_places(objects[u][0]) : -1;
        for (uint32_t p = 1; p < 3; p += 1)
        {
            const int64_t own = built[u][p] ? check_test_target_versions_places(objects[u][p]) : -2;
            char description[256];
            snprintf(description, sizeof(description), "target versions: the %s object's functions have clang's %lld places of code, by the %s path: %lld",
                     u == 0 ? "versions'" : "callers'", (long long)places, p == 1 ? "vector" : "scalar", (long long)own);
            check_test_expect(test, places >= 0 && own == places, description);
        }
    }
    for (uint32_t i = 0; i < 3; i += 1)
    {
        free(symbols[i]);
    }
    static const uint32_t mixes[][2] = { { 0, 0 }, { 1, 1 }, { 2, 2 }, { 1, 0 }, { 0, 1 }, { 2, 0 }, { 0, 2 } };
    char runs[BUSTER_ARRAY_LENGTH(mixes)][512];
    char program[PATH_MAX];
    snprintf(program, sizeof(program), "%s/target-mixed", directory);
    for (uint32_t m = 0; written && m < BUSTER_ARRAY_LENGTH(mixes); m += 1)
    {
        const bool ran = built[0][mixes[m][0]] && built[1][mixes[m][1]] &&
                         check_test_cpu_dispatch_link_run(clang, objects[0][mixes[m][0]], objects[1][mixes[m][1]], program, runs[m], sizeof(runs[m]));
        char description[1280];
        snprintf(description, sizeof(description), "target versions: the versions' object %u and the callers' %u link and run as clang's: \"%s\" (clang \"%s\")",
                 mixes[m][0], mixes[m][1], ran ? runs[m] : "", runs[0]);
        check_test_expect(test, ran && strcmp(runs[m], runs[0]) == 0, description);
    }
    unlink(program);
    // The direct path's executable of both units, by both paths, and -g's object linked by cc.
    char together[PATH_MAX];
    snprintf(together, sizeof(together), "%s/target-together.c", directory);
    char text[sizeof(versions) + sizeof(callers) + 32];
    snprintf(text, sizeof(text), "%s%s", versions, callers);
    const bool joined = written && parse_test_write(together, text, strlen(text));
    // The unit's -c object has clang's symbols: the labels' references are the versions' symbols, with no undefined
    // symbol of their names (#2204).
    {
        char joined_objects[3][PATH_MAX];
        char* joined_symbols[3] = { malloc(SYMBOLS), malloc(SYMBOLS), malloc(SYMBOLS) };
        bool joined_built[3] = { false, false, false };
        for (uint32_t p = 0; joined && p < 3; p += 1)
        {
            snprintf(joined_objects[p], sizeof(joined_objects[p]), "%s/target-together-%u.o", directory, p);
            joined_built[p] = check_test_cpu_dispatch_build(p == 0 ? clang : executable, together, joined_objects[p], true, p == 2, NULL);
        }
        const bool read = joined_symbols[0] != NULL && joined_symbols[1] != NULL && joined_symbols[2] != NULL && joined_built[0] && joined_built[1] && joined_built[2] &&
                          check_test_target_versions_symbols(joined_objects[0], joined_symbols[0], SYMBOLS) &&
                          check_test_target_versions_symbols(joined_objects[1], joined_symbols[1], SYMBOLS) &&
                          check_test_target_versions_symbols(joined_objects[2], joined_symbols[2], SYMBOLS);
        for (uint32_t p = 1; joined && p < 3; p += 1)
        {
            char description[SYMBOLS * 2 + 256];
            snprintf(description, sizeof(description), "target versions: one unit's object of labels of its versions' names has clang's symbols, by the %s path:\n%s(clang\n%s)",
                     p == 1 ? "vector" : "scalar", read ? joined_symbols[p] : "", read ? joined_symbols[0] : "");
            check_test_expect(test, read && check_test_target_versions_same_lines(joined_symbols[0], joined_symbols[p]), description);
        }
        for (uint32_t p = 0; p < 3; p += 1)
        {
            free(joined_symbols[p]);
            if (joined)
            {
                unlink(joined_objects[p]);
            }
        }
    }
    for (uint32_t p = 0; joined && p < 4; p += 1)
    {
        char built_program[PATH_MAX];
        char debug_object[PATH_MAX];
        char captured[512] = { 0 };
        snprintf(built_program, sizeof(built_program), "%s/target-direct-%u", directory, p);
        snprintf(debug_object, sizeof(debug_object), "%s/target-debug-%u.o", directory, p);
        bool ran = false;
        if (p < 2)
        {
            ran = check_test_cpu_dispatch_build(executable, together, built_program, false, p == 1, NULL) && check_test_driver_capture(built_program, captured, sizeof(captured));
        }
        else
        {
            char* arguments[] = { (char*)executable, "-std=gnu17", "-O0", "-w", "-g", "-c", together, "-o", debug_object, p == 3 ? "--scalar" : NULL, NULL };
            char* output = NULL;
            const bool compiled = check_test_driver_spawn(arguments, &output) == 0;
            free(output);
            output = NULL;
            char* link[] = { (char*)clang, debug_object, "-o", built_program, NULL };
            const bool linked = compiled && check_test_driver_spawn(link, &output) == 0;
            free(output);
            ran = linked && check_test_driver_capture(built_program, captured, sizeof(captured));
        }
        char description[1280];
        snprintf(description, sizeof(description), "target versions: %s, by the %s path, runs as clang's build: \"%s\" (clang \"%s\")",
                 p < 2 ? "the direct path's executable" : "-g's object", p % 2 == 0 ? "vector" : "scalar", captured, runs[0]);
        check_test_expect(test, ran && strcmp(captured, runs[0]) == 0, description);
        unlink(built_program);
        unlink(debug_object);
    }
    for (uint32_t u = 0; u < 2; u += 1)
    {
        for (uint32_t p = 0; p < 3; p += 1)
        {
            unlink(objects[u][p]);
        }
        unlink(sources[u]);
    }
    unlink(together);
}

// The resolvers' choices over libgcc's CPU model, which a unit defines (statically initialized, as an IFUNC's resolver
// runs at load), for each of a set of models: target's versions, each returning its own number, by clang and both paths;
// and target_clones', whose versions share their code here, by calling the resolver of a unit that declares them (built
// by clang and both paths, linked before clang's object of their definition, whose versions have addresses of their
// own) and naming the version it returns.
static void check_test_target_versions_models(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char model[] =
        "int printf(const char*, ...);\n"
        "struct M { unsigned vendor, type, subtype; unsigned features[1]; } __cpu_model = { VENDOR, TYPE, SUBTYPE, { W0 } };\n"
        "unsigned __cpu_features2[3] = { W1, W2, W3 };\n"
        // Once main calls a resolver, the frames an unwinder walks from inside it: through the resolver's unwind entry.
        "int _Unwind_Backtrace(int (*)(void*, void*), void*);\n"
        "static int armed;\nstatic int frames;\n"
        "static int frame(void* context, void* count) { (void)context; *(int*)count += 1; return 0; }\n"
        "void __cpu_indicator_init(void) { int count = 0; if (armed) { _Unwind_Backtrace(frame, &count); frames = count; } }\n"
        "__attribute__((target(\"avx2\"))) int f(void) { return 1; }\n"
        "__attribute__((target(\"default\"))) int f(void) { return 0; }\n"
        "__attribute__((target(\"arch=znver4\"))) int f(void) { return 2; }\n"
        "__attribute__((target(\"sse4.2,popcnt\"))) int f(void) { return 3; }\n"
        "__attribute__((target(\"avx2,bmi\"))) int f(void) { return 4; }\n"
        "__attribute__((target(\"bmi2,avx2\"))) int f(void) { return 5; }\n"
        "__attribute__((target(\"gfni\"))) int f(void) { return 6; }\n"
        "__attribute__((target(\"arch=sandybridge,avx\"))) int f(void) { return 7; }\n"
        "__attribute__((target(\"avx512vp2intersect\"))) int f(void) { return 8; }\n"
        "__attribute__((target_clones(\"avx2,bmi\", \"bmi2\", \"arch=x86-64-v4\", \"arch=znver5\", \"avx10.1\", \"sse4.1\", \"default\"))) int c(void);\n"
        "void* resolve(void) __asm__(\"c.resolver\");\n"
        "int c0(void) __asm__(\"c.avx2.0\");\nint c1(void) __asm__(\"c.bmi.1\");\nint c2(void) __asm__(\"c.bmi2.2\");\n"
        "int c3(void) __asm__(\"c.arch_x86-64-v4.3\");\nint c4(void) __asm__(\"c.arch_znver5.4\");\nint c5(void) __asm__(\"c.avx10.1.5\");\n"
        "int c6(void) __asm__(\"c.sse4.1.6\");\nint c7(void) __asm__(\"c.default.7\");\n"
        "int (*address_c)(void) = c;\n"
        // A repeated version and a default that isn't last: d.avx2.0, d.sse4.2.1 and d.default.3.
        "__attribute__((target_clones(\"default\", \"avx2\", \"sse4.2\", \"avx2\"))) int d(void);\n"
        "void* resolve_d(void) __asm__(\"d.resolver\");\n"
        "int d0(void) __asm__(\"d.avx2.0\");\nint d1(void) __asm__(\"d.sse4.2.1\");\nint d3(void) __asm__(\"d.default.3\");\n"
        "int (*address_d)(void) = d;\n"
        // arch= of a CPU type (atom), and target_clones' of a name __builtin_cpu_is doesn't know (skx, corei7-avx: tests of
        // the vendor against 0).
        "__attribute__((target(\"default\"))) int h(void) { return 0; }\n"
        "__attribute__((target(\"arch=atom\"))) int h(void) { return 2; }\n"
        "__attribute__((target(\"avx2\"))) int h(void) { return 3; }\n"
        "__attribute__((target_clones(\"arch=skx\", \"arch=corei7-avx\", \"sse4.2\", \"default\"))) int e(void);\n"
        "void* resolve_e(void) __asm__(\"e.resolver\");\n"
        "int e0(void) __asm__(\"e.arch_skx.0\");\nint e1(void) __asm__(\"e.arch_corei7-avx.1\");\nint e2(void) __asm__(\"e.sse4.2.2\");\n"
        "int e3(void) __asm__(\"e.default.3\");\n"
        "int (*address_e)(void) = e;\n"
        "int main(void)\n{\n    armed = 1;\n    void* chosen = resolve();\n    void* versions[] = { (void*)c0, (void*)c1, (void*)c2, (void*)c3, (void*)c4, (void*)c5, (void*)c6, (void*)c7 };\n"
        "    int index = -1;\n    for (int i = 0; i < 8; i += 1) { index = versions[i] == chosen ? i : index; }\n"
        "    void* chosen_d = resolve_d();\n    int index_d = chosen_d == (void*)d0 ? 0 : chosen_d == (void*)d1 ? 1 : chosen_d == (void*)d3 ? 3 : -1;\n"
        "    void* chosen_e = resolve_e();\n"
        "    int index_e = chosen_e == (void*)e0 ? 0 : chosen_e == (void*)e1 ? 1 : chosen_e == (void*)e2 ? 2 : chosen_e == (void*)e3 ? 3 : -1;\n"
        "    printf(\"%d %d %d %d %d %d\\n\", f(), index, index_d, h(), index_e, frames);\n    return 0;\n}\n";
    static const char clones[] =
        "__attribute__((target_clones(\"avx2,bmi\", \"bmi2\", \"arch=x86-64-v4\", \"arch=znver5\", \"avx10.1\", \"sse4.1\", \"default\"))) int c(void) { return 1; }\n"
        "__attribute__((target_clones(\"default\", \"avx2\", \"sse4.2\", \"avx2\"))) int d(void) { return 2; }\n"
        "__attribute__((target_clones(\"arch=skx\", \"arch=corei7-avx\", \"sse4.2\", \"default\"))) int e(void) { return 3; }\n";
    char path[PATH_MAX];
    char clones_path[PATH_MAX];
    char clones_object[PATH_MAX];
    snprintf(path, sizeof(path), "%s/target-models.c", directory);
    snprintf(clones_path, sizeof(clones_path), "%s/target-clones.c", directory);
    snprintf(clones_object, sizeof(clones_object), "%s/target-clones.o", directory);
    const bool written = parse_test_write(path, model, sizeof(model) - 1) && parse_test_write(clones_path, clones, sizeof(clones) - 1) &&
                         check_test_cpu_dispatch_build(clang, clones_path, clones_object, true, false, NULL);
    check_test_expect(test, written, "target versions: the models' fixtures are written");
    // The models: none, every bit, each version's own bits, its CPU's (znver4 is AMD's family 19h's subtype 29;
    // sandybridge Intel's corei7's 3), and each with its highest bit of word 0 cleared.
    static const uint32_t models[][7] = {
        { 0, 0, 0, 0, 0, 0, 0 },
        { 0, 0, 0, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX },
        { 0, 0, 0, 0x400u, 0, 0, 0 },
        { 0, 0, 0, 0x10400u, 0, 0, 0 },
        { 0, 0, 0, 0x30400u, 0, 0, 0 },
        { 0, 0, 0, 0x20400u, 0, 0, 0 },
        { 0, 0, 0, 0x104u, 0, 0, 0 },
        { 0, 0, 0, 0x100u, 0, 0, 0 },
        { 0, 0, 0, 0, 1u, 0, 0 },
        { 2, 3, 29, 0, 0, 0, 0 },
        { 2, 3, 29, 0x400u, 1u, 0, 0 },
        { 1, 1, 3, 0x200u, 0, 0, 0 },
        { 1, 1, 3, 0, 0, 0, 0 },
        { 0, 0, 0, 0, 0x20u, 0, 0 },
        { 0, 0, 0, 0, 0, 0, 1u },
        { 0, 0, 0, 0, 0, 0, 0x8000u },
        { 0, 0, 0, 0, 0, 0, 0x40000u },
        { 0, 0, 0, 0, 0, 0, 0x4u },
        { 2, 3, 30, 0, 0, 0, 0 },
        { 0, 0, 0, 0x80u, 0, 0, 0 },
        { 0, 0, 0, 0x20000u, 0, 0, 0 },
        // atom, Intel's type 1, with and without avx2 and sse4.2; Intel's vendor with them.
        { 1, 1, 0, 0, 0, 0, 0 },
        { 1, 1, 0, 0x500u, 0, 0, 0 },
        { 1, 0, 0, 0x500u, 0, 0, 0 },
    };
    for (uint32_t m = 0; written && m < BUSTER_ARRAY_LENGTH(models); m += 1)
    {
        static const char* const names[] = { "VENDOR", "TYPE", "SUBTYPE", "W0", "W1", "W2", "W3" };
        char defines[7][48];
        for (uint32_t d = 0; d < 7; d += 1)
        {
            snprintf(defines[d], sizeof(defines[d]), "-D%s=%uu", names[d], models[m][d]);
        }
        char captured[3][64] = { { 0 } };
        bool ran[3] = { false };
        for (uint32_t b = 0; b < 3; b += 1)
        {
            char object[PATH_MAX];
            char built[PATH_MAX];
            snprintf(object, sizeof(object), "%s/target-model-%u.o", directory, b);
            snprintf(built, sizeof(built), "%s/target-model-%u", directory, b);
            char* arguments[20] = { (char*)(b == 0 ? clang : executable), "-std=gnu17", "-O0", "-w", "-c", defines[0], defines[1], defines[2], defines[3], defines[4],
                                    defines[5], defines[6], path, "-o", object, b == 2 ? "--scalar" : NULL, NULL };
            char* text = NULL;
            unlink(object);
            const bool compiled = check_test_driver_spawn(arguments, &text) == 0;
            free(text);
            ran[b] = compiled && check_test_cpu_dispatch_link_run(clang, object, clones_object, built, captured[b], sizeof(captured[b]));
            unlink(object);
            unlink(built);
        }
        for (uint32_t b = 1; b < 3; b += 1)
        {
            char description[384];
            snprintf(description, sizeof(description), "target versions: the resolvers of model %u choose clang's versions, by the %s path: \"%s\" (clang \"%s\")", m,
                     b == 1 ? "vector" : "scalar", captured[b], captured[0]);
            check_test_expect(test, ran[0] && ran[b] && strcmp(captured[b], captured[0]) == 0, description);
        }
    }
    unlink(path);
    unlink(clones_path);
    unlink(clones_object);
}

static void check_test_driver_target_versions(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_target_versions_diagnostics(test, clang, executable, directory);
    check_test_target_versions_objects(test, clang, executable, directory);
    check_test_target_versions_models(test, clang, executable, directory);
}
