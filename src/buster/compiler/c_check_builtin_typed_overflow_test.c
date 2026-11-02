// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #148's fixed-prototype typed overflow actions. Integer and floating conversions are independently performed by the
// host C compiler, then Clang's actual __builtin_umull_overflow (and for every builtin of the family, the host's own:
// check_builtin_typed_overflow_test_every) supplies stored bits and boolean.
// The action's destination/store/effects remain explicit even when folded. Guarded batches compare both paths and
// legal partitions; external source probes check Clang's prototype and constraints. No C-source lowering is claimed.

#include <limits.h>

#define BUSTER_CHECK_TYPED_OVERFLOW_TEST_BATCH 17u

typedef struct CheckBuiltinTypedOverflowTest CheckBuiltinTypedOverflowTest;
struct CheckBuiltinTypedOverflowTest
{
    uint64_t checks;
    uint64_t failures;
    uint64_t random;
};

static void check_builtin_typed_overflow_test_expect(CheckBuiltinTypedOverflowTest* test, bool condition, const char* description)
{
    test->checks += 1;
    if (!condition)
    {
        test->failures += 1;
        if (test->failures <= 24)
        {
            fprintf(stderr, "test-check-builtins-typed-overflow: %s\n", description);
        }
    }
}

static bool check_builtin_typed_overflow_test_equal(const CheckBuiltinTypedOverflowResult* a, const CheckBuiltinTypedOverflowResult* b)
{
    bool equal = check_test_action_equal(&a->builtin.action, &b->builtin.action) && check_test_value_equal(&a->builtin.store_value, &b->builtin.store_value) &&
        check_test_value_equal(&a->builtin.store_destination, &b->builtin.store_destination) && a->builtin.fold_status == b->builtin.fold_status && a->builtin.store == b->builtin.store &&
        a->evaluated_operands == b->evaluated_operands && a->warnings == b->warnings && a->warning_operands == b->warning_operands && a->unsupported == b->unsupported && a->runtime_builtin == b->runtime_builtin;
    for (uint32_t i = 0; i < 3; i += 1)
    {
        equal &= a->argument_conversions[i].type == b->argument_conversions[i].type && a->argument_conversions[i].flags == b->argument_conversions[i].flags;
    }
    return equal;
}

static uint64_t check_builtin_typed_overflow_test_random(CheckBuiltinTypedOverflowTest* test)
{
    test->random ^= test->random << 13;
    test->random ^= test->random >> 7;
    test->random ^= test->random << 17;
    return test->random;
}

static __attribute__((noinline)) unsigned long check_builtin_typed_overflow_test_convert(TypeId type, uint64_t bits)
{
    unsigned long result = bits;
    switch (type)
    {
    case BUSTER_TYPE_BOOL: result = (_Bool)(bits & 1); break;
    case BUSTER_TYPE_SCHAR: result = (signed char)bits; break;
    case BUSTER_TYPE_UCHAR: result = (unsigned char)bits; break;
    case BUSTER_TYPE_SHORT: result = (short)bits; break;
    case BUSTER_TYPE_USHORT: result = (unsigned short)bits; break;
    case BUSTER_TYPE_INT: result = (int)bits; break;
    case BUSTER_TYPE_UINT: result = (unsigned int)bits; break;
    case BUSTER_TYPE_LONG: result = (long)bits; break;
    case BUSTER_TYPE_ULONG: result = (unsigned long)bits; break;
    case BUSTER_TYPE_LLONG: result = (long long)bits; break;
    case BUSTER_TYPE_ULLONG: result = (unsigned long long)bits; break;
    }
    return result;
}

static __attribute__((noinline)) bool check_builtin_typed_overflow_test_oracle(unsigned long a, unsigned long b, unsigned long* stored)
{
    const bool result = __builtin_umull_overflow(a, b, stored);
    return result;
}

static CheckBuiltinAction check_builtin_typed_overflow_test_input(TypeTable* table, CheckValue a, CheckValue b)
{
    const CheckBuiltinAction result =
    {
        .id = CBUILTIN_UMULL_OVERFLOW,
        .operands =
        {
            a,
            b,
            {
                .type = type_pointer(table, BUSTER_TYPE_ULONG),
                .category = CHECK_RVALUE,
                .constant_kind = CHECK_CONSTANT_ADDRESS,
                .object = 148,
                .offset = 16,
            },
        },
        .argument_count = 3,
        .node = 148,
        .context.source_position = UINT32_MAX,
    };
    return result;
}

