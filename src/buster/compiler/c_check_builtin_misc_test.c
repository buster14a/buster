// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original #148 misc-action fixtures. Include after c_check_builtin_misc.c and c_checker_test.c.
// Independent source fixtures use the existing Clang subprocess oracle; no source execution is claimed here.
// Semantics were checked against llvmorg-23-init Builtins.td, SemaChecking.cpp, SemaExpr.cpp and CGBuiltin.cpp:
// https://github.com/llvm/llvm-project/tree/llvmorg-23-init/clang
// Every build and test invocation must use the approved 9700X and its normal exclusive lock.

typedef struct CheckBuiltinMiscTest CheckBuiltinMiscTest;
struct CheckBuiltinMiscTest
{
    uint64_t checks;
    uint64_t failures;
};

static void check_builtin_misc_test_expect(CheckBuiltinMiscTest* test, bool condition, const char* description)
{
    test->checks += 1;
    if (!condition)
    {
        test->failures += 1;
        if (test->failures <= 24)
        {
            fprintf(stderr, "test-check-builtins-misc: %s\n", description);
        }
    }
}

static bool check_builtin_misc_test_equal(const CheckBuiltinMiscResult* a, const CheckBuiltinMiscResult* b)
{
    bool same = check_test_action_equal(&a->action, &b->action) && check_test_value_equal(&a->constant_value, &b->constant_value) &&
                a->constant_evaluated_operands == b->constant_evaluated_operands && a->evaluated_operands == b->evaluated_operands &&
                a->warnings == b->warnings && a->unsupported == b->unsupported && a->frame_level == b->frame_level &&
                a->alignment == b->alignment && a->alignment_offset == b->alignment_offset && a->runtime_builtin == b->runtime_builtin &&
                a->constraint == b->constraint && a->warning_operands == b->warning_operands && a->prefetch_rw == b->prefetch_rw &&
                a->prefetch_locality == b->prefetch_locality && a->terminates == b->terminates && a->reads_memory == b->reads_memory &&
                a->writes_memory == b->writes_memory && a->allocates_stack == b->allocates_stack &&
                a->alignment_offset_known == b->alignment_offset_known;
    for (uint32_t i = 0; i < 3; i += 1)
    {
        same &= a->argument_conversions[i].type == b->argument_conversions[i].type &&
                a->argument_conversions[i].flags == b->argument_conversions[i].flags;
    }
    return same;
}

static CheckValue check_builtin_misc_test_pointer(TypeId type, uint32_t object, int64_t offset)
{
    const CheckValue value = {
        .type = type,
        .category = CHECK_RVALUE,
        .constant_kind = CHECK_CONSTANT_ADDRESS,
        .object = object,
        .offset = offset,
    };
    return value;
}

static CheckBuiltinMiscResult check_builtin_misc_test_action(TypeTable* table, CBuiltinId id, const CheckValue* arguments, uint32_t count)
{
    const CheckBuiltinMiscAction input = {
        .id = id,
        .arguments = arguments,
        .argument_count = count,
        .node = 148,
        .source_position = UINT32_MAX,
    };
    CheckBuiltinMiscResult output;
    check_builtin_misc_one(table, &input, &output);
    return output;
}

