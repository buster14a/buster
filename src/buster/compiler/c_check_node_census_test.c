// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// measure-node-machine's checks (#2524's steps 0 and 1), on original fixtures. Each fixture's line that ends with
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
    "    x = 1 << 31; // refuse: constant\n"
    "    e = 5; // refuse: constant\n"
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

// A system header's macro in a user's expression (with -isystem on the fixtures' directory's `system`): counted apart,
// not refused.
static const char node_census_test_system_header[] = "#define NOTHING ((void)0)\n#define ZERO(x) ((x) - (x))\n";

static const char node_census_test_system[] =
    "#include <zero.h>\n"
    "int s(int x)\n"
    "{\n"
    "    NOTHING; // take\n"
    "    x = ZERO(x) + 1; // take\n"
    "    return x; // take\n"
    "}\n";

// The same warnings as the first fixture's where they are off by default: read without the options that enable them,
// they refuse nothing.
static const char node_census_test_quiet[] =
    "enum A { A0, A1 };\n"
    "int g(unsigned u, int i)\n"
    "{\n"
    "    int x = 0;\n"
    "    enum A e = A0;\n"
    "    x = u < i; // take\n"
    "    x = x; // take\n"
    "    x = 1 << 31; // take\n"
    "    e = 5; // take\n"
    "    return x + e; // take\n"
    "}\n";

// The shadow's fixture (#2524's step 1): each line that ends with `// machine: taken` has expressions the census takes and
// the machine takes them all, each that ends with `// machine: REFUSAL` one the machine refuses for that reason
// (node_machine_refusal_names). Its taken expressions' records and entries must be the pipeline's, by T's rows and by
// the action alike. Its calls: direct and discarded, nested (whose rows the tasks number in preorder, outer first, where
// the entries are in postorder), variadic (a char and a float promoted), of pointers converted to a parameter's (to
// const char and to void, and null), through a pointer, a member and a call's result; and refused, a long double's
// parameter (the templates') and a pointer whose first parameter is a jmp_buf's (a callee that returns twice).
static const char node_shadow_test_text[] =
    "struct S { long m; long n; int a[4]; unsigned char c; int (*cb)(int); };\n"
    "struct __jmp_buf_tag { long b[8]; };\n"
    "int g;\n"
    "double gd;\n"
    "int f(int);\n"
    "int v(int, ...);\n"
    "void w(const char*, void*);\n"
    "int (*pick(int))(int);\n"
    "int ldf(long double);\n"
    "long h(struct S* p, struct S s, int a, int b, unsigned u, char c, double d, float e, int* q, const int* r, int (*fp)(int, double),\n"
    "       int (*sj)(struct __jmp_buf_tag*))\n"
    "{\n"
    "    int x = a + b * 2; // machine: taken\n"
    "    long y = 0; // machine: taken\n"
    "    unsigned long z = u; // machine: taken\n"
    "    p->n = a + b * 2; // machine: taken\n"
    "    s.m = -a; // machine: taken\n"
    "    x = (int)(~u >> 3); // machine: taken\n"
    "    x = !a; // machine: taken\n"
    "    x = c + 1; // machine: taken\n"
    "    x = p->c >> 1; // machine: taken\n"
    "    d = d * e + 0.5; // machine: taken\n"
    "    e = (float)d; // machine: taken\n"
    "    g = x; // machine: taken\n"
    "    gd = g; // machine: taken\n"
    "    *q = q[1] + p->a[2] + *r; // machine: taken\n"
    "    q = &g; // machine: taken\n"
    "    q = q + a; // machine: taken\n"
    "    y = (long)sizeof(*p) + (long)q; // machine: taken\n"
    "    x = (a < b) == (u >= 3u); // machine: taken\n"
    "    x = (a % 7) | ((b & 1) ^ c); // machine: taken\n"
    "    z = z / 3 + (z >> 1) - (unsigned long)y; // machine: taken\n"
    "    s.n = s.m; // machine: taken\n"
    "    (void)a; // machine: taken\n"
    "    if (a > b) // machine: taken\n"
    "        x = 1; // machine: taken\n"
    "    while (q != 0) // machine: taken\n"
    "        q = 0; // machine: taken\n"
    "    x = f(a); // machine: taken\n"
    "    f(b); // machine: taken\n"
    "    x = f(f(a) + 1) * 2; // machine: taken\n"
    "    x = f(f(a)) + f(b); // machine: taken\n"
    "    x = v(a, c, e, q); // machine: taken\n"
    "    w(\"ab\", q); // machine: taken\n"
    "    w(0, 0); // machine: taken\n"
    "    x = fp(c, e); // machine: taken\n"
    "    x = p->cb(x); // machine: taken\n"
    "    x = pick(a)(b); // machine: taken\n"
    "    x = ldf(d); // machine: template\n"
    "    x = sj(0); // machine: callee\n"
    "    x = a && b; // machine: code\n"
    "    x = a ? b : 1; // machine: code\n"
    "    x++; // machine: code\n"
    "    x += 2; // machine: code\n"
    "    y = q - r; // machine: template\n"
    "    z = z / 4; // machine: template\n"
    "    return y + x + (long)z; // machine: taken\n"
    "}\n";

