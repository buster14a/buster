// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Transparent-union argument conversions and source-time activation, with independent Clang constraints.
#ifndef BUSTER_C_CHECK_TRANSPARENT_TEST_INCLUDED
#define BUSTER_C_CHECK_TRANSPARENT_TEST_INCLUDED

static void check_test_transparent_calls(CheckTest* test, TypeTable* table, const char* clang)
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
        .extensions = BUSTER_CHECK_EXTENSION_GNU | BUSTER_CHECK_EXTENSION_GNU_CONSTANT,
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
        action.tag_completions = completions;
        action.transparent_unions = activations;
        action.tag_completion_count = table->tags.count;
        action.source_position = 19;
        argument.type = pointer;
        result = check_test_action(test, table, action);
        check_test_expect(test, result.diagnostic == CHECK_DIAG_INCOMPATIBLE, "a later transparent attribute cannot authorize an earlier call");
        action.source_position = 20;
        result = check_test_action(test, table, action);
        check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && conversion.type == generic,
                          "transparent member conversion begins at the exact activation cutoff");
    }
    arena_release(&history);
    const CheckSourceFixture fixtures[] = {
        { .name = "transparent pointer and null arguments", .source = "typedef union __attribute__((transparent_union)){void*p;int*q;}U;void f(U);void g(int*p){f(p);f(0);f((void*)0);}\n" },
        { .name = "transparent qualifier discard remains incompatible", .source = "typedef union __attribute__((transparent_union)){void*p;int*q;}U;void f(U);void g(const int*p){f(p);}\n", .rejected = true },
        { .name = "transparent object-function extension remains incompatible", .source = "typedef union __attribute__((transparent_union)){void*p;int*q;}U;void f(U);void g(void){f(g);}\n", .rejected = true },
        { .name = "transparent arithmetic member uses assignment conversions", .source = "typedef union __attribute__((transparent_union)){int i;unsigned u;}U;void f(U);void g(short s,long l,float x){f(s);f(l);f(x);}\n" },
        { .name = "transparent atomic member uses value conversion", .source = "typedef union __attribute__((transparent_union)){_Atomic(float) x;int i;}U;void f(U);void g(float x){f(x);}\n" },
        { .name = "transparent conversion is limited to parameters", .source = "typedef union __attribute__((transparent_union)){int i;unsigned u;}U;U x=1;\n", .rejected = true },
        { .name = "transparent attribute source activation rejects earlier call", .source = "union U{int a;unsigned b;};void f(union U);\nvoid before(void){f(1);}\ntypedef union U T __attribute__((transparent_union));\nvoid after(void){f(1);}\n", .rejected = true },
        { .name = "transparent attribute source activation permits later call", .source = "union U{int a;unsigned b;};void f(union U);typedef union U T __attribute__((transparent_union));void after(void){f(1);}\n" },
    };
    char directory[] = "/tmp/buster-check-transparent-XXXXXX";
    bool ready = mkdtemp(directory) != NULL;
    char paths[BUSTER_ARRAY_LENGTH(fixtures)][160];
    char* pointers[BUSTER_ARRAY_LENGTH(fixtures)];
    ParseTestClang outcomes[BUSTER_ARRAY_LENGTH(fixtures)];
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        pointers[i] = paths[i];
        ready = ready && parse_test_write(paths[i], fixtures[i].source, strlen(fixtures[i].source));
    }
    const char* mode = "-std=gnu17";
    const bool checked = ready && parse_test_clang(clang, &mode, 1, pointers, BUSTER_ARRAY_LENGTH(fixtures), outcomes);
    check_test_expect(test, checked, "independent transparent-union oracle runs");
    for (uint32_t i = 0; checked && i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        check_test_expect(test, outcomes[i].rejected == fixtures[i].rejected, fixtures[i].name);
        check_test_source(test, fixtures + i, paths[i], outcomes + i);
    }
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        unlink(paths[i]);
    }
    rmdir(directory);
}

#endif
