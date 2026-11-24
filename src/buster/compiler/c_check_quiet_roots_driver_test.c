// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1340's quiet roots: the common warnings' loop over a unit's expression roots (check_common_graph_warnings) does not
// visit a root its classes call quiet (check_common_root_classes). With BUSTER_TEST_DEFAULT_PATH_LOG set to a path every
// root is visited all the same, and the command writes a line of counts there (CheckDefaultPathCount, c_check_input.c):
// the roots, those of them the classes call quiet, and those of the quiet whose visit did anything. So a compile with
// the switch is the loop without the classes, and the checks here hold a compile without it to that one: the same
// diagnostics, in the same order, the same object, and no quiet root's visit that acted. Over a unit of each kind of
// root, over generated units, and (c_check_warning_driver_test.c, which has the log's reader) over every warning fixture.

// One compile of `source` with `options` (null-terminated), without the switch and with it: the same status and
// diagnostics, the same object where `object` asks for one, and the switch's counts in `*roots`, `*quiet` and `*acted`.
static bool check_test_driver_quiet_roots_pair(const char* executable, const char* directory, const char* source, const char* const* options, bool object,
                                               uint64_t* roots, uint64_t* quiet, uint64_t* acted, char** diagnostics)
{
    char plain[PATH_MAX];
    char visited[PATH_MAX];
    char log[PATH_MAX];
    snprintf(plain, sizeof(plain), "%s/quiet-roots.o", directory);
    snprintf(visited, sizeof(visited), "%s/quiet-roots-visited.o", directory);
    snprintf(log, sizeof(log), "%s/quiet-roots.log", directory);
    // The test's switch is the environment's: what it held is taken off for the compiles here and put back.
    const char* outer = getenv("BUSTER_TEST_DEFAULT_PATH_LOG");
    char* restored = outer != NULL ? strdup(outer) : NULL;
    unsetenv("BUSTER_TEST_DEFAULT_PATH_LOG");
    char* arguments[2][32] = { 0 };
    for (uint32_t run = 0; run < 2; run += 1)
    {
        uint32_t count = 0;
        arguments[run][count++] = (char*)(executable != NULL ? executable : "checker");
        arguments[run][count++] = "--lanes";
        arguments[run][count++] = "1";
        arguments[run][count++] = "-fno-caret-diagnostics";
        arguments[run][count++] = "-fno-color-diagnostics";
        arguments[run][count++] = object ? "-c" : "-fsyntax-only";
        for (uint32_t option = 0; options[option] != NULL && count < 26; option += 1)
        {
            arguments[run][count++] = (char*)options[option];
        }
        if (object)
        {
            arguments[run][count++] = "-o";
            arguments[run][count++] = run == 0 ? plain : visited;
        }
        arguments[run][count++] = (char*)source;
    }
    const bool written = parse_test_write(log, "", 0);
    char* plain_text = NULL;
    char* visited_text = NULL;
    const int plain_status = written ? check_test_driver_buster_command(arguments[0], &plain_text, false, executable != NULL) : -1;
    setenv("BUSTER_TEST_DEFAULT_PATH_LOG", log, 1);
    const int visited_status = written ? check_test_driver_buster_command(arguments[1], &visited_text, false, executable != NULL) : -1;
    unsetenv("BUSTER_TEST_DEFAULT_PATH_LOG");
    const bool counted = check_test_driver_quiet_roots_counts(log, true, roots, quiet, acted);
    const bool same = plain_status >= 0 && plain_status == visited_status && plain_text != NULL && visited_text != NULL && strcmp(plain_text, visited_text) == 0 &&
                      (!object || plain_status != 0 || check_test_driver_same_files(plain, visited));
    if (!same || !counted)
    {
        fprintf(stderr, "test-checker-driver: quiet-roots: %s: status %d and %d with the switch\n%s----\n%s", source, plain_status, visited_status,
                plain_text != NULL ? plain_text : "", visited_text != NULL ? visited_text : "");
    }
    if (diagnostics != NULL)
    {
        *diagnostics = plain_text;
    }
    else
    {
        free(plain_text);
    }
    free(visited_text);
    if (restored != NULL)
    {
        setenv("BUSTER_TEST_DEFAULT_PATH_LOG", restored, 1);
    }
    free(restored);
    unlink(plain);
    unlink(visited);
    unlink(log);
    return same && counted;
}

