// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original source/option fixtures for implicit function declarations; Clang is an external diagnostic/ABI oracle.

static uint32_t check_test_implicit_count(const char* text, const char* part)
{
    uint32_t count = 0;
    for (const char* at = text; at != NULL && (at = strstr(at, part)) != NULL; at += strlen(part))
    {
        count += 1;
    }
    return count;
}

#include "c_check_implicit_library_test.c"

static void check_test_driver_implicit_system(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const struct
    {
        const char* standard;
        const char* first;
        const char* second;
        uint8_t pragma;
        bool user;
    } cases[] = {
        { "-std=gnu17", NULL, NULL, false, false },
        { "-std=gnu17", "-Wsystem-headers", NULL, false, false },
        { "-std=gnu89", "-Wimplicit-function-declaration", NULL, false, false },
        { "-std=gnu89", "-Wimplicit-function-declaration", "-Wsystem-headers", false, false },
        { "-std=gnu17", "-Wsystem-headers", "-Wno-implicit-function-declaration", false, false },
        { "-std=gnu17", "-Wsystem-headers", "-Wno-error=implicit", false, false },
        { "-std=gnu17", "-Wsystem-headers", "-Wno-system-headers", false, false },
        { "-std=gnu17", "-Wno-system-headers", "-Wsystem-headers", false, false },
        { "-std=gnu23", "-Wno-implicit-function-declaration", NULL, false, false },
        { "-std=gnu17", NULL, NULL, true, false },
        { "-std=gnu17", "-Wsystem-headers", NULL, true, false },
        { "-std=gnu17", NULL, NULL, false, true },
        { "-std=gnu89", "-Werror=implicit-function-declaration", "-w", false, true },
        { "-std=gnu89", "-w", NULL, true, true },
        { "-std=gnu17", "-w", NULL, true, true },
        { "-std=gnu17", "-Wsystem-headers", NULL, 2, false },
        { "-std=gnu17", "-Wsystem-headers", NULL, 3, false },
        { "-std=gnu89", "-Wsystem-headers", NULL, 4, false },
    };
    char header[512];
    char source[512];
    char objects[2][512];
    snprintf(header, sizeof(header), "%s/implicit-system.h", directory);
    snprintf(source, sizeof(source), "%s/implicit-system.c", directory);
    snprintf(objects[0], sizeof(objects[0]), "%s/implicit-system-scalar.o", directory);
    snprintf(objects[1], sizeof(objects[1]), "%s/implicit-system-vector.o", directory);
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(cases); c += 1)
    {
        static const char* const pragmas[] = {
            "",
            "#pragma clang diagnostic error \"-Wimplicit-function-declaration\"\n",
            "#pragma clang diagnostic ignored \"-Wall\"\n",
            "#pragma clang diagnostic warning \"-Wall\"\n",
            "#pragma clang diagnostic error \"-Wall\"\n",
        };
        static const char system_only[] = "#include <implicit-system.h>\n";
        static const char user[] = "#include <implicit-system.h>\nint user_function(void){return user_missing(1);}\n";
        char body[256];
        const int body_length = snprintf(body, sizeof(body), "%sint system_function(void){return system_missing(1);}\n", pragmas[cases[c].pragma]);
        const char* unit = cases[c].user ? user : system_only;
        const bool written = body_length > 0 && (uint32_t)body_length < sizeof(body) &&
                             parse_test_write(header, body, (uint64_t)body_length) && parse_test_write(source, unit, strlen(unit));
        check_test_expect(test, written, "implicit system-header fixture is written");
        for (uint32_t mode = 0; written && mode < 2; mode += 1)
        {
            char* expected = NULL;
            char* arguments[18] = { (char*)clang, (char*)cases[c].standard, "-fno-caret-diagnostics", mode == 0 ? "-fsyntax-only" : "-c",
                                    source, "-o", objects[0], "-isystem", (char*)directory };
            uint32_t count = 9;
            if (cases[c].first != NULL)
            {
                arguments[count++] = (char*)cases[c].first;
            }
            if (cases[c].second != NULL)
            {
                arguments[count++] = (char*)cases[c].second;
            }
            const int oracle = check_test_driver_spawn(arguments, &expected);
            for (uint32_t scalar = 0; scalar < 2; scalar += 1)
            {
                char* actual = NULL;
                arguments[0] = (char*)executable;
                arguments[6] = objects[scalar];
                arguments[count] = scalar == 0 ? "--scalar" : NULL;
                arguments[count + 1] = NULL;
                unlink(objects[scalar]);
                const int status = check_test_driver_spawn(arguments, &actual);
                const bool same = expected != NULL && actual != NULL &&
                    check_test_implicit_count(expected, ": error:") == check_test_implicit_count(actual, ": error:") &&
                    check_test_implicit_count(expected, ": warning:") == check_test_implicit_count(actual, ": warning:") &&
                    check_test_implicit_count(expected, "system_missing") == check_test_implicit_count(actual, "system_missing") &&
                    check_test_implicit_count(expected, "user_missing") == check_test_implicit_count(actual, "user_missing");
                char description[160];
                snprintf(description, sizeof(description), "implicit system-header case %u %s %s agrees with Clang", c,
                         scalar == 0 ? "scalar" : "vector", mode == 0 ? "syntax" : "object");
                check_test_expect(test, oracle >= 0 && status == oracle && same, description);
                check_test_expect(test, mode == 0 || (access(objects[scalar], F_OK) == 0) == (status == 0),
                                  "implicit system-header policy controls object publication");
                free(actual);
            }
            if (mode != 0 && oracle == 0)
            {
                check_test_expect(test, check_test_driver_same_files(objects[0], objects[1]), "implicit system-header objects agree byte for byte");
            }
            free(expected);
            unlink(objects[0]);
            unlink(objects[1]);
        }
    }
    unlink(header);
    unlink(source);
}