static CheckBuiltinTypedOverflowResult check_builtin_typed_overflow_test_action(CheckBuiltinTypedOverflowTest* test, TypeTable* table, const CheckBuiltinAction* input)
{
    CheckBuiltinTypedOverflowResult scalar;
    CheckBuiltinTypedOverflowResult vector;
    check_builtin_typed_overflow_one(table, input, &scalar, true);
    check_builtin_typed_overflow_one(table, input, &vector, false);
    check_builtin_typed_overflow_test_expect(test, check_builtin_typed_overflow_test_equal(&scalar, &vector), "scalar/vector typed overflow action twins");
    return scalar;
}

static void check_builtin_typed_overflow_test_fold(CheckBuiltinTypedOverflowTest* test, TypeTable* table, CheckBuiltinAction input, unsigned long a, unsigned long b)
{
    unsigned long stored = 0;
    const bool overflow = check_builtin_typed_overflow_test_oracle(a, b, &stored);
    const CheckBuiltinTypedOverflowResult result = check_builtin_typed_overflow_test_action(test, table, &input);
    check_builtin_typed_overflow_test_expect(test, result.builtin.action.diagnostic == CHECK_DIAG_NONE && result.unsupported == 0 && result.builtin.fold_status == CBUILTIN_FOLDED &&
        result.builtin.action.value.type == BUSTER_TYPE_BOOL && result.builtin.action.value.constant_kind == CHECK_CONSTANT_INTEGER && result.builtin.action.value.integer == overflow &&
        result.builtin.store && result.builtin.store_value.type == BUSTER_TYPE_ULONG && result.builtin.store_value.constant_kind == CHECK_CONSTANT_INTEGER && result.builtin.store_value.integer == stored,
        "Clang fixed-prototype converted multiplication, stored bits and overflow");
    check_builtin_typed_overflow_test_expect(test, result.evaluated_operands == 7 && result.runtime_builtin == CBUILTIN_MUL_OVERFLOW &&
        result.argument_conversions[0].type == BUSTER_TYPE_ULONG && result.argument_conversions[1].type == BUSTER_TYPE_ULONG &&
        result.argument_conversions[2].type == type_pointer(table, BUSTER_TYPE_ULONG) && result.builtin.action.store_conversion.type == BUSTER_TYPE_ULONG &&
        result.builtin.store_destination.object == 148 && result.builtin.store_destination.offset == 16 &&
        (result.builtin.action.value.flags & BUSTER_CHECK_SIDE_EFFECT) != 0 && (result.builtin.action.value.flags & BUSTER_CHECK_ICE) == 0,
        "prototype conversions and mandatory effectful store survive folding");
}

