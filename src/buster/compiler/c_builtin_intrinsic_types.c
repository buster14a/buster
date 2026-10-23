// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original bounded decoder of Clang 23.1.1 builtin type descriptors, for the existing Linux/x86-64 LP64 C type table.
// Include after c_types.c. No generated LLVM data or external implementation is incorporated. Behavioral sources:
// https://github.com/llvm/llvm-project/blob/llvmorg-23.1.1/clang/lib/AST/ASTContext.cpp (DecodeTypeFromStr/GetBuiltinType)
// https://github.com/llvm/llvm-project/blob/llvmorg-23.1.1/clang/lib/Basic/Targets/X86.h (X86_64TargetInfo)
// https://github.com/llvm/llvm-project/blob/llvmorg-23.1.1/clang/include/clang/Basic/BuiltinsBase.td (t/T attributes)
//
// The input slices need no terminator or readable padding. All lengths, nesting and parameter counts are bounded.
// 't' or explicit custom_typechecking wins before decoding the type slice; no custom builtin gains an ordinary
// prototype. 'T' is retained as signature_ignored: successful declaration decoding does not authorize ordinary call
// checking for it. Neither success nor the I mask establishes handwritten constraints, feature eligibility, ICE
// acceptance of a call, folding, evaluation order, ABI lowering or execution support.
// Noreturn/nothrow/calling-convention attributes are not represented in TypeTable's function key; retain the source
// metadata separately for those consumers instead of treating this TypeId as their complete declaration contract.
//
// Scalar, pointer, qualifier, fixed vector, complex and optional canonical SysV va_list array[1]-of-struct identity
// types are materialized through
// existing factories. N/Z are int, W is long, O is long long, z is unsigned long, w is int and Y is long on this target
// in C (not OpenCL). No other target model is claimed. Unsupported references, explicit address spaces (including 0),
// scalable/target/ObjC/header-dependent types and missing va_list context stay explicit, without guessed substitutes.
// Modifiers follow canonical emitter grammar; contradictory/inapplicable prefixes are malformed input rather than
// relying on accidental omissions in Clang's internal assertions. Repeated I is idempotent, as in Clang.
//
// V/E/X children suppress postfix modifiers: V4i* is pointer-to-vector, not vector-of-pointers. The explicit wrapper
// stack replaces input-depth recursion. Parameters undergo array decay and canonical top-level qualifier removal.
// As in GetBuiltinType, v means a zero-argument prototype; v. has no prototype before strict-prototype language modes,
// and is a zero-fixed-argument variadic prototype when strict_prototypes is true (C23). Result types cannot require I.
//
// Outputs are wholly initialized. Non-success publishes no function/result/parameter TypeIds or ICE mask; diagnostic
// status/unsupported/error_offset/type_error survive. UNSUPPORTED can stop at its first unrepresented descriptor;
// it does not certify the unread suffix's validity. TypeTable interning is monotonic: successfully created pointer,
// vector or complex subtypes may remain after a later invalid/unsupported/resource failure, but no partial prototype
// is published. Batches preserve input order and permit any partition; records/slices must not overlap outputs.

#define BUSTER_INTRINSIC_TYPE_MAX_PARAMETERS 64u
#define BUSTER_INTRINSIC_TYPE_MAX_NESTING 64u
#define BUSTER_INTRINSIC_TYPE_MAX_ENCODING 4096u
#define BUSTER_INTRINSIC_TYPE_MAX_ATTRIBUTES 4096u

#define BUSTER_INTRINSIC_TYPE_UNSUPPORTED_CONTEXT 0x01u
#define BUSTER_INTRINSIC_TYPE_UNSUPPORTED_REFERENCE 0x02u
#define BUSTER_INTRINSIC_TYPE_UNSUPPORTED_ADDRESS_SPACE 0x04u
#define BUSTER_INTRINSIC_TYPE_UNSUPPORTED_SCALABLE 0x08u
#define BUSTER_INTRINSIC_TYPE_UNSUPPORTED_TARGET 0x10u
#define BUSTER_INTRINSIC_TYPE_UNSUPPORTED_MODEL 0x20u
#define BUSTER_INTRINSIC_TYPE_UNSUPPORTED_MISSING_TYPE 0x40u