// A unit of each kind of root. The quiet ones: an initialization, an assignment, a compound assignment and an increment as
// statements, braced or not, a for clause's, a condition, a returned value, a call of a function of no mark, a cast to
// void, and a brace initializer's values under their declaration's `;`. The loud ones, each for one term of the classes:
// a call of a pure function, of one declared warn_unused_result and of a library builtin (the callee's name says its
// result may warn), a comparison, a value and a comma (the statement's marker), a division by zero and a shift past its
// width (a reach), two modifications of one variable and an assignment inside a condition (-Wunsequenced's candidates),
// a statement in unreachable code (unevaluated), and the roots at the brace initializers' and a compound literal's
// plans, which emit between the roots.
static const char check_driver_quiet_roots_unit[] =
    "struct pair { char low; int high; };\n"
    "int g(int);\n"
    "int abs(int);\n"
    "__attribute__((pure)) int p(int);\n"
    "__attribute__((warn_unused_result)) int w(int);\n"
    "int f(int a, int b, unsigned u)\n"
    "{\n"
    "    int r = 0;\n"
    "    r = a;\n"
    "    r += b;\n"
    "    r++;\n"
    "    if (a) r = b;\n"
    "    if (b) { r -= a; }\n"
    "    for (r = 0; r < a; r++) { b += r; }\n"
    "    while (b > 100) b -= 100;\n"
    "    g(a);\n"
    "    p(a);\n"
    "    w(b);\n"
    "    abs(b);\n"
    "    (void)a;\n"
    "    a < b;\n"
    "    a;\n"
    "    a, b;\n"
    "    r = a / 0;\n"
    "    r = a << 40;\n"
    "    r = a++ + a;\n"
    "    if ((r = b) != 0) { r = 1; }\n"
    // A condition's root stands after its body: its modification and its access are its operand's, not the body's.
    "    while (b-- > b) { r += a; }\n"
    "    if (0) { r = a << 41; }\n"
    "    struct pair s = { 300, 2.5 };\n"
    "    r = s.high;\n"
    "    struct pair t = { a = 1, a };\n"
    "    r += t.high;\n"
    // A loud root after each plan and a quiet root: a plan emits before it, where every root's visit puts it.
    "    r = b % 0;\n"
    "    r += (struct pair){ 400, 3 }.high;\n"
    "    r = a;\n"
    "    r -= a % 0;\n"
    "    u += (unsigned)r;\n"
    "    return r + (int)u;\n"
    "}\n";

// The generated units' statements: each a root or several, quiet and loud, with the warnings whose order the loop keeps:
// plans (brace initializers and a compound literal) between roots, displaced roots (conditions and for clauses), a
// statement expression's inner roots, unsequenced modifications and unreachable code.
static const char* const check_driver_quiet_roots_statements[] =
{
    "r = a;",
    "r += b;",
    "r++;",
    "--b;",
    "r = b = a;",
    "g(a);",
    "p(b);",
    "w(a);",
    "(void)b;",
    "(void)p(a);",
    "r = (int)a;",
    "(int)a;",
    "a < b;",
    "a;",
    "a, b;",
    "r = a / 0;",
    "r = b << 40;",
    "r = a++ + a;",
    "a = a++;",
    "r = (a = b) + a;",
    "{ char c = 300; r += c; }",
    "{ int i = 2.5; r += i; }",
    "{ struct pair s = { 300, 2.5 }; r += s.high; }",
    "{ struct pair s = { a = 1, a }; r += s.low; }",
    "{ struct pair s = { 1, 2 }; r += s.high; }",
    "r += (struct pair){ 400, 3.5 }.high;",
    "r += ({ int i = a; i < b; i; });",
    "r += ({ char c = 500; a / 0; c; });",
    "u = a;",
    "r = u < a;",
    "r = sizeof(a / 0);",
    "r = a ? b : (b, a);",
    "(void)a;",
    "r = a == a;",
    "r = (a, b);",
};