static void check_builtin_typed_overflow_test_values(CheckBuiltinTypedOverflowTest* test, TypeTable* table)
{
    const TypeId types[] =
    {
        BUSTER_TYPE_BOOL,
        BUSTER_TYPE_SCHAR,
        BUSTER_TYPE_UCHAR,
        BUSTER_TYPE_SHORT,
        BUSTER_TYPE_USHORT,
        BUSTER_TYPE_INT,
        BUSTER_TYPE_UINT,
        BUSTER_TYPE_LONG,
        BUSTER_TYPE_ULONG,
        BUSTER_TYPE_LLONG,
        BUSTER_TYPE_ULLONG,
    };
    const uint64_t boundaries[] = { 0, 1, 2, UINT64_MAX, UINT64_MAX - 1, UINT64_C(0x8000000000000000), UINT64_C(0x7fffffffffffffff), UINT64_C(0x5555555555555555) };
    for (uint32_t at = 0; at < BUSTER_ARRAY_LENGTH(types); at += 1)
    {
        for (uint32_t bt = 0; bt < BUSTER_ARRAY_LENGTH(types); bt += 1)
        {
            for (uint32_t item = 0; item < 24; item += 1)
            {
                uint64_t a = item < BUSTER_ARRAY_LENGTH(boundaries) ? boundaries[item] : check_builtin_typed_overflow_test_random(test);
                uint64_t b = item < BUSTER_ARRAY_LENGTH(boundaries) ? boundaries[BUSTER_ARRAY_LENGTH(boundaries) - 1 - item] : check_builtin_typed_overflow_test_random(test);
                a = types[at] == BUSTER_TYPE_BOOL ? a & 1 : a;
                b = types[bt] == BUSTER_TYPE_BOOL ? b & 1 : b;
                CheckBuiltinAction input = check_builtin_typed_overflow_test_input(table, check_test_integer(types[at], a), check_test_integer(types[bt], b));
                check_builtin_typed_overflow_test_fold(test, table, input, check_builtin_typed_overflow_test_convert(types[at], a), check_builtin_typed_overflow_test_convert(types[bt], b));
            }
        }
    }
    const double floating[] = { -0.75, 0.0, 1.75, 3.75, 0x1.fffffffffffffp63 };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(floating); i += 1)
    {
        const unsigned long converted = (unsigned long)floating[i];
        const CheckBuiltinAction input = check_builtin_typed_overflow_test_input(table, check_test_float(BUSTER_TYPE_DOUBLE, floating[i]), check_test_integer(BUSTER_TYPE_INT, 3));
        check_builtin_typed_overflow_test_fold(test, table, input, converted, 3);
    }
    CheckBuiltinAction input = check_builtin_typed_overflow_test_input(table, check_test_integer(BUSTER_TYPE_INT, UINT32_MAX), check_test_integer(BUSTER_TYPE_INT, UINT32_MAX));
    check_builtin_typed_overflow_test_fold(test, table, input, ULONG_MAX, ULONG_MAX);
    input.operands[0] = check_test_integer(BUSTER_TYPE_UINT128, ((unsigned __int128)1 << 100) | 3);
    input.operands[1] = check_test_integer(BUSTER_TYPE_INT, 2);
    check_builtin_typed_overflow_test_fold(test, table, input, 3, 2);
    const TypeId enumeration = type_tag(table, BUSTER_TYPE_KIND_ENUM, 148);
    const bool completed = enumeration != 0 && type_enum_complete(table, enumeration, BUSTER_TYPE_INT, 0);
    check_builtin_typed_overflow_test_expect(test, completed, "typed overflow enum fixture allocation");
    if (completed)
    {
        input.operands[0] = check_test_integer(enumeration, UINT32_MAX);
        check_builtin_typed_overflow_test_fold(test, table, input, ULONG_MAX, 2);
    }
    input.operands[0] = (CheckValue){ .type = type_qualify(table, BUSTER_TYPE_INT, BUSTER_TYPE_VOLATILE), .category = CHECK_LVALUE };
    CheckBuiltinTypedOverflowResult result = check_builtin_typed_overflow_test_action(test, table, &input);
    check_builtin_typed_overflow_test_expect(test, result.builtin.action.diagnostic == CHECK_DIAG_NONE && result.builtin.fold_status == BUSTER_CHECK_BUILTIN_NOT_CONSTANT && result.builtin.store &&
        result.builtin.action.value.constant_kind == CHECK_CONSTANT_NONE && (result.builtin.action.value.flags & BUSTER_CHECK_SIDE_EFFECT) != 0 &&
        (result.argument_conversions[0].flags & BUSTER_CHECK_CONVERT_LVALUE) != 0, "unknown volatile source remains an effectful runtime operation");
    input.operands[0] = check_test_integer(BUSTER_TYPE_INT, 3);
    input.operands[0].fault = CHECK_FAULT_DIVIDE_BY_ZERO;
    input.operands[0].fault_node = 73;
    result = check_builtin_typed_overflow_test_action(test, table, &input);
    check_builtin_typed_overflow_test_expect(test, result.builtin.action.value.fault == CHECK_FAULT_DIVIDE_BY_ZERO && result.builtin.action.value.fault_node == 73 &&
        result.builtin.action.value.constant_kind == CHECK_CONSTANT_NONE && result.builtin.store, "evaluated input fault retains provenance and required runtime store");
}

