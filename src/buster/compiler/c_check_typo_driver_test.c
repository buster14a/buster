// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Typo correction (#1447, c_check_typo.c): clang 23's "did you mean" for an undeclared name in C. An undeclared identifier
// in a value context is corrected to the one visible object, function or enumerator nearest it, with its note, and goes on
// as the candidate, so that the diagnostics its type gives follow; a call to an undeclared function that clang declares
// implicitly (gnu17) keeps its error and gets the suggestion of a function as notes. Ties, typedefs, names declared later,
// short typos and library builtins give none; past 50 attempts a unit corrects no name. On original fixtures, the whole
// output, line for line, and the status, by the vector path, the scalar one and deferred bodies on one, two and three lanes
// (-c's readings: -fsyntax-only starts no lanes), under gnu17 and gnu23 and the warning and error-limit options; and -c of
// each, which must be refused with no object written. A stress unit of 4000 globals and 50 typos, each with its
// suggestion, is compared the same way. Include after c_check_semantic_recovery_driver_test.c, whose reader this takes.

// The stress unit: `globals` globals named global_name_NNNN, then a function that uses `typos` of them misspelled by one
// byte (globl_name_NNNN), each with a unique nearest name. Into `text`, `capacity` bytes; its length, 0 where it didn't fit.
static uint32_t check_test_typo_stress(char* text, uint32_t capacity, uint32_t globals, uint32_t typos)
{
    uint32_t length = 0;
    bool fits = true;
    for (uint32_t g = 0; fits && g < globals; g += 1)
    {
        const int written = snprintf(text + length, capacity - length, "int global_name_%04u;\n", g);
        fits = written > 0 && (uint32_t)written < capacity - length;
        length += fits ? (uint32_t)written : 0;
    }
    const int opened = fits ? snprintf(text + length, capacity - length, "int use(void)\n{\n    int sum = 0;\n") : 0;
    fits = fits && opened > 0 && (uint32_t)opened < capacity - length;
    length += fits ? (uint32_t)opened : 0;
    for (uint32_t t = 0; fits && t < typos; t += 1)
    {
        const int written = snprintf(text + length, capacity - length, "    sum += globl_name_%04u;\n", t * (globals / typos));
        fits = written > 0 && (uint32_t)written < capacity - length;
        length += fits ? (uint32_t)written : 0;
    }
    const int closed = fits ? snprintf(text + length, capacity - length, "    return sum;\n}\n") : 0;
    fits = fits && closed > 0 && (uint32_t)closed < capacity - length;
    length += fits ? (uint32_t)closed : 0;
    return fits ? length : 0;
}

