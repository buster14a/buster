// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Objective-C's bridges (#1447): objc_bridge, objc_bridge_mutable and objc_bridge_related, by the driver against clang, on
// original fixtures. Each unit's whole output, by both paths, in GNU C17 and C23: objc_bridge_related's parser's errors
// (check_input_objc_bridge_related_parse) and its C23 spelling's location at its name, the handlers' errors of a first
// argument of no identifier (at the declaration's first token) and of objc_bridge on a typedef
// (check_input_attribute_objc_bridge), a later argument that names nothing, and the attributes' subjects and counts.

static void check_test_driver_objc_bridge(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char* const units[] = {
        "struct __attribute__((objc_bridge(id))) S1;\n",
        "typedef struct __attribute__((objc_bridge(NSFoo))) S2 *S2Ref;\n",
        "struct __attribute__((objc_bridge_related(NSColor, colorWithCGColor:, CGColor))) S3 { int a; };\n",
        "struct __attribute__((objc_bridge_related(NSColor,,))) S4 { int a; };\n",
        "struct __attribute__((objc_bridge_mutable(id))) S5 { int a; };\n",
        "int x __attribute__((objc_bridge(id)));\n",
        "typedef void* __attribute__((objc_bridge(id))) VP;\n",
        "struct __attribute__((objc_bridge(1))) S2;\n",
        "struct __attribute__((objc_bridge(a,b))) S3;\n",
        "struct __attribute__((objc_bridge_mutable(1))) S5 { int a; };\n",
        "struct __attribute__((objc_bridge_mutable(a,b))) S5 { int a; };\n",
        "struct __attribute__((objc_bridge_related(1))) S6;\n",
        "struct __attribute__((objc_bridge_related(A))) S7;\n",
        "struct __attribute__((objc_bridge_related(A,b:c,d))) S10;\n",
        "typedef void *__attribute__((objc_bridge(Foo))) VP2;\n",
        "typedef int *__attribute__((objc_bridge(id))) VP3;\n",
        "typedef const void *__attribute__((objc_bridge(id))) VP4;\n",
        "typedef struct __attribute__((objc_bridge_related)) S0 S0;\n",
        "struct __attribute__((objc_bridge_related())) S1;\n",
        "struct __attribute__((objc_bridge_related(A,,))) S2;\n",
        "struct __attribute__((objc_bridge_related(A,b:,c:))) S3;\n",
        "struct __attribute__((objc_bridge_related(A,b::,c))) S4;\n",
        "struct __attribute__((objc_bridge_related(A,b: :,c))) S4;\n",
        "struct __attribute__((objc_bridge_related(A,,c,))) S5;\n",
        "struct __attribute__((objc_bridge_related(A,,1))) S6;\n",
        "struct __attribute__((objc_bridge_related(A,1,))) S7;\n",
        "struct S8 { int a; } __attribute__((objc_bridge_related(A,,)));\n",
        "int v __attribute__((objc_bridge_related(A,,)));\n",
        "[[clang::objc_bridge_related(A,,)]] int w;\n",
        "struct [[clang::objc_bridge_related(A,,)]] S9;\n",
        "struct [[clang::objc_bridge_related(1)]] S10;\n",
        "struct [[clang::objc_bridge(id)]] S11;\n",
        "struct [[clang::objc_bridge(1)]] S12;\n",
        "union __attribute__((objc_bridge(id))) U { int a; };\n",
        "enum __attribute__((objc_bridge(id))) E { A };\n",
        "typedef void *VP __attribute__((objc_bridge(id)));\n",
        "typedef void *VP __attribute__((objc_bridge(1)));\n",
        "typedef void *VP __attribute__((objc_bridge(__id__)));\n",
        "typedef struct S *SP __attribute__((objc_bridge(id)));\n",
        "struct __attribute__((objc_bridge(id, id))) S;\n",
        "struct __attribute__((objc_bridge)) S;\n",
        "struct __attribute__((objc_bridge_mutable)) S;\n",
        "int k; struct __attribute__((objc_bridge_mutable(a, k))) S;\n",
        "struct __attribute__((__objc_bridge_related__(A, b:, c))) S;\n",
        "struct __attribute__((objc_bridge_related(A, b:, c), objc_bridge(B))) S { int x; };\n",
        "struct S { int a; } __attribute__((objc_bridge(1)));\n",
        "void f(void) { struct __attribute__((objc_bridge_related(A,,))) L { int a; } l; (void)l; }\n",
        "void f(void) { struct __attribute__((objc_bridge_related(A))) L { int a; }; }\n",
        "void f(void) { struct __attribute__((objc_bridge(2))) L { int a; }; }\n",
        "struct __attribute__((objc_bridge_related(A,b:,c;))) S;\n",
        "struct __attribute__((objc_bridge_related(A,b:,c d))) S;\n",
        "struct __attribute__((objc_bridge_related(A b))) S;\n",
        "struct __attribute__((objc_bridge_related( A , b : , c ))) S;\n",
        "#define REL(x) __attribute__((objc_bridge_related(x,,)))\n" "struct REL(1) S;\n",
        "typedef struct __attribute__((objc_bridge(id))) S { int a; } T __attribute__((objc_bridge(id)));\n",
        "__attribute__((objc_bridge(id))) struct S { int a; } s;\n",
        "__attribute__((objc_bridge(1))) struct S { int a; };\n",
        "typedef __attribute__((objc_bridge(1))) struct S { int a; } T;\n",
        "[[clang::objc_bridge(1)]] typedef void *T;\n",
        "typedef void *T [[clang::objc_bridge(id)]];\n",
        "typedef void *T [[clang::objc_bridge(x)]];\n",
    };
    static const char* const standards[] = { "-std=gnu17", "-std=gnu23" };
    enum { LINES = 16384 };
    char* expected_lines = malloc(LINES);
    char* actual_lines = malloc(LINES);
    char source[PATH_MAX];
    snprintf(source, sizeof(source), "%s/objc-bridge.c", directory);
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
        snprintf(description, sizeof(description), "objc bridges %u: clang's lines read", unit);
        check_test_expect(test, made && lines, description);
        for (uint32_t scalar = 0; lines && scalar < 2; scalar += 1)
        {
            arguments[0] = (char*)executable;
            arguments[5] = scalar != 0 ? "--scalar" : NULL;
            char* actual = NULL;
            const int status = check_test_driver_spawn(arguments, &actual);
            const bool same = actual != NULL && check_test_diagnostic_lines(actual, actual_lines, LINES) && strcmp(expected_lines, actual_lines) == 0;
            snprintf(description, sizeof(description), "objc bridges %u (%s %s), %s: status %d (clang %d), lines\n%s(clang\n%s)", unit, standard, text,
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
