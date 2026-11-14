// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Transparent-union argument conversions and source-time activation, with independent Clang constraints.
#ifndef BUSTER_C_CHECK_TRANSPARENT_TEST_INCLUDED
#define BUSTER_C_CHECK_TRANSPARENT_TEST_INCLUDED

static __attribute__((noinline)) void check_test_transparent_calls(CheckTest* test, TypeTable* table, const char* clang)
{
    const TypeId pointer = type_pointer(table, BUSTER_TYPE_INT);
    const TypeId generic = type_pointer(table, BUSTER_TYPE_VOID);
    const TypeId qualified = type_pointer(table, type_qualify(table, BUSTER_TYPE_INT, BUSTER_TYPE_CONST));
    const TypeId record = type_tag(table, BUSTER_TYPE_KIND_UNION, 133801);
    const TypeMember members[] = {
        { .name = 133802, .type = generic, .bit_width = BUSTER_TYPE_NOT_BIT_FIELD },
        { .name = 133803, .type = pointer, .bit_width = BUSTER_TYPE_NOT_BIT_FIELD },
    };
    const bool complete = type_complete_record(table, record, members, 2, (TypeRecordAttributes){ 0 });
    const TypeTransparentUnionResult marked = type_mark_transparent_union(table, record);
    check_test_expect(test, complete && marked.status == TYPE_TRANSPARENT_UNION_APPLIED, "transparent pointer union fixture is valid");
    const TypeId function = type_function(table, BUSTER_TYPE_VOID, &record, 1, BUSTER_TYPE_FLAG_PROTOTYPE);
    CheckValue argument = { .type = pointer, .category = CHECK_RVALUE };
    CheckConversion conversion;
    CheckAction action = {
        .op = CHECK_OP_CALL,
        .operands = { { .type = function, .category = CHECK_FUNCTION } },
        .arguments = &argument,
        .argument_count = 1,
        .argument_conversions = &conversion,
        .context.extensions = BUSTER_CHECK_EXTENSION_GNU | BUSTER_CHECK_EXTENSION_GNU_CONSTANT,
    };
    CheckActionResult result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && conversion.type == generic &&
                           (conversion.flags & BUSTER_CHECK_CONVERT_TRANSPARENT_UNION) != 0,
                      "transparent union selects the first assignment-compatible member and retains its actual conversion");
    argument.type = qualified;
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_INCOMPATIBLE,
                      "GNU discarded-qualifier permission does not invent a transparent-union member match");
    argument = check_test_integer(BUSTER_TYPE_INT, 0);
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && conversion.type == generic,
                      "null pointer constants initialize the first pointer member");
    argument = (CheckValue){ .type = record };
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && conversion.type == record &&
                           (conversion.flags & BUSTER_CHECK_CONVERT_TRANSPARENT_UNION) == 0,
                      "an actual union argument keeps ordinary aggregate conversion");
    Arena history = arena_reserve(65536);
    uint32_t* completions = BUSTER_ARENA_PUSH_ARRAY(&history, uint32_t, table->tags.count);
    uint32_t* activations = BUSTER_ARENA_PUSH_ARRAY(&history, uint32_t, table->tags.count);
    const bool allocated = completions != NULL && activations != NULL;
    check_test_expect(test, allocated, "transparent-union activation history storage");
    if (allocated)
    {
        memset(completions, 0, table->tags.count * sizeof(*completions));
        memset(activations, 0, table->tags.count * sizeof(*activations));
        const uint32_t tag = table->types.index[type_index(record)];
        completions[tag] = 10;
        activations[tag] = 20;
        action.context.tag_completions = completions;
        action.context.transparent_unions = activations;
        action.context.tag_completion_count = table->tags.count;
        action.context.source_position = 19;
        argument.type = pointer;
        result = check_test_action(test, table, action);
        check_test_expect(test, result.diagnostic == CHECK_DIAG_INCOMPATIBLE, "a later transparent attribute cannot authorize an earlier call");
        action.context.source_position = 20;
        result = check_test_action(test, table, action);
        check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && conversion.type == generic,
                          "transparent member conversion begins at the exact activation cutoff");
    }
    arena_release(&history);
    // Its source fixtures are runtime data (#2053): tests/checker/transparent, which check_test_suite reads and runs.
    check_test_suite(test, clang, "checker/transparent", "independent transparent-union oracle runs");
}

#endif
