// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Typed builtin fixtures for #148. Include after c_builtin_test.c and c_checker_test.c: their Clang numeric oracle,
// type-table setup and fieldwise value comparisons are shared test helpers. These are action tests, not a claim
// that Buster compiles their C source. Separate external Clang sources verify the prototype and constraint rules.
// Every fixture/source here is original; no compiler implementation or external test corpus is incorporated.

typedef struct CheckBuiltinTest CheckBuiltinTest;
struct CheckBuiltinTest
{
    uint64_t checks;
    uint64_t failures;
    uint64_t random;
};

typedef struct CheckBuiltinTestType CheckBuiltinTestType;
struct CheckBuiltinTestType
{
    TypeId type;
    uint32_t descriptor;
};

static void check_builtin_test_expect(CheckBuiltinTest* test, bool condition, const char* description)
{
    test->checks += 1;
    if (!condition)
    {
        test->failures += 1;
        if (test->failures <= 24)
        {
            fprintf(stderr, "test-check-builtins: %s\n", description);
        }
    }
}

static bool check_builtin_test_equal(const CheckBuiltinResult* a, const CheckBuiltinResult* b)
{
    return check_test_action_equal(&a->action, &b->action) && check_test_value_equal(&a->store_value, &b->store_value) &&
           check_test_value_equal(&a->store_destination, &b->store_destination) && a->fold_status == b->fold_status && a->store == b->store;
}

static CheckBuiltinResult check_builtin_test_action(CheckBuiltinTest* test, TypeTable* table, CheckBuiltinAction action)
{
    CheckBuiltinResult scalar;
    CheckBuiltinAction actions[9];
    CheckBuiltinResult vector[9];
    check_builtins(table, &action, &scalar, 1, true);
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(actions); i += 1)
    {
        actions[i] = action;
    }
    check_builtins(table, actions, vector, BUSTER_ARRAY_LENGTH(actions), false);
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(actions); i += 1)
    {
        check_builtin_test_expect(test, check_builtin_test_equal(&scalar, vector + i), "fixture differs in full/partial vector batch");
    }
    return scalar;
}

