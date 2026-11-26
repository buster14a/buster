// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original #1382 C-source regressions. Include after c_check_tier1_driver_test.c. The ordinary driver command runs
// the semantic fixtures against Clang on both scalar paths and with rematerialization on/off. A separate decoded
// object check proves the C lowering/promotion/if-conversion/selection chain reaches cmov for pure max/clamp and
// conditional arithmetic, while the guarding branches of potentially faulting loads and division remain. #1382's
// kernels follow: a nested clamp, `&&` and `||` values (no select of a tested truth value since #2677: their and and
// or, or #2676's cmp and cmovCC of a comparison only the select reads, which needs no setcc), including a `_Bool`
// `&&` of an `||` (#2846), and loops whose arms
// reload the element the head compared
// (a conditional sum and a select of it or its negation), whose only conditional branch is the loop's. A running max,
// whose condition reads the maximum its select would carry, and a reload after a call keep their branches.
// #2842's floating selects follow, at x86-64, x86-64-v3 and x86-64-v4: the issue's kernels (a sum of two constants by an
// integer comparison, of a reused load or another array's, and a float by an int), selects of each floating comparison
// and its negation, a triangle, a sequential clamp, a negated arm, a reused load's and a select of selects: their blends
// (under k1 at x86-64-v4 for an integer condition, but in a function with a mask value) and no conditional branch but a
// loop's, and no copy of a loop's values that end at its select; a nested clamp keeps its outer test, whose arm's
// comparison would be speculated, and a load nothing proves and a division keep their branches. Clang -O0 is the
// oracle, on NaNs (a payload's too), signed zeros, infinities and both conditions.
// The runtime catalog also covers a conversion into an integer conditional's or logical operator's arms: exact
// constants fold and exact variable conversions retain a blend, while rounding conversions stay after an integer
// join or behind their source guard. Its companion checks all four rounding modes, inexact flags and short-circuit
// volatile effects against Clang, including a null pointer on each skipped arm.

static const char check_test_tier1_if_convert_header[] =
    "long if_rare_code(long a, long b);\n"
    "long if_variable_compare(long a, long b);\n"
    "long if_likely(long a, long b);\n"
    "long if_unlikely(long a, long b);\n"
    "long if_probability(long a, long b);\n"
    "long if_rare_bool(long a, long b);\n"
    "long if_balanced(long a, long b);\n"
    "long if_rare_integer(long a, long b);\n"
    "long if_dynamic(long a, long b, volatile long* expected);\n"
    "long if_mixed_hints(long a, long b, long z);\n"
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
    "_Bool if_andor_bool(int a, int b, int c);\n"
    "long if_condsum(const int* a, int n);\n"
    "long if_abssum(const int* a, int n);\n"
    "int if_runmax(const int* a, int n);\n"
    "unsigned long if_reload_call(const unsigned long* p);\n";

static const char check_test_tier1_if_convert_other[] =
    "#include <stdio.h>\n"
    "#include \"tier1-if-convert.h\"\n"
    "static unsigned long calls;\n"
    "unsigned long other_effect(unsigned long x) { calls += 1; return x * 3 + calls; }\n"
    "int main(void)\n"
    "{\n"
    "    const long edges[9] = { (-9223372036854775807L - 1L), -9223372036854775807L, -10, -1, 0, 1, 10, 9223372036854775806L, 9223372036854775807L };\n"
    "    volatile long expected = 0;\n"
    "    unsigned long h = 0;\n"
    "    for (unsigned i = 0; i < 9; i += 1) for (unsigned j = 0; j < 9; j += 1)\n"
    "    {\n"
    "        h = h * 31 + (unsigned long)if_rare_code(edges[i], edges[j]);\n"
    "        h = h * 31 + (unsigned long)if_variable_compare(edges[i], edges[j]);\n"
    "        h = h * 31 + (unsigned long)if_likely(edges[i], edges[j]);\n"
    "        h = h * 31 + (unsigned long)if_unlikely(edges[i], edges[j]);\n"
    "        h = h * 31 + (unsigned long)if_probability(edges[i], edges[j]);\n"
    "        h = h * 31 + (unsigned long)if_rare_bool(edges[i], edges[j]);\n"
    "        h = h * 31 + (unsigned long)if_balanced(edges[i], edges[j]);\n"
    "        h = h * 31 + (unsigned long)if_rare_integer(edges[i], edges[j]);\n"
    "        h = h * 31 + (unsigned long)if_dynamic(edges[i], edges[j], &expected);\n"
    "        h = h * 31 + (unsigned long)if_mixed_hints(edges[i], edges[j], 3);\n"
    "        h += (unsigned long)expected;\n"
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
    "    for (int a = -2; a < 3; a += 1) for (int b = -2; b < 3; b += 1) for (int c = -2; c < 3; c += 1) h = h * 31 + (unsigned)if_andor(a, b, c) + (unsigned)if_andor_bool(a, b, c);\n"
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
    // A truth value's (#2677): no cmov whose flags a test wrote (`test; cmovne`, the select of a tested truth value),
    // whatever and, or and fused selects of comparisons (#2676's cmp and cmovCC) compute it.
    bool truth;
    uint32_t branches;
};