// The host compiler's own typed overflow builtin `id` of `a` and `b`, the source values as their types give them,
// converted to the builtin's prototype's type as a call converts them: the overflow, and the stored value's bits.
static __attribute__((noinline)) bool check_builtin_typed_overflow_test_host(CBuiltinId id, unsigned long a, unsigned long b, unsigned __int128* stored)
{
    bool result = false;
    int si = 0;
    long sl = 0;
    long long sll = 0;
    unsigned ui = 0;
    unsigned long ul = 0;
    unsigned long long ull = 0;
    *stored = 0;
    switch (id)
    {
    case CBUILTIN_SADD_OVERFLOW:
        result = __builtin_sadd_overflow((int)a, (int)b, &si);
        *stored = (unsigned __int128)si;
        break;
    case CBUILTIN_SADDL_OVERFLOW:
        result = __builtin_saddl_overflow((long)a, (long)b, &sl);
        *stored = (unsigned __int128)sl;
        break;
    case CBUILTIN_SADDLL_OVERFLOW:
        result = __builtin_saddll_overflow((long long)a, (long long)b, &sll);
        *stored = (unsigned __int128)sll;
        break;
    case CBUILTIN_SSUB_OVERFLOW:
        result = __builtin_ssub_overflow((int)a, (int)b, &si);
        *stored = (unsigned __int128)si;
        break;
    case CBUILTIN_SSUBL_OVERFLOW:
        result = __builtin_ssubl_overflow((long)a, (long)b, &sl);
        *stored = (unsigned __int128)sl;
        break;
    case CBUILTIN_SSUBLL_OVERFLOW:
        result = __builtin_ssubll_overflow((long long)a, (long long)b, &sll);
        *stored = (unsigned __int128)sll;
        break;
    case CBUILTIN_SMUL_OVERFLOW:
        result = __builtin_smul_overflow((int)a, (int)b, &si);
        *stored = (unsigned __int128)si;
        break;
    case CBUILTIN_SMULL_OVERFLOW:
        result = __builtin_smull_overflow((long)a, (long)b, &sl);
        *stored = (unsigned __int128)sl;
        break;
    case CBUILTIN_SMULLL_OVERFLOW:
        result = __builtin_smulll_overflow((long long)a, (long long)b, &sll);
        *stored = (unsigned __int128)sll;
        break;
    case CBUILTIN_UADD_OVERFLOW:
        result = __builtin_uadd_overflow((unsigned)a, (unsigned)b, &ui);
        *stored = (unsigned __int128)ui;
        break;
    case CBUILTIN_UADDL_OVERFLOW:
        result = __builtin_uaddl_overflow((unsigned long)a, (unsigned long)b, &ul);
        *stored = (unsigned __int128)ul;
        break;
    case CBUILTIN_UADDLL_OVERFLOW:
        result = __builtin_uaddll_overflow((unsigned long long)a, (unsigned long long)b, &ull);
        *stored = (unsigned __int128)ull;
        break;
    case CBUILTIN_USUB_OVERFLOW:
        result = __builtin_usub_overflow((unsigned)a, (unsigned)b, &ui);
        *stored = (unsigned __int128)ui;
        break;
    case CBUILTIN_USUBL_OVERFLOW:
        result = __builtin_usubl_overflow((unsigned long)a, (unsigned long)b, &ul);
        *stored = (unsigned __int128)ul;
        break;
    case CBUILTIN_USUBLL_OVERFLOW:
        result = __builtin_usubll_overflow((unsigned long long)a, (unsigned long long)b, &ull);
        *stored = (unsigned __int128)ull;
        break;
    case CBUILTIN_UMUL_OVERFLOW:
        result = __builtin_umul_overflow((unsigned)a, (unsigned)b, &ui);
        *stored = (unsigned __int128)ui;
        break;
    case CBUILTIN_UMULL_OVERFLOW:
        result = __builtin_umull_overflow((unsigned long)a, (unsigned long)b, &ul);
        *stored = (unsigned __int128)ul;
        break;
    case CBUILTIN_UMULLL_OVERFLOW:
        result = __builtin_umulll_overflow((unsigned long long)a, (unsigned long long)b, &ull);
        *stored = (unsigned __int128)ull;
        break;
    default:
        break;
    }
    return result;
}

// Every typed overflow builtin (check_builtin_typed_overflows) against the host compiler's own, of every pair of
// integer source types and boundary or random values: the overflow, the stored bits at the prototype's width, the
// conversions to the prototype and the generic builtin it runs as.
static void check_builtin_typed_overflow_test_every(CheckBuiltinTypedOverflowTest* test, TypeTable* table)
{
    const TypeId types[] = { BUSTER_TYPE_SCHAR, BUSTER_TYPE_USHORT, BUSTER_TYPE_INT, BUSTER_TYPE_UINT, BUSTER_TYPE_LONG, BUSTER_TYPE_ULLONG };
    const uint64_t boundaries[] = { 0, 1, 2, UINT64_MAX, UINT64_C(0x7fffffff), UINT64_C(0x80000000), UINT64_C(0xffffffff), UINT64_C(0x8000000000000000),
                                    UINT64_C(0x7fffffffffffffff) };
    for (uint32_t row = 0; row < BUSTER_ARRAY_LENGTH(check_builtin_typed_overflows); row += 1)
    {
        const CheckBuiltinTypedOverflowInfo* info = check_builtin_typed_overflows + row;
        uint32_t width = 0;
        bool signed_type = false;
        check_action_integer_type(table, info->type, &width, &signed_type);
        const unsigned __int128 mask = check_action_mask(width);
        for (uint32_t at = 0; at < BUSTER_ARRAY_LENGTH(types); at += 1)
        {
            for (uint32_t bt = 0; bt < BUSTER_ARRAY_LENGTH(types); bt += 1)
            {
                for (uint32_t item = 0; item < 12; item += 1)
                {
                    const uint64_t a = item < BUSTER_ARRAY_LENGTH(boundaries) ? boundaries[item] : check_builtin_typed_overflow_test_random(test);
                    const uint64_t b = item < BUSTER_ARRAY_LENGTH(boundaries) ? boundaries[(item * 5 + at + bt) % BUSTER_ARRAY_LENGTH(boundaries)] :
                                                                                check_builtin_typed_overflow_test_random(test);
                    CheckBuiltinAction input = check_builtin_typed_overflow_test_input(table, check_test_integer(types[at], a), check_test_integer(types[bt], b));
                    input.id = info->id;
                    input.operands[2].type = type_pointer(table, info->type);
                    unsigned __int128 stored = 0;
                    const bool overflow = check_builtin_typed_overflow_test_host(info->id, check_builtin_typed_overflow_test_convert(types[at], a),
                                                                                 check_builtin_typed_overflow_test_convert(types[bt], b), &stored);
                    const CheckBuiltinTypedOverflowResult result = check_builtin_typed_overflow_test_action(test, table, &input);
                    check_builtin_typed_overflow_test_expect(test, result.builtin.action.diagnostic == CHECK_DIAG_NONE && result.builtin.fold_status == CBUILTIN_FOLDED &&
                        result.builtin.action.value.constant_kind == CHECK_CONSTANT_INTEGER && result.builtin.action.value.integer == overflow && result.builtin.store &&
                        result.builtin.store_value.type == info->type && result.builtin.store_value.constant_kind == CHECK_CONSTANT_INTEGER &&
                        (result.builtin.store_value.integer & mask) == (stored & mask),
                        "every typed overflow builtin: the host compiler's overflow and stored bits");
                    check_builtin_typed_overflow_test_expect(test, result.runtime_builtin == info->generic && result.argument_conversions[0].type == info->type &&
                        result.argument_conversions[1].type == info->type && result.argument_conversions[2].type == type_pointer(table, info->type),
                        "every typed overflow builtin: its prototype's conversions and its generic builtin");
                }
            }
        }
    }
}

