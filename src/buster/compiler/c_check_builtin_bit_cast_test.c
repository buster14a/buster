// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original #148 bit-cast fixtures. Include after c_check_builtin_bit_cast.c and c_checker_test.c.
// Expected bytes are stated independently in little-endian storage order. No fixture loads a floating field:
// even the deliberately poisonous x87 representation is installed and inspected through character bytes.
// Guard pages bound all borrowed columns; individual and partitioned calls must agree byte for byte.

#include <stddef.h>
#include <sys/wait.h>

#define BUSTER_CHECK_BIT_CAST_TEST_BATCH 65u

typedef struct CheckBuiltinBitCastTest CheckBuiltinBitCastTest;
struct CheckBuiltinBitCastTest
{
    uint64_t checks;
    uint64_t failures;
};

static void check_builtin_bit_cast_test_expect(CheckBuiltinBitCastTest* test, bool condition, const char* description)
{
    test->checks += 1;
    if (!condition)
    {
        test->failures += 1;
        if (test->failures <= 24)
        {
            fprintf(stderr, "test-check-builtins-bit-cast: %s\n", description);
        }
    }
}

static CheckBuiltinBitCastAction check_builtin_bit_cast_test_action(TypeId destination, const CheckValue* source)
{
    const CheckBuiltinBitCastAction action =
    {
        .id = CBUILTIN_BIT_CAST,
        .destination_type = destination,
        .arguments = source,
        .argument_count = 1,
        .node = 148,
        .source_position = UINT32_MAX,
    };
    return action;
}

static CheckBuiltinBitCastResult check_builtin_bit_cast_test_apply(const TypeTable* table, CheckBuiltinBitCastAction action)
{
    CheckBuiltinBitCastResult result;
    memset(&result, 0xa5, sizeof(result));
    check_builtin_bit_cast_one(table, &action, &result);
    return result;
}

static bool check_builtin_bit_cast_test_bytes(const CheckBuiltinBitCastResult* result, const uint8_t* expected, uint32_t count)
{
    bool equal = result->payload_count == count && memcmp(result->bytes, expected, count) == 0;
    for (uint32_t i = count; equal && i < sizeof(result->bytes); i += 1)
    {
        equal = result->bytes[i] == 0;
    }
    return equal;
}

static bool check_builtin_bit_cast_test_no_conversions(const CheckBuiltinBitCastResult* result)
{
    bool empty = result->action.store_conversion.type == 0 && result->action.store_conversion.flags == 0;
    for (uint32_t i = 0; empty && i < 3; i += 1)
    {
        empty = result->action.conversions[i].type == 0 && result->action.conversions[i].flags == 0;
    }
    return empty;
}

static void check_builtin_bit_cast_test_integers(CheckBuiltinBitCastTest* test, TypeTable* table)
{
    static const struct
    {
        TypeId source;
        TypeId destination;
        uint32_t bytes;
    } types[] =
    {
        { BUSTER_TYPE_CHAR, BUSTER_TYPE_UCHAR, 1 },
        { BUSTER_TYPE_UCHAR, BUSTER_TYPE_SCHAR, 1 },
        { BUSTER_TYPE_SHORT, BUSTER_TYPE_USHORT, 2 },
        { BUSTER_TYPE_USHORT, BUSTER_TYPE_SHORT, 2 },
        { BUSTER_TYPE_INT, BUSTER_TYPE_UINT, 4 },
        { BUSTER_TYPE_UINT, BUSTER_TYPE_INT, 4 },
        { BUSTER_TYPE_LONG, BUSTER_TYPE_ULONG, 8 },
        { BUSTER_TYPE_ULLONG, BUSTER_TYPE_LLONG, 8 },
        { BUSTER_TYPE_INT128, BUSTER_TYPE_UINT128, 16 },
        { BUSTER_TYPE_UINT128, BUSTER_TYPE_INT128, 16 },
    };
    const unsigned __int128 patterns[] =
    {
        0,
        1,
        ~(unsigned __int128)0,
        (unsigned __int128)UINT64_C(0x0123456789abcdef) << 64 | UINT64_C(0xfedcba9876543210),
        (unsigned __int128)1 << 127,
    };
    for (uint32_t t = 0; t < BUSTER_ARRAY_LENGTH(types); t += 1)
    {
        for (uint32_t p = 0; p < BUSTER_ARRAY_LENGTH(patterns); p += 1)
        {
            uint8_t bytes[16] = { 0 };
            unsigned __int128 expected = 0;
            for (uint32_t i = 0; i < types[t].bytes; i += 1)
            {
                bytes[i] = (uint8_t)(patterns[p] >> (i * 8));
                expected |= (unsigned __int128)bytes[i] << (i * 8);
            }
            CheckValue source = check_test_integer(types[t].source, patterns[p]);
            CheckBuiltinBitCastAction action = check_builtin_bit_cast_test_action(types[t].destination, &source);
            for (uint32_t proof = 0; proof <= CHECK_BUILTIN_BIT_CAST_PROOF_ICE; proof += 1)
            {
                action.source_proof = (CheckBuiltinBitCastProof)proof;
                const CheckBuiltinBitCastResult result = check_builtin_bit_cast_test_apply(table, action);
                check_builtin_bit_cast_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.unsupported == 0 &&
                    result.source_type == types[t].source && result.representation_type == types[t].destination &&
                    result.action.value.type == types[t].destination && result.action.value.category == CHECK_RVALUE &&
                    result.byte_count == types[t].bytes && result.evaluated_operands == 1 && result.runtime_builtin == (proof != 0 ? CBUILTIN_NONE : CBUILTIN_BIT_CAST),
                    "ordinary integer types preserve exact storage and raw operand identity");
                check_builtin_bit_cast_test_expect(test, result.representation_known && check_builtin_bit_cast_test_bytes(&result, bytes, types[t].bytes) &&
                    result.folded == (proof != 0) && result.runtime_required == (proof == 0), "representation knowledge is independent of source proof");
                check_builtin_bit_cast_test_expect(test, result.action.value.constant_kind == (proof != 0 ? CHECK_CONSTANT_INTEGER : CHECK_CONSTANT_NONE) &&
                    result.action.value.integer == (proof != 0 ? expected : 0) &&
                    result.action.value.flags == (proof == CHECK_BUILTIN_BIT_CAST_PROOF_ICE ? BUSTER_CHECK_ICE : 0) &&
                    check_builtin_bit_cast_test_no_conversions(&result), "only certified integer source ICE propagates ICE without ordinary conversions");
            }
        }
    }
    CheckValue source = check_test_integer(BUSTER_TYPE_UINT, UINT32_C(0xaaaaaaaa));
    const uint8_t certificate[] = { 0x78, 0x56, 0x34, 0x12 };
    CheckBuiltinBitCastAction action = check_builtin_bit_cast_test_action(BUSTER_TYPE_INT, &source);
    action.source_bytes = certificate;
    action.source_byte_count = sizeof(certificate);
    action.source_proof = CHECK_BUILTIN_BIT_CAST_PROOF_ICE;
    CheckBuiltinBitCastResult result = check_builtin_bit_cast_test_apply(table, action);
    check_builtin_bit_cast_test_expect(test, result.folded && result.action.value.integer == UINT32_C(0x12345678) &&
        check_builtin_bit_cast_test_bytes(&result, certificate, 4), "full byte certificate takes precedence over operand numeric payload");
    source.constant_kind = CHECK_CONSTANT_NONE;
    action.source_bytes = NULL;
    action.source_byte_count = 0;
    result = check_builtin_bit_cast_test_apply(table, action);
    check_builtin_bit_cast_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && !result.folded && !result.representation_known &&
        result.unsupported == BUSTER_CHECK_BUILTIN_BIT_CAST_REPRESENTATION && result.runtime_required,
        "source proof cannot manufacture an absent object representation");
    for (uint32_t value = 0; value <= 2; value += 1)
    {
        for (uint32_t reverse = 0; reverse < 2; reverse += 1)
        {
            source = check_test_integer(reverse ? BUSTER_TYPE_BOOL : BUSTER_TYPE_UCHAR, value);
            action = check_builtin_bit_cast_test_action(reverse ? BUSTER_TYPE_UCHAR : BUSTER_TYPE_BOOL, &source);
            action.source_proof = CHECK_BUILTIN_BIT_CAST_PROOF_ICE;
            result = check_builtin_bit_cast_test_apply(table, action);
            check_builtin_bit_cast_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE &&
                result.folded == (value < 2) && result.representation_known == (value < 2) &&
                result.unsupported == (value < 2 ? 0 : BUSTER_CHECK_BUILTIN_BIT_CAST_INVALID_REPRESENTATION),
                "bool object representations zero and one are exact and raw two is an explicit gap");
        }
    }
}

