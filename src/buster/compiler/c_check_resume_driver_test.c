// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// clang's recovery past a declaration's missing `;` before a declaration specifier (#147, check_prefix_resume): it reads
// the specifier as the next declaration's, `int x _Alignas(3);`'s `_Alignas(3);` as a declaration that declares nothing,
// and checks what follows as any declaration; and the declarations of no declarator it makes there, which have no type
// specifier (`const;`, `_Alignas(8);`, a calling convention's keyword alone) or a function specifier or restrict, as
// clang's ParsedFreeStandingDeclSpec takes them (check_input_declaration). On original fixtures, the whole output, line
// for line, by both paths of -fsyntax-only, under gnu17 and gnu23, and for one of them under the warnings' options.
// Include after c_check_label_attribute_driver_test.c, whose reader this takes.

// The diagnostic lines of `text` into `lines`, which has room for its length, as check_test_diagnostic_lines takes them,
// each searched within itself alone: linear in a long output (200,000 lines), where a search to the text's end for each
// line is quadratic.
static void check_test_resumed_lines(const char* text, char* lines)
{
    size_t length = 0;
    for (const char* line = text; line != NULL && *line != 0;)
    {
        const char* end = strchr(line, '\n');
        const size_t size = end != NULL ? (size_t)(end - line) : strlen(line);
        const bool kept = memmem(line, size, ": error: ", 9) != NULL || memmem(line, size, ": warning: ", 11) != NULL || memmem(line, size, ": note: ", 8) != NULL;
        if (kept)
        {
            memcpy(lines + length, line, size);
            length += size;
            lines[length++] = '\n';
        }
        line = end != NULL ? end + 1 : NULL;
    }
    lines[length] = 0;
}

