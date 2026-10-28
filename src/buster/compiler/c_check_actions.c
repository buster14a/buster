// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// C semantic actions for #133. The parser adapter and either tree scheduler call the same operator contract.
// Type constraints are reported immediately; faults from potential constant evaluation travel with values until
// a context actually requires their evaluation. Short-circuit operators discard the unselected operand's fault.
// Integer constants hold target bits, never a host signed arithmetic value. No action recurses or prints.
// Include after c_types.c. The caller owns all action/result storage; batches need no alignment or padding.
#ifndef BUSTER_C_CHECK_ACTIONS_INCLUDED
#define BUSTER_C_CHECK_ACTIONS_INCLUDED

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include "host_isa.h"
#include "c_check_warning_events.c"

typedef enum CheckCategory
{
    CHECK_RVALUE,
    CHECK_LVALUE,
    CHECK_FUNCTION,
} CheckCategory;

typedef enum CheckConstantKind
{
    CHECK_CONSTANT_NONE,
    CHECK_CONSTANT_INTEGER,
    CHECK_CONSTANT_FLOATING,
    CHECK_CONSTANT_ADDRESS,
    CHECK_CONSTANT_COMPLEX,
} CheckConstantKind;

typedef enum CheckFault
{
    CHECK_FAULT_NONE,
    CHECK_FAULT_SIGNED_OVERFLOW,
    CHECK_FAULT_DIVIDE_BY_ZERO,
    CHECK_FAULT_SHIFT_COUNT,
    CHECK_FAULT_SHIFT_VALUE,
    CHECK_FAULT_UNSUPPORTED_CONSTANT,
} CheckFault;

#define BUSTER_CHECK_ICE 0x01u
#define BUSTER_CHECK_FLOAT_LITERAL 0x02u
#define BUSTER_CHECK_BIT_FIELD 0x04u
#define BUSTER_CHECK_REGISTER 0x08u
#define BUSTER_CHECK_SIDE_EFFECT 0x10u
#define BUSTER_CHECK_ICE_UNEVALUATED 0x20u
#define BUSTER_CHECK_NULL_POINTER 0x40u
#define BUSTER_CHECK_STATIC_OBJECT 0x80u
#define BUSTER_CHECK_VOLATILE_PART 0x100u
#define BUSTER_CHECK_GNU_CONSTANT 0x200u
#define BUSTER_CHECK_GNU_ICE_FORM 0x400u
#define BUSTER_CHECK_FP_ENVIRONMENT 0x800u
#define BUSTER_CHECK_VECTOR_ELEMENT 0x1000u
#define BUSTER_CHECK_VECTOR_DUPLICATE 0x2000u
#define BUSTER_CHECK_GNU_DIVISION 0x4000u
// A floating value converted to an integer type outside its range, which clang's folding saturates (a NaN to 0): a GNU
// constant where an integer constant expression is required, and a static initializer's value, but no array bound's.
#define BUSTER_CHECK_GNU_FLOAT_RANGE 0x8000u

// -std=gnu*. GNU's extensions themselves (void * arithmetic, sizeof(void), a void conditional arm) apply in every
// language mode, as clang's Sema has them; GNU_CONSTANT, folding beyond ICEs, is every mode's too (c_check_constant.c).
#define BUSTER_CHECK_EXTENSION_GNU 0x01u
#define BUSTER_CHECK_EXTENSION_GNU_CONSTANT 0x02u
// The action is written in a system header, where clang suppresses its warnings that default to errors and converts as
// they would have: an integer and a pointer either way (-Wint-conversion), pointers to incompatible types
// (-Wincompatible-pointer-types and its function pointers' group) and a value returned from a void function
// (-Wreturn-mismatch). Its other errors stay errors there. Per node (CheckGraph.system_headers), never a graph's.
#define BUSTER_CHECK_EXTENSION_SYSTEM_HEADER 0x04u
#define BUSTER_CHECK_FP_ENV_ACCESS 0x01u

typedef struct CheckValue CheckValue;
struct CheckValue
{
    TypeId type;
    CheckCategory category;
    uint32_t flags;
    CheckConstantKind constant_kind;
    unsigned __int128 integer;
    long double floating;
    // Floating complex constants retain both source-format components. GNU integer-complex constants use
    // integer's low/high 64-bit halves, each masked to the component type's width.
    long double imaginary;
    uint32_t object;
    // An immutable overload-set head plus one. It must obtain a contextual function identity before ordinary use.
    uint32_t overload_head;
    int64_t offset;
    uint32_t bit_width;
    CheckFault fault;
    uint32_t fault_node;
};

typedef enum CheckOp
{
    CHECK_OP_VALUE,
    CHECK_OP_POSITIVE,
    CHECK_OP_NEGATIVE,
    CHECK_OP_NOT,
    CHECK_OP_BIT_NOT,
    CHECK_OP_ADD,
    CHECK_OP_SUBTRACT,
    CHECK_OP_MULTIPLY,
    CHECK_OP_DIVIDE,
    CHECK_OP_REMAINDER,
    CHECK_OP_SHIFT_LEFT,
    CHECK_OP_SHIFT_RIGHT,
    CHECK_OP_LESS,
    CHECK_OP_LESS_EQUAL,
    CHECK_OP_GREATER,
    CHECK_OP_GREATER_EQUAL,
    CHECK_OP_EQUAL,
    CHECK_OP_NOT_EQUAL,
    CHECK_OP_BIT_AND,
    CHECK_OP_BIT_OR,
    CHECK_OP_BIT_XOR,
    CHECK_OP_LOGICAL_AND,
    CHECK_OP_LOGICAL_OR,
    CHECK_OP_CONDITIONAL,
    CHECK_OP_CAST,
    CHECK_OP_ADDRESS,
    CHECK_OP_DEREFERENCE,
    CHECK_OP_ASSIGN,
    CHECK_OP_COMMA,
    CHECK_OP_SUBSCRIPT,
    CHECK_OP_SIZEOF,
    CHECK_OP_ALIGNOF,
    CHECK_OP_MEMBER,
    CHECK_OP_ARROW,
    CHECK_OP_CALL,
    CHECK_OP_INITIALIZE,
    CHECK_OP_CONVERT,
    CHECK_OP_CONDITION,
    CHECK_OP_PRE_INCREMENT,
    CHECK_OP_PRE_DECREMENT,
    CHECK_OP_POST_INCREMENT,
    CHECK_OP_POST_DECREMENT,
    CHECK_OP_ADD_ASSIGN,
    CHECK_OP_SUBTRACT_ASSIGN,
    CHECK_OP_MULTIPLY_ASSIGN,
    CHECK_OP_DIVIDE_ASSIGN,
    CHECK_OP_REMAINDER_ASSIGN,
    CHECK_OP_SHIFT_LEFT_ASSIGN,
    CHECK_OP_SHIFT_RIGHT_ASSIGN,
    CHECK_OP_BIT_AND_ASSIGN,
    CHECK_OP_BIT_OR_ASSIGN,
    CHECK_OP_BIT_XOR_ASSIGN,
    // Static selection (_Generic and __builtin_choose_expr) preserves the chosen expression without conversion.
    CHECK_OP_SELECT,
    // Builtin graph actions use member as CBuiltinId and are dispatched after the shared builtin module is included.
    CHECK_OP_BUILTIN_UNARY,
    CHECK_OP_BUILTIN_OVERFLOW,
    // GNU statement-expression final value; member carries the preceding region's SIDE_EFFECT summary.
    CHECK_OP_STATEMENT_VALUE,
    // SysV varargs has a dedicated graph context and an optional type-name operand, never a guessed prototype.
    CHECK_OP_VARARGS,
    // Capture a runtime array bound once, or read its retained value. member is the dense extent identity + 1.
    CHECK_OP_VLA_BOUND,
    CHECK_OP_VLA_EXTENT,
    // Runtime byte-size operand built from retained extent values; type is the queried variable-size array.
    CHECK_OP_SIZEOF_VLA,
    // Bounded argument-slice builtins are dispatched by the graph after their semantic modules are included.
    CHECK_OP_BUILTIN_MISC,
    CHECK_OP_BUILTIN_FLOAT,
    CHECK_OP_BUILTIN_QUERY,
    CHECK_OP_BUILTIN_ATOMIC,
    CHECK_OP_REAL,
    CHECK_OP_IMAGINARY,
    CHECK_OP_BUILTIN_VECTOR,
    CHECK_OP_BUILTIN_X86,
    CHECK_OP_BUILTIN_BIT_CAST,
    CHECK_OP_COUNT,
} CheckOp;

typedef enum CheckDiagnostic
{
    CHECK_DIAG_NONE,
    CHECK_DIAG_INVALID_TYPE,
    CHECK_DIAG_ARITHMETIC_REQUIRED,
    CHECK_DIAG_INTEGER_REQUIRED,
    CHECK_DIAG_SCALAR_REQUIRED,
    CHECK_DIAG_POINTER_REQUIRED,
    CHECK_DIAG_LVALUE_REQUIRED,
    CHECK_DIAG_MODIFIABLE_REQUIRED,
    CHECK_DIAG_INCOMPATIBLE,
    CHECK_DIAG_INCOMPLETE,
    CHECK_DIAG_BIT_FIELD,
    CHECK_DIAG_REGISTER_ADDRESS,
    CHECK_DIAG_MEMBER,
    CHECK_DIAG_CALL,
    CHECK_DIAG_ARGUMENT_COUNT,
    CHECK_DIAG_RESOURCE,
    CHECK_DIAG_UNSUPPORTED,
} CheckDiagnostic;

#define BUSTER_CHECK_CONVERT_LVALUE 0x01u
#define BUSTER_CHECK_CONVERT_ARRAY 0x02u
#define BUSTER_CHECK_CONVERT_FUNCTION 0x04u
#define BUSTER_CHECK_CONVERT_PROMOTION 0x08u
#define BUSTER_CHECK_CONVERT_ARITHMETIC 0x10u
#define BUSTER_CHECK_CONVERT_ASSIGNMENT 0x20u
#define BUSTER_CHECK_CONVERT_CAST 0x40u
#define BUSTER_CHECK_CONVERT_NULL 0x80u
#define BUSTER_CHECK_CONVERT_TRANSPARENT_UNION 0x100u

#define CHECK_WARNING_DISCARDED_QUALIFIERS 0x01u
#define CHECK_WARNING_GNU_CONSTANT 0x02u
#define CHECK_WARNING_ENUM_TOO_LARGE 0x04u
#define CHECK_WARNING_INTEGER_OVERFLOW 0x08u
#define CHECK_WARNING_SHIFT_COUNT_NEGATIVE 0x10u
#define CHECK_WARNING_SHIFT_COUNT_OVERFLOW 0x20u
#define CHECK_WARNING_SHIFT_NEGATIVE_VALUE 0x40u
#define CHECK_WARNING_SHIFT_OVERFLOW 0x80u
// 0x100 to 0x400 are c_check_diagnostic.c's, whose table describes every bit. The driver keeps 16 bits per token.
#define CHECK_WARNING_POINTER_SIGN 0x800u
#define CHECK_WARNING_POINTER_INTEGER_COMPARE 0x1000u
#define CHECK_WARNING_DISTINCT_POINTER_COMPARE 0x2000u
#define CHECK_WARNING_CONDITIONAL_POINTER_INTEGER 0x4000u
#define CHECK_WARNING_CONDITIONAL_POINTER_TYPES 0x8000u
// Cold source/call diagnostics live above the packed 16-bit arithmetic warning keys.
#define CHECK_WARNING_KR_DEFINITION 0x200000u
#define CHECK_WARNING_NON_PROTOTYPE_CALL 0x400000u
#define CHECK_WARNING_KR_TOO_FEW 0x800000u
#define CHECK_WARNING_KR_TOO_MANY 0x1000000u
#define CHECK_WARNING_KR_CALLS (CHECK_WARNING_NON_PROTOTYPE_CALL | CHECK_WARNING_KR_TOO_FEW | CHECK_WARNING_KR_TOO_MANY)

typedef struct CheckConversion CheckConversion;
struct CheckConversion
{
    TypeId type;
    uint32_t flags;
};

typedef struct CheckAction CheckAction;
typedef struct CheckVectorMember CheckVectorMember;
struct CheckAction
{
    CheckOp op;
    // The checker's scalar path (CheckOptions.scalar), whose actions take the exact twins of what has one: a member
    // access's lookup compares one name at a time (type_lookup_member_scalar).
    bool scalar;
    CheckValue operands[3];
    // Cast destination; sizeof/alignof type when nonzero. Otherwise these query operand 0's type.
    TypeId type;
    // IdentId for MEMBER/ARROW; opaque node identity for a newly produced evaluation fault.
    uint32_t member;
    uint32_t node;
    const CheckValue* arguments;
    uint32_t argument_count;
    // Optional argument-conversion output with argument_count writable records. Required for CALL with arguments.
    CheckConversion* argument_conversions;
    // Optional immutable source history. Positions are exclusive expression/type-name ends, independently of
    // diagnostic node identity. A null history uses the caller's current type table, as the direct action API does.
    const uint32_t* tag_completions;
    // Optional activation positions for nominal transparent-union attributes, indexed by the same tag table.
    const uint32_t* transparent_unions;
    uint32_t tag_completion_count;
    uint32_t source_position;
    uint32_t extensions;
    // Runtime floating environment, independent of source constant-expression evaluation.
    uint32_t fp_environment;
    const CheckVectorMember* vector_member;
};

typedef struct CheckActionResult CheckActionResult;
struct CheckActionResult
{
    CheckValue value;
    CheckConversion conversions[3];
    // Compound updates read/convert operand 0 once, then convert their arithmetic result for the store.
    CheckConversion store_conversion;
    CheckDiagnostic diagnostic;
    uint32_t diagnostic_operand;
    uint32_t warnings;
    // MEMBER and ARROW: the member's offset in bits from its record's start, through anonymous members, which the lowering
    // takes from the stream rather than looking the member up again (#591). 0 for other actions.
    uint64_t member_offset;
};

static void check_action_scalar(TypeTable* table, const CheckAction* action, CheckActionResult* result);
static void check_actions(TypeTable* table, const CheckAction* actions, CheckActionResult* results, uint32_t count);
static bool check_action_vector_eligible(const TypeTable* table, const CheckAction* action);
static void check_complex_convert(const TypeTable* table, CheckValue* value, TypeId destination, uint32_t node);
static uint32_t check_complex_fold(const TypeTable* table, CheckOp op, const CheckValue* left, const CheckValue* right, CheckValue* value, uint32_t node, uint32_t extensions);
static void check_complex_part(TypeTable* table, const CheckAction* action, CheckActionResult* result);
static void check_complex_conjugate(TypeTable* table, const CheckAction* action, CheckActionResult* result);
static bool check_vector_type(const TypeTable* table, TypeId type);
static bool check_vector_assignable(const TypeTable* table, TypeId destination, const CheckValue* source, CheckActionResult* result);
static void check_vector_convert(CheckValue* value, TypeId destination);
static bool check_vector_action(TypeTable* table, const CheckAction* action, CheckActionResult* result);

static unsigned __int128 check_action_mask(uint32_t width)
{
    return width == 128 ? ~(unsigned __int128)0 : ((unsigned __int128)1 << width) - 1;
}

static uint8_t check_action_kind(const TypeTable* table, TypeId type)
{
    return type_kind(table, type_strip_aligned(table, type));
}

static bool check_action_runtime_size(const TypeTable* table, TypeId type)
{
    TypeId base = type_strip_aligned(table, type);
    bool runtime = false;
    while (!runtime && type_kind_is_array(type_kind(table, base)))
    {
        runtime = type_kind(table, base) == BUSTER_TYPE_KIND_VARIABLE_ARRAY;
        base = type_strip_aligned(table, type_element(table, base));
    }
    return runtime;
}

static bool check_action_source_complete(const TypeTable* table, TypeId type, const CheckAction* action)
{
    bool complete = type_is_complete(table, type);
    if (complete && action->tag_completions != NULL)
    {
        TypeId base = type_strip_aligned(table, type);
        while (type_kind_is_array(type_kind(table, base)))
        {
            base = type_strip_aligned(table, type_element(table, base));
        }
        const uint8_t kind = type_kind(table, base);
        if (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION || kind == BUSTER_TYPE_KIND_ENUM)
        {
            const uint32_t tag = table->types.index[type_index(base)];
            complete = tag < action->tag_completion_count && action->tag_completions[tag] <= action->source_position;
        }
    }
    return complete;
}

static bool check_action_integer_type(const TypeTable* table, TypeId type, uint32_t* width, bool* is_signed)
{
    TypeId base = type_unqualified(type_strip_aligned(table, type));
    if (type_kind(table, base) == BUSTER_TYPE_KIND_ENUM && type_is_complete(table, base))
    {
        base = table->types.base[type_index(base)];
    }
    const uint8_t kind = type_kind(table, base);
    bool result = kind == BUSTER_TYPE_KIND_INTEGER || kind == BUSTER_TYPE_KIND_BIT_INT;
    *width = 0;
    *is_signed = false;
    if (result)
    {
        uint64_t rank = 0;
        uint64_t bits = 0;
        type_integer_properties(table, base, &rank, &bits, is_signed);
        result = bits >= 1 && bits <= 128;
        *width = result ? (uint32_t)bits : 0;
    }
    return result;
}

