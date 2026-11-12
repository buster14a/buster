// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #148's generic builtins' own errors by the driver against clang, on original fixtures: __builtin_verbose_trap's
// arguments, each a constant string without a `$` (check_input_verbose_trap_arguments), a literal's under parentheses
// and casts, __func__'s, a variable's, a null pointer's and an integer's, and const objects' chars through casts that
// keep or lose clang's designator (through `void *`, to or from another type) and literals' whatever the casts (#1843); and a FILE builtin's call in a unit without a
// file-scope FILE (check_input_file_type), clang's "use of unknown builtin"; and memcpy_inline's and memset_inline's size
// that is no integer constant expression, clang's error at the call; and __builtin_complex's arguments of no real floating
// type or of two types, clang's errors at the argument (check_semantic_expression); and __builtin_clear_padding's argument
// of no pointer or to a pointee it refuses (#2414). The errors are compared, their places and
// messages, in order, with the exit status, by both paths of -fsyntax-only (each function's first error alone, as the
// checker reports them). Warnings aren't compared. Include after c_check_label_attribute_driver_test.c.

static void check_test_driver_generic_builtins(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const struct
    {
        const char* name;
        const char* source;
    } fixtures[] = {
        { "verbose_trap's literals", "void g(int x) {\nif (x) __builtin_verbose_trap(\"c\", \"m\" \"n\");\n__builtin_verbose_trap(((\"c\")), (const char*)\"m\");\n}\n" },
        { "verbose_trap of __func__", "void g(void) {\n__builtin_verbose_trap(__func__, __FUNCTION__);\n}\n" },
        { "verbose_trap of a dollar", "void g(void) {\n__builtin_verbose_trap(\"c\", \"a$b\");\n}\n" },
        { "verbose_trap of a dollar under a cast", "void g(void) {\n__builtin_verbose_trap((const char*)(\"$\"), \"m\");\n}\n" },
        { "verbose_trap of __func__ with a dollar", "void g$h(void) {\n__builtin_verbose_trap(\"c\", __func__);\n}\n" },
        { "verbose_trap of a variable", "void g(const char* p) {\n__builtin_verbose_trap(p, \"m\");\n}\n" },
        { "verbose_trap of a variable under a cast", "void g(char* p) {\n__builtin_verbose_trap(\"c\", (const char*)p);\n}\n" },
        { "verbose_trap of a null pointer", "void g(void) {\n__builtin_verbose_trap((const char*)0, \"m\");\n}\n" },
        { "verbose_trap of a call", "const char* f(void);\nvoid g(void) {\n__builtin_verbose_trap(\"c\", f());\n}\n" },
        // Another address constant than a literal's start (#1843): a const array's, its initializer's chars read to the
        // first NUL, as clang's constant evaluator reads them: a `$` before it, a read past the array's end or an array of
        // no NUL is clang's error; a literal past its start reads alike, and so do a const record's member array (up to its
        // own end) and a const pointer's pointee.
        { "verbose_trap of const arrays", "static const char m[] = \"abc\";\nconst char n[] = \"a\\0$\";\n"
                                          "static const char o[4] = { 'x', 'y', 0, 0 };\n"
                                          "void g(void) {\n__builtin_verbose_trap(m, n);\n__builtin_verbose_trap(&m[1], o);\n__builtin_verbose_trap(m + 2, m + 3);\n}\n" },
        { "verbose_trap of a member array and a const pointer", "struct S { int n; char a[4]; char b[3]; };\nstatic const struct S s = { 1, \"ab\", \"c$\" };\n"
                                                                "static const char m[] = \"xyz\";\nstatic const char* const p = m + 1;\n"
                                                                "void g(void) {\n__builtin_verbose_trap(s.a, &s.a[1]);\n__builtin_verbose_trap(p, s.a + 2);\n}\n" },
        { "verbose_trap of a member array's dollar", "struct S { int n; char a[4]; char b[3]; };\nstatic const struct S s = { 1, \"ab\", \"c$\" };\n"
                                                     "void g(void) {\n__builtin_verbose_trap(s.a, s.b);\n}\n" },
        { "verbose_trap of a const pointer's dollar", "static const char m[] = \"x$z\";\nstatic const char* const p = m;\n"
                                                      "void g(void) {\n__builtin_verbose_trap(\"c\", p);\n}\n" },
        { "verbose_trap of a literal past its start", "void g(void) {\n__builtin_verbose_trap(\"ab$\" + 3, &\"c$d\"[2]);\n}\n" },
        { "verbose_trap of a literal's dollar past its start", "void g(void) {\n__builtin_verbose_trap(\"c\", \"ab$c\" + 1);\n}\n" },
        { "verbose_trap of a const array's dollar", "static const char m[] = \"a$c\";\nvoid g(void) {\n__builtin_verbose_trap(\"c\", m);\n}\n" },
        { "verbose_trap past a const array's end", "static const char m[] = \"abc\";\nvoid g(void) {\n__builtin_verbose_trap(m + 4, \"m\");\n}\n" },
        { "verbose_trap of a const array of no NUL", "static const char m[3] = \"abc\";\nvoid g(void) {\n__builtin_verbose_trap(\"c\", m);\n}\n" },
        { "verbose_trap through casts and other pointers",
          "struct R { char m[8]; int x; };\n"
          "static const struct R r = { \"member\", 1 };\n"
          "static const char arr[] = \"array\";\n"
          "static const unsigned char uarr[] = \"uarr\";\n"
          "static const char* const ptr = \"pointer\";\n"
          "static const char* const ptr2 = arr;\n"
          "static const char* const ptr3 = \"lit\";\n"
          "void f1(void) { __builtin_verbose_trap(\"cat\", r.m); }\n"
          "void f2(void) { __builtin_verbose_trap(\"cat\", ptr); }\n"
          "void f3(void) { __builtin_verbose_trap(\"cat\", (const char*)(const void*)arr); }\n"
          "void f4(void) { __builtin_verbose_trap(\"cat\", (const char*)(void*)\"lit\"); }\n"
          "void f5(void) { __builtin_verbose_trap(\"cat\", ptr2); }\n"
          "void f6(void) { __builtin_verbose_trap(\"cat\", (const char*)arr); }\n"
          "void f7(void) { __builtin_verbose_trap(\"cat\", &r.m[1]); }\n"
          "void f8(void) { __builtin_verbose_trap((const char*)ptr, \"x\"); }\n"
          "void g1(void) { __builtin_verbose_trap(\"cat\", (const char*)(char*)arr); }\n"
          "void g2(void) { __builtin_verbose_trap(\"cat\", (const char*)uarr); }\n"
          "void g3(void) { __builtin_verbose_trap(\"cat\", (char*)ptr); }\n"
          "void g4(void) { __builtin_verbose_trap(\"cat\", (const char*)(const void*)ptr); }\n"
          "void g5(void) { __builtin_verbose_trap(\"cat\", (const char*)(void*)r.m); }\n"
          "void g6(void) { __builtin_verbose_trap(\"cat\", (const char*)(const signed char*)arr); }\n"
          "void g7(void) { __builtin_verbose_trap(\"cat\", (const char*)(long)arr); }\n"
          "void g8(void) { __builtin_verbose_trap(\"cat\", (const char*)(const void*)(void*)\"lit\"); }\n"
          "void g9(void) { __builtin_verbose_trap(\"cat\", (const char*)(const int*)arr); }\n"
          "void g10(void) { __builtin_verbose_trap(\"cat\", (const char*)(const char*)r.m); }\n"
          "void h1(void) { __builtin_verbose_trap(\"cat\", (const char*)(const void*)ptr2); }\n"
          "void h2(void) { __builtin_verbose_trap(\"cat\", (const char*)(const void*)&arr[0]); }\n"
          "void h3(void) { __builtin_verbose_trap(\"cat\", (const char*)(const void*)(arr + 1)); }\n"
          "void h5(void) { __builtin_verbose_trap(\"cat\", (const char*)(const void*)(ptr3 + 1)); }\n"
          "void h6(void) { __builtin_verbose_trap(\"cat\", (const char*)(const void*)(\"lit\" + 1)); }\n"
          "void h7(void) { __builtin_verbose_trap(\"cat\", (const char*)(const void*)&\"lit\"[0]); }\n"
          "void h10(void) { __builtin_verbose_trap(\"cat\", (const char*)(const unsigned char*)\"lit\"); }\n"
          "void h11(void) { __builtin_verbose_trap(\"cat\", (const char*)(const unsigned char*)ptr3); }\n"
          "void k3(void) { __builtin_verbose_trap(\"cat\", uarr); }\n"
          "void k4(void) { __builtin_verbose_trap(\"cat\", (const char*)&r); }\n"
          "void k5(void) { __builtin_verbose_trap(\"cat\", (const char*)(const void*)__func__); }\n"
          "void k6(void) { __builtin_verbose_trap(\"cat\", (const char*)&arr); }\n"
          "void k7(void) { __builtin_verbose_trap(\"cat\", (const char*)(long)\"lit\"); }\n"
          "void k8(void) { __builtin_verbose_trap(\"cat\", (const char*)(const void*)\"lit\" + 1); }\n"
          "void k9(void) { __builtin_verbose_trap(\"cat\", (const char*)(const void*)arr + 1); }\n"
          "void k10(void) { __builtin_verbose_trap(\"cat\", arr + (int)1); }\n"
          "void k11(void) { __builtin_verbose_trap(\"cat\", (const char*)(char*)__func__); }\n"
          "void k12(void) { __builtin_verbose_trap(\"cat\", (const char*)L\"ab\"); }\n"
          "void k13(void) { __builtin_verbose_trap(\"cat\", \"lit\" + 3); }\n"
          "void k14(void) { __builtin_verbose_trap(\"cat\", (const char*)(const void*)\"a$b\"); }\n"
          "void k15(void) { __builtin_verbose_trap(\"cat\", (const char*)u8\"ab\"); }\n"
          "void k16(void) { __builtin_verbose_trap(\"cat\", (const char*)(const void*)\"lit\" + 3); }\n"
          "void k17(void) { __builtin_verbose_trap(\"cat\", (const char*)(const void*)\"lit\" + 4); }\n"
          "void n1(void) { __builtin_verbose_trap(\"cat\", (const char*)(const void*)ptr3 + 1); }\n"
          "void n2(void) { __builtin_verbose_trap(\"cat\", (const char*)(char*)arr + 1); }\n"
          "void n3(void) { __builtin_verbose_trap(\"cat\", (char*)arr + 1); }\n" },
        { "verbose_trap in two functions", "void g(const char* p) {\n__builtin_verbose_trap(p, \"m\");\n}\nvoid h(void) {\n__builtin_verbose_trap(\"$\", \"m\");\n}\n" },
        { "fprintf without FILE", "int g(void* p) {\nreturn 1 + __builtin_fprintf(p, \"x\");\n}\n" },
        { "vfscanf with a FILE of block scope", "void h(void) {\ntypedef int FILE;\n}\nint g(void* p, __builtin_va_list a) {\nreturn __builtin_vfscanf(p, \"x\", a);\n}\n" },
        { "memcpy_inline of a size that isn't constant", "void g(void* d, const void* s, unsigned long n) {\n__builtin_memcpy_inline(d, s, 4);\n"
                                                          "  __builtin_memcpy_inline(d, s, n);\n}\n" },
        { "memset_inline of a size that isn't constant", "void g(void* d, unsigned long n) {\n__builtin_memset_inline(d, 0, sizeof(int));\n"
                                                         "  __builtin_memset_inline(d, 0, n + 1);\n}\n" },
        { "complex of no real floating type",
          "typedef int I;\nI x;\nconst int ci;\nint a[3];\nstruct S { double m; } s;\n_Atomic double ad;\n_Complex double cd;\nenum E { A } e;\n"
          "void g1(double d) { (void)__builtin_complex(x, d); }\nvoid g2(double d) { (void)__builtin_complex(d, ci); }\n"
          "void g3(double d) { (void)__builtin_complex(a, d); }\nvoid g4(double d) { (void)__builtin_complex(s, d); }\n"
          "void g5(double d) { (void)__builtin_complex(d, g1); }\nvoid g6(double d) { (void)__builtin_complex(ad, d); }\n"
          "void g7(double d) { (void)__builtin_complex(d, cd); }\nvoid g8(double d) { (void)__builtin_complex(e, d); }\n"
          "void g9(double d) { (void)__builtin_complex((void*)0, d); }\nvoid g10(void) { (void)__builtin_complex(1, 2); }\n"
          "void g11(double d) { (void)__builtin_complex(d, (_Bool)1); }\nvoid g12(double d) { (void)__builtin_complex(d, (__int128)x + 1); }\n"
          "void g13(I* p) { (void)__builtin_complex(*p, 1.0); }\nvoid g14(double d) { (void)__builtin_complex(d, (I)1); }\n"
          "void g15(double d) { (void)__builtin_complex(\"s\", d); }\nvoid g16(double d) { (void)__builtin_complex(d, (struct T *)0); }\n"
          "void g17(double d) { (void)__builtin_complex(sizeof(int), d); }\nvoid g18(double d) { (void)__builtin_complex(d, _Alignof(int)); }\n" },
        { "complex of different types",
          "typedef double D;\ntypedef float F;\nD dd;\nF ff;\nconst double cd;\nvolatile float vf;\nconst D cdd;\n"
          "void g1(void) { (void)__builtin_complex(dd, ff); }\nvoid g2(void) { (void)__builtin_complex(cd, vf); }\n"
          "void g3(void) { (void)__builtin_complex((D)1, 2.0f); }\nvoid g4(void) { (void)__builtin_complex(1.0L, (_Float16)1); }\n"
          "void g5(void) { (void)__builtin_complex(1.0q, 1.0); }\nvoid g6(void) { (void)__builtin_complex((__bf16)1, (_Float16)1); }\n"
          "void g7(void) { (void)__builtin_complex(cdd, ff); }\nvoid g9(float f) { (void)__builtin_complex(f, (D)2); }\n"
          "void g8(void) { (void)__builtin_complex(cd, dd); (void)__builtin_complex(1.0f, (F)2); (void)__builtin_complex(cdd, dd); }\n" },
        // __builtin_clear_padding's argument refused (#2414, check_semantic_clear_padding): no pointer (an array as written,
        // a typedef's), a function pointer, an incomplete pointee with its tag's note, and a const one, at the argument's
        // first token.
        { "clear_padding of no pointer",
          "struct S { char c; int i; };\ntypedef struct S A[2];\n"
          "void f(void) { struct S b[2]; __builtin_clear_padding(b); }\nvoid g(void) { A a; __builtin_clear_padding(a); }\n"
          "void h(int x) { __builtin_clear_padding((x)); }\nvoid k(void) { __builtin_clear_padding(1.0); }\n"
          "void m(struct S s) { __builtin_clear_padding(s); }\nvoid e(int x) { __builtin_clear_padding(x + 1); }\n" },
        { "clear_padding of a function pointer, an incomplete or a const pointee",
          "struct X;\nstruct S { char c; int i; };\ntypedef const struct S CS;\n"
          "void a(struct X *p) { __builtin_clear_padding(p); }\nvoid b(void (*fp)(void)) { __builtin_clear_padding(fp); }\n"
          "void c(const struct S *p) { __builtin_clear_padding(p + 1); }\nvoid g(const int *p) { __builtin_clear_padding((p)); }\n"
          "void h(CS *p) { __builtin_clear_padding(p); }\nvoid o(void *p) { __builtin_clear_padding(p); }\n"
          "void i(void) { struct S b[2]; __builtin_clear_padding(&b); __builtin_clear_padding(b + 0); }\n" },
    };
    char source[PATH_MAX];
    snprintf(source, sizeof(source), "%s/generic-builtins.c", directory);
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        const bool made = parse_test_write(source, fixtures[i].source, strlen(fixtures[i].source));
        char* arguments[] = { (char*)clang, "-std=gnu17", "-fsyntax-only", "-fno-color-diagnostics", source, NULL };
        char* reference = NULL;
        const int oracle = made ? check_test_driver_spawn(arguments, &reference) : -1;
        char expected[8192];
        const bool expected_valid = check_test_cleanup_errors(reference, 0, expected, sizeof(expected));
        free(reference);
        char description[512];
        snprintf(description, sizeof(description), "generic builtins, %s: clang runs it", fixtures[i].name);
        check_test_expect(test, made && (oracle == 0 || oracle == 1) && expected_valid && (oracle == 0) == (expected[0] == 0), description);
        for (uint32_t scalar = 0; made && scalar < 2; scalar += 1)
        {
            char* checked[] = { (char*)(executable != NULL ? executable : "/proc/self/exe"), "-std=gnu17", "-fsyntax-only", scalar != 0 ? "--scalar" : source,
                                scalar != 0 ? source : NULL, NULL };
            char* text = NULL;
            const int status = check_test_driver_spawn(checked, &text);
            char actual[8192];
            const bool actual_valid = check_test_cleanup_errors(text, 0, actual, sizeof(actual));
            free(text);
            snprintf(description, sizeof(description), "generic builtins, %s, by the %s path: clang's\n%sagainst\n%s", fixtures[i].name,
                     scalar != 0 ? "scalar" : "vector", expected, actual);
            check_test_expect(test, status == oracle && expected_valid && actual_valid && strcmp(expected, actual) == 0, description);
        }
    }
    unlink(source);
}