static void check_builtin_bit_cast_test_floating(CheckBuiltinBitCastTest* test, TypeTable* table)
{
    static const struct
    {
        TypeId floating;
        TypeId integer;
        uint32_t bytes;
        uint8_t snan[8];
    } cases[] =
    {
        { BUSTER_TYPE_HALF, BUSTER_TYPE_USHORT, 2, { 0x01, 0x7c } },
        { BUSTER_TYPE_FLOAT16, BUSTER_TYPE_USHORT, 2, { 0x01, 0xfc } },
        { BUSTER_TYPE_FLOAT, BUSTER_TYPE_UINT, 4, { 0x01, 0x00, 0x80, 0x7f } },
        { BUSTER_TYPE_DOUBLE, BUSTER_TYPE_ULONG, 8, { 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0xff } },
    };
    const uint8_t x87_snan[sizeof(long double)] = { 1, 0, 0, 0, 0, 0, 0, 0x80, 0xff, 0x7f };
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(cases); c += 1)
    {
        CheckValue source = { 0 };
        source.type = cases[c].floating;
        source.category = CHECK_RVALUE;
        source.constant_kind = CHECK_CONSTANT_FLOATING;
        memcpy((uint8_t*)&source + offsetof(CheckValue, floating), x87_snan, sizeof(x87_snan));
        CheckBuiltinBitCastAction action = check_builtin_bit_cast_test_action(cases[c].integer, &source);
        action.source_bytes = cases[c].snan;
        action.source_byte_count = cases[c].bytes;
        action.source_proof = CHECK_BUILTIN_BIT_CAST_PROOF_CONSTANT;
        CheckBuiltinBitCastResult result = check_builtin_bit_cast_test_apply(table, action);
        unsigned __int128 bits = 0;
        for (uint32_t i = 0; i < cases[c].bytes; i += 1)
        {
            bits |= (unsigned __int128)cases[c].snan[i] << (i * 8);
        }
        check_builtin_bit_cast_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.unsupported == 0 &&
            result.folded && !result.runtime_required && result.action.value.constant_kind == CHECK_CONSTANT_INTEGER &&
            result.action.value.integer == bits && result.action.value.flags == 0 && check_builtin_bit_cast_test_bytes(&result, cases[c].snan, cases[c].bytes),
            "HALF/FLOAT16/F32/F64 signaling NaN certificate folds to exact integer bits without ICE");
        check_builtin_bit_cast_test_expect(test, memcmp((uint8_t*)&source + offsetof(CheckValue, floating), x87_snan, sizeof(x87_snan)) == 0,
            "raw signaling NaN operand sentinel is neither loaded nor quieted");
        action.source_proof = CHECK_BUILTIN_BIT_CAST_PROOF_ICE;
        result = check_builtin_bit_cast_test_apply(table, action);
        check_builtin_bit_cast_test_expect(test, result.action.diagnostic == CHECK_DIAG_INVALID_TYPE, "FP source cannot claim integer source ICE provenance");
        action.source_proof = CHECK_BUILTIN_BIT_CAST_PROOF_NONE;
        action.source_bytes = NULL;
        action.source_byte_count = 0;
        result = check_builtin_bit_cast_test_apply(table, action);
        check_builtin_bit_cast_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && !result.representation_known &&
            result.unsupported == BUSTER_CHECK_BUILTIN_BIT_CAST_REPRESENTATION, "floating field is never used to invent object bytes");
        source.type = cases[c].integer;
        source.constant_kind = CHECK_CONSTANT_INTEGER;
        source.integer = bits;
        action = check_builtin_bit_cast_test_action(cases[c].floating, &source);
        action.source_proof = CHECK_BUILTIN_BIT_CAST_PROOF_CONSTANT;
        result = check_builtin_bit_cast_test_apply(table, action);
        check_builtin_bit_cast_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.folded && result.representation_known &&
            result.unsupported == BUSTER_CHECK_BUILTIN_BIT_CAST_CHECK_VALUE_PAYLOAD && result.action.value.constant_kind == CHECK_CONSTANT_NONE &&
            check_builtin_bit_cast_test_bytes(&result, cases[c].snan, cases[c].bytes), "floating results retain raw sNaN payload with explicit CheckValue transport gap");
    }
}

