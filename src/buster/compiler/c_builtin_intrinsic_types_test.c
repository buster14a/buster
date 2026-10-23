// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original descriptor fixtures. Include after c_builtin_intrinsic_types.c and c_checker_test.c, whose table fixture
// is reused. Expected canonical TypeIds are assembled from independently specified C types, not decoded strings or
// inferred header calls. No generated LLVM table or upstream tests are incorporated. Guarded slices have no padding.

typedef struct BuiltinIntrinsicTypeTest BuiltinIntrinsicTypeTest;
struct BuiltinIntrinsicTypeTest
{
    uint64_t checks;
    uint64_t failures;
};

static void builtin_intrinsic_type_test_expect(BuiltinIntrinsicTypeTest* test, bool condition, const char* description)
{
    test->checks += 1;
    if (!condition)
    {
        test->failures += 1;
        if (test->failures <= 24)
        {
            fprintf(stderr, "test-builtin-intrinsic-types: %s\n", description);
        }
    }
}

static BuiltinIntrinsicTypeResult builtin_intrinsic_type_test_decode(TypeTable* table, const char* encoding)
{
    const BuiltinIntrinsicTypeInput input = { .encoding = encoding, .encoding_length = (uint32_t)strlen(encoding) };
    BuiltinIntrinsicTypeResult result;
    builtin_intrinsic_types_decode(table, &input, &result);
    return result;
}

static bool builtin_intrinsic_type_test_equal(const BuiltinIntrinsicTypeResult* a, const BuiltinIntrinsicTypeResult* b)
{
    bool equal = a->status == b->status && a->function_type == b->function_type && a->result_type == b->result_type &&
                 a->parameter_count == b->parameter_count && a->ice_parameters == b->ice_parameters && a->unsupported == b->unsupported &&
                 a->error_offset == b->error_offset && a->type_error == b->type_error && a->variadic == b->variadic &&
                 a->prototype == b->prototype && a->signature_ignored == b->signature_ignored;
    for (uint32_t i = 0; equal && i < BUSTER_INTRINSIC_TYPE_MAX_PARAMETERS; i += 1)
    {
        equal = a->parameters[i] == b->parameters[i];
    }
    return equal;
}

static bool builtin_intrinsic_type_test_unpublished(const BuiltinIntrinsicTypeResult* result)
{
    bool clear = result->function_type == 0 && result->result_type == 0 && result->parameter_count == 0 && result->ice_parameters == 0 &&
                 !result->variadic && !result->prototype;
    for (uint32_t i = 0; clear && i < BUSTER_INTRINSIC_TYPE_MAX_PARAMETERS; i += 1)
    {
        clear = result->parameters[i] == 0;
    }
    return clear;
}

