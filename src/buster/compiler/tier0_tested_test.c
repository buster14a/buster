// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// test-tier0's tested compares check (#2877): a condition as the lowering writes it, its compare's value tested against
// zero before the branch (a compare, CONST 0, NE or EQ, BRANCH_FALSE or BRANCH_TRUE), or tested twice (a negation, or a
// macro's `!= 0`, first), must be the compare and its jump, with no setcc. For every compare, integer class of 4 and 8
// bytes, way the compare takes its operands (both on the stack, a constant, a local, a local and a constant, two
// locals), test or two tests and branch: both paths give the same records, no record makes the compare's value, the
// compare's group ends in its own record, the entries between make none, and the branch's group is one record, the jump
// of the right condition (the compare's for a branch taken where it holds, which BRANCH_TRUE is, turned by each EQ
// among the tests; its opposite's otherwise). The same at every place across a 64-entry block's end, where the vector
// path reads the tests and the compare from the block before, and gives a compare there the flags' variant from the
// branch's block. Then the streams the fusion must leave alone, or take less of: a test against another constant, a
// label on a test's CONST 0 (a value arrives there that isn't the compare's), a third test and a floating compare,
// whose compares keep records of their own. The programs of the shape with Tier0ProgramOptions.tested run such
// conditions natively and through the interpreter. tier0_test.c includes this file before its command, whose tally and
// helpers it uses.

#define BUSTER_TIER0_TEST_TESTED_ENTRIES 96
// The statements before a placed condition: every count that puts each entry of the widest condition (a compare of two
// values on the stack behind two tests, twelve entries) first in a block and last in the one before.
#define BUSTER_TIER0_TEST_TESTED_FIRST_PAD 52
#define BUSTER_TIER0_TEST_TESTED_LAST_PAD 70

// f(a, b): `pad` entries of statements that change nothing (a = a in three entries, a discarded local in two; none, or
// three at least), then the compare of a (local 0) and b (local 1), or of them plus one, or of a constant, as `variant`
// has it; tested by `tests` tests against zero (the last against `zero`), the first of two EQ if `inner_equal` and the
// last if `equal`; branched on; 1 returned on the way that falls through and 2 at the branch's target. Returns the
// stream's length, with the compare's and the branch's entries.
static uint32_t tier0_test_tested_stream(Tier0TestEntry* entries, uint32_t kind, uint32_t class, uint32_t variant, uint32_t tests, bool inner_equal, bool equal, bool when,
                                         uint64_t zero, uint32_t pad, uint32_t* compare, uint32_t* branch)
{
    uint32_t count = 0;
    if (pad % 2 != 0)
    {
        entries[count++] = (Tier0TestEntry){ BUSTER_TYPED_LOCAL, (uint8_t)class, 0 };
        entries[count++] = (Tier0TestEntry){ BUSTER_TYPED_STORE_LOCAL, (uint8_t)class, 0 };
        entries[count++] = (Tier0TestEntry){ BUSTER_TYPED_DISCARD, (uint8_t)class, 0 };
    }
    while (count < pad)
    {
        entries[count++] = (Tier0TestEntry){ BUSTER_TYPED_LOCAL, (uint8_t)class, 0 };
        entries[count++] = (Tier0TestEntry){ BUSTER_TYPED_DISCARD, (uint8_t)class, 0 };
    }
    const bool first_local = variant == BUSTER_TIER0_VARIANT_HOME_CONST || variant == BUSTER_TIER0_VARIANT_HOME_HOME;
    entries[count++] = (Tier0TestEntry){ BUSTER_TYPED_LOCAL, (uint8_t)class, 0 };
    if (!first_local)
    {
        entries[count++] = (Tier0TestEntry){ BUSTER_TYPED_CONST, (uint8_t)class, 1 };
        entries[count++] = (Tier0TestEntry){ BUSTER_TYPED_ADD, (uint8_t)class, 0 };
    }
    if (variant == BUSTER_TIER0_VARIANT_CONST || variant == BUSTER_TIER0_VARIANT_HOME_CONST)
    {
        entries[count++] = (Tier0TestEntry){ BUSTER_TYPED_CONST, (uint8_t)class, 5 };
    }
    else
    {
        entries[count++] = (Tier0TestEntry){ BUSTER_TYPED_LOCAL, (uint8_t)class, 1 };
        if (variant == BUSTER_TIER0_VARIANT_PLAIN)
        {
            entries[count++] = (Tier0TestEntry){ BUSTER_TYPED_CONST, (uint8_t)class, 1 };
            entries[count++] = (Tier0TestEntry){ BUSTER_TYPED_ADD, (uint8_t)class, 0 };
        }
    }
    *compare = count;
    entries[count++] = (Tier0TestEntry){ (uint8_t)kind, BUSTER_TYPED_CLASS_I32, 0 };
    for (uint32_t t = 1; t < tests; t += 1)
    {
        entries[count++] = (Tier0TestEntry){ BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 0 };
        entries[count++] = (Tier0TestEntry){ inner_equal ? BUSTER_TYPED_EQ : BUSTER_TYPED_NE, BUSTER_TYPED_CLASS_I32, 0 };
    }
    entries[count++] = (Tier0TestEntry){ BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, zero };
    entries[count++] = (Tier0TestEntry){ equal ? BUSTER_TYPED_EQ : BUSTER_TYPED_NE, BUSTER_TYPED_CLASS_I32, 0 };
    *branch = count;
    entries[count] = (Tier0TestEntry){ when ? BUSTER_TYPED_BRANCH_TRUE : BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_CLASS_I32, count + 3 };
    count += 1;
    entries[count++] = (Tier0TestEntry){ BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 1 };
    entries[count++] = (Tier0TestEntry){ BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_U64, 0 };
    entries[count++] = (Tier0TestEntry){ BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 2 };
    entries[count++] = (Tier0TestEntry){ BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_U64, 0 };
    return count;
}