static void check_test_driver_implicit(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const struct
    {
        const char* standard;
        const char* first;
        const char* second;
    } options[] = {
        { "-std=gnu17", NULL, NULL },
        { "-std=c99", NULL, NULL },
        { "-std=gnu17", "-Wno-error=implicit-function-declaration", NULL },
        { "-std=gnu17", "-Wno-implicit-function-declaration", NULL },
        { "-std=gnu17", "-Wno-implicit-function-declaration", "-Wno-error=implicit-function-declaration" },
        { "-std=gnu17", "-Wno-error=implicit-function-declaration", "-Werror" },
        { "-std=gnu17", "-Werror", "-Wno-error=implicit-function-declaration" },
        { "-std=gnu17", "-w", NULL },
        { "-std=gnu17", "-w", "-Wno-error=implicit-function-declaration" },
        { "-std=gnu17", "-Werror=implicit-function-declaration", NULL },
        { "-std=gnu89", NULL, NULL },
        { "-std=gnu89", "-Wimplicit-function-declaration", NULL },
        { "-std=gnu23", "-Wno-implicit-function-declaration", NULL },
        { "-std=gnu89", "-Wimplicit", NULL },
        { "-std=gnu89", "-Werror=implicit", NULL },
        { "-std=gnu17", "-Wno-implicit", NULL },
        { "-std=gnu17", "-Wno-error=implicit", NULL },
        { "-std=gnu17", "-Wno-implicit-function-declaration", "-Wimplicit-function-declaration" },
        { "-std=gnu17", "-Wno-implicit", "-Wimplicit-function-declaration" },
        { "-std=gnu17", "-Wimplicit-function-declaration", "-Wno-implicit" },
        { "-std=gnu17", "-Wimplicit", "-Wno-implicit-function-declaration" },
        { "-std=gnu17", "-Wno-implicit-function-declaration", "-Wimplicit" },
        { "-std=gnu17", "-Werror=implicit", "-Wno-error=implicit-function-declaration" },
        { "-std=gnu17", "-Wno-error=implicit-function-declaration", "-Werror=implicit" },
        { "-std=gnu17", "-Werror=implicit-function-declaration", "-Wno-error=implicit" },
        { "-std=gnu17", "-Wno-error=implicit", "-Werror=implicit-function-declaration" },
        { "-std=gnu17", "-Wno-all", NULL },
        { "-std=gnu17", "-Wall", "-Wno-all" },
        { "-std=gnu17", "-Wno-all", "-Wall" },
        { "-std=gnu17", "-Wno-everything", NULL },
        { "-std=gnu89", "-Weverything", NULL },
        { "-std=gnu17", "-Wno-implicit", "-Weverything" },
        { "-std=gnu89", "-Werror-implicit-function-declaration", NULL },
        { "-std=gnu17", "-Wno-error-implicit-function-declaration", NULL },
        { "-std=gnu17", "-Wno-error=all", NULL },
        { "-std=gnu89", "-Werror=all", NULL },
        { "-std=gnu89", "-Werror=implicit-function-declaration", "-w" },
        { "-std=gnu23", "-Wno-implicit", NULL },
        { "-std=gnu23", "-Wno-all", NULL },
    };
    static const char source[] = "int first(void);int second(void);\nint first(void){int x=missing(1);return x+missing(2);}\n"
        "int second(void){return missing(3);}\n";
    char path[512];
    char objects[2][512];
    snprintf(path, sizeof(path), "%s/implicit.c", directory);
    snprintf(objects[0], sizeof(objects[0]), "%s/implicit-scalar.o", directory);
    snprintf(objects[1], sizeof(objects[1]), "%s/implicit-vector.o", directory);
    const bool written = parse_test_write(path, source, sizeof(source) - 1);
    check_test_expect(test, written, "implicit function source fixture is written");
    for (uint32_t c = 0; written && c < BUSTER_ARRAY_LENGTH(options); c += 1)
    {
        for (uint32_t mode = 0; mode < 2; mode += 1)
        {
            char* expected = NULL;
            char* arguments[16] = { (char*)clang, (char*)options[c].standard, "-fno-caret-diagnostics", mode == 0 ? "-fsyntax-only" : "-c", path, "-o", objects[0] };
            uint32_t count = 7;
            if (options[c].first != NULL)
            {
                arguments[count++] = (char*)options[c].first;
            }
            if (options[c].second != NULL)
            {
                arguments[count++] = (char*)options[c].second;
            }
            const int oracle = check_test_driver_spawn(arguments, &expected);
            for (uint32_t scalar = 0; scalar < 2; scalar += 1)
            {
                char* actual = NULL;
                arguments[0] = (char*)executable;
                arguments[6] = objects[scalar];
                arguments[count] = scalar == 0 ? "--scalar" : NULL;
                arguments[count + 1] = NULL;
                unlink(objects[scalar]);
                const int status = check_test_driver_spawn(arguments, &actual);
                const char* message = options[c].standard[8] == '2' ? "use of undeclared identifier 'missing'" :
                                      options[c].standard[8] == '8' ? "implicit declaration of function 'missing'" : "call to undeclared function 'missing'";
                const bool same = expected != NULL && actual != NULL &&
                    check_test_implicit_count(expected, ": error:") == check_test_implicit_count(actual, ": error:") &&
                    check_test_implicit_count(expected, ": warning:") == check_test_implicit_count(actual, ": warning:") &&
                    check_test_implicit_count(expected, message) == check_test_implicit_count(actual, message);
                char description[160];
                snprintf(description, sizeof(description), "implicit function option case %u %s %s agrees with Clang", c,
                         scalar == 0 ? "scalar" : "vector", mode == 0 ? "syntax" : "object");
                check_test_expect(test, oracle >= 0 && oracle == status && same, description);
                check_test_expect(test, mode == 0 || (access(objects[scalar], F_OK) == 0) == (status == 0),
                                  "implicit function diagnostic controls object publication");
                free(actual);
            }
            if (mode != 0 && oracle == 0)
            {
                check_test_expect(test, check_test_driver_same_files(objects[0], objects[1]), "implicit function objects agree byte for byte");
            }
            free(expected);
            unlink(objects[0]);
            unlink(objects[1]);
        }
    }
    unlink(path);
    static const char* const ordinary[] = {
        "int f(void){return (missing)(1);}\n",
        "int f(void){return __builtin_missing(1);}\n",
        "int f(void){return missing;}\n",
    };
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(ordinary); c += 1)
    {
        const bool ready = parse_test_write(path, ordinary[c], strlen(ordinary[c]));
        for (uint32_t scalar = 0; ready && scalar < 2; scalar += 1)
        {
            char* actual = NULL;
            char* arguments[] = { (char*)executable, "-Wno-implicit-function-declaration", "-fsyntax-only", path,
                                  scalar != 0 ? "--scalar" : NULL, NULL };
            const int status = check_test_driver_spawn(arguments, &actual);
            check_test_expect(test, status == 1 && actual != NULL && strstr(actual, "implicit function") == NULL &&
                                    strstr(actual, "implicit-function-declaration") == NULL,
                              "non-direct callees, builtin spellings and ordinary identifiers remain hard errors");
            free(actual);
        }
    }
    unlink(path);
    static const char native[] = "int before(int n){return convert((float)n,(unsigned char)9);}\n"
        "int convert(double d,int v){return (int)(d*7)+v;}\nint main(void){return before(4)==37?0:1;}\n";
    const bool native_written = parse_test_write(path, native, sizeof(native) - 1);
    for (uint32_t path_index = 0; native_written && path_index < 3; path_index += 1)
    {
        char* diagnostics = NULL;
        char* arguments[] = { (char*)(path_index == 0 ? clang : executable), "-std=gnu17", "-Wno-implicit-function-declaration", path,
                              "-o", objects[0], path_index == 2 ? "--scalar" : NULL, NULL };
        const int status = check_test_driver_spawn(arguments, &diagnostics);
        check_test_expect(test, status == 0, "implicit function caller/callee with default float and integer promotions links");
        free(diagnostics);
        if (status == 0)
        {
            char* run[] = { objects[0], NULL };
            diagnostics = NULL;
            check_test_expect(test, check_test_driver_spawn(run, &diagnostics) == 0, "implicit promoted arguments execute as Clang's build");
            free(diagnostics);
        }
        unlink(objects[0]);
    }
    unlink(path);
    check_test_driver_implicit_system(test, clang, executable, directory);
    check_test_driver_implicit_library(test, clang, executable, directory);
}