static void check_builtin_typed_overflow_test_constraints(CheckBuiltinTypedOverflowTest* test, TypeTable* table)
{
    CheckBuiltinAction input = check_builtin_typed_overflow_test_input(table, check_test_integer(BUSTER_TYPE_INT, 3), check_test_integer(BUSTER_TYPE_INT, 7));
    input.argument_count = 2;
    CheckBuiltinTypedOverflowResult result = check_builtin_typed_overflow_test_action(test, table, &input);
    check_builtin_typed_overflow_test_expect(test, result.builtin.action.diagnostic == CHECK_DIAG_ARGUMENT_COUNT && !result.builtin.store, "typed multiply requires exactly three operands");
    input.argument_count = 3;
    input.id = CBUILTIN_MUL_OVERFLOW;
    result = check_builtin_typed_overflow_test_action(test, table, &input);
    check_builtin_typed_overflow_test_expect(test, result.builtin.action.diagnostic == CHECK_DIAG_UNSUPPORTED && !result.builtin.store, "fixed-prototype wrapper does not silently claim generic IDs");
    input.id = CBUILTIN_UMULL_OVERFLOW;
    input.operands[0] = input.operands[2];
    result = check_builtin_typed_overflow_test_action(test, table, &input);
    check_builtin_typed_overflow_test_expect(test, result.builtin.action.diagnostic != CHECK_DIAG_NONE && result.builtin.action.diagnostic_operand == 0 && !result.builtin.store,
        "ordinary unsigned-long value parameter rejects a pointer");
    input.operands[0] = check_test_integer(BUSTER_TYPE_INT, 3);
    const TypeId incompatible[] = { BUSTER_TYPE_UINT, BUSTER_TYPE_ULLONG, BUSTER_TYPE_DOUBLE };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(incompatible); i += 1)
    {
        input.operands[2].type = type_pointer(table, incompatible[i]);
        result = check_builtin_typed_overflow_test_action(test, table, &input);
        check_builtin_typed_overflow_test_expect(test, result.builtin.action.diagnostic != CHECK_DIAG_NONE && result.builtin.action.diagnostic_operand == 2 && !result.builtin.store,
            "destination obeys unsigned-long pointer compatibility rather than generic integer-pointer rule");
    }
    const TypeId qualifiers[] =
    {
        type_qualify(table, BUSTER_TYPE_ULONG, BUSTER_TYPE_CONST),
        type_qualify(table, BUSTER_TYPE_ULONG, BUSTER_TYPE_VOLATILE),
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(qualifiers); i += 1)
    {
        input.operands[2].type = type_pointer(table, qualifiers[i]);
        result = check_builtin_typed_overflow_test_action(test, table, &input);
        check_builtin_typed_overflow_test_expect(test, result.builtin.action.diagnostic == CHECK_DIAG_NONE && (result.warnings & BUSTER_CHECK_BUILTIN_TYPED_OVERFLOW_WARN_QUALIFIERS) != 0 && (result.warning_operands & 4) != 0 &&
            result.argument_conversions[2].type == type_pointer(table, BUSTER_TYPE_ULONG) && result.builtin.action.store_conversion.type == BUSTER_TYPE_ULONG && result.builtin.store,
            "ordinary pointer conversion warns on qualifier loss and retains the converted unqualified store");
    }
    input.operands[2].type = type_pointer(table, BUSTER_TYPE_LONG);
    result = check_builtin_typed_overflow_test_action(test, table, &input);
    check_builtin_typed_overflow_test_expect(test, result.builtin.action.diagnostic == CHECK_DIAG_NONE && (result.warnings & BUSTER_CHECK_BUILTIN_TYPED_OVERFLOW_WARN_POINTER_SIGN) != 0 && (result.warning_operands & 4) != 0 && result.builtin.store,
        "signed-long destination uses ordinary pointer-sign warning");
    input.operands[2].type = type_pointer(table, BUSTER_TYPE_VOID);
    result = check_builtin_typed_overflow_test_action(test, table, &input);
    check_builtin_typed_overflow_test_expect(test, result.builtin.action.diagnostic == CHECK_DIAG_NONE && result.builtin.store && result.argument_conversions[2].type == type_pointer(table, BUSTER_TYPE_ULONG),
        "void pointer takes the ordinary destination conversion");
    input.operands[2] = check_test_integer(BUSTER_TYPE_INT, 0);
    result = check_builtin_typed_overflow_test_action(test, table, &input);
    check_builtin_typed_overflow_test_expect(test, result.builtin.action.diagnostic == CHECK_DIAG_NONE && result.builtin.store && result.builtin.store_destination.constant_kind == CHECK_CONSTANT_ADDRESS &&
        result.builtin.store_destination.object == 0 && result.builtin.store_destination.offset == 0, "null pointer conversion is accepted without erasing the required store");
    input.operands[2] = check_builtin_typed_overflow_test_input(table, input.operands[0], input.operands[1]).operands[2];
    input.operands[2].type = type_array(table, BUSTER_TYPE_ULONG, 1);
    input.operands[2].category = CHECK_LVALUE;
    input.operands[2].flags = BUSTER_CHECK_STATIC_OBJECT;
    result = check_builtin_typed_overflow_test_action(test, table, &input);
    check_builtin_typed_overflow_test_expect(test, result.builtin.action.diagnostic == CHECK_DIAG_NONE && result.builtin.store &&
        (result.argument_conversions[2].flags & BUSTER_CHECK_CONVERT_ARRAY) != 0, "destination array decays before prototype conversion");
    input = check_builtin_typed_overflow_test_input(table, check_test_float(BUSTER_TYPE_FLOAT128, 3), check_test_integer(BUSTER_TYPE_INT, 7));
    result = check_builtin_typed_overflow_test_action(test, table, &input);
    check_builtin_typed_overflow_test_expect(test, result.builtin.action.diagnostic == CHECK_DIAG_NONE && (result.unsupported & BUSTER_CHECK_BUILTIN_TYPED_OVERFLOW_UNSUPPORTED_CONVERSION) != 0 && result.builtin.store && result.evaluated_operands == 7 &&
        result.builtin.action.value.constant_kind == CHECK_CONSTANT_NONE && result.builtin.fold_status == BUSTER_CHECK_BUILTIN_NOT_CONSTANT,
        "unrepresented extended floating conversion keeps required runtime store without invented fold");
    input.operands[0] = (CheckValue){ .type = type_complex(table, BUSTER_TYPE_DOUBLE), .category = CHECK_RVALUE };
    result = check_builtin_typed_overflow_test_action(test, table, &input);
    check_builtin_typed_overflow_test_expect(test, result.builtin.action.diagnostic == CHECK_DIAG_NONE && (result.unsupported & BUSTER_CHECK_BUILTIN_TYPED_OVERFLOW_UNSUPPORTED_CONVERSION) != 0 && result.builtin.store &&
        result.builtin.action.value.constant_kind == CHECK_CONSTANT_NONE && result.builtin.fold_status == BUSTER_CHECK_BUILTIN_NOT_CONSTANT,
        "valid complex-to-real prototype conversion retains unsupported payload boundary");
    input.operands[0] = (CheckValue){ .type = type_bit_int(table, 257, true), .category = CHECK_RVALUE };
    result = check_builtin_typed_overflow_test_action(test, table, &input);
    check_builtin_typed_overflow_test_expect(test, result.builtin.action.diagnostic == CHECK_DIAG_NONE && (result.unsupported & BUSTER_CHECK_BUILTIN_TYPED_OVERFLOW_UNSUPPORTED_WIDE_INTEGER) != 0 && result.builtin.store &&
        result.builtin.action.value.constant_kind == CHECK_CONSTANT_NONE && result.builtin.fold_status == BUSTER_CHECK_BUILTIN_NOT_CONSTANT,
        "wide source conversion is explicit unsupported metadata rather than truncated payload");
    const TypeId enumeration = type_tag(table, BUSTER_TYPE_KIND_ENUM, 149);
    const bool complete = enumeration != 0 && type_enum_complete(table, enumeration, BUSTER_TYPE_INT, 0);
    check_builtin_typed_overflow_test_expect(test, complete, "typed-overflow temporal enum fixture allocation");
    if (complete)
    {
        uint32_t completions[512] = { 0 };
        const uint32_t tag = table->types.index[type_index(enumeration)];
        completions[tag] = 20;
        input = check_builtin_typed_overflow_test_input(table, check_test_integer(enumeration, UINT32_MAX), check_test_integer(BUSTER_TYPE_INT, 2));
        input.context.tag_completions = completions;
        input.context.tag_completion_count = BUSTER_ARRAY_LENGTH(completions);
        input.node = 999;
        input.context.source_position = 19;
        result = check_builtin_typed_overflow_test_action(test, table, &input);
        check_builtin_typed_overflow_test_expect(test, result.builtin.action.diagnostic == CHECK_DIAG_INCOMPLETE && result.builtin.action.diagnostic_operand == 0 && !result.builtin.store,
            "late enum completion is checked before ordinary prototype conversion");
        input.context.source_position = 20;
        check_builtin_typed_overflow_test_fold(test, table, input, ULONG_MAX, 2);
    }
}

