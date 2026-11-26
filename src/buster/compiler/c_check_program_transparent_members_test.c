// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Transparent unions whose first member is a wide vector or an atomic value (#1844), across the target's three
// register widths. The original source fixtures call clang's harness and are called back through pointers, using
// member conversions and union values: registers, exhausted registers and named variadic vector slots. They read
// every byte of the member's payload and the following arguments, never the union's padding.

static void check_test_program_transparent_members(CheckTest* test, const char* clang)
{
    CheckTestFixtures catalog;
    const bool loaded = check_test_fixtures_load(&catalog, "checker/programs/transparent-union-members");
    char description[768];
    snprintf(description, sizeof(description), "transparent union members: %s", loaded ? "native programs loaded" : catalog.problem);
    check_test_expect(test, loaded, description);
    const char* levels[] = { "x86-64", "x86-64-v3", "x86-64-v4" };
    const char* marches[] = { "-march=x86-64", "-march=x86-64-v3", "-march=x86-64-v4" };
    const char* previous_march = check_test_program_march;
    const X86Target native = x86_target_native();
    for (uint32_t level = 0; loaded && level < BUSTER_ARRAY_LENGTH(levels); level += 1)
    {
        X86Target target = { 0 };
        const bool parsed = x86_target_parse(levels[level], strlen(levels[level]), &target);
        check_test_expect(test, parsed, "transparent union member target");
        check_test_program_build_release();
        check_test_program_march = marches[level];
        for (uint32_t i = 0; parsed && i < catalog.count; i += 1)
        {
            const CheckSourceFixture* fixture = catalog.cases + i;
            const bool valid = check_test_fixture_word(fixture->name, 128) && !fixture->rejected && !fixture->unsupported && !fixture->extension &&
                               !fixture->undecided && fixture->standard == 0 && fixture->capacity == NULL && fixture->overload_tables == 0;
            if (valid)
            {
                snprintf(description, sizeof(description), "%s %s", fixture->name, levels[level]);
                check_test_program_source_native_limits(test, fixture->source, description, clang, fixture->char_unsigned, -1, 0, &target,
                                                         (native.features & target.features) == target.features);
            }
            else
            {
                snprintf(description, sizeof(description), "transparent union members: invalid native program '%s'", fixture->name);
                check_test_expect(test, false, description);
            }
        }
    }
    check_test_program_build_release();
    check_test_program_march = previous_march;
    arena_release(&catalog.storage);
}
