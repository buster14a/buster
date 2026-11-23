// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// --node-bodies (#2850): a compile that takes the function bodies the node machine's producer takes before the reader's
// walk (c_check_node_bodies.c) and writes each one's function from its controls and its kept tapes
// (check_lower_taken_body). Its unit's object, diagnostics and status are the ordinary compile's.
//
// The fixture has a body for each thing the lowering's splice maps: a temporary (an update's of an address after its
// postfix), an expression's own labels (`&&`, `||`, a conditional), a condition that is branches and one that is not, a
// loop's and a goto's jumps, calls (direct, through a pointer, variadic, nested, of a void function, discarded), a
// global read, stored and addressed, a static object and a static function named in taken bodies alone, string
// literals, a local whose address is taken, an array's element, a return with no value, and values of every class from
// a byte to a double. Two bodies beside them are read as ever: a `switch`, and an initialization of an object aligned
// past 8 bytes; and a third names a thread-local object, whose entry the lowering writes as a call in
// position-independent code. `floats` has a double's sum of a product, which the lowering fuses for a target with FMA:
// the machine takes no floating sum there, and the body is read as ever.
static const char check_driver_node_bodies_text[] =
    "struct P { int a; long c; char *s; };\n"
    "int g; long gl; static int sg; extern int ext;\n"
    "int h(int); long h2(long, long); int (*fp)(int); void v(void); int var(const char *, ...);\n"
    "static int twice(int x) { return x * 2 + sg; }\n"
    "int post(struct P *p, int *q) { int a = (*q)++; p->a++; return a + p->a--; }\n"
    "char *walk(char *s) { while (*s++) g++; return s; }\n"
    "int logic(int a, int b, int c) { return (a && b) || (c ? a : b); }\n"
    "int cond(int a, int b) { int x = a > b ? a : b; return x; }\n"
    "int branches(int a, int b) { if (a && !b) return 1; else if (a || b) g = 2; while (a < b && g) a++; do { b--; } while (b > 0 && a);\n"
    "  for (int i = 0; i < a; i++) { if (i == 3) continue; if (i == 5) break; g += i; } return g; }\n"
    "int jumps(int n) { again: if (n > 10) goto done; n += 3; goto again; done: return n; }\n"
    "int calls(int a) { v(); h(a); return h(h(a) + 1) + fp(a) + (int)h2(a, 2) + var(\"x%d\", a) + twice(a); }\n"
    "int globals(int a) { g = a; gl += g; sg = g + 1; ext++; int *p = &g; return *p + sg; }\n"
    "const char *strings(int k) { const char *a = \"one\"; return k ? a : \"two\"; }\n"
    "int locals(int a) { int x = a; int *p = &x; int arr[4]; arr[1] = a; *p += arr[1]; return x; }\n"
    "void nothing(int a) { if (a) return; g = 1; }\n"
    "unsigned char narrow(int a) { unsigned char c = (unsigned char)a; c += 2; return c; }\n"
    "double floats(double d, float f, int i) { double r = d * f + i; return r / 2.0; }\n"
    "long widen(int a, unsigned b) { long l = a; l += b; return l << 2; }\n"
    "int chosen(int k) { switch (k) { case 1: return 2; default: return 3; } }\n"
    "int aligned(int c) { int x __attribute__((aligned(16))) = c; return x; }\n"
    "__thread int tls;\nint threaded(int a) { tls += a; return tls; }\n";

