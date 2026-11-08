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
        const bool forbidden = abi == TYPE_FUNCTION_ABI_VECTOR || abi == TYPE_FUNCTION_ABI_REG || abi == TYPE_FUNCTION_ABI_SWIFT || abi == TYPE_FUNCTION_ABI_SWIFT_ASYNC;
        check_test_expect(test, forbidden ? variadic == 0 && table->last_error == TYPE_ERROR_OPERAND :
                                           variadic != 0 && type_function_abi_kind(table, variadic) == abi,
                          "only vectorcall, regcall and the Swift conventions reject variadic prototypes");
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
    const CheckOverloadContext context = { .semantic.source_position = 3 };
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

// The Swift parameter attributes' ABIs (#1447, TypeParameterABI), part of a function's type: in its key (an equal request
// is the same type, other ABIs another), compatible only with the same ones, kept by a composite with an old-style
// declaration's type and by type_function_like, printed before their parameters' types, and over more parameters than an
// ABIs' word holds; ordinary ones are no info at all, so that type_function_abi's type is theirs.
static void check_test_function_parameter_abis(CheckTest* test, TypeTable* table)
{
    const TypeId pointer = type_pointer(table, BUSTER_TYPE_VOID);
    const TypeId pointers[2] = { pointer, type_pointer(table, pointer) };
    const uint8_t context[2] = { TYPE_PARAMETER_ABI_SWIFT_CONTEXT, TYPE_PARAMETER_ABI_SWIFT_ERROR_RESULT };
    const uint8_t ordinary[2] = { TYPE_PARAMETER_ABI_ORDINARY, TYPE_PARAMETER_ABI_ORDINARY };
    const uint8_t other[2] = { TYPE_PARAMETER_ABI_SWIFT_ASYNC_CONTEXT, TYPE_PARAMETER_ABI_ORDINARY };
    const TypeId swift = type_function_full(table, BUSTER_TYPE_VOID, pointers, 2, BUSTER_TYPE_FLAG_PROTOTYPE, TYPE_FUNCTION_ABI_SWIFT, context);
    const TypeId again = type_function_full(table, BUSTER_TYPE_VOID, pointers, 2, BUSTER_TYPE_FLAG_PROTOTYPE, TYPE_FUNCTION_ABI_SWIFT, context);
    const TypeId plain = type_function_full(table, BUSTER_TYPE_VOID, pointers, 2, BUSTER_TYPE_FLAG_PROTOTYPE, TYPE_FUNCTION_ABI_SWIFT, ordinary);
    const TypeId bare = type_function_abi(table, BUSTER_TYPE_VOID, pointers, 2, BUSTER_TYPE_FLAG_PROTOTYPE, TYPE_FUNCTION_ABI_SWIFT);
    const TypeId async = type_function_full(table, BUSTER_TYPE_VOID, pointers, 2, BUSTER_TYPE_FLAG_PROTOTYPE, TYPE_FUNCTION_ABI_SWIFT, other);
    const TypeId sysv = type_function_full(table, BUSTER_TYPE_VOID, pointers, 2, BUSTER_TYPE_FLAG_PROTOTYPE, TYPE_FUNCTION_ABI_SYSV, ordinary);
    check_test_expect(test, swift != 0 && swift == again && plain == bare && swift != plain && async != swift && async != plain &&
                           sysv == type_function(table, BUSTER_TYPE_VOID, pointers, 2, BUSTER_TYPE_FLAG_PROTOTYPE),
                      "parameter ABIs are part of a function type's key, and ordinary ones are none");
    check_test_expect(test, type_function_has_parameter_abis(table, swift) && !type_function_has_parameter_abis(table, plain) &&
                           type_parameter_abi(table, swift, 0) == TYPE_PARAMETER_ABI_SWIFT_CONTEXT &&
                           type_parameter_abi(table, swift, 1) == TYPE_PARAMETER_ABI_SWIFT_ERROR_RESULT && type_parameter_abi(table, swift, 2) == TYPE_PARAMETER_ABI_ORDINARY &&
                           type_function_abi_kind(table, swift) == TYPE_FUNCTION_ABI_SWIFT, "parameter ABIs read back by parameter");
    const TypeId swift_pointer = type_pointer(table, swift);
    check_test_expect(test, type_compatible(table, swift_pointer, type_pointer(table, plain)) == TYPE_ANSWER_NO &&
                           type_compatible(table, swift_pointer, type_pointer(table, async)) == TYPE_ANSWER_NO &&
                           type_same_canonical(table, swift, plain) == TYPE_ANSWER_NO && type_compatible(table, swift_pointer, swift_pointer) == TYPE_ANSWER_YES,
                      "parameter ABIs decide compatibility and canonical identity");
    const TypeId oldstyle = type_function_abi(table, BUSTER_TYPE_VOID, 0, 0, 0, TYPE_FUNCTION_ABI_SWIFT);
    check_test_expect(test, type_composite(table, oldstyle, swift) == swift && type_composite(table, swift, oldstyle) == swift,
                      "a composite with an old-style declaration's type keeps the parameter ABIs");
    const TypeId changed = type_function_like(table, BUSTER_TYPE_INT, pointers, 2, BUSTER_TYPE_FLAG_PROTOTYPE, TYPE_FUNCTION_ABI_SWIFT_ASYNC, swift);
    check_test_expect(test, changed != 0 && type_parameter_abi(table, changed, 0) == TYPE_PARAMETER_ABI_SWIFT_CONTEXT &&
                           type_function_abi_kind(table, changed) == TYPE_FUNCTION_ABI_SWIFT_ASYNC, "type_function_like keeps a model's parameter ABIs");
    Arena text = arena_reserve(65536);
    const String8 printed = type_print(table, &text, swift, (String8){ 0 }, 0, 0);
    static const char expected[] = "void (__attribute__((swift_context)) void *, __attribute__((swift_error_result)) void **) __attribute__((swiftcall))";
    check_test_expect(test, printed.length == sizeof(expected) - 1 && memcmp(printed.pointer, expected, printed.length) == 0,
                      "a parameter's ABI prints before its type, as clang's TypePrinter prints it");
    arena_release(&text);
    // More parameters than a word of ABIs holds, the last one's in the second word.
    TypeId many[11];
    uint8_t abis[11] = { 0 };
    for (uint32_t i = 0; i < 11; i += 1)
    {
        many[i] = pointer;
    }
    abis[0] = TYPE_PARAMETER_ABI_SWIFT_INDIRECT_RESULT;
    abis[10] = TYPE_PARAMETER_ABI_SWIFT_CONTEXT;
    const TypeId wide = type_function_full(table, BUSTER_TYPE_VOID, many, 11, BUSTER_TYPE_FLAG_PROTOTYPE, TYPE_FUNCTION_ABI_SWIFT, abis);
    abis[10] = TYPE_PARAMETER_ABI_SWIFT_ASYNC_CONTEXT;
    const TypeId wide_other = type_function_full(table, BUSTER_TYPE_VOID, many, 11, BUSTER_TYPE_FLAG_PROTOTYPE, TYPE_FUNCTION_ABI_SWIFT, abis);
    check_test_expect(test, wide != 0 && wide_other != 0 && wide != wide_other && type_parameter_abi(table, wide, 10) == TYPE_PARAMETER_ABI_SWIFT_CONTEXT &&
                           type_parameter_abi(table, wide_other, 10) == TYPE_PARAMETER_ABI_SWIFT_ASYNC_CONTEXT &&
                           type_parameter_abi(table, wide, 0) == TYPE_PARAMETER_ABI_SWIFT_INDIRECT_RESULT && type_parameter_abi(table, wide, 5) == TYPE_PARAMETER_ABI_ORDINARY,
                      "parameter ABIs past a word's eight keep their own identity");
    const uint8_t invalid[2] = { TYPE_PARAMETER_ABI_COUNT, TYPE_PARAMETER_ABI_ORDINARY };
    check_test_expect(test, type_function_full(table, BUSTER_TYPE_VOID, pointers, 2, BUSTER_TYPE_FLAG_PROTOTYPE, TYPE_FUNCTION_ABI_SWIFT, invalid) == 0 &&
                           table->last_error == TYPE_ERROR_OPERAND, "an unknown parameter ABI is refused");
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
        "no_caller_saved_registers",
        "intel_ocl_bicc",
        "swiftcall",
        "swiftasynccall",
    };
    _Static_assert(sizeof(names) / sizeof(names[0]) == TYPE_FUNCTION_ABI_COUNT, "a spelling for each function ABI");
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
            String8 printed = type_print(table, &text, pointer, BUSTER_S8("value"), 0, 0);
            String8 nested = type_print(table, &text, type_array(table, pointer, 2), BUSTER_S8("array"), 0, 0);
            check_test_expect(test, printed.pointer != 0 && nested.pointer != 0, "ABI-bearing function pointers and nested arrays print");
            // clang's TypePrinter spells intel_ocl_bicc's canonical type "inteloclbicc", which no attribute is: the
            // roundtrip takes it back by its attribute's name.
            if (i == TYPE_FUNCTION_ABI_INTEL_OCL && printed.pointer != 0 && nested.pointer != 0)
            {
                String8* both[] = { &printed, &nested };
                for (uint32_t b = 0; b < 2; b += 1)
                {
                    const String8 from = *both[b];
                    const char* found = 0;
                    for (uint64_t at = 0; found == 0 && at + 12 <= from.length; at += 1)
                    {
                        found = memcmp(from.pointer + at, "inteloclbicc", 12) == 0 ? from.pointer + at : 0;
                    }
                    check_test_expect(test, found != 0, "intel_ocl_bicc's canonical type prints clang's inteloclbicc");
                    char* copy = found != 0 ? arena_push(&text, from.length + 2, 1) : 0;
                    if (copy != 0)
                    {
                        const uint64_t at = (uint64_t)(found - (const char*)from.pointer);
                        memcpy(copy, from.pointer, at);
                        memcpy(copy + at, "intel_ocl_bicc", 14);
                        memcpy(copy + at + 14, found + 12, from.length - at - 12);
                        *both[b] = (String8){ copy, from.length + 2 };
                    }
                }
            }
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
        const bool rejected = i == TYPE_FUNCTION_ABI_COUNT + TYPE_FUNCTION_ABI_VECTOR || i == TYPE_FUNCTION_ABI_COUNT + TYPE_FUNCTION_ABI_REG ||
                              i == TYPE_FUNCTION_ABI_COUNT + TYPE_FUNCTION_ABI_SWIFT || i == TYPE_FUNCTION_ABI_COUNT + TYPE_FUNCTION_ABI_SWIFT_ASYNC;
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

static __attribute__((noinline)) void check_test_function_abis(CheckTest* test, const char* clang)
{
    TypeTestTable storage;
    const bool ready = type_test_table_open(&storage, 8192);
    check_test_expect(test, ready, "function ABI type storage initializes");
    if (ready)
    {
        check_test_function_abi_relations(test, &storage.table);
        check_test_function_parameter_abis(test, &storage.table);
        check_test_function_abi_oracle(test, &storage.table, clang);
        type_test_table_close(&storage);
    }
    check_test_function_abi_batches(test);
}

#endif