// Whether `text[0..length)` holds `needle`, whose length is passed: the filtered diagnostic lines need no copy.
static bool check_test_driver_common_contains(const char* text, size_t length, const char* needle)
{
    const size_t wanted = strlen(needle);
    bool found = false;
    for (size_t at = 0; !found && at + wanted <= length; at += 1)
    {
        found = memcmp(text + at, needle, wanted) == 0;
    }
    return found;
}

// The diagnostics of #823's common warnings, line for line against Clang's, with the group and -Werror policy.
static void check_test_driver_common_output(const char* text, char* filtered, size_t capacity)
{
    size_t used = 0;
    filtered[0] = 0;
    for (const char* at = text; at != NULL && *at != 0 && used + 1 < capacity;)
    {
        const char* end = strchr(at, '\n');
        end = end != NULL ? end : at + strlen(at);
        const size_t length = (size_t)(end - at);
        const bool diagnostic = check_test_driver_common_contains(at, length, ": warning: ") ||
                                check_test_driver_common_contains(at, length, ": error: ") ||
                                check_test_driver_common_contains(at, length, ": note: ");
        if (diagnostic)
        {
            const size_t copied = length < capacity - used - 1 ? length : capacity - used - 1;
            memcpy(filtered + used, at, copied);
            used += copied;
            filtered[used++] = '\n';
            filtered[used] = 0;
        }
        at = *end != 0 ? end + 1 : end;
    }
}