static void check_builtin_bit_cast_test_effects(CheckBuiltinBitCastTest* test, TypeTable* table)
{
    CheckValue source = check_test_integer(BUSTER_TYPE_INT, UINT32_C(0x80000000));
    CheckBuiltinBitCastAction action = check_builtin_bit_cast_test_action(BUSTER_TYPE_UINT, &source);
    action.source_proof = CHECK_BUILTIN_BIT_CAST_PROOF_ICE;
    for (uint32_t fault = CHECK_FAULT_NONE; fault <= CHECK_FAULT_UNSUPPORTED_CONSTANT; fault += 1)
    {
        for (uint32_t effects = 0; effects < 2; effects += 1)
        {
            source.flags = BUSTER_CHECK_ICE | (effects ? BUSTER_CHECK_SIDE_EFFECT : 0);
            source.fault = (CheckFault)fault;
            source.fault_node = 936;
            const CheckBuiltinBitCastResult result = check_builtin_bit_cast_test_apply(table, action);
            const bool clean = fault == CHECK_FAULT_NONE && effects == 0;
            check_builtin_bit_cast_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.evaluated_operands == 1 &&
                result.representation_known && result.folded == clean && result.runtime_required != clean &&
                result.action.value.fault == (CheckFault)fault && result.action.value.fault_node == 936 &&
                result.action.value.flags == (clean ? BUSTER_CHECK_ICE : effects ? BUSTER_CHECK_SIDE_EFFECT : 0),
                "constant proof preserves actual operand evaluation, effects and deferred fault identity");
        }
    }
    source = check_test_integer(type_qualify(table, BUSTER_TYPE_INT, BUSTER_TYPE_CONST | BUSTER_TYPE_VOLATILE), 17);
    for (uint32_t category = CHECK_RVALUE; category <= CHECK_LVALUE; category += 1)
    {
        source.category = (CheckCategory)category;
        const CheckBuiltinBitCastResult result = check_builtin_bit_cast_test_apply(table, action);
        check_builtin_bit_cast_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && !result.folded && result.runtime_required &&
            result.volatile_read == (category == CHECK_LVALUE) && result.action.value.flags == (category == CHECK_LVALUE ? BUSTER_CHECK_SIDE_EFFECT : 0) &&
            result.source_type == source.type && check_builtin_bit_cast_test_no_conversions(&result), "volatile eligibility and actual lvalue read are separate");
    }
    source = check_test_integer(BUSTER_TYPE_INT, 17);
    action.destination_type = type_qualify(table, BUSTER_TYPE_UINT, BUSTER_TYPE_CONST | BUSTER_TYPE_VOLATILE);
    CheckBuiltinBitCastResult result = check_builtin_bit_cast_test_apply(table, action);
    check_builtin_bit_cast_test_expect(test, result.folded && result.action.value.type == BUSTER_TYPE_UINT && result.representation_type == BUSTER_TYPE_UINT &&
        !result.volatile_read, "top-level destination qualifiers do not create a volatile operand read");
    action.destination_type = type_aligned(table, BUSTER_TYPE_UINT, 4);
    result = check_builtin_bit_cast_test_apply(table, action);
    check_builtin_bit_cast_test_expect(test, action.destination_type != 0 && result.action.diagnostic == CHECK_DIAG_NONE && result.folded &&
        result.action.value.type == action.destination_type && result.representation_type == BUSTER_TYPE_UINT, "aligned destination result retains its type wrapper");
    action.destination_type = BUSTER_TYPE_UINT;
    source.flags |= BUSTER_CHECK_BIT_FIELD;
    source.bit_width = 7;
    result = check_builtin_bit_cast_test_apply(table, action);
    check_builtin_bit_cast_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && !result.folded && !result.representation_known &&
        result.unsupported == BUSTER_CHECK_BUILTIN_BIT_CAST_REPRESENTATION && result.evaluated_operands == 1,
        "source bit-field remains typed with explicit representation requirement");
}

