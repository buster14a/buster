// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original #1382 C-source regressions. Include after c_check_tier1_driver_test.c. The ordinary driver command runs
// the semantic fixtures against Clang on both scalar paths and with rematerialization on/off. A separate decoded
// object check proves the C lowering/promotion/if-conversion/selection chain reaches cmov for pure max/clamp and
// conditional arithmetic, while the guarding branches of potentially faulting loads and division remain. #1382's
// kernels follow: a nested clamp, `&&` and `||` values, and loops whose arms reload the element the head compared
// (a conditional sum, a select of it or its negation, a running max), whose only conditional branch is the loop's,
// and a reload after a call, which keeps its branch.

static const char check_test_tier1_if_convert_header[] =
    "long if_max(long a, long b);\n"
    "unsigned long if_umax(unsigned long a, unsigned long b);\n"
    "long if_clamp(long x, long lo, long hi);\n"
    "unsigned long if_add(unsigned long sum, unsigned long x, int take);\n"
    "unsigned long if_both(unsigned long a, unsigned long b, int take);\n"
    "int if_bool(int a, int b);\n"
    "unsigned long if_sum(const unsigned long* p, unsigned n, unsigned long mask);\n"
    "unsigned long if_load(int take, const unsigned long* p);\n"
    "unsigned long if_div(int take, unsigned long a, unsigned long b);\n"
    "unsigned long if_shift(int take, unsigned long a, unsigned b);\n"
    "unsigned long if_effects(int take, volatile unsigned long* p, unsigned long x);\n"
    "unsigned long if_call(int take, unsigned long x);\n"
    "unsigned long other_effect(unsigned long x);\n"
    "long if_nested(long x);\n"
    "_Bool if_and(int a, int b);\n"
    "int if_andor(int a, int b, int c);\n"
    "long if_condsum(const int* a, int n);\n"
    "long if_abssum(const int* a, int n);\n"
    "int if_runmax(const int* a, int n);\n"
    "unsigned long if_reload_call(const unsigned long* p);\n";

static const char check_test_tier1_if_convert_source[] =
    "#include \"tier1-if-convert.h\"\n"
    "long if_max(long a, long b) { return a > b ? a : b; }\n"
    "unsigned long if_umax(unsigned long a, unsigned long b) { return a > b ? a : b; }\n"
    "long if_clamp(long x, long lo, long hi) { long y = x < lo ? lo : x; return y > hi ? hi : y; }\n"
    "unsigned long if_add(unsigned long sum, unsigned long x, int take) { if (take) sum += x; return sum; }\n"
    "unsigned long if_both(unsigned long a, unsigned long b, int take) { return take ? a + b : a - b; }\n"
    "int if_bool(int a, int b) { return (a < 7 && b > 9) || (a == 11 && b == 13); }\n"
    "unsigned long if_sum(const unsigned long* p, unsigned n, unsigned long mask) { unsigned long sum = 0; for (unsigned i = 0; i < n; i += 1) { unsigned long x = p[i]; if (x & mask) sum += x; } return sum; }\n"
    "unsigned long if_load(int take, const unsigned long* p) { return take ? *p : 9; }\n"
    "unsigned long if_div(int take, unsigned long a, unsigned long b) { return take ? a / b : 11; }\n"
    "unsigned long if_shift(int take, unsigned long a, unsigned b) { return take ? a << b : 13; }\n"
    "unsigned long if_effects(int take, volatile unsigned long* p, unsigned long x) { if (take) { *p = x; return *p; } return 17; }\n"
    "unsigned long if_call(int take, unsigned long x) { return take ? other_effect(x) : 19; }\n"
    "long if_nested(long x) { return x < 0 ? 0 : x > 255 ? 255 : x; }\n"
    "_Bool if_and(int a, int b) { return a < 128 && b < 128; }\n"
    "int if_andor(int a, int b, int c) { return (a < b) && (b < c || c == 0); }\n"
    "long if_condsum(const int* a, int n) { long s = 0; for (int i = 0; i < n; i++) if (a[i] > 128) s += a[i]; return s; }\n"
    "long if_abssum(const int* a, int n) { long s = 0; for (int i = 0; i < n; i++) s += a[i] > 128 ? a[i] : -a[i]; return s; }\n"
    "int if_runmax(const int* a, int n) { int m = 0; for (int i = 0; i < n; i++) m = a[i] > m ? a[i] : m; return m; }\n"
    "unsigned long if_reload_call(const unsigned long* p) { unsigned long v = *p; unsigned long w = other_effect(v); return v > 3 ? *p + w : w; }\n";