// How many of the code's records make a compare's value (a setcc).
static uint32_t tier0_test_tested_values(const Tier0Code* code)
{
    uint32_t values = 0;
    for (uint32_t r = 0; r < code->count; r += 1)
    {
        for (uint32_t cc = 0; cc < 16; cc += 1)
        {
            values += code->records[r].form == tier0_setcc[cc] ? 1 : 0;
        }
    }
    return values;
}

// A stream of `count` entries over f's two locals of `class`, compiled by both paths: whether it is valid, both compile
// it and their code is the same; the vector path's code in `code`.
static bool tier0_test_tested_compile(const Tier0TestEntry* entries, uint32_t count, uint32_t class, Arena* arena, Tier0Code* code)
{
    static const AbiLocation parameters[2] = {
        { .registers = { BUSTER_ABI_RDI, BUSTER_ABI_REGISTER_NONE }, .kind = BUSTER_ABI_LOCATION_REGISTERS },
        { .registers = { BUSTER_ABI_RSI, BUSTER_ABI_REGISTER_NONE }, .kind = BUSTER_ABI_LOCATION_REGISTERS },
    };
    static const AbiLocation floating_parameters[2] = {
        { .registers = { BUSTER_ABI_REGISTER_VECTOR | 0, BUSTER_ABI_REGISTER_NONE }, .kind = BUSTER_ABI_LOCATION_REGISTERS },
        { .registers = { BUSTER_ABI_REGISTER_VECTOR | 1, BUSTER_ABI_REGISTER_NONE }, .kind = BUSTER_ABI_LOCATION_REGISTERS },
    };
    static const uint8_t local_flags[2];
    uint8_t kinds[BUSTER_TIER0_TEST_TESTED_ENTRIES];
    uint8_t classes[BUSTER_TIER0_TEST_TESTED_ENTRIES];
    uint64_t values[BUSTER_TIER0_TEST_TESTED_ENTRIES];
    const uint8_t local_classes[2] = { (uint8_t)class, (uint8_t)class };
    Tier0Function function = tier0_test_stack_function(entries, count, kinds, classes, values, local_classes, local_flags, 2);
    function.parameter_count = 2;
    function.parameters = tier0_class_floating(class) ? floating_parameters : parameters;
    function.entry_records = true;
    const Tier0Check check = tier0_validate(&function);
    const Tier0Code scalar = tier0_compile_scalar(&function, arena);
    *code = tier0_compile(&function, arena);
    return check.error == TIER0_OK && scalar.error == TIER0_OK && code->error == TIER0_OK && tier0_test_same_code(&scalar, code) && code->entry_records != 0 &&
           x86_validate(code->records, code->count) == code->count;
}

