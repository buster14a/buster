// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original function calling-convention identity tests. Independent Clang spellings check the semantic relation;
// direct tests exercise interning growth, composite reconstruction, exact overload identities and batch consumers.
#ifndef BUSTER_TYPES_FUNCTION_ABI_TEST_IMPLEMENTATION
#define BUSTER_TYPES_FUNCTION_ABI_TEST_IMPLEMENTATION 1

static void check_test_function_abi_relations(CheckTest* test, TypeTable* table)
{
    const TypeId integer = BUSTER_TYPE_INT;
    TypeId functions[TYPE_FUNCTION_ABI_COUNT];
    TypeId oldstyle[TYPE_FUNCTION_ABI_COUNT];
    for (uint32_t abi = 0; abi < TYPE_FUNCTION_ABI_COUNT; abi += 1)
    {
        functions[abi] = type_function_abi(table, integer, &integer, 1, BUSTER_TYPE_FLAG_PROTOTYPE, (TypeFunctionABI)abi);
        oldstyle[abi] = type_function_abi(table, integer, 0, 0, 0, (TypeFunctionABI)abi);
        check_test_expect(test, functions[abi] != 0 && oldstyle[abi] != 0 && type_function_abi_kind(table, functions[abi]) == abi &&
                               type_function_abi_kind(table, oldstyle[abi]) == abi && !type_is_transparent_union(table, functions[abi]),
                          "every supported ABI has explicit prototype and old-style identity without record flags leaking");
    }
    check_test_expect(test, functions[0] == type_function(table, integer, &integer, 1, BUSTER_TYPE_FLAG_PROTOTYPE) &&
                           oldstyle[0] == type_function(table, integer, 0, 0, 0), "existing constructors retain exact default System V identity");
    for (uint32_t left = 0; left < TYPE_FUNCTION_ABI_COUNT; left += 1)
    {
        for (uint32_t right = 0; right < TYPE_FUNCTION_ABI_COUNT; right += 1)
        {
            const bool same = left == right;
            const TypeId lp = type_pointer(table, functions[left]);
            const TypeId rp = type_pointer(table, functions[right]);
            check_test_expect(test, type_same_canonical(table, functions[left], functions[right]) == (same ? TYPE_ANSWER_YES : TYPE_ANSWER_NO) &&
                                   type_compatible(table, lp, rp) == (same ? TYPE_ANSWER_YES : TYPE_ANSWER_NO) &&
                                   type_composite(table, lp, rp) == (same ? lp : 0),
                              "function ABI affects canonical, pointer compatibility and composite identity");
            check_test_expect(test, type_compatible(table, oldstyle[left], functions[right]) == (same ? TYPE_ANSWER_YES : TYPE_ANSWER_NO) &&
                                   type_composite(table, oldstyle[left], functions[right]) == (same ? functions[right] : 0) &&
                                   type_composite(table, functions[right], oldstyle[left]) == (same ? functions[right] : 0),
                              "prototype completion preserves ABI in both declaration orders");
        }
    }
    const TypeId incomplete = type_pointer(table, type_incomplete_array(table, integer));
    const TypeId complete = type_pointer(table, type_array(table, integer, 3));
    const TypeId return_complete = type_pointer(table, type_array(table, integer, 5));
    for (uint32_t abi = 0; abi < TYPE_FUNCTION_ABI_COUNT; abi += 1)
    {
        const TypeId left = type_function_abi(table, return_complete, &incomplete, 1, BUSTER_TYPE_FLAG_PROTOTYPE, (TypeFunctionABI)abi);
        const TypeId right = type_function_abi(table, incomplete, &complete, 1, BUSTER_TYPE_FLAG_PROTOTYPE, (TypeFunctionABI)abi);
        const TypeId expected = type_function_abi(table, return_complete, &complete, 1, BUSTER_TYPE_FLAG_PROTOTYPE, (TypeFunctionABI)abi);
        check_test_expect(test, type_composite(table, left, right) == expected && type_composite(table, right, left) == expected &&
                               expected != left && expected != right && type_function_abi_kind(table, expected) == abi,
                          "a composite that rebuilds both return and parameter types preserves the calling convention");
        const TypeId aligned = type_pointer(table, type_aligned(table, integer, 4));
        const TypeId plain = type_pointer(table, integer);
        const TypeId aligned_function = type_function_abi(table, integer, &aligned, 1, BUSTER_TYPE_FLAG_PROTOTYPE, (TypeFunctionABI)abi);
        const TypeId plain_function = type_function_abi(table, integer, &plain, 1, BUSTER_TYPE_FLAG_PROTOTYPE, (TypeFunctionABI)abi);
        const TypeId other_function = type_function_abi(table, integer, &plain, 1, BUSTER_TYPE_FLAG_PROTOTYPE,
                                                       (TypeFunctionABI)((abi + 1) % TYPE_FUNCTION_ABI_COUNT));
        check_test_expect(test, type_same_canonical(table, aligned_function, plain_function) == TYPE_ANSWER_YES &&
                               type_same_canonical(table, aligned_function, other_function) == TYPE_ANSWER_NO,
                          "nested alignment sugar cannot erase or invent a calling-convention match");
        const TypeId variadic = type_function_abi(table, integer, &integer, 1, BUSTER_TYPE_FLAG_PROTOTYPE | BUSTER_TYPE_FLAG_VARIADIC, (TypeFunctionABI)abi);
        const bool forbidden = abi == TYPE_FUNCTION_ABI_VECTOR || abi == TYPE_FUNCTION_ABI_REG;
        check_test_expect(test, forbidden ? variadic == 0 && table->last_error == TYPE_ERROR_OPERAND :
                                           variadic != 0 && type_function_abi_kind(table, variadic) == abi,
                          "only vectorcall and regcall reject variadic prototypes");
    }
    const uint32_t before_types = table->types.count;
    const uint32_t before_parameters = table->parameters.count;
    check_test_expect(test, type_function_abi(table, integer, &integer, 1, BUSTER_TYPE_FLAG_PROTOTYPE, TYPE_FUNCTION_ABI_INVALID) == 0 &&
                           type_function_abi(table, integer, &integer, 1, BUSTER_TYPE_FLAG_PROTOTYPE, TYPE_FUNCTION_ABI_COUNT) == 0 &&
                           type_function_abi(table, integer, &integer, 1, BUSTER_TYPE_FLAG_COMPLETE, TYPE_FUNCTION_ABI_SYSV) == 0 &&
                           type_function_abi(table, integer, 0, 1, BUSTER_TYPE_FLAG_PROTOTYPE, TYPE_FUNCTION_ABI_SYSV) == 0 &&
                           table->types.count == before_types && table->parameters.count == before_parameters,
                      "invalid ABI requests do not append partial type or parameter records");
    check_test_expect(test, type_function_abi_kind(table, integer) == TYPE_FUNCTION_ABI_INVALID &&
                           type_function_abi_kind(table, type_pointer(table, functions[0])) == TYPE_FUNCTION_ABI_INVALID &&
                           type_function_abi_kind(table, 0) == TYPE_FUNCTION_ABI_INVALID,
                      "ABI queries reject non-function and invalid types explicitly");

    const uint32_t buckets = table->index.bucket_count;
    for (uint32_t i = 0; i < 256; i += 1)
    {
        const TypeId parameter = type_pointer(table, type_array(table, integer, i + 10));
        for (uint32_t abi = 0; abi < TYPE_FUNCTION_ABI_COUNT; abi += 1)
        {
            const TypeId type = type_function_abi(table, integer, &parameter, 1, BUSTER_TYPE_FLAG_PROTOTYPE, (TypeFunctionABI)abi);
            check_test_expect(test, type != 0 && type_function_abi_kind(table, type) == abi, "ABI keys survive repeated index growth");
        }
    }
    check_test_expect(test, table->index.bucket_count > buckets, "ABI interning controls actually rebuild the hash index");
    for (uint32_t abi = 0; abi < TYPE_FUNCTION_ABI_COUNT; abi += 1)
    {
        check_test_expect(test, type_function_abi(table, integer, &integer, 1, BUSTER_TYPE_FLAG_PROTOTYPE, (TypeFunctionABI)abi) == functions[abi] &&
                               type_function_abi(table, integer, 0, 0, 0, (TypeFunctionABI)abi) == oldstyle[abi],
                          "rebuilt index retains every original ABI key, including keys sharing the non-function WIDE bit");
    }
    const TypeId floating = BUSTER_TYPE_DOUBLE;
    const TypeId other = type_function(table, floating, &floating, 1, BUSTER_TYPE_FLAG_PROTOTYPE);
    const CheckOverloadCandidate candidates[] = {
        { .type = functions[TYPE_FUNCTION_ABI_MICROSOFT], .declaration = 1, .canonical = 1, .object = 101, .previous = UINT32_MAX, .scope = 1, .position = 1, .flags = BUSTER_CHECK_OVERLOADABLE },
        { .type = other, .declaration = 2, .canonical = 2, .object = 102, .previous = 0, .scope = 1, .position = 2, .flags = BUSTER_CHECK_OVERLOADABLE },
    };
    const CheckOverloadContext context = { .source_position = 3 };
    Arena scratch = arena_reserve(65536);
    const CheckOverloadResult wrong = check_overload_address(table, candidates, 2, 1, type_pointer(table, functions[0]), &context, &scratch);
    const CheckOverloadResult exact = check_overload_address(table, candidates, 2, 1, type_pointer(table, functions[TYPE_FUNCTION_ABI_MICROSOFT]), &context, &scratch);
    check_test_expect(test, wrong.status == CHECK_OVERLOAD_NO_MATCH && exact.status == CHECK_OVERLOAD_OK && exact.candidate == 0 && exact.object == 101,
                      "contextual overload addresses compare the complete ABI-bearing function type");
    arena_release(&scratch);
    const uint32_t capacity = table->types.capacity;
    const uint32_t count = table->types.count;
    table->types.capacity = count;
    check_test_expect(test, type_function_abi(table, integer, &integer, 1, BUSTER_TYPE_FLAG_PROTOTYPE, TYPE_FUNCTION_ABI_MICROSOFT) == functions[TYPE_FUNCTION_ABI_MICROSOFT] &&
                           type_function_abi(table, BUSTER_TYPE_LONG, &integer, 1, BUSTER_TYPE_FLAG_PROTOTYPE, TYPE_FUNCTION_ABI_MICROSOFT) == 0 &&
                           table->last_error == TYPE_ERROR_FULL && table->types.count == count,
                      "exhausted type storage still finds existing ABI identities and reports new construction failure");
    table->types.capacity = capacity;
}

