// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// measure-node-machine's checks (#2524's step 0), on original fixtures. Each fixture's line that ends with
// `// refuse: TEST` must have an expression the census refuses by that test, and each that ends with `// take` an
// expression the census takes and none it refuses. The first fixture gives one warning a line, which the compiler reports
// with -Wall -Wextra (its silence tests must refuse what warns); the second is clean idioms the tests once refused (they
// must take them); the third the set's refusals; and with neither -Wall nor -Wextra, the warnings that are off by default
// refuse nothing. Include after c_check_node_census.c; test-checker calls this through its forward declaration.
#ifndef BUSTER_C_CHECK_NODE_CENSUS_TEST_INCLUDED
#define BUSTER_C_CHECK_NODE_CENSUS_TEST_INCLUDED

static const char node_census_test_warnings[] =
    "enum A { A0, A1 };\n"
    "enum B { B0, B1 };\n"
    "struct P { char c; int i; } __attribute__((packed));\n"
    "__attribute__((warn_unused_result)) int wur(void);\n"
    "__attribute__((deprecated)) int old(int);\n"
    "int g(int* p, unsigned u, int i, enum A ea, enum B eb, char* s, void* v, int* q, struct P* pp)\n"
    "{\n"
    "    int x = 0;\n"
    "    int arr[4];\n"
    "    enum A e = A0;\n"
    "    x = i << 40; // refuse: constant\n"
    "    x = i / 0; // refuse: constant\n"
    "    x = -1 << 2; // refuse: constant\n"
    "    if (x = i) x = 1; // refuse: parentheses\n"
    "    x = (x & 2 == 2); // refuse: parentheses\n"
    "    x = !x == 3; // refuse: parentheses\n"
    "    x = i << x + 1; // refuse: parentheses\n"
    "    if (i << 3) x = 2; // refuse: parentheses\n"
    "    x == 3; // refuse: unused-value\n"
    "    i + 1; // refuse: unused-value\n"
    "    wur(); // refuse: unused-value\n"
    "    x = (i, 1); // refuse: unused-value\n"
    "    x = x++ + x; // refuse: unsequenced\n"
    "    x = ea == eb; // refuse: enum-mix\n"
    "    e = eb; // refuse: enum-mix\n"
    "    x = (char)p; // refuse: pointer-cast-or-void-dereference\n"
    "    s = \"abc\" + i; // refuse: string-plus-int\n"
    "    x = sizeof(x++); // refuse: unevaluated-operand\n"
    "    unsigned char c = 300; // refuse: constant\n"
    "    x = arr[5]; // refuse: constant\n"
    "    x = *(int*)0; // refuse: constant\n"
    "    x = 3.7; // refuse: constant\n"
    "    q = (0 + 0); // refuse: constant\n"
    "    x = (p == 1); // refuse: action-warning\n"
    "    q = i ? p : 1; // refuse: action-warning\n"
    "    x = u < i; // refuse: sign-compare\n"
    "    x = x; // refuse: self-assign\n"
    "    *v; // refuse: pointer-cast-or-void-dereference\n"
    "    q = &pp->i; // refuse: packed-member-address\n"
    "    x = \"a\\q\"[0]; // refuse: warning-event\n"
    "    x = old(1); // refuse: declaration-flag\n"
    "    return x + c + e; // take\n"
    "}\n"
    "int* h(void)\n"
    "{\n"
    "    int local = 0;\n"
    "    return &local; // refuse: return-address\n"
    "}\n";