// The records of entry e's group in `code`.
static uint32_t tier0_test_tested_records(const Tier0Code* code, uint32_t e)
{
    return code->entry_records[e + 1] - code->entry_records[e];
}

// One condition after `pad` entries: both paths give the same records, none makes the compare's value, the compare has
// records of its own, the tests between none, and the branch's is the jump alone, taken as the tests and the branch say.
static bool tier0_test_tested_condition(Tier0TestTally* tally, Arena* arena, uint32_t kind, uint32_t class, uint32_t variant, uint32_t tests, bool inner_equal, bool equal,
                                        bool when, uint32_t pad)
{
    static const char* const variants[5] = { "both on the stack", "a constant", "a local", "a local and a constant", "two locals" };
    Tier0TestEntry entries[BUSTER_TIER0_TEST_TESTED_ENTRIES];
    uint32_t compare = 0;
    uint32_t branch = 0;
    const uint32_t count = tier0_test_tested_stream(entries, kind, class, variant, tests, inner_equal, equal, when, 0, pad, &compare, &branch);
    const uint64_t mark = arena->length;
    Tier0Code code;
    char name[160];
    snprintf(name, sizeof(name), "kind %u of class %u with %s, %s%s and BRANCH_%s after %u entries", kind, class, variants[variant],
             tests == 2 ? (inner_equal ? "EQ, " : "NE, ") : "", equal ? "EQ" : "NE", when ? "TRUE" : "FALSE", pad);
    bool ready = tier0_test_expect(tally, tier0_test_tested_compile(entries, count, class, arena, &code), "tested compares", "%s: invalid, refused, or the paths differ", name);
    // The branch is taken where the compare holds for BRANCH_TRUE, turned by each EQ among the tests.
    const uint32_t cc = tier0_condition(kind, tier0_class_signed(class));
    const bool holds = when != (equal != (tests == 2 && inner_equal));
    const uint16_t jump = tier0_jcc_rel8[holds ? cc : cc ^ 1];
    ready = ready && tier0_test_expect(tally, tier0_test_tested_values(&code) == 0, "tested compares", "%s: %u records make the compare's value", name,
                                       tier0_test_tested_values(&code));
    ready = ready && tier0_test_expect(tally, tier0_test_tested_records(&code, compare) >= 1 && code.entry_records[compare + 1] == code.entry_records[branch],
                                       "tested compares", "%s: the compare has %u records and the tests between %u", name, tier0_test_tested_records(&code, compare),
                                       code.entry_records[branch] - code.entry_records[compare + 1]);
    ready = ready && tier0_test_expect(tally, tier0_test_tested_records(&code, branch) == 1 && code.records[code.entry_records[branch]].form == jump, "tested compares",
                                       "%s: the branch's %u records start with form %u, not the jump %u alone", name, tier0_test_tested_records(&code, branch),
                                       code.records[code.entry_records[branch]].form, jump);
    arena_pop_to(arena, mark);
    return ready;
}