static void check_test_function_abi_oracle(CheckTest* test, TypeTable* table, const char* clang)
{
    static const char* const names[] = {
        "sysv_abi",
        "ms_abi",
        "vectorcall",
        "regcall",
        "preserve_most",
        "preserve_all",
        "preserve_none",
    };
    char directory[] = "/tmp/buster-check-function-abi-XXXXXX";
    bool ready = mkdtemp(directory) != 0;
    enum { COUNT = TYPE_FUNCTION_ABI_COUNT * 2 + 1 };
    char paths[COUNT][160];
    char* pointers[COUNT];
    ParseTestClang outcomes[COUNT];
    Arena text = arena_reserve(65536);
    const TypeId integer = BUSTER_TYPE_INT;
    for (uint32_t i = 0; i < COUNT; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        pointers[i] = paths[i];
        char source[16384];
        uint32_t length = 0;
        if (i < TYPE_FUNCTION_ABI_COUNT)
        {
            const TypeId function = type_function_abi(table, integer, &integer, 1, BUSTER_TYPE_FLAG_PROTOTYPE, (TypeFunctionABI)i);
            const TypeId pointer = type_pointer(table, function);
            const String8 printed = type_print(table, &text, pointer, BUSTER_S8("value"), 0, 0);
            const String8 nested = type_print(table, &text, type_array(table, pointer, 2), BUSTER_S8("array"), 0, 0);
            check_test_expect(test, printed.pointer != 0 && nested.pointer != 0, "ABI-bearing function pointers and nested arrays print");
            const int written = snprintf(source, sizeof(source),
                "typedef int Expected(int) __attribute__((%s));\n%.*s;\n%.*s;\n"
                "_Static_assert(__builtin_types_compatible_p(__typeof__(value),Expected*),\"printed pointer ABI\");\n"
                "_Static_assert(__builtin_types_compatible_p(__typeof__(array),Expected*[2]),\"printed array ABI\");\n",
                names[i], (int)printed.length, printed.pointer != 0 ? printed.pointer : "", (int)nested.length, nested.pointer != 0 ? nested.pointer : "");
            ready &= written > 0 && (uint32_t)written < sizeof(source);
            length = written > 0 ? (uint32_t)written : 0;
        }
        else if (i < TYPE_FUNCTION_ABI_COUNT * 2)
        {
            length = (uint32_t)snprintf(source, sizeof(source), "typedef int F(int,...) __attribute__((%s));\n", names[i - TYPE_FUNCTION_ABI_COUNT]);
        }
        else
        {
            for (uint32_t abi = 0; abi < TYPE_FUNCTION_ABI_COUNT; abi += 1)
            {
                length += (uint32_t)snprintf(source + length, sizeof(source) - length, "typedef int F%u(int) __attribute__((%s));\n", abi, names[abi]);
            }
            for (uint32_t left = 0; left < TYPE_FUNCTION_ABI_COUNT; left += 1)
            {
                for (uint32_t right = 0; right < TYPE_FUNCTION_ABI_COUNT; right += 1)
                {
                    length += (uint32_t)snprintf(source + length, sizeof(source) - length,
                                                "_Static_assert(__builtin_types_compatible_p(F%u*,F%u*)==%u,\"independent ABI matrix\");\n",
                                                left, right, left == right);
                }
            }
        }
        ready &= length < sizeof(source) && parse_test_write(paths[i], source, length);
    }
    const char* option = "-std=gnu17";
    const bool oracle = ready && parse_test_clang(clang, &option, 1, pointers, COUNT, outcomes);
    check_test_expect(test, oracle, "independent ABI spellings, all compatibility pairs and printer roundtrips compile through Clang");
    for (uint32_t i = 0; i < COUNT; i += 1)
    {
        const bool rejected = i == TYPE_FUNCTION_ABI_COUNT + TYPE_FUNCTION_ABI_VECTOR || i == TYPE_FUNCTION_ABI_COUNT + TYPE_FUNCTION_ABI_REG;
        if (oracle)
        {
            check_test_expect(test, outcomes[i].rejected == rejected, "Clang confirms the exact ABI identity, printed spelling and variadic constraint");
        }
        unlink(paths[i]);
    }
    rmdir(directory);
    arena_release(&text);
}