typedef enum BuiltinIntrinsicTypeStatus
{
    BUILTIN_INTRINSIC_TYPES_INVALID,
    BUILTIN_INTRINSIC_TYPES_SUCCESS,
    BUILTIN_INTRINSIC_TYPES_CUSTOM,
    BUILTIN_INTRINSIC_TYPES_UNSUPPORTED,
    BUILTIN_INTRINSIC_TYPES_CAPACITY,
} BuiltinIntrinsicTypeStatus;

typedef struct BuiltinIntrinsicTypeInput BuiltinIntrinsicTypeInput;
struct BuiltinIntrinsicTypeInput
{
    const char* encoding;
    uint32_t encoding_length;
    const char* attributes;
    uint32_t attributes_length;
    TypeId va_list_type;
    bool custom_typechecking;
    bool strict_prototypes;
};

typedef struct BuiltinIntrinsicTypeResult BuiltinIntrinsicTypeResult;
struct BuiltinIntrinsicTypeResult
{
    BuiltinIntrinsicTypeStatus status;
    TypeId function_type;
    TypeId result_type;
    TypeId parameters[BUSTER_INTRINSIC_TYPE_MAX_PARAMETERS];
    uint32_t parameter_count;
    uint64_t ice_parameters;
    uint32_t unsupported;
    uint32_t error_offset;
    TypeError type_error;
    bool variadic;
    bool prototype;
    bool signature_ignored;
};

typedef struct BuiltinIntrinsicTypeReader BuiltinIntrinsicTypeReader;
struct BuiltinIntrinsicTypeReader
{
    TypeTable* table;
    const BuiltinIntrinsicTypeInput* input;
    BuiltinIntrinsicTypeResult* result;
    uint32_t position;
};

typedef struct BuiltinIntrinsicTypePrefix BuiltinIntrinsicTypePrefix;
struct BuiltinIntrinsicTypePrefix
{
    uint8_t sign;
    uint8_t longs;
    uint8_t special;
    bool ice;
};

typedef struct BuiltinIntrinsicTypeWrapper BuiltinIntrinsicTypeWrapper;
struct BuiltinIntrinsicTypeWrapper
{
    uint32_t count;
    uint32_t offset;
    uint8_t code;
};

static void builtin_intrinsic_type_fail(BuiltinIntrinsicTypeReader* reader, BuiltinIntrinsicTypeStatus status, uint32_t unsupported, uint32_t offset)
{
    if (reader->result->status == BUILTIN_INTRINSIC_TYPES_SUCCESS)
    {
        reader->result->status = status;
        reader->result->unsupported = unsupported;
        reader->result->error_offset = offset;
    }
}

static bool builtin_intrinsic_type_decimal(BuiltinIntrinsicTypeReader* reader, uint32_t* value)
{
    const uint32_t start = reader->position;
    uint32_t number = 0;
    while (reader->result->status == BUILTIN_INTRINSIC_TYPES_SUCCESS && reader->position < reader->input->encoding_length &&
           reader->input->encoding[reader->position] >= '0' && reader->input->encoding[reader->position] <= '9')
    {
        const uint32_t digit = (uint32_t)(reader->input->encoding[reader->position] - '0');
        if (number > (UINT32_MAX - digit) / 10u)
        {
            builtin_intrinsic_type_fail(reader, BUILTIN_INTRINSIC_TYPES_INVALID, 0, reader->position);
        }
        else
        {
            number = number * 10u + digit;
            reader->position += 1;
        }
    }
    *value = number;
    const bool present = reader->position != start;
    return present;
}