static void check_builtin_misc_test_values(CheckBuiltinMiscTest* test, TypeTable* table)
{
    CheckValue arguments[3] = {
        check_test_integer(BUSTER_TYPE_INT, UINT32_MAX),
        check_test_float(BUSTER_TYPE_DOUBLE, 3.75L),
    };
    CheckBuiltinMiscResult result = check_builtin_misc_test_action(table, CBUILTIN_EXPECT, arguments, 2);
    check_builtin_misc_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == BUSTER_TYPE_LONG &&
                                  result.action.value.integer == UINT64_MAX && result.evaluated_operands == 3 &&
                                  result.argument_conversions[0].type == BUSTER_TYPE_LONG && result.argument_conversions[1].type == BUSTER_TYPE_LONG &&
                                  result.runtime_builtin == CBUILTIN_EXPECT, "expect converts both parameters to long and returns converted first");
    arguments[1] = (CheckValue){ .type = BUSTER_TYPE_INT, .category = CHECK_RVALUE };
    result = check_builtin_misc_test_action(table, CBUILTIN_EXPECT, arguments, 2);
    check_builtin_misc_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.integer == UINT64_MAX &&
                                  result.evaluated_operands == 3 && result.constant_evaluated_operands == 1 &&
                                  result.constant_value.integer == UINT64_MAX && (result.constant_value.flags & BUSTER_CHECK_ICE) != 0,
                                  "expect source evaluator ignores an unknown expected value, while runtime still evaluates it");
    arguments[1].category = CHECK_LVALUE;
    arguments[1].type = type_qualify(table, BUSTER_TYPE_INT, BUSTER_TYPE_VOLATILE);
    result = check_builtin_misc_test_action(table, CBUILTIN_EXPECT, arguments, 2);
    check_builtin_misc_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && (result.action.value.flags & BUSTER_CHECK_SIDE_EFFECT) != 0 &&
                                  (result.action.value.flags & BUSTER_CHECK_ICE) == 0 && result.evaluated_operands == 3 &&
                                  result.constant_evaluated_operands == 1 && (result.constant_value.flags & BUSTER_CHECK_ICE) != 0 &&
                                  (result.constant_value.flags & BUSTER_CHECK_SIDE_EFFECT) == 0 &&
                                  (result.argument_conversions[1].flags & BUSTER_CHECK_CONVERT_LVALUE) != 0,
                                  "expect retains second volatile read at runtime and omits it from source constant evaluation");
    arguments[1] = check_test_integer(BUSTER_TYPE_INT, 0);
    arguments[1].constant_kind = CHECK_CONSTANT_NONE;
    arguments[1].fault = CHECK_FAULT_DIVIDE_BY_ZERO;
    arguments[1].fault_node = 42;
    result = check_builtin_misc_test_action(table, CBUILTIN_EXPECT, arguments, 2);
    check_builtin_misc_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE &&
                                  result.action.value.fault == CHECK_FAULT_DIVIDE_BY_ZERO && result.action.value.fault_node == 42 &&
                                  result.constant_value.fault == CHECK_FAULT_NONE && result.constant_value.integer == UINT64_MAX &&
                                  (result.constant_value.flags & BUSTER_CHECK_ICE) != 0 && result.constant_evaluated_operands == 1,
                                  "expect keeps runtime fault provenance separate from first-only source constant result");
    const CBuiltinId terminal[] = { CBUILTIN_TRAP, CBUILTIN_UNREACHABLE };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(terminal); i += 1)
    {
        result = check_builtin_misc_test_action(table, terminal[i], NULL, 0);
        check_builtin_misc_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == BUSTER_TYPE_VOID &&
                                      result.terminates && result.evaluated_operands == 0 && (result.action.value.flags & BUSTER_CHECK_SIDE_EFFECT) != 0,
                                      "terminal builtin has void type and explicit terminal effect");
    }

    const TypeId pointer = type_pointer(table, BUSTER_TYPE_INT);
    arguments[0] = check_builtin_misc_test_pointer(pointer, 7, 8);
    arguments[1] = check_test_integer(BUSTER_TYPE_UINT, 16);
    arguments[2] = check_test_float(BUSTER_TYPE_DOUBLE, 3.75L);
    result = check_builtin_misc_test_action(table, CBUILTIN_ASSUME_ALIGNED, arguments, 3);
    check_builtin_misc_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.alignment == 16 &&
                                  result.alignment_offset == 3 && result.alignment_offset_known && result.argument_conversions[0].type == pointer &&
                                  result.argument_conversions[1].type == BUSTER_TYPE_UINT && result.argument_conversions[2].type == BUSTER_TYPE_ULONG &&
                                  result.action.value.type == type_pointer(table, BUSTER_TYPE_VOID) && result.action.value.constant_kind == CHECK_CONSTANT_NONE,
                                  "assume_aligned retains pointer/alignment types, converts optional offset and does not invent aligned constants");
    arguments[1] = check_test_integer(BUSTER_TYPE_INT, UINT32_C(0x80000000));
    result = check_builtin_misc_test_action(table, CBUILTIN_ASSUME_ALIGNED, arguments, 2);
    check_builtin_misc_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.alignment == UINT32_C(0x80000000),
                                  "Clang alignment power test admits signed minimum raw one-bit representation");
    arguments[1] = check_test_integer(BUSTER_TYPE_ULLONG, UINT64_C(1) << 40);
    result = check_builtin_misc_test_action(table, CBUILTIN_ASSUME_ALIGNED, arguments, 2);
    check_builtin_misc_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && (result.warnings & BUSTER_CHECK_BUILTIN_MISC_WARN_ALIGNMENT) != 0,
                                  "large alignment warns instead of rejecting an otherwise valid power");
    arguments[1] = check_test_integer(BUSTER_TYPE_UINT128, (unsigned __int128)1 << 80);
    result = check_builtin_misc_test_action(table, CBUILTIN_ASSUME_ALIGNED, arguments, 2);
    check_builtin_misc_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE &&
                                  (result.unsupported & BUSTER_CHECK_BUILTIN_MISC_UNSUPPORTED_ALIGNMENT) != 0,
                                  "wider alignment promise remains explicit unsupported metadata");
    arguments[1] = check_test_integer(BUSTER_TYPE_UCHAR, 1);
    arguments[2] = check_test_integer(BUSTER_TYPE_SHORT, 3);
    result = check_builtin_misc_test_action(table, CBUILTIN_PREFETCH, arguments, 3);
    check_builtin_misc_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == BUSTER_TYPE_VOID &&
                                  result.prefetch_rw == 1 && result.prefetch_locality == 3 &&
                                  result.argument_conversions[1].type == BUSTER_TYPE_INT && result.argument_conversions[2].type == BUSTER_TYPE_INT,
                                  "prefetch optional operands use default integer promotions and their ranges");
    result = check_builtin_misc_test_action(table, CBUILTIN_PREFETCH, arguments, 1);
    check_builtin_misc_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.prefetch_rw == 0 && result.prefetch_locality == 3,
                                  "prefetch has documented optional defaults");

    const TypeId constant_pointer = type_pointer(table, type_qualify(table, BUSTER_TYPE_INT, BUSTER_TYPE_CONST));
    arguments[0] = check_builtin_misc_test_pointer(constant_pointer, 3, 0);
    arguments[1] = check_builtin_misc_test_pointer(pointer, 4, 0);
    arguments[2] = check_test_integer(BUSTER_TYPE_INT, 7);
    const CBuiltinId memory[] = { CBUILTIN_MEMCPY, CBUILTIN_MEMMOVE, CBUILTIN_MEMCMP };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(memory); i += 1)
    {
        result = check_builtin_misc_test_action(table, memory[i], arguments, 3);
        const bool writing = memory[i] != CBUILTIN_MEMCMP;
        check_builtin_misc_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.reads_memory && result.writes_memory == writing &&
                                      result.action.value.constant_kind == CHECK_CONSTANT_NONE && result.argument_conversions[2].type == BUSTER_TYPE_ULONG &&
                                      ((result.warnings & BUSTER_CHECK_BUILTIN_MISC_WARN_QUALIFIERS) != 0) == writing,
                                      "memory prototype converts size, preserves effects and reports qualifier loss without fabricating results");
    }
    arguments[1] = check_test_float(BUSTER_TYPE_DOUBLE, 257.75L);
    result = check_builtin_misc_test_action(table, CBUILTIN_MEMSET, arguments, 3);
    check_builtin_misc_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.writes_memory && !result.reads_memory &&
                                  result.argument_conversions[1].type == BUSTER_TYPE_INT, "memset uses int parameter then runtime byte semantics");
    arguments[0] = check_builtin_misc_test_pointer(type_pointer(table, BUSTER_TYPE_UCHAR), 5, 0);
    result = check_builtin_misc_test_action(table, CBUILTIN_STRLEN, arguments, 1);
    check_builtin_misc_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == BUSTER_TYPE_ULONG &&
                                  result.reads_memory && result.action.value.constant_kind == CHECK_CONSTANT_NONE &&
                                  (result.warnings & BUSTER_CHECK_BUILTIN_MISC_WARN_POINTER_SIGN) != 0,
                                  "strlen has size_t type, pointer-sign warning and no guessed byte content");
    arguments[0] = check_test_float(BUSTER_TYPE_DOUBLE, 19.75L);
    result = check_builtin_misc_test_action(table, CBUILTIN_ALLOCA, arguments, 1);
    check_builtin_misc_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.allocates_stack &&
                                  result.argument_conversions[0].type == BUSTER_TYPE_ULONG && result.action.value.constant_kind == CHECK_CONSTANT_NONE,
                                  "alloca converts size_t and retains stack allocation effect");

    arguments[0] = check_test_integer(BUSTER_TYPE_ULLONG, UINT64_C(1) << 32);
    result = check_builtin_misc_test_action(table, CBUILTIN_FRAME_ADDRESS, arguments, 1);
    check_builtin_misc_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.frame_level == 0 && result.unsupported == 0 &&
                                  result.argument_conversions[0].type == BUSTER_TYPE_UINT, "frame level converts to unsigned int before ICE range checking");
    arguments[0] = check_test_integer(BUSTER_TYPE_UINT, 0);
    result = check_builtin_misc_test_action(table, CBUILTIN_FRAME_ADDRESS, arguments, 1);
    check_builtin_misc_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.frame_level == 0,
                                  "an explicitly integer-cast floating literal arrives as an integer ICE");
    check_builtin_misc_test_expect(test, result.evaluated_operands == 0 && result.runtime_builtin == CBUILTIN_FRAME_ADDRESS,
                                  "a frame builtin's level, an ICE without effects, is not evaluated at run time");
    arguments[0] = check_test_integer(BUSTER_TYPE_INT, 1);
    result = check_builtin_misc_test_action(table, CBUILTIN_RETURN_ADDRESS, arguments, 1);
    check_builtin_misc_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.frame_level == 1 &&
                                  (result.unsupported & BUSTER_CHECK_BUILTIN_MISC_UNSUPPORTED_FRAME_LEVEL) != 0 &&
                                  (result.warnings & BUSTER_CHECK_BUILTIN_MISC_WARN_FRAME_LEVEL) != 0,
                                  "nonzero valid frame level remains explicitly unsupported by runtime contract");
    arguments[0] = check_builtin_misc_test_pointer(pointer, 11, 3);
    const CBuiltinId frame_identities[] = { CBUILTIN_EXTRACT_RETURN_ADDR, CBUILTIN_FROB_RETURN_ADDR };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(frame_identities); i += 1)
    {
        result = check_builtin_misc_test_action(table, frame_identities[i], arguments, 1);
        check_builtin_misc_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == type_pointer(table, BUSTER_TYPE_VOID) &&
                                      result.action.value.constant_kind == CHECK_CONSTANT_NONE && result.runtime_builtin == frame_identities[i],
                                      "return-address representation helpers retain target identity without claiming source constexpr");
    }
}