static void check_test_function_abi_batches(CheckTest* test)
{
    TypeTestTable scalar = { 0 };
    TypeTestTable vector = { 0 };
    const bool ready = type_test_table_open(&scalar, 512) && type_test_table_open(&vector, 512);
    check_test_expect(test, ready, "ABI batch twin tables initialize");
    if (ready)
    {
        TypeId functions[TYPE_FUNCTION_ABI_COUNT];
        const TypeId integer = BUSTER_TYPE_INT;
        for (uint32_t abi = 0; abi < TYPE_FUNCTION_ABI_COUNT; abi += 1)
        {
            functions[abi] = type_function_abi(&scalar.table, integer, &integer, 1, BUSTER_TYPE_FLAG_PROTOTYPE, (TypeFunctionABI)abi);
            check_test_expect(test, functions[abi] == type_function_abi(&vector.table, integer, &integer, 1, BUSTER_TYPE_FLAG_PROTOTYPE, (TypeFunctionABI)abi),
                              "scalar and batch consumers start with identical ABI function IDs");
        }
        enum { COUNT = 65 };
        uint8_t kinds[COUNT];
        uint8_t flags[COUNT] = { 0 };
        TypeId bases[COUNT];
        uint64_t extents[COUNT] = { 0 };
        TypeId results[COUNT];
        TypeId expected[COUNT];
        for (uint32_t i = 0; i < COUNT; i += 1)
        {
            kinds[i] = BUSTER_TYPE_KIND_POINTER;
            bases[i] = functions[i % TYPE_FUNCTION_ABI_COUNT];
            expected[i] = type_pointer(&scalar.table, bases[i]);
        }
        const TypeKeys keys = { .kinds = kinds, .flags = flags, .bases = bases, .extents = extents };
        type_derive_batch(&vector.table, keys, COUNT, results);
        check_test_expect(test, memcmp(results, expected, sizeof(results)) == 0, "masked vector pointer derivation retains distinct function ABI identities");
        for (uint32_t i = 0; i < COUNT; i += 1)
        {
            kinds[i] = BUSTER_TYPE_KIND_ARRAY;
            bases[i] = results[i];
            extents[i] = i + 1;
            expected[i] = type_array(&scalar.table, bases[i], extents[i]);
        }
        type_derive_batch(&vector.table, keys, COUNT, results);
        check_test_expect(test, memcmp(results, expected, sizeof(results)) == 0 && scalar.table.types.count == vector.table.types.count &&
                               memcmp(scalar.table.types.flags, vector.table.types.flags, scalar.table.types.count) == 0,
                          "nested scalar/vector consumers retain byte-identical ABI metadata through a masked tail");
    }
    type_test_table_close(&vector);
    type_test_table_close(&scalar);
}

static void check_test_function_abis(CheckTest* test, const char* clang)
{
    TypeTestTable storage;
    const bool ready = type_test_table_open(&storage, 8192);
    check_test_expect(test, ready, "function ABI type storage initializes");
    if (ready)
    {
        check_test_function_abi_relations(test, &storage.table);
        check_test_function_abi_oracle(test, &storage.table, clang);
        type_test_table_close(&storage);
    }
    check_test_function_abi_batches(test);
}

#endif