static void builtin_intrinsic_type_test_scalars(BuiltinIntrinsicTypeTest* test, TypeTable* table)
{
    const struct
    {
        const char* encoding;
        TypeId expected;
    } cases[] =
    {
        { "v", BUSTER_TYPE_VOID },
        { "b", BUSTER_TYPE_BOOL },
        { "c", BUSTER_TYPE_CHAR },
        { "Sc", BUSTER_TYPE_SCHAR },
        { "Uc", BUSTER_TYPE_UCHAR },
        { "s", BUSTER_TYPE_SHORT },
        { "Ss", BUSTER_TYPE_SHORT },
        { "Us", BUSTER_TYPE_USHORT },
        { "i", BUSTER_TYPE_INT },
        { "Si", BUSTER_TYPE_INT },
        { "Ui", BUSTER_TYPE_UINT },
        { "Li", BUSTER_TYPE_LONG },
        { "ULi", BUSTER_TYPE_ULONG },
        { "LLi", BUSTER_TYPE_LLONG },
        { "ULLi", BUSTER_TYPE_ULLONG },
        { "LLLi", BUSTER_TYPE_INT128 },
        { "ULLLi", BUSTER_TYPE_UINT128 },
        { "Bi", BUSTER_TYPE_SCHAR },
        { "UBi", BUSTER_TYPE_UCHAR },
        { "Ti", BUSTER_TYPE_SHORT },
        { "UTi", BUSTER_TYPE_USHORT },
        { "Ni", BUSTER_TYPE_INT },
        { "UNi", BUSTER_TYPE_UINT },
        { "Wi", BUSTER_TYPE_LONG },
        { "UWi", BUSTER_TYPE_ULONG },
        { "Zi", BUSTER_TYPE_INT },
        { "UZi", BUSTER_TYPE_UINT },
        { "Oi", BUSTER_TYPE_LLONG },
        { "UOi", BUSTER_TYPE_ULLONG },
        { "f", BUSTER_TYPE_FLOAT },
        { "d", BUSTER_TYPE_DOUBLE },
        { "Ld", BUSTER_TYPE_LONG_DOUBLE },
        { "LLd", BUSTER_TYPE_FLOAT128 },
        { "x", BUSTER_TYPE_FLOAT16 },
        { "y", BUSTER_TYPE_BF16 },
        { "z", BUSTER_TYPE_ULONG },
        { "w", BUSTER_TYPE_INT },
        { "Y", BUSTER_TYPE_LONG },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        const BuiltinIntrinsicTypeResult result = builtin_intrinsic_type_test_decode(table, cases[i].encoding);
        const TypeId function = type_function(table, cases[i].expected, 0, 0, BUSTER_TYPE_FLAG_PROTOTYPE);
        builtin_intrinsic_type_test_expect(test, result.status == BUILTIN_INTRINSIC_TYPES_SUCCESS && result.result_type == cases[i].expected &&
            result.function_type == function && result.parameter_count == 0 && result.ice_parameters == 0 && result.prototype && !result.variadic,
            "primitive and target-sized scalar descriptors preserve canonical type identity");
    }
    BuiltinIntrinsicTypeResult result = builtin_intrinsic_type_test_decode(table, "h*");
    builtin_intrinsic_type_test_expect(test, result.status == BUILTIN_INTRINSIC_TYPES_SUCCESS && result.result_type == type_pointer(table, BUSTER_TYPE_HALF),
        "__fp16 remains a distinct legal pointed-to type");
    result = builtin_intrinsic_type_test_decode(table, "h");
    builtin_intrinsic_type_test_expect(test, result.status == BUILTIN_INTRINSIC_TYPES_UNSUPPORTED &&
        result.unsupported == BUSTER_INTRINSIC_TYPE_UNSUPPORTED_MODEL && builtin_intrinsic_type_test_unpublished(&result),
        "the current function model's scalar __fp16 restriction is explicit unsupported metadata");
}