static void check_builtin_typed_overflow_test_pages(CheckBuiltinTypedOverflowTest* test, TypeTable* table)
{
    const long page_value = sysconf(_SC_PAGESIZE);
    const size_t page = page_value > 0 ? (size_t)page_value : 0;
    const size_t input_region = page != 0 ? ((BUSTER_CHECK_TYPED_OVERFLOW_TEST_BATCH * sizeof(CheckBuiltinAction) + 16 + page - 1) / page) * page : 0;
    const size_t output_region = page != 0 ? ((BUSTER_CHECK_TYPED_OVERFLOW_TEST_BATCH * sizeof(CheckBuiltinTypedOverflowResult) + 16 + page - 1) / page) * page : 0;
    const size_t bytes = input_region + output_region + 2 * page;
    uint8_t* mapping = page != 0 ? mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    bool ready = mapping != MAP_FAILED;
    uint8_t* input_end = ready ? mapping + input_region : NULL;
    uint8_t* output_end = ready ? input_end + page + output_region : NULL;
    if (ready)
    {
        ready = mprotect(input_end, page, PROT_NONE) == 0 && mprotect(output_end, page, PROT_NONE) == 0;
    }
    check_builtin_typed_overflow_test_expect(test, ready, "guarded typed-overflow action/result slices allocated");
    for (uint32_t count = 0; ready && count <= BUSTER_CHECK_TYPED_OVERFLOW_TEST_BATCH; count += 1)
    {
        CheckBuiltinAction* actions = (CheckBuiltinAction*)(input_end - count * sizeof(*actions));
        CheckBuiltinTypedOverflowResult* results = (CheckBuiltinTypedOverflowResult*)(output_end - count * sizeof(*results));
        CheckBuiltinTypedOverflowResult expected[BUSTER_CHECK_TYPED_OVERFLOW_TEST_BATCH];
        memset((uint8_t*)actions - 16, 0xa5, 16 + count * sizeof(*actions));
        memset((uint8_t*)results - 16, 0xa5, 16 + count * sizeof(*results));
        for (uint32_t i = 0; i < count; i += 1)
        {
            actions[i] = check_builtin_typed_overflow_test_input(table, check_test_integer(BUSTER_TYPE_INT, UINT32_MAX - i), check_test_integer(BUSTER_TYPE_UCHAR, i));
            check_builtin_typed_overflow_one(table, actions + i, expected + i, true);
        }
        for (uint32_t scalar = 0; scalar < 2; scalar += 1)
        {
            check_builtin_typed_overflow(table, actions, results, count, scalar != 0);
            for (uint32_t i = 0; i < count; i += 1)
            {
                check_builtin_typed_overflow_test_expect(test, check_builtin_typed_overflow_test_equal(expected + i, results + i), "guarded typed-overflow batch equals individual scalar actions");
            }
            for (uint32_t partition = 1; partition <= 5; partition += 1)
            {
                for (uint32_t first = 0; first < count; first += partition)
                {
                    const uint32_t remaining = count - first < partition ? count - first : partition;
                    check_builtin_typed_overflow(table, actions + first, results + first, remaining, scalar != 0);
                }
                for (uint32_t i = 0; i < count; i += 1)
                {
                    check_builtin_typed_overflow_test_expect(test, check_builtin_typed_overflow_test_equal(expected + i, results + i), "legal typed-overflow partition preserves semantic metadata");
                }
            }
        }
        for (uint32_t i = 1; i <= 16; i += 1)
        {
            check_builtin_typed_overflow_test_expect(test, *((uint8_t*)actions - i) == 0xa5 && *((uint8_t*)results - i) == 0xa5, "typed-overflow batch preserves leading slice guards");
        }
    }
    check_builtin_typed_overflow(NULL, NULL, NULL, 0, false);
    check_builtin_typed_overflow(NULL, NULL, NULL, 0, true);
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, bytes);
    }
}