static void check_builtin_bit_cast_test_types(CheckBuiltinBitCastTest* test, TypeTable* table)
{
    const TypeId record = type_tag(table, BUSTER_TYPE_KIND_STRUCT, 481);
    const TypeMember member = { .name = 482, .type = BUSTER_TYPE_INT, .bit_width = BUSTER_TYPE_NOT_BIT_FIELD };
    const bool completed = type_complete_record(table, record, &member, 1, (TypeRecordAttributes){ 0 });
    const TypeId enumeration = type_tag(table, BUSTER_TYPE_KIND_ENUM, 483);
    const bool enum_completed = type_enum_complete(table, enumeration, BUSTER_TYPE_UINT, 0);
    const TypeId array = type_array(table, BUSTER_TYPE_UCHAR, 4);
    const TypeId vla = type_variable_array(table, BUSTER_TYPE_INT, 99);
    const TypeId atomic = type_atomic(table, BUSTER_TYPE_INT);
    const TypeId pointer = type_pointer(table, BUSTER_TYPE_INT);
    const TypeId vector = type_vector(table, BUSTER_TYPE_UINT, 1, false);
    check_builtin_bit_cast_test_expect(test, completed && enum_completed && array != 0 && vla != 0 && atomic != 0 && pointer != 0 && vector != 0,
        "record, enum, array, variable array, atomic, pointer and vector fixtures constructed");
    const TypeId sources[] = { record, array, pointer, vector, BUSTER_TYPE_LONG_DOUBLE, type_bit_int(table, 17, true), type_bit_int(table, 129, true) };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(sources); i += 1)
    {
        CheckValue source = { .type = sources[i], .category = CHECK_LVALUE };
        CheckBuiltinBitCastAction action = check_builtin_bit_cast_test_action(sources[i], &source);
        const CheckBuiltinBitCastResult result = check_builtin_bit_cast_test_apply(table, action);
        check_builtin_bit_cast_test_expect(test, sources[i] != 0 && result.action.diagnostic == CHECK_DIAG_NONE && result.source_type == sources[i] &&
            result.action.value.type == sources[i] && result.action.value.category == CHECK_RVALUE && result.evaluated_operands == 1 &&
            result.unsupported == BUSTER_CHECK_BUILTIN_BIT_CAST_REPRESENTATION && result.runtime_required && check_builtin_bit_cast_test_no_conversions(&result),
            "valid unmodeled representations stay typed without decay, loads or invented payloads");
    }
    const uint32_t widths[] = { 1, 17, 129 };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(widths); i += 1)
    {
        CheckValue source = check_test_integer(type_bit_int(table, widths[i], true), 1);
        CheckBuiltinBitCastAction action = check_builtin_bit_cast_test_action(source.type, &source);
        action.source_proof = CHECK_BUILTIN_BIT_CAST_PROOF_ICE;
        const CheckBuiltinBitCastResult result = check_builtin_bit_cast_test_apply(table, action);
        check_builtin_bit_cast_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.unsupported == BUSTER_CHECK_BUILTIN_BIT_CAST_REPRESENTATION &&
            !result.folded && result.runtime_required, "subbyte, padded and wide integer ICE is valid source metadata with explicit payload gap");
    }
    CheckValue source = { .type = array, .category = CHECK_LVALUE };
    CheckBuiltinBitCastAction action = check_builtin_bit_cast_test_action(BUSTER_TYPE_UINT, &source);
    CheckBuiltinBitCastResult result = check_builtin_bit_cast_test_apply(table, action);
    check_builtin_bit_cast_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.source_type == array && result.byte_count == 4 &&
        check_builtin_bit_cast_test_no_conversions(&result), "array operand uses complete array storage rather than pointer decay");
    source.type = BUSTER_TYPE_INT;
    action.destination_type = array;
    result = check_builtin_bit_cast_test_apply(table, action);
    check_builtin_bit_cast_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == array &&
        result.action.value.category == CHECK_RVALUE, "array destination has its raw array result type");
    const TypeId qualified_array = type_array(table, type_qualify(table, BUSTER_TYPE_UCHAR, BUSTER_TYPE_CONST), 4);
    const TypeId qualified_typedef_array = type_qualify(table, array, BUSTER_TYPE_CONST);
    action.destination_type = qualified_array;
    result = check_builtin_bit_cast_test_apply(table, action);
    check_builtin_bit_cast_test_expect(test, qualified_array == qualified_typedef_array && result.action.diagnostic == CHECK_DIAG_NONE &&
        result.action.value.type == 0 && result.representation_type == qualified_array && result.runtime_required && !result.folded &&
        result.unsupported == (BUSTER_CHECK_BUILTIN_BIT_CAST_REPRESENTATION | BUSTER_CHECK_BUILTIN_BIT_CAST_RESULT_TYPE),
        "qualified array spelling lost by canonical TypeId remains an explicit result-type gap");
    action.destination_type = type_qualify(table, record, BUSTER_TYPE_CONST);
    result = check_builtin_bit_cast_test_apply(table, action);
    check_builtin_bit_cast_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == record &&
        result.representation_type == action.destination_type && result.unsupported == BUSTER_CHECK_BUILTIN_BIT_CAST_REPRESENTATION,
        "qualified record constraint destination and unqualified expression result remain separate");
    source.type = vla;
    action.destination_type = BUSTER_TYPE_INT;
    result = check_builtin_bit_cast_test_apply(table, action);
    check_builtin_bit_cast_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPATIBLE,
        "variable source static size zero differs from a nonzero fixed destination");
    source.type = BUSTER_TYPE_INT;
    action.destination_type = vla;
    result = check_builtin_bit_cast_test_apply(table, action);
    check_builtin_bit_cast_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPATIBLE,
        "nonzero fixed source differs from variable destination static size zero");
    source.type = vla;
    result = check_builtin_bit_cast_test_apply(table, action);
    check_builtin_bit_cast_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.byte_count == 0 &&
        result.unsupported == BUSTER_CHECK_BUILTIN_BIT_CAST_SIZE && result.runtime_required, "equal static-zero variable storage retains an explicit bounded size gap");
    source.type = atomic;
    action.destination_type = BUSTER_TYPE_INT;
    result = check_builtin_bit_cast_test_apply(table, action);
    check_builtin_bit_cast_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPATIBLE && result.action.diagnostic_operand == 0,
        "atomic source fails trivially-copyable constraint without an implicit atomic load");
    source.type = BUSTER_TYPE_INT;
    action.destination_type = atomic;
    result = check_builtin_bit_cast_test_apply(table, action);
    check_builtin_bit_cast_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == atomic &&
        result.representation_type == BUSTER_TYPE_INT && result.unsupported == BUSTER_CHECK_BUILTIN_BIT_CAST_REPRESENTATION,
        "atomic destination constraint strips atomic but preserves its result type and transport gap");
    const TypeId incomplete = type_tag(table, BUSTER_TYPE_KIND_STRUCT, 484);
    const TypeId invalid_sources[] = { incomplete, type_incomplete_array(table, BUSTER_TYPE_INT), BUSTER_TYPE_VOID, type_function(table, BUSTER_TYPE_INT, NULL, 0, 0) };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(invalid_sources); i += 1)
    {
        source.type = invalid_sources[i];
        action.destination_type = BUSTER_TYPE_INT;
        result = check_builtin_bit_cast_test_apply(table, action);
        check_builtin_bit_cast_test_expect(test, result.action.diagnostic != CHECK_DIAG_NONE && result.evaluated_operands == 0,
            "incomplete, void and function sources are rejected without operand conversion");
    }
    source = check_test_integer(BUSTER_TYPE_UINT, 7);
    action = check_builtin_bit_cast_test_action(BUSTER_TYPE_ULONG, &source);
    result = check_builtin_bit_cast_test_apply(table, action);
    check_builtin_bit_cast_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPATIBLE, "unequal static storage sizes are rejected");
    source.type = enumeration;
    action.destination_type = BUSTER_TYPE_UINT;
    action.source_proof = CHECK_BUILTIN_BIT_CAST_PROOF_ICE;
    uint32_t history[512] = { 0 };
    const uint32_t tag = table->types.index[type_index(enumeration)];
    history[tag] = 20;
    action.tag_completions = history;
    action.tag_completion_count = BUSTER_ARRAY_LENGTH(history);
    for (uint32_t position = 19; position <= 21; position += 1)
    {
        action.source_position = position;
        result = check_builtin_bit_cast_test_apply(table, action);
        check_builtin_bit_cast_test_expect(test, result.action.diagnostic == (position < 20 ? CHECK_DIAG_INCOMPLETE : CHECK_DIAG_NONE) &&
            result.folded == (position >= 20), "enum completeness uses the expression source position rather than final table completion");
    }
    action.tag_completion_count = tag;
    result = check_builtin_bit_cast_test_apply(table, action);
    check_builtin_bit_cast_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPLETE, "missing history entry is incomplete without reading beyond history");
    source.type = type_array(table, record, 1);
    action.destination_type = BUSTER_TYPE_UINT;
    action.source_proof = CHECK_BUILTIN_BIT_CAST_PROOF_NONE;
    history[table->types.index[type_index(record)]] = 30;
    action.tag_completion_count = BUSTER_ARRAY_LENGTH(history);
    action.source_position = 29;
    result = check_builtin_bit_cast_test_apply(table, action);
    check_builtin_bit_cast_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPLETE, "array element tag uses source completion history");
}

static void check_builtin_bit_cast_test_complex(CheckBuiltinBitCastTest* test, TypeTable* table)
{
    CheckValue source = { .type = type_complex(table, BUSTER_TYPE_FLOAT), .category = CHECK_RVALUE, .constant_kind = CHECK_CONSTANT_COMPLEX };
    const uint8_t poison[sizeof(long double)] = { 1, 0, 0, 0, 0, 0, 0, 0x80, 0xff, 0x7f };
    memcpy((uint8_t*)&source + offsetof(CheckValue, floating), poison, sizeof(poison));
    memcpy((uint8_t*)&source + offsetof(CheckValue, imaginary), poison, sizeof(poison));
    uint8_t saved[sizeof(source)];
    memcpy(saved, &source, sizeof(saved));
    CheckBuiltinBitCastAction action = check_builtin_bit_cast_test_action(BUSTER_TYPE_ULONG, &source);
    action.source_proof = CHECK_BUILTIN_BIT_CAST_PROOF_CONSTANT;
    const CheckBuiltinBitCastResult result = check_builtin_bit_cast_test_apply(table, action);
    check_builtin_bit_cast_test_expect(test, source.type != 0 && result.action.diagnostic == CHECK_DIAG_NONE &&
        result.source_type == source.type && result.action.value.type == BUSTER_TYPE_ULONG && result.byte_count == 8 &&
        result.evaluated_operands == 1 && result.unsupported == BUSTER_CHECK_BUILTIN_BIT_CAST_REPRESENTATION &&
        result.runtime_required && !result.folded && !result.representation_known && result.payload_count == 0,
        "known complex operands remain semantically valid with an explicit representation gap");
    check_builtin_bit_cast_test_expect(test, memcmp(saved, &source, sizeof(saved)) == 0 &&
        result.action.value.constant_kind == CHECK_CONSTANT_NONE && check_builtin_bit_cast_test_no_conversions(&result),
        "complex compatibility never reads or changes either raw floating payload");
}