// __builtin_available (#148, c_check_availability_input.c), by the driver against clang, by both paths of -fsyntax-only:
// the whole output, line for line, with the exit status. First a unit of valid checks, warned about where they guard
// nothing (not an if's condition, through parentheses and `!`, nor a case label's value), at file scope never, under
// -Wunsupported-availability-guard's and -Wavailability's options; then units of clang's errors of the specs: a version
// that isn't one, a zero one, an unknown platform, an anyAppleOS below 26, a repeated platform or `*`, no `*`.
static void check_test_driver_availability(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char guards[] =
        "int s = sizeof(__builtin_available(*));\n"
        "typeof(__builtin_available(macos 10.12, *)) t;\n"
        "int u = _Generic(__builtin_available(*), _Bool: 1, default: 2);\n"
        "int f(int x) {\n"
        "    int n = 0;\n"
        "    if (__builtin_available(macos 10.12, ios 10, tvos 10_1, watchos 3.1.2, *)) n += 1;\n"
        "    if (!(__builtin_available(macOS 10, iOSApplicationExtension 1, *))) n += 2; else if ((!!__builtin_available(*))) n += 3;\n"
        "    if (__builtin_available(*) && x) n += 4;\n"
        "    while (__builtin_available(visionOS 1, ShaderModel 6, macosx 10, *) && x--) n += 5;\n"
        "    for (; __builtin_available(android 21, ohos 3, fuchsia 1, swift 5, *) && n < 9;) n += 1;\n"
        "    _Bool b = __builtin_available(anyAppleOS 26, driverkit 19, maccatalyst 13.1, *);\n"
        "    n += (int)sizeof(__builtin_available(*)) + _Generic(__builtin_available(*), _Bool: 1, default: 0);\n"
        "    typeof(__builtin_available(*)) c = 1;\n"
        "    int a[__builtin_available(*) + 1];\n"
        "    switch (x) { case sizeof(__builtin_available(*)): n += 1; break; default: break; }\n"
        "    n += ({ __builtin_available(*); }) + (x ? __builtin_available(macos 1_2_3, *) : 0) + b + c + (int)sizeof a;\n"
        "    return n + __builtin_available(macos 1.2_3, ios 1_2.3, *);\n"
        "}\n";
    // A unit each, as the checker stops a unit at its first error: a spec's errors, in order, then the list's, which leave
    // clang's expression, and so its warning.
    static const char* const errors[] = {
        "int f(void) { return __builtin_available(macos true, *); }\n",
        "int f(void) { return __builtin_available(macos 0x10, ios 1.2.3.4, *); }\n",
        "int f(void) { return __builtin_available(macos 1e5, tvos 10.); }\n",
        "int f(void) { return __builtin_available(macos 10.2_3x, *); }\n",
        "int f(void) { return __builtin_available(macos 0, *); }\n",
        "int f(void) { return __builtin_available(ios 0.0); }\n",
        "int f(void) { return __builtin_available(foo 1, xros 1, *); }\n",
        "int f(void) { return __builtin_available(xros_app_extension 2, *); }\n",
        "int f(void) { return __builtin_available(anyAppleOS 25.3, anyAppleOS 25_9_9, *); }\n",
        "int f(void) { return __builtin_available(foo 0x1, bar 1, *); }\n",
        "int f(void) { return __builtin_available(macos 10, macOS 11, *, *); }\n",
        "int f(void) { return __builtin_available(visionOS 1, visionos 2); }\n",
        "int f(void) { return __builtin_available(macos 10); }\n",
        "int f(void) { if (__builtin_available(tvos 4294967296, *)) return 1; return 0; }\n",
        "int f(void) { if (__builtin_available(ios 1, tvos 2_3)) return 1; return 0; }\n",
    };
    static const char* const options[] = { NULL, "-Wno-unsupported-availability-guard", "-Werror=unsupported-availability-guard", "-Wno-availability",
                                            "-Werror=availability", "-w" };
    enum { LINES = 16384 };
    char* expected_lines = malloc(LINES);
    char* actual_lines = malloc(LINES);
    char source[PATH_MAX];
    snprintf(source, sizeof(source), "%s/availability.c", directory);
    const uint32_t units = 1 + BUSTER_ARRAY_LENGTH(errors);
    for (uint32_t unit = 0; expected_lines != NULL && actual_lines != NULL && unit < units; unit += 1)
    {
        const char* text = unit == 0 ? guards : errors[unit - 1];
        const bool made = parse_test_write(source, text, strlen(text));
        for (uint32_t o = 0; made && o < (unit == 0 ? BUSTER_ARRAY_LENGTH(options) : 1); o += 1)
        {
            char* arguments[] = { (char*)clang, "-std=gnu23", "-fsyntax-only", "-fno-caret-diagnostics", source, (char*)options[o], NULL, NULL };
            char* expected = NULL;
            const int oracle = check_test_driver_spawn(arguments, &expected);
            const bool lines = expected != NULL && check_test_diagnostic_lines(expected, expected_lines, LINES);
            char description[4096];
            snprintf(description, sizeof(description), "availability %u %s: clang's lines read", unit, options[o] != NULL ? options[o] : "(no option)");
            check_test_expect(test, lines && (expected_lines[0] == 0) == (unit == 0 && o == 5), description);
            for (uint32_t scalar = 0; lines && scalar < 2; scalar += 1)
            {
                const uint32_t last = options[o] != NULL ? 6 : 5;
                arguments[0] = (char*)executable;
                arguments[last] = scalar != 0 ? "--scalar" : NULL;
                char* actual = NULL;
                const int status = check_test_driver_spawn(arguments, &actual);
                const bool same = actual != NULL && check_test_diagnostic_lines(actual, actual_lines, LINES) && strcmp(expected_lines, actual_lines) == 0;
                snprintf(description, sizeof(description), "availability %u %s, %s: status %d (clang %d), lines\n%s(clang\n%s)", unit,
                         options[o] != NULL ? options[o] : "(no option)", scalar != 0 ? "scalar" : "vector", status, oracle, actual_lines, expected_lines);
                check_test_expect(test, same && (status != 0) == (oracle != 0), description);
                free(actual);
            }
            free(expected);
        }
        unlink(source);
    }
    free(expected_lines);
    free(actual_lines);
}