static BuiltinIntrinsicTypePrefix builtin_intrinsic_type_prefix(BuiltinIntrinsicTypeReader* reader)
{
    BuiltinIntrinsicTypePrefix prefix = {0};
    bool done = false;
    while (!done && reader->result->status == BUILTIN_INTRINSIC_TYPES_SUCCESS && reader->position < reader->input->encoding_length)
    {
        const uint8_t code = (uint8_t)reader->input->encoding[reader->position];
        if (code == 'I')
        {
            prefix.ice = true;
        }
        else if (code == 'S' || code == 'U')
        {
            if (prefix.sign != 0)
            {
                builtin_intrinsic_type_fail(reader, BUILTIN_INTRINSIC_TYPES_INVALID, 0, reader->position);
            }
            prefix.sign = code;
        }
        else if (code == 'L')
        {
            if (prefix.special != 0 || prefix.longs == 3)
            {
                builtin_intrinsic_type_fail(reader, BUILTIN_INTRINSIC_TYPES_INVALID, 0, reader->position);
            }
            prefix.longs += 1;
        }
        else if (code == 'B' || code == 'T' || code == 'N' || code == 'W' || code == 'Z' || code == 'O')
        {
            if (prefix.special != 0 || prefix.longs != 0)
            {
                builtin_intrinsic_type_fail(reader, BUILTIN_INTRINSIC_TYPES_INVALID, 0, reader->position);
            }
            prefix.special = code;
        }
        else
        {
            done = true;
        }
        reader->position += !done;
    }
    return prefix;
}

static TypeId builtin_intrinsic_type_factory(BuiltinIntrinsicTypeReader* reader, TypeId type, uint32_t offset)
{
    if (type == 0)
    {
        reader->result->type_error = reader->table->last_error;
        builtin_intrinsic_type_fail(reader, reader->table->last_error == TYPE_ERROR_FULL ? BUILTIN_INTRINSIC_TYPES_CAPACITY : BUILTIN_INTRINSIC_TYPES_INVALID, 0, offset);
    }
    return type;
}