static bool check_action_is_integer(const TypeTable* table, TypeId type)
{
    const uint8_t kind = check_action_kind(table, type);
    return kind == BUSTER_TYPE_KIND_INTEGER || kind == BUSTER_TYPE_KIND_BIT_INT || kind == BUSTER_TYPE_KIND_ENUM;
}

static bool check_action_is_arithmetic(const TypeTable* table, TypeId type)
{
    const uint8_t kind = check_action_kind(table, type);
    return check_action_is_integer(table, type) || kind == BUSTER_TYPE_KIND_FLOATING || kind == BUSTER_TYPE_KIND_COMPLEX;
}

static bool check_action_is_scalar(const TypeTable* table, TypeId type)
{
    const uint8_t kind = check_action_kind(table, type);
    return check_action_is_arithmetic(table, type) || kind == BUSTER_TYPE_KIND_POINTER || kind == BUSTER_TYPE_KIND_NULLPTR;
}

static void check_action_diagnose(CheckActionResult* result, CheckDiagnostic diagnostic, uint32_t operand)
{
    if (result->diagnostic == CHECK_DIAG_NONE)
    {
        result->diagnostic = diagnostic;
        result->diagnostic_operand = operand;
    }
}

static void check_action_set_fault(CheckValue* value, CheckFault fault, uint32_t node)
{
    if (value->fault == CHECK_FAULT_NONE)
    {
        value->fault = fault;
        value->fault_node = node;
    }
    value->constant_kind = CHECK_CONSTANT_NONE;
    value->integer = 0;
    value->floating = 0;
    value->imaginary = 0;
}

static void check_action_inherit(CheckValue* value, const CheckValue* operand)
{
    value->flags |= operand->flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_GNU_CONSTANT | BUSTER_CHECK_FP_ENVIRONMENT | BUSTER_CHECK_GNU_DIVISION |
                                      BUSTER_CHECK_GNU_FLOAT_RANGE);
    if (value->fault == CHECK_FAULT_NONE && operand->fault != CHECK_FAULT_NONE)
    {
        value->fault = operand->fault;
        value->fault_node = operand->fault_node;
    }
}

static bool check_action_integer_constant(const CheckValue* value, uint32_t extensions)
{
    return value->fault == CHECK_FAULT_NONE && value->constant_kind == CHECK_CONSTANT_INTEGER &&
           (value->flags & BUSTER_CHECK_SIDE_EFFECT) == 0 &&
           ((value->flags & BUSTER_CHECK_ICE) != 0 || (extensions & BUSTER_CHECK_EXTENSION_GNU_CONSTANT) != 0);
}

static bool check_action_truth(const TypeTable* table, const CheckValue* value, bool* truth)
{
    bool known = value->fault == CHECK_FAULT_NONE;
    if (value->constant_kind == CHECK_CONSTANT_INTEGER)
    {
        uint32_t width = 0;
        bool is_signed = false;
        const bool integer = check_action_integer_type(table, value->type, &width, &is_signed);
        known &= integer;
        *truth = integer && (value->integer & check_action_mask(width)) != 0;
    }
    else if (value->constant_kind == CHECK_CONSTANT_FLOATING)
    {
        *truth = value->floating != 0;
    }
    else if (value->constant_kind == CHECK_CONSTANT_COMPLEX)
    {
        const TypeId element = type_element(table, type_strip_aligned(table, value->type));
        *truth = check_action_is_integer(table, element) ? value->integer != 0 : value->floating != 0 || value->imaginary != 0;
    }
    else if (value->constant_kind == CHECK_CONSTANT_ADDRESS)
    {
        // Absolute addresses, including null, are independent of final placement and weak symbol resolution.
        *truth = value->offset != 0;
        known &= value->object == 0;
    }
    else
    {
        *truth = false;
        known = false;
    }
    return known;
}

static bool check_action_null(const TypeTable* table, const CheckValue* value, uint32_t extensions)
{
    bool truth = false;
    const uint32_t forms = BUSTER_CHECK_ICE | ((extensions & BUSTER_CHECK_EXTENSION_GNU_CONSTANT) != 0 ? BUSTER_CHECK_GNU_ICE_FORM : 0);
    const bool zero_integer = (value->flags & forms) != 0 && value->constant_kind == CHECK_CONSTANT_INTEGER && check_action_truth(table, value, &truth) && !truth;
    return check_action_kind(table, value->type) == BUSTER_TYPE_KIND_NULLPTR || (value->flags & BUSTER_CHECK_NULL_POINTER) != 0 || zero_integer;
}

static bool check_action_float_supported(const TypeTable* table, TypeId type)
{
    const TypeId base = type_unqualified(type_strip_aligned(table, type));
    return base == BUSTER_TYPE_FLOAT || base == BUSTER_TYPE_DOUBLE || base == BUSTER_TYPE_LONG_DOUBLE;
}

static long double check_action_round_float(TypeId type, long double value)
{
    long double result = value;
    if (type == BUSTER_TYPE_FLOAT)
    {
        result = (float)value;
    }
    else if (type == BUSTER_TYPE_DOUBLE)
    {
        result = (double)value;
    }
    return result;
}

// Convert a constant's representation after type checking has authorized the conversion. Every integer operation
// uses unsigned bits, and every floating-to-integer cast is range checked before host C is allowed to evaluate it.
static void check_action_convert_plain(const TypeTable* table, CheckValue* value, TypeId destination, uint32_t node)
{
    const TypeId source = value->type;
    const uint8_t target_kind = check_action_kind(table, destination);
    uint32_t source_width = 0;
    uint32_t target_width = 0;
    bool source_signed = false;
    bool target_signed = false;
    const bool source_integer = check_action_integer_type(table, source, &source_width, &source_signed);
    const bool target_integer = check_action_integer_type(table, destination, &target_width, &target_signed);
    if (target_integer && type_unqualified(type_strip_aligned(table, destination)) == BUSTER_TYPE_BOOL)
    {
        bool truth = false;
        const bool known = check_action_truth(table, value, &truth);
        value->constant_kind = known ? CHECK_CONSTANT_INTEGER : CHECK_CONSTANT_NONE;
        value->integer = truth;
    }
    else if (target_integer && value->constant_kind == CHECK_CONSTANT_INTEGER && source_integer)
    {
        const unsigned __int128 mask = check_action_mask(source_width);
        unsigned __int128 bits = value->integer & mask;
        if (source_signed && (bits >> (source_width - 1)) != 0 && target_width > source_width)
        {
            bits |= ~mask;
        }
        value->integer = bits & check_action_mask(target_width);
    }
    else if (target_integer && value->constant_kind == CHECK_CONSTANT_FLOATING)
    {
        const unsigned __int128 high_bit = (unsigned __int128)1 << (target_width - 1);
        const long double limit = (long double)high_bit * (target_signed ? 1.0L : 2.0L);
        const long double minimum = target_signed ? -limit : 0;
        const long double input = value->floating;
        // For 128-bit signed values the host x87 cannot distinguish minimum - 1 from minimum.
        const bool lower = target_signed && target_width == 128 ? input >= minimum : input > minimum - 1;
        if (!__builtin_isfinite(input) || !lower || input >= limit)
        {
            // Undefined, and as clang's folding takes it, APFloat's conversion: saturated to the type's range, a NaN 0. A
            // GNU constant, not an ICE (BUSTER_CHECK_GNU_FLOAT_RANGE).
            value->integer = __builtin_isnan(input) ? 0 : input < 0 ? (target_signed ? high_bit : 0) : (target_signed ? high_bit - 1 : check_action_mask(target_width));
            value->constant_kind = CHECK_CONSTANT_INTEGER;
            value->flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM);
            value->flags |= BUSTER_CHECK_GNU_CONSTANT | BUSTER_CHECK_GNU_FLOAT_RANGE;
        }
        else
        {
            value->integer = input < 0 ? (unsigned __int128)(__int128)input : (unsigned __int128)input;
            value->integer &= check_action_mask(target_width);
            value->constant_kind = CHECK_CONSTANT_INTEGER;
        }
    }
    else if (target_integer && value->constant_kind == CHECK_CONSTANT_ADDRESS)
    {
        const bool pointer = check_action_kind(table, source) == BUSTER_TYPE_KIND_POINTER;
        if (value->object == 0 && pointer)
        {
            value->integer = (unsigned __int128)(uint64_t)value->offset & check_action_mask(target_width);
            value->constant_kind = CHECK_CONSTANT_INTEGER;
        }
        else if (target_width != 64 || (!pointer && (!source_integer || source_width != 64)) || (value->flags & BUSTER_CHECK_STATIC_OBJECT) == 0)
        {
            // The current relocation representation is ABS64. Narrowing or widening a symbolic address cannot
            // be expressed by changing the addend; leave those cases nonconstant for the consumer to reject.
            value->constant_kind = CHECK_CONSTANT_NONE;
        }
    }
    else if (target_kind == BUSTER_TYPE_KIND_FLOATING && (value->constant_kind == CHECK_CONSTANT_INTEGER || value->constant_kind == CHECK_CONSTANT_FLOATING))
    {
        if (!check_action_float_supported(table, destination) || (value->constant_kind == CHECK_CONSTANT_INTEGER && !source_integer))
        {
            check_action_set_fault(value, CHECK_FAULT_UNSUPPORTED_CONSTANT, node);
        }
        else
        {
            long double number = value->floating;
            if (value->constant_kind == CHECK_CONSTANT_INTEGER)
            {
                const unsigned __int128 mask = check_action_mask(source_width);
                const unsigned __int128 bits = value->integer & mask;
                const bool negative = source_signed && (bits >> (source_width - 1)) != 0;
                const unsigned __int128 magnitude = negative ? (0 - bits) & mask : bits;
                const TypeId floating_type = type_unqualified(type_strip_aligned(table, destination));
                // An intermediate x87 conversion can double round a 128-bit integer near a float/double midpoint.
                // Convert the unsigned magnitude directly to the destination format; sign reversal is exact.
                if (floating_type == BUSTER_TYPE_FLOAT)
                {
                    number = (float)magnitude;
                }
                else if (floating_type == BUSTER_TYPE_DOUBLE)
                {
                    number = (double)magnitude;
                }
                else
                {
                    number = (long double)magnitude;
                }
                number = negative ? -number : number;
            }
            value->floating = check_action_round_float(type_unqualified(type_strip_aligned(table, destination)), number);
            value->constant_kind = CHECK_CONSTANT_FLOATING;
        }
    }
    else if (target_kind == BUSTER_TYPE_KIND_POINTER && check_action_null(table, value, BUSTER_CHECK_EXTENSION_GNU_CONSTANT))
    {
        value->constant_kind = CHECK_CONSTANT_ADDRESS;
        value->object = 0;
        value->offset = 0;
    }
    else if (target_kind == BUSTER_TYPE_KIND_POINTER && source_integer && value->constant_kind == CHECK_CONSTANT_INTEGER)
    {
        const unsigned __int128 mask = check_action_mask(source_width);
        unsigned __int128 bits = value->integer & mask;
        if (source_signed && (bits >> (source_width - 1)) != 0)
        {
            bits |= ~mask;
        }
        // The target is x86-64: integer-to-pointer casts retain the low address bits. object zero denotes an
        // absolute address; offset retains those bits as the target's signed relocation addend representation.
        value->constant_kind = CHECK_CONSTANT_ADDRESS;
        value->object = 0;
        value->offset = (int64_t)(uint64_t)bits;
    }
    else if (target_kind == BUSTER_TYPE_KIND_POINTER && value->constant_kind == CHECK_CONSTANT_ADDRESS)
    {
        // Changing pointer type preserves the symbolic object and byte addend.
    }
    else if (type_unqualified(source) != type_unqualified(destination))
    {
        value->constant_kind = CHECK_CONSTANT_NONE;
    }
    value->type = destination;
    value->category = CHECK_RVALUE;
    value->flags &= ~(BUSTER_CHECK_BIT_FIELD | BUSTER_CHECK_REGISTER | BUSTER_CHECK_NULL_POINTER);
    if (!target_integer)
    {
        value->flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM);
    }
}

static void check_action_convert_value(const TypeTable* table, CheckValue* value, TypeId destination, uint32_t node)
{
    if (check_vector_type(table, value->type) || check_vector_type(table, destination))
    {
        check_vector_convert(value, destination);
    }
    else if (check_action_kind(table, value->type) == BUSTER_TYPE_KIND_COMPLEX || check_action_kind(table, destination) == BUSTER_TYPE_KIND_COMPLEX)
    {
        check_complex_convert(table, value, destination, node);
    }
    else
    {
        check_action_convert_plain(table, value, destination, node);
    }
}

static void check_action_conversion(const TypeTable* table, CheckValue* value, TypeId destination, uint32_t flags, CheckConversion* conversion, uint32_t node)
{
    conversion->flags |= flags;
    conversion->type = destination;
    check_action_convert_value(table, value, destination, node);
}

static CheckValue check_action_operand(TypeTable* table, const CheckAction* action, CheckActionResult* result, uint32_t index)
{
    CheckValue value = action->operands[index];
    CheckConversion* conversion = &result->conversions[index];
    conversion->type = value.type;
    const TypeId plain = type_strip_aligned(table, value.type);
    const uint8_t kind = type_kind(table, plain);
    if (type_kind_is_array(kind))
    {
        if ((value.flags & BUSTER_CHECK_REGISTER) != 0)
        {
            check_action_diagnose(result, CHECK_DIAG_REGISTER_ADDRESS, index);
        }
        const TypeId pointer = type_pointer(table, type_element(table, plain));
        if (pointer == 0)
        {
            check_action_diagnose(result, CHECK_DIAG_RESOURCE, index);
        }
        else
        {
            value.type = pointer;
            value.category = CHECK_RVALUE;
            value.flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_BIT_FIELD | BUSTER_CHECK_REGISTER);
            if ((value.flags & BUSTER_CHECK_STATIC_OBJECT) == 0 && value.constant_kind == CHECK_CONSTANT_ADDRESS)
            {
                value.constant_kind = CHECK_CONSTANT_NONE;
            }
            conversion->type = pointer;
            conversion->flags |= BUSTER_CHECK_CONVERT_ARRAY;
        }
    }
    else if (kind == BUSTER_TYPE_KIND_FUNCTION)
    {
        const TypeId pointer = type_pointer(table, plain);
        if (pointer == 0)
        {
            check_action_diagnose(result, CHECK_DIAG_RESOURCE, index);
        }
        else
        {
            value.type = pointer;
            value.category = CHECK_RVALUE;
            value.flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_FLOAT_LITERAL);
            conversion->type = pointer;
            conversion->flags |= BUSTER_CHECK_CONVERT_FUNCTION;
        }
    }
    else if (value.category == CHECK_LVALUE)
    {
        // A void lvalue (*p of a void *) converts as GNU's extension, in every language mode as clang has it.
        if (!check_action_source_complete(table, value.type, action) && kind != BUSTER_TYPE_KIND_VOID)
        {
            check_action_diagnose(result, CHECK_DIAG_INCOMPLETE, index);
        }
        if (value.constant_kind == CHECK_CONSTANT_ADDRESS || (value.flags & BUSTER_CHECK_ICE) == 0)
        {
            value.constant_kind = CHECK_CONSTANT_NONE;
        }
        if ((type_qualifiers(value.type) & BUSTER_TYPE_VOLATILE) != 0 || (value.flags & BUSTER_CHECK_VOLATILE_PART) != 0 || kind == BUSTER_TYPE_KIND_ATOMIC)
        {
            value.flags |= BUSTER_CHECK_SIDE_EFFECT;
            value.constant_kind = CHECK_CONSTANT_NONE;
        }
        value.type = type_conversion_operand(table, value.type);
        value.category = CHECK_RVALUE;
        value.flags &= ~(BUSTER_CHECK_REGISTER | BUSTER_CHECK_VOLATILE_PART);
        conversion->type = value.type;
        conversion->flags |= BUSTER_CHECK_CONVERT_LVALUE;
    }
    return value;
}

static void check_action_promote(const TypeTable* table, CheckValue* value, CheckConversion* conversion, uint32_t node)
{
    const TypeId promoted = (value->flags & BUSTER_CHECK_BIT_FIELD) != 0 ? type_promote_bit_field(table, value->type, value->bit_width) : type_promote(table, value->type);
    if (promoted != value->type)
    {
        check_action_conversion(table, value, promoted, BUSTER_CHECK_CONVERT_PROMOTION, conversion, node);
    }
}