static const char check_test_tier1_if_convert_other[] =
    "#include <stdio.h>\n"
    "#include \"tier1-if-convert.h\"\n"
    "static unsigned long calls;\n"
    "unsigned long other_effect(unsigned long x) { calls += 1; return x * 3 + calls; }\n"
    "int main(void)\n"
    "{\n"
    "    const long edges[9] = { (-9223372036854775807L - 1L), -9223372036854775807L, -10, -1, 0, 1, 10, 9223372036854775806L, 9223372036854775807L };\n"
    "    unsigned long h = 0;\n"
    "    for (unsigned i = 0; i < 9; i += 1) for (unsigned j = 0; j < 9; j += 1)\n"
    "    {\n"
    "        h = h * 31 + (unsigned long)if_max(edges[i], edges[j]);\n"
    "        h = h * 31 + if_umax((unsigned long)edges[i], (unsigned long)edges[j]);\n"
    "        h = h * 31 + (unsigned long)if_clamp(edges[i], -7, 11);\n"
    "        h = h * 31 + if_add((unsigned long)edges[i], (unsigned long)edges[j], (int)(i & 1));\n"
    "        h = h * 31 + if_both((unsigned long)edges[i], (unsigned long)edges[j], (int)(j & 1));\n"
    "    }\n"
    "    for (int a = -3; a < 16; a += 1) for (int b = -3; b < 16; b += 1) h = h * 31 + (unsigned)if_bool(a, b);\n"
    "    unsigned long data[97];\n"
    "    unsigned long state = 1382;\n"
    "    for (unsigned i = 0; i < 97; i += 1) { state = state * 6364136223846793005UL + 1; data[i] = state; }\n"
    "    for (unsigned n = 0; n < 98; n += 1) h = h * 31 + if_sum(data, n, 0x8000000000000011UL);\n"
    "    volatile unsigned long observed = 23;\n"
    "    for (unsigned i = 0; i < 97; i += 1)\n"
    "    {\n"
    "        const int take = (int)(i & 1);\n"
    "        h = h * 31 + if_load(take, take ? &data[i] : (const unsigned long*)0);\n"
    "        h = h * 31 + if_div(take, data[i], take ? (data[i] | 1) : 0);\n"
    "        h = h * 31 + if_shift(take, data[i], take ? i % 64 : 64);\n"
    "        h = h * 31 + if_effects(take, take ? &observed : (volatile unsigned long*)0, data[i]);\n"
    "        h = h * 31 + if_call(take, data[i]);\n"
    "    }\n"
    "    for (long x = -300; x < 600; x += 7) h = h * 31 + (unsigned long)if_nested(x);\n"
    "    h = h * 31 + (unsigned long)if_nested(edges[0]) + (unsigned long)if_nested(edges[8]);\n"
    "    for (int a = 120; a < 136; a += 1) for (int b = 120; b < 136; b += 1) h = h * 31 + (unsigned)if_and(a, b);\n"
    "    for (int a = -2; a < 3; a += 1) for (int b = -2; b < 3; b += 1) for (int c = -2; c < 3; c += 1) h = h * 31 + (unsigned)if_andor(a, b, c);\n"
    "    int elements[64];\n"
    "    for (unsigned i = 0; i < 64; i += 1) { state = state * 6364136223846793005UL + 1; elements[i] = (int)((state >> 33) % 300) - 20; }\n"
    "    for (int n = 0; n <= 64; n += 1) h = h * 31 + (unsigned long)if_condsum(elements, n) + (unsigned long)if_abssum(elements, n) * 7 + (unsigned)if_runmax(elements, n);\n"
    "    for (unsigned i = 0; i < 97; i += 1) h = h * 31 + if_reload_call(&data[i]) + (data[i] & 3);\n"
    "    printf(\"%lu %lu %lu\\n\", h, calls, observed);\n"
    "    return 0;\n"
    "}\n";

typedef struct CheckTestIfConvertShape CheckTestIfConvertShape;
struct CheckTestIfConvertShape
{
    const char* name;
    uint32_t selects;
    uint32_t branches;
};