static const char node_census_test_clean[] =
    "typedef unsigned long size_t;\n"
    "void* memset(void*, int, size_t);\n"
    "struct S { int a; int b; };\n"
    "int h(int* p, size_t l, unsigned char* z, struct S* sp, int i)\n"
    "{\n"
    "    int x = 0; // take\n"
    "    int arr[4];\n"
    "    unsigned char uc = 255; // take\n"
    "    unsigned char ud = -1; // take\n"
    "    int big = 0x80000000u; // take\n"
    "    int* e = &arr[4]; // take\n"
    "    double d = 86400000; // take\n"
    "    memset(arr, 0, sizeof(arr)); // take\n"
    "    (void)i; // take\n"
    "    x = x + 1; // take\n"
    "    sp->a = sp->b = 0; // take\n"
    "    x = i < 3 ? (i = 1) : (int)(long)&i; // take\n"
    "    x = l == (size_t)-1; // take\n"
    "    x = z[0] >= 128U; // take\n"
    "    x = l > 0; // take\n"
    "    x += sizeof(arr) / sizeof(arr[0]); // take\n"
    "    if ((x = i)) x = 2; // take\n"
    "    p[x] = *e; // take\n"
    "    return x + uc + ud + big + (int)d; // take\n"
    "}\n";

static const char node_census_test_set[] =
    "struct B { unsigned f : 3; };\n"
    "struct S { int a; int b; };\n"
    "int printf(const char*, ...);\n"
    "unsigned long strlen(const char*);\n"
    "int k(struct B* b, struct S* s, struct S* t, int i, ...)\n"
    "{\n"
    "    register int r = i;\n"
    "    int x = 0;\n"
    "    int a2[2] = { 1, 2 }; // refuse: control\n"
    "    _Complex double z = 0; // refuse: complex-bitint-atomic-vector-half\n"
    "    __builtin_va_list ap;\n"
    "    b->f = 1; // refuse: bit-field\n"
    "    *s = *t; // refuse: aggregate-value\n"
    "    x = r + 1; // refuse: declaration-flag\n"
    "    x = ((struct S){ 1, 2 }).a; // refuse: compound-literal\n"
    "    printf(\"%d\", x); // refuse: callee\n"
    "    x = (int)strlen(\"ab\"); // refuse: callee\n"
    "    __builtin_va_start(ap, i); // refuse: operator\n"
    "    __builtin_va_end(ap); // refuse: operator\n"
    "    z = z * 2; // refuse: complex-bitint-atomic-vector-half\n"
    "    x = ({ int y = 1; y; }); // refuse: operator\n"
    "    return x + a2[0];\n"
    "}\n"
    "int fenv(double d)\n"
    "{\n"
    "#pragma STDC FENV_ACCESS ON\n"
    "    d = d * 2; // refuse: region\n"
    "    return (int)d; // refuse: region\n"
    "}\n";

// The same warnings as the first fixture's where they are off by default: read with neither -Wall nor -Wextra, they
// refuse nothing.
static const char node_census_test_quiet[] =
    "int g(unsigned u, int i)\n"
    "{\n"
    "    int x = 0;\n"
    "    x = u < i; // take\n"
    "    x = x; // take\n"
    "    return x; // take\n"
    "}\n";

// The test whose name `name` (`length` bytes) is, or NODE_CENSUS_TESTS.
static uint32_t node_census_test_named(const char* name, uint32_t length)
{
    uint32_t found = NODE_CENSUS_TESTS;
    for (uint32_t t = 0; t < NODE_CENSUS_TESTS && found == NODE_CENSUS_TESTS; t += 1)
    {
        found = strlen(node_census_test_names[t]) == length && memcmp(node_census_test_names[t], name, length) == 0 ? t : found;
    }
    return found;
}