static void check_builtin_typed_overflow_test_clang(CheckBuiltinTypedOverflowTest* test, const char* clang)
{
    static const struct
    {
        const char* source;
        bool rejected;
    } cases[] =
    {
        { "_Static_assert(_Generic(__builtin_umull_overflow(1,2,(unsigned long*)0), _Bool:1, default:0), \"bool result\");\n", false },
        { "_Bool f(unsigned long* p) { return __builtin_umull_overflow(-1,-1,p); }\n", false },
        { "_Bool f(unsigned long* p) { return __builtin_umull_overflow(3.75,2.5,p); }\n", false },
        { "enum E { N=-1 }; _Bool f(unsigned long* p) { return __builtin_umull_overflow(N,(_Bool)1,p); }\n", false },
        { "_Bool f(const unsigned long* p) { return __builtin_umull_overflow(2,3,p); }\n", false },
        { "_Bool f(volatile unsigned long* p) { return __builtin_umull_overflow(2,3,p); }\n", false },
        { "_Bool f(long* p) { return __builtin_umull_overflow(2,3,p); }\n", false },
        { "_Bool f(void* p) { return __builtin_umull_overflow(2,3,p); }\n", false },
        { "_Bool f(void) { return __builtin_umull_overflow(2,3,0); }\n", false },
        { "_Bool f(void) { unsigned long p[1]; return __builtin_umull_overflow(2,3,p); }\n", false },
        { "_Bool f(unsigned __int128 a,unsigned long* p) { return __builtin_umull_overflow(a,2,p); }\n", false },
        { "_Bool f(unsigned _BitInt(257) a,unsigned long* p) { return __builtin_umull_overflow(a,2,p); }\n", false },
        { "_Bool f(double _Complex a,unsigned long* p) { return __builtin_umull_overflow(a,2,p); }\n", false },
        { "_Bool f(unsigned int* p) { return __builtin_umull_overflow(2,3,p); }\n", true },
        { "_Bool f(unsigned long long* p) { return __builtin_umull_overflow(2,3,p); }\n", true },
        { "_Bool f(double* p) { return __builtin_umull_overflow(2,3,p); }\n", true },
        { "_Bool f(unsigned long* p,int* x) { return __builtin_umull_overflow(x,3,p); }\n", true },
        { "_Bool f(unsigned long* p) { return __builtin_umull_overflow(2,p); }\n", true },
        { "_Bool f(unsigned long* p) { return __builtin_umull_overflow(2,3,p,0); }\n", true },
    };
    char directory[] = "/tmp/buster-check-typed-overflow-XXXXXX";
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
    check_builtin_typed_overflow_test_expect(test, ready, "independent Clang typed-overflow prototype/constraint oracle ran");
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        check_builtin_typed_overflow_test_expect(test, results[i].rejected == cases[i].rejected, "Clang agrees with original typed-overflow source constraint fixture");
    }
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        unlink(paths[i]);
    }
    rmdir(directory);
}

