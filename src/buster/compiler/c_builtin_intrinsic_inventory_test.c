// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original interoperability test for the complete 3,775-record Clang 23.1.1 generated catalogue and the bounded
// type decoder. Include after c_types.c, c_builtin_intrinsic_types.c, c_builtin_intrinsic_catalog.c and the existing
// checker tests. No generated rows, external implementation or upstream tests are duplicated here. The independently
// specified known signatures below were checked against the hash-pinned raw Info comments in Builtins.inc and the
// two X86 .inc files, not inferred from intrinsic-header call sites or the decoder's own calculations.
//
// Both pre-C23 and strict-prototype modes get fresh explicitly sized tables. Missing canonical va_list context is
// deliberately left missing, so contextual descriptors remain UNSUPPORTED rather than receiving an invented type.
// All actual rows must decode as SUCCESS, CUSTOM or UNSUPPORTED; an INVALID/CAPACITY result is an interoperability
// failure with its exact row identity. Status counts are printed only from actual execution. Success establishes a
// declaration TypeId, not full call checking, feature eligibility, ICE completeness, folding, ABI or runtime support.

#define BUSTER_INTRINSIC_INVENTORY_EXPECTED_COUNT 3775u

typedef struct BuiltinIntrinsicInventoryTest BuiltinIntrinsicInventoryTest;
struct BuiltinIntrinsicInventoryTest
{
    uint64_t checks;
    uint64_t failures;
};

static void builtin_intrinsic_inventory_expect(BuiltinIntrinsicInventoryTest* test, bool condition, const char* description)
{
    test->checks += 1;
    if (!condition)
    {
        test->failures += 1;
        if (test->failures <= 32)
        {
            fprintf(stderr, "test-builtin-intrinsic-inventory: %s\n", description);
        }
    }
}

static void builtin_intrinsic_inventory_row_expect(BuiltinIntrinsicInventoryTest* test, bool condition, const BuiltinIntrinsicInfo* info,
                                                  bool strict, const BuiltinIntrinsicTypeResult* result, const char* description)
{
    test->checks += 1;
    if (!condition)
    {
        test->failures += 1;
        if (test->failures <= 32)
        {
            fprintf(stderr, "test-builtin-intrinsic-inventory: %s strict=%u name=%.*s encoding=%.*s status=%u unsupported=%#x offset=%u type_error=%u\n",
                description, strict ? 1u : 0u, (int)info->name_length, info->name, (int)info->encoding_length, info->encoding,
                (unsigned)result->status, result->unsupported, result->error_offset, (unsigned)result->type_error);
        }
    }
}

static bool builtin_intrinsic_inventory_unpublished(const BuiltinIntrinsicTypeResult* result)
{
    bool clear = result->function_type == 0 && result->result_type == 0 && result->parameter_count == 0 && result->ice_parameters == 0 &&
                 !result->variadic && !result->prototype;
    for (uint32_t i = 0; clear && i < BUSTER_INTRINSIC_TYPE_MAX_PARAMETERS; i += 1)
    {
        clear = result->parameters[i] == 0;
    }
    return clear;
}

static BuiltinIntrinsicTypeInput builtin_intrinsic_inventory_input(const BuiltinIntrinsicInfo* info, bool strict)
{
    const BuiltinIntrinsicTypeInput input =
    {
        .encoding = info->encoding,
        .encoding_length = info->encoding_length,
        .attributes = info->attributes,
        .attributes_length = info->attributes_length,
        .custom_typechecking = info->custom_typechecking,
        .strict_prototypes = strict,
    };
    return input;
}

