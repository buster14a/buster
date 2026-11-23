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