static void check_builtin_misc_test_constraints(CheckBuiltinMiscTest* test, TypeTable* table)
{
    const TypeId pointer = type_pointer(table, BUSTER_TYPE_INT);
    CheckValue arguments[3] = {
        check_builtin_misc_test_pointer(pointer, 1, 0),
        check_test_integer(BUSTER_TYPE_INT, 16),
        check_test_integer(BUSTER_TYPE_INT, 3),
    };
    const CBuiltinId unary[] = { CBUILTIN_STRLEN, CBUILTIN_ALLOCA, CBUILTIN_FRAME_ADDRESS, CBUILTIN_RETURN_ADDRESS,
                                CBUILTIN_EXTRACT_RETURN_ADDR, CBUILTIN_FROB_RETURN_ADDR };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(unary); i += 1)
    {
        CheckBuiltinMiscResult result = check_builtin_misc_test_action(table, unary[i], NULL, 0);
        check_builtin_misc_test_expect(test, result.action.diagnostic == CHECK_DIAG_ARGUMENT_COUNT, "unary misc arity rejected before reading arguments");
    }
    CheckBuiltinMiscResult result = check_builtin_misc_test_action(table, CBUILTIN_EXPECT, NULL, 2);
    check_builtin_misc_test_expect(test, result.action.diagnostic == CHECK_DIAG_INVALID_TYPE, "nonnull argument slice required for nonempty call");
    result = check_builtin_misc_test_action(table, CBUILTIN_EXPECT, arguments, 2);
    check_builtin_misc_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPATIBLE && result.action.diagnostic_operand == 0,
                                  "expect ordinary long parameter rejects pointer conversion");
    result = check_builtin_misc_test_action(table, CBUILTIN_STRLEN, arguments, 1);
    check_builtin_misc_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPATIBLE, "strlen rejects incompatible integer pointer");
    arguments[0] = check_test_integer(BUSTER_TYPE_INT, 0);
    result = check_builtin_misc_test_action(table, CBUILTIN_ASSUME_ALIGNED, arguments, 2);
    check_builtin_misc_test_expect(test, result.action.diagnostic == CHECK_DIAG_POINTER_REQUIRED, "assume_aligned custom first parameter rejects integer zero");
    arguments[0] = check_builtin_misc_test_pointer(pointer, 1, 0);
    arguments[1] = check_test_integer(BUSTER_TYPE_INT, 3);
    result = check_builtin_misc_test_action(table, CBUILTIN_ASSUME_ALIGNED, arguments, 2);
    check_builtin_misc_test_expect(test, result.constraint == CHECK_BUILTIN_MISC_CONSTRAINT_ALIGNMENT && result.action.diagnostic != CHECK_DIAG_NONE,
                                  "non-power alignment rejected");
    arguments[1] = check_test_float(BUSTER_TYPE_DOUBLE, 16);
    result = check_builtin_misc_test_action(table, CBUILTIN_ASSUME_ALIGNED, arguments, 2);
    check_builtin_misc_test_expect(test, result.action.diagnostic == CHECK_DIAG_INTEGER_REQUIRED, "alignment retains original integer ICE requirement");
    arguments[1] = check_test_integer(BUSTER_TYPE_INT, 2);
    result = check_builtin_misc_test_action(table, CBUILTIN_PREFETCH, arguments, 2);
    check_builtin_misc_test_expect(test, result.constraint == CHECK_BUILTIN_MISC_CONSTRAINT_RANGE && result.action.diagnostic_operand == 1,
                                  "prefetch rw range is zero through one");
    arguments[1] = (CheckValue){ .type = BUSTER_TYPE_INT };
    result = check_builtin_misc_test_action(table, CBUILTIN_PREFETCH, arguments, 2);
    check_builtin_misc_test_expect(test, result.constraint == CHECK_BUILTIN_MISC_CONSTRAINT_CONSTANT && result.action.diagnostic != CHECK_DIAG_NONE,
                                  "prefetch optional operands must be ICE");
    arguments[0] = check_test_integer(BUSTER_TYPE_UINT, 65536);
    result = check_builtin_misc_test_action(table, CBUILTIN_FRAME_ADDRESS, arguments, 1);
    check_builtin_misc_test_expect(test, result.constraint == CHECK_BUILTIN_MISC_CONSTRAINT_RANGE && result.action.diagnostic != CHECK_DIAG_NONE,
                                  "frame level maximum is 65535");
    arguments[0] = (CheckValue){ .type = BUSTER_TYPE_UINT };
    result = check_builtin_misc_test_action(table, CBUILTIN_RETURN_ADDRESS, arguments, 1);
    check_builtin_misc_test_expect(test, result.constraint == CHECK_BUILTIN_MISC_CONSTRAINT_CONSTANT && result.action.diagnostic != CHECK_DIAG_NONE,
                                  "frame level must be ICE");
    arguments[0] = check_test_float(BUSTER_TYPE_DOUBLE, 0);
    result = check_builtin_misc_test_action(table, CBUILTIN_FRAME_ADDRESS, arguments, 1);
    check_builtin_misc_test_expect(test, result.constraint == CHECK_BUILTIN_MISC_CONSTRAINT_CONSTANT && result.action.diagnostic == CHECK_DIAG_INCOMPATIBLE,
                                  "implicit floating-to-unsigned parameter conversion does not make a floating argument ICE");

    const TypeId enumeration = type_tag(table, BUSTER_TYPE_KIND_ENUM, 148);
    const bool completed = enumeration != 0 && type_enum_complete(table, enumeration, BUSTER_TYPE_INT, 0);
    check_builtin_misc_test_expect(test, completed, "temporal completion fixture allocation");
    if (completed)
    {
        uint32_t completions[512] = { 0 };
        const uint32_t tag = table->types.index[type_index(enumeration)];
        completions[tag] = 20;
        arguments[0] = check_test_integer(enumeration, 1);
        arguments[1] = check_test_integer(BUSTER_TYPE_INT, 1);
        CheckBuiltinMiscAction action = {
            .id = CBUILTIN_EXPECT,
            .arguments = arguments,
            .argument_count = 2,
            .node = 999,
            .tag_completions = completions,
            .tag_completion_count = BUSTER_ARRAY_LENGTH(completions),
            .source_position = 19,
        };
        check_builtin_misc_one(table, &action, &result);
        check_builtin_misc_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPLETE, "late enum completion is not visible at earlier source position");
        action.source_position = 20;
        check_builtin_misc_one(table, &action, &result);
        check_builtin_misc_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.integer == 1,
                                      "enum completion and diagnostic node use distinct unsigned source fields");
    }
}