static void builtin_intrinsic_type_test_derived(BuiltinIntrinsicTypeTest* test, TypeTable* table)
{
    const TypeId vector = type_vector(table, BUSTER_TYPE_INT, 4, false);
    const TypeId vector_pointer = type_pointer(table, vector);
    const TypeId constant_pointer = type_pointer(table, type_qualify(table, BUSTER_TYPE_INT, BUSTER_TYPE_CONST));
    BuiltinIntrinsicTypeResult result = builtin_intrinsic_type_test_decode(table, "iV4i*");
    builtin_intrinsic_type_test_expect(test, result.status == BUILTIN_INTRINSIC_TYPES_SUCCESS && result.result_type == BUSTER_TYPE_INT &&
        result.parameter_count == 1 && result.parameters[0] == vector_pointer &&
        result.function_type == type_function(table, BUSTER_TYPE_INT, &vector_pointer, 1, BUSTER_TYPE_FLAG_PROTOTYPE),
        "postfix pointer applies to the complete vector wrapper");
    result = builtin_intrinsic_type_test_decode(table, "iiC*");
    builtin_intrinsic_type_test_expect(test, result.status == BUILTIN_INTRINSIC_TYPES_SUCCESS && result.parameters[0] == constant_pointer,
        "pointee qualifiers survive function canonicalization");
    result = builtin_intrinsic_type_test_decode(table, "iiCDR");
    builtin_intrinsic_type_test_expect(test, result.status == BUILTIN_INTRINSIC_TYPES_SUCCESS && result.parameters[0] == BUSTER_TYPE_INT,
        "top-level parameter qualifiers are canonicalized away");
    result = builtin_intrinsic_type_test_decode(table, "ii*CDR*");
    const TypeId inner = type_qualify(table, type_pointer(table, BUSTER_TYPE_INT), BUSTER_TYPE_CONST | BUSTER_TYPE_VOLATILE | BUSTER_TYPE_RESTRICT);
    builtin_intrinsic_type_test_expect(test, result.status == BUILTIN_INTRINSIC_TYPES_SUCCESS && result.parameters[0] == type_pointer(table, inner),
        "each postfix qualifier applies at its written pointer depth");
    const TypeId wide_vector = type_vector(table, BUSTER_TYPE_FLOAT, 16, false);
    result = builtin_intrinsic_type_test_decode(table, "V16fV16fV16fIi");
    const TypeId arguments[3] = { wide_vector, wide_vector, BUSTER_TYPE_INT };
    builtin_intrinsic_type_test_expect(test, result.status == BUILTIN_INTRINSIC_TYPES_SUCCESS && result.result_type == wide_vector && result.parameter_count == 3 &&
        result.parameters[0] == wide_vector && result.parameters[1] == wide_vector && result.parameters[2] == BUSTER_TYPE_INT && result.ice_parameters == 4 &&
        result.function_type == type_function(table, wide_vector, arguments, 3, BUSTER_TYPE_FLAG_PROTOTYPE),
        "ordinary vector prototypes retain the exact immediate-parameter marker");
    result = builtin_intrinsic_type_test_decode(table, "V4UOiV4UWi");
    builtin_intrinsic_type_test_expect(test, result.status == BUILTIN_INTRINSIC_TYPES_SUCCESS && result.result_type == type_vector(table, BUSTER_TYPE_ULLONG, 4, false) &&
        result.parameters[0] == type_vector(table, BUSTER_TYPE_ULONG, 4, false) && result.result_type != result.parameters[0],
        "equal-width long and long long vector elements retain different canonical identities");
    result = builtin_intrinsic_type_test_decode(table, "E3fE8b");
    builtin_intrinsic_type_test_expect(test, result.status == BUILTIN_INTRINSIC_TYPES_SUCCESS && result.result_type == type_vector(table, BUSTER_TYPE_FLOAT, 3, true) &&
        result.parameters[0] == type_vector(table, BUSTER_TYPE_BOOL, 8, true), "extended vectors retain their distinct factory and boolean elements");
    result = builtin_intrinsic_type_test_decode(table, "XdXfXi");
    builtin_intrinsic_type_test_expect(test, result.status == BUILTIN_INTRINSIC_TYPES_SUCCESS && result.result_type == BUSTER_TYPE_COMPLEX_DOUBLE &&
        result.parameters[0] == BUSTER_TYPE_COMPLEX_FLOAT && result.parameters[1] == type_complex(table, BUSTER_TYPE_INT),
        "complex wrappers preserve both floating and GNU integer element types");
    result = builtin_intrinsic_type_test_decode(table, "vIUiIIi");
    builtin_intrinsic_type_test_expect(test, result.status == BUILTIN_INTRINSIC_TYPES_SUCCESS && result.parameter_count == 2 && result.parameters[0] == BUSTER_TYPE_UINT &&
        result.parameters[1] == BUSTER_TYPE_INT && result.ice_parameters == 3, "I is idempotent and applies to exact integer parameter positions");

    const uint32_t names[5] = { 701, 702, 703, 704, 705 };
    const TypeId list = type_builtin_va_list(table, names);
    const TypeId tag_pointer = type_pointer(table, type_element(table, list));
    BuiltinIntrinsicTypeInput input = { .encoding = "vaA", .encoding_length = 3, .va_list_type = list };
    builtin_intrinsic_types_decode(table, &input, &result);
    builtin_intrinsic_type_test_expect(test, list != 0 && result.status == BUILTIN_INTRINSIC_TYPES_SUCCESS && result.parameter_count == 2 &&
        result.parameters[0] == tag_pointer && result.parameters[1] == tag_pointer,
        "both va_list forms decay the supplied canonical SysV array identity");
    input.encoding = "va*";
    builtin_intrinsic_types_decode(table, &input, &result);
    builtin_intrinsic_type_test_expect(test, result.status == BUILTIN_INTRINSIC_TYPES_SUCCESS && result.parameters[0] == type_pointer(table, list) &&
        result.parameters[0] != tag_pointer, "an explicit pointer to va_list preserves pointer-to-array identity");
    input.encoding = "vA";
    input.encoding_length = 2;
    input.va_list_type = type_aligned(table, list, 5);
    builtin_intrinsic_types_decode(table, &input, &result);
    builtin_intrinsic_type_test_expect(test, result.status == BUILTIN_INTRINSIC_TYPES_SUCCESS && result.parameters[0] == tag_pointer,
        "A decay uses the same accepted aligned canonical list shape");
    input.va_list_type = BUSTER_TYPE_INT;
    builtin_intrinsic_types_decode(table, &input, &result);
    builtin_intrinsic_type_test_expect(test, result.status == BUILTIN_INTRINSIC_TYPES_UNSUPPORTED && builtin_intrinsic_type_test_unpublished(&result),
        "an unrelated valid TypeId cannot become a fabricated va_list prototype");

    input = (BuiltinIntrinsicTypeInput){ .encoding = "i.", .encoding_length = 2 };
    builtin_intrinsic_types_decode(table, &input, &result);
    builtin_intrinsic_type_test_expect(test, result.status == BUILTIN_INTRINSIC_TYPES_SUCCESS && result.variadic && !result.prototype &&
        result.function_type == type_function(table, BUSTER_TYPE_INT, 0, 0, 0), "pre-C23 empty variadic encoding follows Clang's no-prototype rule");
    input.strict_prototypes = true;
    builtin_intrinsic_types_decode(table, &input, &result);
    builtin_intrinsic_type_test_expect(test, result.status == BUILTIN_INTRINSIC_TYPES_SUCCESS && result.variadic && result.prototype &&
        result.function_type == type_function(table, BUSTER_TYPE_INT, 0, 0, BUSTER_TYPE_FLAG_PROTOTYPE | BUSTER_TYPE_FLAG_VARIADIC),
        "C23 strict prototypes preserve a variadic prototype even with no fixed parameters");
}

