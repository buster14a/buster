// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Translation-unit-wide semantic error recovery, phase 3 (#1447): statements after an error.
// - A `break` or `continue` outside its loop is clang's invalid statement, after which the body and the unit are checked
//   on, so a second one is reported too (CheckInput's recovered reports).
// - A body clang ends after the unit's first error gets no CFG analysis (no -Wuninitialized, no -Wreturn-type), and every
//   runtime-behavior warning it queued is given, reachable or not (check_common_flush).
// - An expression graph's errors no longer cut the asm and control diagnostics of the statements that don't read them.
// On original fixtures, the whole output, line for line, and the status, by the vector path, the scalar one and deferred
// bodies on two lanes, under gnu17 and gnu23 and the warning and error-limit options; and -c of each, which must be
// refused with no object written. The asm and control errors are compared whole too: every one, in clang's words, a
// duplicate case's with its "previous case defined here" note, after a graph's error and in a unit without one (#1447).
// A rejected unit's other bodies get their common warnings (check_driver_rejected_common, #1447).
// Include after c_check_semantic_recovery_driver_test.c, whose reader this takes.

static void check_test_driver_statement_recovery(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char* const fixtures[] = {
        // Two control errors in two bodies, and a later body's runtime warning.
        "void f(void) { break; }\nvoid g(void) { continue; }\nint h(int x) { return x << 40; }\n",
        // Two in one body, after a switch whose own `break` is valid.
        "void f(int x) { switch (x) { case 1: break; } continue; break; }\n",
        // A control error, then a later body's -Wparentheses and runtime warning.
        "void g(void) { break; }\nint h(int x) { if (x = 2) return x << 40; return 0; }\n",
        // -Wuninitialized before the error, none after it, and no -Wreturn-type after it either.
        "int u0(void) { int y; return y; }\nvoid e(void) { break; }\nint u(void) { int y; return y; }\nint r(int x) { if (x) return 1; }\n",
        // After a preprocessing error, the unreachable statements' and the unselected arms' runtime warnings.
        "#error boom\nint h(int x) { if (0) return x << 40; return 0; }\nint k(int x) { return 1 ? 0 : x / 0; }\n"
          "int m(int x) { return 0; return x / 0; }\n",
        // A body before the error keeps its reachability, as does a file-scope initializer after it; after it, the short
        // circuits, a static selection's other arms, a `goto`'s and a `for (;;)`'s unreachable statements, the bounds, a
        // null dereference, a sign comparison and a constant conversion are given, but nothing in `sizeof`.
        "int before(int x) { if (0) return x << 40; return 0; }\nint g1 = 1 ? 0 : 1 / 0;\n#error boom\nint g2 = 1 ? 0 : 1 / 0;\n"
          "int a(int x) { return 0 && x / 0; }\nint b(int x) { return sizeof(x / 0); }\n"
          "int c(int x) { return _Generic(x, int: 0, default: x / 0); }\nint d(int x) { return __builtin_choose_expr(1, 0, x / 0); }\n"
          "int e(int x) { return x || (x / 0); }\nint f(int x) { int a[2]; (void)x; return 0 ? a[4] : 0; }\n"
          "int g(int x) { for (;;) {} return x / 0; }\nint h(int x) { goto out; x = x / 0; out: return 0; }\n"
          "unsigned i(int x, unsigned u) { if (0) return x < u; return 0; }\nchar j(void) { if (0) return 300; return 0; }\n"
          "int k(void) { if (0) return *(int *)0; return 0; }\n",
        // An error inside a body: the body's runtime warnings before it follow it, at the body's end.
        "int a(int x) { if (0) return x << 40; return 0; }\nint b(int x) { if (0) return x << 40; break; return 0; }\n"
          "int c(int x) { if (0) return x << 40; return 0; }\n",
        // A recovered attribute error (phase 1's), then a body's unreachable runtime warning.
        "__attribute__((nonnull(5))) void g(int *p);\nint s(int x) { if (0) return x << 40; return x; }\nint u(void) { int y; return y; }\n",
        // A graph's error, then a later statement's asm and control errors.
        "struct S { int a; } s;\nint f(void) { return s.b; }\nvoid a(int x) { __asm__(\"\" : \"=r\"(1)); }\n",
        "struct S { int a; } s;\nint f(void) { return s.b; }\nint g(double x) { switch (x) { case 1: return 1; } return 0; }\n",
        // A graph's error in one of an asm statement's operands leaves its other operands checked.
        "struct S { int a; } s;\nvoid b(void) { __asm__(\"\" : : \"r\"(s.b), \"m\"(1)); }\n",
        // After a graph's error, every switch's conflicts, each noted at the case it repeats: the singletons in value
        // order, the ranges by their low values, a nested switch's at its own end, and the values in the condition's type.
        "struct S { int a; } s;\nint f(void) { return s.b; }\nint g(int i)\n{\n"
        "    switch (i) { case 7: case 7: case 7: break; case 1 ... 3: case 2 ... 4: break; }\n"
        "    switch (i) { case 4: case 2: case 4: case 2: break; }\n"
        "    switch (i) { case 1: { switch (i) { case 2: case 2: break; } } case 1: break; }\n"
        "    switch (i) { case 1 ... 3: case 2 ... 4: case 3: case 0 ... 9: break; }\n"
        "    switch (i) { case 5: case 1 ... 9: case 7: break; }\n"
        "    switch (i) { case -1: case 0xffffffffu: break; }\n"
        "    switch ((unsigned char)i) { case 1: case 1: break; }\n    return 0;\n}\n",
        // The defaults, by clang's walk of the order it makes them in: a default labelling another is made first.
        "struct S { int a; } s;\nint f(void) { return s.b; }\nint g(int i, int y)\n{\n"
        "    switch (i) { default: break; default: break; default: default: break; }\n"
        "    switch (i) { default: { y = 1; default: y = 2; } }\n"
        "    switch (i) { default: case 1: default: y = 3; }\n    return y;\n}\n",
        // A condition of no integer type, by its typedef's name, a pointer's and a record's; and a case that reads the
        // error is still a case of its switch.
        "typedef double D;\nstruct S { int a; } s;\nint f(void) { return s.b; }\n"
        "int g(double x, D y, int *p) { switch (x) { case 1: return 1; } switch (y) { default: return 2; } switch (p) { default: return 3; }"
        " switch (s) { default: return 4; } return 0; }\n"
        "int h(int i) { switch (i) { case 1: return s.b; case 1: return 2; } return 0; }\n",
        // Without a graph's error too.
        "int g(int i, double x)\n{\n    switch (i) { case 2: case 2: break; }\n    switch (x) { case 1: break; }\n"
        "    switch (i) { default: default: break; }\n    return 0;\n}\n",
        // After a graph's error, each asm statement's first error in clang's words (an output's naming an input, and the
        // reverse, as clang's do), an incomplete type's with its forward declaration's note, a tie's after the
        // statement's other operands, and a cast's, which doesn't end its statement.
        "typedef float F;\nstruct S { int a : 3; int b; } s;\nstruct I;\nextern struct I inc;\nconst int ci = 0;\n_BitInt(8) bi;\n"
        "void v(void);\nint x;\nint f(void) { return s.c; }\nvoid a(void)\n{\n"
        "    __asm__(\"\" : \"=r\"(1));\n    __asm__(\"\" : \"=r\"(ci));\n    __asm__(\"\" : \"=m\"(s.a));\n"
        "    __asm__(\"\" : : \"m\"(s.a));\n    __asm__(\"\" : : \"m\"(1));\n    __asm__(\"\" : : \"r\"(v()));\n"
        "    __asm__(\"\" : : \"r\"(bi));\n    __asm__(\"\" : \"=r\"(bi));\n    __asm__(\"\" : \"=r\"(inc));\n"
        "    __asm__(\"\" : : \"m\"(inc));\n    __asm__(\"\" : \"=r\"(x) : \"0\"((F)1.0f));\n"
        "    __asm__(\"\" : \"=r\"(x), \"=r\"(2) : \"m\"(3));\n    __asm__(\"\" : \"=r\"((int)x), \"=r\"(1), \"=r\"((long)x));\n"
        "    __asm__(\"\" : \"=r\"(x) : \"0\"(1.0), \"m\"(2));\n    __asm__(\"\" : \"=r\"(((unsigned)(x))));\n}\n",
        // Without a graph's error too.
        "int x;\nvoid a(void)\n{\n    __asm__(\"\" : \"=r\"(1));\n    __asm__(\"\" : : \"m\"(1));\n"
        "    __asm__(\"\" : \"=r\"((long)x), \"=r\"(x) : \"0\"(1.0));\n}\n",
        // A graph's error in one body: the others' common warnings, -Wuninitialized before it and none after it, and their
        // -Wreturn-type, runtime, -Wswitch, -Wformat and -Wempty-body warnings (#1447: a second reading without that body).
        "struct S { int a; } s;\n"
        "enum E { A, B, C };\n"
        "int printf(const char *, ...);\n"
        "int un0(void) { int y; return y; }\n"
        "int bad(void) { return s.b; }\n"
        "int after(int x) { if (x = 2) return x << 40; return 0; }\n"
        "void sw(enum E e) { switch (e) { case A: break; } }\n"
        "void fmt(int x) { printf(\"%s\\n\", x); }\n"
        "int ret(int x) { if (x) return 1; }\n"
        "int un(void) { int y; return y; }\n"
        "void empty(int x) { if (x); }\n",
        // A control error on the valid path: its scope's unused variable and the unit-end ones are left out, the other
        // bodies' common warnings given.
        "int printf(const char *, ...);\n"
        "int f(int x)\n"
        "{\n"
        "    int u;\n"
        "    switch (x) { case 1: case 1: break; }\n"
        "    return 0;\n"
        "}\n"
        "int g(int y) { if (y = 1) return printf(\"%d\\n\", 1.5); return 0; }\n"
        "static int z;\n"
        "void h(void) { int w; }\n",
        // An asm error's body left out, the next one's warnings given.
        "void a(void) { __asm__(\"\" : \"=r\"(1)); }\n"
        "int g(int y) { if (y = 1) return 1; return y << 40; }\n",
        // -Wreturn-type before a graph's error, and the next body's unused variable and -Wparentheses.
        "int r(int x) { if (x) return 1; }\n"
        "struct S { int a; } s;\n"
        "int bad(void) { return s.b; }\n"
        "int g(int y) { int w; if (y = 1) return 1; return 0; }\n",
        // A reader's error (an undeclared name), after which the other bodies' unused variables and common warnings are given.
        "int f(void){ return zz; }\n"
        "int g(int y){ if (y=1) return 1; int w; return 0; }\n"
        "int h(void) { return 1 << 40; }\n",
        // The same with bodies before it.
        "int g(int y){ int w; return 0; }\n"
        "int f(void){ return zz; }\n"
        "int g2(int y){ int w2; return 0; }\n",
    };
    static const char* const standards[] = { "-std=gnu17", "-std=gnu23" };
    static const char* const options[] = { NULL, "-Wall", "-ferror-limit=2", "-w" };
    enum { LINES = 16384 };
    char* expected_lines = malloc(LINES);
    char* actual_lines = malloc(LINES);
    char source[PATH_MAX];
    char object[PATH_MAX];
    snprintf(object, sizeof(object), "%s/statement-recovery.o", directory);
    for (uint32_t f = 0; expected_lines != NULL && actual_lines != NULL && f < BUSTER_ARRAY_LENGTH(fixtures); f += 1)
    {
        snprintf(source, sizeof(source), "%s/statement-recovery-%u.c", directory, f);
        const bool made = parse_test_write(source, fixtures[f], strlen(fixtures[f]));
        for (uint32_t s = 0; made && s < BUSTER_ARRAY_LENGTH(standards); s += 1)
        {
            for (uint32_t o = 0; o < BUSTER_ARRAY_LENGTH(options); o += 1)
            {
                char* arguments[12] = { (char*)clang, (char*)standards[s], "-fsyntax-only", "-fno-caret-diagnostics", source };
                uint32_t count = 5;
                if (options[o] != NULL)
                {
                    arguments[count++] = (char*)options[o];
                }
                arguments[count] = NULL;
                char* expected = NULL;
                const int oracle = check_test_driver_spawn(arguments, &expected);
                const bool lines = expected != NULL && check_test_diagnostic_lines(expected, expected_lines, LINES);
                check_test_expect(test, lines && oracle != 0, "statement recovery: clang rejects the fixture and its lines are read");
                for (uint32_t mode = 0; lines && mode < 3; mode += 1)
                {
                    arguments[0] = (char*)executable;
                    uint32_t at = count;
                    if (mode == 1)
                    {
                        arguments[at++] = "--scalar";
                    }
                    else if (mode == 2)
                    {
                        arguments[at++] = "--deferred-bodies";
                        arguments[at++] = "--lanes";
                        arguments[at++] = "2";
                    }
                    arguments[at] = NULL;
                    char* actual = NULL;
                    const int status = check_test_driver_spawn(arguments, &actual);
                    const bool same = actual != NULL && check_test_diagnostic_lines(actual, actual_lines, LINES) && strcmp(expected_lines, actual_lines) == 0;
                    char description[8192];
                    snprintf(description, sizeof(description), "statement recovery %u, %s %s, mode %u: status %d (clang %d), lines\n%s(clang\n%s)", f, standards[s],
                             options[o] != NULL ? options[o] : "", mode, status, oracle, actual_lines, expected_lines);
                    check_test_expect(test, same && (status != 0) == (oracle != 0), description);
                    free(actual);
                }
                free(expected);
            }
        }
        // -c is refused, and writes no object, by both paths.
        for (uint32_t scalar = 0; made && scalar < 2; scalar += 1)
        {
            unlink(object);
            char* arguments[] = { (char*)executable, "-std=gnu17", "-c", source, "-o", object, scalar != 0 ? "--scalar" : NULL, NULL };
            char* text = NULL;
            const int status = check_test_driver_spawn(arguments, &text);
            const bool written = access(object, F_OK) == 0;
            char description[256];
            snprintf(description, sizeof(description), "statement recovery %u, -c by the %s path: refused (status %d), no object", f, scalar != 0 ? "scalar" : "vector",
                     status);
            check_test_expect(test, status != 0 && !written, description);
            free(text);
        }
        check_test_expect(test, made, "statement recovery: fixture written");
        unlink(source);
    }
    unlink(object);
    free(expected_lines);
    free(actual_lines);
}