static void check_builtin_bit_cast_test_invalid(CheckBuiltinBitCastTest* test, TypeTable* table)
{
    CheckValue source = check_test_integer(BUSTER_TYPE_UINT, 7);
    CheckBuiltinBitCastAction action = check_builtin_bit_cast_test_action(BUSTER_TYPE_INT, &source);
    CheckBuiltinBitCastResult result;
    action.arguments = NULL;
    result = check_builtin_bit_cast_test_apply(table, action);
    check_builtin_bit_cast_test_expect(test, result.action.diagnostic == CHECK_DIAG_INVALID_TYPE, "positive argument count requires a readable value");
    action.arguments = &source;
    action.destination_type = UINT32_MAX;
    result = check_builtin_bit_cast_test_apply(table, action);
    check_builtin_bit_cast_test_expect(test, result.action.diagnostic == CHECK_DIAG_INVALID_TYPE, "invalid destination type is diagnosed");
    action.destination_type = BUSTER_TYPE_INT;
    source.type = UINT32_MAX;
    result = check_builtin_bit_cast_test_apply(table, action);
    check_builtin_bit_cast_test_expect(test, result.action.diagnostic == CHECK_DIAG_INVALID_TYPE, "invalid operand type is diagnosed");
    source.type = BUSTER_TYPE_UINT;
    for (uint32_t field = 0; field < 4; field += 1)
    {
        for (uint32_t sign = 0; sign < 2; sign += 1)
        {
            const uint32_t invalid = sign ? UINT32_MAX : 999;
            action.source_proof = field == 0 ? (CheckBuiltinBitCastProof)invalid : CHECK_BUILTIN_BIT_CAST_PROOF_NONE;
            source.category = field == 1 ? (CheckCategory)invalid : CHECK_RVALUE;
            source.constant_kind = field == 2 ? (CheckConstantKind)invalid : CHECK_CONSTANT_INTEGER;
            source.fault = field == 3 ? (CheckFault)invalid : CHECK_FAULT_NONE;
            result = check_builtin_bit_cast_test_apply(table, action);
            check_builtin_bit_cast_test_expect(test, result.action.diagnostic == CHECK_DIAG_INVALID_TYPE, "negative and out-of-range enum metadata is rejected");
        }
    }
    source.fault = CHECK_FAULT_NONE;
    action.source_byte_count = 1;
    result = check_builtin_bit_cast_test_apply(table, action);
    check_builtin_bit_cast_test_expect(test, result.action.diagnostic == CHECK_DIAG_INVALID_TYPE, "nonempty byte slice requires a pointer");
    const uint8_t bytes[] = { 1, 2, 3, 4, 5 };
    action.source_bytes = bytes;
    const uint32_t counts[] = { 0, 3, 5 };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(counts); i += 1)
    {
        action.source_byte_count = counts[i];
        result = check_builtin_bit_cast_test_apply(table, action);
        check_builtin_bit_cast_test_expect(test, result.action.diagnostic == CHECK_DIAG_INVALID_TYPE, "byte certificate length exactly matches static source storage");
    }
    action.source_bytes = NULL;
    action.source_byte_count = 0;
    action.tag_completion_count = 1;
    result = check_builtin_bit_cast_test_apply(table, action);
    check_builtin_bit_cast_test_expect(test, result.action.diagnostic == CHECK_DIAG_INVALID_TYPE, "nonempty completion history requires a pointer");
}

static void check_builtin_bit_cast_test_environment(CheckBuiltinBitCastTest* test, TypeTable* table)
{
    CheckValue source = { .type = BUSTER_TYPE_FLOAT, .category = CHECK_RVALUE, .constant_kind = CHECK_CONSTANT_FLOATING };
    const uint8_t poison[sizeof(long double)] = { 1, 0, 0, 0, 0, 0, 0, 0x80, 0xff, 0x7f };
    const uint8_t bytes[] = { 1, 0, 0x80, 0x7f };
    memcpy((uint8_t*)&source + offsetof(CheckValue, floating), poison, sizeof(poison));
    CheckBuiltinBitCastAction action = check_builtin_bit_cast_test_action(BUSTER_TYPE_UINT, &source);
    action.source_bytes = bytes;
    action.source_byte_count = sizeof(bytes);
    action.source_proof = CHECK_BUILTIN_BIT_CAST_PROOF_CONSTANT;
    uint16_t original_control = 0;
    uint16_t original_status = 0;
    uint32_t original_mxcsr = 0;
    __asm__ volatile("fnstcw %0\n\tfnstsw %1\n\tstmxcsr %2" : "=m"(original_control), "=m"(original_status), "=m"(original_mxcsr) : : "memory");
    // A child can clear pre-existing exception flags without disturbing the test driver's FP environment.
    const pid_t child = fork();
    if (child == 0)
    {
        CheckBuiltinBitCastResult result;
        uint16_t before_status = 0;
        uint16_t after_status = 0;
        uint16_t after_control = 0;
        uint32_t after_mxcsr = 0;
        const uint16_t control = (uint16_t)((original_control | 0x003f) ^ 0x0400);
        const uint32_t mxcsr = ((original_mxcsr | 0x1f80u) & ~0x603fu) | 0x4000u;
        __asm__ volatile("fldcw %1\n\tfnclex\n\tfnstsw %0\n\tldmxcsr %2" : "=m"(before_status) : "m"(control), "m"(mxcsr) : "memory");
        check_builtin_bit_cast_one(table, &action, &result);
        __asm__ volatile("fnstcw %0\n\tfnstsw %1\n\tstmxcsr %2" : "=m"(after_control), "=m"(after_status), "=m"(after_mxcsr) : : "memory");
        const bool preserved = after_control == control && after_status == before_status && after_mxcsr == mxcsr &&
            result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.integer == UINT32_C(0x7f800001);
        _exit(preserved ? 0 : 1);
    }
    int status = 0;
    pid_t waited = -1;
    if (child > 0)
    {
        do
        {
            waited = waitpid(child, &status, 0);
        } while (waited < 0 && errno == EINTR);
    }
    uint16_t final_control = 0;
    uint16_t final_status = 0;
    uint32_t final_mxcsr = 0;
    __asm__ volatile("fnstcw %0\n\tfnstsw %1\n\tstmxcsr %2" : "=m"(final_control), "=m"(final_status), "=m"(final_mxcsr) : : "memory");
    check_builtin_bit_cast_test_expect(test, child > 0 && waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
        "metadata-only sNaN bit-cast preserves x87 and MXCSR controls and exception flags");
    check_builtin_bit_cast_test_expect(test, final_control == original_control && final_status == original_status && final_mxcsr == original_mxcsr,
        "isolated FP metadata fixture preserves the parent FP environment");
}