static void check_builtin_misc_test_pages(CheckBuiltinMiscTest* test, TypeTable* table)
{
    enum { MAXIMUM = 17 };
    const long page_value = sysconf(_SC_PAGESIZE);
    const size_t page = page_value > 0 ? (size_t)page_value : 0;
    const size_t input_region = page != 0 ? ((MAXIMUM * sizeof(CheckBuiltinMiscAction) + 16 + page - 1) / page) * page : 0;
    const size_t output_region = page != 0 ? ((MAXIMUM * sizeof(CheckBuiltinMiscResult) + 16 + page - 1) / page) * page : 0;
    const size_t argument_region = page != 0 ? ((3 * MAXIMUM * sizeof(CheckValue) + 16 + page - 1) / page) * page : 0;
    const size_t size = input_region + output_region + argument_region + 3 * page;
    uint8_t* mapping = page != 0 ? mmap(0, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    bool ready = mapping != MAP_FAILED;
    uint8_t* input_end = ready ? mapping + input_region : NULL;
    uint8_t* output_end = ready ? input_end + page + output_region : NULL;
    uint8_t* argument_end = ready ? output_end + page + argument_region : NULL;
    if (ready)
    {
        ready = mprotect(input_end, page, PROT_NONE) == 0 && mprotect(output_end, page, PROT_NONE) == 0 &&
                mprotect(argument_end, page, PROT_NONE) == 0;
    }
    check_builtin_misc_test_expect(test, ready, "guarded action/result/argument slices allocated");
    const TypeId pointer = type_pointer(table, BUSTER_TYPE_CHAR);
    for (uint32_t count = 0; ready && count <= MAXIMUM; count += 1)
    {
        uint32_t total_arguments = 0;
        for (uint32_t i = 0; i < count; i += 1)
        {
            total_arguments += i % 3 == 0 ? 2 : i % 3 == 1 ? 1 : 3;
        }
        CheckBuiltinMiscAction* actions = (CheckBuiltinMiscAction*)(input_end - count * sizeof(*actions));
        CheckBuiltinMiscResult* results = (CheckBuiltinMiscResult*)(output_end - count * sizeof(*results));
        CheckValue* arguments = (CheckValue*)(argument_end - total_arguments * sizeof(*arguments));
        memset((uint8_t*)actions - 16, 0xa5, 16 + count * sizeof(*actions));
        memset((uint8_t*)results - 16, 0xa5, 16 + count * sizeof(*results));
        memset((uint8_t*)arguments - 16, 0xa5, 16 + total_arguments * sizeof(*arguments));
        uint32_t cursor = 0;
        for (uint32_t i = 0; i < count; i += 1)
        {
            const uint32_t arity = i % 3 == 0 ? 2 : i % 3 == 1 ? 1 : 3;
            const CBuiltinId id = i % 3 == 0 ? CBUILTIN_EXPECT : i % 3 == 1 ? CBUILTIN_STRLEN : CBUILTIN_MEMSET;
            for (uint32_t arg = 0; arg < arity; arg += 1)
            {
                arguments[cursor + arg] = arg == 0 && id != CBUILTIN_EXPECT ? check_builtin_misc_test_pointer(pointer, i + 1, 0) :
                                             check_test_integer(BUSTER_TYPE_INT, i + arg);
            }
            actions[i] = (CheckBuiltinMiscAction){
                .id = id,
                .arguments = arguments + cursor,
                .argument_count = arity,
                .node = i,
            };
            cursor += arity;
        }
        CheckBuiltinMiscResult expected[MAXIMUM];
        check_builtin_misc(table, actions, expected, count);
        check_builtin_misc(table, actions, results, count);
        for (uint32_t i = 0; i < count; i += 1)
        {
            check_builtin_misc_test_expect(test, check_builtin_misc_test_equal(expected + i, results + i), "guarded ordered batch changed semantic output");
        }
        for (uint32_t partition = 1; partition <= 5; partition += 1)
        {
            for (uint32_t first = 0; first < count; first += partition)
            {
                const uint32_t remaining = count - first < partition ? count - first : partition;
                check_builtin_misc(table, actions + first, results + first, remaining);
            }
            for (uint32_t i = 0; i < count; i += 1)
            {
                check_builtin_misc_test_expect(test, check_builtin_misc_test_equal(expected + i, results + i), "legal batch partition changed semantic output");
            }
        }
        for (uint32_t i = 1; i <= 16; i += 1)
        {
            check_builtin_misc_test_expect(test, *((uint8_t*)actions - i) == 0xa5 && *((uint8_t*)results - i) == 0xa5 &&
                                          *((uint8_t*)arguments - i) == 0xa5, "misc batch wrote before a logical slice");
        }
    }
    if (ready)
    {
        const CheckBuiltinMiscAction invalid = { .id = CBUILTIN_EXPECT, .arguments = (const CheckValue*)argument_end, .argument_count = 3 };
        CheckBuiltinMiscResult result;
        check_builtin_misc_one(table, &invalid, &result);
        check_builtin_misc_test_expect(test, result.action.diagnostic == CHECK_DIAG_ARGUMENT_COUNT, "bad arity does not inspect inaccessible argument memory");
    }
    check_builtin_misc(NULL, NULL, NULL, 0);
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, size);
    }
}