static void check_test_driver_common_warnings(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char defaults[] =
        "#include <stdio.h>\n#include <string.h>\nenum E { A, B, C };\n"
        "int *f(void) { int a = 1; return &a; }\n"
        "void g(void) { printf(\"%d\\n\", \"str\"); }\n"
        "int h(int x) { if (x) return 1; }\n"
        "void i(int x) { if (x = 2) {} }\n"
        "int j(void) { int x = 1 << 40; return x; }\n"
        "int k(int x) { return x / 0; }\n"
        "char l(void) { char c = 300; return c; }\n"
        "int m(int x) { x == 1; return 0; }\n"
        "int n(int x) { return !x == 1; }\n"
        "int o(void) { int a[3]; return a[5]; }\n"
        "int p(int *a, unsigned *b) { a = b; return 0; }\n";
    static const char wall[] =
        "#include <stdio.h>\nenum E { A, B, C };\n"
        "static int unused_function(void) { return 0; }\n"
        "int a(void) { int unused; return 0; }\n"
        "int b(void) { int x; return x; }\n"
        "int c(enum E e) { switch (e) { case A: return 1; } return 0; }\n"
        "int d(int x, int y) { if (x) if (y) return 1; else return 2; return 0; }\n"
        "int e(int a, int b, int c) { return a & b == c; }\n"
        "int f(int x) { x = x; return x; }\n"
        "int h(int x) { if (x); return 0; }\n"
        "void j(const char *s) { printf(s); }\n";
    static const char extra[] =
        "int f(int unused) { return 0; }\n"
        "int g(int x, unsigned y) { return x < y; }\n";
    static const char clean[] =
        "int f(int x) { return x + 1; }\n"
        "static int g(void) { return 2; }\n"
        "int h(void) { return g(); }\n";
    static const char promoted[] = "int f(int x) { if (x = 1) return 1; return 0; }\n";
    static const char unused_function[] = "static int f(void) { return 0; }\n";
    static const char unused_const[] = "static const int unused_const = 3;\n";
    static const char uninit_contexts[] =
        "int a(void) { int x; return sizeof x; }\n"
        "int b(void) { int x; return (void)x, 1; }\n"
        "int c(void) { int x; return 1 || x; }\n"
        "int d(void) { int x; return 0 ? x : 1; }\n"
        "int e(void) { int x; return 1 ? 1 : x; }\n"
        "int f(void) { int x; int y = x; return y; }\n"
        "int g(void) { int x; x++; return x; }\n"
        "int h(void) { int x; x += 1; return x; }\n"
        "int i(void) { int x; x = x + 1; return x; }\n";
    static const char format_wide[] =
        "#include <stdio.h>\n#include <wchar.h>\n"
        "void f(void) { printf(\"%ls\\n\", L\"x\"); }\n"
        "void g(void) { printf(\"%lc\\n\", (wint_t)L'x'); }\n";
    static const char switch_alias[] =
        "enum E { A = 1, B = 1, C = 2 };\n"
        "int f(enum E e) { switch (e) { case A: return 1; case C: return 2; } return 0; }\n";
    static const char sign_zero[] =
        "int a(int i) { return i < 0u; }\n"
        "int b(int i) { return i >= 0u; }\n"
        "int c(int i) { return 0u <= i; }\n"
        "int d(int i) { return 0u > i; }\n"
        "int e(int x) { return x <= 0u; }\n"
        "int f(int x) { return x > 0u; }\n"
        "int g(int x) { return 0u < x; }\n"
        "int h(int x) { return 0u >= x; }\n";
    static const char array_shapes[] =
        "struct S { int a[3]; };\n"
        "int f(struct S *s) { return s->a[5]; }\n"
        "int g(struct S s) { return s.a[5]; }\n"
        "int h(void) { int b[2][3]; return b[1][5]; }\n";
    static const char unused_chains[] =
        "static int f(void);\n"
        "static int f(void) { return 1; }\n"
        "static int g(void);\n"
        "static int g(void);\n"
        "static int x;\n"
        "int h(void) { int x = 1; return x; }\n";
    static const char unused_values[] =
        "int f(int x) { x; return 0; }\n"
        "int g(int x) { (x == 1); return 0; }\n"
        "int h(int x) { (void)x; return 0; }\n"
        "int i(int x) { x + 1; return 0; }\n";
    static const char return_stack[] =
        "int *a(void) { int x[3]; return &x[0]; }\n"
        "struct S { int m; };\n"
        "int *c(void) { struct S s; return &s.m; }\n";
    static const char generic_selection[] =
        "int a(void) { int x; return _Generic(1, int: x, default: 0); }\n"
        "int b(void) { int x; return _Generic(1, int: 0, default: x); }\n"
        "int c(void) { int x; return __builtin_choose_expr(1, 0, x); }\n"
        "int d(void) { int x; return __builtin_choose_expr(0, x, 0); }\n";
    static const char self_initialization[] =
        "int a(void) { int x = x; return x; }\n"
        "int b(void) { int x = x + 1; return x; }\n";
    static const char unused_but_set_parameters[] =
        "int a(int x) { x++; return 0; }\n"
        "int b(int x) { x += 1; return 0; }\n"
        "int c(int x) { x = 1; return 0; }\n";
    static const char empty_else_body[] =
        "int a(int x) { if (x); else x++; return 0; }\n"
        "int b(int x) { if (x); return 0; }\n";
    static const char empty_bodies[] =
        "int a(int x) { while (x) ; x++; return 0; }\n"
        "int b(int x)\n{\n    for (;;) ;\n    x++;\n    return 0;\n}\n"
        "int c(int x)\n{\n    while (x) ;\n        x++;\n    return 0;\n}\n"
        "int d(int x)\n{\n    while (x) ;\n    {\n        x++;\n    }\n    return 0;\n}\n"
        "int e(int x)\n{\n    if (x) ;\n    while (x) ;\n    return 0;\n}\n"
        "int f(int x)\n{\n    if (x)\n        while (x) ;\n    return 0;\n}\n"
        "int g(int x)\n{\n    while (x)\n        ;\n    return 0;\n}\n"
        "int h(int x)\n{\n    while (x) ;\n}\n"
        "int i(int x)\n{\n    while (x) ;\n      if (x) ;\n    return 0;\n}\n"
        "int j(int x)\n{\n    switch (x) {\n    case 1:\n        while (x) ;\n        break;\n    }\n    return 0;\n}\n";
    static const char zero_length_array[] =
        "int a(void) { int x[0]; return x[0]; }\n"
        "int b(void) { int x[0]; return x[1]; }\n";
    static const char switch_fixed_shadow[] =
        "enum E : int { A, B, C };\n"
        "int f(enum E e) { switch (e) { case A: return 1; } return 0; }\n"
        "int g(void) { enum E { A, B }; enum E e = A; switch (e) { case A: return 1; case B: return 2; } return 0; }\n"
        "int h(void) { enum E { A, B, C }; enum E e = A; switch (e) { case A: return 1; case B: return 2; } return 0; }\n";
    static const char format_mismatches[] =
        "#include <stdio.h>\n"
        "void a(void){ printf(\"%lld\\n\", 1); }\n"
        "void b(void){ printf(\"%zd\\n\", 1); }\n"
        "void c(void){ printf(\"%n\\n\", (char*)0); }\n"
        "void d(void){ printf(\"%d\\n\", 1, 2); }\n"
        "void e(void){ printf(\"%d\\n\"); }\n"
        "void f(void){ printf(\"%*d\\n\", \"x\", 1); }\n"
        "void g(void){ printf(\"%ls\\n\", (unsigned int*)0); }\n";
    static const char unused_value_statements[] =
        "int f(int x){ (int)x; return 0; }\n"
        "int g(int x){ x ? 1 : 2; return 0; }\n"
        "int h(int x,int y){ (x, y); return 0; }\n"
        "int i(int x){ (void)x; return 0; }\n";
    static const char for_condition[] =
        "int a(int x){ for (; x = 2; ) { x++; } return x; }\n";
    static const char vla_bounds[] =
        "int f(void) { int x; return sizeof(int[x]); }\n"
        "int g(void) { int x; int y[x]; return y[0]; }\n";
    static const char enum_scope[] =
        "enum E { A, B };\n"
        "int f(void) { enum E e = A; { enum E { A, B, C }; } switch (e) { case A: return 1; case B: return 2; } return 0; }\n";
    static const char switch_many[] =
        "enum E { E0, E1, E2, E3, E4, E5, E6, E7, E8, E9, E10, E11, E12, E13, E14, E15, E16, E17, E18, E19, E20, E21, E22, E23, E24, E25, E26, E27, E28, E29, E30, E31, E32, E33, E34, E35, E36, E37, E38, E39, E40, E41, E42, E43, E44, E45, E46, E47, E48, E49, E50, E51, E52, E53, E54, E55, E56, E57, E58, E59, E60, E61, E62, E63, E64 };\n"
        "int f(enum E e) { switch (e) { case E0: return 0; case E1: return 0; case E2: return 0; case E3: return 0; case E4: return 0; case E5: return 0; case E6: return 0; case E7: return 0; case E8: return 0; case E9: return 0; case E10: return 0; case E11: return 0; case E12: return 0; case E13: return 0; case E14: return 0; case E15: return 0; case E16: return 0; case E17: return 0; case E18: return 0; case E19: return 0; case E20: return 0; case E21: return 0; case E22: return 0; case E23: return 0; case E24: return 0; case E25: return 0; case E26: return 0; case E27: return 0; case E28: return 0; case E29: return 0; case E30: return 0; case E31: return 0; case E32: return 0; case E33: return 0; case E34: return 0; case E35: return 0; case E36: return 0; case E37: return 0; case E38: return 0; case E39: return 0; case E40: return 0; case E41: return 0; case E42: return 0; case E43: return 0; case E44: return 0; case E45: return 0; case E46: return 0; case E47: return 0; case E48: return 0; case E49: return 0; case E50: return 0; case E51: return 0; case E52: return 0; case E53: return 0; case E54: return 0; case E55: return 0; case E56: return 0; case E57: return 0; case E58: return 0; case E59: return 0; case E60: return 0; case E61: return 0; case E62: return 0; case E63: return 0; } return 0; }\n"
        "enum G { G0, G1, G2, G3, G4, G5, G6, G7, G8, G9 };\n"
        "int g(enum G x) { switch (x) { case G0: return 1; case G1: return 1; case G2: return 1; case G3: return 1; case G4: return 1; case G5: return 1; } return 0; }\n";
    static const char comma_effects[] =
        "int f(void);\n"
        "int a(void) { int y; (f(), y); return 0; }\n"
        "int c(int x, int y) { (x++, y); return 0; }\n"
        "int d(int x, int y) { x, y; return 0; }\n";
    static const char typedef_void[] =
        "typedef void V;\n"
        "int f(int x) { (V)x; return 0; }\n"
        "int g(int x) { (void)x; return 0; }\n";
    static const char format_positional[] =
        "#include <stdio.h>\n"
        "void a(void) { printf(\"%1$d\\n\", 1); }\n"
        "void b(void) { printf(\"%1$d %2$d\\n\", 1, 2); }\n"
        "void c(void) { printf(\"%b\\n\", 1); }\n"
        "void d(void) { printf(\"%B\\n\", 1); }\n";
    static const char format_exact[] =
        "#include <stdio.h>\n"
        "void a(void) { printf(\"%lld\\n\", 1L); }\n"
        "void b(void) { printf(\"%ld\\n\", 1LL); }\n"
        "void c(void) { printf(\"%llu\\n\", 1UL); }\n"
        "void d(void) { printf(\"%hhn\\n\", (signed char*)0); }\n"
        "void e(void) { printf(\"%hn\\n\", (short*)0); }\n"
        "void f(void) { printf(\"%ln\\n\", (long long*)0); }\n";
    static const char format_precision[] =
        "#include <stdio.h>\n"
        "void a(void) { printf(\"%.*f\\n\", \"x\", 1.0); }\n";
    static const char for_write[] =
        "int f(int x) { for (; x = 2; ) return 1; return 0; }\n";
    static const char brace_initializers[] =
        "enum E { A, B };\n"
        "int a(void) { int x = { 1 }; return x; }\n"
        "int b(void) { char c = { 'a' }; return c; }\n"
        "int c(void) { enum E e = { A }; return e; }\n"
        "int d(void) { int x = { x }; return x; }\n";
    static const char format_n_unsigned[] =
        "#include <stdio.h>\n"
        "#include <stddef.h>\n"
        "void a(void) { printf(\"%n\\n\", (unsigned int*)0); }\n"
        "void b(void) { printf(\"%hhn\\n\", (unsigned char*)0); }\n"
        "void c(void) { printf(\"%hn\\n\", (unsigned short*)0); }\n"
        "void d(void) { printf(\"%ln\\n\", (unsigned long*)0); }\n"
        "void e(void) { printf(\"%lln\\n\", (unsigned long long*)0); }\n"
        "void f(void) { printf(\"%zn\\n\", (size_t*)0); }\n"
        "void g(void) { printf(\"%tn\\n\", (size_t*)0); }\n"
        "void h(void) { printf(\"%jn\\n\", (size_t*)0); }\n"
        "void i(void) { printf(\"%ln\\n\", (long long*)0); }\n"
        "void j(void) { printf(\"%hhn\\n\", (int*)0); }\n";
    static const char comma_unparenthesized[] =
        "int f(void);\n"
        "int a(void) { int x, y; x = 1, y = 2; return x + y; }\n"
        "int b(void) { int x, y = 0; (x = 1, y); return x + y; }\n"
        "int c(void) { int y; (f(), y); return 0; }\n"
        "int d(int x, int y) { x++, y; return 0; }\n"
        "int e(int x, int y) { x, y; return 0; }\n";
    static const char positional_format_edges[] =
        "#include <stdio.h>\n"
        "void a(void) { printf(\"%2$s\\n\", 1, \"x\"); }\n"
        "void b(void) { printf(\"%2$d\\n\", \"x\", 1); }\n"
        "void c(void) { printf(\"%2$s %1$d\\n\", 1, \"x\"); }\n"
        "void d(void) { printf(\"%2$d\\n\", 1); }\n"
        "void e(void) { printf(\"%1$d\\n\", 1, 2); }\n"
        "void f(void) { printf(\"%3$*1$d\\n\", 2, 0, 7); }\n"
        "void g(void) { printf(\"%1$*2$d\\n\", 7, 2); }\n"
        "void h(void) { printf(\"%1$d %d\\n\", 1, 2); }\n"
        "void i(void) { printf(\"%0$d\\n\", 1); }\n"
        "void j(void) { printf(\"%1$.*2$f\\n\", 1.0, 2); }\n";
    static const char format_integer_class[] =
        "#include <stdio.h>\n"
        "void a(void) { printf(\"%ld\\n\", 1UL); }\n"
        "void b(void) { printf(\"%lld\\n\", 1ULL); }\n"
        "void c(void) { printf(\"%lu\\n\", 1L); }\n"
        "void d(void) { printf(\"%llu\\n\", 1LL); }\n"
        "void e(void) { printf(\"%b\\n\", 1L); }\n"
        "void f(void) { printf(\"%lb\\n\", 1ULL); }\n"
        "void g(void) { printf(\"%hhd\\n\", 1L); }\n"
        "void h(void) { printf(\"%hd\\n\", 1L); }\n"
        "void i(void) { printf(\"%tu\\n\", 1LL); }\n";
    static const char format_specifiers[] =
        "#include <stdio.h>\n"
        "void a(void) { printf(\"%D\\n\", 1); }\n"
        "void b(void) { printf(\"%D\\n\", 1, 2); }\n"
        "void c(void) { printf(\"%D %d\\n\", 1, 2); }\n"
        "void d(void) { printf(\"%!\\n\", 1); }\n"
        "void e(void) { printf(\"%*!\\n\", 1, 2); }\n"
        "void f(void) { printf(\"%.*!\\n\", 1, 2, 3); }\n"
        "void g(void) { printf(\"%*0$d\\n\", 1, 2); }\n"
        "void h(void) { printf(\"%*1$d\\n\", 1, 2); }\n"
        "void i(void) { printf(\"%2$*0$d\\n\", 1, 2); }\n"
        "void j(void) { printf(\"%$\\n\"); }\n"
        "void k(void) { printf(\"%1$!\\n\", 1); }\n"
        "void l(void) { printf(\"%2$!\\n\", 1, 2); }\n"
        "void m(void) { printf(\"%m\\n\"); }\n"
        "void n(void) { printf(\"%D %s\\n\", 1, \"x\"); }\n"
        "void o(void) { printf(\"%Z %s\\n\", 1, \"x\"); }\n"
        "void p(void) { printf(\"%%\\n\", 1); }\n"
        "void q(void) { printf(\"%d %1$d\\n\", 1, 2); }\n";
    static const char format_modifiers[] =
        "#include <stdio.h>\n"
        "void a(void) { printf(\"%qd\\n\", 1); }\n"
        "void b(void) { printf(\"%qd\\n\", 1LL); }\n"
        "void c(void) { printf(\"%qn\\n\", (long long*)0); }\n"
        "void d(void) { printf(\"%qn\\n\", (char*)0); }\n"
        "void e(void) { printf(\"%qs\\n\", (int*)0); }\n"
        "void f(void) { printf(\"%qc\\n\", 1.5); }\n"
        "void g(void) { printf(\"%qp\\n\", 1); }\n"
        "void h(void) { printf(\"%qf\\n\", 1); }\n"
        "void i(void) { printf(\"%wd\\n\", \"x\"); }\n"
        "void j(void) { printf(\"%wc\\n\", 1LL); }\n"
        "void k(void) { printf(\"%ws\\n\", \"x\"); }\n"
        "void l(void) { printf(\"%wp\\n\", 1); }\n"
        "void m(void) { printf(\"%wn\\n\", (int*)0); }\n"
        "void n(void) { printf(\"%w64d\\n\", 1LL); }\n"
        "void o(void) { printf(\"%q64d\\n\", 1LL); }\n"
        "void p(void) { printf(\"%w$d\\n\", 1); }\n"
        "void q(void) { printf(\"%qw$d\\n\", 1); }\n"
        "void r(void) { printf(\"%q!d\\n\", 1); }\n";
    static const char format_argument_order[] =
        "#include <stdio.h>\n"
        "char *s; int x;\n"
        "void a(void) { printf(\"%s %d %d\\n\", 1, s, 3); }\n"
        "void b(void) { printf(\"%s %d %d\\n\", 1, 2); }\n"
        "void c(void) { printf(\"%s %d %d %d\\n\", 1, 2, 3, 4); }\n"
        "void d(void) { printf(\"%*s %d\\n\", s, s, 3); }\n"
        "void e(void) { printf(\"%s %*d\\n\", 1, x, s); }\n"
        "void f(void) { printf(\"%n %d\\n\", s, x); }\n";
    static const char comma_statements[] =
        "int y;\n"
        "struct S { int m; };\n"
        "void a(void) { int a; (a, y); }\n"
        "void b(void) { int b; (b + 1, y); }\n"
        "void c(void) { int c; ((int)c, y); }\n"
        "void d(void) { int d; ((long)d, y); }\n"
        "void e(void) { int e; (e = 1, y); y = e; }\n"
        "void f(int x) { (x == 1, y); }\n"
        "void g(int x) { (x != 1, y); }\n"
        "void h(void) { int h; ((void)h, y); }\n"
        "void i(void) { int *p; ((void*)p, y); }\n"
        "void j(void) { int j; (j ? j : 1, y); }\n"
        "void k(void) { int k; ((k), (k), y); }\n"
        "void l(void) { int l; (l + 0, y); }\n"
        "void m(void) { int m; m, y; }\n"
        "void n(void) { struct S s; (s.m, y); }\n"
        "void o(void) { for (int i = 0, j = 0; i < 3; i++) y += j; }\n"
        "void p(void) { int v = (v, 1); (void)v; }\n"
        "void q(void) { int q; q; (q, y); }\n"
        "void r(void) { long r; ((void*)r, y); }\n"
        "void s(void) { long s; ((const void*)s, y); }\n"
        "void t(void) { int t; (t, (void*)&t); }\n"
        "void u(void) { long u; (void*)u; y += u; }\n";
    static const char self_init_edges[] =
        "int a(void) { int x = x; return x; }\n"
        "int b(void) { int x = { x }; return x; }\n"
        "int c(void) { int x = { x + 1 }; return x; }\n"
        "int d(void) { int x = { (x = 2, x) }; return x; }\n"
        "int e(void) { int x; x = x; x; return 0; }\n"
        "int f(void) { int x; x = x; return x; }\n"
        "int g(void) { int x = { (x, 1) }; return x; }\n"
        "int h(void) { int x = { (int)x }; return x; }\n"
        "int i(void) { int x = { x = x }; return x; }\n"
        "int j(void) { int x = { x++ }; return x; }\n"
        "int k(void) { int x = { 0 ? x : 1 }; return x; }\n"
        "int l(void) { int x = { 1 ? 1 : x }; return x; }\n"
        "int m(void) { int x = { sizeof(x) ? 0 : x }; return x; }\n"
        "int n(void) { int x = { !x }; return x; }\n"
        "int o(void) { int x = { x || 1 }; return x; }\n"
        "int p(void) { int x = { x && 1 }; return x; }\n"
        "int q(void) { int x = (x = x); return x; }\n"
        "int r(void) { int x; x = (x); x; return 0; }\n";
    static const struct
    {
        const char* source;
        const char* options[5];
    } cases[] = {
        { defaults, { NULL, NULL, NULL, NULL } },
        { wall, { "-Wall", NULL, NULL, NULL } },
        { extra, { "-Wextra", NULL, NULL, NULL } },
        { promoted, { "-Werror", NULL, NULL, NULL } },
        { unused_function, { "-Werror=unused-function", NULL, NULL, NULL } },
        { unused_const, { "-Wall", NULL, NULL, NULL } },
        { uninit_contexts, { "-Wall", NULL, NULL, NULL } },
        { format_wide, { "-Wall", NULL, NULL, NULL } },
        { switch_alias, { "-Wall", NULL, NULL, NULL } },
        { sign_zero, { "-Wextra", NULL, NULL, NULL } },
        { array_shapes, { "-Wall", NULL, NULL, NULL } },
        { unused_chains, { "-Wall", NULL, NULL, NULL } },
        { unused_values, { "-Wall", NULL, NULL, NULL } },
        { return_stack, { "-Wall", NULL, NULL, NULL } },
        { generic_selection, { "-Wall", NULL, NULL, NULL } },
        { self_initialization, { "-Wall", NULL, NULL, NULL } },
        { unused_but_set_parameters, { "-Wextra", NULL, NULL, NULL } },
        { empty_else_body, { "-Wall", NULL, NULL, NULL } },
        { empty_bodies, { "-Wall", NULL, NULL, NULL } },
        { zero_length_array, { "-Wall", NULL, NULL, NULL } },
        { switch_fixed_shadow, { "-Wall", NULL, NULL, NULL } },
        { format_mismatches, { "-Wall", NULL, NULL, NULL } },
        { unused_value_statements, { "-Wall", NULL, NULL, NULL } },
        { for_condition, { "-Wall", NULL, NULL, NULL } },
        { vla_bounds, { "-Wall", NULL, NULL, NULL } },
        { enum_scope, { "-Wall", NULL, NULL, NULL } },
        { switch_many, { "-Wall", NULL, NULL, NULL } },
        { comma_effects, { "-Wall", "-Wextra", NULL, NULL } },
        { typedef_void, { "-Wall", NULL, NULL, NULL } },
        { format_positional, { "-Wall", NULL, NULL, NULL } },
        { format_exact, { "-Wall", NULL, NULL, NULL } },
        { format_precision, { "-Wall", NULL, NULL, NULL } },
        { for_write, { "-Wall", "-Wextra", NULL, NULL } },
        { brace_initializers, { "-Wall", NULL, NULL, NULL } },
        { format_n_unsigned, { "-Wall", NULL, NULL, NULL } },
        { comma_unparenthesized, { "-Wall", "-Wextra", NULL, NULL } },
        { positional_format_edges, { "-Wall", NULL, NULL, NULL } },
        { format_integer_class, { "-Wall", NULL, NULL, NULL } },
        { format_specifiers, { "-Wall", NULL, NULL, NULL } },
        { format_modifiers, { "-Wall", NULL, NULL, NULL } },
        { format_argument_order, { "-Wall", NULL, NULL, NULL } },
        { comma_statements, { "-Wall", "-Wextra", NULL, NULL } },
        { self_init_edges, { "-Wall", NULL, NULL, NULL } },
        { wall, { "-Wall", "-Wno-unused-variable", "-Wno-switch", "-Wno-dangling-else", NULL } },
        { clean, { "-Wall", "-Wextra", "-Werror", NULL } },
    };
    char source_path[512];
    snprintf(source_path, sizeof(source_path), "%s/common-warnings.c", directory);
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(cases); c += 1)
    {
        const bool written = parse_test_write(source_path, cases[c].source, strlen(cases[c].source));
        check_test_expect(test, written, "common warning fixture is written");
        for (uint32_t mode = 0; written && mode < 2; mode += 1)
        {
            char* oracle_arguments[12] = { (char*)clang, "-std=gnu17", "-fsyntax-only", "-fno-caret-diagnostics", "-fno-color-diagnostics", source_path };
            char* checker_arguments[12] = { (char*)executable, "-std=gnu17", "-fsyntax-only", source_path };
            uint32_t oracle_count = 6;
            uint32_t checker_count = 4;
            for (uint32_t o = 0; o < BUSTER_ARRAY_LENGTH(cases[c].options); o += 1)
            {
                if (cases[c].options[o] != NULL)
                {
                    oracle_arguments[oracle_count++] = (char*)cases[c].options[o];
                    checker_arguments[checker_count++] = (char*)cases[c].options[o];
                }
            }
            oracle_arguments[oracle_count] = NULL;
            if (mode == 0)
            {
                checker_arguments[checker_count++] = (char*)"--scalar";
            }
            checker_arguments[checker_count] = NULL;
            char* expected = NULL;
            char* actual = NULL;
            const int oracle = check_test_driver_spawn(oracle_arguments, &expected);
            const int result = check_test_driver_spawn(checker_arguments, &actual);
            char filtered_expected[16384];
            char filtered_actual[16384];
            check_test_driver_common_output(expected != NULL ? expected : "", filtered_expected, sizeof(filtered_expected));
            check_test_driver_common_output(actual != NULL ? actual : "", filtered_actual, sizeof(filtered_actual));
            const char* path = mode == 0 ? "scalar" : "vector";
            char description[160];
            snprintf(description, sizeof(description), "common warning case %u %s diagnostics agree with Clang", c, path);
            const bool same = oracle >= 0 && result == oracle && strcmp(filtered_expected, filtered_actual) == 0;
            check_test_expect(test, same, description);
            if (!same)
            {
                fprintf(stderr, "common warning case %u %s:\nClang:\n%s\nCheck:\n%s\n", c, path, filtered_expected, filtered_actual);
            }
            free(actual);
            free(expected);
        }
    }
    unlink(source_path);
    // Clang leaves a header's file-scope variables unused: every unit that includes the header shares them, so their
    // warnings belong to no one unit. The compiler's own main.c includes such files, and -Wall -Werror must accept it.
    static const char header[] = "static int header_variable;\nstatic const int header_const = 2;\n";
    static const char header_source[] = "#include \"common-warnings-header.h\"\nint main(void) { return 0; }\n";
    char header_path[512];
    snprintf(header_path, sizeof(header_path), "%s/common-warnings-header.h", directory);
    const bool written = parse_test_write(header_path, header, strlen(header)) &&
                         parse_test_write(source_path, header_source, strlen(header_source));
    check_test_expect(test, written, "common warning header fixture is written");
    for (uint32_t mode = 0; written && mode < 2; mode += 1)
    {
        char* oracle_arguments[10] = { (char*)clang, "-std=gnu17", "-fsyntax-only", "-fno-caret-diagnostics", "-fno-color-diagnostics",
                                       "-Wall", "-Wextra", "-Werror", source_path, NULL };
        char* checker_arguments[10] = { (char*)executable, "-std=gnu17", "-fsyntax-only", "-Wall", "-Wextra", "-Werror", source_path,
                                        mode == 0 ? (char*)"--scalar" : NULL, NULL };
        char* expected = NULL;
        char* actual = NULL;
        const int oracle = check_test_driver_spawn(oracle_arguments, &expected);
        const int result = check_test_driver_spawn(checker_arguments, &actual);
        char filtered_expected[1024];
        char filtered_actual[1024];
        check_test_driver_common_output(expected != NULL ? expected : "", filtered_expected, sizeof(filtered_expected));
        check_test_driver_common_output(actual != NULL ? actual : "", filtered_actual, sizeof(filtered_actual));
        const bool same = oracle >= 0 && result == oracle && strcmp(filtered_expected, filtered_actual) == 0;
        check_test_expect(test, same, mode == 0 ? "header file-scope variables stay unused in the scalar path"
                                                : "header file-scope variables stay unused in the vector path");
        free(actual);
        free(expected);
    }
    unlink(header_path);
    unlink(source_path);
}