static void check_builtin_bit_cast_test_pages(CheckBuiltinBitCastTest* test, TypeTable* table)
{
    const TypeId enumeration = type_tag(table, BUSTER_TYPE_KIND_ENUM, 485);
    const bool completed = enumeration != 0 && type_enum_complete(table, enumeration, BUSTER_TYPE_UINT, 0);
    const TypeId qualified_array = type_array(table, type_qualify(table, BUSTER_TYPE_UCHAR, BUSTER_TYPE_CONST), 4);
    const uint32_t tag = completed ? table->types.index[type_index(enumeration)] : 0;
    const uint32_t history_count = tag + 1;
    const long page_value = sysconf(_SC_PAGESIZE);
    const size_t page = page_value > 0 ? (size_t)page_value : 0;
    const size_t logical[] =
    {
        BUSTER_CHECK_BIT_CAST_TEST_BATCH * sizeof(CheckBuiltinBitCastAction),
        BUSTER_CHECK_BIT_CAST_TEST_BATCH * sizeof(CheckBuiltinBitCastResult),
        BUSTER_CHECK_BIT_CAST_TEST_BATCH * sizeof(CheckValue),
        4 * BUSTER_CHECK_BIT_CAST_TEST_BATCH,
        history_count * sizeof(uint32_t),
    };
    uint8_t* mappings[5] = { MAP_FAILED, MAP_FAILED, MAP_FAILED, MAP_FAILED, MAP_FAILED };
    uint8_t* ends[5] = { 0 };
    size_t capacities[5] = { 0 };
    bool ready = page != 0 && completed && qualified_array != 0 && history_count <= 512;
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(mappings); i += 1)
    {
        capacities[i] = ((logical[i] + 16 + page - 1) / page) * page;
        mappings[i] = mmap(NULL, capacities[i] + page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        ready = mappings[i] != MAP_FAILED;
        if (ready)
        {
            ends[i] = mappings[i] + capacities[i];
            ready = mprotect(ends[i], page, PROT_NONE) == 0;
        }
    }
    check_builtin_bit_cast_test_expect(test, ready, "exact-tail action, result, argument, payload and history pages allocated");
    uint32_t* history = ready ? (uint32_t*)(ends[4] - history_count * sizeof(*history)) : NULL;
    uint32_t saved_history[512] = { 0 };
    if (ready)
    {
        memset((uint8_t*)history - 16, 0xa5, 16);
        memset(history, 0, history_count * sizeof(*history));
        history[tag] = 20;
        memcpy(saved_history, history, history_count * sizeof(*history));
    }
    for (uint32_t count = 0; ready && count <= BUSTER_CHECK_BIT_CAST_TEST_BATCH; count += 1)
    {
        CheckBuiltinBitCastAction* actions = (CheckBuiltinBitCastAction*)(ends[0] - count * sizeof(*actions));
        CheckBuiltinBitCastResult* results = (CheckBuiltinBitCastResult*)(ends[1] - count * sizeof(*results));
        CheckValue* arguments = (CheckValue*)(ends[2] - count * sizeof(*arguments));
        uint8_t* payload = ends[3] - count * 4;
        memset((uint8_t*)actions - 16, 0xa5, 16 + count * sizeof(*actions));
        memset((uint8_t*)results - 16, 0xa5, 16 + count * sizeof(*results));
        memset((uint8_t*)arguments - 16, 0xa5, 16 + count * sizeof(*arguments));
        memset(payload - 16, 0xa5, 16 + count * 4);
        CheckBuiltinBitCastResult expected[BUSTER_CHECK_BIT_CAST_TEST_BATCH];
        CheckBuiltinBitCastAction saved_actions[BUSTER_CHECK_BIT_CAST_TEST_BATCH];
        CheckValue saved_arguments[BUSTER_CHECK_BIT_CAST_TEST_BATCH];
        uint8_t saved_payload[BUSTER_CHECK_BIT_CAST_TEST_BATCH * 4];
        for (uint32_t i = 0; i < count; i += 1)
        {
            arguments[i] = check_test_integer(i % 5 == 0 ? enumeration : BUSTER_TYPE_UINT, i * 977u);
            for (uint32_t b = 0; b < 4; b += 1)
            {
                payload[i * 4 + b] = (uint8_t)(i + 37 * b);
            }
            actions[i] = check_builtin_bit_cast_test_action(i % 3 == 0 ? BUSTER_TYPE_FLOAT : BUSTER_TYPE_INT, arguments + i);
            actions[i].source_proof = i % 3 == 0 ? CHECK_BUILTIN_BIT_CAST_PROOF_CONSTANT : (CheckBuiltinBitCastProof)(i % 3);
            if (i % 11 == 0)
            {
                actions[i].destination_type = qualified_array;
                actions[i].source_proof = CHECK_BUILTIN_BIT_CAST_PROOF_CONSTANT;
            }
            actions[i].source_bytes = payload + i * 4;
            actions[i].source_byte_count = 4;
            actions[i].tag_completions = history;
            actions[i].tag_completion_count = history_count;
            actions[i].source_position = i % 4 == 0 ? 19 : 20;
            arguments[i].flags |= i % 7 == 0 ? BUSTER_CHECK_SIDE_EFFECT : 0;
            check_builtin_bit_cast_one(table, actions + i, expected + i);
            check_builtin_bit_cast_test_expect(test, expected[i].action.diagnostic == (i % 5 == 0 && i % 4 == 0 ? CHECK_DIAG_INCOMPLETE : CHECK_DIAG_NONE),
                "guarded history entry applies at its exact source position");
            if (i % 11 == 0 && expected[i].action.diagnostic == CHECK_DIAG_NONE)
            {
                check_builtin_bit_cast_test_expect(test, expected[i].action.value.type == 0 && expected[i].representation_type == qualified_array &&
                    expected[i].unsupported == (BUSTER_CHECK_BUILTIN_BIT_CAST_REPRESENTATION | BUSTER_CHECK_BUILTIN_BIT_CAST_RESULT_TYPE),
                    "qualified array result-type gap participates in guarded batch and partition equality");
            }
        }
        memcpy(saved_actions, actions, count * sizeof(*actions));
        memcpy(saved_arguments, arguments, count * sizeof(*arguments));
        memcpy(saved_payload, payload, count * 4);
        check_builtin_bit_cast(table, actions, results, count);
        check_builtin_bit_cast_test_expect(test, memcmp(expected, results, count * sizeof(*results)) == 0, "guarded batch equals independent individual calls including all output bytes");
        const uint32_t partitions[] = { 1, 3, 8, 16, 31 };
        for (uint32_t p = 0; p < BUSTER_ARRAY_LENGTH(partitions); p += 1)
        {
            for (uint32_t first = 0; first < count; first += partitions[p])
            {
                const uint32_t remaining = count - first < partitions[p] ? count - first : partitions[p];
                check_builtin_bit_cast(table, actions + first, results + first, remaining);
            }
            check_builtin_bit_cast_test_expect(test, memcmp(expected, results, count * sizeof(*results)) == 0, "legal batch partitions preserve every output field and representation byte");
        }
        check_builtin_bit_cast_test_expect(test, memcmp(saved_actions, actions, count * sizeof(*actions)) == 0 &&
            memcmp(saved_arguments, arguments, count * sizeof(*arguments)) == 0 && memcmp(saved_payload, payload, count * 4) == 0 &&
            memcmp(saved_history, history, history_count * sizeof(*history)) == 0,
            "all borrowed input slices remain unchanged");
        for (uint32_t i = 1; i <= 16; i += 1)
        {
            check_builtin_bit_cast_test_expect(test, *((uint8_t*)actions - i) == 0xa5 && *((uint8_t*)results - i) == 0xa5 &&
                *((uint8_t*)arguments - i) == 0xa5 && *(payload - i) == 0xa5 && *((uint8_t*)history - i) == 0xa5,
                "batch preserves all bytes before its logical slices");
        }
    }
    if (ready)
    {
        static const struct
        {
            uint32_t bytes;
            TypeId source;
            TypeId destination;
        } widths[] =
        {
            { 1, BUSTER_TYPE_UCHAR, BUSTER_TYPE_SCHAR },
            { 2, BUSTER_TYPE_USHORT, BUSTER_TYPE_SHORT },
            { 8, BUSTER_TYPE_ULONG, BUSTER_TYPE_LONG },
            { 16, BUSTER_TYPE_UINT128, BUSTER_TYPE_INT128 },
        };
        for (uint32_t w = 0; w < BUSTER_ARRAY_LENGTH(widths); w += 1)
        {
            CheckBuiltinBitCastAction* guarded_action = (CheckBuiltinBitCastAction*)(ends[0] - sizeof(*guarded_action));
            CheckBuiltinBitCastResult* guarded_result = (CheckBuiltinBitCastResult*)(ends[1] - sizeof(*guarded_result));
            CheckValue* guarded_argument = (CheckValue*)(ends[2] - sizeof(*guarded_argument));
            uint8_t* payload = ends[3] - widths[w].bytes;
            memset((uint8_t*)guarded_action - 16, 0xa5, 16);
            memset((uint8_t*)guarded_result - 16, 0xa5, 16);
            memset((uint8_t*)guarded_argument - 16, 0xa5, 16);
            memset(payload - 16, 0xa5, 16);
            unsigned __int128 expected_bits = 0;
            for (uint32_t b = 0; b < widths[w].bytes; b += 1)
            {
                payload[b] = (uint8_t)(0x83u + b * 19u);
                expected_bits |= (unsigned __int128)payload[b] << (b * 8);
            }
            *guarded_argument = check_test_integer(widths[w].source, 0);
            *guarded_action = check_builtin_bit_cast_test_action(widths[w].destination, guarded_argument);
            guarded_action->source_bytes = payload;
            guarded_action->source_byte_count = widths[w].bytes;
            guarded_action->source_proof = CHECK_BUILTIN_BIT_CAST_PROOF_ICE;
            check_builtin_bit_cast_one(table, guarded_action, guarded_result);
            check_builtin_bit_cast_test_expect(test, guarded_result->action.diagnostic == CHECK_DIAG_NONE && guarded_result->unsupported == 0 &&
                guarded_result->folded && guarded_result->action.value.integer == expected_bits &&
                check_builtin_bit_cast_test_bytes(guarded_result, payload, widths[w].bytes), "one, two, eight and sixteen-byte certificates end exactly at inaccessible storage");
            for (uint32_t b = 1; b <= 16; b += 1)
            {
                check_builtin_bit_cast_test_expect(test, *((uint8_t*)guarded_action - b) == 0xa5 && *((uint8_t*)guarded_result - b) == 0xa5 &&
                    *((uint8_t*)guarded_argument - b) == 0xa5 && *(payload - b) == 0xa5, "all widths preserve exact-slice prefix guards");
            }
        }
        CheckBuiltinBitCastAction action = check_builtin_bit_cast_test_action(BUSTER_TYPE_INT, (const CheckValue*)ends[2]);
        const uint32_t invalid_counts[] = { 0, 2, UINT32_MAX };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(invalid_counts); i += 1)
        {
            action.argument_count = invalid_counts[i];
            const CheckBuiltinBitCastResult result = check_builtin_bit_cast_test_apply(table, action);
            check_builtin_bit_cast_test_expect(test, result.action.diagnostic == CHECK_DIAG_ARGUMENT_COUNT, "arity rejection never reads inaccessible argument storage");
        }
        action.argument_count = 1;
        action.id = CBUILTIN_POPCOUNT;
        CheckBuiltinBitCastResult result = check_builtin_bit_cast_test_apply(table, action);
        check_builtin_bit_cast_test_expect(test, result.action.diagnostic == CHECK_DIAG_UNSUPPORTED, "wrong builtin ID never reads an operand");
        CheckValue source = check_test_integer(BUSTER_TYPE_UINT, 1);
        action = check_builtin_bit_cast_test_action(BUSTER_TYPE_INT, &source);
        action.source_bytes = ends[3];
        action.source_byte_count = 3;
        result = check_builtin_bit_cast_test_apply(table, action);
        check_builtin_bit_cast_test_expect(test, result.action.diagnostic == CHECK_DIAG_INVALID_TYPE, "wrong byte length is rejected before reading certificate storage");
        source.type = enumeration;
        action.source_bytes = NULL;
        action.source_byte_count = 0;
        action.tag_completions = history;
        action.tag_completion_count = tag;
        result = check_builtin_bit_cast_test_apply(table, action);
        check_builtin_bit_cast_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPLETE, "missing guarded history entry is refused without reading its tail");
        check_builtin_bit_cast(NULL, (const CheckBuiltinBitCastAction*)ends[0], (CheckBuiltinBitCastResult*)ends[1], 0);
    }
    check_builtin_bit_cast(NULL, NULL, NULL, 0);
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(mappings); i += 1)
    {
        if (mappings[i] != MAP_FAILED)
        {
            munmap(mappings[i], capacities[i] + page);
        }
    }
}