static void check_builtin_misc_test_clang(CheckBuiltinMiscTest* test, const char* clang)
{
    static const struct
    {
        const char* source;
        bool rejected;
    } cases[] =
    {
        { "_Static_assert(_Generic(__builtin_expect(1, 2), long:1, default:0), \"long result\");\n", false },
        { "long f(int x) { return __builtin_expect(7, x++); }\n", false },
        { "int x; _Static_assert(__builtin_expect(3, x) == 3, \"unknown expected value\");\n", false },
        { "int x; _Static_assert(__builtin_expect(3, x++) == 3, \"effectful expected value\");\n", false },
        { "_Static_assert(__builtin_expect(3, 1 / 0) == 3, \"faulting expected value\");\n", false },
        { "int x; long y = __builtin_expect(3, x++);\n", false },
        { "long y = __builtin_expect(3, 1 / 0);\n", false },
        { "long f(int x) { return __builtin_expect(x); }\n", true },
        { "long f(int* x) { return __builtin_expect(x, 1); }\n", true },
        { "void f(void) { __builtin_trap(); } void g(void) { __builtin_unreachable(); }\n", false },
        { "void f(void) { __builtin_trap(1); }\n", true },
        { "void f(void) { __builtin_unreachable(1); }\n", true },
        { "void f(void* p) { __builtin_prefetch(p); __builtin_prefetch(p, 1); __builtin_prefetch(p, 1, 3); }\n", false },
        { "void f(void* p) { __builtin_prefetch(p, 2); }\n", true },
        { "void f(void* p) { __builtin_prefetch(p, 0, 4); }\n", true },
        { "void f(void* p, int rw) { __builtin_prefetch(p, rw); }\n", true },
        { "void f(void* p) { __builtin_prefetch(p, 1.0); }\n", true },
        { "void f(void* p) { __builtin_prefetch(p, 0, 0, 0); }\n", true },
        { "void* f(int* p, int offset) { return __builtin_assume_aligned(p, 16, offset); }\n", false },
        { "void* f(void) { return __builtin_assume_aligned(0, 16); }\n", true },
        { "void* f(void* p) { return __builtin_assume_aligned(p, 3); }\n", true },
        { "void* f(void* p) { return __builtin_assume_aligned(p, 16.0); }\n", true },
        { "void* f(void* p) { return __builtin_assume_aligned(p, (-2147483647 - 1)); }\n", false },
        { "void* f(void* p, int a) { return __builtin_assume_aligned(p, a); }\n", true },
        { "void* f(const int* p, const int* q) { return __builtin_memcpy(p, q, 4); }\n", false },
        { "void* f(void* p, const void* q) { return __builtin_memmove(p, q, 4.5); }\n", false },
        { "void* f(void* p) { return __builtin_memset(p, 257.75, 4); }\n", false },
        { "int f(const void* p, const void* q) { return __builtin_memcmp(p, q, 4); }\n", false },
        { "__SIZE_TYPE__ f(const char* p) { return __builtin_strlen(p); }\n", false },
        { "__SIZE_TYPE__ f(unsigned char* p) { return __builtin_strlen(p); }\n", false },
        { "__SIZE_TYPE__ f(int* p) { return __builtin_strlen(p); }\n", true },
        { "void* f(void) { return __builtin_alloca(19.75); }\n", false },
        { "void* f(void) { return __builtin_frame_address(0); }\n", false },
        { "void* f(void) { return __builtin_frame_address(0.0); }\n", true },
        { "void* f(void) { return __builtin_frame_address((unsigned)0.0); }\n", false },
        { "void* f(void) { return __builtin_return_address(1); }\n", false },
        { "void* f(unsigned x) { return __builtin_frame_address(x); }\n", true },
        { "void* f(void) { return __builtin_frame_address(65536); }\n", true },
        { "void* f(void) { return __builtin_return_address(4294967296ULL); }\n", false },
        { "void* f(const int* p) { return __builtin_extract_return_addr(p); }\n", false },
        { "void* f(void* p) { return __builtin_frob_return_addr(p); }\n", false },
    };
    char directory[] = "/tmp/buster-check-builtin-misc-XXXXXX";
    bool ready = mkdtemp(directory) != NULL;
    char paths[BUSTER_ARRAY_LENGTH(cases)][160];
    char* pointers[BUSTER_ARRAY_LENGTH(cases)];
    ParseTestClang results[BUSTER_ARRAY_LENGTH(cases)];
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        pointers[i] = paths[i];
        ready = parse_test_write(paths[i], cases[i].source, strlen(cases[i].source));
    }
    const char* arguments[] = { "-std=gnu17" };
    ready = ready && parse_test_clang(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), pointers, BUSTER_ARRAY_LENGTH(cases), results);
    check_builtin_misc_test_expect(test, ready, "independent Clang misc prototype/constraint oracle ran");
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        check_builtin_misc_test_expect(test, results[i].rejected == cases[i].rejected, "Clang agrees with original misc source constraint fixture");
    }
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        unlink(paths[i]);
    }
    rmdir(directory);
}