// clang's counted_by family (#148, c_check_counted_by_input.c) and __builtin_counted_by_ref, by the driver against clang:
// the whole output, line for line, with the exit status, by both paths of -fsyntax-only, a unit each (most of them have
// one error): the attribute's places and argument forms, valid and not, with clang's errors of its member,
// its pointee and its count, a count member whose name a typedef has too (its declarator parenthesized, after a tag's
// name; a function declarator's parameter and a nested named record's member aren't the record's: #1842), the
// struct-with-flexible-member element warning, the attribute off a member, and a type name read as its argument's expression
// (the parser's error and clang's second, #1842); the builtin's types (with _Generic), its
// arguments' errors, and the uses of its value clang refuses (assigned, passed, returned, subscripted, in a binary
// expression) beside those it takes.
static void check_test_driver_counted_by(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char* const units[] = {
        "struct S { int n; int fam[] __attribute__((counted_by(n))); };\n",
        "struct S { int n; int *p __attribute__((counted_by(n))); };\n",
        "struct S { long n; char *p __attribute__((sized_by(n))); };\n",
        "struct S { int n; int *p __attribute__((counted_by_or_null(n))); };\n",
        "struct S { int n; void *p __attribute__((sized_by_or_null(n))); };\n",
        "struct B { int *p __attribute__((counted_by(n))); int n; };\n",
        "struct C { int n; struct { int x; int fam[] __attribute__((counted_by(n))); }; };\n",
        "struct D { int n; int m[2] __attribute__((counted_by(n))); };\n",
        "struct E { long n; char fam[] __attribute__((sized_by(n))); };\n",
        "struct F { _Bool n; int fam[] __attribute__((counted_by(n))); };\n",
        "int g;\nstruct G { int n; int fam[] __attribute__((counted_by(g))); };\n",
        "struct H { int n; int fam[] __attribute__((counted_by(n + 1))); };\n",
        "struct I { int n; void *p __attribute__((counted_by(n))); };\n",
        "struct J { int n; int x __attribute__((counted_by(n))); };\n",
        "union K { int n; int *p __attribute__((counted_by(n))); };\n",
        "struct L { int n; int (*fp)(void) __attribute__((counted_by(n))); };\n",
        "struct A1 { int n; int fam[] __attribute__((counted_by(n, 1))); };\n",
        "struct A2 { int n; int fam[] __attribute__((counted_by())); };\n",
        "struct A3 { int n; int fam[] __attribute__((counted_by)); };\n",
        "struct A4 { int n; int * __attribute__((counted_by(n))) p; };\n",
        "struct A5 { int n; int *p __attribute__((counted_by(n))), *q __attribute__((counted_by(n))); };\n",
        "struct A6 { int n; int __attribute__((counted_by(n))) *p; };\n",
        "struct A7 { int n; [[clang::counted_by(n)]] int *p; };\n",
        "struct A8 { int n; int *p [[clang::counted_by(n)]]; };\n",
        "int *g __attribute__((counted_by(x)));\n",
        "int x; int *g __attribute__((counted_by(x)));\n",
        "void f(int n, int *p __attribute__((counted_by(n))));\n",
        "struct A9 { int n; int fam[] __attribute__((counted_by(n))) __attribute__((counted_by(n))); };\n",
        "struct A10 { int n; int m; int fam[] __attribute__((counted_by(n))) __attribute__((sized_by(m))); };\n",
        "struct A11 { int n; int fam[] __attribute__((counted_by((n)))); };\n",
        "struct A12 { char n; unsigned long m; struct { int fam[] __attribute__((counted_by(m))); }; };\n",
        "struct A13 { int n; struct { int x; } s; int fam[] __attribute__((counted_by(s))); };\n",
        "struct A14 { int n : 3; int fam[] __attribute__((counted_by(n))); };\n",
        "enum { EN = 3 };\nstruct A15 { int n; int fam[] __attribute__((counted_by(EN))); };\n",
        "struct A16 { int n; int fam[] __attribute__((counted_by(fam))); };\n",
        "int n;\nstruct B1 { char n; int a[sizeof(n)]; int fam[] __attribute__((counted_by(n))); };\n",
        "int n;\nstruct B2 { int z; int fam[] __attribute__((counted_by(n))); };\n",
        "struct B4 { int x; int * __attribute__((counted_by(x))) *pp; };\n",
        "struct B5 { int x; int *__attribute__((counted_by(x))) arr[3]; };\n",
        "struct B6 { int x; struct B6 *__attribute__((counted_by(x))) self; };\n",
        "struct Inc;\nstruct B7 { int x; struct Inc *__attribute__((counted_by(x))) p; };\n",
        "struct B9 { int x; int (*__attribute__((counted_by(x))) pa)[]; };\n",
        "struct B1 { int n; int fam[]; };\nstruct B10 { int x; struct B1 *__attribute__((counted_by(x))) p; };\n",
        "struct B1 { int n; int fam[]; };\nstruct B11 { int x; struct B1 fam[] __attribute__((counted_by(x))); };\n",
        "struct B12 { union { int c; }; int fam[] __attribute__((counted_by(c))); };\n",
        "struct B13 { struct { int c; }; int fam[] __attribute__((counted_by(c))); };\n",
        "struct B14 { int n; struct In { int fam[] __attribute__((counted_by(n))); } in; };\n",
        "struct B15 { int n; int fam[] __attribute__((counted_by(1))); };\n",
        "struct B16 { int n; int fam[] __attribute__((counted_by(1.5))); };\n",
        "int f(void);\nstruct B17 { int n; int fam[] __attribute__((counted_by(f))); };\n",
        "struct B18 { struct { int n; } s; int fam[] __attribute__((counted_by(n))); };\n",
        "union U { int n; struct { int m; int fam[] __attribute__((counted_by(m))); } s; };\n",
        "struct B19 { int n; int *p __attribute__((sized_by(n))); int fam[] __attribute__((sized_by_or_null(n))); };\n",
        "typedef int n;\nstruct B20 { int n; int fam[] __attribute__((counted_by(n))); };\nint f(struct B20 *s) { return __builtin_dynamic_object_size(s->fam, 0); }\n",
        "typedef long m;\nstruct B21 { int k; unsigned long m : 4, *x; int *p __attribute__((sized_by(m))); };\n",
        "typedef long m;\nstruct B23 { m m; struct { int fam[] __attribute__((counted_by(m))); }; };\n",
        "typedef int n;\nstruct B26 { int n; struct X { int k; } x; int (*fp)(int k); int fam[] __attribute__((counted_by(n))); };\n",
        "typedef int n;\nstruct B27 { int (n); int fam[] __attribute__((counted_by(n))); };\nint f(struct B27 *s) { return __builtin_dynamic_object_size(s->fam, 0); }\n",
        "typedef int n;\nstruct B28 { int ((n)); int fam[] __attribute__((counted_by(n))); };\nint f(struct B28 *s) { return __builtin_dynamic_object_size(s->fam, 0); }\n",
        // A type name read as the count's expression (#1842): the parser's error, then clang's second, at the argument's first
        // token, whatever the depth, for each attribute of the family; past a top-level `,`, clang's "expected ')'" alone.
        "typedef int T;\nint f(int);\nstruct C1 { int n; int *p __attribute__((counted_by(T))); };\nstruct C2 { int n; int fam[] __attribute__((counted_by(n + T))); };\n"
        "struct C3 { int n; void *p __attribute__((sized_by(f(T)))); };\nstruct C4 { int n; int *p __attribute__((counted_by_or_null(-T))); };\n"
        "struct C5 { int n; void *p __attribute__((sized_by_or_null(n ? T : 1))); };\nstruct C6 { int n; int *p __attribute__((__counted_by__(n[T]))); };\n",
        "typedef int T;\nstruct C7 { int n; int *p __attribute__((counted_by(n, T))); };\n",
        "typedef int T;\nint f(int);\nstruct C8 { int n; int *p __attribute__((counted_by(n, f(T)))); };\n",
        "typedef int T;\nstruct C9 { int n; int *p __attribute__((counted_by((T)))); };\nstruct C10 { int n; int *p __attribute__((counted_by(sizeof T))); };\n"
        "struct C11 { int n; int *p __attribute__((aligned(T))); };\n",
        "typedef int n;\nstruct X { int a; };\nstruct B29 { struct X n; int *p __attribute__((counted_by(n))); };\n",
        "typedef int n;\nenum E { A };\nstruct B30 { enum E n; int *p __attribute__((counted_by(n))); };\n",
        // An argument of more than a name typed with the members' names (#1842): one of no integer type is clang's integer
        // error, any other its reference error, a name that names nothing its undeclared one; sizeof's, alignof's and typeof's
        // operands see no members. Each error drops its attribute and the unit goes on, as clang's does.
        "typedef double D;\nint g;\ndouble h;\nstruct T { long x; };\nstruct D1 { int n; double d; struct T t; struct T *pt; int *p1 __attribute__((counted_by(n + 1.5)));\n"
        "int *p2 __attribute__((counted_by(d + 1))); int *p3 __attribute__((counted_by((D)n))); int *p4 __attribute__((counted_by(&n)));\n"
        "int *p5 __attribute__((counted_by((_Bool)n))); int *p6 __attribute__((counted_by((void)0))); int *p7 __attribute__((counted_by(h * 2)));\n"
        "int *p8 __attribute__((counted_by(t.x))); int *p9 __attribute__((counted_by(pt->x + 0.5f))); int fam[] __attribute__((counted_by(n / 2.0))); };\n"
        "struct D2 { int n; int *p1 __attribute__((counted_by(n + 1))); int *p2 __attribute__((counted_by((long)n))); int *p3 __attribute__((counted_by(g + n)));\n"
        "int *p4 __attribute__((counted_by(n == 1))); void *p5 __attribute__((sized_by(n * 2.0))); int *p6 __attribute__((counted_by_or_null(-n)));\n"
        "void *p7 __attribute__((sized_by_or_null(__builtin_inf()))); };\nint x = 1.5 + \"\";\n",
        "int n;\nstruct D3 { int n; int *p1 __attribute__((counted_by(sizeof n + 1.0))); int *p2 __attribute__((counted_by(n + sizeof(n))));\n"
        "int *p3 __attribute__((counted_by((typeof(n))1))); int *p4 __attribute__((counted_by(n + nope))); int *p5 __attribute__((counted_by(m + n))); int m; };\n",
        "struct D4 { int n; int *p __attribute__((counted_by(sizeof(n) * 1.0))); };\n",
        // An argument's own diagnostics (#1842): -Wdivision-by-zero at a `/` or `%` by a constant zero, before the
        // attribute's error; a member access on no record, clang's error, then its integer one.
        "struct A { int n; int *p __attribute__((counted_by(n / 0))); };\nstruct B { int n; int *p __attribute__((counted_by(__alignof__(n)))); };\nstruct C { int n; int *p __attribute__((counted_by(n.x))); };\n",
        "int g;\nstruct A { int n; int *p __attribute__((counted_by(n % 0))); };\nstruct B { int n; int *p __attribute__((counted_by(n / (g - g)))); };\nstruct C { int n; int *p __attribute__((counted_by(sizeof(n / 0)))); };\nstruct D { int n; int *p __attribute__((counted_by(n / 0 + n / 0))); };\n",
        "struct T { int a; };\nstruct A { int n; struct T t; int *p __attribute__((counted_by(n.b))); };\n",
        "struct D5 { int n; int x __attribute__((counted_by(n))); };\nunion D6 { int n; int *p __attribute__((counted_by(n))); };\n"
        "struct D7 { int n; int *p __attribute__((counted_by(-n))); int *q __attribute__((counted_by(n))); };\nint f(struct D7 *s) { return s->q != 0; }\nint y = 1.5 + \"\";\n",
        "struct S { int n; int fam[] __attribute__((counted_by(n))); };\nstruct P { int k; char *p __attribute__((sized_by(k))); int *q __attribute__((counted_by(k))); };\nint g(int *);\nint *f(struct S *s) { return __builtin_counted_by_ref(s->fam); }\n",
        "struct S { int n; int fam[] __attribute__((counted_by(n))); };\nstruct P { int k; char *p __attribute__((sized_by(k))); int *q __attribute__((counted_by(k))); };\nint g(int *);\nvoid f(struct S *s) { int *p = __builtin_counted_by_ref(s->fam); (void)p; }\n",
        "struct S { int n; int fam[] __attribute__((counted_by(n))); };\nstruct P { int k; char *p __attribute__((sized_by(k))); int *q __attribute__((counted_by(k))); };\nint g(int *);\nvoid f(struct S *s) { int *p; p = __builtin_counted_by_ref(s->fam); (void)p; }\n",
        "struct S { int n; int fam[] __attribute__((counted_by(n))); };\nstruct P { int k; char *p __attribute__((sized_by(k))); int *q __attribute__((counted_by(k))); };\nint g(int *);\nint f(struct S *s) { return g(__builtin_counted_by_ref(s->fam)); }\n",
        "struct S { int n; int fam[] __attribute__((counted_by(n))); };\nstruct P { int k; char *p __attribute__((sized_by(k))); int *q __attribute__((counted_by(k))); };\nint g(int *);\nint f(struct S *s) { return __builtin_counted_by_ref(s->fam)[0]; }\n",
        "struct S { int n; int fam[] __attribute__((counted_by(n))); };\nstruct P { int k; char *p __attribute__((sized_by(k))); int *q __attribute__((counted_by(k))); };\nint g(int *);\nint f(struct S *s) { return __builtin_counted_by_ref(s->fam) != 0; }\n",
        "struct S { int n; int fam[] __attribute__((counted_by(n))); };\nstruct P { int k; char *p __attribute__((sized_by(k))); int *q __attribute__((counted_by(k))); };\nint g(int *);\nint f(struct S *s) { return (__builtin_counted_by_ref(s->fam), 1); }\n",
        "struct S { int n; int fam[] __attribute__((counted_by(n))); };\nstruct P { int k; char *p __attribute__((sized_by(k))); int *q __attribute__((counted_by(k))); };\nint g(int *);\nint f(struct S *s) { return 0[__builtin_counted_by_ref(s->fam)]; }\n",
        "struct S { int n; int fam[] __attribute__((counted_by(n))); };\nstruct P { int k; char *p __attribute__((sized_by(k))); int *q __attribute__((counted_by(k))); };\nint g(int *);\nvoid f(struct S *s) { *__builtin_counted_by_ref(s->fam) = 3; (void)__builtin_counted_by_ref(s->fam); int *p = (int *)__builtin_counted_by_ref(s->fam); (void)p; }\n",
        "struct S { int n; int fam[] __attribute__((counted_by(n))); };\nstruct P { int k; char *p __attribute__((sized_by(k))); int *q __attribute__((counted_by(k))); };\nint g(int *);\nvoid f(struct S *s) { int *a[1] = { __builtin_counted_by_ref(s->fam) }; (void)a; }\n",
        "struct S { int n; int fam[] __attribute__((counted_by(n))); };\nstruct P { int k; char *p __attribute__((sized_by(k))); int *q __attribute__((counted_by(k))); };\nint g(int *);\nint f(struct S *s, int c) { return *(c ? __builtin_counted_by_ref(s->fam) : 0); }\n",
        "struct S { int n; int fam[] __attribute__((counted_by(n))); };\nstruct P { int k; char *p __attribute__((sized_by(k))); int *q __attribute__((counted_by(k))); };\nint g(int *);\nvoid f(struct S *s) { typeof(__builtin_counted_by_ref(s->fam)) p = 0; (void)p; }\n",
        "struct S { int n; int fam[] __attribute__((counted_by(n))); };\nstruct P { int k; char *p __attribute__((sized_by(k))); int *q __attribute__((counted_by(k))); };\nint g(int *);\nvoid f(struct P *p) { _Static_assert(_Generic(__builtin_counted_by_ref(p->p), void *: 1, default: 0), \"\"); _Static_assert(_Generic(__builtin_counted_by_ref(p->q), int *: 1, default: 0), \"\"); }\n",
        "struct S { int n; int fam[] __attribute__((counted_by(n))); };\nstruct P { int k; char *p __attribute__((sized_by(k))); int *q __attribute__((counted_by(k))); };\nint g(int *);\nvoid f(struct S *s) { (void)__builtin_counted_by_ref(s->n); }\n",
        "struct S { int n; int fam[] __attribute__((counted_by(n))); };\nstruct P { int k; char *p __attribute__((sized_by(k))); int *q __attribute__((counted_by(k))); };\nint g(int *);\nvoid f(struct S *s) { (void)__builtin_counted_by_ref(s); }\n",
        "struct S { int n; int fam[] __attribute__((counted_by(n))); };\nstruct P { int k; char *p __attribute__((sized_by(k))); int *q __attribute__((counted_by(k))); };\nint g(int *);\nvoid f(struct S *s) { (void)__builtin_counted_by_ref(s++->fam); }\n",
        "struct S { int n; int fam[] __attribute__((counted_by(n))); };\nstruct P { int k; char *p __attribute__((sized_by(k))); int *q __attribute__((counted_by(k))); };\nint g(int *);\nvoid f(struct S *s) { (void)__builtin_counted_by_ref((int *)s->fam); }\n",
        "struct S { int n; int fam[] __attribute__((counted_by(n))); };\nstruct P { int k; char *p __attribute__((sized_by(k))); int *q __attribute__((counted_by(k))); };\nint g(int *);\nvoid f(struct S *s) { (void)__builtin_counted_by_ref(((s)->fam)); }\n",
        "struct S { int n; int fam[] __attribute__((counted_by(n))); };\nstruct P { int k; char *p __attribute__((sized_by(k))); int *q __attribute__((counted_by(k))); };\nint g(int *);\nvoid f(int *x) { (void)__builtin_counted_by_ref(x); }\n",
        "struct S { int n; int fam[] __attribute__((counted_by(n))); };\nstruct P { int k; char *p __attribute__((sized_by(k))); int *q __attribute__((counted_by(k))); };\nint g(int *);\nstruct S *h(void);\nvoid f(void) { (void)__builtin_counted_by_ref(h()->fam); }\n",
        "struct S { int n; int fam[] __attribute__((counted_by(n))); };\nstruct P { int k; char *p __attribute__((sized_by(k))); int *q __attribute__((counted_by(k))); };\nint g(int *);\nvoid f(struct S s[2]) { (void)__builtin_counted_by_ref(s[1].fam); }\n",
        "struct S { int n; int fam[] __attribute__((counted_by(n))); };\nint g(int *, int);\nstruct T { int *p; };\nvoid f(struct S *s, int c) {\n    int x = g(0, (__builtin_counted_by_ref(s->fam), 1));\n    struct T t = { .p = __builtin_counted_by_ref(s->fam) };\n    struct T u = { __builtin_counted_by_ref(s->fam) };\n    int *a = c ? __builtin_counted_by_ref(s->fam) : 0;\n    (void)x; (void)t; (void)u; (void)a;\n}\n",
    };
    enum { LINES = 16384 };
    char* expected_lines = malloc(LINES);
    char* actual_lines = malloc(LINES);
    char source[PATH_MAX];
    snprintf(source, sizeof(source), "%s/counted-by.c", directory);
    for (uint32_t unit = 0; expected_lines != NULL && actual_lines != NULL && unit < BUSTER_ARRAY_LENGTH(units); unit += 1)
    {
        const bool made = parse_test_write(source, units[unit], strlen(units[unit]));
        char* arguments[] = { (char*)clang, "-std=gnu23", "-fsyntax-only", "-fno-caret-diagnostics", source, NULL, NULL };
        char* expected = NULL;
        const int oracle = made ? check_test_driver_spawn(arguments, &expected) : -1;
        const bool lines = expected != NULL && check_test_diagnostic_lines(expected, expected_lines, LINES);
        char description[4096];
        snprintf(description, sizeof(description), "counted_by %u: clang's lines read", unit);
        check_test_expect(test, made && lines, description);
        for (uint32_t scalar = 0; lines && scalar < 2; scalar += 1)
        {
            arguments[0] = (char*)executable;
            arguments[5] = scalar != 0 ? "--scalar" : NULL;
            char* actual = NULL;
            const int status = check_test_driver_spawn(arguments, &actual);
            const bool same = actual != NULL && check_test_diagnostic_lines(actual, actual_lines, LINES) && strcmp(expected_lines, actual_lines) == 0;
            snprintf(description, sizeof(description), "counted_by %u, %s: status %d (clang %d), lines\n%s(clang\n%s)", unit, scalar != 0 ? "scalar" : "vector",
                     status, oracle, actual_lines, expected_lines);
            check_test_expect(test, same && (status != 0) == (oracle != 0), description);
            free(actual);
        }
        free(expected);
        unlink(source);
    }
    free(expected_lines);
    free(actual_lines);
}