// Units that say something, each with the body `beside` that says nothing: with --node-bodies each one's diagnostics
// and status are the ordinary compile's, and where it compiles its object too, with `taken` of its bodies taken. What is
// said is of a stage each: before the walk (a break outside a loop); by the walk (a division by zero, a result not
// used, a deprecated function's call); by the reader's passes after it (a naked function's statement, a packed member's
// address, a type that is available only in the default evaluation method, used in a body the producer takes and
// reported at its typedef); by the common warnings' pass (an empty body); by the program (a function that may not
// return a value, an enumeration's value a switch does not handle); by the initializers (excess elements); at the
// unit's end (a function with internal linkage that is not defined); and by code generation's check (a vector
// parameter wider than the target's registers, reported at the parameter, its body taken). The last has an
// always_inline wrapper, which says nothing: the lowering expands its call from the wrapper's own body, and neither
// that body nor its caller's is taken.
static const struct
{
    const char* name;
    const char* text;
    const char* option;
    int taken;
} check_driver_node_bodies_units[] = {
    { "said", "void f(void) { break; }\nint beside(int a) { return a + 2; }\n", NULL, -1 },
    { "zero", "int f(int a) { return a / 0; }\nint beside(int a) { return a + 2; }\n", NULL, 1 },
    { "result", "__attribute__((warn_unused_result)) int must(void);\nvoid f(void) { must(); }\nint beside(int a) { return a + 2; }\n", NULL, 1 },
    { "deprecated", "__attribute__((deprecated)) void old(void);\nvoid f(void) { old(); }\nint beside(int a) { return a + 2; }\n", NULL, 1 },
    { "naked", "int g;\n__attribute__((naked)) void f(void) { g = 1; }\nint beside(int a) { return a + 2; }\n", NULL, -1 },
    { "packed", "struct __attribute__((packed)) P { char c; int i; } gpk;\nint *f(void) { return &gpk.i; }\nint beside(int a) { return a + 2; }\n", NULL, 1 },
    { "eval", "typedef float T __attribute__((available_only_in_default_eval_method));\n#pragma float_control(push)\n#pragma clang fp eval_method(double)\n"
              "#pragma float_control(pop)\nvoid inner(void) { T value; (void)value; }\n", "-ffp-eval-method=source", -1 },
    { "empty", "int g;\nvoid f(int a) { if (a); g = 1; }\nint beside(int a) { return a + 2; }\n", NULL, 1 },
    { "flow", "int f(int a) { if (a) return 1; }\nint beside(int a) { return a + 2; }\n", NULL, 1 },
    { "switched", "enum E { A, B };\nint f(enum E e) { switch (e) { case A: return 1; } return 0; }\nint beside(int a) { return a + 2; }\n", NULL, 1 },
    { "initialized", "void f(void) { int a[1] = { 1, 2 }; (void)a; }\nint beside(int a) { return a + 2; }\n", NULL, 1 },
    { "undefined", "static int und(void);\nint f(void) { return und(); }\nint beside(int a) { return a + 2; }\n", NULL, 1 },
    { "vector", "typedef float V8 __attribute__((vector_size(32)));\nint g;\nvoid f(V8 v) { g = 1; }\nint beside(int a) { return a + 2; }\n", NULL, 2 },
    { "inlined", "__attribute__((always_inline)) inline int wrap(int a) { return a + 1; }\nint f(int a) { return wrap(a); }\nint beside(int a) { return a + 2; }\n", NULL, 1 },
};

// Whether the files at `left` and `right` hold the same bytes, and some.
static bool check_test_driver_node_bodies_same(const char* left, const char* right)
{
    FILE* a = fopen(left, "rb");
    FILE* b = fopen(right, "rb");
    bool same = a != NULL && b != NULL;
    uint64_t total = 0;
    while (same)
    {
        char first[4096];
        char second[4096];
        const size_t read = fread(first, 1, sizeof(first), a);
        same = fread(second, 1, sizeof(second), b) == read && memcmp(first, second, read) == 0;
        total += read;
        if (read < sizeof(first))
        {
            break;
        }
    }
    if (a != NULL)
    {
        fclose(a);
    }
    if (b != NULL)
    {
        fclose(b);
    }
    return same && total != 0;
}

// The unit at `source` compiled to `object` on one lane with `options` (at most 4) before it: its status, and its
// diagnostics' text into `text`.
static int check_test_driver_node_bodies_compile(const char* executable, const char* source, const char* object, const char* const* options, uint32_t option_count, char** text)
{
    char* arguments[16] = { 0 };
    uint32_t count = 0;
    arguments[count++] = (char*)(executable != NULL ? executable : "checker");
    arguments[count++] = "--lanes";
    arguments[count++] = "1";
    for (uint32_t i = 0; i < option_count && i < 4; i += 1)
    {
        arguments[count++] = (char*)options[i];
    }
    arguments[count++] = "-std=gnu17";
    arguments[count++] = "-c";
    arguments[count++] = "-o";
    arguments[count++] = (char*)object;
    arguments[count++] = (char*)source;
    return check_test_driver_buster_command(arguments, text, false, executable != NULL);
}