static TypeId builtin_intrinsic_type_scalar(BuiltinIntrinsicTypeReader* reader, BuiltinIntrinsicTypePrefix prefix, uint8_t code, uint32_t offset)
{
    TypeId type = 0;
    const bool unsigned_type = prefix.sign == 'U';
    const bool plain = prefix.sign == 0 && prefix.longs == 0 && prefix.special == 0;
    bool modifiers = true;
    if (code == 'i')
    {
        uint32_t rank = prefix.longs;
        rank = prefix.special == 'W' ? 1u : (prefix.special == 'O' ? 2u : rank);
        if (prefix.special == 'B')
        {
            type = unsigned_type ? BUSTER_TYPE_UCHAR : BUSTER_TYPE_SCHAR;
        }
        else if (prefix.special == 'T')
        {
            type = unsigned_type ? BUSTER_TYPE_USHORT : BUSTER_TYPE_SHORT;
        }
        else
        {
            const TypeId signed_types[4] = { BUSTER_TYPE_INT, BUSTER_TYPE_LONG, BUSTER_TYPE_LLONG, BUSTER_TYPE_INT128 };
            const TypeId unsigned_types[4] = { BUSTER_TYPE_UINT, BUSTER_TYPE_ULONG, BUSTER_TYPE_ULLONG, BUSTER_TYPE_UINT128 };
            type = unsigned_type ? unsigned_types[rank] : signed_types[rank];
        }
    }
    else if (code == 's' || code == 'c')
    {
        modifiers = prefix.longs == 0 && prefix.special == 0;
        type = code == 's' ? (unsigned_type ? BUSTER_TYPE_USHORT : BUSTER_TYPE_SHORT) :
               (unsigned_type ? BUSTER_TYPE_UCHAR : (prefix.sign == 'S' ? BUSTER_TYPE_SCHAR : BUSTER_TYPE_CHAR));
    }
    else if (code == 'd')
    {
        modifiers = prefix.sign == 0 && prefix.special == 0 && prefix.longs <= 2;
        type = prefix.longs == 0 ? BUSTER_TYPE_DOUBLE : (prefix.longs == 1 ? BUSTER_TYPE_LONG_DOUBLE : BUSTER_TYPE_FLOAT128);
    }
    else if (code == 'v' || code == 'b' || code == 'h' || code == 'x' || code == 'y' || code == 'f' || code == 'z' || code == 'w' || code == 'Y')
    {
        modifiers = plain;
        switch (code)
        {
            case 'v': type = BUSTER_TYPE_VOID; break;
            case 'b': type = BUSTER_TYPE_BOOL; break;
            case 'h': type = BUSTER_TYPE_HALF; break;
            case 'x': type = BUSTER_TYPE_FLOAT16; break;
            case 'y': type = BUSTER_TYPE_BF16; break;
            case 'f': type = BUSTER_TYPE_FLOAT; break;
            case 'z': type = BUSTER_TYPE_ULONG; break;
            case 'w': type = BUSTER_TYPE_INT; break;
            case 'Y': type = BUSTER_TYPE_LONG; break;
        }
    }
    else if (code == 'a' || code == 'A')
    {
        modifiers = plain;
        type = reader->input->va_list_type;
        bool supported_list = type_is_valid(reader->table, type);
        if (supported_list)
        {
            const TypeId canonical = type_strip_aligned(reader->table, type);
            supported_list = type_kind(reader->table, canonical) == BUSTER_TYPE_KIND_ARRAY && reader->table->types.extent[type_index(canonical)] == 1;
            if (supported_list)
            {
                supported_list = type_kind(reader->table, type_element(reader->table, canonical)) == BUSTER_TYPE_KIND_STRUCT;
            }
        }
        if (modifiers && !supported_list)
        {
            builtin_intrinsic_type_fail(reader, BUILTIN_INTRINSIC_TYPES_UNSUPPORTED, BUSTER_INTRINSIC_TYPE_UNSUPPORTED_CONTEXT, offset);
            type = 0;
        }
        else if (modifiers && code == 'A')
        {
            type = type_strip_aligned(reader->table, type);
            if (type_kind_is_array(type_kind(reader->table, type)))
            {
                type = builtin_intrinsic_type_factory(reader, type_pointer(reader->table, type_element(reader->table, type)), offset);
            }
            else
            {
                builtin_intrinsic_type_fail(reader, BUILTIN_INTRINSIC_TYPES_UNSUPPORTED, BUSTER_INTRINSIC_TYPE_UNSUPPORTED_REFERENCE, offset);
                type = 0;
            }
        }
    }
    else if (code == 'F' || code == 'G' || code == 'H' || code == 'M' || code == 'P' || code == 'J' || code == 'K' || code == 'p')
    {
        builtin_intrinsic_type_fail(reader, BUILTIN_INTRINSIC_TYPES_UNSUPPORTED, BUSTER_INTRINSIC_TYPE_UNSUPPORTED_CONTEXT, offset);
    }
    else if (code == 'Q' || code == 'm')
    {
        builtin_intrinsic_type_fail(reader, BUILTIN_INTRINSIC_TYPES_UNSUPPORTED, BUSTER_INTRINSIC_TYPE_UNSUPPORTED_TARGET, offset);
    }
    else
    {
        builtin_intrinsic_type_fail(reader, BUILTIN_INTRINSIC_TYPES_INVALID, 0, offset);
    }
    if (!modifiers)
    {
        builtin_intrinsic_type_fail(reader, BUILTIN_INTRINSIC_TYPES_INVALID, 0, offset);
        type = 0;
    }
    return type;
}