// The generated units' contexts of a statement `%s`: braced, an if's and a loop's body with and without braces, a for
// clause, and unreachable code.
static const char* const check_driver_quiet_roots_contexts[] =
{
    "    %s\n",
    "    if (a) %s\n",
    "    if (b) { %s } else { r--; }\n",
    "    while (a-- > b) %s\n",
    "    while (b-- > b) { %s }\n",
    "    for (r = 0; r < b; r++) { %s }\n",
    "    for (int i = 300; i < a; i += 1) %s\n",
    "    if (0) { %s }\n",
    "    do { %s } while (r++ < 3);\n",
    "    switch (a) { case 1: %s break; default: r = 2; }\n",
};

// The generated unit `seed`: three functions of sixteen statements each, a statement and its context chosen by a
// multiplicative congruence (Knuth's MMIX constants), so every run makes the same units.
static uint64_t check_test_driver_quiet_roots_generate(char* text, uint64_t capacity, uint32_t seed)
{
    uint64_t state = UINT64_C(6364136223846793005) * (seed + 1) + UINT64_C(1442695040888963407);
    int length = snprintf(text, capacity, "struct pair { char low; int high; };\nint g(int);\n__attribute__((pure)) int p(int);\n"
                                          "__attribute__((warn_unused_result)) int w(int);\n");
    for (uint32_t function = 0; function < 3 && length > 0 && (uint64_t)length < capacity; function += 1)
    {
        length += snprintf(text + length, capacity - (uint64_t)length, "int f%u(int a, int b, unsigned u)\n{\n    int r = 0;\n", function);
        for (uint32_t statement = 0; statement < 16 && length > 0 && (uint64_t)length < capacity; statement += 1)
        {
            state = state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
            const char* body = check_driver_quiet_roots_statements[(state >> 33) % BUSTER_ARRAY_LENGTH(check_driver_quiet_roots_statements)];
            state = state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
            const char* context = check_driver_quiet_roots_contexts[(state >> 33) % BUSTER_ARRAY_LENGTH(check_driver_quiet_roots_contexts)];
            const char* hole = strstr(context, "%s");
            length += snprintf(text + length, capacity - (uint64_t)length, "%.*s%s%s", (int)(hole - context), context, body, hole + 2);
        }
        if (length > 0 && (uint64_t)length < capacity)
        {
            length += snprintf(text + length, capacity - (uint64_t)length, "    return r + (int)u;\n}\n");
        }
    }
    return length > 0 && (uint64_t)length < capacity ? (uint64_t)length : 0;
}

#define BUSTER_TEST_QUIET_ROOTS_UNITS 48u
// The unit of each kind's roots and the quiet ones of them, as it is and behind the filler.
#define BUSTER_TEST_QUIET_ROOTS_UNIT_ROOTS 45u
#define BUSTER_TEST_QUIET_ROOTS_UNIT_QUIET 23u
#define BUSTER_TEST_QUIET_ROOTS_FILLED_ROOTS 45u
#define BUSTER_TEST_QUIET_ROOTS_FILLED_QUIET 23u
// A unit's filler: a table of this many values ahead of it. The reader leaves a statement's discard wrapper out where
// its expression says as much (CheckInput.discard_roots), in a unit of 8,192 tokens or more: behind the filler a
// statement's root is its assignment, its call or its cast to void itself, as most of a large unit's are, and without
// it the wrapper is.
#define BUSTER_TEST_QUIET_ROOTS_FILLER 4200u

// The filler's text into `text`, and its length; 0 where it doesn't fit.
static uint64_t check_test_driver_quiet_roots_filler(char* text, uint64_t capacity)
{
    int length = snprintf(text, capacity, "static const unsigned char quiet_roots_filler[%u] = {", BUSTER_TEST_QUIET_ROOTS_FILLER);
    for (uint32_t i = 0; i < BUSTER_TEST_QUIET_ROOTS_FILLER && length > 0 && (uint64_t)length < capacity; i += 1)
    {
        length += snprintf(text + length, capacity - (uint64_t)length, i % 32 == 0 ? "\n    0," : " 0,");
    }
    if (length > 0 && (uint64_t)length < capacity)
    {
        length += snprintf(text + length, capacity - (uint64_t)length, "\n};\n");
    }
    return length > 0 && (uint64_t)length < capacity ? (uint64_t)length : 0;
}