static void check_test_driver_typo_correction(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char* const fixtures[] = {
        // A value's typo, corrected to an object, an enumerator, a local, a parameter and a function used as a value; a call
        // to an undeclared function: gnu17's implicit declaration with its notes, gnu23's use corrected.
        "int counter;\nint compute(int x) { return x; }\nenum { RED, GREEN };\nint f(void) { return countr + 1; }\n"
        "int h(void) { return GREN; }\nint k(int param) { int local = 3; return locl + parm; }\n"
        "static int helper(int x) { return x; }\nint m(void) { return helpr(2); }\nint (*p)(int) = &compte;\n",
        // The corrected name's type drives what follows: a constant's conversion into it, an assignment as a condition.
        "unsigned char small;\nshort level;\nvoid f(void) { smal = 300; levl = 70000; }\nint h(void) { return levl; }\n"
        "enum Color { RED, GREEN } color;\nint g(void) { if (colr = GREN) return 1; return 0; }\n",
        // A pointer's type, whose error comes after the typo's. (Alone: a unit with that error gets no common warnings yet.)
        "float fl;\nint k(void) { int *q = &fll; return *q; }\n",
        // Two typos in one full expression, each its own error, before the return's conversion; the same typo again.
        "static int helper(int x) { return x; }\nint g(void) { return helper(2) + helpr(1) + helpr(3); }\n"
        "int counter;\nint a(void) { return countr + countr; }\nint b(void) { return countr; }\n",
        // No suggestion: a tie (whichever candidate would type-check), a typedef's name, a name declared later, a typo
        // shorter than 3 bytes, a distance past a third of the typo's length, a library builtin's name.
        "int abcx, abcy;\nint t1(void) { return abcz; }\nstruct P { int x; } pa1;\nint pb1;\nint t2(void) { return pc1 + 1; }\n"
        "typedef int myint;\nint t3(void) { return myin; }\nint t4(void) { return later1; }\nint later;\n"
        "int ab;\nint t5(void) { return ac; }\nint abcdef;\nint t6(void) { return abcxyz; }\nint t7(void) { prinf(\"x\"); return 0; }\n",
        // An implicit call's candidates are functions alone; a second call of a name an earlier one declared gets no
        // suggestion; underscores count as bytes.
        "int process(int);\nint g3(void) { return proces(1); }\nint g4(int x) { return x + sizeof(proces(1)); }\n"
        "int __hidden1;\nint h1(void) { return __hiden1; }\nint _xval1;\nint h2(void) { return xval1; }\n"
        "int yval2;\nint h3(void) { return _yval2; }\nint rval10x;\nint h4(void) { return _rval10; }\n",
    };
    static const char* const standards[] = { "-std=gnu17", "-std=gnu23" };
    static const char* const options[] = { NULL, "-Wall", "-ferror-limit=2", "-w" };
    enum { LINES = 32768, STRESS = 256 * 1024 };
    char* expected_lines = malloc(LINES);
    char* actual_lines = malloc(LINES);
    char* stress = malloc(STRESS);
    // The limit: 55 typos, each correctable, of which the first 50 are; and 52 attempts that fail before a correctable one.
    char* limit = malloc(8192);
    char* failing = malloc(8192);
    uint32_t limit_length = 0;
    uint32_t failing_length = 0;
    for (uint32_t i = 0; limit != NULL && i < 55; i += 1)
    {
        limit_length += (uint32_t)snprintf(limit + limit_length, 8192 - limit_length, "int counter%02u;\n", i);
    }
    limit_length += limit != NULL ? (uint32_t)snprintf(limit + limit_length, 8192 - limit_length, "int f(void) { return 0") : 0;
    for (uint32_t i = 0; limit != NULL && i < 55; i += 1)
    {
        limit_length += (uint32_t)snprintf(limit + limit_length, 8192 - limit_length, " + countr%02u", i);
    }
    limit_length += limit != NULL ? (uint32_t)snprintf(limit + limit_length, 8192 - limit_length, "; }\n") : 0;
    failing_length = failing != NULL ? (uint32_t)snprintf(failing, 8192, "int counterzz;\nint f(void) { return 0") : 0;
    for (uint32_t i = 0; failing != NULL && i < 52; i += 1)
    {
        failing_length += (uint32_t)snprintf(failing + failing_length, 8192 - failing_length, " + qqqq");
    }
    failing_length += failing != NULL ? (uint32_t)snprintf(failing + failing_length, 8192 - failing_length, " + countrzz; }\n") : 0;
    const uint32_t stress_length = stress != NULL ? check_test_typo_stress(stress, STRESS, 4000, 50) : 0;
    const char* units[BUSTER_ARRAY_LENGTH(fixtures) + 3];
    uint32_t lengths[BUSTER_ARRAY_LENGTH(fixtures) + 3];
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(fixtures); f += 1)
    {
        units[f] = fixtures[f];
        lengths[f] = (uint32_t)strlen(fixtures[f]);
    }
    units[BUSTER_ARRAY_LENGTH(fixtures)] = limit;
    lengths[BUSTER_ARRAY_LENGTH(fixtures)] = limit_length;
    units[BUSTER_ARRAY_LENGTH(fixtures) + 1] = failing;
    lengths[BUSTER_ARRAY_LENGTH(fixtures) + 1] = failing_length;
    units[BUSTER_ARRAY_LENGTH(fixtures) + 2] = stress;
    lengths[BUSTER_ARRAY_LENGTH(fixtures) + 2] = stress_length;
    check_test_expect(test, limit != NULL && failing != NULL && stress_length != 0, "typo correction: the limit's and the stress's units are made");
    char source[PATH_MAX];
    char object[PATH_MAX];
    snprintf(object, sizeof(object), "%s/typo-correction.o", directory);
    for (uint32_t f = 0; expected_lines != NULL && actual_lines != NULL && f < BUSTER_ARRAY_LENGTH(units); f += 1)
    {
        snprintf(source, sizeof(source), "%s/typo-correction-%u.c", directory, f);
        const bool made = units[f] != NULL && lengths[f] != 0 && parse_test_write(source, units[f], lengths[f]);
        for (uint32_t s = 0; made && s < BUSTER_ARRAY_LENGTH(standards); s += 1)
        {
            for (uint32_t o = 0; o < BUSTER_ARRAY_LENGTH(options); o += 1)
            {
                // (The limit's and the stress's whole output: no error limit, where the option gives none.)
                const char* option = options[o] != NULL ? options[o] : f >= BUSTER_ARRAY_LENGTH(fixtures) ? "-ferror-limit=0" : NULL;
                char* arguments[12] = { (char*)clang, (char*)standards[s], "-fsyntax-only", "-fno-caret-diagnostics", source };
                uint32_t count = 5;
                if (option != NULL)
                {
                    arguments[count++] = (char*)option;
                }
                arguments[count] = NULL;
                char* expected = NULL;
                const int oracle = check_test_driver_spawn(arguments, &expected);
                // The deferred readings' arguments: -c's, of no object (-fsyntax-only starts no lanes).
                char* compile[16] = { (char*)executable, (char*)standards[s], "-c", "-o", object, "-fno-caret-diagnostics", source };
                uint32_t compile_count = 7;
                if (option != NULL)
                {
                    compile[compile_count++] = (char*)option;
                }
                const bool lines = expected != NULL && check_test_diagnostic_lines(expected, expected_lines, LINES);
                check_test_expect(test, lines && oracle != 0, "typo correction: clang rejects the fixture and its lines are read");
                // The vector path, the scalar one, and deferred bodies on 1, 2 and 3 lanes.
                static const char* const mode_lanes[] = { NULL, NULL, "1", "2", "3" };
                for (uint32_t mode = 0; lines && mode < BUSTER_ARRAY_LENGTH(mode_lanes); mode += 1)
                {
                    arguments[0] = (char*)executable;
                    uint32_t at = count;
                    char** spawned = arguments;
                    if (mode == 1)
                    {
                        arguments[at++] = "--scalar";
                        arguments[at] = NULL;
                    }
                    else if (mode_lanes[mode] != NULL)
                    {
                        at = compile_count;
                        compile[at++] = "--deferred-bodies";
                        compile[at++] = "--lanes";
                        compile[at++] = (char*)mode_lanes[mode];
                        compile[at] = NULL;
                        spawned = compile;
                        unlink(object);
                    }
                    else
                    {
                        arguments[at] = NULL;
                    }
                    char* actual = NULL;
                    const int status = check_test_driver_spawn(spawned, &actual);
                    const bool written = spawned == compile && access(object, F_OK) == 0;
                    const bool same = actual != NULL && check_test_diagnostic_lines(actual, actual_lines, LINES) && strcmp(expected_lines, actual_lines) == 0;
                    char description[8192];
                    snprintf(description, sizeof(description), "typo correction %u, %s %s, mode %u: status %d (clang %d), lines\n%.3000s(clang\n%.3000s)", f, standards[s],
                             option != NULL ? option : "", mode, status, oracle, actual_lines, expected_lines);
                    check_test_expect(test, same && (status != 0) == (oracle != 0) && !written, description);
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
            snprintf(description, sizeof(description), "typo correction %u, -c by the %s path: refused (status %d), no object", f, scalar != 0 ? "scalar" : "vector",
                     status);
            check_test_expect(test, status != 0 && !written, description);
            free(text);
        }
        check_test_expect(test, made, "typo correction: fixture written");
        unlink(source);
    }
    unlink(object);
    free(expected_lines);
    free(actual_lines);
    free(stress);
    free(limit);
    free(failing);
}