// A floating select's expected shape (#2842): its selects, how many of them by a comparison's lane mask, and its
// conditional branches: none, its loop's alone, or a kept one (UINT32_MAX).
typedef struct CheckTestFloatSelectShape CheckTestFloatSelectShape;
struct CheckTestFloatSelectShape
{
    const char* name;
    uint32_t selects;
    uint32_t lanes;
    uint32_t branches;
    // Integer-to-floating records: none for folded constant arms, one for a variable or a retained join's conversion.
    uint32_t conversions;
    // A loop's select of values that end at it: its blend's destination takes one's register, so no xmm register is
    // copied into another but xmm15, the comparison's (tier1_select_ties).
    bool tied;
    // A function with a mask value, defined at x86-64-v4 alone: its select takes the lane mask there, since k1 may hold
    // the mask (Tier1SelectHoles.opmask).
    bool masks;
};

// The floating selects' fixture at each target (#2842): clang -O0's run is the oracle of the unit's Tier 1 code (by both
// paths, rematerializing or not, and by --verify, its code certified), and each function's decoded code has its blends:
// at x86-64-v4, kmovw and vpblendmq of an integer condition's select, at either width, pxor, pand and pxor of the lane
// mask elsewhere and of a comparison's cmps. Its converted constants have no run-time conversion; every remaining
// conversion has the expected shape without adding a branch to an exact integer conditional.
static void check_test_driver_tier1_float_select(CheckTest* test, const char* clang, const char* executable, const char* directory, const CheckDriverProgramInput* input)
{
    const char* header_source = check_test_driver_input_text(test, input);
    const char* unit_source = check_test_driver_input_text(test, input + 1);
    const char* other_source = check_test_driver_input_text(test, input + 2);
    static const char* const targets[] = {
        "-march=x86-64",
        "-march=x86-64-v3",
        "-march=x86-64-v4",
    };
    static const CheckTestFloatSelectShape shapes[] = {
        {
            .name = "fsel_count",
            .selects = 1,
            .lanes = 0,
            .branches = 1,
            .tied = true,
        },
        {
            .name = "fsel_int",
            .selects = 1,
            .lanes = 0,
            .branches = 0,
        },
        {
            .name = "fsel_less",
            .selects = 1,
            .lanes = 1,
            .branches = 0,
        },
        {
            .name = "fsel_greater",
            .selects = 1,
            .lanes = 1,
            .branches = 0,
        },
        {
            .name = "fsel_at_most",
            .selects = 1,
            .lanes = 1,
            .branches = 0,
        },
        {
            .name = "fsel_at_least",
            .selects = 1,
            .lanes = 1,
            .branches = 0,
        },
        {
            .name = "fsel_equal",
            .selects = 1,
            .lanes = 1,
            .branches = 0,
        },
        {
            .name = "fsel_unequal",
            .selects = 1,
            .lanes = 1,
            .branches = 0,
        },
        {
            .name = "fsel_not_less",
            .selects = 1,
            .lanes = 1,
            .branches = 0,
        },
        {
            .name = "fsel_not_equal",
            .selects = 1,
            .lanes = 1,
            .branches = 0,
        },
        {
            .name = "fsel_float_less",
            .selects = 1,
            .lanes = 1,
            .branches = 0,
        },
        {
            .name = "fsel_float_not_greater",
            .selects = 1,
            .lanes = 1,
            .branches = 0,
        },
        {
            .name = "fsel_float_unequal",
            .selects = 1,
            .lanes = 1,
            .branches = 0,
        },
        {
            .name = "fsel_mixed",
            .selects = 1,
            .lanes = 0,
            .branches = 0,
        },
        {
            .name = "fsel_triangle",
            .selects = 1,
            .lanes = 0,
            .branches = 0,
        },
        {
            .name = "fsel_clamp",
            .selects = 2,
            .lanes = 2,
            .branches = 0,
        },
        {
            .name = "fsel_nested",
            .selects = 1,
            .lanes = 1,
            .branches = 1,
        },
        {
            .name = "fsel_magnitude",
            .selects = 1,
            .lanes = 1,
            .branches = 0,
        },
        {
            .name = "fsel_reused",
            .selects = 1,
            .lanes = 1,
            .branches = 1,
            .tied = true,
        },
        {
            .name = "fsel_choice",
            .selects = 2,
            .lanes = 0,
            .branches = 0,
        },
        {
            .name = "fsel_convert_double",
            .selects = 1,
            .conversions = 1,
        },
        {
            .name = "fsel_convert_double_reverse",
            .selects = 1,
            .conversions = 1,
        },
        {
            .name = "fsel_convert_unsigned",
            .selects = 1,
            .conversions = 1,
        },
        {
            .name = "fsel_convert_double_constants",
            .selects = 1,
        },
        {
            .name = "fsel_convert_float_constants",
            .selects = 1,
        },
        {
            .name = "fsel_convert_mixed_signedness",
            .selects = 1,
        },
        {
            .name = "fsel_convert_double_logical",
            .selects = 1,
            .conversions = 1,
        },
        {
            .name = "fsel_convert_float_logical",
            .selects = 1,
            .conversions = 1,
        },
        {
            .name = "fsel_convert_count",
            .selects = 1,
            .branches = 1,
            .tied = true,
        },
        {
            .name = "fsel_convert_effect",
            .branches = UINT32_MAX,
            .conversions = 1,
        },
        {
            .name = "fsel_convert_effect_float",
            .branches = UINT32_MAX,
            .conversions = 1,
        },
        {
            .name = "fsel_round_float",
            .conversions = 1,
        },
        {
            .name = "fsel_round_double",
            .conversions = 1,
        },
        {
            .name = "fsel_guarded_float_convert",
            .branches = UINT32_MAX,
            .conversions = 1,
        },
        {
            .name = "fsel_guarded_double_convert",
            .branches = UINT32_MAX,
            .conversions = 1,
        },
        {
            .name = "fsel_round_float_constant",
            .conversions = 1,
        },
        {
            .name = "fsel_round_double_constant",
            .conversions = 1,
        },
        {
            .name = "fsel_masked",
            .selects = 1,
            .lanes = 0,
            .branches = 0,
            .masks = true,
        },
        {
            .name = "fsel_values",
            .selects = 0,
            .lanes = 0,
            .branches = UINT32_MAX,
        },
        {
            .name = "fsel_guarded",
            .selects = 0,
            .lanes = 0,
            .branches = UINT32_MAX,
        },
        {
            .name = "fsel_divided",
            .selects = 0,
            .lanes = 0,
            .branches = UINT32_MAX,
        },
    };
    char header[512];
    char source[512];
    snprintf(header, sizeof(header), "%s/tier1-float-select.h", directory);
    snprintf(source, sizeof(source), "%s/tier1-float-select.c", directory);
    enum
    {
        CHECK_TEST_FLOAT_SELECT_RECORDS = 512,
    };
    X86Decoded* records = calloc(CHECK_TEST_FLOAT_SELECT_RECORDS, sizeof(X86Decoded));
    uint32_t* offsets = calloc(CHECK_TEST_FLOAT_SELECT_RECORDS, sizeof(uint32_t));
    uint32_t* types = calloc(CHECK_TEST_FLOAT_SELECT_RECORDS, sizeof(uint32_t));
    const bool ready = records != NULL && offsets != NULL && types != NULL && x86_disassembler_initialize();
    check_test_expect(test, ready, "floating selects' native code-shape fixture prepares");
    for (uint32_t target = 0; ready && target < BUSTER_ARRAY_LENGTH(targets); target += 1)
    {
        check_test_driver_tier1_units(test, clang, executable, directory, "tier1-float-select", "floating selects'", header_source, unit_source, other_source, targets[target], true, 0);
        const bool masked = target == 2;
        const bool written = parse_test_write(header, header_source, input[0].length) && parse_test_write(source, unit_source, input[1].length);
        for (uint32_t path = 0; written && path < 2; path += 1)
        {
            char object[512];
            snprintf(object, sizeof(object), "%s/tier1-float-select-shape-%u-%u.o", directory, target, path);
            CheckTestTier1Build build;
            const bool compiled = check_test_tier1_build(executable, source, "", object, false, path != 0, true, false, targets[target], &build);
            char message[256];
            snprintf(message, sizeof(message), "floating selects' shape unit compiles entirely by Tier 1 (%s, path %u)", targets[target], path);
            check_test_expect(test, compiled, message);
            for (uint32_t function = 0; compiled && function < BUSTER_ARRAY_LENGTH(shapes); function += 1)
            {
                const CheckTestFloatSelectShape* shape = &shapes[function];
                uint32_t relocations = 0;
                const uint32_t count = !shape->masks || masked ? check_test_tier1_function(object, shape->name, records, CHECK_TEST_FLOAT_SELECT_RECORDS, offsets, types, &relocations) : 0;
                uint32_t blends = 0;
                uint32_t opmasks = 0;
                uint32_t lanes = 0;
                uint32_t branches = 0;
                uint32_t copies = 0;
                uint32_t conversions = 0;
                for (uint32_t i = 0; i < count; i += 1)
                {
                    const X86Instruction* instruction = &records[i].instruction;
                    const uint32_t form = instruction->form & BUSTER_X86_FORM_NUMBER;
                    const bool masked_blend = form == X86_FORM_VPBLENDMQ_X_X_XM128 && instruction->decorations != 0;
                    // At a target with AVX, each legacy SSE record's VEX twin (#2830, tier1_emit_vex).
                    blends += form == X86_FORM_PAND_X_XM128 || form == X86_FORM_VPAND_X_X_XM128 || masked_blend;
                    opmasks += masked_blend && instruction->decorations == 1;
                    lanes += form == X86_FORM_CMPSD_X_XM64_IMM8 || form == X86_FORM_CMPSS_X_XM32_IMM8 || form == X86_FORM_VCMPSD_X_X_XM64_IMM8 || form == X86_FORM_VCMPSS_X_X_XM32_IMM8;
                    branches += (x86_effects(form)->kinds >> 4) == BUSTER_X86_CONTROL_BRANCH;
                    copies += (form == X86_FORM_MOVAPS_X_XM || form == X86_FORM_VMOVAPS_X_XM128) && instruction->mode == BUSTER_X86_REGISTER_DIRECT && instruction->reg != 15;
                    conversions += x86_effects(form)->operation == X86_OPERATION_INT_TO_FLOAT;
                }
                const bool guarded = shape->branches == UINT32_MAX;
                // At x86-64-v4 an integer condition's selects are under k1, but in a function with a mask value, and a
                // comparison's by its lanes.
                const uint32_t under = masked && !shape->masks ? shape->selects - shape->lanes : 0;
                const bool shaped = (guarded ? branches != 0 && blends == 0 :
                                               blends == shape->selects && lanes == shape->lanes && opmasks == under && branches == shape->branches && (!shape->tied || copies == 0)) &&
                                    conversions == shape->conversions;
                snprintf(message, sizeof(message), "floating select %s (%s, path %u): %u blends/%u expected, %u lanes/%u, %u masked/%u, %u branches/%s, %u conversions/%u, %u copies",
                         shape->name, targets[target], path, blends, shape->selects, lanes, shape->lanes, opmasks, under, branches,
                         guarded ? "guarded" : shape->branches == 0 ? "none" : "loop", conversions, shape->conversions, copies);
                check_test_expect(test, (shape->masks && !masked) || (count != 0 && shaped), message);
            }
            unlink(object);
        }
        unlink(header);
        unlink(source);
    }
    free(records);
    free(offsets);
    free(types);
}

