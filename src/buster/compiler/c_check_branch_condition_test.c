// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Independently written expectations for #2883: a statement's or a conditional's condition whose `&&`, `||` and `!` are
// written as branches to its own targets (check_lower_branch), with no 0 or 1 made, joined and tested again, and for
// #2891: an `&&` or `||` value's first operand that is one, written so to the value's own label. Included by
// c_check_program_test.c; its source harness compares both lowerings and native output with Clang, and checks these
// fixtures' entries by name (check_test_branch_condition_lowered). The programs are tests/checker/branch-conditions.

// The shapes fixture's functions written as branches, its first bodies; the next ones use `&&` and `||` as values.
#define BUSTER_CHECK_TEST_BRANCH_CONDITIONS 10u
#define BUSTER_CHECK_TEST_BRANCH_VALUES 6u

// Each function's joins: a condition function's conditionals' joins of their arms (`pick`'s one, `pick_nested`'s two),
// a value function's its outermost operator's alone (its first operand's `&&` or `||` none, #2891).
static const uint8_t check_test_branch_condition_joins[BUSTER_CHECK_TEST_BRANCH_CONDITIONS + BUSTER_CHECK_TEST_BRANCH_VALUES] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 1, 1, 1, 1, 1, 1,
};

// Whether entry `e` of `function` tests a value against zero: an integer CONST 0 that an NE or EQ of I32 takes.
static bool check_test_branch_condition_test_at(const Tier0Function* function, uint32_t e)
{
    return e + 1 < function->count && function->kinds[e] == BUSTER_TYPED_CONST && function->values[e] == 0 && function->classes[e] >= BUSTER_TYPED_CLASS_I8 &&
           function->classes[e] <= BUSTER_TYPED_CLASS_U64 && (function->kinds[e + 1] == BUSTER_TYPED_NE || function->kinds[e + 1] == BUSTER_TYPED_EQ) &&
           function->classes[e + 1] == BUSTER_TYPED_CLASS_I32;
}

// In `function`, the JUMP_VALUEs (the joins of a value) and the jumps' targets where a value that arrives there is tested
// against zero (tested again: the entry a branch, a jump or a JUMP_VALUE goes to is such a test). The branches' and
// jumps' values are their targets' entries once the body is lowered.
static void check_test_branch_condition_count(const Tier0Function* function, uint32_t* joins, uint32_t* retested)
{
    *joins = 0;
    *retested = 0;
    for (uint32_t e = 0; e < function->count; e += 1)
    {
        const uint8_t kind = function->kinds[e];
        *joins += kind == BUSTER_TYPED_JUMP_VALUE;
        if (kind >= BUSTER_TYPED_BRANCH_FALSE && kind <= BUSTER_TYPED_JUMP_VALUE && function->values[e] < function->count)
        {
            *retested += check_test_branch_condition_test_at(function, (uint32_t)function->values[e]);
        }
    }
}

// Both lowerings' functions of #2883's fixtures, by the fixture's name (`lowered` the harness's two lowerings of one
// checked program), and its node templates' (check_program_lower_templates), which must write the tasks' entries.
// "branch-condition-shapes": its first BUSTER_CHECK_TEST_BRANCH_CONDITIONS functions have `&&` and `||` only in
// conditions, which leave no value tested again at a jump's target and no JUMP_VALUE but their conditionals' joins of
// their arms (check_test_branch_condition_joins); the next BUSTER_CHECK_TEST_BRANCH_VALUES use them as values (returned,
// assigned and as an expression statement, chains whose first operand is an `&&` or `||` under `!`s), and each keeps
// its outermost operator's join alone, with no value tested again (#2891). "branch-condition-order" has its own checks
// in its run against clang alone.
static void check_test_branch_condition_lowered(CheckTest* test, const char* name, const CheckProgramLowered lowered[2], const CheckProgram* program, Arena* output,
                                                Arena* scratch)
{
    const bool shapes = strcmp(name, "branch-condition-shapes") == 0;
    char description[256];
    for (uint32_t p = 0; shapes && p < 2; p += 1)
    {
        const char* way = p == 0 ? "first lowering" : "second lowering";
        const uint32_t count = lowered[p].count;
        snprintf(description, sizeof(description), "%s (%s): the fixture's %u functions are lowered (%u)", name, way,
                 BUSTER_CHECK_TEST_BRANCH_CONDITIONS + BUSTER_CHECK_TEST_BRANCH_VALUES + 1, count);
        check_test_expect(test, count == BUSTER_CHECK_TEST_BRANCH_CONDITIONS + BUSTER_CHECK_TEST_BRANCH_VALUES + 1, description);
        for (uint32_t f = 0; f < BUSTER_CHECK_TEST_BRANCH_CONDITIONS + BUSTER_CHECK_TEST_BRANCH_VALUES && f < count; f += 1)
        {
            uint32_t joins = 0;
            uint32_t retested = 0;
            check_test_branch_condition_count(lowered[p].functions + f, &joins, &retested);
            const bool condition = f < BUSTER_CHECK_TEST_BRANCH_CONDITIONS;
            snprintf(description, sizeof(description),
                     condition ? "%s (%s): function %u's conditions are branches: its conditionals' joins alone, no value tested again (%u joins, %u tested again)" :
                                 "%s (%s): function %u's values keep their own joins alone, no value tested again (%u joins, %u tested again)",
                     name, way, f, joins, retested);
            check_test_expect(test, joins == check_test_branch_condition_joins[f] && retested == 0, description);
        }
    }
    CheckProgramLowered templated = { 0 };
    bool same = check_program_lower_templates(&templated, program, true, output, scratch) && templated.count == lowered[0].count && templated.template_mismatches == 0;
    for (uint32_t b = 0; same && b < templated.count; b += 1)
    {
        same = check_test_program_function(templated.functions + b, lowered[0].functions + b);
    }
    snprintf(description, sizeof(description), "%s: the node templates write the conditions' branches as the tasks do", name);
    check_test_expect(test, same, description);
}

// The fixtures are runtime data (#2053): tests/checker/branch-conditions, two programs that check_test_program_source
// runs natively against clang's build, by the catalog's names, which select their checks
// (check_test_branch_condition_lowered).
static void check_test_branch_conditions(CheckTest* test, const char* clang)
{
    CheckTestFixtures catalog;
    const bool loaded = check_test_fixtures_load(&catalog, "checker/branch-conditions");
    char problem[sizeof(catalog.problem) + 128];
    snprintf(problem, sizeof(problem), "checker/branch-conditions fixtures (#2053): %s", catalog.problem);
    check_test_expect(test, loaded && catalog.count == 2, problem);
    for (uint32_t i = 0; loaded && i < catalog.count; i += 1)
    {
        check_test_program_source(test, catalog.cases[i].source, catalog.cases[i].name, clang);
    }
    arena_release(&catalog.storage);
}