static TypeId builtin_intrinsic_type_one(BuiltinIntrinsicTypeReader* reader, bool* ice)
{
    BuiltinIntrinsicTypeWrapper wrappers[BUSTER_INTRINSIC_TYPE_MAX_NESTING];
    uint32_t depth = 0;
    TypeId type = 0;
    bool terminal = false;
    while (!terminal && reader->result->status == BUILTIN_INTRINSIC_TYPES_SUCCESS)
    {
        const uint32_t offset = reader->position;
        const BuiltinIntrinsicTypePrefix prefix = builtin_intrinsic_type_prefix(reader);
        if (reader->position == reader->input->encoding_length)
        {
            builtin_intrinsic_type_fail(reader, BUILTIN_INTRINSIC_TYPES_INVALID, 0, reader->position);
        }
        if (reader->result->status == BUILTIN_INTRINSIC_TYPES_SUCCESS)
        {
            const uint8_t code = (uint8_t)reader->input->encoding[reader->position++];
            if (code == 'V' || code == 'E' || code == 'X' || code == 'q')
            {
                if (prefix.sign != 0 || prefix.special != 0 || prefix.longs != 0 || prefix.ice)
                {
                    builtin_intrinsic_type_fail(reader, BUILTIN_INTRINSIC_TYPES_INVALID, 0, offset);
                }
                uint32_t count = 0;
                if (code != 'X' && (!builtin_intrinsic_type_decimal(reader, &count) || count == 0))
                {
                    builtin_intrinsic_type_fail(reader, BUILTIN_INTRINSIC_TYPES_INVALID, 0, reader->position);
                }
                if (code == 'q')
                {
                    builtin_intrinsic_type_fail(reader, BUILTIN_INTRINSIC_TYPES_UNSUPPORTED, BUSTER_INTRINSIC_TYPE_UNSUPPORTED_SCALABLE, offset);
                }
                else if (depth == BUSTER_INTRINSIC_TYPE_MAX_NESTING)
                {
                    builtin_intrinsic_type_fail(reader, BUILTIN_INTRINSIC_TYPES_CAPACITY, 0, offset);
                }
                else if (reader->result->status == BUILTIN_INTRINSIC_TYPES_SUCCESS)
                {
                    wrappers[depth++] = (BuiltinIntrinsicTypeWrapper){ .count = count, .offset = offset, .code = code };
                }
            }
            else
            {
                type = builtin_intrinsic_type_scalar(reader, prefix, code, offset);
                *ice = prefix.ice;
                terminal = true;
            }
        }
    }
    while (depth != 0 && reader->result->status == BUILTIN_INTRINSIC_TYPES_SUCCESS)
    {
        const BuiltinIntrinsicTypeWrapper wrapper = wrappers[--depth];
        if (*ice)
        {
            builtin_intrinsic_type_fail(reader, BUILTIN_INTRINSIC_TYPES_INVALID, 0, wrapper.offset);
        }
        else
        {
            type = wrapper.code == 'X' ? type_complex(reader->table, type) : type_vector(reader->table, type, wrapper.count, wrapper.code == 'E');
            type = builtin_intrinsic_type_factory(reader, type, wrapper.offset);
        }
    }
    bool done = false;
    while (!done && reader->result->status == BUILTIN_INTRINSIC_TYPES_SUCCESS && reader->position < reader->input->encoding_length)
    {
        const uint32_t offset = reader->position;
        const uint8_t code = (uint8_t)reader->input->encoding[reader->position];
        if (code == 'C' || code == 'D' || code == 'R')
        {
            const uint32_t qualifiers = code == 'C' ? BUSTER_TYPE_CONST : (code == 'D' ? BUSTER_TYPE_VOLATILE : BUSTER_TYPE_RESTRICT);
            type = type_qualify(reader->table, type, qualifiers);
            reader->position += 1;
        }
        else if (code == '*' || code == '&')
        {
            reader->position += 1;
            uint32_t address_space = 0;
            const bool explicit_space = builtin_intrinsic_type_decimal(reader, &address_space);
            if (explicit_space)
            {
                builtin_intrinsic_type_fail(reader, BUILTIN_INTRINSIC_TYPES_UNSUPPORTED, BUSTER_INTRINSIC_TYPE_UNSUPPORTED_ADDRESS_SPACE, offset);
            }
            else if (code == '&')
            {
                builtin_intrinsic_type_fail(reader, BUILTIN_INTRINSIC_TYPES_UNSUPPORTED, BUSTER_INTRINSIC_TYPE_UNSUPPORTED_REFERENCE, offset);
            }
            else if (reader->result->status == BUILTIN_INTRINSIC_TYPES_SUCCESS)
            {
                type = builtin_intrinsic_type_factory(reader, type_pointer(reader->table, type), offset);
            }
        }
        else
        {
            done = true;
        }
    }
    if (reader->result->status == BUILTIN_INTRINSIC_TYPES_SUCCESS && *ice && type_kind(reader->table, type) != BUSTER_TYPE_KIND_INTEGER &&
        type_kind(reader->table, type) != BUSTER_TYPE_KIND_ENUM)
    {
        builtin_intrinsic_type_fail(reader, BUILTIN_INTRINSIC_TYPES_INVALID, 0, reader->position);
    }
    return type;
}