static bool check_action_compatible(const TypeTable* table, TypeId left, TypeId right, CheckActionResult* result)
{
    const TypeAnswer answer = left == right ? TYPE_ANSWER_YES : type_compatible(table, left, right);
    if (answer == TYPE_ANSWER_FULL)
    {
        check_action_diagnose(result, CHECK_DIAG_RESOURCE, 0);
    }
    return answer == TYPE_ANSWER_YES;
}

static bool check_action_compatible_at(const TypeTable* table, TypeId left, TypeId right, const CheckAction* action, CheckActionResult* result)
{
    const TypeAnswer answer = left == right ? TYPE_ANSWER_YES :
                              type_compatible_at(table, left, right, action->transparent_unions, action->tag_completion_count, action->source_position);
    if (answer == TYPE_ANSWER_FULL)
    {
        check_action_diagnose(result, CHECK_DIAG_RESOURCE, 0);
    }
    return answer == TYPE_ANSWER_YES;
}

// clang's pointer conversions take pointees that differ only in signedness for one type: each character type as
// unsigned char, each signed integer type as its unsigned counterpart (a complete enumeration's through its underlying
// type, but an enumeration of an unsigned type as itself), each _BitInt by its width, and anything else as itself.
static uint64_t check_action_sign_key(const TypeTable* table, TypeId type)
{
    const TypeId base = type_unqualified(type_strip_aligned(table, type));
    const uint8_t kind = type_kind(table, base);
    TypeId integer = base;
    if (kind == BUSTER_TYPE_KIND_ENUM && type_is_complete(table, base))
    {
        integer = type_unqualified(table->types.base[type_index(base)]);
    }
    const uint8_t integer_kind = type_kind(table, integer);
    uint64_t rank = 0;
    uint64_t width = 0;
    bool is_signed = false;
    if (integer_kind == BUSTER_TYPE_KIND_INTEGER || integer_kind == BUSTER_TYPE_KIND_BIT_INT)
    {
        type_integer_properties(table, integer, &rank, &width, &is_signed);
    }
    uint64_t key = base;
    if (kind == BUSTER_TYPE_KIND_INTEGER || kind == BUSTER_TYPE_KIND_BIT_INT || (kind == BUSTER_TYPE_KIND_ENUM && is_signed))
    {
        key = integer_kind == BUSTER_TYPE_KIND_BIT_INT ? (UINT64_C(1) << 32) | width : BUSTER_TYPE_BUILTIN(type_integer_info[type_index(integer)].unsigned_code);
    }
    return key;
}

// clang's rule for pointers to pointers: pointees that reach one canonical type through as many pointer levels on
// both sides differ only in their levels' qualifiers (char ** to const char **), which it warns about.
static bool check_action_nested_qualifiers(const TypeTable* table, TypeId left, TypeId right, CheckActionResult* result)
{
    TypeId l = type_strip_aligned(table, left);
    TypeId r = type_strip_aligned(table, right);
    const bool nested = type_kind(table, l) == BUSTER_TYPE_KIND_POINTER && type_kind(table, r) == BUSTER_TYPE_KIND_POINTER;
    while (type_kind(table, l) == BUSTER_TYPE_KIND_POINTER && type_kind(table, r) == BUSTER_TYPE_KIND_POINTER)
    {
        l = type_strip_aligned(table, type_element(table, l));
        r = type_strip_aligned(table, type_element(table, r));
    }
    const TypeAnswer answer = nested ? type_same_canonical(table, type_unqualified(l), type_unqualified(r)) : TYPE_ANSWER_NO;
    if (answer == TYPE_ANSWER_FULL)
    {
        check_action_diagnose(result, CHECK_DIAG_RESOURCE, 0);
    }
    return answer == TYPE_ANSWER_YES;
}

// Pointer assignment as clang's checkPointerTypesForAssignment decides it in every language mode: the pointees'
// qualifiers may only be added, but losing one only warns, and so do pointees that differ only in signedness and
// pointers to pointers whose levels' qualifiers differ. void * converts to and from any object pointer, and to and
// from a function pointer as a pedantic extension, without the qualifier warning. strict takes clang's Compatible
// alone, as transparent-union selection does: no warning, and no function pointer through void *.
static bool check_action_assignable_mode(const TypeTable* table, TypeId destination, const CheckValue* source, const CheckAction* action, CheckActionResult* result, bool strict)
{
    const TypeId target = type_unqualified(type_strip_aligned(table, destination));
    const TypeId input = type_unqualified(type_strip_aligned(table, source->type));
    const uint8_t target_kind = type_kind(table, target);
    const uint8_t input_kind = type_kind(table, input);
    bool accepted = false;
    if (check_vector_type(table, target) || check_vector_type(table, input))
    {
        accepted = check_vector_assignable(table, target, source, result);
    }
    else if (check_action_is_arithmetic(table, target) && check_action_is_arithmetic(table, input))
    {
        accepted = true;
    }
    else if (target == BUSTER_TYPE_BOOL && check_action_is_scalar(table, input))
    {
        accepted = true;
    }
    else if (target_kind == BUSTER_TYPE_KIND_POINTER && check_action_null(table, source, action->extensions))
    {
        accepted = true;
    }
    else if (target_kind == BUSTER_TYPE_KIND_POINTER && input_kind == BUSTER_TYPE_KIND_POINTER)
    {
        const TypeId to = type_element(table, target);
        const TypeId from = type_element(table, input);
        const uint8_t tk = check_action_kind(table, to);
        const uint8_t fk = check_action_kind(table, from);
        const bool function = tk == BUSTER_TYPE_KIND_FUNCTION || fk == BUSTER_TYPE_KIND_FUNCTION;
        uint32_t warning = (type_qualifiers(from) & ~type_qualifiers(to)) == 0 ? 0 : CHECK_WARNING_DISCARDED_QUALIFIERS;
        bool extension = false;
        if (tk == BUSTER_TYPE_KIND_VOID || fk == BUSTER_TYPE_KIND_VOID)
        {
            accepted = true;
            extension = function;
            warning = function ? 0 : warning;
        }
        else if (check_action_compatible_at(table, type_unqualified(to), type_unqualified(from), action, result))
        {
            accepted = true;
        }
        else if (result->diagnostic == CHECK_DIAG_NONE && check_action_sign_key(table, to) == check_action_sign_key(table, from))
        {
            accepted = true;
            warning = warning != 0 ? warning : CHECK_WARNING_POINTER_SIGN;
        }
        else if (result->diagnostic == CHECK_DIAG_NONE && check_action_nested_qualifiers(table, to, from, result))
        {
            accepted = true;
            warning = CHECK_WARNING_DISCARDED_QUALIFIERS;
        }
        accepted = accepted && !(strict && (warning != 0 || extension));
        result->warnings |= accepted ? warning : 0;
    }
    else if ((target_kind == BUSTER_TYPE_KIND_STRUCT || target_kind == BUSTER_TYPE_KIND_UNION || target_kind == BUSTER_TYPE_KIND_NULLPTR ||
              target_kind == BUSTER_TYPE_KIND_ATOMIC) && target_kind == input_kind)
    {
        accepted = check_action_compatible_at(table, target, input, action, result);
    }
    if (!accepted && !strict && (action->extensions & BUSTER_CHECK_EXTENSION_SYSTEM_HEADER) != 0 && result->diagnostic == CHECK_DIAG_NONE &&
        !check_vector_type(table, target) && !check_vector_type(table, input))
    {
        accepted = (target_kind == BUSTER_TYPE_KIND_POINTER && (input_kind == BUSTER_TYPE_KIND_POINTER || check_action_is_integer(table, input))) ||
                   (input_kind == BUSTER_TYPE_KIND_POINTER && check_action_is_integer(table, target));
    }
    return accepted;
}

static bool check_action_assignable(const TypeTable* table, TypeId destination, const CheckValue* source, const CheckAction* action, CheckActionResult* result)
{
    const bool accepted = check_action_assignable_mode(table, destination, source, action, result, false);
    return accepted;
}

typedef struct CheckIntegerFold CheckIntegerFold;
struct CheckIntegerFold
{
    bool active;
    CheckOp op;
    unsigned __int128 left;
    unsigned __int128 right;
    uint32_t width;
    uint32_t right_width;
    bool is_signed;
    bool right_signed;
    uint32_t node;
    uint32_t extensions;
};

#include "c_check_integer_extension.c"

static uint32_t check_action_integer_warning(CheckIntegerFold fold, CheckFault fault)
{
    uint32_t warning = 0;
    if (fault == CHECK_FAULT_SIGNED_OVERFLOW &&
        (fold.op == CHECK_OP_NEGATIVE || fold.op == CHECK_OP_ADD || fold.op == CHECK_OP_SUBTRACT || fold.op == CHECK_OP_MULTIPLY))
    {
        warning = CHECK_WARNING_INTEGER_OVERFLOW;
    }
    else if (fault == CHECK_FAULT_SHIFT_COUNT)
    {
        const bool negative = fold.right_signed && ((fold.right >> (fold.right_width - 1)) & 1) != 0;
        warning = negative ? CHECK_WARNING_SHIFT_COUNT_NEGATIVE : CHECK_WARNING_SHIFT_COUNT_OVERFLOW;
    }
    else if (fault == CHECK_FAULT_SHIFT_VALUE)
    {
        const bool negative = fold.is_signed && ((fold.left >> (fold.width - 1)) & 1) != 0;
        warning = negative ? CHECK_WARNING_SHIFT_NEGATIVE_VALUE : CHECK_WARNING_SHIFT_OVERFLOW;
    }
    return warning;
}

static uint32_t check_action_fold_integer_result(CheckIntegerFold fold, CheckValue* value, unsigned __int128 answer, CheckFault fault)
{
    CheckIntegerExtension extension = { 0 };
    if (fault != CHECK_FAULT_NONE && (fold.extensions & BUSTER_CHECK_EXTENSION_GNU_CONSTANT) != 0)
    {
        extension = check_integer_extension(fold, fault);
    }
    if (extension.applied)
    {
        const bool ice_form = (value->flags & (BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM)) != 0;
        value->flags &= ~BUSTER_CHECK_ICE;
        value->flags |= BUSTER_CHECK_GNU_CONSTANT | (ice_form ? BUSTER_CHECK_GNU_ICE_FORM : 0);
        value->flags |= fold.op == CHECK_OP_DIVIDE || fold.op == CHECK_OP_REMAINDER ? BUSTER_CHECK_GNU_DIVISION : 0;
        value->constant_kind = CHECK_CONSTANT_INTEGER;
        value->integer = extension.integer;
    }
    else if (fault != CHECK_FAULT_NONE)
    {
        check_action_set_fault(value, fault, fold.node);
    }
    else
    {
        value->constant_kind = CHECK_CONSTANT_INTEGER;
        value->integer = answer;
    }
    // The arithmetic warning belongs to this operation. A GNU folding diagnostic belongs to the required
    // constant context, which can have a different default severity and must be attached by its source reader.
    return extension.applied ? check_action_integer_warning(fold, fault) : 0;
}

static uint32_t check_action_fold_integer(CheckIntegerFold fold, CheckValue* value)
{
    const unsigned __int128 mask = check_action_mask(fold.width);
    const unsigned __int128 a = fold.left & mask;
    const unsigned __int128 b = fold.right & check_action_mask(fold.right_width);
    const unsigned __int128 sign = (unsigned __int128)1 << (fold.width - 1);
    const bool an = fold.is_signed && (a & sign) != 0;
    const bool bn = fold.right_signed && (b >> (fold.right_width - 1)) != 0;
    const unsigned __int128 am = an ? (0 - a) & mask : a;
    const unsigned __int128 bm = bn ? (0 - b) & check_action_mask(fold.right_width) : b;
    unsigned __int128 answer = 0;
    CheckFault fault = CHECK_FAULT_NONE;
    switch (fold.op)
    {
        case CHECK_OP_POSITIVE: answer = a; break;
        case CHECK_OP_NEGATIVE:
        {
            answer = (0 - a) & mask;
            fault = fold.is_signed && a == sign ? CHECK_FAULT_SIGNED_OVERFLOW : CHECK_FAULT_NONE;
        } break;
        case CHECK_OP_BIT_NOT: answer = (~a) & mask; break;
        case CHECK_OP_ADD:
        {
            answer = (a + b) & mask;
            fault = fold.is_signed && ((~(a ^ b) & (a ^ answer) & sign) != 0) ? CHECK_FAULT_SIGNED_OVERFLOW : CHECK_FAULT_NONE;
        } break;
        case CHECK_OP_SUBTRACT:
        {
            answer = (a - b) & mask;
            fault = fold.is_signed && (((a ^ b) & (a ^ answer) & sign) != 0) ? CHECK_FAULT_SIGNED_OVERFLOW : CHECK_FAULT_NONE;
        } break;
        case CHECK_OP_MULTIPLY:
        {
            answer = (a * b) & mask;
            const unsigned __int128 limit = (sign - 1) + (an != bn);
            fault = fold.is_signed && bm != 0 && am > limit / bm ? CHECK_FAULT_SIGNED_OVERFLOW : CHECK_FAULT_NONE;
        } break;
        case CHECK_OP_DIVIDE:
        case CHECK_OP_REMAINDER:
        {
            if (b == 0)
            {
                fault = CHECK_FAULT_DIVIDE_BY_ZERO;
            }
            else if (fold.is_signed && a == sign && b == mask)
            {
                fault = CHECK_FAULT_SIGNED_OVERFLOW;
            }
            else
            {
                const unsigned __int128 magnitude = fold.op == CHECK_OP_DIVIDE ? am / bm : am % bm;
                const bool negative = fold.op == CHECK_OP_DIVIDE ? an != bn : an;
                answer = (negative ? 0 - magnitude : magnitude) & mask;
            }
        } break;
        case CHECK_OP_SHIFT_LEFT:
        case CHECK_OP_SHIFT_RIGHT:
        {
            if (bn || b >= fold.width)
            {
                fault = CHECK_FAULT_SHIFT_COUNT;
            }
            else
            {
                const uint32_t shift = (uint32_t)b;
                if (fold.op == CHECK_OP_SHIFT_LEFT)
                {
                    answer = (a << shift) & mask;
                    // Clang's C constant evaluator admits a nonnegative value shifted into the sign bit, but
                    // diagnoses discarded bits and negative operands, including in its C17/C23 pedantic modes.
                    fault = fold.is_signed && (an || a > mask >> shift) ? CHECK_FAULT_SHIFT_VALUE : CHECK_FAULT_NONE;
                }
                else
                {
                    answer = a >> shift;
                    if (an)
                    {
                        answer |= mask ^ (mask >> shift);
                    }
                }
            }
        } break;
        case CHECK_OP_LESS: answer = an != bn ? an : a < b; break;
        case CHECK_OP_LESS_EQUAL: answer = an != bn ? an : a <= b; break;
        case CHECK_OP_GREATER: answer = an != bn ? bn : a > b; break;
        case CHECK_OP_GREATER_EQUAL: answer = an != bn ? bn : a >= b; break;
        case CHECK_OP_EQUAL: answer = a == b; break;
        case CHECK_OP_NOT_EQUAL: answer = a != b; break;
        case CHECK_OP_BIT_AND: answer = a & b; break;
        case CHECK_OP_BIT_OR: answer = a | b; break;
        case CHECK_OP_BIT_XOR: answer = a ^ b; break;
        default: fault = CHECK_FAULT_UNSUPPORTED_CONSTANT; break;
    }
    return check_action_fold_integer_result(fold, value, answer, fault);
}

static void check_action_fold_floating(CheckOp op, const CheckValue* a, const CheckValue* b, CheckValue* value, uint32_t node)
{
    long double number = 0;
    bool integer = op >= CHECK_OP_LESS && op <= CHECK_OP_NOT_EQUAL;
    bool supported = true;
    // Each operation is performed in its target format. Computing a double division as long double then rounding
    // can double round; keeping the original format here also retains signed zero and IEEE unordered comparisons.
#define BUSTER_CHECK_REAL_OPERATION(T) \
    do \
    { \
        const T left = (T)a->floating; \
        const T right = (T)b->floating; \
        switch (op) \
        { \
            case CHECK_OP_POSITIVE: number = left; break; \
            case CHECK_OP_NEGATIVE: number = -left; break; \
            case CHECK_OP_ADD: number = (T)(left + right); break; \
            case CHECK_OP_SUBTRACT: number = (T)(left - right); break; \
            case CHECK_OP_MULTIPLY: number = (T)(left * right); break; \
            case CHECK_OP_DIVIDE: number = (T)(left / right); break; \
            case CHECK_OP_LESS: number = left < right; break; \
            case CHECK_OP_LESS_EQUAL: number = left <= right; break; \
            case CHECK_OP_GREATER: number = left > right; break; \
            case CHECK_OP_GREATER_EQUAL: number = left >= right; break; \
            case CHECK_OP_EQUAL: number = left == right; break; \
            case CHECK_OP_NOT_EQUAL: number = left != right; break; \
            default: supported = false; break; \
        } \
    } while (0)
    if (a->type == BUSTER_TYPE_FLOAT)
    {
        BUSTER_CHECK_REAL_OPERATION(float);
    }
    else if (a->type == BUSTER_TYPE_DOUBLE)
    {
        BUSTER_CHECK_REAL_OPERATION(double);
    }
    else if (a->type == BUSTER_TYPE_LONG_DOUBLE)
    {
        BUSTER_CHECK_REAL_OPERATION(long double);
    }
    else
    {
        supported = false;
    }
#undef BUSTER_CHECK_REAL_OPERATION
    if (!supported)
    {
        check_action_set_fault(value, CHECK_FAULT_UNSUPPORTED_CONSTANT, node);
    }
    else if (integer)
    {
        value->constant_kind = CHECK_CONSTANT_INTEGER;
        value->integer = number != 0;
    }
    else
    {
        value->constant_kind = CHECK_CONSTANT_FLOATING;
        value->floating = number;
    }
}

