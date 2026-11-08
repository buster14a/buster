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
// refused with no object written. The asm and control diagnostics a graph's error leaves are compared by place and
// severity alone: their messages are the checker's own, without clang's wording. Include after
// c_check_semantic_recovery_driver_test.c, whose reader this takes.

// Each diagnostic line of `lines` cut after its severity (its place and severity alone), in place.
static void check_test_statement_recovery_places(char* lines)
{
    size_t length = 0;
    for (const char* line = lines; *line != 0;)
    {
        const char* end = strchr(line, '\n');
        const size_t size = end != NULL ? (size_t)(end - line) : strlen(line);
        const char* severity = NULL;
        static const char* const severities[] = { ": error:", ": warning:", ": note:" };
        for (uint32_t s = 0; severity == NULL && s < BUSTER_ARRAY_LENGTH(severities); s += 1)
        {
            const char* found = memmem(line, size, severities[s], strlen(severities[s]));
            severity = found != NULL ? found + strlen(severities[s]) : NULL;
        }
        const size_t kept = severity != NULL ? (size_t)(severity - line) : size;
        memmove(lines + length, line, kept);
        length += kept;
        lines[length++] = '\n';
        line = end != NULL ? end + 1 : line + size;
    }
    lines[length] = 0;
}

static void check_test_driver_statement_recovery(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    typedef struct
    {
        const char* source;
        // Compared by place and severity alone.
        bool places;
    } Fixture;
    static const Fixture fixtures[] = {
        // Two control errors in two bodies, and a later body's runtime warning.
        { "void f(void) { break; }\nvoid g(void) { continue; }\nint h(int x) { return x << 40; }\n", false },
        // Two in one body, after a switch whose own `break` is valid.
        { "void f(int x) { switch (x) { case 1: break; } continue; break; }\n", false },
        // A control error, then a later body's -Wparentheses and runtime warning.
        { "void g(void) { break; }\nint h(int x) { if (x = 2) return x << 40; return 0; }\n", false },
        // -Wuninitialized before the error, none after it, and no -Wreturn-type after it either.
        { "int u0(void) { int y; return y; }\nvoid e(void) { break; }\nint u(void) { int y; return y; }\nint r(int x) { if (x) return 1; }\n", false },
        // After a preprocessing error, the unreachable statements' and the unselected arms' runtime warnings.
        { "#error boom\nint h(int x) { if (0) return x << 40; return 0; }\nint k(int x) { return 1 ? 0 : x / 0; }\n"
          "int m(int x) { return 0; return x / 0; }\n", false },
        // A body before the error keeps its reachability, as does a file-scope initializer after it; after it, the short
        // circuits, a static selection's other arms, a `goto`'s and a `for (;;)`'s unreachable statements, the bounds, a
        // null dereference, a sign comparison and a constant conversion are given, but nothing in `sizeof`.
        { "int before(int x) { if (0) return x << 40; return 0; }\nint g1 = 1 ? 0 : 1 / 0;\n#error boom\nint g2 = 1 ? 0 : 1 / 0;\n"
          "int a(int x) { return 0 && x / 0; }\nint b(int x) { return sizeof(x / 0); }\n"
          "int c(int x) { return _Generic(x, int: 0, default: x / 0); }\nint d(int x) { return __builtin_choose_expr(1, 0, x / 0); }\n"
          "int e(int x) { return x || (x / 0); }\nint f(int x) { int a[2]; (void)x; return 0 ? a[4] : 0; }\n"
          "int g(int x) { for (;;) {} return x / 0; }\nint h(int x) { goto out; x = x / 0; out: return 0; }\n"
          "unsigned i(int x, unsigned u) { if (0) return x < u; return 0; }\nchar j(void) { if (0) return 300; return 0; }\n"
          "int k(void) { if (0) return *(int *)0; return 0; }\n", false },
        // An error inside a body: the body's runtime warnings before it follow it, at the body's end.
        { "int a(int x) { if (0) return x << 40; return 0; }\nint b(int x) { if (0) return x << 40; break; return 0; }\n"
          "int c(int x) { if (0) return x << 40; return 0; }\n", false },
        // A recovered attribute error (phase 1's), then a body's unreachable runtime warning.
        { "__attribute__((nonnull(5))) void g(int *p);\nint s(int x) { if (0) return x << 40; return x; }\nint u(void) { int y; return y; }\n", false },
        // A graph's error, then a later statement's asm and control errors.
        { "struct S { int a; } s;\nint f(void) { return s.b; }\nvoid a(int x) { __asm__(\"\" : \"=r\"(1)); }\n", true },
        { "struct S { int a; } s;\nint f(void) { return s.b; }\nint g(double x) { switch (x) { case 1: return 1; } return 0; }\n", true },
        // A graph's error in one of an asm statement's operands leaves its other operands checked.
        { "struct S { int a; } s;\nvoid b(void) { __asm__(\"\" : : \"r\"(s.b), \"m\"(1)); }\n", true },
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
        const bool made = parse_test_write(source, fixtures[f].source, strlen(fixtures[f].source));
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
                if (lines && fixtures[f].places)
                {
                    check_test_statement_recovery_places(expected_lines);
                }
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
                    bool same = actual != NULL && check_test_diagnostic_lines(actual, actual_lines, LINES);
                    if (same && fixtures[f].places)
                    {
                        check_test_statement_recovery_places(actual_lines);
                    }
                    same = same && strcmp(expected_lines, actual_lines) == 0;
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