static __attribute__((unused)) void builtin_intrinsic_types_decode(TypeTable* table, const BuiltinIntrinsicTypeInput* input, BuiltinIntrinsicTypeResult* result)
{
    if (result != 0)
    {
        *result = (BuiltinIntrinsicTypeResult){ .status = BUILTIN_INTRINSIC_TYPES_INVALID };
        if (table != 0 && input != 0 && (input->attributes_length == 0 || input->attributes != 0))
        {
            BuiltinIntrinsicTypeReader reader = { .table = table, .input = input, .result = result };
            result->status = BUILTIN_INTRINSIC_TYPES_SUCCESS;
            if (input->attributes_length > BUSTER_INTRINSIC_TYPE_MAX_ATTRIBUTES)
            {
                builtin_intrinsic_type_fail(&reader, BUILTIN_INTRINSIC_TYPES_CAPACITY, 0, 0);
            }
            bool custom = input->custom_typechecking;
            for (uint32_t i = 0; result->status == BUILTIN_INTRINSIC_TYPES_SUCCESS && i < input->attributes_length; i += 1)
            {
                custom = custom || input->attributes[i] == 't';
                result->signature_ignored = result->signature_ignored || input->attributes[i] == 'T';
            }
            if (result->status == BUILTIN_INTRINSIC_TYPES_SUCCESS && custom)
            {
                result->status = BUILTIN_INTRINSIC_TYPES_CUSTOM;
            }
            else if (result->status == BUILTIN_INTRINSIC_TYPES_SUCCESS && input->encoding_length == 0)
            {
                builtin_intrinsic_type_fail(&reader, BUILTIN_INTRINSIC_TYPES_UNSUPPORTED, BUSTER_INTRINSIC_TYPE_UNSUPPORTED_MISSING_TYPE, 0);
            }
            else if (result->status == BUILTIN_INTRINSIC_TYPES_SUCCESS && (input->encoding == 0 || input->encoding_length > BUSTER_INTRINSIC_TYPE_MAX_ENCODING))
            {
                builtin_intrinsic_type_fail(&reader, input->encoding == 0 ? BUILTIN_INTRINSIC_TYPES_INVALID : BUILTIN_INTRINSIC_TYPES_CAPACITY, 0, 0);
            }
            if (result->status == BUILTIN_INTRINSIC_TYPES_SUCCESS)
            {
                bool ice = false;
                result->result_type = builtin_intrinsic_type_one(&reader, &ice);
                if (result->status == BUILTIN_INTRINSIC_TYPES_SUCCESS && ice)
                {
                    builtin_intrinsic_type_fail(&reader, BUILTIN_INTRINSIC_TYPES_INVALID, 0, 0);
                }
                while (result->status == BUILTIN_INTRINSIC_TYPES_SUCCESS && reader.position < input->encoding_length && input->encoding[reader.position] != '.')
                {
                    if (result->parameter_count == BUSTER_INTRINSIC_TYPE_MAX_PARAMETERS)
                    {
                        builtin_intrinsic_type_fail(&reader, BUILTIN_INTRINSIC_TYPES_CAPACITY, 0, reader.position);
                    }
                    else
                    {
                        const uint32_t start = reader.position;
                        ice = false;
                        TypeId parameter = builtin_intrinsic_type_one(&reader, &ice);
                        if (result->status == BUILTIN_INTRINSIC_TYPES_SUCCESS)
                        {
                            parameter = builtin_intrinsic_type_factory(&reader, type_adjust_parameter(table, parameter), start);
                            if (result->status == BUILTIN_INTRINSIC_TYPES_SUCCESS && type_kind(table, parameter) == BUSTER_TYPE_KIND_VOID)
                            {
                                builtin_intrinsic_type_fail(&reader, BUILTIN_INTRINSIC_TYPES_INVALID, 0, start);
                            }
                            if (result->status == BUILTIN_INTRINSIC_TYPES_SUCCESS)
                            {
                                result->parameters[result->parameter_count] = parameter;
                                result->ice_parameters |= ice ? UINT64_C(1) << result->parameter_count : 0;
                                result->parameter_count += 1;
                            }
                        }
                    }
                }
                if (result->status == BUILTIN_INTRINSIC_TYPES_SUCCESS && reader.position < input->encoding_length)
                {
                    result->variadic = true;
                    reader.position += 1;
                    if (reader.position != input->encoding_length)
                    {
                        builtin_intrinsic_type_fail(&reader, BUILTIN_INTRINSIC_TYPES_INVALID, 0, reader.position);
                    }
                }
                if (result->status == BUILTIN_INTRINSIC_TYPES_SUCCESS)
                {
                    result->prototype = input->strict_prototypes || result->parameter_count != 0 || !result->variadic;
                    bool model = type_unqualified(result->result_type) == BUSTER_TYPE_HALF;
                    for (uint32_t i = 0; i < result->parameter_count; i += 1)
                    {
                        model = model || result->parameters[i] == BUSTER_TYPE_HALF;
                    }
                    if (model)
                    {
                        builtin_intrinsic_type_fail(&reader, BUILTIN_INTRINSIC_TYPES_UNSUPPORTED, BUSTER_INTRINSIC_TYPE_UNSUPPORTED_MODEL, 0);
                    }
                    else
                    {
                        const uint8_t flags = (result->prototype ? BUSTER_TYPE_FLAG_PROTOTYPE : 0) |
                                              (result->prototype && result->variadic ? BUSTER_TYPE_FLAG_VARIADIC : 0);
                        result->function_type = builtin_intrinsic_type_factory(&reader, type_function(table, result->result_type, result->parameters, result->parameter_count, flags), 0);
                    }
                }
            }
        }
        if (result->status != BUILTIN_INTRINSIC_TYPES_SUCCESS)
        {
            const BuiltinIntrinsicTypeResult failure = { .status = result->status, .unsupported = result->unsupported, .error_offset = result->error_offset,
                                                        .type_error = result->type_error, .signature_ignored = result->signature_ignored };
            *result = failure;
        }
    }
}

static __attribute__((unused)) void builtin_intrinsic_types_decode_batch(TypeTable* table, const BuiltinIntrinsicTypeInput* inputs, BuiltinIntrinsicTypeResult* results, uint32_t count)
{
    for (uint32_t i = 0; i < count; i += 1)
    {
        builtin_intrinsic_types_decode(table, inputs + i, results + i);
    }
}