static void builtin_intrinsic_type_test_invalid(BuiltinIntrinsicTypeTest* test, TypeTable* table)
{
    const char* invalid[] = { "I", "Iii", "vI", "vv", "V", "V0i", "V4294967296i", "X", "V4", "E", "i..", "i.i", "i?", "i\ni", "SUii",
        "SSi", "UUi", "LLLLi", "LWi", "WLi", "NWi", "Bci", "Lsi", "Udi", "LLLdi", "LV4i", "IV4i", "V4Ii", "vIf", "vIi*", "V4v", "Xb" };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(invalid); i += 1)
    {
        const BuiltinIntrinsicTypeResult result = builtin_intrinsic_type_test_decode(table, invalid[i]);
        builtin_intrinsic_type_test_expect(test, result.status == BUILTIN_INTRINSIC_TYPES_INVALID && builtin_intrinsic_type_test_unpublished(&result),
            "malformed descriptors publish no partial type or ICE metadata");
    }
    const char* unsupported[] = { "", "vP", "vJ", "vSJ", "vK", "vF", "vG", "vH", "vM", "vp", "vq4i", "vQa", "vm", "va", "vA", "vi&", "vi*0", "vi*4", "vh", "Xh", "vXh", "V4Xh" };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(unsupported); i += 1)
    {
        const BuiltinIntrinsicTypeResult result = builtin_intrinsic_type_test_decode(table, unsupported[i]);
        builtin_intrinsic_type_test_expect(test, result.status == BUILTIN_INTRINSIC_TYPES_UNSUPPORTED && result.unsupported != 0 &&
            builtin_intrinsic_type_test_unpublished(&result), "context/reference/address-space/target gaps are explicit without guessed substitutions");
    }
    const char nul[] = { 'i', 0, 'i' };
    BuiltinIntrinsicTypeInput input = { .encoding = nul, .encoding_length = sizeof(nul) };
    BuiltinIntrinsicTypeResult result;
    builtin_intrinsic_types_decode(table, &input, &result);
    builtin_intrinsic_type_test_expect(test, result.status == BUILTIN_INTRINSIC_TYPES_INVALID && builtin_intrinsic_type_test_unpublished(&result),
        "embedded NUL does not terminate a bounded descriptor early");
    input = (BuiltinIntrinsicTypeInput){ .encoding_length = 1 };
    builtin_intrinsic_types_decode(table, &input, &result);
    builtin_intrinsic_type_test_expect(test, result.status == BUILTIN_INTRINSIC_TYPES_INVALID, "nonnull encoding is required for nonempty ordinary input");
    input = (BuiltinIntrinsicTypeInput){ .encoding = "i", .encoding_length = BUSTER_INTRINSIC_TYPE_MAX_ENCODING + 1 };
    builtin_intrinsic_types_decode(table, &input, &result);
    builtin_intrinsic_type_test_expect(test, result.status == BUILTIN_INTRINSIC_TYPES_CAPACITY && builtin_intrinsic_type_test_unpublished(&result),
        "oversized logical slices are rejected before reading their payload");
    char parameters[1 + 2 * (BUSTER_INTRINSIC_TYPE_MAX_PARAMETERS + 1)];
    parameters[0] = 'v';
    for (uint32_t count = 0; count <= BUSTER_INTRINSIC_TYPE_MAX_PARAMETERS + 1; count += 1)
    {
        if (count != 0)
        {
            parameters[2 * count - 1] = 'I';
            parameters[2 * count] = 'i';
        }
        input = (BuiltinIntrinsicTypeInput){ .encoding = parameters, .encoding_length = 1 + 2 * count };
        builtin_intrinsic_types_decode(table, &input, &result);
        const uint64_t mask = count >= 64 ? UINT64_MAX : (UINT64_C(1) << count) - 1;
        builtin_intrinsic_type_test_expect(test, count <= BUSTER_INTRINSIC_TYPE_MAX_PARAMETERS ?
            result.status == BUILTIN_INTRINSIC_TYPES_SUCCESS && result.parameter_count == count && result.ice_parameters == mask :
            result.status == BUILTIN_INTRINSIC_TYPES_CAPACITY && builtin_intrinsic_type_test_unpublished(&result),
            "parameter capacity includes mask bit63 and rejects the next argument without shifting out of range");
    }
    char nested[BUSTER_INTRINSIC_TYPE_MAX_NESTING + 2];
    memset(nested, 'X', sizeof(nested));
    nested[sizeof(nested) - 1] = 'i';
    input = (BuiltinIntrinsicTypeInput){ .encoding = nested, .encoding_length = sizeof(nested) };
    builtin_intrinsic_types_decode(table, &input, &result);
    builtin_intrinsic_type_test_expect(test, result.status == BUILTIN_INTRINSIC_TYPES_CAPACITY && builtin_intrinsic_type_test_unpublished(&result),
        "adversarial wrapper depth has a bounded explicit stack");
    input = (BuiltinIntrinsicTypeInput){ .encoding = "v.", .encoding_length = 2, .attributes = "nt", .attributes_length = 2 };
    builtin_intrinsic_types_decode(table, &input, &result);
    builtin_intrinsic_type_test_expect(test, result.status == BUILTIN_INTRINSIC_TYPES_CUSTOM && builtin_intrinsic_type_test_unpublished(&result),
        "attribute t prevents accidental ordinary decoding even when the explicit flag is false");
    input = (BuiltinIntrinsicTypeInput){ .custom_typechecking = true, .encoding_length = UINT32_MAX };
    builtin_intrinsic_types_decode(table, &input, &result);
    builtin_intrinsic_type_test_expect(test, result.status == BUILTIN_INTRINSIC_TYPES_CUSTOM && builtin_intrinsic_type_test_unpublished(&result),
        "explicit custom metadata never dereferences or invents an ordinary encoding");
    input = (BuiltinIntrinsicTypeInput){ .encoding = "ii", .encoding_length = 2, .attributes = "T", .attributes_length = 1 };
    builtin_intrinsic_types_decode(table, &input, &result);
    builtin_intrinsic_type_test_expect(test, result.status == BUILTIN_INTRINSIC_TYPES_SUCCESS && result.signature_ignored,
        "uppercase T retains its distinct ignore-signature metadata");
}