static bool check_action_foldable(const CheckValue* value)
{
    return value->fault == CHECK_FAULT_NONE && value->constant_kind != CHECK_CONSTANT_NONE;
}

static bool check_action_fold_symbolic(const TypeTable* table, CheckOp op, const CheckValue* left, const CheckValue* right, CheckValue* result)
{
    const bool reverse = op == CHECK_OP_ADD && right->constant_kind == CHECK_CONSTANT_ADDRESS;
    const CheckValue* address = reverse ? right : left;
    const CheckValue* delta = reverse ? left : right;
    const bool identity = op == CHECK_OP_POSITIVE;
    uint32_t width = 0;
    bool sign = false;
    const bool valid = check_action_integer_type(table, result->type, &width, &sign) && width == 64 &&
                       address->constant_kind == CHECK_CONSTANT_ADDRESS && address->object != 0 &&
                       (address->flags & BUSTER_CHECK_STATIC_OBJECT) != 0 &&
                       (identity || ((op == CHECK_OP_ADD || op == CHECK_OP_SUBTRACT) && delta->constant_kind == CHECK_CONSTANT_INTEGER));
    if (valid)
    {
        // A 64-bit integer relocation retains an object coefficient of one. Addends are target address bits;
        // unsigned host arithmetic preserves modulo-64 behavior without overflowing a signed host expression.
        const uint64_t amount = identity ? 0 : (uint64_t)delta->integer;
        const uint64_t offset = (uint64_t)address->offset;
        result->constant_kind = CHECK_CONSTANT_ADDRESS;
        result->object = address->object;
        result->offset = (int64_t)(op == CHECK_OP_SUBTRACT ? offset - amount : offset + amount);
        result->flags |= BUSTER_CHECK_STATIC_OBJECT;
        result->flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM);
    }
    return valid;
}

static void check_action_arithmetic(TypeTable* table, const CheckAction* action, CheckActionResult* result, TypeId common_hint, CheckIntegerFold* pending)
{
    const CheckOp op = action->op;
    const bool unary = op == CHECK_OP_POSITIVE || op == CHECK_OP_NEGATIVE || op == CHECK_OP_BIT_NOT;
    const bool shifts = op == CHECK_OP_SHIFT_LEFT || op == CHECK_OP_SHIFT_RIGHT;
    const bool bits = op == CHECK_OP_BIT_AND || op == CHECK_OP_BIT_OR || op == CHECK_OP_BIT_XOR || op == CHECK_OP_REMAINDER || op == CHECK_OP_BIT_NOT || shifts;
    const bool comparison = op >= CHECK_OP_LESS && op <= CHECK_OP_NOT_EQUAL;
    CheckValue left = check_action_operand(table, action, result, 0);
    CheckValue right = unary ? left : check_action_operand(table, action, result, 1);
    const bool left_ok = bits ? check_action_is_integer(table, left.type) : check_action_is_arithmetic(table, left.type);
    const bool right_ok = bits ? check_action_is_integer(table, right.type) : check_action_is_arithmetic(table, right.type);
    if (!left_ok || !right_ok)
    {
        check_action_diagnose(result, bits ? CHECK_DIAG_INTEGER_REQUIRED : CHECK_DIAG_ARITHMETIC_REQUIRED, left_ok ? 1 : 0);
    }
    else
    {
        check_action_promote(table, &left, &result->conversions[0], action->node);
        if (!unary)
        {
            check_action_promote(table, &right, &result->conversions[1], action->node);
        }
        TypeId common = left.type;
        if (!unary && !shifts)
        {
            common = common_hint != 0 ? common_hint : type_usual_arithmetic(table, left.type, right.type);
            if (common == 0)
            {
                check_action_diagnose(result, table->last_error == TYPE_ERROR_FULL ? CHECK_DIAG_RESOURCE : CHECK_DIAG_ARITHMETIC_REQUIRED, 0);
            }
            else
            {
                if (left.type != common)
                {
                    check_action_conversion(table, &left, common, BUSTER_CHECK_CONVERT_ARITHMETIC, &result->conversions[0], action->node);
                }
                if (right.type != common)
                {
                    check_action_conversion(table, &right, common, BUSTER_CHECK_CONVERT_ARITHMETIC, &result->conversions[1], action->node);
                }
            }
        }
        if (common != 0)
        {
            const uint8_t kind = check_action_kind(table, common);
            if (comparison && op <= CHECK_OP_GREATER_EQUAL && kind == BUSTER_TYPE_KIND_COMPLEX)
            {
                check_action_diagnose(result, CHECK_DIAG_ARITHMETIC_REQUIRED, 0);
            }
            result->value.type = comparison ? BUSTER_TYPE_INT : common;
            result->value.category = CHECK_RVALUE;
            check_action_inherit(&result->value, &left);
            if (!unary)
            {
                check_action_inherit(&result->value, &right);
            }
            const bool ice = (left.flags & BUSTER_CHECK_ICE) != 0 && (unary || (right.flags & BUSTER_CHECK_ICE) != 0);
            result->value.flags |= ice && check_action_is_integer(table, result->value.type) ? BUSTER_CHECK_ICE : 0;
            const uint32_t ice_forms = BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM;
            const bool gnu_ice = !ice && (left.flags & ice_forms) != 0 && (unary || (right.flags & ice_forms) != 0);
            result->value.flags |= gnu_ice && check_action_is_integer(table, result->value.type) ? BUSTER_CHECK_GNU_ICE_FORM : 0;
            if (result->diagnostic == CHECK_DIAG_NONE && check_action_foldable(&left) && (unary || check_action_foldable(&right)))
            {
                uint32_t width = 0;
                uint32_t right_width = 0;
                bool is_signed = false;
                bool right_signed = false;
                if (left.constant_kind == CHECK_CONSTANT_INTEGER && (unary || right.constant_kind == CHECK_CONSTANT_INTEGER) && check_action_integer_type(table, left.type, &width, &is_signed) && check_action_integer_type(table, unary ? left.type : right.type, &right_width, &right_signed))
                {
                    const CheckIntegerFold fold =
                    {
                        .active = true,
                        .op = op,
                        .left = left.integer,
                        .right = unary ? 0 : right.integer,
                        .width = width,
                        .right_width = right_width,
                        .is_signed = is_signed,
                        .right_signed = right_signed,
                        .node = action->node,
                        .extensions = action->extensions,
                    };
                    if (pending != NULL && width <= 64 && right_width <= 64 && op != CHECK_OP_DIVIDE && op != CHECK_OP_REMAINDER && op != CHECK_OP_MULTIPLY)
                    {
                        *pending = fold;
                    }
                    else
                    {
                        result->warnings |= check_action_fold_integer(fold, &result->value);
                    }
                }
                else if (left.constant_kind == CHECK_CONSTANT_FLOATING && (unary || right.constant_kind == CHECK_CONSTANT_FLOATING))
                {
                    check_action_fold_floating(op, &left, &right, &result->value, action->node);
                }
                else if (left.constant_kind == CHECK_CONSTANT_COMPLEX && (unary || right.constant_kind == CHECK_CONSTANT_COMPLEX))
                {
                    result->warnings |= check_complex_fold(table, op, &left, &right, &result->value, action->node, action->extensions);
                }
                else if (!check_action_fold_symbolic(table, op, &left, &right, &result->value))
                {
                    check_action_set_fault(&result->value, CHECK_FAULT_UNSUPPORTED_CONSTANT, action->node);
                }
            }
        }
    }
}

static void check_action_logic(TypeTable* table, const CheckAction* action, CheckActionResult* result)
{
    const bool unary = action->op == CHECK_OP_NOT || action->op == CHECK_OP_CONDITION;
    const CheckValue left = check_action_operand(table, action, result, 0);
    const CheckValue right = unary ? left : check_action_operand(table, action, result, 1);
    if (!check_action_is_scalar(table, left.type) || (!unary && !check_action_is_scalar(table, right.type)))
    {
        check_action_diagnose(result, CHECK_DIAG_SCALAR_REQUIRED, check_action_is_scalar(table, left.type) ? 1 : 0);
    }
    result->value.type = BUSTER_TYPE_INT;
    bool left_truth = false;
    bool right_truth = false;
    const bool left_known = check_action_truth(table, &left, &left_truth);
    const bool right_known = unary ? false : check_action_truth(table, &right, &right_truth);
    const bool skips_right = !unary && left_known && (action->op == CHECK_OP_LOGICAL_AND ? !left_truth : left_truth);
    check_action_inherit(&result->value, &left);
    if (!unary && !skips_right)
    {
        check_action_inherit(&result->value, &right);
    }
    const bool right_ice = unary || (right.flags & BUSTER_CHECK_ICE) != 0 ||
                           (skips_right && (right.flags & (BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_GNU_ICE_FORM)) != 0);
    const bool ice = (left.flags & BUSTER_CHECK_ICE) != 0 && right_ice;
    const uint32_t forms = BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM;
    const bool right_form = unary || (right.flags & forms) != 0 || (skips_right && (right.flags & BUSTER_CHECK_ICE_UNEVALUATED) != 0);
    result->value.flags |= ice ? BUSTER_CHECK_ICE : ((left.flags & forms) != 0 && right_form ? BUSTER_CHECK_GNU_ICE_FORM : 0);
    if (left_known && (unary || skips_right || right_known) && result->diagnostic == CHECK_DIAG_NONE)
    {
        result->value.constant_kind = CHECK_CONSTANT_INTEGER;
        result->value.integer = unary ? (action->op == CHECK_OP_NOT ? !left_truth : left_truth) : (action->op == CHECK_OP_LOGICAL_AND ? left_truth && right_truth : left_truth || right_truth);
    }
}

// The common type of two pointer operands (C 6.5.15p6): the other operand's for a null pointer constant, void with
// both pointees' qualifiers for void and an object type, else the composite of compatible pointees with both
// qualifiers. 0 for other pointees (void and a function among them), which each caller takes as clang does.
static TypeId check_action_pointer_common(TypeTable* table, const CheckValue* left, const CheckValue* right, const CheckAction* action, CheckActionResult* result)
{
    TypeId common = 0;
    const bool lp = check_action_kind(table, left->type) == BUSTER_TYPE_KIND_POINTER;
    const bool rp = check_action_kind(table, right->type) == BUSTER_TYPE_KIND_POINTER;
    if (lp && check_action_null(table, right, action->extensions))
    {
        common = left->type;
    }
    else if (rp && check_action_null(table, left, action->extensions))
    {
        common = right->type;
    }
    else if (lp && rp)
    {
        const TypeId l = type_element(table, left->type);
        const TypeId r = type_element(table, right->type);
        const uint8_t lk = check_action_kind(table, l);
        const uint8_t rk = check_action_kind(table, r);
        const uint32_t qualifiers = type_qualifiers(l) | type_qualifiers(r);
        TypeId pointee = 0;
        if ((lk == BUSTER_TYPE_KIND_VOID && rk != BUSTER_TYPE_KIND_FUNCTION) || (rk == BUSTER_TYPE_KIND_VOID && lk != BUSTER_TYPE_KIND_FUNCTION))
        {
            pointee = BUSTER_TYPE_VOID;
        }
        else if (check_action_compatible_at(table, type_unqualified(l), type_unqualified(r), action, result))
        {
            pointee = type_composite_at(table, type_unqualified(l), type_unqualified(r), action->transparent_unions, action->tag_completion_count, action->source_position);
            if (pointee == 0 && table->last_error == TYPE_ERROR_FULL)
            {
                check_action_diagnose(result, CHECK_DIAG_RESOURCE, 0);
            }
        }
        if (pointee != 0)
        {
            common = type_pointer(table, type_qualify(table, pointee, qualifiers));
            if (common == 0)
            {
                check_action_diagnose(result, CHECK_DIAG_RESOURCE, 0);
            }
        }
    }
    return common;
}

static void check_action_conditional(TypeTable* table, const CheckAction* action, CheckActionResult* result)
{
    const CheckValue condition = check_action_operand(table, action, result, 0);
    CheckValue left = check_action_operand(table, action, result, 1);
    CheckValue right = check_action_operand(table, action, result, 2);
    TypeId common = 0;
    if (!check_action_is_scalar(table, condition.type))
    {
        check_action_diagnose(result, CHECK_DIAG_SCALAR_REQUIRED, 0);
    }
    if (check_action_is_arithmetic(table, left.type) && check_action_is_arithmetic(table, right.type))
    {
        check_action_promote(table, &left, &result->conversions[1], action->node);
        check_action_promote(table, &right, &result->conversions[2], action->node);
        common = type_usual_arithmetic(table, left.type, right.type);
    }
    else if (check_action_kind(table, left.type) == BUSTER_TYPE_KIND_VOID || check_action_kind(table, right.type) == BUSTER_TYPE_KIND_VOID)
    {
        common = BUSTER_TYPE_VOID;
    }
    else if ((check_action_kind(table, left.type) == BUSTER_TYPE_KIND_STRUCT || check_action_kind(table, left.type) == BUSTER_TYPE_KIND_UNION || check_action_kind(table, left.type) == BUSTER_TYPE_KIND_NULLPTR) && check_action_compatible_at(table, left.type, right.type, action, result))
    {
        common = left.type;
    }
    else
    {
        // GCC's softening, which clang keeps with a warning each: pointers to incompatible types (void and a
        // function among them) make void *, unqualified, and a pointer and an integer (not a null pointer constant,
        // which the common type takes) the pointer's type.
        common = check_action_pointer_common(table, &left, &right, action, result);
        const bool lp = check_action_kind(table, left.type) == BUSTER_TYPE_KIND_POINTER;
        const bool rp = check_action_kind(table, right.type) == BUSTER_TYPE_KIND_POINTER;
        if (common == 0 && result->diagnostic == CHECK_DIAG_NONE && lp && rp)
        {
            common = type_pointer(table, BUSTER_TYPE_VOID);
            if (common == 0)
            {
                check_action_diagnose(result, CHECK_DIAG_RESOURCE, 0);
            }
            result->warnings |= CHECK_WARNING_CONDITIONAL_POINTER_TYPES;
        }
        else if (common == 0 && result->diagnostic == CHECK_DIAG_NONE &&
                 ((lp && check_action_is_integer(table, right.type)) || (rp && check_action_is_integer(table, left.type))))
        {
            common = lp ? left.type : right.type;
            result->warnings |= CHECK_WARNING_CONDITIONAL_POINTER_INTEGER;
        }
    }
    if (common == 0)
    {
        check_action_diagnose(result, CHECK_DIAG_INCOMPATIBLE, 1);
    }
    else
    {
        check_action_conversion(table, &left, common, left.type == common ? 0 : BUSTER_CHECK_CONVERT_ARITHMETIC, &result->conversions[1], action->node);
        check_action_conversion(table, &right, common, right.type == common ? 0 : BUSTER_CHECK_CONVERT_ARITHMETIC, &result->conversions[2], action->node);
        bool truth = false;
        const bool known = check_action_truth(table, &condition, &truth);
        if (known)
        {
            result->value = truth ? left : right;
        }
        else
        {
            result->value.type = common;
            check_action_inherit(&result->value, &left);
            check_action_inherit(&result->value, &right);
        }
        result->value.category = CHECK_RVALUE;
        result->value.flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_BIT_FIELD | BUSTER_CHECK_REGISTER);
        const bool left_ice = (left.flags & BUSTER_CHECK_ICE) != 0 ||
                              (known && !truth && (left.flags & (BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_GNU_ICE_FORM)) != 0);
        const bool right_ice = (right.flags & BUSTER_CHECK_ICE) != 0 ||
                               (known && truth && (right.flags & (BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_GNU_ICE_FORM)) != 0);
        const bool ice = (condition.flags & BUSTER_CHECK_ICE) != 0 && left_ice && right_ice;
        const uint32_t forms = BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM;
        const bool left_form = (left.flags & forms) != 0 || (known && !truth && (left.flags & BUSTER_CHECK_ICE_UNEVALUATED) != 0);
        const bool right_form = (right.flags & forms) != 0 || (known && truth && (right.flags & BUSTER_CHECK_ICE_UNEVALUATED) != 0);
        const bool gnu_ice = (condition.flags & forms) != 0 && left_form && right_form;
        result->value.flags |= check_action_is_integer(table, common) ? (ice ? BUSTER_CHECK_ICE : (gnu_ice ? BUSTER_CHECK_GNU_ICE_FORM : 0)) : 0;
        check_action_inherit(&result->value, &condition);
    }
}