static void builtin_intrinsic_inventory_check_result(BuiltinIntrinsicInventoryTest* test, TypeTable* table, const BuiltinIntrinsicInfo* info,
                                                   bool strict, const BuiltinIntrinsicTypeResult* result)
{
    const bool custom = memchr(info->attributes, 't', info->attributes_length) != NULL;
    const bool ignored = memchr(info->attributes, 'T', info->attributes_length) != NULL;
    const bool allowed = result->status == BUILTIN_INTRINSIC_TYPES_SUCCESS || result->status == BUILTIN_INTRINSIC_TYPES_CUSTOM ||
                         result->status == BUILTIN_INTRINSIC_TYPES_UNSUPPORTED;
    builtin_intrinsic_inventory_row_expect(test, allowed, info, strict, result, "valid catalogue descriptor was rejected or exhausted capacity");
    builtin_intrinsic_inventory_row_expect(test, info->custom_typechecking == custom && (result->status == BUILTIN_INTRINSIC_TYPES_CUSTOM) == custom,
        info, strict, result, "custom t metadata did not remain custom");
    builtin_intrinsic_inventory_row_expect(test, result->signature_ignored == ignored, info, strict, result, "uppercase T metadata was erased or invented");
    if (result->status == BUILTIN_INTRINSIC_TYPES_SUCCESS)
    {
        bool valid = type_is_valid(table, result->function_type) && type_is_valid(table, result->result_type) &&
                     result->parameter_count <= BUSTER_INTRINSIC_TYPE_MAX_PARAMETERS;
        valid = valid && type_kind(table, result->function_type) == BUSTER_TYPE_KIND_FUNCTION;
        builtin_intrinsic_inventory_row_expect(test, valid, info, strict, result, "successful decode did not publish a valid ordinary function");
        if (valid)
        {
            uint32_t count = 0;
            const TypeId* parameters = type_parameters(table, result->function_type, &count);
            bool exact = type_return(table, result->function_type) == result->result_type && count == result->parameter_count &&
                         (count == 0 || parameters != NULL);
            for (uint32_t i = 0; exact && i < count; i += 1)
            {
                exact = type_is_valid(table, result->parameters[i]) && parameters[i] == result->parameters[i];
                if (exact && (result->ice_parameters & (UINT64_C(1) << i)) != 0)
                {
                    const uint8_t kind = type_kind(table, result->parameters[i]);
                    exact = kind == BUSTER_TYPE_KIND_INTEGER || kind == BUSTER_TYPE_KIND_ENUM;
                }
            }
            const uint64_t mask = result->parameter_count == 64 ? UINT64_MAX : (UINT64_C(1) << result->parameter_count) - 1;
            exact = exact && (result->ice_parameters & ~mask) == 0 && result->unsupported == 0 && result->type_error == TYPE_ERROR_NONE;
            const uint8_t flags = table->types.flags[type_index(result->function_type)];
            exact = exact && ((flags & BUSTER_TYPE_FLAG_PROTOTYPE) != 0) == result->prototype &&
                    ((flags & BUSTER_TYPE_FLAG_VARIADIC) != 0) == (result->prototype && result->variadic);
            builtin_intrinsic_inventory_row_expect(test, exact, info, strict, result, "function/parameter/ICE metadata is inconsistent");
        }
    }
    else
    {
        builtin_intrinsic_inventory_row_expect(test, builtin_intrinsic_inventory_unpublished(result), info, strict, result,
            "non-success leaked a partial ordinary prototype");
        if (result->status == BUILTIN_INTRINSIC_TYPES_UNSUPPORTED)
        {
            builtin_intrinsic_inventory_row_expect(test, result->unsupported != 0 && result->error_offset <= info->encoding_length,
                info, strict, result, "unsupported descriptor lacks bounded explicit gap metadata");
        }
    }
}