// The refusal whose name `name` (`length` bytes) is, or NODE_MACHINE_REFUSALS.
static uint32_t node_shadow_test_named(const char* name, uint32_t length)
{
    uint32_t found = NODE_MACHINE_REFUSALS;
    for (uint32_t r = 0; r < NODE_MACHINE_REFUSALS && found == NODE_MACHINE_REFUSALS; r += 1)
    {
        found = strlen(node_machine_refusal_names[r]) == length && memcmp(node_machine_refusal_names[r], name, length) == 0 ? r : found;
    }
    return found;
}

// The shadow's fixture written to `directory` and read with `options`, its T by the rows (and verified against the
// action) or (`scalar`) by the action alone: whether every line held and nothing differed from the pipeline.
static bool node_shadow_test_fixture(const char* directory, bool scalar, char** options, uint32_t option_count)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/shadow.c", directory);
    const char* text = node_shadow_test_text;
    bool valid = parse_test_write(path, text, strlen(text));
    NodeShadow shadow = { .records_out = calloc(512, sizeof(NodeShadowRecord)), .record_capacity = 512, .scalar = scalar, .verify = !scalar, .passes = 1, .repeat = 1 };
    NodeCensus census = { .shadow = &shadow };
    fflush(stdout);
    valid = valid && shadow.records_out != NULL && node_census_file(&census, path, options, option_count) && census.files == 1 && census.failures == 0 &&
            shadow.record_count < shadow.record_capacity;
    const bool same = shadow.value_differences == 0 && shadow.entry_differences == 0 && shadow.twin_differences == 0 && shadow.taken_expressions != 0 &&
                      (scalar || shadow.row_nodes != 0);
    if (!valid || !same)
    {
        fprintf(stderr, "test-checker: node-shadow%s: %s: values %" PRIu64 ", entries %" PRIu64 ", twin %" PRIu64 " differ; %" PRIu64 " expressions taken, %" PRIu64 " row-typed nodes\n",
                scalar ? " --scalar" : "", valid ? "read" : "NOT READ", shadow.value_differences, shadow.entry_differences, shadow.twin_differences, shadow.taken_expressions,
                shadow.row_nodes);
    }
    uint32_t line = 1;
    const char* at = text;
    while (valid && *at != 0)
    {
        const char* end = strchr(at, '\n');
        end = end != NULL ? end : at + strlen(at);
        const char* expectation = NULL;
        for (const char* c = at; c + 2 < end && expectation == NULL; c += 1)
        {
            expectation = strncmp(c, "// machine: ", 12) == 0 ? c + 12 : NULL;
        }
        const uint32_t expected = expectation != NULL ? node_shadow_test_named(expectation, (uint32_t)(end - expectation)) : NODE_MACHINE_REFUSALS;
        uint32_t found = 0;
        uint32_t matched = 0;
        for (uint32_t r = 0; r < shadow.record_count; r += 1)
        {
            found += shadow.records_out[r].line == line;
            matched += shadow.records_out[r].line == line && shadow.records_out[r].refusal == expected;
        }
        const bool held = expectation == NULL || (expected != NODE_MACHINE_REFUSALS && (expected == NODE_MACHINE_TAKEN ? found != 0 && matched == found : matched != 0));
        if (!held)
        {
            fprintf(stderr, "test-checker: node-shadow%s: line %u: expected %.*s, found %u expressions, %u as expected\n", scalar ? " --scalar" : "", line,
                    (int)(expectation != NULL ? end - expectation : 0), expectation != NULL ? expectation : "", found, matched);
        }
        valid = valid && held;
        line += 1;
        at = *end != 0 ? end + 1 : end;
    }
    free(shadow.records_out);
    unlink(path);
    return valid && same;
}

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
// expectation, and its expressions with a system header's macro in `system`. Each line that fails is reported on stderr;
// whether every line held.
static bool node_census_test_fixture(const char* directory, const char* name, const char* text, char** options, uint32_t option_count, uint64_t* system)
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
    *system = census.system_expressions;
    free(census.records);
    unlink(path);
    return valid;
}