static bool check_action_object_pointer(const TypeTable* table, TypeId pointer, const CheckAction* action, CheckActionResult* result)
{
    bool valid = check_action_kind(table, pointer) == BUSTER_TYPE_KIND_POINTER;
    if (valid)
    {
        const TypeId element = type_element(table, type_strip_aligned(table, pointer));
        const uint8_t kind = check_action_kind(table, element);
        const bool gnu = kind == BUSTER_TYPE_KIND_VOID || (kind == BUSTER_TYPE_KIND_FUNCTION && action->op != CHECK_OP_SUBSCRIPT);
        valid = gnu || (kind != BUSTER_TYPE_KIND_VOID && kind != BUSTER_TYPE_KIND_FUNCTION && check_action_source_complete(table, element, action));
        if (!valid)
        {
            check_action_diagnose(result, CHECK_DIAG_INCOMPLETE, 0);
        }
    }
    else
    {
        check_action_diagnose(result, CHECK_DIAG_POINTER_REQUIRED, 0);
    }
    return valid;
}

static uint64_t check_action_pointer_stride(const TypeTable* table, TypeId pointer, const CheckAction* action)
{
    const TypeId element = type_element(table, type_strip_aligned(table, pointer));
    const uint8_t kind = check_action_kind(table, element);
    const bool gnu_byte = kind == BUSTER_TYPE_KIND_VOID || kind == BUSTER_TYPE_KIND_FUNCTION;
    return gnu_byte ? 1 : type_size(table, element);
}

static bool check_action_i64(const TypeTable* table, const CheckValue* value, int64_t* number)
{
    uint32_t width = 0;
    bool is_signed = false;
    bool fits = value->constant_kind == CHECK_CONSTANT_INTEGER && value->fault == CHECK_FAULT_NONE && check_action_integer_type(table, value->type, &width, &is_signed);
    *number = 0;
    if (fits)
    {
        const unsigned __int128 mask = check_action_mask(width);
        const unsigned __int128 bits = value->integer & mask;
        const bool negative = is_signed && (bits >> (width - 1)) != 0;
        const unsigned __int128 magnitude = negative ? (0 - bits) & mask : bits;
        fits = magnitude <= (unsigned __int128)INT64_MAX + negative;
        if (fits)
        {
            *number = negative ? (magnitude == ((unsigned __int128)1 << 63) ? INT64_MIN : -(int64_t)magnitude) : (int64_t)magnitude;
        }
    }
    return fits;
}

static void check_action_pointer_arithmetic(TypeTable* table, const CheckAction* action, CheckActionResult* result)
{
    CheckValue left = check_action_operand(table, action, result, 0);
    CheckValue right = check_action_operand(table, action, result, 1);
    const bool lp = check_action_kind(table, left.type) == BUSTER_TYPE_KIND_POINTER;
    const bool rp = check_action_kind(table, right.type) == BUSTER_TYPE_KIND_POINTER;
    check_action_inherit(&result->value, &left);
    check_action_inherit(&result->value, &right);
    if (action->op == CHECK_OP_SUBTRACT && lp && rp)
    {
        const TypeId le = type_element(table, left.type);
        const TypeId re = type_element(table, right.type);
        const bool complete = check_action_object_pointer(table, left.type, action, result) && check_action_object_pointer(table, right.type, action, result);
        if (!check_action_compatible_at(table, type_unqualified(le), type_unqualified(re), action, result))
        {
            check_action_diagnose(result, CHECK_DIAG_INCOMPATIBLE, 1);
        }
        result->value.type = BUSTER_TYPE_LONG;
        if (complete && result->diagnostic == CHECK_DIAG_NONE && check_action_foldable(&left) && check_action_foldable(&right) && left.constant_kind == CHECK_CONSTANT_ADDRESS && right.constant_kind == CHECK_CONSTANT_ADDRESS && left.object != 0 && left.object == right.object)
        {
            const uint64_t stride = check_action_pointer_stride(table, left.type, action);
            int64_t difference = 0;
            if (stride != 0 && stride <= INT64_MAX && !__builtin_sub_overflow(left.offset, right.offset, &difference) && difference % (int64_t)stride == 0)
            {
                result->value.constant_kind = CHECK_CONSTANT_INTEGER;
                result->value.integer = (uint64_t)(difference / (int64_t)stride);
            }
        }
    }
    else
    {
        const bool reverse = !lp && rp && action->op != CHECK_OP_SUBTRACT;
        CheckValue pointer = reverse ? right : left;
        CheckValue index = reverse ? left : right;
        const uint32_t index_slot = reverse ? 0 : 1;
        if (!check_action_is_integer(table, index.type))
        {
            check_action_diagnose(result, CHECK_DIAG_INTEGER_REQUIRED, index_slot);
        }
        if (check_action_object_pointer(table, pointer.type, action, result) && result->diagnostic == CHECK_DIAG_NONE)
        {
            check_action_promote(table, &index, &result->conversions[index_slot], action->node);
            result->value.type = action->op == CHECK_OP_SUBSCRIPT ? type_element(table, pointer.type) : pointer.type;
            result->value.category = action->op == CHECK_OP_SUBSCRIPT ? CHECK_LVALUE : CHECK_RVALUE;
            result->value.flags |= pointer.flags & BUSTER_CHECK_STATIC_OBJECT;
            int64_t number = 0;
            const uint64_t stride = check_action_pointer_stride(table, pointer.type, action);
            int64_t delta = 0;
            int64_t addend = 0;
            if (!check_action_runtime_size(table, type_element(table, pointer.type)) && pointer.constant_kind == CHECK_CONSTANT_ADDRESS &&
                pointer.fault == CHECK_FAULT_NONE && stride <= INT64_MAX && check_action_i64(table, &index, &number) &&
                !__builtin_mul_overflow(number, (int64_t)stride, &delta))
            {
                const bool fits = action->op == CHECK_OP_SUBTRACT ? !__builtin_sub_overflow(pointer.offset, delta, &addend) : !__builtin_add_overflow(pointer.offset, delta, &addend);
                if (fits)
                {
                    result->value.constant_kind = CHECK_CONSTANT_ADDRESS;
                    result->value.object = pointer.object;
                    result->value.offset = addend;
                }
            }
        }
    }
}

// Comparisons as clang's CheckCompareOperands has them in C, in every language mode: two pointers always compare,
// with a warning for incompatible pointees (but for void and any pointee under equality), the right converted to the
// left's type (the left to the right's when only the left is a null pointer constant). A pointer and an integer
// compare as two pointers, with a warning under equality unless the integer is a null pointer constant; clang's
// warning for a relational comparison with a nonzero integer is in no group, and so has no record here.
static void check_action_pointer_compare(TypeTable* table, const CheckAction* action, CheckActionResult* result)
{
    CheckValue left = check_action_operand(table, action, result, 0);
    CheckValue right = check_action_operand(table, action, result, 1);
    const bool equality = action->op == CHECK_OP_EQUAL || action->op == CHECK_OP_NOT_EQUAL;
    const uint8_t lk = check_action_kind(table, left.type);
    const uint8_t rk = check_action_kind(table, right.type);
    const bool lp = lk == BUSTER_TYPE_KIND_POINTER;
    const bool rp = rk == BUSTER_TYPE_KIND_POINTER;
    const bool left_null = check_action_null(table, &left, action->extensions);
    const bool right_null = check_action_null(table, &right, action->extensions);
    TypeId common = 0;
    if (equality)
    {
        common = check_action_pointer_common(table, &left, &right, action, result);
        if ((lk == BUSTER_TYPE_KIND_NULLPTR && right_null) || (rk == BUSTER_TYPE_KIND_NULLPTR && left_null))
        {
            common = BUSTER_TYPE_NULLPTR;
        }
    }
    if (common == 0 && result->diagnostic == CHECK_DIAG_NONE && lp && rp)
    {
        const TypeId le = type_element(table, left.type);
        const TypeId re = type_element(table, right.type);
        const bool void_pointee = check_action_kind(table, le) == BUSTER_TYPE_KIND_VOID || check_action_kind(table, re) == BUSTER_TYPE_KIND_VOID;
        if (!check_action_compatible_at(table, type_unqualified(le), type_unqualified(re), action, result) && !(equality && void_pointee))
        {
            result->warnings |= CHECK_WARNING_DISTINCT_POINTER_COMPARE;
        }
        common = left_null && !right_null ? right.type : left.type;
    }
    else if (common == 0 && result->diagnostic == CHECK_DIAG_NONE &&
             ((lp && check_action_is_integer(table, right.type)) || (rp && check_action_is_integer(table, left.type))))
    {
        common = lp ? left.type : right.type;
        result->warnings |= equality && !(lp ? right_null : left_null) ? CHECK_WARNING_POINTER_INTEGER_COMPARE : 0;
    }
    if (common == 0)
    {
        check_action_diagnose(result, CHECK_DIAG_INCOMPATIBLE, 1);
    }
    else
    {
        const uint32_t left_flags = left.type == common ? 0 : check_action_is_integer(table, left.type) && !left_null ? BUSTER_CHECK_CONVERT_CAST : BUSTER_CHECK_CONVERT_NULL;
        const uint32_t right_flags = right.type == common ? 0 : check_action_is_integer(table, right.type) && !right_null ? BUSTER_CHECK_CONVERT_CAST : BUSTER_CHECK_CONVERT_NULL;
        check_action_conversion(table, &left, common, left_flags, &result->conversions[0], action->node);
        check_action_conversion(table, &right, common, right_flags, &result->conversions[1], action->node);
        result->value.type = BUSTER_TYPE_INT;
        check_action_inherit(&result->value, &left);
        check_action_inherit(&result->value, &right);
        // Different objects can have equal one-past/beginning addresses. Leave their comparison to emitted code.
        if (left.constant_kind == CHECK_CONSTANT_ADDRESS && right.constant_kind == CHECK_CONSTANT_ADDRESS && left.object == right.object && (equality || left.object != 0) && result->value.fault == CHECK_FAULT_NONE)
        {
            bool answer = false;
            switch (action->op)
            {
                case CHECK_OP_LESS: answer = left.offset < right.offset; break;
                case CHECK_OP_LESS_EQUAL: answer = left.offset <= right.offset; break;
                case CHECK_OP_GREATER: answer = left.offset > right.offset; break;
                case CHECK_OP_GREATER_EQUAL: answer = left.offset >= right.offset; break;
                case CHECK_OP_EQUAL: answer = left.offset == right.offset; break;
                case CHECK_OP_NOT_EQUAL: answer = left.offset != right.offset; break;
                default: break;
            }
            result->value.constant_kind = CHECK_CONSTANT_INTEGER;
            result->value.integer = answer;
        }
    }
}

// The member of a record that a name finds, through its anonymous members, with its bit offset and the qualifiers it
// inherits: by the records' lookups (type_lookup_member), or on the scalar path by its exact twin. The usual scan/index
// paths avoid a whole-table clear and a linear member scan; collision chains, anonymous depth and the capacity fallback
// still determine their worst-case work (#591). Every member access runs it, inlined into check_action_member;
// the atomic alignment walk calls its copy (check_action_member_lookup), so that a second caller doesn't take it out of
// check_action_member's code.
static inline __attribute__((always_inline)) uint32_t check_action_find_member(const TypeTable* table, TypeId record, uint32_t name, uint64_t* offset,
                                                                                uint32_t* qualifiers, bool scalar, CheckActionResult* result)
{
    uint32_t member = scalar ? type_lookup_member_scalar(table, record, name, offset, qualifiers) : type_lookup_member(table, record, name, offset, qualifiers);
    if (member == BUSTER_TYPE_MEMBER_UNKNOWN)
    {
        check_action_diagnose(result, CHECK_DIAG_RESOURCE, 0);
        member = 0;
    }
    return member;
}

static __attribute__((noinline)) uint32_t check_action_member_lookup(const TypeTable* table, TypeId record, uint32_t name, uint64_t* offset, uint32_t* qualifiers,
                                                                      CheckActionResult* result)
{
    return check_action_find_member(table, record, name, offset, qualifiers, false, result);
}

static void check_action_member(TypeTable* table, const CheckAction* action, CheckActionResult* result)
{
    const bool arrow = action->op == CHECK_OP_ARROW;
    CheckValue base = arrow ? check_action_operand(table, action, result, 0) : action->operands[0];
    TypeId record = type_strip_aligned(table, base.type);
    if (arrow)
    {
        if (type_kind(table, record) != BUSTER_TYPE_KIND_POINTER)
        {
            check_action_diagnose(result, CHECK_DIAG_POINTER_REQUIRED, 0);
        }
        else
        {
            record = type_strip_aligned(table, type_element(table, record));
        }
    }
    const uint8_t kind = type_kind(table, record);
    if (result->diagnostic == CHECK_DIAG_NONE && kind != BUSTER_TYPE_KIND_STRUCT && kind != BUSTER_TYPE_KIND_UNION)
    {
        check_action_diagnose(result, CHECK_DIAG_MEMBER, 0);
    }
    else if (result->diagnostic == CHECK_DIAG_NONE && !check_action_source_complete(table, record, action))
    {
        check_action_diagnose(result, CHECK_DIAG_INCOMPLETE, 0);
    }
    if (result->diagnostic == CHECK_DIAG_NONE)
    {
        uint64_t offset = 0;
        uint32_t qualifiers = 0;
        const uint32_t member = check_action_find_member(table, record, action->member, &offset, &qualifiers, action->scalar, result);
        if (member == 0)
        {
            check_action_diagnose(result, CHECK_DIAG_MEMBER, 0);
        }
        else
        {
            result->member_offset = offset;
            result->value.type = type_qualify(table, table->members.type[member], qualifiers);
            result->value.category = arrow || base.category == CHECK_LVALUE ? CHECK_LVALUE : CHECK_RVALUE;
            check_action_inherit(&result->value, &base);
            result->value.flags |= base.flags & BUSTER_CHECK_STATIC_OBJECT;
            if ((table->members.flags[member] & BUSTER_TYPE_MEMBER_BIT_FIELD) != 0)
            {
                result->value.flags |= BUSTER_CHECK_BIT_FIELD;
                result->value.bit_width = table->members.bit_width[member];
            }
            if (!arrow)
            {
                result->value.flags |= base.flags & BUSTER_CHECK_REGISTER;
            }
            if (base.constant_kind == CHECK_CONSTANT_ADDRESS && offset % 8 == 0 && offset / 8 <= INT64_MAX)
            {
                int64_t addend = 0;
                if (!__builtin_add_overflow(base.offset, (int64_t)(offset / 8), &addend))
                {
                    result->value.constant_kind = CHECK_CONSTANT_ADDRESS;
                    result->value.object = base.object;
                    result->value.offset = addend;
                }
            }
        }
    }
}

// Whether no member of a record, nor of the records and arrays it holds, is const. Each type is entered once: a sparse set
// over type indices that nothing clears (as type_lookup_walk's), `entered` listing them and `places` giving each its place
// there, so that a check costs the types it reaches, not a byte cleared per type of the table (#591).
static bool check_action_record_modifiable(const TypeTable* table, TypeId record, CheckActionResult* result)
{
    Scratch scratch = scratch_begin(NULL);
    const uint32_t count = table->types.count;
    TypeId* stack = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, TypeId, count);
    uint32_t* entered = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, count);
    uint32_t* places = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, count);
    bool valid = stack != NULL && entered != NULL && places != NULL;
    uint32_t depth = 0;
    uint32_t seen = 0;
    if (!valid)
    {
        check_action_diagnose(result, CHECK_DIAG_RESOURCE, 0);
    }
    else
    {
        const uint32_t index = type_index(type_strip_aligned(table, record));
        places[index] = seen;
        entered[seen++] = index;
        stack[depth++] = record;
    }
    while (valid && depth != 0)
    {
        const TypeId next = type_strip_aligned(table, stack[--depth]);
        const uint32_t index = type_index(next);
        const uint8_t kind = type_kind(table, next);
        if ((type_qualifiers(next) & BUSTER_TYPE_CONST) != 0)
        {
            valid = false;
        }
        else if (type_kind_is_array(kind))
        {
            const TypeId element = type_strip_aligned(table, type_element(table, next));
            const uint32_t element_index = type_index(element);
            valid = (type_qualifiers(element) & BUSTER_TYPE_CONST) == 0;
            if (valid && !(places[element_index] < seen && entered[places[element_index]] == element_index))
            {
                places[element_index] = seen;
                entered[seen++] = element_index;
                stack[depth++] = element;
            }
        }
        else if (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION)
        {
            const uint32_t tag = table->types.index[index];
            const uint32_t begin = table->tags.member_begin[tag];
            const uint32_t members = table->tags.member_count[tag];
            for (uint32_t i = 0; i < members && valid; i += 1)
            {
                const TypeId member = type_strip_aligned(table, table->members.type[begin + i]);
                const uint32_t member_index = type_index(member);
                valid = (type_qualifiers(member) & BUSTER_TYPE_CONST) == 0;
                if (valid && !(places[member_index] < seen && entered[places[member_index]] == member_index))
                {
                    places[member_index] = seen;
                    entered[seen++] = member_index;
                    stack[depth++] = member;
                }
            }
        }
    }
    scratch_end(scratch);
    if (!valid && result->diagnostic == CHECK_DIAG_NONE)
    {
        check_action_diagnose(result, CHECK_DIAG_MODIFIABLE_REQUIRED, 0);
    }
    return valid;
}

