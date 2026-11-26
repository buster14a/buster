// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Independently written expectations for #2678: an integer constant's own conversion folded into its CONST
// (check_lower_fold_conversion), so that Tier 0 takes it as an immediate. Included by c_check_program_test.c; its source
// harness compares both lowerings and native output with Clang, and checks these fixtures' entries and code by name
// (check_test_constant_conversion_lowered). The programs are tests/checker/constant-conversions.

// Whether entry `e` of `function` is an integer CONST that a CONVERT follows: the pair the fold leaves none of, a
// conditional's or a logical operator's constant arm's too, whose exact CONVERT, after the label that
// joined the arms, went into them (check_lower_conditional_target).
static bool check_test_constant_conversion_pair(const Tier0Function* function, uint32_t e)
{
    return e + 1 < function->count && function->kinds[e] == BUSTER_TYPED_CONST && function->classes[e] >= BUSTER_TYPED_CLASS_I8 &&
           function->classes[e] <= BUSTER_TYPED_CLASS_U64 && function->kinds[e + 1] == BUSTER_TYPED_CONVERT;
}

// Whether `function` has a CONST of `class` holding `value`.
static bool check_test_constant_conversion_has(const Tier0Function* function, uint8_t class, uint64_t value)
{
    bool found = false;
    for (uint32_t e = 0; e < function->count; e += 1)
    {
        found |= function->kinds[e] == BUSTER_TYPED_CONST && function->classes[e] == class && function->values[e] == value;
    }
    return found;
}

// The issue's four functions, the fixture's first bodies, in Tier 0's code: no constant moved to a register and extended
// (movslq) or converted (cvtsi2sd), its one CONST of the operation's class an immediate instead, as clang -O0 takes it:
// `i + 1` an inc or an add of 1, `x == 0` a test or a compare with 0, `i * 8` an imul by 8, `d + 1` 1.0's bits. The one
// movslq left is `x == 0`'s int result's, returned as a long, which clang -O0 extends too (cltq).
static void check_test_constant_conversion_code(CheckTest* test, const CheckProgramLowered* lowered, const char* way, Arena* scratch)
{
    static const uint8_t classes[4] = { BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_I64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_F64 };
    static const uint64_t values[4] = { 1, 0, 8, UINT64_C(0x3ff0000000000000) };
    char description[256];
    for (uint32_t f = 0; f < 4 && f < lowered->count; f += 1)
    {
        const Tier0Function* function = lowered->functions + f;
        const uint64_t mark = scratch->length;
        const Tier0Code vector = tier0_compile(function, scratch);
        const bool compiled = vector.error == TIER0_OK && x86_validate(vector.records, vector.count) == vector.count;
        uint32_t extended = 0;
        uint32_t converted = 0;
        uint32_t immediate = 0;
        for (uint32_t r = 0; compiled && r < vector.count; r += 1)
        {
            const uint32_t form = vector.records[r].form & BUSTER_X86_FORM_NUMBER;
            extended += form == X86_FORM_MOVSXD_R64_RM32;
            converted += form == X86_FORM_CVTSI2SD_X_RM32 || form == X86_FORM_CVTSI2SD_X_RM64 || form == X86_FORM_CVTSI2SS_X_RM32 || form == X86_FORM_CVTSI2SS_X_RM64;
            immediate += f == 0 && (form == X86_FORM_INC_RM64 || form == X86_FORM_ADD_RM64_IMM8);
            immediate += f == 1 && (form == X86_FORM_TEST_RM64_R64 || form == X86_FORM_CMP_RM64_IMM8);
            immediate += f == 2 && form == X86_FORM_IMUL_R64_RM64_IMM8;
        }
        bool pairs = false;
        for (uint32_t e = 0; e < function->count; e += 1)
        {
            pairs |= check_test_constant_conversion_pair(function, e);
        }
        scratch->length = mark;
        snprintf(description, sizeof(description),
                 "constant-conversion-code (%s): function %u takes its constant as a CONST of its operation's class and an immediate, with no movslq or cvtsi2s", way, f);
        check_test_expect(test, compiled && extended == (f == 1 ? 1u : 0u) && converted == 0 && (f == 3 || immediate != 0) && !pairs &&
                                check_test_constant_conversion_has(function, classes[f], values[f]),
                          description);
    }
}

// Whether `function` has an integer CONST holding `value` that a CONVERT to `class` follows: a conversion kept.
static bool check_test_constant_conversion_kept(const Tier0Function* function, uint64_t value, uint8_t class)
{
    bool found = false;
    for (uint32_t e = 0; e < function->count; e += 1)
    {
        found |= check_test_constant_conversion_pair(function, e) && function->values[e] == value && function->classes[e + 1] == class;
    }
    return found;
}