static void check_builtin_bit_cast_test_clang(CheckBuiltinBitCastTest* test, const char* clang)
{
    static const struct
    {
        const char* source;
        bool rejected;
    } cases[] =
    {
        { "_Static_assert(__clang_major__==23 && __clang_minor__==1 && __clang_patchlevel__==1, \"exact oracle\");\n", false },
        { "enum { A=__builtin_bit_cast(unsigned int,-1) };\n_Static_assert(A==0xffffffffu, \"GNU folded integer\");\n_Static_assert(__builtin_bit_cast(int,0x80000000u)==(-2147483647-1), \"signed bits\");\n", false },
        { "int x=__builtin_choose_expr(__builtin_bit_cast(unsigned,1u),1,0);\n", true },
        { "int x=__builtin_choose_expr(__builtin_bit_cast(unsigned,1.0f),1,0);\n", true },
        { "_Static_assert(__builtin_bit_cast(unsigned int,1.0f)==0x3f800000u, \"float object bits\");\n_Static_assert(__builtin_bit_cast(unsigned long,-0.0)==0x8000000000000000ul, \"negative zero\");\n_Static_assert(__builtin_constant_p(__builtin_bit_cast(float,0x3f800000u)), \"constant FP result\");\n", false },
        { "_Static_assert(__builtin_bit_cast(unsigned short,(_Float16)1.0)==0x3c00u, \"float16 bits\");\nunsigned short f(__fp16* p) { return __builtin_bit_cast(unsigned short,*p); }\n", false },
        { "_Static_assert(_Generic(__builtin_bit_cast(const volatile unsigned int,1), unsigned int:1, default:0), \"unqualified result\");\nunsigned f(volatile int* p) { return __builtin_bit_cast(unsigned,*p); }\n", false },
        { "typedef unsigned char A[4]; unsigned f(A* p) { return __builtin_bit_cast(unsigned,*p); }\n_Static_assert(sizeof(__builtin_bit_cast(A,1u))==4, \"array destination\");\n", false },
        { "typedef const unsigned A[1]; _Static_assert(_Generic(__builtin_bit_cast(A,1u), const unsigned*:1, default:0), \"const element array result\");\n", false },
        { "typedef unsigned A[1]; _Static_assert(_Generic(__builtin_bit_cast(const A,1u), unsigned*:1, default:0), \"qualified typedef array result\");\n", false },
        { "struct S { unsigned x; }; unsigned f(struct S s) { return __builtin_bit_cast(unsigned,s); }\nstruct S g(unsigned x) { return __builtin_bit_cast(struct S,x); }\n", false },
        { "unsigned long f(int* p) { return __builtin_bit_cast(unsigned long,p); }\nint* g(unsigned long x) { return __builtin_bit_cast(int*,x); }\n", false },
        { "typedef unsigned V __attribute__((vector_size(4))); unsigned f(V x) { return __builtin_bit_cast(unsigned,x); }\n", false },
        { "unsigned long f(void) { return __builtin_bit_cast(unsigned long, (1.0f+2.0fi)); }\n", false },
        { "_Static_assert(__builtin_bit_cast(unsigned char,(_Bool)1)==1, \"bool source\");\n_Static_assert(__builtin_bit_cast(_Bool,(unsigned char)1), \"bool destination\");\n", false },
        { "_Atomic(int) f(unsigned x) { return __builtin_bit_cast(_Atomic(int),x); }\n", false },
        { "unsigned f(_Atomic(int)* p) { return __builtin_bit_cast(unsigned,*p); }\n", true },
        { "unsigned long f(unsigned x) { return __builtin_bit_cast(unsigned long,x); }\n", true },
        { "struct S; unsigned f(struct S* p) { return __builtin_bit_cast(unsigned,*p); }\n", true },
        { "struct S; void f(unsigned x) { __builtin_bit_cast(struct S,x); }\n", true },
        { "void f(unsigned x) { __builtin_bit_cast(void,x); }\n", true },
        { "int f(void); unsigned long g(void) { return __builtin_bit_cast(unsigned long,f); }\n", true },
        { "typedef int F(void); void g(unsigned long x) { __builtin_bit_cast(F,x); }\n", true },
        { "unsigned f(unsigned x) { return __builtin_bit_cast(unsigned); }\n", true },
        // ParseExpression consumes commas after the type into ONE value expression, not extra action arguments.
        { "unsigned f(unsigned x) { return __builtin_bit_cast(unsigned,x,x); }\n", false },
        { "unsigned f(int n,int (*p)[n]) { return __builtin_bit_cast(unsigned,*p); }\n", true },
        { "void f(int n,int (*p)[n]) { typedef int A[n]; __builtin_bit_cast(A,*p); }\n", false },
        { "unsigned f(struct { unsigned x:7; } s) { return __builtin_bit_cast(unsigned,s.x); }\n", false },
    };
    char directory[] = "/tmp/buster-check-bit-cast-XXXXXX";
    const bool created = mkdtemp(directory) != NULL;
    bool ready = created;
    char paths[BUSTER_ARRAY_LENGTH(cases)][160];
    char* pointers[BUSTER_ARRAY_LENGTH(cases)];
    ParseTestClang outcomes[BUSTER_ARRAY_LENGTH(cases)];
    uint32_t written = 0;
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        pointers[i] = paths[i];
        written += 1;
        ready = parse_test_write(paths[i], cases[i].source, strlen(cases[i].source));
    }
    const char* arguments[] = { "-std=gnu17" };
    ready = ready && parse_test_clang(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), pointers, BUSTER_ARRAY_LENGTH(cases), outcomes);
    check_builtin_bit_cast_test_expect(test, ready, "independent exact Clang GNU17 bit-cast source assertions and constraints ran");
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        if (outcomes[i].rejected != cases[i].rejected)
        {
            fprintf(stderr, "test-check-builtins-bit-cast: fixture %u expected %s, got %s at %u:%u: %s\n", i,
                cases[i].rejected ? "rejection" : "acceptance", outcomes[i].rejected ? "rejection" : "acceptance",
                outcomes[i].line, outcomes[i].column, outcomes[i].message);
        }
        check_builtin_bit_cast_test_expect(test, outcomes[i].rejected == cases[i].rejected, "Clang agrees with original bit-cast source fixture");
    }
    for (uint32_t i = 0; i < written; i += 1)
    {
        unlink(paths[i]);
    }
    if (created)
    {
        rmdir(directory);
    }
}