static void check_builtin_test_fixtures(CheckBuiltinTest* test, TypeTable* table)
{
    CheckBuiltinAction action = {
        .id = CBUILTIN_BSWAP16,
        .operands = {
            check_test_integer(BUSTER_TYPE_INT, 0x123456),
        },
        .argument_count = 1,
        .node = 77,
    };
    CheckBuiltinResult result = check_builtin_test_action(test, table, action);
    check_builtin_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == BUSTER_TYPE_USHORT &&
                              result.action.value.integer == 0x5634 && result.action.conversions[0].type == BUSTER_TYPE_USHORT &&
                              (result.action.value.flags & BUSTER_CHECK_ICE) != 0, "bswap truncates before reversing and returns its unsigned width");
    action.id = CBUILTIN_BSWAP64;
    result = check_builtin_test_action(test, table, action);
    check_builtin_test_expect(test, result.action.value.type == BUSTER_TYPE_ULONG, "LP64 bswap64 has target uint64_t type");
    action.id = CBUILTIN_POPCOUNT;
    action.operands[0] = check_test_float(BUSTER_TYPE_DOUBLE, 3.75L);
    result = check_builtin_test_action(test, table, action);
    check_builtin_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.integer == 2 &&
                              result.action.conversions[0].type == BUSTER_TYPE_UINT && (result.action.value.flags & BUSTER_CHECK_ICE) != 0,
                              "ordinary unary prototype converts a floating argument before folding");
    action.operands[0] = check_test_integer(BUSTER_TYPE_INT, 3);
    action.operands[0].flags = 0;
    result = check_builtin_test_action(test, table, action);
    check_builtin_test_expect(test, (result.action.value.flags & BUSTER_CHECK_ICE) != 0 && result.action.value.integer == 2,
                              "builtin constant evaluation admits a folded comma-like argument");
    action.operands[0].flags = BUSTER_CHECK_SIDE_EFFECT;
    result = check_builtin_test_action(test, table, action);
    check_builtin_test_expect(test, result.action.value.integer == 2 && (result.action.value.flags & BUSTER_CHECK_SIDE_EFFECT) != 0 &&
                              (result.action.value.flags & BUSTER_CHECK_ICE) == 0, "known unary result retains argument effects and is not ICE");
    action.operands[0] = check_test_integer(BUSTER_TYPE_INT, UINT32_MAX);
    action.id = CBUILTIN_FFS;
    result = check_builtin_test_action(test, table, action);
    check_builtin_test_expect(test, result.action.value.integer == 1 && result.action.conversions[0].type == BUSTER_TYPE_INT,
                              "ffs converts to its signed prototype parameter");
    const CBuiltinId undefined[] = {
        CBUILTIN_CLZ,
        CBUILTIN_CLZL,
        CBUILTIN_CLZLL,
        CBUILTIN_CTZ,
        CBUILTIN_CTZL,
        CBUILTIN_CTZLL,
    };
    action.operands[0] = check_test_integer(BUSTER_TYPE_INT, 0);
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(undefined); i += 1)
    {
        action.id = undefined[i];
        result = check_builtin_test_action(test, table, action);
        check_builtin_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.fold_status == CBUILTIN_UNDEFINED &&
                                  result.action.value.constant_kind == CHECK_CONSTANT_NONE && result.action.value.fault == CHECK_FAULT_UNSUPPORTED_CONSTANT &&
                                  result.action.value.fault_node == action.node && (result.action.value.flags & BUSTER_CHECK_ICE) == 0 &&
                                  (result.action.value.flags & BUSTER_CHECK_ICE_UNEVALUATED) != 0,
                                  "zero search retains a deferred evaluation fault instead of a folded width");
        const CheckAction conditional = {
            .op = CHECK_OP_CONDITIONAL,
            .operands = {
                check_test_integer(BUSTER_TYPE_INT, 1),
                check_test_integer(BUSTER_TYPE_INT, 3),
                result.action.value,
            },
        };
        CheckActionResult selected;
        check_action_scalar(table, &conditional, &selected);
        check_builtin_test_expect(test, selected.diagnostic == CHECK_DIAG_NONE && selected.value.fault == CHECK_FAULT_NONE &&
                                  selected.value.integer == 3 && (selected.value.flags & BUSTER_CHECK_ICE) != 0,
                                  "an unevaluated undefined builtin does not invalidate the selected constant arm");
    }
    action.id = CBUILTIN_POPCOUNT;
    action.operands[0] = check_test_integer(BUSTER_TYPE_INT, 1);
    action.operands[0].fault = CHECK_FAULT_DIVIDE_BY_ZERO;
    action.operands[0].fault_node = 19;
    result = check_builtin_test_action(test, table, action);
    check_builtin_test_expect(test, result.action.value.fault == CHECK_FAULT_DIVIDE_BY_ZERO && result.action.value.fault_node == 19 &&
                              result.action.value.constant_kind == CHECK_CONSTANT_NONE, "input evaluation fault keeps its original node");
    const TypeId pointer = type_pointer(table, BUSTER_TYPE_SCHAR);
    action = (CheckBuiltinAction){
        .id = CBUILTIN_ADD_OVERFLOW,
        .operands = {
            check_test_integer(BUSTER_TYPE_UCHAR, 255),
            check_test_integer(BUSTER_TYPE_INT, 1),
            {
                .type = pointer,
                .category = CHECK_RVALUE,
                .flags = BUSTER_CHECK_SIDE_EFFECT,
                .constant_kind = CHECK_CONSTANT_ADDRESS,
                .object = 51,
                .offset = 9,
            },
        },
        .argument_count = 3,
        .node = 81,
    };
    result = check_builtin_test_action(test, table, action);
    check_builtin_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == BUSTER_TYPE_BOOL &&
                              result.action.value.integer == 1 && result.store && result.store_value.type == BUSTER_TYPE_SCHAR &&
                              result.store_value.integer == 0 && result.store_destination.object == 51 && result.store_destination.offset == 9 &&
                              (result.action.value.flags & BUSTER_CHECK_SIDE_EFFECT) != 0 && (result.action.value.flags & BUSTER_CHECK_ICE) == 0,
                              "overflow keeps original operand shapes, modulo stored bits, boolean and destination effects");
    check_builtin_test_expect(test, result.action.conversions[0].type == BUSTER_TYPE_UCHAR && result.action.conversions[1].type == BUSTER_TYPE_INT &&
                              (result.action.conversions[0].flags & BUSTER_CHECK_CONVERT_PROMOTION) == 0 &&
                              result.action.store_conversion.type == BUSTER_TYPE_SCHAR, "overflow arguments do not take usual arithmetic conversions");
    action.operands[0].category = CHECK_LVALUE;
    action.operands[0].constant_kind = CHECK_CONSTANT_ADDRESS;
    action.operands[0].flags = 0;
    result = check_builtin_test_action(test, table, action);
    check_builtin_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.fold_status == BUSTER_CHECK_BUILTIN_NOT_CONSTANT &&
                              result.store && result.store_value.type == BUSTER_TYPE_SCHAR && result.store_value.constant_kind == CHECK_CONSTANT_NONE &&
                              (result.action.conversions[0].flags & BUSTER_CHECK_CONVERT_LVALUE) != 0, "runtime overflow retains a typed store without inventing constant bits");
    action.operands[0] = check_test_integer(BUSTER_TYPE_BOOL, 1);
    action.operands[1] = check_test_integer(BUSTER_TYPE_BOOL, 1);
    action.operands[2].type = type_pointer(table, BUSTER_TYPE_BOOL);
    result = check_builtin_test_action(test, table, action);
    check_builtin_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.integer == 1 && result.store_value.integer == 0,
                              "generic bool destination stores the truncated low bit on overflow");
    const TypeId enumeration = type_tag(table, BUSTER_TYPE_KIND_ENUM, 148);
    check_builtin_test_expect(test, type_enum_complete(table, enumeration, BUSTER_TYPE_UINT, 0), "enum fixture construction");
    action.operands[0] = check_test_integer(enumeration, UINT32_MAX);
    action.operands[1] = check_test_integer(BUSTER_TYPE_BOOL, 1);
    action.operands[2].type = type_pointer(table, enumeration);
    result = check_builtin_test_action(test, table, action);
    check_builtin_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.integer == 1 && result.store_value.type == enumeration &&
                              result.store_value.integer == 0 && result.action.conversions[0].type == enumeration,
                              "generic enum operands and destination retain their actual type");
    uint32_t completions[8] = { 0 };
    const uint32_t tag = table->types.index[type_index(enumeration)];
    if (tag < BUSTER_ARRAY_LENGTH(completions))
    {
        completions[tag] = 100;
        action.tag_completions = completions;
        action.tag_completion_count = BUSTER_ARRAY_LENGTH(completions);
        action.source_position = 99;
        result = check_builtin_test_action(test, table, action);
        check_builtin_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPLETE && !result.store,
                                  "later enum completion cannot validate an earlier overflow operand");
        action.operands[0] = check_test_integer(BUSTER_TYPE_UINT, UINT32_MAX);
        result = check_builtin_test_action(test, table, action);
        check_builtin_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPLETE && result.action.diagnostic_operand == 2 && !result.store,
                                  "later enum completion cannot validate an earlier overflow destination");
        action.source_position = 100;
        result = check_builtin_test_action(test, table, action);
        check_builtin_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.store,
                                  "enum is usable at the retained completion cutoff");
        action.id = CBUILTIN_POPCOUNT;
        action.argument_count = 1;
        action.operands[0] = check_test_integer(enumeration, 3);
        action.source_position = 99;
        result = check_builtin_test_action(test, table, action);
        check_builtin_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPLETE,
                                  "later enum completion cannot validate an earlier unary conversion");
        action.id = CBUILTIN_ADD_OVERFLOW;
        action.argument_count = 3;
        action.tag_completions = NULL;
    }
    else
    {
        check_builtin_test_expect(test, false, "source-history fixture tag capacity");
    }
    const TypeId qualified = type_qualify(table, BUSTER_TYPE_INT, BUSTER_TYPE_VOLATILE);
    action.operands[0] = check_test_integer(BUSTER_TYPE_INT, 1);
    action.operands[1] = check_test_integer(BUSTER_TYPE_INT, 2);
    action.operands[2].type = type_pointer(table, qualified);
    result = check_builtin_test_action(test, table, action);
    check_builtin_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.store_value.integer == 3 &&
                              result.action.store_conversion.type == qualified && result.store_destination.type == action.operands[2].type,
                              "volatile destination qualification survives a successful fold");
    action.operands[2].type = type_array(table, BUSTER_TYPE_INT, 1);
    result = check_builtin_test_action(test, table, action);
    check_builtin_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.store &&
                              (result.action.conversions[2].flags & BUSTER_CHECK_CONVERT_ARRAY) != 0, "destination array decays once to its pointer");
}