static void check_test_driver_tier1_if_convert(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    const int descriptor = open("tests/driver/tier1_if_convert.c", O_RDONLY | O_CLOEXEC);
    char* unit = NULL;
    const bool loaded = descriptor >= 0 && check_test_driver_read_file(descriptor, &unit);
    if (descriptor >= 0)
    {
        close(descriptor);
    }
    check_test_expect(test, loaded, "if-conversion C fixture loads");
    if (loaded)
    {
        check_test_driver_tier1_units(test, clang, executable, directory, "tier1-if-convert", "safe conditional values'", check_test_tier1_if_convert_header,
                                      unit, check_test_tier1_if_convert_other, NULL, true, 0);
    }
    // Keep the code-shape oracle independent of textual disassembly and of the selector's IR rule numbers.
    char header[512];
    char source[512];
    snprintf(header, sizeof(header), "%s/tier1-if-convert.h", directory);
    snprintf(source, sizeof(source), "%s/tier1-if-convert.c", directory);
    const bool written = parse_test_write(header, check_test_tier1_if_convert_header, sizeof(check_test_tier1_if_convert_header) - 1) &&
                         loaded && parse_test_write(source, unit, strlen(unit));
    free(unit);
    enum
    {
        CHECK_TEST_IF_CONVERT_RECORDS = 512,
    };
    X86Decoded* records = calloc(CHECK_TEST_IF_CONVERT_RECORDS, sizeof(X86Decoded));
    uint32_t* offsets = calloc(CHECK_TEST_IF_CONVERT_RECORDS, sizeof(uint32_t));
    uint32_t* types = calloc(CHECK_TEST_IF_CONVERT_RECORDS, sizeof(uint32_t));
    const bool ready = written && records != NULL && offsets != NULL && types != NULL && x86_disassembler_initialize();
    check_test_expect(test, ready, "if-conversion native code-shape fixture prepares");
    // Each function's least cmov count, or a truth value's lack of a tested select, and its conditional branches: none,
    // the loop's alone, or a kept one (any).
    static const CheckTestIfConvertShape shapes[] = {
        {
            .name = "if_max",
            .selects = 1,
            .branches = 0,
        },
        {
            .name = "if_rare_code",
            .branches = 1,
        },
        {
            .name = "if_variable_compare",
            .selects = 1,
        },
        {
            .name = "if_likely",
            .selects = 0,
            .branches = 1,
        },
        {
            .name = "if_unlikely",
            .selects = 0,
            .branches = 1,
        },
        {
            .name = "if_probability",
            .selects = 0,
            .branches = 1,
        },
        {
            .name = "if_rare_bool",
            .selects = 0,
            .branches = 1,
        },
        {
            .name = "if_balanced",
            .selects = 1,
            .branches = 0,
        },
        {
            .name = "if_rare_integer",
            .selects = 1,
            .branches = 0,
        },
        {
            .name = "if_dynamic",
            .selects = 1,
            .branches = 0,
        },
        {
            .name = "if_mixed_hints",
            .selects = 1,
            .branches = 1,
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
            .truth = true,
            .branches = 0,
        },
        {
            .name = "if_andor",
            .truth = true,
            .branches = 0,
        },
        {
            .name = "if_andor_bool",
            .truth = true,
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
            .selects = 0,
            .branches = UINT32_MAX,
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
            uint32_t tested = 0;
            uint32_t branches = 0;
            // The operation that last wrote the flags, which a cmov reads.
            uint32_t flags = X86_OPERATION_NONE;
            for (uint32_t i = 0; i < count; i += 1)
            {
                const uint32_t form = records[i].instruction.form & BUSTER_X86_FORM_NUMBER;
                const X86Effects* effects = x86_effects(form);
                selects += effects->operation == X86_OPERATION_CMOVCC;
                tested += effects->operation == X86_OPERATION_CMOVCC && flags == X86_OPERATION_TEST;
                branches += (effects->kinds >> 4) == BUSTER_X86_CONTROL_BRANCH;
                flags = effects->flags_written != 0 ? effects->operation : flags;
            }
            const bool guarded = shapes[function].branches == UINT32_MAX;
            const bool truth = shapes[function].truth;
            const bool kept = !guarded && shapes[function].branches != 0 && shapes[function].selects == 0;
            const bool selected = truth ? tested == 0 : kept ? selects == 0 : selects >= shapes[function].selects;
            char message[256];
            snprintf(message, sizeof(message), "if-conversion %s has %s (path %u)", shapes[function].name,
                     kept ? "its predicted branch and no cmov" : guarded ? "a guarding conditional branch" : truth ? "no select of a tested truth value and no conditional branch" :
                     shapes[function].branches == 0 ? "cmov and no conditional branch" : "cmov and one conditional branch", path);
            check_test_expect(test, count != 0 && selected && (guarded ? branches != 0 : branches == shapes[function].branches), message);
        }
        unlink(object);
    }
    free(records);
    free(offsets);
    free(types);
    unlink(header);
    unlink(source);
    check_test_driver_programs(test, clang, executable, directory, "tier1-float-select", "tier1-float-select.h\ttier1-float-select.c\tmain.c", check_test_driver_tier1_float_select);
}