// The unit of each kind, as it is or behind the filler (`text`, `length` bytes): its counts are frozen, so that a root
// that turns loud or quiet shows, and its diagnostics are clang's.
static void check_test_driver_quiet_roots_unit(CheckTest* test, const char* clang, const char* executable, const char* directory, const char* source,
                                               const char* text, uint64_t length, uint64_t expected_roots, uint64_t expected_quiet)
{
    static const char* const defaults[] = { "-std=gnu17", NULL };
    uint64_t roots = 0;
    uint64_t quiet = 0;
    uint64_t acted = 0;
    char* diagnostics = NULL;
    const bool written = length != 0 && parse_test_write(source, text, length);
    const bool held = written && check_test_driver_quiet_roots_pair(executable, directory, source, defaults, true, &roots, &quiet, &acted, &diagnostics);
    check_test_expect(test, held, "quiet-roots: with the test's switch the unit's object and diagnostics are the same");
    check_test_expect(test, held && roots == expected_roots && quiet == expected_quiet && acted == 0,
                      "quiet-roots: the unit's roots, the quiet ones of them, and none of those whose visit acted");
    // What its loud roots give, each once: the walks', the statements' markers, a call's result, the plans' and -Wunsequenced's.
    static const char* const expected[] =
    {
        "ignoring return value of function declared with pure attribute",
        "ignoring return value of function declared with 'warn_unused_result' attribute",
        "ignoring return value of function declared with const attribute",
        "relational comparison result unused",
        "division by zero is undefined",
        "shift count >= width of type",
        "unsequenced modification and access to 'a'",
        "unsequenced modification and access to 'b'",
        "implicit conversion from 'int' to 'char' changes value from 300 to 44",
        "implicit conversion from 'double' to 'int' changes value from 2.5 to 2",
        "implicit conversion from 'int' to 'char' changes value from 400 to -112",
        "remainder by zero is undefined",
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(expected); i += 1)
    {
        char description[160];
        snprintf(description, sizeof(description), "quiet-roots: the unit's loud roots still warn: %s", expected[i]);
        check_test_expect(test, diagnostics != NULL && strstr(diagnostics, expected[i]) != NULL, description);
    }
    // And clang's own, in its order: the plans' between the roots' and -Wunsequenced's after each root's.
    char* oracle_arguments[] = { (char*)clang, "-std=gnu17", "-fno-caret-diagnostics", "-fno-color-diagnostics", "-fsyntax-only", (char*)source, NULL };
    char* oracle_text = NULL;
    const int oracle_status = written ? check_test_driver_spawn(oracle_arguments, &oracle_text) : -1;
    const bool ordered = oracle_status == 0 && oracle_text != NULL && diagnostics != NULL && strcmp(oracle_text, diagnostics) == 0;
    check_test_expect(test, ordered, "quiet-roots: the unit's diagnostics are clang's, in clang's order");
    if (!held || !ordered || roots != expected_roots || quiet != expected_quiet || acted != 0)
    {
        fprintf(stderr, "test-checker-driver: quiet-roots: the unit: roots %llu, quiet %llu, acted %llu\n%s----\n%s", (unsigned long long)roots,
                (unsigned long long)quiet, (unsigned long long)acted, diagnostics != NULL ? diagnostics : "", oracle_text != NULL ? oracle_text : "");
    }
    free(oracle_text);
    free(diagnostics);
}