static void builtin_intrinsic_inventory_known(BuiltinIntrinsicInventoryTest* test, TypeTable* table, bool strict)
{
    const TypeId int4 = type_vector(table, BUSTER_TYPE_INT, 4, false);
    const TypeId float16 = type_vector(table, BUSTER_TYPE_FLOAT, 16, false);
    const TypeId unsigned_pointer = type_pointer(table, BUSTER_TYPE_UINT);
    const struct
    {
        const char* name;
        const char* encoding;
        TypeId result;
        TypeId parameters[4];
        uint32_t count;
        uint64_t ice;
    } fixtures[] =
    {
        { "__builtin_bswap32", "UZiUZi", BUSTER_TYPE_UINT, { BUSTER_TYPE_UINT }, 1, 0 },
        { "__builtin_bswap64", "UWiUWi", BUSTER_TYPE_ULONG, { BUSTER_TYPE_ULONG }, 1, 0 },
        { "__builtin_rotateleft8", "UcUcUc", BUSTER_TYPE_UCHAR, { BUSTER_TYPE_UCHAR, BUSTER_TYPE_UCHAR }, 2, 0 },
        { "__builtin_rotateleft16", "UsUsUs", BUSTER_TYPE_USHORT, { BUSTER_TYPE_USHORT, BUSTER_TYPE_USHORT }, 2, 0 },
        { "__builtin_rotateleft32", "UZiUZiUZi", BUSTER_TYPE_UINT, { BUSTER_TYPE_UINT, BUSTER_TYPE_UINT }, 2, 0 },
        { "__builtin_rotateleft64", "UWiUWiUWi", BUSTER_TYPE_ULONG, { BUSTER_TYPE_ULONG, BUSTER_TYPE_ULONG }, 2, 0 },
        { "__builtin_rotateright8", "UcUcUc", BUSTER_TYPE_UCHAR, { BUSTER_TYPE_UCHAR, BUSTER_TYPE_UCHAR }, 2, 0 },
        { "__builtin_rotateright16", "UsUsUs", BUSTER_TYPE_USHORT, { BUSTER_TYPE_USHORT, BUSTER_TYPE_USHORT }, 2, 0 },
        { "__builtin_rotateright32", "UZiUZiUZi", BUSTER_TYPE_UINT, { BUSTER_TYPE_UINT, BUSTER_TYPE_UINT }, 2, 0 },
        { "__builtin_rotateright64", "UWiUWiUWi", BUSTER_TYPE_ULONG, { BUSTER_TYPE_ULONG, BUSTER_TYPE_ULONG }, 2, 0 },
        { "__builtin_ia32_pshufd", "V4iV4iIi", int4, { int4, BUSTER_TYPE_INT }, 2, 2 },
        { "__builtin_ia32_addps512", "V16fV16fV16fIi", float16, { float16, float16, BUSTER_TYPE_INT }, 3, 4 },
        { "__builtin_ia32_addcarryx_u32", "UcUcUiUiUi*", BUSTER_TYPE_UCHAR, { BUSTER_TYPE_UCHAR, BUSTER_TYPE_UINT, BUSTER_TYPE_UINT, unsigned_pointer }, 4, 0 },
        { "__builtin_ia32_rdtscp", "UOiUi*", BUSTER_TYPE_ULLONG, { unsigned_pointer }, 1, 0 },
        // No I occurs here. Handwritten tile constraints must not be invented by the prototype decoder.
        { "__builtin_ia32_tilezero", "vUc", BUSTER_TYPE_VOID, { BUSTER_TYPE_UCHAR }, 1, 0 },
    };
    builtin_intrinsic_inventory_expect(test, int4 != 0 && float16 != 0 && unsigned_pointer != 0, "independent known signature type allocation");
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        const BuiltinIntrinsicInfo* info = builtin_intrinsic_lookup(fixtures[i].name, (uint32_t)strlen(fixtures[i].name));
        builtin_intrinsic_inventory_expect(test, info != NULL, fixtures[i].name);
        if (info != NULL)
        {
            const BuiltinIntrinsicTypeInput input = builtin_intrinsic_inventory_input(info, strict);
            BuiltinIntrinsicTypeResult result;
            builtin_intrinsic_types_decode(table, &input, &result);
            const size_t encoding_length = strlen(fixtures[i].encoding);
            bool exact = info->encoding_length == encoding_length && memcmp(info->encoding, fixtures[i].encoding, encoding_length) == 0 &&
                         result.status == BUILTIN_INTRINSIC_TYPES_SUCCESS && result.result_type == fixtures[i].result &&
                         result.parameter_count == fixtures[i].count && result.ice_parameters == fixtures[i].ice && result.prototype && !result.variadic;
            for (uint32_t p = 0; exact && p < fixtures[i].count; p += 1)
            {
                exact = result.parameters[p] == fixtures[i].parameters[p];
            }
            const TypeId expected = type_function(table, fixtures[i].result, fixtures[i].parameters, fixtures[i].count, BUSTER_TYPE_FLAG_PROTOTYPE);
            exact = exact && expected != 0 && result.function_type == expected;
            builtin_intrinsic_inventory_row_expect(test, exact, info, strict, &result, "lookup-to-decode disagrees with an independently specified exact signature");
        }
    }
}