// Declarations of builtins' names with external linkage (#1843, check_input_builtin_declaration), by the driver against
// clang, by both paths of -fsyntax-only: the whole output, line for line, with the exit status, a unit each. A definition
// of a header builtin's or a catalog builtin's name is clang's "definition of builtin function", C99's inline one too; a
// declaration of another type is clang's "conflicting types" with its note of the builtin's type, and any of a
// custom-checked builtin clang's "cannot redeclare builtin function"; and a static
// definition, a compatible declaration (with no prototype, a const parameter or a typedef's type) and a library
// builtin's definition are the program's. Then a program of __builtin_bswap32 and __builtin_clz declared, at file scope
// and at block scope, which stay the builtins (an integer constant expression's too), and of a static definition of a
// builtin's name, which is the program's, run by clang and by both paths, by Tier 0 and by Tier 1.
static void check_test_driver_builtin_declarations(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char* const units[] = {
        "void _mm_pause(void) {}\n",
        "void _mm_pause(void);\nvoid _mm_pause(void) {}\nint g(void) { _mm_pause(); return 0; }\n",
        "extern void _mm_sfence(void) {}\n",
        "inline void _mm_pause(void) {}\n",
        "unsigned int _mm_getcsr(void) { return 0; }\n",
        "unsigned __builtin_bswap32(unsigned x) { return x; }\n",
        "int __builtin_clz(unsigned x) { return 0; }\n",
        "void __builtin_ia32_pause(void) {}\n",
        "int __builtin_bswap32(int);\n",
        "unsigned short __builtin_bswap32(unsigned);\n",
        "void _mm_lfence(int);\n",
        "int g(void) { long __builtin_clz(unsigned); return 0; }\n",
        "static void _mm_pause(void) {}\nstatic unsigned __builtin_bswap32(unsigned x) { return x + 1; }\nint g(void) { _mm_pause(); return (int)__builtin_bswap32(1); }\n",
        "unsigned __builtin_bswap32();\ntypedef unsigned U;\nU __builtin_bswap32(const U);\nint __builtin_clz(unsigned) __attribute__((const));\n"
        "void _mm_pause(void);\nint abs(int x) { return x < 0 ? -x : x; }\n",
        "int g(void) { int __builtin_clz = 3; return __builtin_clz; }\n",
        // Custom-checked builtins (#1918's review): any declaration is clang's "cannot redeclare builtin function".
        "int __builtin_add_overflow(int);\n",
        "_Bool __builtin_add_overflow();\n",
        "int __builtin_add_overflow(int x) { return x; }\n",
        "int __builtin_constant_p(int);\n",
        "void* __builtin_launder(void*);\n",
        "int __builtin_isnan(double);\n",
        "int __sync_fetch_and_add(int*, int);\n",
        "int g(void) { int __builtin_classify_type(); return 0; }\n",
        "unsigned long __builtin_object_size(const void*, int);\nint __builtin_expect(long, long);\n",
    };
    enum { LINES = 8192 };
    char* expected_lines = malloc(LINES);
    char* actual_lines = malloc(LINES);
    char source[PATH_MAX];
    snprintf(source, sizeof(source), "%s/builtin-declarations.c", directory);
    for (uint32_t unit = 0; expected_lines != NULL && actual_lines != NULL && unit < BUSTER_ARRAY_LENGTH(units); unit += 1)
    {
        const bool made = parse_test_write(source, units[unit], strlen(units[unit]));
        char* arguments[] = { (char*)clang, "-std=gnu17", "-fsyntax-only", "-fno-caret-diagnostics", source, NULL, NULL };
        char* expected = NULL;
        const int oracle = made ? check_test_driver_spawn(arguments, &expected) : -1;
        const bool lines = expected != NULL && check_test_diagnostic_lines(expected, expected_lines, LINES);
        char description[4096];
        snprintf(description, sizeof(description), "builtin declarations %u: clang's lines read", unit);
        check_test_expect(test, made && lines, description);
        for (uint32_t scalar = 0; lines && scalar < 2; scalar += 1)
        {
            arguments[0] = (char*)executable;
            arguments[5] = scalar != 0 ? "--scalar" : NULL;
            char* actual = NULL;
            const int status = check_test_driver_spawn(arguments, &actual);
            const bool same = actual != NULL && check_test_diagnostic_lines(actual, actual_lines, LINES) && strcmp(expected_lines, actual_lines) == 0;
            snprintf(description, sizeof(description), "builtin declarations %u, %s: status %d (clang %d), lines\n%s(clang\n%s)", unit,
                     scalar != 0 ? "scalar" : "vector", status, oracle, actual_lines, expected_lines);
            check_test_expect(test, same && (status != 0) == (oracle != 0), description);
            free(actual);
        }
        free(expected);
        unlink(source);
    }
    free(expected_lines);
    free(actual_lines);
    static const char program[] =
        "int printf(const char*, ...);\n"
        "unsigned __builtin_bswap32(unsigned);\n"
        "int __builtin_clz(unsigned);\n"
        "static unsigned long long __builtin_bswap64(unsigned long long x) { return x + 1; }\n"
        "enum { E = __builtin_clz(1u) + __builtin_bswap32(0x01000000u) };\n"
        "static unsigned f(unsigned x) { return __builtin_bswap32(x) ^ (unsigned)__builtin_clz(x | 1u); }\n"
        "static int g(unsigned x) { int __builtin_ctz(unsigned); unsigned __builtin_bswap32(unsigned); return __builtin_ctz(x | 0x80000000u) + (int)(__builtin_bswap32(x) & 0xff); }\n"
        "int main(void) {\n"
        "    volatile unsigned v = 0x12345678u;\n"
        "    printf(\"%u %u %d %llu %d\\n\", f(v), f(v >> 9), g(v), __builtin_bswap64(v), (int)E);\n"
        "    return 0;\n"
        "}\n";
    check_test_library_run(test, clang, executable, directory, "builtins under their declarations", program, "-std=gnu17", NULL);
    check_test_library_own_option = "--tier1";
    check_test_library_run(test, clang, executable, directory, "builtins under their declarations by Tier 1", program, "-std=gnu17", NULL);
    check_test_library_own_option = NULL;
}