// Both lowerings' functions of #2678's fixtures, by the fixture's name (`lowered` the harness's two lowerings of one
// checked program), and its node templates' (check_program_lower_templates), which must write the tasks' entries.
// "constant-conversions" folds every integer constant's conversion, a conditional's and a logical operator's arms'
// among them: no integer CONST a CONVERT follows, F32's and F64's CONSTs where the source writes integers, and _Bool's
// U8 1; "constant-conversion-code" is the issue's four functions'
// code (check_test_constant_conversion_code); "constant-conversion-controls" keeps the conversions that round: 16777217
// and 4294967295 to float, 2^53 + 1 and 2^64 - 1 to double. Conditional joins that can round keep their conversion too;
// #2842's exact floating arms and logical truths are in the folding fixture.
static void check_test_constant_conversion_lowered(CheckTest* test, const char* name, const CheckProgramLowered lowered[2], const CheckProgram* program, Arena* output,
                                                   Arena* scratch)
{
    const bool code = strcmp(name, "constant-conversion-code") == 0;
    const bool controls = strcmp(name, "constant-conversion-controls") == 0;
    char description[256];
    for (uint32_t p = 0; p < 2; p += 1)
    {
        const char* way = p == 0 ? "first lowering" : "second lowering";
        uint32_t pairs = 0;
        bool float_two = false;
        bool double_one = false;
        bool double_two = false;
        bool float_three = false;
        bool double_three = false;
        bool bool_one = false;
        uint32_t rounding = 0;
        for (uint32_t b = 0; b < lowered[p].count; b += 1)
        {
            const Tier0Function* function = lowered[p].functions + b;
            for (uint32_t e = 0; e < function->count; e += 1)
            {
                pairs += check_test_constant_conversion_pair(function, e);
            }
            float_two |= check_test_constant_conversion_has(function, BUSTER_TYPED_CLASS_F32, UINT32_C(0x40000000));
            double_one |= check_test_constant_conversion_has(function, BUSTER_TYPED_CLASS_F64, UINT64_C(0x3ff0000000000000));
            double_two |= check_test_constant_conversion_has(function, BUSTER_TYPED_CLASS_F64, UINT64_C(0xc000000000000000));
            float_three |= check_test_constant_conversion_has(function, BUSTER_TYPED_CLASS_F32, UINT32_C(0x40400000));
            double_three |= check_test_constant_conversion_has(function, BUSTER_TYPED_CLASS_F64, UINT64_C(0x4008000000000000));
            bool_one |= check_test_constant_conversion_has(function, BUSTER_TYPED_CLASS_U8, 1);
            rounding |= check_test_constant_conversion_kept(function, 16777217, BUSTER_TYPED_CLASS_F32) ? 1u : 0u;
            rounding |= check_test_constant_conversion_kept(function, UINT64_C(9007199254740993), BUSTER_TYPED_CLASS_F64) ? 2u : 0u;
            rounding |= check_test_constant_conversion_kept(function, UINT64_MAX, BUSTER_TYPED_CLASS_F64) ? 4u : 0u;
            rounding |= check_test_constant_conversion_kept(function, UINT32_MAX, BUSTER_TYPED_CLASS_F32) ? 8u : 0u;
        }
        if (code)
        {
            check_test_constant_conversion_code(test, lowered + p, way, scratch);
        }
        else if (controls)
        {
            snprintf(description, sizeof(description),
                     "%s (%s): the four constants a conversion rounds keep their CONVERT (found %#x of 0xf)",
                     name, way, rounding);
            check_test_expect(test, rounding == 0xf, description);
        }
        else
        {
            snprintf(description, sizeof(description),
                     "%s (%s): no integer constant is converted at run time, and its F32, F64 and _Bool CONSTs are folded (%u pairs)", name, way, pairs);
            check_test_expect(test, pairs == 0 && float_two && double_one && double_two && float_three && double_three && bool_one, description);
        }
    }
    CheckProgramLowered templated = { 0 };
    bool same = check_program_lower_templates(&templated, program, true, output, scratch) && templated.count == lowered[0].count && templated.template_mismatches == 0;
    for (uint32_t b = 0; same && b < templated.count; b += 1)
    {
        same = check_test_program_function(templated.functions + b, lowered[0].functions + b);
    }
    snprintf(description, sizeof(description), "%s: the node templates fold the constants' conversions as the tasks do", name);
    check_test_expect(test, same, description);
}

// The fixtures are runtime data (#2053): tests/checker/constant-conversions, three programs that check_test_program_source
// runs natively against clang's build, by the catalog's names, which select their checks
// (check_test_constant_conversion_lowered).
static void check_test_constant_conversions(CheckTest* test, const char* clang)
{
    CheckTestFixtures catalog;
    const bool loaded = check_test_fixtures_load(&catalog, "checker/constant-conversions");
    char problem[sizeof(catalog.problem) + 128];
    snprintf(problem, sizeof(problem), "checker/constant-conversions fixtures (#2053): %s", catalog.problem);
    check_test_expect(test, loaded && catalog.count == 3, problem);
    for (uint32_t i = 0; loaded && i < catalog.count; i += 1)
    {
        check_test_program_source(test, catalog.cases[i].source, catalog.cases[i].name, clang);
    }
    arena_release(&catalog.storage);
}