static void check_builtin_test_constraints(CheckBuiltinTest* test, TypeTable* table)
{
    CheckBuiltinAction action = {
        .id = CBUILTIN_ADD_OVERFLOW,
        .operands = {
            check_test_integer(BUSTER_TYPE_INT, 1),
            check_test_integer(BUSTER_TYPE_INT, 2),
            {
                .type = type_pointer(table, BUSTER_TYPE_INT),
                .category = CHECK_RVALUE,
            },
        },
        .argument_count = 3,
    };
    const TypeId invalid_destinations[] = {
        type_pointer(table, type_qualify(table, BUSTER_TYPE_INT, BUSTER_TYPE_CONST)),
        type_pointer(table, BUSTER_TYPE_DOUBLE),
        type_pointer(table, type_atomic(table, BUSTER_TYPE_INT)),
        type_pointer(table, BUSTER_TYPE_VOID),
        BUSTER_TYPE_INT,
        0,
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(invalid_destinations); i += 1)
    {
        action.operands[2].type = invalid_destinations[i];
        const CheckBuiltinResult result = check_builtin_test_action(test, table, action);
        check_builtin_test_expect(test, result.action.diagnostic != CHECK_DIAG_NONE && result.action.diagnostic_operand == 2 &&
                                  !result.store && result.action.value.constant_kind == CHECK_CONSTANT_NONE, "invalid destination rejects without publishing a store");
    }
    action.operands[2].type = type_pointer(table, BUSTER_TYPE_INT);
    action.operands[0] = check_test_float(BUSTER_TYPE_DOUBLE, 1);
    CheckBuiltinResult result = check_builtin_test_action(test, table, action);
    check_builtin_test_expect(test, result.action.diagnostic == CHECK_DIAG_INTEGER_REQUIRED && result.action.diagnostic_operand == 0,
                              "generic overflow rejects floating input instead of converting it");
    action.operands[0] = check_test_integer(BUSTER_TYPE_UINT128, (unsigned __int128)1 << 100);
    result = check_builtin_test_action(test, table, action);
    check_builtin_test_expect(test, result.action.diagnostic == CHECK_DIAG_UNSUPPORTED && result.fold_status == CBUILTIN_UNSUPPORTED && !result.store,
                              "wide overflow input rejects explicitly before descriptor truncation");
    action.operands[0] = check_test_integer(BUSTER_TYPE_INT, 1);
    action.operands[2].type = type_pointer(table, type_bit_int(table, 65, true));
    result = check_builtin_test_action(test, table, action);
    check_builtin_test_expect(test, result.action.diagnostic == CHECK_DIAG_UNSUPPORTED && !result.store, "wide overflow destination is unsupported");
    action.operands[2].type = type_pointer(table, BUSTER_TYPE_INT);
    for (uint32_t count = 0; count <= 4; count += 1)
    {
        action.argument_count = count;
        result = check_builtin_test_action(test, table, action);
        check_builtin_test_expect(test, (result.action.diagnostic == CHECK_DIAG_ARGUMENT_COUNT) == (count != 3), "overflow fixed arity check");
    }
    action.id = CBUILTIN_POPCOUNT;
    for (uint32_t count = 0; count <= 3; count += 1)
    {
        action.argument_count = count;
        result = check_builtin_test_action(test, table, action);
        check_builtin_test_expect(test, (result.action.diagnostic == CHECK_DIAG_ARGUMENT_COUNT) == (count != 1), "unary fixed arity check");
    }
    action.argument_count = 1;
    action.operands[0].type = type_pointer(table, BUSTER_TYPE_INT);
    result = check_builtin_test_action(test, table, action);
    check_builtin_test_expect(test, result.action.diagnostic == CHECK_DIAG_ARITHMETIC_REQUIRED, "unary rejects pointer input");
    action.id = (CBuiltinId)UINT32_MAX;
    result = check_builtin_test_action(test, table, action);
    check_builtin_test_expect(test, result.action.diagnostic == CHECK_DIAG_UNSUPPORTED, "invalid full builtin ID cannot alias a recognized action");
}