static bool check_action_modifiable(const TypeTable* table, const CheckValue* value, const CheckAction* action, CheckActionResult* result)
{
    const uint8_t kind = check_action_kind(table, value->type);
    bool valid = value->category == CHECK_LVALUE;
    if (!valid)
    {
        check_action_diagnose(result, CHECK_DIAG_LVALUE_REQUIRED, 0);
    }
    else if ((value->flags & BUSTER_CHECK_VECTOR_DUPLICATE) != 0 || (type_qualifiers(value->type) & BUSTER_TYPE_CONST) != 0 || type_kind_is_array(kind) || kind == BUSTER_TYPE_KIND_VOID || !check_action_source_complete(table, value->type, action))
    {
        valid = false;
        check_action_diagnose(result, CHECK_DIAG_MODIFIABLE_REQUIRED, 0);
    }
    else if (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION)
    {
        valid = check_action_record_modifiable(table, value->type, result);
    }
    return valid;
}

static void check_action_bit_field_store(const TypeTable* table, const CheckValue* target, CheckValue* value)
{
    if ((target->flags & BUSTER_CHECK_BIT_FIELD) != 0)
    {
        value->flags |= BUSTER_CHECK_BIT_FIELD;
        value->bit_width = target->bit_width;
        uint32_t width = 0;
        bool is_signed = false;
        if (value->constant_kind == CHECK_CONSTANT_INTEGER && check_action_integer_type(table, value->type, &width, &is_signed) && target->bit_width != 0 && target->bit_width <= width)
        {
            const unsigned __int128 mask = check_action_mask(target->bit_width);
            unsigned __int128 bits = value->integer & mask;
            if (is_signed && (bits >> (target->bit_width - 1)) != 0)
            {
                bits |= ~mask;
            }
            value->integer = bits & check_action_mask(width);
        }
    }
}

static void check_action_assignment(TypeTable* table, const CheckAction* action, CheckActionResult* result)
{
    const bool store = action->op == CHECK_OP_ASSIGN;
    const uint32_t slot = store ? 1 : 0;
    CheckValue source = check_action_operand(table, action, result, slot);
    TypeId target = store ? action->operands[0].type : action->type;
    if (store)
    {
        check_action_modifiable(table, &action->operands[0], action, result);
    }
    if (type_is_valid(table, target))
    {
        target = check_action_kind(table, source.type) == BUSTER_TYPE_KIND_ATOMIC ? type_unqualified(type_strip_aligned(table, target))
                                                                               : type_conversion_operand(table, target);
        const bool void_return = action->op == CHECK_OP_CONVERT && check_action_kind(table, target) == BUSTER_TYPE_KIND_VOID &&
                                 (check_action_kind(table, source.type) == BUSTER_TYPE_KIND_VOID || (action->extensions & BUSTER_CHECK_EXTENSION_SYSTEM_HEADER) != 0);
        if (!void_return && !check_action_assignable(table, target, &source, action, result))
        {
            check_action_diagnose(result, CHECK_DIAG_INCOMPATIBLE, slot);
        }
        else
        {
            const uint32_t conversion = BUSTER_CHECK_CONVERT_ASSIGNMENT | (check_action_kind(table, target) == BUSTER_TYPE_KIND_POINTER && check_action_null(table, &source, action->extensions) ? BUSTER_CHECK_CONVERT_NULL : 0);
            check_action_conversion(table, &source, target, conversion, &result->conversions[slot], action->node);
            result->value = source;
            result->value.category = CHECK_RVALUE;
            if (store)
            {
                check_action_bit_field_store(table, &action->operands[0], &result->value);
                result->value.flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_ICE_UNEVALUATED);
                result->value.flags |= BUSTER_CHECK_SIDE_EFFECT;
                check_action_inherit(&result->value, &action->operands[0]);
            }
        }
    }
    else
    {
        check_action_diagnose(result, CHECK_DIAG_INVALID_TYPE, slot);
    }
}

static void check_action_update(TypeTable* table, const CheckAction* action, CheckActionResult* result)
{
    const CheckValue target = action->operands[0];
    const bool increment = action->op >= CHECK_OP_PRE_INCREMENT && action->op <= CHECK_OP_POST_DECREMENT;
    const bool postfix = action->op == CHECK_OP_POST_INCREMENT || action->op == CHECK_OP_POST_DECREMENT;
    const uint8_t kind = check_action_kind(table, type_conversion_operand(table, target.type));
    if (check_action_modifiable(table, &target, action, result))
    {
        if (increment && kind == BUSTER_TYPE_KIND_COMPLEX)
        {
            check_action_diagnose(result, CHECK_DIAG_ARITHMETIC_REQUIRED, 0);
        }
        else
        {
            CheckAction arithmetic = *action;
            switch (action->op)
            {
                case CHECK_OP_PRE_INCREMENT:
                case CHECK_OP_POST_INCREMENT:
                case CHECK_OP_ADD_ASSIGN: arithmetic.op = CHECK_OP_ADD; break;
                case CHECK_OP_PRE_DECREMENT:
                case CHECK_OP_POST_DECREMENT:
                case CHECK_OP_SUBTRACT_ASSIGN: arithmetic.op = CHECK_OP_SUBTRACT; break;
                case CHECK_OP_MULTIPLY_ASSIGN: arithmetic.op = CHECK_OP_MULTIPLY; break;
                case CHECK_OP_DIVIDE_ASSIGN: arithmetic.op = CHECK_OP_DIVIDE; break;
                case CHECK_OP_REMAINDER_ASSIGN: arithmetic.op = CHECK_OP_REMAINDER; break;
                case CHECK_OP_SHIFT_LEFT_ASSIGN: arithmetic.op = CHECK_OP_SHIFT_LEFT; break;
                case CHECK_OP_SHIFT_RIGHT_ASSIGN: arithmetic.op = CHECK_OP_SHIFT_RIGHT; break;
                case CHECK_OP_BIT_AND_ASSIGN: arithmetic.op = CHECK_OP_BIT_AND; break;
                case CHECK_OP_BIT_OR_ASSIGN: arithmetic.op = CHECK_OP_BIT_OR; break;
                case CHECK_OP_BIT_XOR_ASSIGN: arithmetic.op = CHECK_OP_BIT_XOR; break;
                default: break;
            }
            if (increment)
            {
                arithmetic.operands[1] = (CheckValue){
                    .type = BUSTER_TYPE_INT,
                    .flags = BUSTER_CHECK_ICE,
                    .constant_kind = CHECK_CONSTANT_INTEGER,
                    .integer = 1
                };
            }
            // This is one read-modify-store action, with one graph edge to the lvalue. The synthetic integer one
            // belongs to the update operation itself; no extra operand edge or duplicated lvalue evaluation exists.
            if ((arithmetic.op == CHECK_OP_ADD || arithmetic.op == CHECK_OP_SUBTRACT) && kind == BUSTER_TYPE_KIND_POINTER)
            {
                check_action_pointer_arithmetic(table, &arithmetic, result);
                if (check_action_kind(table, result->value.type) != BUSTER_TYPE_KIND_POINTER && result->diagnostic == CHECK_DIAG_NONE)
                {
                    check_action_diagnose(result, CHECK_DIAG_INTEGER_REQUIRED, 1);
                }
            }
            else
            {
                check_action_arithmetic(table, &arithmetic, result, 0, NULL);
            }
            if (increment)
            {
                result->conversions[1] = (CheckConversion){ 0 };
            }
            if (result->diagnostic == CHECK_DIAG_NONE)
            {
                const TypeId destination = type_conversion_operand(table, target.type);
                check_action_conversion(table, &result->value, destination, BUSTER_CHECK_CONVERT_ASSIGNMENT, &result->store_conversion, action->node);
                check_action_bit_field_store(table, &target, &result->value);
                if (postfix)
                {
                    CheckActionResult old = { 0 };
                    CheckValue before = check_action_operand(table, action, &old, 0);
                    check_action_inherit(&before, &result->value);
                    before.flags &= ~BUSTER_CHECK_BIT_FIELD;
                    before.bit_width = 0;
                    result->value = before;
                }
                result->value.category = CHECK_RVALUE;
                result->value.flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_NULL_POINTER | BUSTER_CHECK_REGISTER);
                result->value.flags |= BUSTER_CHECK_SIDE_EFFECT;
                if (result->value.fault != CHECK_FAULT_NONE)
                {
                    result->value.constant_kind = CHECK_CONSTANT_NONE;
                }
            }
        }
    }
}

static void check_action_cast(TypeTable* table, const CheckAction* action, CheckActionResult* result)
{
    CheckValue source = check_action_operand(table, action, result, 0);
    const TypeId target = type_unqualified(type_strip_aligned(table, action->type));
    const uint8_t target_kind = type_kind(table, target);
    const uint8_t input_kind = check_action_kind(table, source.type);
    const bool target_pointer = target_kind == BUSTER_TYPE_KIND_POINTER;
    const bool source_pointer = input_kind == BUSTER_TYPE_KIND_POINTER;
    const bool pointer_float = (target_pointer && !check_action_is_integer(table, source.type) && !source_pointer && input_kind != BUSTER_TYPE_KIND_NULLPTR) || (source_pointer && !target_pointer && !check_action_is_integer(table, target));
    const bool scalar = check_action_is_scalar(table, target) && check_action_is_scalar(table, source.type) && !pointer_float;
    const bool record = (target_kind == BUSTER_TYPE_KIND_STRUCT || target_kind == BUSTER_TYPE_KIND_UNION) &&
                        target_kind == input_kind && check_action_compatible_at(table, target, type_unqualified(source.type), action, result);
    if ((target_kind == BUSTER_TYPE_KIND_ENUM || target_kind == BUSTER_TYPE_KIND_STRUCT || target_kind == BUSTER_TYPE_KIND_UNION) && !check_action_source_complete(table, target, action))
    {
        check_action_diagnose(result, CHECK_DIAG_INCOMPLETE, 0);
    }
    else if (target_kind != BUSTER_TYPE_KIND_VOID && !scalar && !record)
    {
        check_action_diagnose(result, CHECK_DIAG_INCOMPATIBLE, 0);
    }
    else if (target_kind == BUSTER_TYPE_KIND_NULLPTR && input_kind != BUSTER_TYPE_KIND_NULLPTR)
    {
        check_action_diagnose(result, CHECK_DIAG_INCOMPATIBLE, 0);
    }
    else if (input_kind == BUSTER_TYPE_KIND_NULLPTR && !target_pointer && target_kind != BUSTER_TYPE_KIND_NULLPTR && target_kind != BUSTER_TYPE_KIND_VOID && target != BUSTER_TYPE_BOOL)
    {
        check_action_diagnose(result, CHECK_DIAG_INCOMPATIBLE, 0);
    }
    else
    {
        const bool ice = check_action_is_integer(table, target) && ((source.flags & BUSTER_CHECK_ICE) != 0 || (source.flags & BUSTER_CHECK_FLOAT_LITERAL) != 0);
        const bool gnu_ice = check_action_is_integer(table, target) && (source.flags & BUSTER_CHECK_GNU_ICE_FORM) != 0;
        const bool null = target_pointer && check_action_kind(table, type_element(table, target)) == BUSTER_TYPE_KIND_VOID && check_action_null(table, &source, action->extensions);
        check_action_conversion(table, &source, target, BUSTER_CHECK_CONVERT_CAST, &result->conversions[0], action->node);
        result->value = source;
        result->value.flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_ICE_UNEVALUATED);
        // A floating constant outside the target's range saturates (check_action_convert_plain): a GNU constant, not an ICE.
        const bool saturated = (result->value.flags & BUSTER_CHECK_GNU_FLOAT_RANGE) != 0;
        result->value.flags |= (saturated ? 0 : ice ? BUSTER_CHECK_ICE : (gnu_ice ? BUSTER_CHECK_GNU_ICE_FORM : 0)) | (null ? BUSTER_CHECK_NULL_POINTER : 0);
        if (target_kind == BUSTER_TYPE_KIND_VOID)
        {
            result->value.constant_kind = CHECK_CONSTANT_NONE;
        }
    }
}

static void check_action_query(TypeTable* table, const CheckAction* action, CheckActionResult* result)
{
    const TypeId target = action->type != 0 ? action->type : action->operands[0].type;
    const uint8_t kind = check_action_kind(table, target);
    const bool bit_field = action->type == 0 && action->operands[0].category == CHECK_LVALUE && (action->operands[0].flags & BUSTER_CHECK_BIT_FIELD) != 0;
    const bool gnu = kind == BUSTER_TYPE_KIND_FUNCTION || kind == BUSTER_TYPE_KIND_VOID;
    if (bit_field || (!gnu && (kind == BUSTER_TYPE_KIND_FUNCTION || kind == BUSTER_TYPE_KIND_VOID || !check_action_source_complete(table, target, action))))
    {
        check_action_diagnose(result, bit_field ? CHECK_DIAG_BIT_FIELD : CHECK_DIAG_INCOMPLETE, 0);
    }
    else if (check_action_runtime_size(table, target) && action->op == CHECK_OP_SIZEOF)
    {
        check_action_diagnose(result, CHECK_DIAG_UNSUPPORTED, 0);
    }
    else
    {
        result->value.type = BUSTER_TYPE_ULONG;
        result->value.category = CHECK_RVALUE;
        result->value.flags = BUSTER_CHECK_ICE;
        result->value.constant_kind = CHECK_CONSTANT_INTEGER;
        const uint64_t alignment = type_alignment(table, target);
        const uint64_t gnu_alignment = kind == BUSTER_TYPE_KIND_FUNCTION ? 4 : 1;
        result->value.integer = action->op == CHECK_OP_SIZEOF ? (gnu ? 1 : type_size(table, target)) : gnu && alignment < gnu_alignment ? gnu_alignment : alignment;
    }
}

static void check_action_vla(TypeTable* table, const CheckAction* action, CheckActionResult* result)
{
    if (action->op == CHECK_OP_VLA_EXTENT)
    {
        result->value.type = BUSTER_TYPE_ULONG;
        if (action->member == 0)
        {
            check_action_diagnose(result, CHECK_DIAG_UNSUPPORTED, 0);
        }
    }
    else
    {
        CheckValue size = check_action_operand(table, action, result, 0);
        if (!check_action_is_integer(table, size.type))
        {
            check_action_diagnose(result, CHECK_DIAG_INTEGER_REQUIRED, 0);
        }
        else if (action->op == CHECK_OP_VLA_BOUND && action->member == 0)
        {
            check_action_diagnose(result, CHECK_DIAG_UNSUPPORTED, 0);
        }
        else if (action->op == CHECK_OP_SIZEOF_VLA && !check_action_source_complete(table, action->type, action))
        {
            check_action_diagnose(result, CHECK_DIAG_INCOMPLETE, 0);
        }
        else if (action->op == CHECK_OP_SIZEOF_VLA && !check_action_runtime_size(table, action->type))
        {
            check_action_diagnose(result, CHECK_DIAG_INCOMPATIBLE, 0);
        }
        else
        {
            check_action_conversion(table, &size, BUSTER_TYPE_ULONG, BUSTER_CHECK_CONVERT_ARITHMETIC, &result->conversions[0], action->node);
            result->value = size;
            result->value.flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_NULL_POINTER);
            result->value.constant_kind = CHECK_CONSTANT_NONE;
        }
    }
}

static TypeId check_action_transparent_parameter(const TypeTable* table, TypeId destination, const CheckValue* source,
                                                const CheckAction* action, CheckActionResult* result)
{
    TypeId selected = 0;
    if (type_is_transparent_union(table, destination))
    {
        const uint32_t index = type_index(type_strip_aligned(table, destination));
        const uint32_t tag = table->types.index[index];
        const bool active = action->transparent_unions == NULL ||
                            (tag < action->tag_completion_count && action->transparent_unions[tag] != 0 &&
                             action->transparent_unions[tag] <= action->source_position);
        if (active && check_action_source_complete(table, destination, action))
        {
            const uint32_t first = table->tags.member_begin[tag];
            const uint32_t count = table->tags.member_count[tag];
            // Transparent-union selection requires an actual assignment-compatible member. The conversions that
            // only warn, and object/function pointers through void *, do not create a matching alternative.
            for (uint32_t i = 0; selected == 0 && result->diagnostic == CHECK_DIAG_NONE && i < count; i += 1)
            {
                CheckActionResult candidate = { 0 };
                const TypeId member = type_conversion_operand(table, table->members.type[first + i]);
                if (check_action_assignable_mode(table, member, source, action, &candidate, true))
                {
                    selected = member;
                }
                else if (candidate.diagnostic == CHECK_DIAG_RESOURCE)
                {
                    check_action_diagnose(result, CHECK_DIAG_RESOURCE, 0);
                }
            }
        }
    }
    return selected;
}