static void check_test_driver_quiet_roots(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char* const defaults[] = { "-std=gnu17", NULL };
    static const char* const extra[] = { "-std=gnu17", "-Wall", "-Wextra", "-Wunsequenced", "-Wunused-value", "-Wsign-compare", "-Wshift-overflow", NULL };
    char source[PATH_MAX];
    snprintf(source, sizeof(source), "%s/quiet-roots.c", directory);
    const uint64_t capacity = UINT64_C(1) << 17;
    char* text = malloc(capacity);
    const uint64_t filler = text != NULL ? check_test_driver_quiet_roots_filler(text, capacity) : 0;
    check_test_expect(test, filler != 0 && filler + sizeof(check_driver_quiet_roots_unit) < capacity, "quiet-roots: the filler fits its buffer");
    // The unit of each kind, with the discard wrappers and, behind the filler, without them.
    check_test_driver_quiet_roots_unit(test, clang, executable, directory, source, check_driver_quiet_roots_unit, sizeof(check_driver_quiet_roots_unit) - 1,
                                       BUSTER_TEST_QUIET_ROOTS_UNIT_ROOTS, BUSTER_TEST_QUIET_ROOTS_UNIT_QUIET);
    if (filler != 0 && filler + sizeof(check_driver_quiet_roots_unit) < capacity)
    {
        memcpy(text + filler, check_driver_quiet_roots_unit, sizeof(check_driver_quiet_roots_unit));
        check_test_driver_quiet_roots_unit(test, clang, executable, directory, source, text, filler + sizeof(check_driver_quiet_roots_unit) - 1,
                                           BUSTER_TEST_QUIET_ROOTS_FILLED_ROOTS, BUSTER_TEST_QUIET_ROOTS_FILLED_QUIET);
    }
    // The generated units, every other one behind the filler, with the default warnings as an object and with the
    // walks' other warnings on.
    uint64_t roots = 0;
    uint64_t quiet = 0;
    uint64_t acted = 0;
    uint64_t all_roots = 0;
    uint64_t all_quiet = 0;
    uint64_t all_acted = 0;
    bool all_held = filler != 0;
    for (uint32_t seed = 0; filler != 0 && seed < BUSTER_TEST_QUIET_ROOTS_UNITS; seed += 1)
    {
        const uint64_t prefix = seed % 2 != 0 ? filler : 0;
        const uint64_t length = check_test_driver_quiet_roots_generate(text + prefix, capacity - prefix, seed);
        bool unit_held = length != 0 && parse_test_write(source, text, prefix + length);
        for (uint32_t set = 0; unit_held && set < 2; set += 1)
        {
            unit_held = check_test_driver_quiet_roots_pair(executable, directory, source, set == 0 ? defaults : extra, set == 0, &roots, &quiet, &acted, NULL);
            all_roots += roots;
            all_quiet += quiet;
            all_acted += acted;
        }
        if (!unit_held)
        {
            fprintf(stderr, "test-checker-driver: quiet-roots: generated unit %u differs with the switch\n", seed);
        }
        all_held &= unit_held;
        // (An even unit's text overwrote the filler's start: it is written again for the next odd one.)
        if (seed % 2 == 0 && check_test_driver_quiet_roots_filler(text, capacity) != filler)
        {
            all_held = false;
        }
    }
    check_test_expect(test, all_held, "quiet-roots: each generated unit's object and diagnostics are the same with the test's switch");
    check_test_expect(test, all_acted == 0 && all_quiet != 0 && all_quiet < all_roots,
                      "quiet-roots: the generated units have quiet roots and loud ones, and no quiet root's visit acted");
    if (all_acted != 0 || all_quiet == 0 || all_quiet >= all_roots)
    {
        fprintf(stderr, "test-checker-driver: quiet-roots: generated units: roots %llu, quiet %llu, acted %llu\n", (unsigned long long)all_roots,
                (unsigned long long)all_quiet, (unsigned long long)all_acted);
    }
    free(text);
    unlink(source);
}

// -Wunsequenced's fixtures and the quiet roots' rule above, as one of the driver test's sections: the parts are dealt a
// section at a time, and a section of its own would move every later one to the next part.
static void check_test_driver_unsequenced_sections(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_unsequenced(test, clang, executable, directory);
    check_test_driver_quiet_roots(test, clang, executable, directory);
}