static void tier0_test_tested(Tier0TestTally* tally)
{
    static const uint8_t classes[4] = { BUSTER_TYPED_CLASS_I32, BUSTER_TYPED_CLASS_U32, BUSTER_TYPED_CLASS_I64, BUSTER_TYPED_CLASS_U64 };
    Tier0TestEntry entries[BUSTER_TIER0_TEST_TESTED_ENTRIES];
    Arena arena = arena_reserve(UINT64_C(1) << 24);
    tier0_test_expect(tally, arena.base != NULL, "tested compares", "no memory");
    uint32_t fused = 0;
    for (uint32_t kind = BUSTER_TYPED_EQ; arena.base != NULL && kind <= BUSTER_TYPED_GE; kind += 1)
    {
        for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(classes); c += 1)
        {
            for (uint32_t variant = BUSTER_TIER0_VARIANT_PLAIN; variant <= BUSTER_TIER0_VARIANT_HOME_HOME; variant += 1)
            {
                // One test: its kind and the branch's. Two: the first one's kind too.
                for (uint32_t context = 0; context < 12; context += 1)
                {
                    const uint32_t tests = context < 4 ? 1 : 2;
                    fused += tier0_test_tested_condition(tally, &arena, kind, classes[c], variant, tests, (context & 4) == 0, (context & 1) != 0, (context & 2) != 0, 0) ? 1 : 0;
                }
            }
        }
    }
    tier0_test_expect(tally, arena.base == NULL || fused == 6 * 4 * 5 * 12, "tested compares", "%u of the 1440 conditions are a compare and its jump", fused);
    // Across a block's end: each way the compare takes its operands behind one test and behind two at every place, the
    // compare, the class, the tests and the branch changing with the place.
    uint32_t placed = 0;
    for (uint32_t pad = BUSTER_TIER0_TEST_TESTED_FIRST_PAD; arena.base != NULL && pad <= BUSTER_TIER0_TEST_TESTED_LAST_PAD; pad += 1)
    {
        for (uint32_t variant = BUSTER_TIER0_VARIANT_PLAIN; variant <= BUSTER_TIER0_VARIANT_HOME_HOME; variant += 1)
        {
            for (uint32_t tests = 1; tests <= 2; tests += 1)
            {
                const uint32_t turn = pad + variant + tests;
                placed += tier0_test_tested_condition(tally, &arena, BUSTER_TYPED_EQ + turn % 6, classes[turn % 4], variant, tests, (turn & 4) != 0, (turn & 1) != 0,
                                                      (turn & 2) != 0, pad) ? 1 : 0;
            }
        }
    }
    const uint32_t places = (BUSTER_TIER0_TEST_TESTED_LAST_PAD - BUSTER_TIER0_TEST_TESTED_FIRST_PAD + 1) * 5 * 2;
    tier0_test_expect(tally, arena.base == NULL || placed == places, "tested compares", "%u of the %u conditions across a block's end are a compare and its jump", placed, places);
    // What the fusion leaves alone, or takes less of.
    if (arena.base != NULL)
    {
        uint32_t compare = 0;
        uint32_t branch = 0;
        Tier0Code code;
        // A test against one, not zero: the value is made, and the branch has the test of it and the jump.
        uint32_t count = tier0_test_tested_stream(entries, BUSTER_TYPED_LT, BUSTER_TYPED_CLASS_I32, BUSTER_TIER0_VARIANT_HOME_HOME, 1, false, false, false, 1, 0, &compare, &branch);
        tier0_test_expect(tally, tier0_test_tested_compile(entries, count, BUSTER_TYPED_CLASS_I32, &arena, &code) && tier0_test_tested_values(&code) == 1 &&
                                     tier0_test_tested_records(&code, branch) == 2,
                          "tested compares", "a compare tested against 1 must make its value");
        // A floating compare: no template leaves a floating compare's flags to a jump, so its value is made (cmpltsd,
        // movd and and, not a setcc) and the branch has the test of it.
        count = tier0_test_tested_stream(entries, BUSTER_TYPED_LT, BUSTER_TYPED_CLASS_F64, BUSTER_TIER0_VARIANT_HOME_HOME, 1, false, false, false, 0, 0, &compare, &branch);
        tier0_test_expect(tally, tier0_test_tested_compile(entries, count, BUSTER_TYPED_CLASS_F64, &arena, &code) && tier0_test_tested_records(&code, compare) >= 2 &&
                                     tier0_test_tested_records(&code, branch) == 2,
                          "tested compares", "a floating compare tested against zero must make its value");
        // And behind two tests: the first test is the branch's compare, of the value the floating compare made.
        count = tier0_test_tested_stream(entries, BUSTER_TYPED_LT, BUSTER_TYPED_CLASS_F64, BUSTER_TIER0_VARIANT_HOME_HOME, 2, true, false, false, 0, 0, &compare, &branch);
        tier0_test_expect(tally, tier0_test_tested_compile(entries, count, BUSTER_TYPED_CLASS_F64, &arena, &code) && tier0_test_tested_records(&code, compare) >= 2 &&
                                     tier0_test_tested_records(&code, compare + 2) == 1 && tier0_test_tested_records(&code, branch) == 1 &&
                                     code.records[code.entry_records[branch]].form == X86_FORM_JNE_REL8,
                          "tested compares", "a floating compare behind two tests must make its value, and its first test be the branch's compare");
        // A label on the CONST 0: with a not zero, 7 arrives there as the tested value, by a JUMP_VALUE over the compare.
        //   0: LOCAL a; 1: BRANCH_FALSE 4; 2: CONST 7; 3: JUMP_VALUE 7; 4: LOCAL a; 5: LOCAL b; 6: LT; 7: CONST 0; 8: NE;
        //   9: BRANCH_FALSE 12; 10: CONST 1; 11: RETURN; 12: CONST 2; 13: RETURN.
        static const Tier0TestEntry joined[] = {
            { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I32, 0 },  { BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_CLASS_I32, 4 },
            { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 7 },  { BUSTER_TYPED_JUMP_VALUE, BUSTER_TYPED_CLASS_I32, 7 },
            { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I32, 0 },  { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I32, 1 },
            { BUSTER_TYPED_LT, BUSTER_TYPED_CLASS_I32, 0 },     { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 0 },
            { BUSTER_TYPED_NE, BUSTER_TYPED_CLASS_I32, 0 },     { BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_CLASS_I32, 12 },
            { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 1 },  { BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_U64, 0 },
            { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 2 },  { BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_U64, 0 },
        };
        tier0_test_expect(tally, tier0_test_tested_compile(joined, BUSTER_ARRAY_LENGTH(joined), BUSTER_TYPED_CLASS_I32, &arena, &code) &&
                                     tier0_test_tested_values(&code) == 1 && tier0_test_tested_records(&code, 9) == 2,
                          "tested compares", "a compare whose test is a jump's target must make its value");
        // The same stream without the jump over the compare: its first local is a branch's target still, where the
        // compare's group starts, and no label is behind it. Fused again.
        static const Tier0TestEntry straight[] = {
            { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I32, 0 },  { BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_CLASS_I32, 4 },
            { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 7 },  { BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_I32, 0 },
            { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I32, 0 },  { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I32, 1 },
            { BUSTER_TYPED_LT, BUSTER_TYPED_CLASS_I32, 0 },     { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 0 },
            { BUSTER_TYPED_NE, BUSTER_TYPED_CLASS_I32, 0 },     { BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_CLASS_I32, 12 },
            { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 1 },  { BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_U64, 0 },
            { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 2 },  { BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_U64, 0 },
        };
        tier0_test_expect(tally, tier0_test_tested_compile(straight, BUSTER_ARRAY_LENGTH(straight), BUSTER_TYPED_CLASS_I32, &arena, &code) &&
                                     tier0_test_tested_values(&code) == 0 && tier0_test_tested_records(&code, 9) == 1,
                          "tested compares", "a tested compare whose first local is a jump's target is still its branch's");
        // Two tests with a label on the first one's CONST 0, where 7 arrives as that test's value: the first test is
        // the branch's compare, and the compare before it makes its value.
        //   0: LOCAL a; 1: BRANCH_FALSE 4; 2: CONST 7; 3: JUMP_VALUE 7; 4: LOCAL a; 5: LOCAL b; 6: LT; 7: CONST 0; 8: EQ;
        //   9: CONST 0; 10: NE; 11: BRANCH_FALSE 14; 12: CONST 1; 13: RETURN; 14: CONST 2; 15: RETURN.
        static const Tier0TestEntry joined_twice[] = {
            { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I32, 0 },  { BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_CLASS_I32, 4 },
            { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 7 },  { BUSTER_TYPED_JUMP_VALUE, BUSTER_TYPED_CLASS_I32, 7 },
            { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I32, 0 },  { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I32, 1 },
            { BUSTER_TYPED_LT, BUSTER_TYPED_CLASS_I32, 0 },     { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 0 },
            { BUSTER_TYPED_EQ, BUSTER_TYPED_CLASS_I32, 0 },     { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 0 },
            { BUSTER_TYPED_NE, BUSTER_TYPED_CLASS_I32, 0 },     { BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_CLASS_I32, 14 },
            { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 1 },  { BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_U64, 0 },
            { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 2 },  { BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_U64, 0 },
        };
        tier0_test_expect(tally, tier0_test_tested_compile(joined_twice, BUSTER_ARRAY_LENGTH(joined_twice), BUSTER_TYPED_CLASS_I32, &arena, &code) &&
                                     tier0_test_tested_values(&code) == 1 && tier0_test_tested_records(&code, 8) == 1 && tier0_test_tested_records(&code, 11) == 1 &&
                                     code.records[code.entry_records[11]].form == X86_FORM_JNE_REL8,
                          "tested compares", "a compare whose first of two tests is a jump's target must make its value, and that test be the branch's compare");
        // Three tests: the branch takes the last two, with the first as its compare, of the value the compare made.
        //   0: LOCAL a; 1: LOCAL b; 2: LT; 3: CONST 0; 4: EQ; 5: CONST 0; 6: EQ; 7: CONST 0; 8: NE; 9: BRANCH_FALSE 12;
        //   10: CONST 1; 11: RETURN; 12: CONST 2; 13: RETURN. !!(a < b) is false where (a < b) == 0 holds: je.
        static const Tier0TestEntry thrice[] = {
            { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I32, 0 },  { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I32, 1 },
            { BUSTER_TYPED_LT, BUSTER_TYPED_CLASS_I32, 0 },     { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 0 },
            { BUSTER_TYPED_EQ, BUSTER_TYPED_CLASS_I32, 0 },     { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 0 },
            { BUSTER_TYPED_EQ, BUSTER_TYPED_CLASS_I32, 0 },     { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 0 },
            { BUSTER_TYPED_NE, BUSTER_TYPED_CLASS_I32, 0 },     { BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_CLASS_I32, 12 },
            { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 1 },  { BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_U64, 0 },
            { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 2 },  { BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_U64, 0 },
        };
        tier0_test_expect(tally, tier0_test_tested_compile(thrice, BUSTER_ARRAY_LENGTH(thrice), BUSTER_TYPED_CLASS_I32, &arena, &code) &&
                                     tier0_test_tested_values(&code) == 1 && tier0_test_tested_records(&code, 4) == 1 && tier0_test_tested_records(&code, 9) == 1 &&
                                     code.records[code.entry_records[9]].form == X86_FORM_JE_REL8,
                          "tested compares", "behind three tests a compare makes its value, and the first test is the branch's compare");
    }
    arena_release(&arena);
}