static void check_action_call(TypeTable* table, const CheckAction* action, CheckActionResult* result)
{
    const CheckValue callee = check_action_operand(table, action, result, 0);
    TypeId function = type_strip_aligned(table, callee.type);
    if (type_kind(table, function) == BUSTER_TYPE_KIND_POINTER)
    {
        function = type_strip_aligned(table, type_element(table, function));
    }
    if (type_kind(table, function) != BUSTER_TYPE_KIND_FUNCTION)
    {
        check_action_diagnose(result, CHECK_DIAG_CALL, 0);
    }
    else if (action->argument_count != 0 && (action->arguments == NULL || action->argument_conversions == NULL))
    {
        check_action_diagnose(result, CHECK_DIAG_INVALID_TYPE, 0);
    }
    else
    {
        const uint8_t flags = table->types.flags[type_index(function)];
        const bool prototype = (flags & BUSTER_TYPE_FLAG_PROTOTYPE) != 0;
        const bool variadic = (flags & BUSTER_TYPE_FLAG_VARIADIC) != 0;
        // A direct K&R designator has no prototype. The reader retains its known definition's promoted signature
        // separately, as clang does: convert the known arguments, but diagnose a count mismatch as a warning.
        // A real prototype (including a function pointer's) never takes this exception.
        const bool kr = !prototype && type_kind(table, action->type) == BUSTER_TYPE_KIND_FUNCTION;
        uint32_t count = 0;
        const TypeId* parameters = type_parameters(table, kr ? action->type : function, &count);
        if ((prototype || kr) && (action->argument_count < count || (!variadic && action->argument_count != count)))
        {
            if (kr)
            {
                result->warnings |= action->argument_count < count ? CHECK_WARNING_KR_TOO_FEW : CHECK_WARNING_KR_TOO_MANY;
            }
            else
            {
                // As clang reports them: too few at the call, too many at the first argument past the last parameter.
                check_action_diagnose(result, CHECK_DIAG_ARGUMENT_COUNT, action->argument_count > count ? count + 1 : 0);
            }
        }
        else if (!prototype && !kr && action->argument_count != 0)
        {
            result->warnings |= CHECK_WARNING_NON_PROTOTYPE_CALL;
        }
        result->value.type = type_return(table, function);
        result->value.flags = BUSTER_CHECK_SIDE_EFFECT;
        check_action_inherit(&result->value, &callee);
        if (check_action_kind(table, result->value.type) != BUSTER_TYPE_KIND_VOID && !check_action_source_complete(table, result->value.type, action))
        {
            check_action_diagnose(result, CHECK_DIAG_INCOMPLETE, 0);
        }
        for (uint32_t i = 0; i < action->argument_count; i += 1)
        {
            CheckAction argument = { 0 };
            argument.operands[0] = action->arguments[i];
            argument.node = action->node;
            argument.tag_completions = action->tag_completions;
            argument.tag_completion_count = action->tag_completion_count;
            argument.source_position = action->source_position;
            argument.extensions = action->extensions;
            CheckActionResult converted = { 0 };
            if (!type_is_valid(table, argument.operands[0].type))
            {
                check_action_diagnose(result, CHECK_DIAG_INVALID_TYPE, i + 1);
                action->argument_conversions[i] = (CheckConversion){ 0 };
            }
            else
            {
                CheckValue value = check_action_operand(table, &argument, &converted, 0);
                const bool parameter = (prototype || kr) && i < count;
                if (!parameter)
                {
                    check_action_promote(table, &value, &converted.conversions[0], action->node);
                }
                TypeId destination = parameter ? parameters[i] : type_default_argument_promote(table, value.type);
                if (check_action_kind(table, value.type) != BUSTER_TYPE_KIND_ATOMIC)
                {
                    destination = type_conversion_operand(table, destination);
                }
                bool assignable = check_action_assignable(table, destination, &value, action, &converted);
                bool transparent = false;
                if (!assignable && parameter && converted.diagnostic == CHECK_DIAG_NONE)
                {
                    const TypeId member = check_action_transparent_parameter(table, destination, &value, action, &converted);
                    if (member != 0)
                    {
                        destination = member;
                        assignable = true;
                        transparent = true;
                    }
                }
                if (!assignable)
                {
                    check_action_diagnose(result, converted.diagnostic != CHECK_DIAG_NONE ? converted.diagnostic : CHECK_DIAG_INCOMPATIBLE, i + 1);
                }
                else
                {
                    const uint32_t conversion = (parameter ? BUSTER_CHECK_CONVERT_ASSIGNMENT : destination != value.type ? BUSTER_CHECK_CONVERT_PROMOTION : 0) |
                                                (transparent ? BUSTER_CHECK_CONVERT_TRANSPARENT_UNION : 0);
                    check_action_conversion(table, &value, destination, conversion, &converted.conversions[0], action->node);
                }
                if (converted.diagnostic != CHECK_DIAG_NONE)
                {
                    check_action_diagnose(result, converted.diagnostic, i + 1);
                }
                result->warnings |= converted.warnings;
                action->argument_conversions[i] = converted.conversions[0];
                check_action_inherit(&result->value, &value);
            }
        }
    }
}

#include "c_check_complex.c"
#include "c_check_vector.c"

static uint32_t check_action_operand_count(const CheckAction* action)
{
    uint32_t count = 2;
    switch (action->op)
    {
        case CHECK_OP_VLA_EXTENT:
        case CHECK_OP_BUILTIN_MISC:
        case CHECK_OP_BUILTIN_FLOAT:
        case CHECK_OP_BUILTIN_QUERY:
        case CHECK_OP_BUILTIN_ATOMIC:
        case CHECK_OP_BUILTIN_VECTOR:
        case CHECK_OP_BUILTIN_X86:
        case CHECK_OP_BUILTIN_BIT_CAST: count = 0; break;
        case CHECK_OP_VALUE:
        case CHECK_OP_POSITIVE:
        case CHECK_OP_NEGATIVE:
        case CHECK_OP_NOT:
        case CHECK_OP_BIT_NOT:
        case CHECK_OP_REAL:
        case CHECK_OP_IMAGINARY:
        case CHECK_OP_CAST:
        case CHECK_OP_ADDRESS:
        case CHECK_OP_DEREFERENCE:
        case CHECK_OP_MEMBER:
        case CHECK_OP_ARROW:
        case CHECK_OP_CALL:
        case CHECK_OP_INITIALIZE:
        case CHECK_OP_CONVERT:
        case CHECK_OP_CONDITION:
        case CHECK_OP_PRE_INCREMENT:
        case CHECK_OP_PRE_DECREMENT:
        case CHECK_OP_POST_INCREMENT:
        case CHECK_OP_POST_DECREMENT:
        case CHECK_OP_SELECT:
        case CHECK_OP_BUILTIN_UNARY:
        case CHECK_OP_STATEMENT_VALUE:
        case CHECK_OP_VLA_BOUND:
        case CHECK_OP_SIZEOF_VLA: count = 1; break;
        case CHECK_OP_SIZEOF:
        case CHECK_OP_ALIGNOF: count = action->type == 0 ? 1 : 0; break;
        case CHECK_OP_CONDITIONAL:
        case CHECK_OP_BUILTIN_OVERFLOW: count = 3; break;
        default: break;
    }
    return count;
}

static bool check_action_floating_type(const TypeTable* table, TypeId type)
{
    TypeId base = type_strip_aligned(table, type);
    if (type_kind(table, base) == BUSTER_TYPE_KIND_COMPLEX || check_vector_type(table, base))
    {
        base = type_element(table, base);
    }
    return type_kind(table, base) == BUSTER_TYPE_KIND_FLOATING;
}

static bool check_action_fp_conversion(const TypeTable* table, TypeId source, TypeId destination)
{
    const TypeId from = type_unqualified(type_strip_aligned(table, source));
    const TypeId to = type_unqualified(type_strip_aligned(table, destination));
    return from != to && to != BUSTER_TYPE_VOID &&
           (check_action_floating_type(table, from) || check_action_floating_type(table, to));
}

static void check_action_fp_environment(const TypeTable* table, const CheckAction* action, CheckActionResult* result)
{
    if ((action->fp_environment & BUSTER_CHECK_FP_ENV_ACCESS) != 0 && result->diagnostic == CHECK_DIAG_NONE)
    {
        bool floating = false;
        bool sensitive = false;
        const uint32_t count = check_action_operand_count(action);
        uint32_t evaluated = (1u << count) - 1;
        bool truth = false;
        const bool fixed_condition = count != 0 && (action->operands[0].flags & BUSTER_CHECK_FP_ENVIRONMENT) == 0 &&
                                     !check_action_floating_type(table, action->operands[0].type) &&
                                     check_action_truth(table, &action->operands[0], &truth);
        if (fixed_condition && ((action->op == CHECK_OP_LOGICAL_AND && !truth) || (action->op == CHECK_OP_LOGICAL_OR && truth)))
        {
            evaluated = 1;
        }
        else if (fixed_condition && action->op == CHECK_OP_CONDITIONAL)
        {
            evaluated = truth ? 3 : 5;
        }
        for (uint32_t i = 0; i < count; i += 1)
        {
            if ((evaluated & (1u << i)) != 0)
            {
                floating |= check_action_floating_type(table, action->operands[i].type);
                sensitive |= result->conversions[i].type != 0 &&
                             check_action_fp_conversion(table, action->operands[i].type, result->conversions[i].type);
            }
        }
        if (action->op == CHECK_OP_CALL && action->argument_conversions != NULL)
        {
            for (uint32_t i = 0; i < action->argument_count; i += 1)
            {
                sensitive |= check_action_fp_conversion(table, action->arguments[i].type, action->argument_conversions[i].type);
            }
        }
        // Source constants still describe translation-time evaluation. This separate flag prevents runtime
        // consumers from discarding exceptions, assuming nearest rounding, or pruning environment-dependent branches.
        const bool arithmetic = action->op == CHECK_OP_POSITIVE || action->op == CHECK_OP_NEGATIVE || action->op == CHECK_OP_NOT ||
                                (action->op >= CHECK_OP_ADD && action->op <= CHECK_OP_LOGICAL_OR) || action->op == CHECK_OP_CONDITION ||
                                (action->op >= CHECK_OP_PRE_INCREMENT && action->op <= CHECK_OP_BIT_XOR_ASSIGN);
        sensitive |= floating && arithmetic;
        sensitive |= action->op == CHECK_OP_CONDITIONAL && check_action_floating_type(table, action->operands[0].type);
        if (sensitive)
        {
            result->value.flags |= BUSTER_CHECK_FP_ENVIRONMENT;
        }
    }
}

static void check_action_compute(TypeTable* table, const CheckAction* action, CheckActionResult* result, TypeId common_hint, CheckIntegerFold* pending)
{
    memset(result, 0, sizeof(*result));
    if (pending != NULL)
    {
        memset(pending, 0, sizeof(*pending));
    }
    if (action->op >= CHECK_OP_COUNT)
    {
        check_action_diagnose(result, CHECK_DIAG_UNSUPPORTED, 0);
    }
    const uint32_t count = check_action_operand_count(action);
    for (uint32_t i = 0; i < count; i += 1)
    {
        if (!type_is_valid(table, action->operands[i].type))
        {
            check_action_diagnose(result, CHECK_DIAG_INVALID_TYPE, i);
        }
        if (action->operands[i].overload_head != 0 && action->op != CHECK_OP_VALUE && action->op != CHECK_OP_SELECT && action->op != CHECK_OP_ADDRESS)
        {
            check_action_diagnose(result, CHECK_DIAG_INCOMPATIBLE, i);
        }
        result->conversions[i].type = action->operands[i].type;
    }
    for (uint32_t i = 0; action->arguments != 0 && i < action->argument_count; i += 1)
    {
        if (action->arguments[i].overload_head != 0)
        {
            check_action_diagnose(result, CHECK_DIAG_INCOMPATIBLE, i + 1);
        }
    }
    if ((action->op == CHECK_OP_CAST || action->op == CHECK_OP_INITIALIZE || action->op == CHECK_OP_CONVERT || action->op == CHECK_OP_SIZEOF_VLA || ((action->op == CHECK_OP_SIZEOF || action->op == CHECK_OP_ALIGNOF) && action->type != 0)) && !type_is_valid(table, action->type))
    {
        check_action_diagnose(result, CHECK_DIAG_INVALID_TYPE, 0);
    }
    if (result->diagnostic == CHECK_DIAG_NONE && !check_vector_action(table, action, result))
    {
        const uint8_t left_kind = count != 0 ? check_action_kind(table, action->operands[0].type) : BUSTER_TYPE_KIND_NONE;
        const uint8_t right_kind = count >= 2 ? check_action_kind(table, action->operands[1].type) : BUSTER_TYPE_KIND_NONE;
        const bool pointer_pair = left_kind == BUSTER_TYPE_KIND_POINTER || right_kind == BUSTER_TYPE_KIND_POINTER || type_kind_is_array(left_kind) || type_kind_is_array(right_kind) || left_kind == BUSTER_TYPE_KIND_FUNCTION || right_kind == BUSTER_TYPE_KIND_FUNCTION || left_kind == BUSTER_TYPE_KIND_NULLPTR || right_kind == BUSTER_TYPE_KIND_NULLPTR;
        switch (action->op)
        {
            case CHECK_OP_VALUE:
            case CHECK_OP_SELECT: result->value = action->operands[0]; break;
            case CHECK_OP_VLA_BOUND:
            case CHECK_OP_VLA_EXTENT:
            case CHECK_OP_SIZEOF_VLA: check_action_vla(table, action, result); break;
            case CHECK_OP_STATEMENT_VALUE:
            {
                result->value = check_action_operand(table, action, result, 0);
                result->value.category = CHECK_RVALUE;
                result->value.flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_NULL_POINTER);
                result->value.flags |= action->member & BUSTER_CHECK_SIDE_EFFECT;
                if ((action->member & ~BUSTER_CHECK_SIDE_EFFECT) != 0)
                {
                    check_action_diagnose(result, CHECK_DIAG_UNSUPPORTED, 0);
                }
            } break;
            case CHECK_OP_POSITIVE:
            case CHECK_OP_NEGATIVE:
            case CHECK_OP_MULTIPLY:
            case CHECK_OP_DIVIDE:
            case CHECK_OP_REMAINDER:
            case CHECK_OP_SHIFT_LEFT:
            case CHECK_OP_SHIFT_RIGHT:
            case CHECK_OP_BIT_AND:
            case CHECK_OP_BIT_OR:
            case CHECK_OP_BIT_XOR: check_action_arithmetic(table, action, result, common_hint, pending); break;
            case CHECK_OP_BIT_NOT:
            {
                if (left_kind == BUSTER_TYPE_KIND_COMPLEX)
                {
                    check_complex_conjugate(table, action, result);
                }
                else
                {
                    check_action_arithmetic(table, action, result, common_hint, pending);
                }
            } break;
            case CHECK_OP_REAL:
            case CHECK_OP_IMAGINARY: check_complex_part(table, action, result); break;
            case CHECK_OP_ADD:
            case CHECK_OP_SUBTRACT:
            {
                if (pointer_pair)
                {
                    check_action_pointer_arithmetic(table, action, result);
                }
                else
                {
                    check_action_arithmetic(table, action, result, common_hint, pending);
                }
            } break;
            case CHECK_OP_LESS:
            case CHECK_OP_LESS_EQUAL:
            case CHECK_OP_GREATER:
            case CHECK_OP_GREATER_EQUAL:
            case CHECK_OP_EQUAL:
            case CHECK_OP_NOT_EQUAL:
            {
                if (pointer_pair)
                {
                    check_action_pointer_compare(table, action, result);
                }
                else
                {
                    check_action_arithmetic(table, action, result, common_hint, pending);
                }
            } break;
            case CHECK_OP_NOT:
            case CHECK_OP_CONDITION:
            case CHECK_OP_LOGICAL_AND:
            case CHECK_OP_LOGICAL_OR: check_action_logic(table, action, result); break;
            case CHECK_OP_CONDITIONAL: check_action_conditional(table, action, result); break;
            case CHECK_OP_CAST: check_action_cast(table, action, result); break;
            case CHECK_OP_ASSIGN:
            case CHECK_OP_INITIALIZE:
            case CHECK_OP_CONVERT: check_action_assignment(table, action, result); break;
            case CHECK_OP_PRE_INCREMENT:
            case CHECK_OP_PRE_DECREMENT:
            case CHECK_OP_POST_INCREMENT:
            case CHECK_OP_POST_DECREMENT:
            case CHECK_OP_ADD_ASSIGN:
            case CHECK_OP_SUBTRACT_ASSIGN:
            case CHECK_OP_MULTIPLY_ASSIGN:
            case CHECK_OP_DIVIDE_ASSIGN:
            case CHECK_OP_REMAINDER_ASSIGN:
            case CHECK_OP_SHIFT_LEFT_ASSIGN:
            case CHECK_OP_SHIFT_RIGHT_ASSIGN:
            case CHECK_OP_BIT_AND_ASSIGN:
            case CHECK_OP_BIT_OR_ASSIGN:
            case CHECK_OP_BIT_XOR_ASSIGN: check_action_update(table, action, result); break;
            case CHECK_OP_COMMA:
            {
                const CheckValue left = check_action_operand(table, action, result, 0);
                const CheckValue right = check_action_operand(table, action, result, 1);
                result->value = right;
                result->value.category = CHECK_RVALUE;
                result->value.flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_NULL_POINTER);
                const uint32_t allowed = BUSTER_CHECK_ICE | BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_GNU_ICE_FORM;
                result->value.flags |= (left.flags & allowed) != 0 && (right.flags & allowed) != 0 ? BUSTER_CHECK_ICE_UNEVALUATED : 0;
                if (left.fault != CHECK_FAULT_NONE)
                {
                    result->value.fault = left.fault;
                    result->value.fault_node = left.fault_node;
                }
                check_action_inherit(&result->value, &left);
            } break;
            case CHECK_OP_ADDRESS:
            {
                const CheckValue operand = action->operands[0];
                if ((operand.flags & BUSTER_CHECK_VECTOR_ELEMENT) != 0)
                {
                    check_action_diagnose(result, CHECK_DIAG_LVALUE_REQUIRED, 0);
                }
                else if ((operand.flags & BUSTER_CHECK_BIT_FIELD) != 0)
                {
                    check_action_diagnose(result, CHECK_DIAG_BIT_FIELD, 0);
                }
                else if ((operand.flags & BUSTER_CHECK_REGISTER) != 0)
                {
                    check_action_diagnose(result, CHECK_DIAG_REGISTER_ADDRESS, 0);
                }
                else if (operand.category != CHECK_LVALUE && operand.category != CHECK_FUNCTION)
                {
                    check_action_diagnose(result, CHECK_DIAG_LVALUE_REQUIRED, 0);
                }
                else
                {
                    result->value = operand;
                    result->value.type = type_pointer(table, operand.type);
                    result->value.category = CHECK_RVALUE;
                    result->value.flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_BIT_FIELD | BUSTER_CHECK_REGISTER | BUSTER_CHECK_VOLATILE_PART);
                    if ((operand.flags & BUSTER_CHECK_STATIC_OBJECT) == 0 && result->value.constant_kind == CHECK_CONSTANT_ADDRESS && result->value.object != 0)
                    {
                        result->value.constant_kind = CHECK_CONSTANT_NONE;
                    }
                    if (result->value.type == 0)
                    {
                        check_action_diagnose(result, CHECK_DIAG_RESOURCE, 0);
                    }
                }
            } break;
            case CHECK_OP_DEREFERENCE:
            {
                const CheckValue pointer = check_action_operand(table, action, result, 0);
                if (check_action_kind(table, pointer.type) != BUSTER_TYPE_KIND_POINTER)
                {
                    check_action_diagnose(result, CHECK_DIAG_POINTER_REQUIRED, 0);
                }
                else
                {
                    result->value = pointer;
                    result->value.type = type_element(table, type_strip_aligned(table, pointer.type));
                    result->value.category = check_action_kind(table, result->value.type) == BUSTER_TYPE_KIND_FUNCTION ? CHECK_FUNCTION : CHECK_LVALUE;
                    result->value.flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_NULL_POINTER | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_ICE_UNEVALUATED);
                    // Keep an incomplete designator until its context is known: &*p is valid even for void*.
                    // An ordinary value context rejects the incomplete type in check_action_operand.
                }
            } break;
            case CHECK_OP_SUBSCRIPT: check_action_pointer_arithmetic(table, action, result); break;
            case CHECK_OP_SIZEOF:
            case CHECK_OP_ALIGNOF: check_action_query(table, action, result); break;
            case CHECK_OP_MEMBER:
            case CHECK_OP_ARROW: check_action_member(table, action, result); break;
            case CHECK_OP_CALL: check_action_call(table, action, result); break;
            case CHECK_OP_BUILTIN_UNARY:
            case CHECK_OP_BUILTIN_OVERFLOW:
            case CHECK_OP_VARARGS:
            case CHECK_OP_BUILTIN_MISC:
            case CHECK_OP_BUILTIN_FLOAT:
            case CHECK_OP_BUILTIN_QUERY:
            case CHECK_OP_BUILTIN_ATOMIC:
            case CHECK_OP_BUILTIN_VECTOR:
            case CHECK_OP_BUILTIN_X86:
            case CHECK_OP_BUILTIN_BIT_CAST: check_action_diagnose(result, CHECK_DIAG_UNSUPPORTED, 0); break;
            default: check_action_diagnose(result, CHECK_DIAG_UNSUPPORTED, 0); break;
        }
    }
    check_action_fp_environment(table, action, result);
    if (result->diagnostic != CHECK_DIAG_NONE)
    {
        result->value.constant_kind = CHECK_CONSTANT_NONE;
        result->value.flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_ICE_UNEVALUATED);
        if (pending != NULL)
        {
            pending->active = false;
        }
    }
}