static void builtin_intrinsic_inventory_mode(BuiltinIntrinsicInventoryTest* test, bool strict)
{
    const TypeCapacity capacity =
    {
        .types = 16384,
        .parameters = 65536,
        .tags = 64,
        .members = 256,
    };
    Arena storage = arena_reserve(type_capacity_bytes(capacity));
    Arena index = arena_reserve(type_index_capacity_bytes(capacity));
    TypeTable table = {0};
    const bool initialized = type_initialize(&table, &storage, &index, capacity);
    builtin_intrinsic_inventory_expect(test, initialized, "complete-catalogue explicit type capacity allocation");
    if (initialized)
    {
        uint32_t counts[5] = {0};
        uint32_t visited = 0;
        for (uint32_t i = 0; i < BUSTER_BUILTIN_INTRINSIC_COUNT; i += 1)
        {
            const BuiltinIntrinsicInfo* info = builtin_intrinsic_at(i);
            builtin_intrinsic_inventory_expect(test, info != NULL, "catalogue ordinal unexpectedly missing");
            if (info != NULL)
            {
                const BuiltinIntrinsicTypeInput input = builtin_intrinsic_inventory_input(info, strict);
                BuiltinIntrinsicTypeResult result;
                builtin_intrinsic_types_decode(&table, &input, &result);
                visited += 1;
                if ((uint32_t)result.status < BUSTER_ARRAY_LENGTH(counts))
                {
                    counts[result.status] += 1;
                }
                builtin_intrinsic_inventory_row_expect(test, strcmp(info->record_kind, "table-record") == 0 &&
                    builtin_intrinsic_lookup(info->name, info->name_length) == info, info, strict, &result,
                    "full inventory contains a missing lookup or a non-table placeholder");
                builtin_intrinsic_inventory_check_result(test, &table, info, strict, &result);
            }
        }
        builtin_intrinsic_inventory_expect(test, visited == BUSTER_INTRINSIC_INVENTORY_EXPECTED_COUNT, "every exact generated descriptor must be visited");
        printf("test-builtin-intrinsic-inventory: strict=%u visited=%u success=%u custom=%u unsupported=%u invalid=%u capacity=%u\n",
            strict ? 1u : 0u, visited, counts[BUILTIN_INTRINSIC_TYPES_SUCCESS], counts[BUILTIN_INTRINSIC_TYPES_CUSTOM], counts[BUILTIN_INTRINSIC_TYPES_UNSUPPORTED],
            counts[BUILTIN_INTRINSIC_TYPES_INVALID], counts[BUILTIN_INTRINSIC_TYPES_CAPACITY]);
        builtin_intrinsic_inventory_known(test, &table, strict);
    }
    arena_release(&index);
    arena_release(&storage);
}

static __attribute__((unused)) int run_builtin_intrinsic_inventory_tests(int argc, char** argv)
{
    (void)argv;
    BuiltinIntrinsicInventoryTest test = {0};
    const bool options = argc == 0;
    builtin_intrinsic_inventory_expect(&test, options, "usage: test-builtin-intrinsic-inventory");
    if (options)
    {
        builtin_intrinsic_inventory_expect(&test, BUSTER_BUILTIN_INTRINSIC_COUNT == BUSTER_INTRINSIC_INVENTORY_EXPECTED_COUNT,
            "test requires the complete 3,775-row generated catalogue");
        builtin_intrinsic_inventory_mode(&test, false);
        builtin_intrinsic_inventory_mode(&test, true);
    }
    printf("test-builtin-intrinsic-inventory: %" PRIu64 " checks, %" PRIu64 " failures\n", test.checks, test.failures);
    const int result = test.failures != 0;
    return result;
}