static void check_builtin_test_batches(CheckBuiltinTest* test, TypeTable* table)
{
    const CheckBuiltinTestType types[] = {
        { BUSTER_TYPE_BOOL, 1 },
        { BUSTER_TYPE_SCHAR, 8 | BUSTER_CBUILTIN_SIGNED },
        { BUSTER_TYPE_UCHAR, 8 },
        { BUSTER_TYPE_SHORT, 16 | BUSTER_CBUILTIN_SIGNED },
        { BUSTER_TYPE_USHORT, 16 },
        { BUSTER_TYPE_INT, 32 | BUSTER_CBUILTIN_SIGNED },
        { BUSTER_TYPE_UINT, 32 },
        { BUSTER_TYPE_LONG, 64 | BUSTER_CBUILTIN_SIGNED },
        { BUSTER_TYPE_ULONG, 64 },
        { BUSTER_TYPE_LLONG, 64 | BUSTER_CBUILTIN_SIGNED },
        { BUSTER_TYPE_ULLONG, 64 },
        { type_bit_int(table, 3, false), 3 | BUSTER_CBUILTIN_SIGNED },
        { type_bit_int(table, 7, true), 7 },
        { type_bit_int(table, 33, false), 33 | BUSTER_CBUILTIN_SIGNED },
        { type_bit_int(table, 63, true), 63 },
    };
    TypeId pointers[BUSTER_ARRAY_LENGTH(types)];
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(types); i += 1)
    {
        pointers[i] = type_pointer(table, types[i].type);
    }
    enum { COUNT = 257 };
    CheckBuiltinAction actions[COUNT];
    CheckBuiltinResult scalar[COUNT];
    CheckBuiltinResult vector[COUNT];
    uint32_t a_types[COUNT];
    uint32_t b_types[COUNT];
    uint32_t r_types[COUNT];
    for (uint32_t i = 0; i < COUNT; i += 1)
    {
        const uint32_t a = cbuiltin_test_random(&test->random) % BUSTER_ARRAY_LENGTH(types);
        const uint32_t b = cbuiltin_test_random(&test->random) % BUSTER_ARRAY_LENGTH(types);
        const uint32_t r = cbuiltin_test_random(&test->random) % BUSTER_ARRAY_LENGTH(types);
        const CBuiltinId id = (CBuiltinId)(1 + i % (CBUILTIN_COUNT - 1));
        a_types[i] = types[a].descriptor;
        b_types[i] = types[b].descriptor;
        r_types[i] = types[r].descriptor;
        actions[i] = (CheckBuiltinAction){
            .id = id,
            .operands = {
                check_test_integer(types[a].type, cbuiltin_test_random(&test->random)),
                check_test_integer(types[b].type, cbuiltin_test_random(&test->random)),
                {
                    .type = pointers[r],
                    .category = CHECK_RVALUE,
                    .constant_kind = CHECK_CONSTANT_ADDRESS,
                    .object = 1 + i,
                },
            },
            .argument_count = check_builtin_overflow(id) ? 3 : 1,
            .node = i,
        };
        if (!check_builtin_overflow(id))
        {
            actions[i].operands[0] = check_test_integer(BUSTER_TYPE_ULLONG, actions[i].operands[0].integer | 1);
        }
        if (i % 29 == 0)
        {
            actions[i].operands[0].constant_kind = CHECK_CONSTANT_NONE;
        }
        if (i % 31 == 0)
        {
            actions[i].argument_count = 7;
        }
    }
    check_builtins(table, actions, scalar, COUNT, true);
    check_builtins(table, actions, vector, COUNT, false);
    for (uint32_t i = 0; i < COUNT; i += 1)
    {
        check_builtin_test_expect(test, check_builtin_test_equal(scalar + i, vector + i), "mixed scalar/vector typed action mismatch");
        if (scalar[i].fold_status == CBUILTIN_FOLDED)
        {
            uint64_t expected = 0;
            if (check_builtin_overflow(actions[i].id))
            {
                const __int128 a = cbuiltin_test_integer((uint64_t)actions[i].operands[0].integer, a_types[i]);
                const __int128 b = cbuiltin_test_integer((uint64_t)actions[i].operands[1].integer, b_types[i]);
                const uint32_t overflow = cbuiltin_test_overflow(actions[i].id, a, b, r_types[i], &expected);
                check_builtin_test_expect(test, scalar[i].store && scalar[i].store_value.integer == expected && scalar[i].action.value.integer == overflow,
                                          "typed overflow differs from Clang's independent numeric oracle");
            }
            else
            {
                expected = cbuiltin_test_bits(actions[i].id, (uint64_t)actions[i].operands[0].integer);
                check_builtin_test_expect(test, scalar[i].action.value.integer == expected, "typed unary differs from Clang's builtin oracle");
            }
        }
    }
    for (uint32_t partition = 1; partition <= 17; partition += 1)
    {
        memset(vector, 0xa5, sizeof(vector));
        for (uint32_t first = 0; first < COUNT;)
        {
            const uint32_t count = COUNT - first < partition ? COUNT - first : partition;
            check_builtins(table, actions + first, vector + first, count, false);
            first += count;
        }
        for (uint32_t i = 0; i < COUNT; i += 1)
        {
            check_builtin_test_expect(test, check_builtin_test_equal(scalar + i, vector + i), "legal typed-action partition changed its semantic output");
        }
    }
}

