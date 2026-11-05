// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// clang's lifetimebound, loader_uninitialized, weak_import, internal_linkage and nocf_check, and its thread-safety
// analysis's attributes (#147), by the driver against clang, on original fixtures. Each unit's whole output (diagnostics, their places and notes) must be clang's by both
// paths, in C17 and C23: their subjects, their handlers' warnings and errors, and their redeclarations. Then a unit of
// their effects must give clang's symbols (binding, type, size, common or not), by both paths and with -fcommon: a
// loader_uninitialized variable is a definition without an initializer (weak where common linkage would be clang's), a
// weak_import declaration a weak reference where the unit doesn't define its entity, and internal_linkage a local
// symbol. A program of them must run as clang's build does. Include after c_check_common_driver_test.c, whose symbol
// reader it takes.

static void check_test_driver_thread_arguments(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char* const units[] = {
        "int g; int x __attribute__((guarded_by(g.x)));\n",
        "int g; int x __attribute__((guarded_by(g)));\n",
        "int *g; int x __attribute__((guarded_by(g)));\n",
        "int x __attribute__((guarded_by(1)));\n",
        "int x __attribute__((guarded_by(\"lock\")));\n",
        "int x __attribute__((guarded_by(\"\")));\n",
        "int x __attribute__((guarded_by(\"*\")));\n",
        "int g; int x __attribute__((pt_guarded_by(g)));\n",
        "int x __attribute__((pt_guarded_var));\n",
        "int *x __attribute__((pt_guarded_var));\n",
        "int f(void) __attribute__((try_acquire_capability(1.0 + 2.0)));\n",
        "int f(void) __attribute__((try_acquire_capability(1L)));\n",
        "int f(void) __attribute__((try_acquire_capability('x')));\n",
        "void f(void) __attribute__((acquire_capability()));\n",
        "struct __attribute__((capability(\"mutex\"))) M { int n; }; struct M m; int x __attribute__((guarded_by(m)));\n",
        "struct __attribute__((capability(\"mutex\"))) M { int n; }; struct M *m; int x __attribute__((guarded_by(m)));\n",
        "struct __attribute__((capability(\"mutex\"))) M { int n; }; struct M m; int x __attribute__((guarded_by(m || m)));\n",
        "struct __attribute__((capability(\"mutex\"))) M { int n; }; struct M *m; int x __attribute__((guarded_by(m || !m)));\n",
        "struct M { int n; }; struct M m; int x __attribute__((guarded_by(m)));\n",
        "struct M; struct M *m; int x __attribute__((guarded_by(m)));\n",
        "struct S { int m; int x __attribute__((guarded_by(m))); };\n",
        "struct S { struct { int n; } m; int x __attribute__((guarded_by(m.n))); };\n",
        "void f(int p) __attribute__((requires_capability(p + 1)));\n",
        "void f(float p) __attribute__((try_acquire_capability(p + 1)));\n",
        "struct __attribute__((capability(\"mutex\"))) M { int n; }; void f(struct M *p) __attribute__((acquire_capability(p)));\n",
        "struct __attribute__((capability(\"mutex\"))) M { int n; }; void f(struct M *p) __attribute__((acquire_capability(1)));\n",
        "void f(int p) __attribute__((acquire_capability(1)));\n",
        "int a; int b __attribute__((acquired_after(a)));\n",
        "struct __attribute__((capability(\"mutex\"))) M { int n; }; struct M a, b __attribute__((acquired_after(a)));\n",
        "int a; int b __attribute__((requires_capability(a)));\n",
        "int a; void f(int p __attribute__((requires_capability(a))));\n",
        "typedef int __attribute__((capability(\"mutex\"))) Lock; Lock a; int x __attribute__((guarded_by(a)));\n",
        "int a; [[clang::guarded_by(a)]] int x;\n",
    };
    enum { LINES = 16384 };
    char* expected_lines = malloc(LINES);
    char* actual_lines = malloc(LINES);
    char source[PATH_MAX];
    snprintf(source, sizeof(source), "%s/thread-attribute-arguments.c", directory);
    for (uint32_t unit = 0; expected_lines != NULL && actual_lines != NULL && unit < BUSTER_ARRAY_LENGTH(units); unit += 1)
    {
        const bool made = parse_test_write(source, units[unit], strlen(units[unit]));
        char* arguments[] = { (char*)clang, "-std=gnu23", "-Wthread-safety-attributes", "-fsyntax-only", "-fno-caret-diagnostics", source, NULL, NULL };
        char* expected = NULL;
        const int oracle = made ? check_test_driver_spawn(arguments, &expected) : -1;
        const bool lines = expected != NULL && check_test_diagnostic_lines(expected, expected_lines, LINES);
        char description[4096];
        snprintf(description, sizeof(description), "thread attribute %u: clang's lines read", unit);
        check_test_expect(test, made && lines, description);
        for (uint32_t scalar = 0; lines && scalar < 2; scalar += 1)
        {
            arguments[0] = (char*)executable;
            arguments[6] = scalar != 0 ? "--scalar" : NULL;
            char* actual = NULL;
            const int status = check_test_driver_spawn(arguments, &actual);
            const bool same = actual != NULL && check_test_diagnostic_lines(actual, actual_lines, LINES) && strcmp(expected_lines, actual_lines) == 0;
            snprintf(description, sizeof(description), "thread attribute %u (%s), %s: status %d (clang %d), lines\n%s(clang\n%s)", unit, units[unit],
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

static void check_test_driver_attribute_effects(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_thread_arguments(test, clang, executable, directory);
    static const struct
    {
        const char* standard;
        const char* source;
    } units[] = {
        { "gnu17", "extern int h __attribute__((weak_import));\n" },
        { "gnu17", "int h __attribute__((weak_import));\n" },
        { "gnu17", "int h __attribute__((weak_import)) = 1;\n" },
        { "gnu17", "__attribute__((weak_import)) void f(void) {}\n" },
        { "gnu17", "void f(void) {}\nvoid f(void) __attribute__((weak_import));\n" },
        { "gnu17", "void g(void) { int x __attribute__((weak_import)); }\n" },
        { "gnu17", "typedef int t __attribute__((weak_import));\n" },
        { "gnu17", "void p(int a __attribute__((weak_import)));\n" },
        { "gnu17", "struct s { int m __attribute__((weak_import)); };\n" },
        { "gnu17", "__attribute__((weak_import(1))) extern int q;\n" },
        { "gnu17", "extern int a __attribute__((weak_import)), b __attribute__((weak_import)) = 2;\n" },
        { "gnu17", "__attribute__((loader_uninitialized)) int g1;\n" },
        { "gnu17", "__attribute__((loader_uninitialized)) int g2 = 1;\n" },
        { "gnu17", "__attribute__((loader_uninitialized)) extern int g3;\n" },
        { "gnu17", "int g5 = 1;\n__attribute__((loader_uninitialized)) int g5;\n" },
        { "gnu17", "int g6;\n__attribute__((loader_uninitialized)) int g6;\n" },
        { "gnu17", "__attribute__((loader_uninitialized)) int g7;\nint g7;\n" },
        { "gnu17", "void l(void) { __attribute__((loader_uninitialized)) int x; (void)x; }\n" },
        { "gnu17", "void l2(void) { __attribute__((loader_uninitialized)) static int x; (void)x; }\n" },
        { "gnu17", "__attribute__((loader_uninitialized)) void lf(void);\n" },
        { "gnu17", "__attribute__((loader_uninitialized)) static int g9;\n" },
        { "gnu17", "__attribute__((loader_uninitialized(1))) int g10;\n" },
        { "gnu17", "struct S; struct S x __attribute__((loader_uninitialized));\n" },
        { "gnu17", "typedef struct S T; T x __attribute__((loader_uninitialized));\n" },
        { "gnu17", "int x[] __attribute__((loader_uninitialized));\n" },
        { "gnu17", "int x __attribute__((loader_uninitialized)); int x = 1;\n" },
        { "gnu17", "int x __attribute__((loader_uninitialized)); int x __attribute__((loader_uninitialized));\n" },
        { "gnu17", "int x __attribute__((loader_uninitialized)); int x; int x __attribute__((loader_uninitialized));\n" },
        { "gnu17", "static int x __attribute__((loader_uninitialized)); static int x __attribute__((loader_uninitialized));\n" },
        { "gnu17", "__attribute__((internal_linkage)) int il(void) { return 1; }\n" },
        { "gnu17", "int il2(void);\n__attribute__((internal_linkage)) int il2(void) { return 1; }\n" },
        { "gnu17", "__attribute__((internal_linkage)) int il3(void);\nint il3(void) { return 1; }\n" },
        { "gnu17", "void il4(void) { __attribute__((internal_linkage)) int x; (void)x; }\n" },
        { "gnu17", "void il5(void) { __attribute__((internal_linkage)) static int x; (void)x; }\n" },
        { "gnu17", "void il6(int a __attribute__((internal_linkage)));\n" },
        { "gnu17", "__attribute__((internal_linkage)) int iv;\nextern int iv;\n" },
        { "gnu17", "__attribute__((internal_linkage)) int iu(void);\nint use_iu(void) { return iu(); }\n" },
        { "gnu17", "struct __attribute__((internal_linkage)) S1 { int x; };\n" },
        { "gnu17", "static __attribute__((internal_linkage)) int iv2;\n" },
        { "gnu17", "int iv3;\n__attribute__((internal_linkage)) int iv3;\n" },
        { "gnu17", "int *lb(int *p __attribute__((lifetimebound)));\n" },
        { "gnu17", "void lb2(int *p __attribute__((lifetimebound)));\n" },
        { "gnu17", "int *lb3(int *p) __attribute__((lifetimebound));\n" },
        { "gnu17", "int lb5 __attribute__((lifetimebound));\n" },
        { "gnu17", "int *lb6(int *p __attribute__((lifetimebound(1))));\n" },
        { "gnu17", "void (*lb7)(int *p __attribute__((lifetimebound)));\n" },
        { "gnu17", "typedef void V;\nV lb8(int *p __attribute__((lifetimebound)));\n" },
        { "gnu17", "void (*lb9(int *p __attribute__((lifetimebound))))(void);\n" },
        { "gnu17", "__attribute__((nocf_check)) void nc(void) {}\n" },
        { "gnu17", "void (*fp)(void) __attribute__((nocf_check));\n" },
        { "gnu17", "int ncv __attribute__((nocf_check));\n" },
        { "gnu17", "void nc2(void) __attribute__((nocf_check(1)));\n" },
        { "gnu17", "typedef void nct(void) __attribute__((nocf_check));\n" },
        { "c23", "int il2(void);\n[[clang::internal_linkage]] int il2(void) { return 1; }\n" },
        { "c23", "void g(void) { [[clang::internal_linkage]] int x; (void)x; }\n" },
        { "c23", "typedef int t [[clang::weak_import]];\n" },
        { "c23", "[[clang::weak_import]] typedef int t2;\n" },
        { "c23", "[[clang::weak_import]] int wi;\n" },
        { "c23", "void f(int a [[clang::internal_linkage]]);\n" },
        { "c23", "void nc(void) [[gnu::nocf_check]];\n" },
        { "c23", "[[gnu::nocf_check]] void nc2(void);\n" },
        { "c23", "[[clang::lifetimebound]] int lbx;\n" },
        { "c23", "void lb2(int *p [[clang::lifetimebound]]);\n" },
        { "c23", "int *lb4(int *p) [[clang::lifetimebound]];\n" },
        { "c23", "[[clang::loader_uninitialized]] int lu = 1;\n" },
        { "c23", "[[clang::loader_uninitialized]] void luf(void);\n" },
        { "c23", "int ok(void);\n[[_Clang::internal_linkage]] int ok(void) { return 0; }\n" },
        { "c23", "int ok(void);\n[[clang::__internal_linkage__]] int ok(void) { return 0; }\n" },
        { "gnu17", "extern int b __attribute__((weak_import)) = 2;\n" },
        { "gnu17", "extern const int c = 2;\nvoid f(void) { extern int d; }\n" },
        // clang's thread-safety analysis's attributes: their arguments' names, counts, handlers' errors.
        { "gnu17", "struct S { int x __attribute__((guarded_by(m))); int m; };\n" },
        { "gnu17", "struct S { int m; int x __attribute__((guarded_by(m))); };\n" },
        { "gnu17", "struct S { int x __attribute__((guarded_by(nosuch))); };\n" },
        { "gnu17", "struct S { struct { int m; } in; int x __attribute__((guarded_by(in.m))); };\n" },
        { "gnu17", "void f(int a) __attribute__((requires_capability(a)));\n" },
        { "gnu17", "void f(int a, int b __attribute__((guarded_by(a))));\n" },
        { "gnu17", "int g; void f(void) __attribute__((requires_capability(g + 1)));\n" },
        { "gnu17", "int g; void f(void) __attribute__((requires_capability(!g)));\n" },
        { "gnu17", "int g; void f(void) __attribute__((requires_capability(g, \"x\", \"\")));\n" },
        { "gnu17", "void f(void) __attribute__((requires_capability(1)));\n" },
        { "gnu17", "void f(int a) __attribute__((acquire_capability(1)));\n" },
        { "gnu17", "void f(int a) __attribute__((acquire_capability(2)));\n" },
        { "gnu17", "void f(void) __attribute__((acquire_capability(0)));\n" },
        { "gnu17", "int g; int f(void) __attribute__((try_acquire_capability(g)));\n" },
        { "gnu17", "int g; int f(void) __attribute__((try_acquire_capability(1, g)));\n" },
        { "gnu17", "int f(void) __attribute__((try_acquire_capability(1.5)));\n" },
        { "gnu17", "struct __attribute__((capability(1))) C { int x; };\n" },
        { "gnu17", "struct __attribute__((capability(\"a\", \"b\"))) C { int x; };\n" },
        { "gnu17", "typedef int __attribute__((capability(\"m\"))) mt;\n" },
        { "gnu17", "int g; int *f(void) __attribute__((lock_returned(g)));\n" },
        { "gnu17", "int f(void) __attribute__((lock_returned()));\n" },
        { "gnu17", "int g; void f(void) __attribute__((locks_excluded(g)));\n" },
        { "gnu17", "void f(void) __attribute__((locks_excluded()));\n" },
        { "gnu17", "int g __attribute__((guarded_by(g)));\n" },
        { "gnu17", "int a, b __attribute__((acquired_after(a)));\n" },
        { "gnu17", "void f(void) __attribute__((guarded_by(x)));\n" },
        { "gnu17", "int g; void f(void) __attribute__((requires_capability(g)));\nvoid h(void) { f(); }\n" },
        { "gnu17", "int g; void f(void) __attribute__((requires_capability(sizeof(int))));\n" },
        { "gnu17", "int g; void f(void) __attribute__((requires_capability((struct Z *)0)));\n" },
        { "gnu17", "struct __attribute__((capability(\"mutex\"))) mu { int x; }; struct mu L; int v __attribute__((guarded_by(L)));\n" },
        { "gnu17", "struct S { int m; int x __attribute__((guarded_by(m))); };\n" },
        { "gnu17", "struct mu2 { int x; } __attribute__((lockable)); struct mu2 L2; int *p __attribute__((pt_guarded_by(L2)));\n" },
        { "gnu17", "struct __attribute__((capability(\"mutex\"))) M { int x; }; void lock(struct M *m) __attribute__((acquire_capability(m))); void unlock(struct M *m) __attribute__((release_capability(m)));\n" },
        { "gnu17", "struct __attribute__((capability(\"mutex\"))) M { int x; }; struct M mm; void f(void) __attribute__((requires_capability(mm)));\n" },
        { "gnu17", "void f(void) __attribute__((requires_capability(nosuch)));\n" },
        { "gnu17", "void f(void) __attribute__((no_thread_safety_analysis));\n" },
        { "gnu17", "int v __attribute__((guarded_by(1+2)));\n" },
        { "gnu17", "int v __attribute__((guarded_by()));\n" },
        { "gnu17", "int x; int v __attribute__((guarded_var)); int w __attribute__((guarded_by(x)));\n" },
        { "gnu17", "struct __attribute__((capability)) C2 { int x; };\n" },
        { "gnu17", "void f(void) __attribute__((acquire_capability(\"lock\")));\n" },
        { "gnu17", "void lk(void) __attribute__((exclusive_lock_function));\n" },
        { "gnu17", "int g(void) __attribute__((try_acquire_capability(1)));\n" },
        { "gnu17", "struct __attribute__((capability(\"mutex\"))) M { int x; }; struct M a, b __attribute__((acquired_after(a)));\n" },
        { "c23", "struct [[clang::capability(\"mutex\")]] M { int x; }; struct M m; int v [[clang::guarded_by(m)]];\n" },
        { "c23", "int v [[clang::guarded_by(nope)]];\n" },
        { "c23", "struct [[clang::capability(1)]] C { int x; };\n" },
        { "gnu17", "struct __attribute__((shared_capability(2))) C { int x; };\n" },
        { "gnu17", "int *p; int f(void) __attribute__((try_acquire_capability(p)));\n" },
        { "gnu17", "enum E { ON = 1 }; int f(void) __attribute__((try_acquire_capability(ON)));\n" },
        { "gnu17", "_Bool b; int f(void) __attribute__((try_acquire_capability(b)));\n" },
        { "gnu17", "int f(void) __attribute__((try_acquire_capability(\"x\")));\n" },
        { "gnu17", "int f(void) __attribute__((try_acquire_capability('c')));\n" },
        { "gnu17", "int f(void) __attribute__((exclusive_trylock_function(1.0f)));\n" },
        { "gnu17", "void f(int a, int b) __attribute__((acquire_capability(3)));\n" },
        { "gnu17", "void f(int a, int b) __attribute__((release_capability(1, 0)));\n" },
        { "gnu17", "void (*fp)(int a) __attribute__((acquire_capability(3)));\n" },
        { "gnu17", "struct S { int mu; int *p __attribute__((pt_guarded_by(mu))); int q __attribute__((pt_guarded_var)); };\n" },
        { "gnu17", "void f(void) { int x __attribute__((guarded_by(x))); }\n" },
        { "gnu17", "int a; void f(void) __attribute__((requires_capability(a))) __attribute__((locks_excluded(a)));\n" },
        { "gnu17", "__attribute__((requires_capability(nope2))) void f(void);\n" },
        { "gnu17", "int g; void f(void) __attribute__((requires_capability(&g, *&g, (g), sizeof g)));\n" },
        { "gnu17", "struct S2 { int a; struct { int b; int c __attribute__((guarded_by(b))); }; };\n" },
        { "gnu17", "struct S3 { int a; int c __attribute__((guarded_by(S3))); };\n" },
        { "gnu17", "int f(void) __attribute__((lock_returned(1, 2)));\n" },
        { "gnu17", "void f(void) __attribute__((no_thread_safety_analysis(1)));\n" },
        { "gnu17", "struct __attribute__((lockable(\"x\"))) L { int x; };\n" },
        { "gnu17", "typedef struct __attribute__((capability(\"role\"))) R R;\n" },
        { "gnu17", "void f(void) __attribute__((guarded_var));\n" },
    };
    enum { LINES = 16384 };
    char* expected_lines = malloc(LINES);
    char* actual_lines = malloc(LINES);
    char source[PATH_MAX];
    snprintf(source, sizeof(source), "%s/attribute-effects.c", directory);
    for (uint32_t unit = 0; expected_lines != NULL && actual_lines != NULL && unit < BUSTER_ARRAY_LENGTH(units); unit += 1)
    {
        char standard[32];
        snprintf(standard, sizeof(standard), "-std=%s", units[unit].standard);
        const bool made = parse_test_write(source, units[unit].source, strlen(units[unit].source));
        char* arguments[] = { (char*)clang, standard, "-fsyntax-only", "-fno-caret-diagnostics", source, NULL, NULL };
        char* expected = NULL;
        const int oracle = made ? check_test_driver_spawn(arguments, &expected) : -1;
        const bool lines = expected != NULL && check_test_diagnostic_lines(expected, expected_lines, LINES);
        char description[4096];
        snprintf(description, sizeof(description), "attribute effects %u: clang's lines read", unit);
        check_test_expect(test, made && lines, description);
        for (uint32_t scalar = 0; lines && scalar < 2; scalar += 1)
        {
            arguments[0] = (char*)executable;
            arguments[5] = scalar != 0 ? "--scalar" : NULL;
            char* actual = NULL;
            const int status = check_test_driver_spawn(arguments, &actual);
            const bool same = actual != NULL && check_test_diagnostic_lines(actual, actual_lines, LINES) && strcmp(expected_lines, actual_lines) == 0;
            snprintf(description, sizeof(description), "attribute effects %u (%s), %s: status %d (clang %d), lines\n%s(clang\n%s)", unit, units[unit].source,
                     scalar != 0 ? "scalar" : "vector", status, oracle, actual_lines, expected_lines);
            check_test_expect(test, same && (status != 0) == (oracle != 0), description);
            free(actual);
        }
        free(expected);
    }
    free(expected_lines);
    free(actual_lines);
    unlink(source);
    // The symbols, by both paths, by default and with -fcommon.
    static const char unit[] =
        "__attribute__((loader_uninitialized)) int loader;\n"
        "__attribute__((loader_uninitialized, nocommon)) int loader_kept;\n"
        "__attribute__((loader_uninitialized, common)) int loader_common;\n"
        "__attribute__((loader_uninitialized, section(\".loader\"))) int loader_named;\n"
        "__attribute__((loader_uninitialized)) static int loader_internal;\n"
        "int *loader_address(void) { return &loader_internal; }\n"
        "extern int imported __attribute__((weak_import));\n"
        "extern int imported_defined __attribute__((weak_import));\n"
        "int imported_defined = 5;\n"
        "__attribute__((weak_import)) int imported_function(void);\n"
        "int use(void) { return imported + imported_defined + (imported_function != 0); }\n"
        "__attribute__((internal_linkage)) int internal_function(void) { return 1; }\n"
        "__attribute__((internal_linkage)) int internal_variable = 3;\n"
        "__attribute__((internal_linkage)) int internal_later(void);\n"
        "int internal_later(void) { return 7; }\n"
        "int calls(void) { return internal_function() + internal_variable + internal_later(); }\n";
    static const char* const names[] = { "loader", "loader_kept", "loader_common", "loader_internal", "loader_named", "imported", "imported_defined", "imported_function",
                                          "internal_function", "internal_variable", "internal_later" };
    char path[512];
    snprintf(path, sizeof(path), "%s/attribute-effects-symbols.c", directory);
    const bool written = parse_test_write(path, unit, sizeof(unit) - 1);
    char* text = NULL;
    for (uint32_t common = 0; written && common < 2; common += 1)
    {
        char objects[3][512];
        for (uint32_t p = 0; p < 3; p += 1)
        {
            snprintf(objects[p], sizeof(objects[p]), "%s/attribute-effects-%u-%u.o", directory, common, p);
            char* arguments[] = { (char*)(p == 0 ? clang : executable), "-std=gnu17", "-O0", "-w", common != 0 ? "-fcommon" : "-fno-common", "-c", path, "-o",
                                  objects[p], p == 2 ? "--scalar" : NULL, NULL };
            const bool compiled = check_test_driver_spawn(arguments, &text) == 0;
            free(text);
            check_test_expect(test, compiled, "attribute effects: the unit compiles through clang and both checker paths");
            if (compiled && p != 0)
            {
                char difference[768];
                const bool same = check_test_driver_same_sections(objects[p], objects[0], ".loader", true, difference, sizeof(difference));
                check_test_expect(test, same, same ? "loader_uninitialized's named section matches clang's type, flags and bytes" : difference);
            }
            for (uint32_t n = 0; compiled && p != 0 && n < BUSTER_ARRAY_LENGTH(names); n += 1)
            {
                char expected[192] = { 0 };
                char actual[192] = { 0 };
                const bool both = check_test_common_symbol(objects[0], names[n], expected, sizeof(expected)) &&
                                  check_test_common_symbol(objects[p], names[n], actual, sizeof(actual));
                // A function's size is its code's, which isn't clang's.
                char* expected_size = strstr(expected, " size ");
                char* actual_size = strstr(actual, " size ");
                if (strncmp(names[n], "internal_", 9) == 0 && strstr(names[n], "variable") == NULL && expected_size != NULL && actual_size != NULL)
                {
                    *expected_size = 0;
                    *actual_size = 0;
                }
                char description[512];
                snprintf(description, sizeof(description), "attribute effects%s, by the %s path: clang's %s, and ours %s", common != 0 ? " with -fcommon" : "",
                         p == 1 ? "vector" : "scalar", expected, actual);
                check_test_expect(test, both && strcmp(expected, actual) == 0, description);
            }
        }
        for (uint32_t p = 0; p < 3; p += 1)
        {
            unlink(objects[p]);
        }
    }
    unlink(path);
    // A program of them, against clang's build: a weak_import reference the link doesn't define is null, an
    // internal_linkage function is called, and a loader_uninitialized variable is written and read.
    static const char program[] =
        "#include <stdio.h>\n"
        "extern int absent __attribute__((weak_import));\n"
        "__attribute__((weak_import)) int absent_function(void);\n"
        "__attribute__((internal_linkage)) int internal(int x) { return x * 3; }\n"
        "__attribute__((loader_uninitialized)) int loader[4];\n"
        "int main(void) { for (int i = 0; i < 4; i++) loader[i] = internal(i);\n"
        "printf(\"%d %d %d %d\\n\", &absent == 0, absent_function == 0, loader[3], internal(7)); return 0; }\n";
    char program_path[512];
    char reference[512];
    char ours[512];
    snprintf(program_path, sizeof(program_path), "%s/attribute-effects-program.c", directory);
    snprintf(reference, sizeof(reference), "%s/attribute-effects-clang", directory);
    snprintf(ours, sizeof(ours), "%s/attribute-effects-ours", directory);
    const bool program_written = parse_test_write(program_path, program, sizeof(program) - 1);
    char* clang_build[] = { (char*)clang, "-std=gnu17", "-O0", "-w", program_path, "-o", reference, NULL };
    char* our_build[] = { (char*)executable, "-std=gnu17", "-w", program_path, "-o", ours, NULL };
    const bool built = program_written && check_test_driver_spawn(clang_build, &text) == 0;
    free(text);
    const bool ours_built = built && check_test_driver_spawn(our_build, &text) == 0;
    free(text);
    char expected[256] = { 0 };
    char actual[256] = { 0 };
    const bool ran = ours_built && check_test_driver_capture(reference, expected, sizeof(expected)) && check_test_driver_capture(ours, actual, sizeof(actual));
    char description[768];
    snprintf(description, sizeof(description), "attribute effects: the program runs as clang's build: %s against %s", actual, expected);
    check_test_expect(test, ran && strcmp(expected, actual) == 0, description);
    unlink(program_path);
    unlink(reference);
    unlink(ours);
}