static int run_check_builtin_typed_overflow_tests(int argc, char** argv)
{
    const bool epoch = oracle_epoch_begin();
    CheckBuiltinTypedOverflowTest test = { .random = 148 };
    const char* clang = "clang";
    bool options = true;
    for (int i = 0; options && i < argc; i += 1)
    {
        if (strcmp(argv[i], "--clang") == 0 && i + 1 < argc)
        {
            i += 1;
            clang = argv[i];
            options = *clang != 0;
        }
        else
        {
            options = false;
        }
    }
    check_builtin_typed_overflow_test_expect(&test, options, "usage: test-check-builtins-typed-overflow [--clang PATH]");
    CheckTestTypes types = { 0 };
    if (options)
    {
        const bool initialized = check_test_types_open(&types);
        check_builtin_typed_overflow_test_expect(&test, initialized, "type-table allocation");
        if (initialized)
        {
            check_builtin_typed_overflow_test_values(&test, &types.table);
            check_builtin_typed_overflow_test_every(&test, &types.table);
            check_builtin_typed_overflow_test_constraints(&test, &types.table);
            check_builtin_typed_overflow_test_pages(&test, &types.table);
        }
        check_test_types_close(&types);
        check_builtin_typed_overflow_test_clang(&test, clang);
    }
    printf("test-check-builtins-typed-overflow: %" PRIu64 " checks, %" PRIu64 " failures\n", test.checks, test.failures);
    const int result = test.failures != 0;
    oracle_cache_report(oracle_cache(), "test-check-builtins-typed-overflow");
    oracle_epoch_end(epoch);
    return result;
}