static int run_check_builtin_bit_cast_tests(int argc, char** argv)
{
    CheckBuiltinBitCastTest test = { 0 };
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
    check_builtin_bit_cast_test_expect(&test, options, "usage: test-check-builtins-bit-cast [--clang PATH]");
    CheckTestTypes types = { 0 };
    if (options)
    {
        const bool initialized = check_test_types_open(&types);
        check_builtin_bit_cast_test_expect(&test, initialized, "type-table allocation");
        if (initialized)
        {
            check_builtin_bit_cast_test_integers(&test, &types.table);
            check_builtin_bit_cast_test_floating(&test, &types.table);
            check_builtin_bit_cast_test_effects(&test, &types.table);
            check_builtin_bit_cast_test_types(&test, &types.table);
            check_builtin_bit_cast_test_complex(&test, &types.table);
            check_builtin_bit_cast_test_invalid(&test, &types.table);
            check_builtin_bit_cast_test_environment(&test, &types.table);
            check_builtin_bit_cast_test_pages(&test, &types.table);
        }
        check_test_types_close(&types);
        check_builtin_bit_cast_test_clang(&test, clang);
    }
    printf("test-check-builtins-bit-cast: %" PRIu64 " checks, %" PRIu64 " failures\n", test.checks, test.failures);
    const int result = test.failures != 0;
    return result;
}