static void check_builtin_test_pages(CheckBuiltinTest* test, TypeTable* table)
{
    const long page_value = sysconf(_SC_PAGESIZE);
    const size_t page = page_value > 0 ? (size_t)page_value : 0;
    const size_t input_bytes = 33 * sizeof(CheckBuiltinAction);
    const size_t output_bytes = 33 * sizeof(CheckBuiltinResult);
    const size_t input_region = page != 0 ? ((input_bytes + 32 + page - 1) / page) * page : 0;
    const size_t output_region = page != 0 ? ((output_bytes + 32 + page - 1) / page) * page : 0;
    const size_t size = input_region + output_region + 2 * page;
    uint8_t* mapping = page != 0 ? mmap(0, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    bool ready = mapping != MAP_FAILED;
    if (ready)
    {
        ready = mprotect(mapping + input_region, page, PROT_NONE) == 0 &&
                mprotect(mapping + input_region + page + output_region, page, PROT_NONE) == 0;
    }
    check_builtin_test_expect(test, ready, "guarded action/result allocation");
    for (uint32_t count = 0; ready && count <= 33; count += 1)
    {
        CheckBuiltinAction* actions = (CheckBuiltinAction*)(mapping + input_region - count * sizeof(*actions));
        CheckBuiltinResult* results = (CheckBuiltinResult*)(mapping + input_region + page + output_region - count * sizeof(*results));
        CheckBuiltinResult expected[33];
        memset((uint8_t*)actions - 16, 0xa5, 16 + count * sizeof(*actions));
        memset((uint8_t*)results - 16, 0xa5, 16 + count * sizeof(*results));
        for (uint32_t i = 0; i < count; i += 1)
        {
            actions[i] = (CheckBuiltinAction){
                .id = (CBuiltinId)(CBUILTIN_CLZ + i % 15),
                .operands = {
                    check_test_integer(BUSTER_TYPE_UINT, i),
                },
                .argument_count = i % 9 == 0 ? 0 : 1,
                .node = i,
            };
        }
        check_builtins(table, actions, expected, count, true);
        check_builtins(table, actions, results, count, false);
        for (uint32_t i = 0; i < count; i += 1)
        {
            check_builtin_test_expect(test, check_builtin_test_equal(expected + i, results + i), "guarded typed tail differs from scalar");
        }
        for (uint32_t i = 1; i <= 16; i += 1)
        {
            check_builtin_test_expect(test, *((uint8_t*)actions - i) == 0xa5 && *((uint8_t*)results - i) == 0xa5, "typed batch wrote before its logical slice");
        }
    }
    check_builtins(NULL, NULL, NULL, 0, false);
    check_builtins(NULL, NULL, NULL, 0, true);
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, size);
    }
}