static void check_test_driver_tier1_if_convert(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_tier1_units(test, clang, executable, directory, "tier1-if-convert", "safe conditional values'", check_test_tier1_if_convert_header,
                                  check_test_tier1_if_convert_source, check_test_tier1_if_convert_other, NULL, true, 0);
    // Keep the code-shape oracle independent of textual disassembly and of the selector's IR rule numbers.
    char header[512];
    char source[512];
    snprintf(header, sizeof(header), "%s/tier1-if-convert.h", directory);
    snprintf(source, sizeof(source), "%s/tier1-if-convert.c", directory);
    const bool written = parse_test_write(header, check_test_tier1_if_convert_header, sizeof(check_test_tier1_if_convert_header) - 1) &&
                         parse_test_write(source, check_test_tier1_if_convert_source, sizeof(check_test_tier1_if_convert_source) - 1);
    enum
    {
        CHECK_TEST_IF_CONVERT_RECORDS = 512,
    };
    X86Decoded* records = calloc(CHECK_TEST_IF_CONVERT_RECORDS, sizeof(X86Decoded));
    uint32_t* offsets = calloc(CHECK_TEST_IF_CONVERT_RECORDS, sizeof(uint32_t));
    uint32_t* types = calloc(CHECK_TEST_IF_CONVERT_RECORDS, sizeof(uint32_t));
    const bool ready = written && records != NULL && offsets != NULL && types != NULL && x86_disassembler_initialize();
    check_test_expect(test, ready, "if-conversion native code-shape fixture prepares");
    // Each function's least cmov count and its conditional branches: none, the loop's alone, or a guard's (any).
    static const CheckTestIfConvertShape shapes[] = {
        {
            .name = "if_max",
            .selects = 1,
            .branches = 0,
        },
        {
            .name = "if_umax",
            .selects = 1,
            .branches = 0,
        },
        {
            .name = "if_clamp",
            .selects = 2,
            .branches = 0,
        },
        {
            .name = "if_add",
            .selects = 1,
            .branches = 0,
        },
        {
            .name = "if_both",
            .selects = 1,
            .branches = 0,
        },
        {
            .name = "if_nested",
            .selects = 2,
            .branches = 0,
        },
        {
            .name = "if_and",
            .selects = 1,
            .branches = 0,
        },
        {
            .name = "if_andor",
            .selects = 2,
            .branches = 0,
        },
        {
            .name = "if_condsum",
            .selects = 1,
            .branches = 1,
        },
        {
            .name = "if_abssum",
            .selects = 1,
            .branches = 1,
        },
        {
            .name = "if_runmax",
            .selects = 1,
            .branches = 1,
        },
        {
            .name = "if_load",
            .selects = 0,
            .branches = UINT32_MAX,
        },
        {
            .name = "if_div",
            .selects = 0,
            .branches = UINT32_MAX,
        },
        {
            .name = "if_shift",
            .selects = 0,
            .branches = UINT32_MAX,
        },
        {
            .name = "if_call",
            .selects = 0,
            .branches = UINT32_MAX,
        },
        {
            .name = "if_reload_call",
            .selects = 0,
            .branches = UINT32_MAX,
        },
    };
    for (uint32_t path = 0; ready && path < 2; path += 1)
    {
        char object[512];
        snprintf(object, sizeof(object), "%s/tier1-if-convert-shape-%u.o", directory, path);
        CheckTestTier1Build build;
        const bool compiled = check_test_tier1_build(executable, source, "", object, false, path != 0, true, false, NULL, &build);
        check_test_expect(test, compiled, "if-conversion native shape unit compiles entirely by Tier 1");
        for (uint32_t function = 0; compiled && function < BUSTER_ARRAY_LENGTH(shapes); function += 1)
        {
            uint32_t relocations = 0;
            const uint32_t count = check_test_tier1_function(object, shapes[function].name, records, CHECK_TEST_IF_CONVERT_RECORDS, offsets, types, &relocations);
            uint32_t selects = 0;
            uint32_t branches = 0;
            for (uint32_t i = 0; i < count; i += 1)
            {
                const uint32_t form = records[i].instruction.form & BUSTER_X86_FORM_NUMBER;
                selects += x86_effects(form)->operation == X86_OPERATION_CMOVCC;
                branches += (x86_effects(form)->kinds >> 4) == BUSTER_X86_CONTROL_BRANCH;
            }
            const bool guarded = shapes[function].branches == UINT32_MAX;
            char message[256];
            snprintf(message, sizeof(message), "if-conversion %s has %s (path %u)", shapes[function].name,
                     guarded ? "a guarding conditional branch" : shapes[function].branches == 0 ? "cmov and no conditional branch" : "cmov and only its loop's conditional branch", path);
            check_test_expect(test, count != 0 && selects >= shapes[function].selects && (guarded ? branches != 0 : branches == shapes[function].branches), message);
        }
        unlink(object);
    }
    free(records);
    free(offsets);
    free(types);
    unlink(header);
    unlink(source);
}