static void check_action_scalar(TypeTable* table, const CheckAction* action, CheckActionResult* result)
{
    check_action_compute(table, action, result, 0, NULL);
}

static bool check_action_vector_eligible(const TypeTable* table, const CheckAction* action)
{
    const CheckOp op = action->op;
    bool result = op == CHECK_OP_POSITIVE || op == CHECK_OP_NEGATIVE || op == CHECK_OP_BIT_NOT || op == CHECK_OP_ADD || op == CHECK_OP_SUBTRACT || (op >= CHECK_OP_SHIFT_LEFT && op <= CHECK_OP_BIT_XOR);
    const uint32_t count = op == CHECK_OP_POSITIVE || op == CHECK_OP_NEGATIVE || op == CHECK_OP_BIT_NOT ? 1 : 2;
    for (uint32_t i = 0; i < count && result; i += 1)
    {
        const CheckValue* value = &action->operands[i];
        const uint32_t index = type_index(value->type);
        result = type_is_valid(table, value->type) && value->category <= CHECK_LVALUE && index >= BUSTER_TYPE_CODE_BOOL && index <= BUSTER_TYPE_CODE_ULLONG && (value->flags & BUSTER_CHECK_BIT_FIELD) == 0;
    }
    return result;
}

#define BUSTER_CHECK_FOLD_LANES 8u

// Eight potentially different integer operations. Scalar packing avoids Zen 5 gathers; all arithmetic and fault
// predicates are vector operations, including the 32-bit target's overflow/shift checks in 64-bit host lanes.
static void check_action_fold_vector(const CheckIntegerFold* folds, CheckActionResult* results, uint32_t count)
{
    uint64_t aa[BUSTER_CHECK_FOLD_LANES] = { 0 };
    uint64_t bb[BUSTER_CHECK_FOLD_LANES] = { 0 };
    uint64_t mm[BUSTER_CHECK_FOLD_LANES] = { 0 };
    uint64_t ss[BUSTER_CHECK_FOLD_LANES] = { 0 };
    uint64_t ww[BUSTER_CHECK_FOLD_LANES] = { 0 };
    uint64_t oo[BUSTER_CHECK_FOLD_LANES] = { 0 };
    __mmask8 active = 0;
    __mmask8 signed_lanes = 0;
    __mmask8 negative_right = 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        if (folds[i].active)
        {
            const uint32_t width = folds[i].width;
            aa[i] = (uint64_t)folds[i].left;
            bb[i] = (uint64_t)(folds[i].right & check_action_mask(folds[i].right_width));
            mm[i] = (uint64_t)check_action_mask(width);
            ss[i] = UINT64_C(1) << (width - 1);
            ww[i] = width;
            oo[i] = folds[i].op;
            active |= (__mmask8)(1u << i);
            signed_lanes |= (__mmask8)((uint32_t)folds[i].is_signed << i);
            negative_right |= (__mmask8)((uint32_t)(folds[i].right_signed && (bb[i] >> (folds[i].right_width - 1)) != 0) << i);
        }
    }
    const __m512i zero = _mm512_setzero_si512();
    const __m512i one = _mm512_set1_epi64(1);
    const __m512i masks = _mm512_loadu_si512(mm);
    const __m512i sign = _mm512_loadu_si512(ss);
    const __m512i a = _mm512_and_si512(_mm512_loadu_si512(aa), masks);
    const __m512i b = _mm512_loadu_si512(bb);
    const __m512i ops = _mm512_loadu_si512(oo);
    const __m512i width = _mm512_loadu_si512(ww);
    const __mmask8 an = _mm512_test_epi64_mask(a, sign) & signed_lanes;
    const __mmask8 bn = negative_right;
    const __mmask8 less = ((an ^ bn) & an) | ((__mmask8)~(an ^ bn) & _mm512_cmplt_epu64_mask(a, b));
    const __mmask8 equal = _mm512_cmpeq_epi64_mask(a, b);
    __m512i answer = zero;
    __mmask8 overflow = 0;
    __mmask8 bad_shift = 0;
    __mmask8 bad_value = 0;
#define BUSTER_CHECK_OP_MASK(op) (_mm512_cmpeq_epi64_mask(ops, _mm512_set1_epi64(op)) & active)
    __mmask8 chosen = BUSTER_CHECK_OP_MASK(CHECK_OP_POSITIVE);
    answer = _mm512_mask_mov_epi64(answer, chosen, a);
    chosen = BUSTER_CHECK_OP_MASK(CHECK_OP_NEGATIVE);
    answer = _mm512_mask_sub_epi64(answer, chosen, zero, a);
    overflow |= chosen & signed_lanes & _mm512_cmpeq_epi64_mask(a, sign);
    chosen = BUSTER_CHECK_OP_MASK(CHECK_OP_BIT_NOT);
    answer = _mm512_mask_xor_epi64(answer, chosen, a, masks);
    const __m512i sum = _mm512_and_si512(_mm512_add_epi64(a, b), masks);
    chosen = BUSTER_CHECK_OP_MASK(CHECK_OP_ADD);
    answer = _mm512_mask_mov_epi64(answer, chosen, sum);
    overflow |= chosen & signed_lanes & _mm512_test_epi64_mask(_mm512_andnot_si512(_mm512_xor_si512(a, b), _mm512_xor_si512(a, sum)), sign);
    const __m512i difference = _mm512_and_si512(_mm512_sub_epi64(a, b), masks);
    chosen = BUSTER_CHECK_OP_MASK(CHECK_OP_SUBTRACT);
    answer = _mm512_mask_mov_epi64(answer, chosen, difference);
    overflow |= chosen & signed_lanes & _mm512_test_epi64_mask(_mm512_and_si512(_mm512_xor_si512(a, b), _mm512_xor_si512(a, difference)), sign);
    const __mmask8 left_shift = BUSTER_CHECK_OP_MASK(CHECK_OP_SHIFT_LEFT);
    const __mmask8 right_shift = BUSTER_CHECK_OP_MASK(CHECK_OP_SHIFT_RIGHT);
    bad_shift = (left_shift | right_shift) & (bn | _mm512_cmpge_epu64_mask(b, width));
    answer = _mm512_mask_sllv_epi64(answer, left_shift, a, b);
    const __m512i limit = _mm512_srlv_epi64(masks, b);
    bad_value = left_shift & (__mmask8)~bad_shift & signed_lanes & (an | _mm512_cmpgt_epu64_mask(a, limit));
    __m512i shifted = _mm512_srlv_epi64(a, b);
    const __m512i extension = _mm512_xor_si512(masks, _mm512_srlv_epi64(masks, b));
    shifted = _mm512_mask_or_epi64(shifted, an, shifted, extension);
    answer = _mm512_mask_mov_epi64(answer, right_shift, shifted);
    chosen = BUSTER_CHECK_OP_MASK(CHECK_OP_LESS);
    answer = _mm512_mask_mov_epi64(answer, chosen, _mm512_maskz_mov_epi64(less, one));
    chosen = BUSTER_CHECK_OP_MASK(CHECK_OP_LESS_EQUAL);
    answer = _mm512_mask_mov_epi64(answer, chosen, _mm512_maskz_mov_epi64(less | equal, one));
    chosen = BUSTER_CHECK_OP_MASK(CHECK_OP_GREATER);
    answer = _mm512_mask_mov_epi64(answer, chosen, _mm512_maskz_mov_epi64((__mmask8)~(less | equal), one));
    chosen = BUSTER_CHECK_OP_MASK(CHECK_OP_GREATER_EQUAL);
    answer = _mm512_mask_mov_epi64(answer, chosen, _mm512_maskz_mov_epi64((__mmask8)~less, one));
    chosen = BUSTER_CHECK_OP_MASK(CHECK_OP_EQUAL);
    answer = _mm512_mask_mov_epi64(answer, chosen, _mm512_maskz_mov_epi64(equal, one));
    chosen = BUSTER_CHECK_OP_MASK(CHECK_OP_NOT_EQUAL);
    answer = _mm512_mask_mov_epi64(answer, chosen, _mm512_maskz_mov_epi64((__mmask8)~equal, one));
    chosen = BUSTER_CHECK_OP_MASK(CHECK_OP_BIT_AND);
    answer = _mm512_mask_and_epi64(answer, chosen, a, b);
    chosen = BUSTER_CHECK_OP_MASK(CHECK_OP_BIT_OR);
    answer = _mm512_mask_or_epi64(answer, chosen, a, b);
    chosen = BUSTER_CHECK_OP_MASK(CHECK_OP_BIT_XOR);
    answer = _mm512_mask_xor_epi64(answer, chosen, a, b);
#undef BUSTER_CHECK_OP_MASK
    uint64_t values[BUSTER_CHECK_FOLD_LANES];
    _mm512_storeu_si512(values, _mm512_and_si512(answer, masks));
    for (uint32_t i = 0; i < count; i += 1)
    {
        if (folds[i].active)
        {
            const uint32_t bit = 1u << i;
            const CheckFault fault = (overflow & bit) != 0 ? CHECK_FAULT_SIGNED_OVERFLOW : ((bad_shift & bit) != 0 ? CHECK_FAULT_SHIFT_COUNT : ((bad_value & bit) != 0 ? CHECK_FAULT_SHIFT_VALUE : CHECK_FAULT_NONE));
            results[i].warnings |= check_action_fold_integer_result(folds[i], &results[i].value, values[i], fault);
        }
    }
}

static void check_actions(TypeTable* table, const CheckAction* actions, CheckActionResult* results, uint32_t count)
{
    if (count == 1)
    {
        check_action_scalar(table, actions, results);
    }
    for (uint32_t begin = count == 1 ? 1 : 0; begin < count;)
    {
        const uint32_t remaining = count - begin;
        const uint32_t lanes = remaining < BUSTER_CHECK_FOLD_LANES ? remaining : BUSTER_CHECK_FOLD_LANES;
        CheckIntegerFold folds[BUSTER_CHECK_FOLD_LANES] = { 0 };
        uint8_t left_codes[BUSTER_CHECK_FOLD_LANES] = { 0 };
        uint8_t right_codes[BUSTER_CHECK_FOLD_LANES] = { 0 };
        uint8_t common_codes[BUSTER_CHECK_FOLD_LANES] = { 0 };
        bool eligible[BUSTER_CHECK_FOLD_LANES] = { 0 };
        bool any_eligible = false;
        for (uint32_t i = 0; i < lanes; i += 1)
        {
            const CheckAction* action = &actions[begin + i];
            eligible[i] = check_action_vector_eligible(table, action);
            if (eligible[i])
            {
                any_eligible = true;
                left_codes[i] = type_arithmetic_code(table, action->operands[0].type);
                const bool unary = check_action_operand_count(action) == 1;
                right_codes[i] = unary ? left_codes[i] : type_arithmetic_code(table, action->operands[1].type);
            }
        }
        if (any_eligible)
        {
            type_usual_arithmetic_codes(left_codes, right_codes, lanes, common_codes);
        }
        bool any_fold = false;
        for (uint32_t i = 0; i < lanes; i += 1)
        {
            // Scalar preparation stays in input order: pointer decay and other deferred actions may intern types.
            const TypeId hint = eligible[i] ? BUSTER_TYPE_BUILTIN(common_codes[i]) : 0;
            check_action_compute(table, &actions[begin + i], &results[begin + i], hint, eligible[i] ? &folds[i] : NULL);
            any_fold |= folds[i].active;
        }
        if (any_fold)
        {
            check_action_fold_vector(folds, results + begin, lanes);
        }
        begin += lanes;
    }
}

#endif