static void check_test_driver_resumed_declarations(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char* const fixtures[] = {
        "int x _Alignas(3);\n",
        "int x _Alignas(8);\nint y const;\nint z static;\n",
        // What follows is checked as clang's recovery reads it: the declaration before the specifier, and those after it.
        "int a const int b;\nconst int *c = &b;\nint d = 1 << 40;\n",
        "typedef int T;\nint a T;\nint b typedef;\nint c _Atomic;\nint d typeof(int);\nint e __stdcall;\nint f _Nonnull;\nint g register;\n",
        "int a inline;\n",
        "int b _Noreturn;\n",
        "int c restrict;\n",
        "int d inline _Noreturn;\n",
        "int x __attribute__((unused)) _Alignas(8);\nint y _Alignas(8) __attribute__((unused));\n",
        // A body's: its declarations after the specifier, and a second missing `;` there, which the parser's recovery skips
        // with the rest of the body and the view's next round finds.
        "void f(void) { int x const; int y = 1 << 40; }\n",
        "void f(void) { int x const; int y static; }\n",
        "void f(void) { int x const; }\nvoid g(void) { int y _Alignas(4); }\n",
        // A body's twelve, one round each, and what follows them.
        "void f(void) { int x0 const; int x1 const; int x2 const; int x3 const; int x4 const; int x5 const; int x6 const; int x7 const;\n"
        "int x8 const; int x9 const; int x10 const; int x11 const; int final = 1 << 40; }\n",
        // The parser's errors in the declaration after the specifier, and in a later one.
        "int x const y z;\n",
        "int x _Alignas(3);\nvoid f(void) { int q = ; }\n",
        // No recovery: a `#pragma pack` between, whose annotation token clang's parser reads first, and _Static_assert,
        // which is no declaration specifier; a pragma its preprocessor takes alone doesn't come between.
        "int x\n#pragma pack(1)\nconst;\n",
        "int x _Static_assert(1, \"\");\n",
        "int x\n#pragma clang diagnostic ignored \"-Wunused\"\nconst;\n",
        // Declarations of no declarator as written: no type specifier, a function specifier, restrict.
        "const;\n_Alignas(8);\nstatic;\n__stdcall;\n_Nonnull;\ntypedef const;\n_Atomic const;\n",
        "inline restrict _Noreturn int;\n",
        "typedef int *P;\nrestrict P;\n",
        "void f(void) { _Alignas(8); __stdcall; _Noreturn; }\n",
    };
    static const char* const standards[] = { "-std=gnu17", "-std=gnu23" };
    static const char* const options[] = { NULL, "-w", "-Wno-missing-declarations", "-Werror=missing-declarations" };
    enum { LINES = 16384 };
    char* expected_lines = malloc(LINES);
    char* actual_lines = malloc(LINES);
    char source[PATH_MAX];
    for (uint32_t f = 0; expected_lines != NULL && actual_lines != NULL && f < BUSTER_ARRAY_LENGTH(fixtures); f += 1)
    {
        snprintf(source, sizeof(source), "%s/resumed-declaration-%u.c", directory, f);
        const bool made = parse_test_write(source, fixtures[f], strlen(fixtures[f]));
        // The warnings' options on the first fixture alone.
        const uint32_t option_count = f == 0 ? BUSTER_ARRAY_LENGTH(options) : 1;
        for (uint32_t s = 0; made && s < BUSTER_ARRAY_LENGTH(standards); s += 1)
        {
            for (uint32_t o = 0; o < option_count; o += 1)
            {
                char* arguments[] = { (char*)clang, (char*)standards[s], "-fsyntax-only", "-fno-caret-diagnostics", source, NULL, NULL, NULL };
                uint32_t count = 5;
                if (options[o] != NULL)
                {
                    arguments[count++] = (char*)options[o];
                }
                char* expected = NULL;
                const int oracle = check_test_driver_spawn(arguments, &expected);
                const bool lines = expected != NULL && check_test_diagnostic_lines(expected, expected_lines, LINES);
                check_test_expect(test, lines, "resumed declarations: clang's lines read");
                for (uint32_t scalar = 0; lines && scalar < 2; scalar += 1)
                {
                    arguments[0] = (char*)executable;
                    arguments[count] = scalar != 0 ? "--scalar" : NULL;
                    char* actual = NULL;
                    const int status = check_test_driver_spawn(arguments, &actual);
                    const bool same = actual != NULL && check_test_diagnostic_lines(actual, actual_lines, LINES) && strcmp(expected_lines, actual_lines) == 0;
                    char description[4096];
                    snprintf(description, sizeof(description), "resumed declaration %u, %s %s, %s: status %d (clang %d), lines\n%s(clang\n%s)", f, standards[s],
                             options[o] != NULL ? options[o] : "", scalar != 0 ? "scalar" : "vector", status, oracle, actual_lines, expected_lines);
                    check_test_expect(test, same && (status != 0) == (oracle != 0), description);
                    free(actual);
                }
                free(expected);
            }
        }
        check_test_expect(test, made, "resumed declarations: fixture written");
        unlink(source);
    }
    free(expected_lines);
    free(actual_lines);
    // Many bodies of two each, whose second errors come a round after their first (#1443's review): the whole output,
    // without an error limit, by both paths, in about linear time (check_prefix_sort_diagnostics).
    enum { BODIES = 50000 };
    const size_t capacity = (size_t)BODIES * 64 + 1;
    char* text = malloc(capacity);
    size_t length = 0;
    for (uint32_t b = 0; text != NULL && b < BODIES; b += 1)
    {
        length += (size_t)snprintf(text + length, capacity - length, "void f%u(void) { int x const; int y static; }\n", b);
    }
    snprintf(source, sizeof(source), "%s/resumed-declarations-many.c", directory);
    const bool made = text != NULL && parse_test_write(source, text, length);
    char* arguments[] = { (char*)clang, "-std=gnu17", "-fsyntax-only", "-fno-caret-diagnostics", "-ferror-limit=0", source, NULL, NULL };
    char* expected = NULL;
    const int oracle = made ? check_test_driver_spawn(arguments, &expected) : -1;
    char* expected_many = expected != NULL ? malloc(strlen(expected) + 1) : NULL;
    if (expected_many != NULL)
    {
        check_test_resumed_lines(expected, expected_many);
    }
    const bool lines = expected_many != NULL && expected_many[0] != 0;
    for (uint32_t scalar = 0; lines && scalar < 2; scalar += 1)
    {
        arguments[0] = (char*)executable;
        arguments[6] = scalar != 0 ? "--scalar" : NULL;
        char* actual = NULL;
        const int status = check_test_driver_spawn(arguments, &actual);
        char* actual_many = actual != NULL ? malloc(strlen(actual) + 1) : NULL;
        if (actual_many != NULL)
        {
            check_test_resumed_lines(actual, actual_many);
        }
        const bool same = actual_many != NULL && strcmp(expected_many, actual_many) == 0;
        char description[256];
        snprintf(description, sizeof(description), "resumed declarations in %u bodies, %s: status %d (clang %d), the same lines", BODIES,
                 scalar != 0 ? "scalar" : "vector", status, oracle);
        check_test_expect(test, same && status != 0 && oracle != 0, description);
        free(actual_many);
        free(actual);
    }
    check_test_expect(test, made && lines, "resumed declarations in many bodies: fixture written and clang's lines read");
    free(expected_many);
    free(expected);
    free(text);
    unlink(source);
}