static __attribute__((noinline)) void check_test_node_census(CheckTest* test)
{
    char directory[] = "/tmp/buster-node-census-XXXXXX";
    const bool ready = mkdtemp(directory) != 0;
    check_test_expect(test, ready, "node-census fixtures' directory is made");
    char* loud[] = { "-nostdinc", "-Wall", "-Wextra", "-Wshift-sign-overflow", "-Wassign-enum" };
    char* quiet[] = { "-nostdinc" };
    uint64_t system = 0;
    check_test_expect(test, ready && node_census_test_fixture(directory, "warnings.c", node_census_test_warnings, loud, BUSTER_ARRAY_LENGTH(loud), &system),
                      "node-census refuses each expression the compiler warns of, by the test that stands for its warning");
    check_test_expect(test, ready && node_census_test_fixture(directory, "clean.c", node_census_test_clean, loud, BUSTER_ARRAY_LENGTH(loud), &system),
                      "node-census takes the clean idioms its silence tests must not refuse");
    check_test_expect(test, ready && node_census_test_fixture(directory, "set.c", node_census_test_set, loud, BUSTER_ARRAY_LENGTH(loud), &system),
                      "node-census refuses what the machine's set has no code for, by its test");
    check_test_expect(test, ready && node_census_test_fixture(directory, "quiet.c", node_census_test_quiet, quiet, BUSTER_ARRAY_LENGTH(quiet), &system),
                      "node-census refuses nothing for warnings that are off by default");
    char headers[512];
    char header[640];
    snprintf(headers, sizeof(headers), "%s/system", directory);
    snprintf(header, sizeof(header), "%s/zero.h", headers);
    const bool written = ready && mkdir(headers, 0700) == 0 && parse_test_write(header, node_census_test_system_header, strlen(node_census_test_system_header));
    char* isystem[] = { "-nostdinc", "-Wall", "-Wextra", "-isystem", headers };
    system = 0;
    check_test_expect(test, written && node_census_test_fixture(directory, "system.c", node_census_test_system, isystem, BUSTER_ARRAY_LENGTH(isystem), &system) &&
                                system == 2,
                      "node-census takes a system header's macros in a user's expression and counts them apart");
    if (written)
    {
        unlink(header);
        rmdir(headers);
    }
    check_test_expect(test, ready && node_shadow_test_fixture(directory, false, loud, BUSTER_ARRAY_LENGTH(loud)),
                      "node-shadow: the machine takes its codes with the pipeline's values and entries, T's rows the action's, and refuses the rest by reason");
    check_test_expect(test, ready && node_shadow_test_fixture(directory, true, loud, BUSTER_ARRAY_LENGTH(loud)),
                      "node-shadow --scalar: T by the action alone gives the pipeline's values and entries");
    // Every warning has the census's class (node_census_warning_classes): one added since fails here until it has one.
    uint32_t unclassified = 0;
    for (uint32_t id = 0; id < CHECK_WARNING_ID_COUNT; id += 1)
    {
        if (node_census_warning_classes[id] == NODE_CENSUS_WARNING_UNCLASSIFIED)
        {
            unclassified += 1;
            fprintf(stderr, "test-checker: node-census: warning %u (%s) has no class of the census's\n", id,
                    id < BUSTER_ARRAY_LENGTH(check_warning_descriptions) && check_warning_descriptions[id].option != NULL ? check_warning_descriptions[id].option : "-");
        }
    }
    check_test_expect(test, unclassified == 0, "node-census classifies every warning: a test stands for it, or it is no expression's");
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