static __attribute__((unused)) int run_check_builtin_misc_tests(int argc, char** argv)
{
    const bool epoch = oracle_epoch_begin();
    CheckBuiltinMiscTest test = { 0 };
    const char* clang = "clang";
    bool options = true;
    for (int i = 0; options && i < argc; i += 1)
    {
        if (strcmp(argv[i], "--clang") == 0 && i + 1 < argc)
        {
            clang = argv[++i];
            options = *clang != 0;
        }
        else
        {
            options = false;
        }
    }
    check_builtin_misc_test_expect(&test, options, "usage: test-check-builtins-misc [--clang PATH]");
    CheckTestTypes types = { 0 };
    if (options)
    {
        const bool initialized = check_test_types_open(&types);
        check_builtin_misc_test_expect(&test, initialized, "type-table allocation");
        if (initialized)
        {
            check_builtin_misc_test_values(&test, &types.table);
            check_builtin_misc_test_constraints(&test, &types.table);
            check_builtin_misc_test_pages(&test, &types.table);
        }
        check_test_types_close(&types);
        check_builtin_misc_test_clang(&test, clang);
    }
    printf("test-check-builtins-misc: %" PRIu64 " checks, %" PRIu64 " failures\n", test.checks, test.failures);
    oracle_cache_report(oracle_cache(), "test-check-builtins-misc");
    oracle_epoch_end(epoch);
    return test.failures != 0;
}