static void check_test_driver_node_bodies(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    (void)clang;
    char source[512];
    char plain[512];
    char taken[512];
    snprintf(source, sizeof(source), "%s/node-bodies.c", directory);
    snprintf(plain, sizeof(plain), "%s/node-bodies-plain.o", directory);
    snprintf(taken, sizeof(taken), "%s/node-bodies-taken.o", directory);
    const bool written = parse_test_write(source, check_driver_node_bodies_text, sizeof(check_driver_node_bodies_text) - 1);
    // The test's switch is the environment's. What it held, a run of every test with the option forced among them, is
    // taken off for the compiles here, which give the option or a selection themselves, and put back at the end.
    const char* outer = getenv("BUSTER_TEST_NODE_BODIES");
    char* restored = outer != NULL ? strdup(outer) : NULL;
    unsetenv("BUSTER_TEST_NODE_BODIES");
    char* plain_text = NULL;
    const int plain_status = written ? check_test_driver_node_bodies_compile(executable, source, plain, NULL, 0, &plain_text) : -1;
    check_test_expect(test, written && plain_status == 0 && plain_text != NULL, "node-bodies: the fixture compiles without the option");
    // With the option: the same object, diagnostics and status.
    static const char* const on[] = { "--node-bodies" };
    char* taken_text = NULL;
    const int taken_status = plain_status == 0 ? check_test_driver_node_bodies_compile(executable, source, taken, on, 1, &taken_text) : -1;
    check_test_expect(test, taken_status == 0 && taken_text != NULL && plain_text != NULL && strcmp(plain_text, taken_text) == 0 && check_test_driver_node_bodies_same(plain, taken),
                      "node-bodies: with --node-bodies the object, the diagnostics and the status are the ordinary compile's");
    free(taken_text);
    // What it took: every body but the two it reads as ever, whose expressions' nodes are the unit's only ones.
    static const char* const timed[] = { "--node-bodies", "--time-stages" };
    char* timed_text = NULL;
    const int timed_status = plain_status == 0 ? check_test_driver_node_bodies_compile(executable, source, taken, timed, 2, &timed_text) : -1;
    const char* line = timed_text != NULL ? strstr(timed_text, "node-bodies: bodies=") : NULL;
    unsigned bodies = 0;
    unsigned took = 0;
    unsigned long long tokens = 0;
    unsigned nodes = 0;
    const bool counted = line != NULL && sscanf(line, "node-bodies: bodies=%u taken=%u tokens=%llu graph-nodes=%u", &bodies, &took, &tokens, &nodes) == 4;
    check_test_expect(test, timed_status == 0 && counted && bodies == 18 && took == 15 && nodes != 0 && nodes < 48 && check_test_driver_node_bodies_same(plain, taken),
                      "node-bodies: fifteen of the fixture's eighteen bodies are taken, and the walk makes nodes of the other three alone");
    if (timed_status == 0 && !(counted && bodies == 18 && took == 15 && nodes != 0 && nodes < 48))
    {
        fprintf(stderr, "test-checker-driver: node-bodies: bodies %u, taken %u, tokens %llu, graph nodes %u\n", bodies, took, tokens, nodes);
    }
    free(timed_text);
    // Some of the producer's bodies taken and the others read as ever, by a test's selection: each third of them.
    for (uint32_t residue = 0; residue < 3 && plain_status == 0; residue += 1)
    {
        char selection[16];
        snprintf(selection, sizeof(selection), "3:%u", residue);
        setenv("BUSTER_TEST_NODE_BODIES", selection, 1);
        char* selected_text = NULL;
        const int selected_status = check_test_driver_node_bodies_compile(executable, source, taken, NULL, 0, &selected_text);
        unsetenv("BUSTER_TEST_NODE_BODIES");
        check_test_expect(test, selected_status == 0 && selected_text != NULL && plain_text != NULL && strcmp(plain_text, selected_text) == 0 &&
                                check_test_driver_node_bodies_same(plain, taken),
                          "node-bodies: with a third of the producer's bodies taken the object is the ordinary compile's");
        free(selected_text);
    }
    // And as position-independent code, where a thread-local's entry is a call of its accessor.
    static const char* const pic[] = { "-fPIC" };
    static const char* const pic_on[] = { "-fPIC", "--node-bodies" };
    char* pic_text = NULL;
    char* pic_taken_text = NULL;
    const int pic_status = plain_status == 0 ? check_test_driver_node_bodies_compile(executable, source, plain, pic, 1, &pic_text) : -1;
    const int pic_taken_status = pic_status == 0 ? check_test_driver_node_bodies_compile(executable, source, taken, pic_on, 2, &pic_taken_text) : -1;
    check_test_expect(test, pic_status == 0 && pic_taken_status == 0 && pic_text != NULL && pic_taken_text != NULL && strcmp(pic_text, pic_taken_text) == 0 &&
                            check_test_driver_node_bodies_same(plain, taken),
                      "node-bodies: as position-independent code the object is the ordinary compile's");
    free(pic_text);
    free(pic_taken_text);
    // And for a target that fuses a product and a sum, where the machine takes no floating sum (NodeMachine.fuses): the
    // body with one is read as ever, and its function is the lowering's fused one.
    static const char* const fused[] = { "-mfma" };
    static const char* const fused_on[] = { "-mfma", "--node-bodies" };
    static const char* const fused_timed[] = { "-mfma", "--node-bodies", "--time-stages" };
    char* fused_text = NULL;
    char* fused_taken_text = NULL;
    char* fused_timed_text = NULL;
    const int fused_status = plain_status == 0 ? check_test_driver_node_bodies_compile(executable, source, plain, fused, 1, &fused_text) : -1;
    const int fused_taken_status = fused_status == 0 ? check_test_driver_node_bodies_compile(executable, source, taken, fused_on, 2, &fused_taken_text) : -1;
    const bool fused_same = fused_taken_status == 0 && fused_text != NULL && fused_taken_text != NULL && strcmp(fused_text, fused_taken_text) == 0 &&
                            check_test_driver_node_bodies_same(plain, taken);
    const int fused_timed_status = fused_status == 0 ? check_test_driver_node_bodies_compile(executable, source, taken, fused_timed, 3, &fused_timed_text) : -1;
    const char* fused_line = fused_timed_text != NULL ? strstr(fused_timed_text, "node-bodies: bodies=") : NULL;
    unsigned fused_bodies = 0;
    unsigned fused_took = 0;
    const bool fused_counted = fused_line != NULL && sscanf(fused_line, "node-bodies: bodies=%u taken=%u", &fused_bodies, &fused_took) == 2;
    check_test_expect(test, fused_same && fused_timed_status == 0 && fused_counted && fused_bodies == 18 && fused_took == 14 && check_test_driver_node_bodies_same(plain, taken),
                      "node-bodies: for a target that fuses a product and a sum the body with a floating sum is left, and the object is the ordinary compile's");
    if (fused_timed_status == 0 && !(fused_counted && fused_bodies == 18 && fused_took == 14))
    {
        fprintf(stderr, "test-checker-driver: node-bodies: -mfma: bodies %u, taken %u\n", fused_bodies, fused_took);
    }
    free(fused_text);
    free(fused_taken_text);
    free(fused_timed_text);
    // The units that say something: as loud with the option as without.
    for (uint32_t u = 0; u < BUSTER_ARRAY_LENGTH(check_driver_node_bodies_units) && written; u += 1)
    {
        const char* text = check_driver_node_bodies_units[u].text;
        const char* option = check_driver_node_bodies_units[u].option;
        const int expected = check_driver_node_bodies_units[u].taken;
        const char* unit_options[3] = { 0 };
        uint32_t unit_option_count = 0;
        if (option != NULL)
        {
            unit_options[unit_option_count++] = option;
        }
        char* unit_plain_text = NULL;
        char* unit_taken_text = NULL;
        char* unit_timed_text = NULL;
        unlink(plain);
        unlink(taken);
        const bool unit_written = parse_test_write(source, text, strlen(text));
        const int unit_plain_status = unit_written ? check_test_driver_node_bodies_compile(executable, source, plain, unit_options, unit_option_count, &unit_plain_text) : -1;
        unit_options[unit_option_count++] = "--node-bodies";
        const int unit_taken_status = unit_written ? check_test_driver_node_bodies_compile(executable, source, taken, unit_options, unit_option_count, &unit_taken_text) : -2;
        bool same = unit_written && unit_plain_status == unit_taken_status && unit_plain_text != NULL && unit_taken_text != NULL && strcmp(unit_plain_text, unit_taken_text) == 0 &&
                    (unit_plain_status != 0 || check_test_driver_node_bodies_same(plain, taken)) && (expected < 0) == (unit_plain_status != 0);
        unsigned unit_bodies = 0;
        unsigned unit_took = 0;
        if (same && expected >= 0)
        {
            unit_options[unit_option_count++] = "--time-stages";
            const int unit_timed_status = check_test_driver_node_bodies_compile(executable, source, taken, unit_options, unit_option_count, &unit_timed_text);
            const char* unit_line = unit_timed_text != NULL ? strstr(unit_timed_text, "node-bodies: bodies=") : NULL;
            same = unit_timed_status == 0 && unit_line != NULL && sscanf(unit_line, "node-bodies: bodies=%u taken=%u", &unit_bodies, &unit_took) == 2 &&
                   unit_took == (unsigned)expected && check_test_driver_node_bodies_same(plain, taken);
        }
        check_test_expect(test, same, "node-bodies: a unit that says something says the same with --node-bodies, and compiles to the same object");
        if (!same)
        {
            fprintf(stderr, "test-checker-driver: node-bodies: unit %s: status %d and %d with the option, bodies %u, taken %u (%d expected)\n%s----\n%s",
                    check_driver_node_bodies_units[u].name, unit_plain_status, unit_taken_status, unit_bodies, unit_took, expected,
                    unit_plain_text != NULL ? unit_plain_text : "", unit_taken_text != NULL ? unit_taken_text : "");
        }
        free(unit_plain_text);
        free(unit_taken_text);
        free(unit_timed_text);
    }
    if (restored != NULL)
    {
        setenv("BUSTER_TEST_NODE_BODIES", restored, 1);
    }
    free(restored);
    free(plain_text);
    unlink(source);
    unlink(plain);
    unlink(taken);
}