static void check_builtin_test_clang(CheckBuiltinTest* test, const char* clang)
{
    const char* sources[] = {
        "_Static_assert(_Generic(__builtin_bswap16(1), unsigned short:1, default:0), \"bswap16 type\");\n"
        "_Static_assert(_Generic(__builtin_bswap32(1), unsigned int:1, default:0), \"bswap32 type\");\n"
        "_Static_assert(_Generic(__builtin_bswap64(1), unsigned long:1, default:0), \"LP64 bswap64 type\");\n",
        "int f(void);\n_Static_assert(__builtin_popcount(3.75)==2, \"floating conversion\");\n"
        "_Static_assert(__builtin_popcount((1,3))==2, \"folded comma\");\n"
        "_Static_assert(__builtin_popcount(1 ? 3 : f())==2, \"unevaluated call\");\n",
        "enum E { E0, E1 }; void f(void) { _Bool b; enum E e; volatile int v;\n"
        "__builtin_add_overflow(1,1,&b); __builtin_add_overflow(1,1,&e); __builtin_add_overflow((_Bool)1,E1,&v); }\n",
        "void f(void) { int a[1]; int *const p=a; __builtin_mul_overflow(2,3,a); __builtin_sub_overflow(2,3,p); }\n",
        "void f(void) { const int a=1; __builtin_add_overflow(1,2,&a); }\n",
        "void f(void) { _Atomic(int) a; __builtin_add_overflow(1,2,&a); }\n",
        "void f(void) { double a; __builtin_add_overflow(1,2,&a); }\n",
        "void f(void) { int a; __builtin_add_overflow(1.0,2,&a); }\n",
        "void f(void) { __builtin_popcount(1,2); }\n",
        "void f(void) { __builtin_add_overflow(1,2); }\n",
        "void f(void) { __builtin_popcount((int*)0); }\n",
        "enum { A=sizeof(__builtin_ctz(0)), B=1?3:__builtin_clz(0) }; _Static_assert(B==3, \"unevaluated builtin\");\n",
    };
    const bool rejected[] = {
        false,
        false,
        false,
        false,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        false,
    };
    char directory[] = "/tmp/buster-check-builtins-XXXXXX";
    bool ready = mkdtemp(directory) != NULL;
    char paths[BUSTER_ARRAY_LENGTH(sources)][160];
    char* pointers[BUSTER_ARRAY_LENGTH(sources)];
    ParseTestClang results[BUSTER_ARRAY_LENGTH(sources)];
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(sources); i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        pointers[i] = paths[i];
        ready = parse_test_write(paths[i], sources[i], strlen(sources[i]));
    }
    const char* arguments[] = {
        "-std=gnu17",
    };
    ready = ready && parse_test_clang(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), pointers, BUSTER_ARRAY_LENGTH(sources), results);
    check_builtin_test_expect(test, ready, "independent Clang prototype/constraint oracle could not run");
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(sources); i += 1)
    {
        check_builtin_test_expect(test, results[i].rejected == rejected[i], "Clang disagrees with a builtin prototype/constraint fixture");
    }
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(sources); i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        unlink(paths[i]);
    }
    rmdir(directory);
}