// Fixture `text`, written to `directory` as `name` and read with `options`: the census's records against each line's
// expectation. Each line that fails is reported on stderr; whether every line held.
static bool node_census_test_fixture(const char* directory, const char* name, const char* text, char** options, uint32_t option_count)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", directory, name);
    bool valid = parse_test_write(path, text, strlen(text));
    NodeCensus census = { .records = calloc(512, sizeof(NodeCensusRecord)), .record_capacity = 512 };
    fflush(stdout);
    valid = valid && census.records != NULL && node_census_file(&census, path, options, option_count) && census.files == 1 && census.failures == 0 &&
            census.record_count < census.record_capacity;
    if (!valid)
    {
        fprintf(stderr, "test-checker: node-census: %s wasn't counted\n", name);
    }
    uint32_t line = 1;
    const char* at = text;
    while (valid && *at != 0)
    {
        const char* end = strchr(at, '\n');
        end = end != NULL ? end : at + strlen(at);
        const char* refuse = NULL;
        const char* take = NULL;
        for (const char* c = at; c + 2 < end && refuse == NULL && take == NULL; c += 1)
        {
            refuse = strncmp(c, "// refuse: ", 11) == 0 ? c + 11 : NULL;
            take = strncmp(c, "// take", 7) == 0 ? c : NULL;
        }
        const uint32_t expected = refuse != NULL ? node_census_test_named(refuse, (uint32_t)(end - refuse)) : NODE_CENSUS_TESTS;
        uint32_t found = 0;
        uint32_t refused = 0;
        bool matched = false;
        for (uint32_t r = 0; r < census.record_count; r += 1)
        {
            const NodeCensusRecord* record = census.records + r;
            found += record->line == line;
            refused += record->line == line && record->tests != 0;
            matched |= record->line == line && expected != NODE_CENSUS_TESTS && (record->tests >> expected & 1) != 0;
        }
        const bool held = refuse != NULL ? matched : take != NULL ? found != 0 && refused == 0 : true;
        if (!held)
        {
            fprintf(stderr, "test-checker: node-census: %s line %u: expected %s, found %u expressions, %u refused\n", name, line,
                    refuse != NULL ? "a refusal by its test" : "every expression taken", found, refused);
        }
        valid = valid && (refuse == NULL || expected != NODE_CENSUS_TESTS);
        valid = valid && held;
        line += 1;
        at = *end != 0 ? end + 1 : end;
    }
    free(census.records);
    unlink(path);
    return valid;
}

static __attribute__((noinline)) void check_test_node_census(CheckTest* test)
{
    char directory[] = "/tmp/buster-node-census-XXXXXX";
    const bool ready = mkdtemp(directory) != 0;
    check_test_expect(test, ready, "node-census fixtures' directory is made");
    char* loud[] = { "-nostdinc", "-Wall", "-Wextra" };
    char* quiet[] = { "-nostdinc" };
    check_test_expect(test, ready && node_census_test_fixture(directory, "warnings.c", node_census_test_warnings, loud, BUSTER_ARRAY_LENGTH(loud)),
                      "node-census refuses each expression the compiler warns of, by the test that stands for its warning");
    check_test_expect(test, ready && node_census_test_fixture(directory, "clean.c", node_census_test_clean, loud, BUSTER_ARRAY_LENGTH(loud)),
                      "node-census takes the clean idioms its silence tests must not refuse");
    check_test_expect(test, ready && node_census_test_fixture(directory, "set.c", node_census_test_set, loud, BUSTER_ARRAY_LENGTH(loud)),
                      "node-census refuses what the machine's set has no code for, by its test");
    check_test_expect(test, ready && node_census_test_fixture(directory, "quiet.c", node_census_test_quiet, quiet, BUSTER_ARRAY_LENGTH(quiet)),
                      "node-census refuses nothing for warnings that are off by default");
    // The command refuses a run without a file.
    char* missing[] = { "--list", "1" };
    fflush(stdout);
    const int saved = dup(STDERR_FILENO);
    const int null = open("/dev/null", O_WRONLY);
    const bool silenced = saved >= 0 && null >= 0 && dup2(null, STDERR_FILENO) >= 0;
    const int empty = silenced ? run_node_census(BUSTER_ARRAY_LENGTH(missing), missing) : 0;
    if (saved >= 0)
    {
        dup2(saved, STDERR_FILENO);
        close(saved);
    }
    if (null >= 0)
    {
        close(null);
    }
    node_census_list = 0;
    check_test_expect(test, silenced && empty == 1, "measure-node-machine refuses a run without a file");
    if (ready)
    {
        rmdir(directory);
    }
}

#endif