// __builtin_cpu_supports of a feature clang doesn't know (#1843, check_input_cpu_supports_literal), by the driver against
// clang, by both paths of -fsyntax-only: the whole output, line for line, with the exit status, under no option, -w and
// -Werror. clang's warning of no option is at the callee's first token (a parenthesized name's outermost `(`), in a body,
// under parentheses, in sizeof's operand and in
// a condition; a feature it knows, a concatenated one, gives none, and an empty string and another case's spelling do.
static void check_test_driver_cpu_supports(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char unit[] =
        "int s = sizeof(__builtin_cpu_supports(\"bar\"));\n"
        "int f(void) {\n"
        "    return __builtin_cpu_supports(\"foo\") + __builtin_cpu_supports(((\"x86-64-v9\"))) + __builtin_cpu_supports(\"avx2\") +\n"
        "           __builtin_cpu_supports(\"avx\" \"512f\") + __builtin_cpu_supports(\"\") + __builtin_cpu_supports(\"AVX2\");\n"
        "}\n"
        "int g(void) { if (__builtin_cpu_supports(\"avx2,sse\")) return 1; return __builtin_cpu_supports(\"x86-64-v3\"); }\n"
        "int h(void) { return (__builtin_cpu_supports)(\"unknown\") + ((__builtin_cpu_supports))((\"x\")) + (__builtin_cpu_supports)(\"avx2\"); }\n";
    static const char* const options[] = { NULL, "-w", "-Werror" };
    enum { LINES = 8192 };
    char* expected_lines = malloc(LINES);
    char* actual_lines = malloc(LINES);
    char source[PATH_MAX];
    snprintf(source, sizeof(source), "%s/cpu-supports.c", directory);
    const bool made = expected_lines != NULL && actual_lines != NULL && parse_test_write(source, unit, strlen(unit));
    for (uint32_t o = 0; made && o < BUSTER_ARRAY_LENGTH(options); o += 1)
    {
        char* arguments[] = { (char*)clang, "-std=gnu17", "-fsyntax-only", "-fno-caret-diagnostics", source, (char*)options[o], NULL, NULL };
        char* expected = NULL;
        const int oracle = check_test_driver_spawn(arguments, &expected);
        const bool lines = expected != NULL && check_test_diagnostic_lines(expected, expected_lines, LINES);
        char description[4096];
        snprintf(description, sizeof(description), "cpu_supports %s: clang's lines read", options[o] != NULL ? options[o] : "(no option)");
        check_test_expect(test, lines && (expected_lines[0] == 0) == (o == 1), description);
        for (uint32_t scalar = 0; lines && scalar < 2; scalar += 1)
        {
            const uint32_t last = options[o] != NULL ? 6 : 5;
            arguments[0] = (char*)executable;
            arguments[last] = scalar != 0 ? "--scalar" : NULL;
            char* actual = NULL;
            const int status = check_test_driver_spawn(arguments, &actual);
            const bool same = actual != NULL && check_test_diagnostic_lines(actual, actual_lines, LINES) && strcmp(expected_lines, actual_lines) == 0;
            snprintf(description, sizeof(description), "cpu_supports %s, %s: status %d (clang %d), lines\n%s(clang\n%s)", options[o] != NULL ? options[o] : "(no option)",
                     scalar != 0 ? "scalar" : "vector", status, oracle, actual_lines, expected_lines);
            check_test_expect(test, same && (status != 0) == (oracle != 0), description);
            free(actual);
        }
        free(expected);
    }
    unlink(source);
    free(expected_lines);
    free(actual_lines);
}