static int run_check_builtin_tests(int argc, char** argv)
{
    CheckBuiltinTest test = {
        .random = 148,
    };
    const char* clang = "clang";
    bool options = true;
    for (int i = 0; options && i < argc; i += 1)
    {
        if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
        {
            char* end = NULL;
            const char* value = argv[++i];
            errno = 0;
            test.random = strtoull(value, &end, 10);
            options = errno == 0 && value != end && *end == 0 && *value != '-';
        }
        else if (strcmp(argv[i], "--clang") == 0 && i + 1 < argc)
        {
            clang = argv[++i];
            options = *clang != 0;
        }
        else
        {
            options = false;
        }
    }
    CheckTestTypes types = { 0 };
    check_builtin_test_expect(&test, options, "usage: test-check-builtins [--seed N] [--clang PATH]");
    if (options)
    {
        const bool initialized = check_test_types_open(&types);
        check_builtin_test_expect(&test, initialized, "type-table allocation");
        if (initialized)
        {
            check_builtin_test_fixtures(&test, &types.table);
            check_builtin_test_constraints(&test, &types.table);
            check_builtin_test_batches(&test, &types.table);
            check_builtin_test_pages(&test, &types.table);
        }
        check_test_types_close(&types);
        check_builtin_test_clang(&test, clang);
    }
    printf("test-check-builtins: %" PRIu64 " checks, %" PRIu64 " failures\n", test.checks, test.failures);
    return test.failures != 0;
}