static void builtin_intrinsic_type_test_pages(BuiltinIntrinsicTypeTest* test, TypeTable* table)
{
    const long page_value = sysconf(_SC_PAGESIZE);
    const size_t page = page_value > 0 ? (size_t)page_value : 0;
    uint8_t* payload = page != 0 ? mmap(0, 2 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    const size_t input_region = page != 0 ? ((33 * sizeof(BuiltinIntrinsicTypeInput) + 16 + page - 1) / page) * page : 0;
    const size_t output_region = page != 0 ? ((33 * sizeof(BuiltinIntrinsicTypeResult) + 16 + page - 1) / page) * page : 0;
    uint8_t* input_mapping = page != 0 ? mmap(0, input_region + page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    uint8_t* output_mapping = page != 0 ? mmap(0, output_region + page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    const bool ready = payload != MAP_FAILED && input_mapping != MAP_FAILED && output_mapping != MAP_FAILED &&
                       mprotect(payload + page, page, PROT_NONE) == 0 && mprotect(input_mapping + input_region, page, PROT_NONE) == 0 &&
                       mprotect(output_mapping + output_region, page, PROT_NONE) == 0;
    builtin_intrinsic_type_test_expect(test, ready, "guarded descriptor and record allocation");
    if (ready)
    {
        const char* encoding = "V4iV4iIi";
        const uint32_t length = (uint32_t)strlen(encoding);
        for (uint32_t count = 0; count <= length; count += 1)
        {
            char* tail = (char*)payload + page - count;
            memcpy(tail, encoding, count);
            BuiltinIntrinsicTypeInput input = { .encoding = tail, .encoding_length = count };
            BuiltinIntrinsicTypeResult guarded;
            BuiltinIntrinsicTypeResult expected;
            builtin_intrinsic_types_decode(table, &input, &guarded);
            input.encoding = encoding;
            builtin_intrinsic_types_decode(table, &input, &expected);
            builtin_intrinsic_type_test_expect(test, builtin_intrinsic_type_test_equal(&guarded, &expected),
                "every descriptor prefix has exact scalar semantics at a guarded logical end");
        }
        char* attribute = (char*)payload + page - 1;
        *attribute = 't';
        const BuiltinIntrinsicTypeInput custom = { .encoding = (const char*)payload + page, .encoding_length = UINT32_MAX,
                                                  .attributes = attribute, .attributes_length = 1 };
        BuiltinIntrinsicTypeResult result;
        builtin_intrinsic_types_decode(table, &custom, &result);
        builtin_intrinsic_type_test_expect(test, result.status == BUILTIN_INTRINSIC_TYPES_CUSTOM && builtin_intrinsic_type_test_unpublished(&result),
            "guarded custom attribute suppresses all ordinary descriptor reads");
        builtin_intrinsic_types_decode_batch(0, 0, 0, 0);
        for (uint32_t count = 0; count <= 33; count += 1)
        {
            BuiltinIntrinsicTypeInput* inputs = (BuiltinIntrinsicTypeInput*)(input_mapping + input_region) - count;
            BuiltinIntrinsicTypeResult* outputs = (BuiltinIntrinsicTypeResult*)(output_mapping + output_region) - count;
            memset((uint8_t*)outputs - 16, 0xa5, 16);
            BuiltinIntrinsicTypeResult expected[33];
            for (uint32_t i = 0; i < count; i += 1)
            {
                const char* text = i % 3 == 0 ? "iV4i*" : (i % 3 == 1 ? "vIUi" : "vi*0");
                inputs[i] = (BuiltinIntrinsicTypeInput){ .encoding = text, .encoding_length = (uint32_t)strlen(text), .strict_prototypes = i % 2 != 0 };
                builtin_intrinsic_types_decode(table, inputs + i, expected + i);
            }
            builtin_intrinsic_types_decode_batch(table, inputs, outputs, count);
            for (uint32_t i = 0; i < count; i += 1)
            {
                builtin_intrinsic_type_test_expect(test, builtin_intrinsic_type_test_equal(outputs + i, expected + i), "guarded batch output matches independent record decoding");
            }
            bool prefix = true;
            for (uint32_t i = 0; i < 16; i += 1)
            {
                prefix = prefix && ((uint8_t*)outputs)[(int)i - 16] == 0xa5;
            }
            builtin_intrinsic_type_test_expect(test, prefix, "batch writes no bytes before its logical output slice");
            const uint32_t split = count / 2;
            builtin_intrinsic_types_decode_batch(table, inputs, outputs, split);
            builtin_intrinsic_types_decode_batch(table, inputs + split, outputs + split, count - split);
            for (uint32_t i = 0; i < count; i += 1)
            {
                builtin_intrinsic_type_test_expect(test, builtin_intrinsic_type_test_equal(outputs + i, expected + i), "legal batch partitions preserve every semantic field");
            }
        }
    }
    if (payload != MAP_FAILED)
    {
        munmap(payload, 2 * page);
    }
    if (input_mapping != MAP_FAILED)
    {
        munmap(input_mapping, input_region + page);
    }
    if (output_mapping != MAP_FAILED)
    {
        munmap(output_mapping, output_region + page);
    }
}

static void builtin_intrinsic_type_test_capacity(BuiltinIntrinsicTypeTest* test)
{
    const TypeCapacity capacity = { .types = 1, .parameters = 1, .tags = 1, .members = 1 };
    Arena storage = arena_reserve(type_capacity_bytes(capacity));
    Arena index = arena_reserve(type_index_capacity_bytes(capacity));
    TypeTable table = {0};
    const bool initialized = type_initialize(&table, &storage, &index, capacity);
    builtin_intrinsic_type_test_expect(test, initialized, "bounded-capacity type-table allocation");
    if (initialized)
    {
        const BuiltinIntrinsicTypeResult result = builtin_intrinsic_type_test_decode(&table, "i*i");
        builtin_intrinsic_type_test_expect(test, result.status == BUILTIN_INTRINSIC_TYPES_CAPACITY && result.type_error == TYPE_ERROR_FULL &&
            builtin_intrinsic_type_test_unpublished(&result), "late factory exhaustion publishes no partial function or result TypeIds");
        builtin_intrinsic_type_test_expect(test, table.types.count == BUSTER_TYPE_BUILTIN_COUNT + 1 &&
            type_pointer(&table, BUSTER_TYPE_INT) != 0, "documented monotonic interning retains the valid subtype created before exhaustion");
    }
    arena_release(&index);
    arena_release(&storage);
}

static int run_builtin_intrinsic_type_tests(int argc, char** argv)
{
    (void)argv;
    BuiltinIntrinsicTypeTest test = {0};
    const bool options = argc == 0;
    builtin_intrinsic_type_test_expect(&test, options, "usage: test-builtin-intrinsic-types");
    if (options)
    {
        CheckTestTypes types = {0};
        const bool initialized = check_test_types_open(&types);
        builtin_intrinsic_type_test_expect(&test, initialized, "intrinsic prototype type-table allocation");
        if (initialized)
        {
            builtin_intrinsic_type_test_scalars(&test, &types.table);
            builtin_intrinsic_type_test_derived(&test, &types.table);
            builtin_intrinsic_type_test_invalid(&test, &types.table);
            builtin_intrinsic_type_test_pages(&test, &types.table);
        }
        check_test_types_close(&types);
        builtin_intrinsic_type_test_capacity(&test);
    }
    printf("test-builtin-intrinsic-types: %" PRIu64 " checks, %" PRIu64 " failures\n", test.checks, test.failures);
    const int result = test.failures != 0;
    return result;
}
