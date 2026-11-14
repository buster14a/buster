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
#include <float.h>
#include "host_math.h"
#include "host_isa.h"
#include "c_check_warning_events.c"
#include "c_check_warning_set.c"
#include "c_check_context.c"

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
    // GNU's difference of two labels' addresses in a static initializer (#1447, check_init_label_difference): the
    // minuend label's object in `object`, the subtrahend's in `integer`. Only a static initializer's constants and parts
    // hold one, which the static data writes as the labels' distance in their function's code (check_data_label_distance).
    CHECK_CONSTANT_LABEL_DIFFERENCE,
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
// A register object's own name, unparenthesized (#1447): clang's DefaultFunctionArrayConversion rejects the decay of a
// register array only when it is a DeclRefExpr, at that name; a parenthesized name, a member array of a register record
// and a subscript's array decay quietly, though the & operator still rejects them (BUSTER_CHECK_REGISTER).
#define BUSTER_CHECK_REGISTER_NAME 0x1000000u
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
// A known value read from a completed const object's initializer. It can be a GNU constant without being an ICE
// or an ICE-form null pointer constant. An enumerator declared from it receives its own ordinary ICE provenance.
#define BUSTER_CHECK_GNU_OBJECT_READ 0x10000u
// Source provenance, not constant-expression eligibility: numeric zero literals survive only transparent
// parentheses/selections. A character, enum, object read, cast or computed zero does not acquire this proof.
#define BUSTER_CHECK_LITERAL_ZERO 0x20000u
// A value an assignment, a compound assignment or an increment gives, or one computed from such a value: clang's evaluator
// fails on it, as it may modify no object whose lifetime began before the evaluation, even past side effects (an
// alignment builtin's `(s = 3)` is no constant to it). In a constant evaluation, a comma's left operand, whose value is
// discarded, doesn't pass it on; a runtime view's comma, which nothing reads it from, keeps it.
#define BUSTER_CHECK_MODIFICATION 0x400000u
// The designator names an inactive member of a known const union. Its address remains valid; value conversion
// cannot use the initializer receipt. Runtime union access keeps its ordinary semantics.
#define BUSTER_CHECK_INACTIVE_UNION_READ 0x40000u
#define BUSTER_CHECK_INACTIVE_UNION_ADDRESS 0x80000u
// A symbolic address can remain valid when Clang refuses a value read through its source path: a changed
// pointee type (including a void round trip), or an array member without a declared bound. This evidence
// follows the selected pointer, not unrelated/discarded operands. Qualifier-only casts keep the read proof.
#define BUSTER_CHECK_CONST_READ_RESTRICTED 0x100000u
// A discarded operand that clang's evaluator can't evaluate: a comma's left operand, or a cast to void's, that is no
// constant (a variable's read, a modification), or holds such an operand itself. A constant expression's evaluation fails
// on it (expect_with_probability's probability: `(s, 0.75)`, `((void)s, 0.75)`), where a fold past side effects goes on.
#define BUSTER_CHECK_DISCARDED_UNEVALUABLE 0x200000u
// The address of an object or function that is not weak. It is never null, at any byte displacement. A weak
// declaration or `#pragma weak` can be a null pointer, so those addresses do not carry this flag.
#define BUSTER_CHECK_NONNULL_ADDRESS 0x800000u

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
// Lexical evaluation methods insert ordinary scalar/complex promotions, as Clang's UsualUnaryFPConversions.
// They do not mean a dynamic floating environment and must remain active during constant evaluation.
#define BUSTER_CHECK_FP_EVAL_DOUBLE 0x02u
#define BUSTER_CHECK_FP_EVAL_EXTENDED 0x04u
#define BUSTER_CHECK_FP_EVAL_MASK (BUSTER_CHECK_FP_EVAL_DOUBLE | BUSTER_CHECK_FP_EVAL_EXTENDED)
#define BUSTER_CHECK_FP_ENV_MASK (BUSTER_CHECK_FP_ENV_ACCESS | BUSTER_CHECK_FP_EVAL_MASK)
// Token history only: an evaluation-method pragma occurred in this lexical scope, even if it selected source.
#define BUSTER_CHECK_FP_EVAL_PRAGMA 0x08u
// Typedef restrictions use the last pragma's method, which float_control(pop) does not restore with arithmetic.
#define BUSTER_CHECK_FP_EVAL_LAST_DOUBLE 0x10u
#define BUSTER_CHECK_FP_EVAL_LAST_EXTENDED 0x20u
#define BUSTER_CHECK_FP_EVAL_LAST_MASK (BUSTER_CHECK_FP_EVAL_LAST_DOUBLE | BUSTER_CHECK_FP_EVAL_LAST_EXTENDED)

typedef struct CheckValue CheckValue;
struct CheckValue
{
    TypeId type;
    CheckCategory category;
    uint32_t flags;
    CheckConstantKind constant_kind;
    unsigned __int128 integer;
    // Integer storage comes first so omitted payloads in aggregate initializers are zeroed without x87.
    // The floating view preserves the existing format, alignment and all arithmetic consumers.
    union
    {
        uint64_t floating_storage[2];
        long double floating;
    };
    // Floating complex constants retain both source-format components. GNU integer-complex constants use
    // integer's low/high 64-bit halves, each masked to the component type's width.
    union
    {
        uint64_t imaginary_storage[2];
        long double imaginary;
    };
    uint32_t object;
    // An immutable overload-set head plus one. It must obtain a contextual function identity before ordinary use.
    uint32_t overload_head;
    int64_t offset;
    uint32_t bit_width;
    CheckFault fault;
    uint32_t fault_node;
    // Bits within offset's byte for a bit-field designator. This occupies the value's existing tail padding.
    uint32_t object_bit_offset;
};

_Static_assert(sizeof(CheckValue) == 96 && __builtin_offsetof(CheckValue, object) == 64, "check_value_copy copies a value as its first 64 bytes and its last 32");

// `source` copied whole into `destination` as two halves, its first 64 bytes and its last 32, by one load and one store
// each on the vector path. A value the hot path copies (an action's operands, check_stream_value_to's) is written this way
// and read back this way, so that every load takes its data from one store: Zen 5's code copies 96 bytes by two
// overlapping 64-byte loads, of which the second spans both halves' stores, which no store can forward (#1050). The scalar
// path is the assignment.
// Always inlined (#880), as profile-guided builds do; hot callers: check_stream_value_to and check_action_operand_value.
static inline __attribute__((always_inline)) void check_value_copy(CheckValue* destination, const CheckValue* source)
{
#if BUSTER_HOST_VECTOR
    uint8_t* bytes = (uint8_t*)destination;
    const uint8_t* from = (const uint8_t*)source;
    _mm512_storeu_si512(bytes, _mm512_loadu_si512(from));
    _mm256_storeu_si256((__m256i*)(bytes + 64), _mm256_loadu_si256((const __m256i*)(from + 64)));
#else
    *destination = *source;
#endif
}

// A value's two x87 payloads as their ten meaningful bytes, the significand and the sign and exponent, copied as
// integer storage: an x87 load and store of one is microcoded, and the six pad bytes needn't be read (#879). The
// helper zeroes the destination's pad bytes itself, so its padding is deterministic whatever its caller left there.
static void check_value_copy_floating(CheckValue* destination, const CheckValue* source)
{
    destination->floating_storage[0] = source->floating_storage[0];
    destination->floating_storage[1] = source->floating_storage[1] & UINT64_C(0xffff);
    destination->imaginary_storage[0] = source->imaginary_storage[0];
    destination->imaginary_storage[1] = source->imaginary_storage[1] & UINT64_C(0xffff);
}

#include "c_check_quad.c"

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
    // A pointer converted to a pointer whose pointee's address space doesn't include its own's (#1447): clang's "changes
    // address space of pointer", worded by the conversion (c_check_semantic_diagnostic.c).
    CHECK_DIAG_ADDRESS_SPACE,
    // A pointer subtraction's or a conditional's pointers to address spaces neither of which includes the other (#1447).
    CHECK_DIAG_ADDRESS_SPACE_OVERLAP,
    // A pointer to pointers whose levels' address spaces differ (#1447): clang's "changes address space of nested
    // pointer", worded by the conversion (c_check_semantic_diagnostic.c).
    CHECK_DIAG_ADDRESS_SPACE_NESTED,
    // A signed _BitInt past 128 bits as one of mul_overflow's operands or its destination (#1447): clang's
    // "__builtin_mul_overflow does not support 'signed _BitInt' operands of more than 128 bits", at that argument.
    CHECK_DIAG_OVERFLOW_BIT_INT,
    // A use of a counted_by or counted_by_or_null pointer member whose pointee type is incomplete there (#1842's 3d,
    // clang's BoundsSafetyCheckUseOfCountAttrPtr and its assignment's check): "cannot use 'b->p' with '__counted_by'
    // attributed type ...", at the member's expression, or at an assignment to it "cannot assign to 'B::p' ...", worded
    // with its two notes by c_check_semantic_diagnostic.c (check_graph_counted_uses).
    CHECK_DIAG_COUNTED_INCOMPLETE,
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
// The destination is an _Atomic object (#1447): an assignment's, an initialization's or initializer element's, a return's
// or a prototype argument's. The conversion is to its value type, so this says what the type no longer does, for the
// conversion value warnings, which clang gives no atomic destination (c_check_common_warnings.c).
#define BUSTER_CHECK_CONVERT_ATOMIC 0x200u
// GCC's cast to a union (#2301, clang's CK_ToUnion): the conversion's type is the member the operand initializes, the
// first of the operand's unqualified type, and the cast's value is a union whose first bytes are that member's.
#define BUSTER_CHECK_CONVERT_TO_UNION 0x400u
// No conversion: a builtin's argument that didn't convert to its parameter, the conversion's type (#2460,
// check_builtin_misc_convert), for the error's words (check_driver_builtin_conversion). Only a unit in error has one.
#define BUSTER_CHECK_CONVERT_FAILED 0x800u

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
// No warning: an arithmetic, comparison or conditional operator has an operand of an enumeration type, whatever the
// operand's form (a member, a call, a cast, a subscript...). check_graph_store moves it from the result to its stream's
// enum_operands, the gate of the enumeration-mix scan (check_common_enum_arithmetic), so no warnings column holds it.
#define CHECK_WARNING_ENUM_OPERAND 0x80000000u

typedef struct CheckConversion CheckConversion;
struct CheckConversion
{
    TypeId type;
    uint32_t flags;
};

typedef struct CheckAction CheckAction;
typedef struct CheckVectorMember CheckVectorMember;
struct CheckConstValues;

// What an action read of its source position (#1050): each tag whose completion it asked about, with the answer (bit i of
// `answers`, tag i complete there), for a memoized transition (c_check_transitions.c) to ask again at another node.
// `unknown` marks a read the guards can't hold: more tags than they have room for, or a transparent union's activation.
#define BUSTER_CHECK_ACTION_GUARDS 4u
typedef struct CheckActionGuards CheckActionGuards;
struct CheckActionGuards
{
    uint32_t tags[BUSTER_CHECK_ACTION_GUARDS];
    uint32_t count;
    uint32_t answers;
    bool unknown;
};
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
    CheckContext context;
    const CheckVectorMember* vector_member;
    // Optional: where the action records the answers it read of the source position, which only the checker's memoized
    // transitions ask for (c_check_transitions.c). Null records nothing.
    CheckActionGuards* guards;
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
static uint32_t check_complex_fold(const TypeTable* table, CheckOp op, const CheckValue* left, const CheckValue* right, uint32_t domains, CheckValue* value,
                                   uint32_t node, uint32_t extensions, bool constant_evaluated);
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

// Records that the action read tag `tag`'s completion at its source position, and the answer (CheckAction.guards).
static void check_action_guard(const CheckAction* action, uint32_t tag, bool complete)
{
    CheckActionGuards* guards = action->guards;
    if (guards != NULL)
    {
        bool found = false;
        for (uint32_t i = 0; i < guards->count; i += 1)
        {
            found |= guards->tags[i] == tag;
        }
        const bool room = guards->count < BUSTER_CHECK_ACTION_GUARDS;
        if (!found && room)
        {
            guards->tags[guards->count] = tag;
            guards->answers |= (uint32_t)complete << guards->count;
            guards->count += 1;
        }
        guards->unknown |= !found && !room;
    }
}

// Records that the action read a transparent union's activation at its source position, which no guard holds.
static void check_action_guard_unknown(const CheckAction* action)
{
    if (action->guards != NULL && action->context.transparent_unions != NULL)
    {
        action->guards->unknown = true;
    }
}

static bool check_action_source_complete(const TypeTable* table, TypeId type, const CheckAction* action)
{
    const bool complete = check_context_source_complete(table, type, &action->context);
    // The memoized transitions' guard (CheckAction.guards): the nominal tag whose completion at the source position
    // check_context_source_complete read, and its answer. A tag past the history is incomplete wherever the action is.
    if (action->guards != NULL && action->context.tag_completions != NULL && type_is_complete(table, type))
    {
        TypeId base = type_strip_aligned(table, type);
        while (type_kind_is_array(type_kind(table, base)))
        {
            base = type_strip_aligned(table, type_element(table, base));
        }
        const uint8_t kind = type_kind(table, base);
        const uint32_t tag = table->types.index[type_index(base)];
        if ((kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION || kind == BUSTER_TYPE_KIND_ENUM) && tag < action->context.tag_completion_count)
        {
            check_action_guard(action, tag, complete);
        }
    }
    return complete;
}

// An enumeration's type is rare among the operands (#1175).
static bool check_action_integer_type(const TypeTable* table, TypeId type, uint32_t* width, bool* is_signed)
{
    TypeId base = type_unqualified(type_strip_aligned(table, type));
    if (__builtin_expect(type_kind(table, base) == BUSTER_TYPE_KIND_ENUM, 0) && type_is_complete(table, base))
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

// _BitInt constants past 128 bits (#1447, c_check_wide.c): the cold branches that the gatekeeper above leaves to them.
static bool check_wide_truth(const TypeTable* table, const CheckValue* value, bool* truth);
static bool check_wide_convert(const TypeTable* table, CheckValue* value, TypeId destination, uint32_t node);
static bool check_wide_type(const TypeTable* table, TypeId type, uint32_t* width, bool* is_signed);
static uint32_t check_wide_count(uint32_t width);
static uint64_t* check_wide_allocate(uint32_t count);
static void check_wide_publish(CheckValue* value, const uint64_t* limbs);
static const uint64_t* check_wide_limbs(const CheckValue* value, uint32_t count);

static bool check_action_is_integer(const TypeTable* table, TypeId type)
{
    const uint8_t kind = check_action_kind(table, type);
    return kind == BUSTER_TYPE_KIND_INTEGER || kind == BUSTER_TYPE_KIND_BIT_INT || kind == BUSTER_TYPE_KIND_ENUM;
}

// Always inlined, as it was before #1447 grew the type readers it inlines past clang's threshold.
static inline __attribute__((always_inline)) bool check_action_is_arithmetic(const TypeTable* table, TypeId type)
{
    const uint8_t kind = check_action_kind(table, type);
    return check_action_is_integer(table, type) || kind == BUSTER_TYPE_KIND_FLOATING || kind == BUSTER_TYPE_KIND_COMPLEX;
}

static bool check_action_is_scalar(const TypeTable* table, TypeId type)
{
    const uint8_t kind = check_action_kind(table, type);
    return check_action_is_arithmetic(table, type) || kind == BUSTER_TYPE_KIND_POINTER || kind == BUSTER_TYPE_KIND_NULLPTR;
}

// The first diagnostic stays. One of an operand to be named (UINT32_MAX: a conversion's, whose callers know the
// operand it converts) takes the next one's operand.
static void check_action_diagnose(CheckActionResult* result, CheckDiagnostic diagnostic, uint32_t operand)
{
    if (result->diagnostic == CHECK_DIAG_NONE)
    {
        result->diagnostic = diagnostic;
        result->diagnostic_operand = operand;
    }
    else if (result->diagnostic_operand == UINT32_MAX)
    {
        result->diagnostic_operand = operand;
    }
}

// A builtin's wrong argument count (#1860), the count clang names: `expected`, the bound the call breaks (0x3f at most), with
// `bound` where clang words it as one ("expected at least" for too few, "at most" for too many). The operand carries
// them, with 0x80 that marks it present, for the driver's words (check_driver_call_count). A count past 0x3f keeps the
// generic operand.
static void check_action_diagnose_expected(CheckActionResult* result, uint32_t expected, bool bound)
{
    const uint32_t operand = expected <= 0x3fu ? 0x80u | (bound ? 0x40u : 0u) | expected : 0u;
    check_action_diagnose(result, CHECK_DIAG_ARGUMENT_COUNT, operand);
}

// `count` arguments where the builtin takes `minimum` to `maximum` (UINT32_MAX for no bound), as clang's checkArgCountRange
// and checkArgCountAtLeast word them: too few is "expected at least" only where there is no maximum, too many "at most"
// where the two differ.
static void check_action_diagnose_count(CheckActionResult* result, uint32_t count, uint32_t minimum, uint32_t maximum)
{
    const bool few = count < minimum;
    check_action_diagnose_expected(result, few ? minimum : maximum, few ? maximum == UINT32_MAX : minimum != maximum);
}

#include "c_check_provenance.c"

static bool check_action_truth(const TypeTable* table, const CheckValue* value, bool* truth)
{
    bool known = value->fault == CHECK_FAULT_NONE;
    if (value->constant_kind == CHECK_CONSTANT_INTEGER)
    {
        uint32_t width = 0;
        bool is_signed = false;
        const bool integer = check_action_integer_type(table, value->type, &width, &is_signed);
        *truth = integer && (value->integer & check_action_mask(width)) != 0;
        if (__builtin_expect(!integer, 0))
        {
            known &= check_wide_truth(table, value, truth);
        }
    }
    else if (value->constant_kind == CHECK_CONSTANT_FLOATING)
    {
        *truth = value->floating != 0;
    }
    else if (value->constant_kind == CHECK_CONSTANT_COMPLEX)
    {
        const TypeId element = type_element(table, type_strip_aligned(table, value->type));
        // A _Complex __float128's parts are its bits (check_quad_complex_set): true where either is no zero.
        const bool quad = type_unqualified(type_strip_aligned(table, element)) == BUSTER_TYPE_FLOAT128;
        *truth = check_action_is_integer(table, element) ? value->integer != 0 :
                 quad ? ((value->integer | check_quad_complex_imaginary(value)) & ~BUSTER_CHECK_QUAD_SIGN) != 0 :
                        value->floating != 0 || value->imaginary != 0;
    }
    else if (value->constant_kind == CHECK_CONSTANT_ADDRESS)
    {
        // Absolute addresses, including null, are independent of final placement. An rvalue address of a non-weak
        // object or function is non-null, including a displacement that wraps in 64 bits: clang folds (_Bool)&s and
        // (_Bool)((char*)&s + (1ul << 63)) to 1. The flag stays on the lvalue too, but a failed value load must not
        // become "true" just because the object exists. A weak symbol is not a constant.
        const bool nonnull = (value->flags & BUSTER_CHECK_NONNULL_ADDRESS) != 0 && value->category != CHECK_LVALUE;
        *truth = nonnull || value->offset != 0;
        known &= value->object == 0 || nonnull;
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

// The half-precision formats: _Float16's and __fp16's IEEE binary16 and __bf16's bfloat16. A constant of one is folded
// where its value is exact or rounded once: a conversion, a negation, a unary + and a compare. Required constant
// evaluation also rounds arithmetic after each operation, as clang's APFloat evaluator does. The runtime graph must
// not fold that arithmetic: clang computes its chains in float (excess precision, c_check_program_half.c), which a
// constant rounded per operation wouldn't be.
typedef enum CheckHalf
{
    CHECK_HALF_NONE,
    CHECK_HALF_IEEE,
    CHECK_HALF_BRAIN,
} CheckHalf;

// An unqualified, unaligned type's half-precision format.
static CheckHalf check_action_half_format(TypeId base)
{
    return base == BUSTER_TYPE_FLOAT16 || base == BUSTER_TYPE_HALF ? CHECK_HALF_IEEE : base == BUSTER_TYPE_BF16 ? CHECK_HALF_BRAIN : CHECK_HALF_NONE;
}

// The bits of a format's value nearest `significand` * 2^`exponent` (ties to even), of sign `negative`: a subnormal's
// below its least normal, an infinity's past its largest, as clang's APFloat rounds a constant once, from its exact value
// (an integer's too: a 128-bit one's through a long double could round twice).
static uint16_t check_action_half_round(CheckHalf format, bool negative, unsigned __int128 significand, int32_t exponent)
{
    const uint32_t precision = format == CHECK_HALF_IEEE ? 11 : 8;
    const int32_t least = format == CHECK_HALF_IEEE ? -14 : -126;
    const int32_t greatest = format == CHECK_HALF_IEEE ? 15 : 127;
    uint32_t bits = negative ? 0x8000u : 0;
    if (significand != 0)
    {
        const uint64_t high = (uint64_t)(significand >> 64);
        const int32_t top = high != 0 ? 127 - __builtin_clzll(high) : 63 - __builtin_clzll((uint64_t)significand);
        int32_t binary = top + exponent;
        // The result's last place: a normal value's, or the subnormals' below the least normal.
        const int32_t place = (binary < least ? least : binary) - (int32_t)(precision - 1);
        const int32_t shift = place - exponent;
        unsigned __int128 rounded = 0;
        if (shift <= 0)
        {
            rounded = significand << -shift;
        }
        else if (shift < 128)
        {
            const unsigned __int128 rest = significand & ((((unsigned __int128)1) << shift) - 1);
            const unsigned __int128 half = ((unsigned __int128)1) << (shift - 1);
            rounded = significand >> shift;
            rounded += rest > half || (rest == half && (rounded & 1) != 0) ? 1 : 0;
        }
        else
        {
            // Below half the least subnormal, but at shift 128 past it.
            rounded = shift == 128 && significand > ((unsigned __int128)1 << 127) ? 1 : 0;
        }
        if (binary >= least && rounded == ((unsigned __int128)1 << precision))
        {
            rounded >>= 1;
            binary += 1;
        }
        if (binary >= least && binary > greatest)
        {
            bits |= (uint32_t)(2 * greatest + 1) << (precision - 1);
        }
        else if (binary >= least)
        {
            bits |= ((uint32_t)(binary + greatest) << (precision - 1)) | ((uint32_t)rounded - (1u << (precision - 1)));
        }
        else
        {
            // A subnormal's count of the least place, or the least normal's bits where it rounds up to it.
            bits |= (uint32_t)rounded;
        }
    }
    return (uint16_t)bits;
}

// NaNs (#148). The checker's long double holds a NaN of float, double, long double and the half-precision formats as the
// x87's own loads place one, and as __builtin_nan, __builtin_nans and the arithmetic leave it: the sign, the all-ones
// exponent, the integer bit and the format's fraction at the significand's top (a float's 23 bits from bit 62, a
// double's 52, a _Float16's 10, a long double's 63), so that bit 62 is every format's quiet bit and a signaling NaN keeps
// its bits until a conversion to another format quiets it (check_action_convert_nan). A NaN's bits in a format of
// `fraction` fraction bits and `exponent` exponent bits are its fraction's top bits; a fraction that narrowing would
// empty is quiet, as APFloat's conversion leaves a NaN. For the formats of 64 bits or fewer (float, double, _Float16 and
// __bf16: fraction + exponent below 64); a long double's NaN is its significand as it is, and a __float128's its bits
// (c_check_quad.c), whose mirror holds its top 63 fraction bits here.
static uint64_t check_action_nan_bits(long double value, uint32_t fraction, uint32_t exponent)
{
    uint64_t significand = 0;
    memcpy(&significand, &value, sizeof(significand));
    uint64_t bits = (significand & UINT64_C(0x7fffffffffffffff)) >> (63 - fraction);
    bits = bits != 0 ? bits : UINT64_C(1) << (fraction - 1);
    const uint64_t sign = signbit(value) != 0 ? UINT64_C(1) << (fraction + exponent) : 0;
    return sign | (((UINT64_C(1) << exponent) - 1) << fraction) | bits;
}

// A floating constant converted from `from` to `to`, before its rounding: a NaN converted to another format is quiet, as
// APFloat's conversion makes it, its payload the top bits `to`'s fraction holds (a float's 23, a double's 52, a
// _Float16's 10, a __bf16's 7, a long double's 63), and to its own format keeps its bits (a signaling NaN of __builtin
// _nans's stays one). A __float128 NaN's mirror (c_check_quad.c) holds its fraction's top bits in the others' place, as
// a narrowing keeps them.
static long double check_action_convert_nan(TypeId from, TypeId to, long double value)
{
    long double result = value;
    if (isnan(value) && from != to)
    {
        const CheckHalf half = check_action_half_format(to);
        const uint32_t fraction = to == BUSTER_TYPE_FLOAT ? 23 : to == BUSTER_TYPE_DOUBLE ? 52 : half == CHECK_HALF_IEEE ? 10 : half == CHECK_HALF_BRAIN ? 7 : 63;
        uint64_t significand = 0;
        memcpy(&significand, &result, sizeof(significand));
        significand &= ~((UINT64_C(1) << (63 - fraction)) - 1);
        significand |= UINT64_C(1) << 62;
        memcpy(&result, &significand, sizeof(significand));
    }
    return result;
}

// A floating value's bits in a half-precision format: its sign with its NaN's bits (check_action_nan_bits, its payload
// kept) for a NaN, its infinity, or its value rounded.
static uint16_t check_action_half_bits(CheckHalf format, long double value)
{
    const bool negative = signbit(value) != 0;
    const uint32_t sign = negative ? 0x8000u : 0;
    uint16_t bits = (uint16_t)sign;
    if (isnan(value))
    {
        bits = (uint16_t)(format == CHECK_HALF_IEEE ? check_action_nan_bits(value, 10, 5) : check_action_nan_bits(value, 7, 8));
    }
    else if (isinf(value))
    {
        bits = (uint16_t)(sign | (format == CHECK_HALF_IEEE ? 0x7c00u : 0x7f80u));
    }
    else if (value != 0)
    {
        int binary = 0;
        const long double fraction = host_frexpl(fabsl(value), &binary);
        bits = check_action_half_round(format, negative, (uint64_t)host_ldexpl(fraction, 64), binary - 64);
    }
    return bits;
}

// A format's bits' value, exactly (a float's too). Decoding a NaN preserves its representation: semantic
// conversions and arithmetic decide whether to quiet it, not a vector lane's read from constant bytes (#1598).
static long double check_action_half_value(CheckHalf format, uint16_t bits)
{
    _Static_assert(sizeof(long double) == 16 && LDBL_MANT_DIG == 64 && LDBL_MAX_EXP == 16384, "the checker host uses x87 extended precision");
    const uint32_t precision = format == CHECK_HALF_IEEE ? 11 : 8;
    const uint32_t greatest = format == CHECK_HALF_IEEE ? 15 : 127;
    const uint32_t mantissa = bits & ((1u << (precision - 1)) - 1);
    const uint32_t field = (bits & 0x7fffu) >> (precision - 1);
    long double result = 0;
    if (field == 2 * greatest + 1 && mantissa != 0)
    {
        // The fraction occupies the x87 significand's top bits, as check_action_nan_bits reads it. Construct
        // even the sign from bytes: a host floating conversion or arithmetic must not quiet a signaling NaN.
        const uint64_t significand = UINT64_C(0x8000000000000000) | ((uint64_t)mantissa << (64 - precision));
        const uint16_t exponent = (uint16_t)(0x7fffu | (bits & 0x8000u));
        uint8_t bytes[16] = { 0 };
        memcpy(bytes, &significand, sizeof(significand));
        memcpy(bytes + 8, &exponent, sizeof(exponent));
        memcpy(&result, bytes, sizeof(result));
    }
    else
    {
        const long double magnitude = field == 2 * greatest + 1 ? __builtin_infl() :
                                      field == 0                ? host_ldexpl((long double)mantissa, 1 - (int)greatest - (int)(precision - 1)) :
                                                                  host_ldexpl((long double)(mantissa | (1u << (precision - 1))), (int)field - (int)greatest - (int)(precision - 1));
        result = (bits & 0x8000u) != 0 ? -magnitude : magnitude;
    }
    return result;
}

static long double check_action_round_float(TypeId type, long double value)
{
    // A NaN's rounding is its bits' (check_action_nan_bits): the x87's rounding would quiet a signaling one.
    long double result = value;
    if (type == BUSTER_TYPE_FLOAT && !isnan(value))
    {
        result = (float)value;
    }
    else if (type == BUSTER_TYPE_DOUBLE && !isnan(value))
    {
        result = (double)value;
    }
    else if (check_action_half_format(type) != CHECK_HALF_NONE && !isnan(value))
    {
        result = check_action_half_value(check_action_half_format(type), check_action_half_bits(check_action_half_format(type), value));
    }
    return result;
}

// A constant's conversion to a half-precision type, which float, double and long double's folding doesn't take:
// an integer's exact magnitude rounded once, or a floating value rounded; false, and the value as it was, for any other
// destination. Out of line, so that check_action_convert_plain's other conversions keep their registers.
static __attribute__((noinline)) bool check_action_convert_half(const TypeTable* table, CheckValue* value, TypeId destination)
{
    uint32_t source_width = 0;
    bool source_signed = false;
    const bool source_integer = check_action_integer_type(table, value->type, &source_width, &source_signed);
    const TypeId base = type_unqualified(type_strip_aligned(table, destination));
    const CheckHalf half = check_action_half_format(base);
    const bool integer = half != CHECK_HALF_NONE && value->constant_kind == CHECK_CONSTANT_INTEGER && source_integer;
    const bool floating = half != CHECK_HALF_NONE && value->constant_kind == CHECK_CONSTANT_FLOATING;
    if (integer)
    {
        const unsigned __int128 mask = check_action_mask(source_width);
        const unsigned __int128 bits = value->integer & mask;
        const bool negative = source_signed && (bits >> (source_width - 1)) != 0;
        value->floating = check_action_half_value(half, check_action_half_round(half, negative, negative ? (0 - bits) & mask : bits, 0));
        value->constant_kind = CHECK_CONSTANT_FLOATING;
    }
    else if (floating)
    {
        const long double converted = check_action_convert_nan(type_unqualified(type_strip_aligned(table, value->type)), base, value->floating);
        value->floating = check_action_round_float(base, converted);
    }
    return integer || floating;
}

// A __float128 constant converted to an integer type of `width` bits (#1250): truncated toward zero from its bits, or,
// past the type's range or a NaN, saturated as check_action_convert_plain's other floating sources are. Its least value
// below -2^(width-1) is -2^(width-1) - 1 where that's a binary128 value (a width to 112), and -2^(width-1) itself
// otherwise, as no binary128 value lies between the two there.
static __attribute__((noinline)) void check_action_convert_quad_integer(CheckValue* value, uint32_t width, bool is_signed)
{
    const unsigned __int128 high_bit = (unsigned __int128)1 << (width - 1);
    const __float128 limit = (__float128)high_bit * (is_signed ? 1 : 2);
    const __float128 minimum = is_signed ? -limit : 0;
    const bool nan = check_quad_is_nan(value->integer);
    const __float128 input = nan ? 0 : check_quad_host(value->integer);
    const bool lower = width > 112 && is_signed ? input >= minimum : input > minimum - 1;
    const bool infinite = (value->integer & ~BUSTER_CHECK_QUAD_SIGN) == BUSTER_CHECK_QUAD_EXPONENT;
    if (nan || infinite || !lower || input >= limit)
    {
        const bool negative = (value->integer & BUSTER_CHECK_QUAD_SIGN) != 0;
        value->integer = nan ? 0 : negative ? (is_signed ? high_bit : 0) : (is_signed ? high_bit - 1 : check_action_mask(width));
        value->flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM);
        value->flags |= BUSTER_CHECK_GNU_CONSTANT | BUSTER_CHECK_GNU_FLOAT_RANGE;
    }
    else
    {
        value->integer = input < 0 ? (unsigned __int128)(__int128)input : (unsigned __int128)input;
        value->integer &= check_action_mask(width);
    }
    value->constant_kind = CHECK_CONSTANT_INTEGER;
}

// Convert a constant's representation after type checking has authorized the conversion. Every integer operation
// uses unsigned bits, and every floating-to-integer cast is range checked before host C is allowed to evaluate it.
static void check_action_convert_plain(const TypeTable* table, CheckValue* value, TypeId destination, uint32_t node)
{
    const TypeId source = value->type;
    const uint8_t target_kind = check_action_kind(table, destination);
    const bool source_pointer = check_action_kind(table, source) == BUSTER_TYPE_KIND_POINTER;
    const bool target_pointer = target_kind == BUSTER_TYPE_KIND_POINTER;
    if (source_pointer || target_pointer)
    {
        const TypeId from = source_pointer ? type_unqualified(type_strip_aligned(table, type_element(table, source))) : 0;
        const TypeId to = target_pointer ? type_unqualified(type_strip_aligned(table, type_element(table, destination))) : 0;
        value->flags |= source_pointer && target_pointer && from == to ? 0 : BUSTER_CHECK_CONST_READ_RESTRICTED;
    }
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
    else if (__builtin_expect(value->constant_kind != CHECK_CONSTANT_NONE &&
                              ((!target_integer && target_kind == BUSTER_TYPE_KIND_BIT_INT) ||
                               (!source_integer && check_action_kind(table, source) == BUSTER_TYPE_KIND_BIT_INT)), 0) &&
             check_wide_convert(table, value, destination, node))
    {
        // A _BitInt past 128 bits on either side (#1447, c_check_wide.c).
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
    else if (target_integer && value->constant_kind == CHECK_CONSTANT_FLOATING && type_unqualified(type_strip_aligned(table, source)) == BUSTER_TYPE_FLOAT128)
    {
        check_action_convert_quad_integer(value, target_width, target_signed);
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
    else if (target_kind == BUSTER_TYPE_KIND_FLOATING &&
             type_unqualified(type_strip_aligned(table, destination)) == BUSTER_TYPE_FLOAT128 &&
             (value->constant_kind == CHECK_CONSTANT_INTEGER || value->constant_kind == CHECK_CONSTANT_FLOATING))
    {
        // A __float128 constant (#1250, c_check_quad.c): an integer's magnitude rounded once, another format's value
        // exactly, its NaN quieted with its payload at the fraction's top, as APFloat converts them; a __float128's own
        // bits unchanged.
        const TypeId from = type_unqualified(type_strip_aligned(table, source));
        if (value->constant_kind == CHECK_CONSTANT_INTEGER && source_integer)
        {
            const unsigned __int128 mask = check_action_mask(source_width);
            const unsigned __int128 bits = value->integer & mask;
            const bool negative = source_signed && (bits >> (source_width - 1)) != 0;
            check_quad_set(value, check_quad_from_integer(negative, negative ? (0 - bits) & mask : bits));
        }
        else if (value->constant_kind == CHECK_CONSTANT_FLOATING && from != BUSTER_TYPE_FLOAT128)
        {
            check_quad_set(value, check_quad_widen(value->floating, true));
        }
        else if (value->constant_kind != CHECK_CONSTANT_FLOATING)
        {
            check_action_set_fault(value, CHECK_FAULT_UNSUPPORTED_CONSTANT, node);
        }
    }
    else if (target_kind == BUSTER_TYPE_KIND_FLOATING && value->constant_kind == CHECK_CONSTANT_FLOATING &&
             type_unqualified(type_strip_aligned(table, source)) == BUSTER_TYPE_FLOAT128 &&
             type_unqualified(type_strip_aligned(table, destination)) == BUSTER_TYPE_LONG_DOUBLE)
    {
        // A __float128 narrowed to long double, rounded once from its bits; to the narrower formats its mirror rounds
        // once more as the exact value would (c_check_quad.c), below.
        value->floating = check_quad_narrow(value->integer);
    }
    else if (target_kind == BUSTER_TYPE_KIND_FLOATING && (value->constant_kind == CHECK_CONSTANT_INTEGER || value->constant_kind == CHECK_CONSTANT_FLOATING))
    {
        if (!check_action_float_supported(table, destination) || (value->constant_kind == CHECK_CONSTANT_INTEGER && !source_integer))
        {
            if (!check_action_convert_half(table, value, destination))
            {
                check_action_set_fault(value, CHECK_FAULT_UNSUPPORTED_CONSTANT, node);
            }
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
            const TypeId to = type_unqualified(type_strip_aligned(table, destination));
            number = check_action_convert_nan(type_unqualified(type_strip_aligned(table, source)), to, number);
            value->floating = check_action_round_float(to, number);
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
    value->flags &= ~(BUSTER_CHECK_BIT_FIELD | BUSTER_CHECK_REGISTER | BUSTER_CHECK_REGISTER_NAME | BUSTER_CHECK_NULL_POINTER);
    if (!target_integer && target_kind != BUSTER_TYPE_KIND_BIT_INT)
    {
        value->flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM);
    }
}

// A vector's or a complex number's conversion is rare (#1175).
static void check_action_convert_value(const TypeTable* table, CheckValue* value, TypeId destination, uint32_t node)
{
    if (__builtin_expect(check_vector_type(table, value->type) || check_vector_type(table, destination), 0))
    {
        check_vector_convert(value, destination);
    }
    else if (__builtin_expect(check_action_kind(table, value->type) == BUSTER_TYPE_KIND_COMPLEX || check_action_kind(table, destination) == BUSTER_TYPE_KIND_COMPLEX, 0))
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

#include "c_check_const_values.c"

// check_action_operand of `operand`, as operand `index` of `action`, whose context and guards alone the conversion reads of
// it: a call's argument takes its call's (check_action_call), with no action of its own to copy the argument into (#1050).
// An operand's decays and its lvalue's exceptions (an incomplete, a volatile or an atomic one, or one the constant
// evaluation reads) are rare, and the hints say so (#1175).
static CheckValue check_action_operand_value(TypeTable* table, const CheckAction* action, const CheckValue* operand, CheckActionResult* result, uint32_t index)
{
    CheckValue value;
    check_value_copy(&value, operand);
    CheckConversion* conversion = &result->conversions[index];
    conversion->type = value.type;
    const TypeId plain = type_strip_aligned(table, value.type);
    const uint8_t kind = type_kind(table, plain);
    if (__builtin_expect(type_kind_is_array(kind), 0))
    {
        if ((value.flags & BUSTER_CHECK_REGISTER_NAME) != 0)
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
            value.flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_BIT_FIELD | BUSTER_CHECK_REGISTER | BUSTER_CHECK_REGISTER_NAME);
            if (!action->context.constant_evaluated && (value.flags & BUSTER_CHECK_STATIC_OBJECT) == 0 && value.constant_kind == CHECK_CONSTANT_ADDRESS)
            {
                value.constant_kind = CHECK_CONSTANT_NONE;
            }
            conversion->type = pointer;
            conversion->flags |= BUSTER_CHECK_CONVERT_ARRAY;
        }
    }
    else if (__builtin_expect(kind == BUSTER_TYPE_KIND_FUNCTION, 0))
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
        if (__builtin_expect(!check_action_source_complete(table, value.type, action), 0) && kind != BUSTER_TYPE_KIND_VOID)
        {
            check_action_diagnose(result, CHECK_DIAG_INCOMPLETE, index);
        }
        const bool volatile_read = (type_qualifiers(value.type) & BUSTER_TYPE_VOLATILE) != 0 ||
                                   (value.flags & BUSTER_CHECK_VOLATILE_PART) != 0;
        CheckValue loaded = { 0 };
        const bool known = __builtin_expect(action->context.constant_evaluated, 0) && !volatile_read && value.fault == CHECK_FAULT_NONE &&
                           (value.flags & (BUSTER_CHECK_INACTIVE_UNION_READ | BUSTER_CHECK_CONST_READ_RESTRICTED)) == 0 &&
                           (value.constant_kind == CHECK_CONSTANT_ADDRESS || (value.flags & BUSTER_CHECK_BIT_FIELD) != 0) &&
                           check_const_load(table, action->context.const_values, value.object, value.offset, value.object_bit_offset,
                                            value.bit_width, value.type, action->context.source_position, &loaded);
        if (known)
        {
            check_action_inherit(&loaded, &value);
            loaded.flags |= value.flags & BUSTER_CHECK_BIT_FIELD;
            loaded.bit_width = value.bit_width;
            value = loaded;
        }
        else if (value.constant_kind == CHECK_CONSTANT_ADDRESS || (value.flags & BUSTER_CHECK_ICE) == 0)
        {
            value.constant_kind = CHECK_CONSTANT_NONE;
        }
        if (__builtin_expect(volatile_read, 0) || (__builtin_expect(kind == BUSTER_TYPE_KIND_ATOMIC, 0) && !known))
        {
            value.flags |= BUSTER_CHECK_SIDE_EFFECT;
            value.constant_kind = CHECK_CONSTANT_NONE;
        }
        value.type = type_conversion_operand(table, value.type);
        value.category = CHECK_RVALUE;
        value.flags &= ~(BUSTER_CHECK_REGISTER | BUSTER_CHECK_REGISTER_NAME | BUSTER_CHECK_VOLATILE_PART);
        conversion->type = value.type;
        conversion->flags |= BUSTER_CHECK_CONVERT_LVALUE;
    }
    return value;
}

static CheckValue check_action_operand(TypeTable* table, const CheckAction* action, CheckActionResult* result, uint32_t index)
{
    return check_action_operand_value(table, action, &action->operands[index], result, index);
}

// Always inlined (#880), as profile-guided builds do; hot caller: check_action_arithmetic.
static inline __attribute__((always_inline)) void check_action_promote(const TypeTable* table, CheckValue* value, CheckConversion* conversion, uint32_t node)
{
    const TypeId promoted = (value->flags & BUSTER_CHECK_BIT_FIELD) != 0 ? type_promote_bit_field(table, value->type, value->bit_width) : type_promote(table, value->type);
    if (promoted != value->type)
    {
        check_action_conversion(table, value, promoted, BUSTER_CHECK_CONVERT_PROMOTION, conversion, node);
    }
}

static TypeId check_action_fp_evaluation_type(uint32_t environment)
{
    return (environment & BUSTER_CHECK_FP_EVAL_EXTENDED) != 0 ? BUSTER_TYPE_LONG_DOUBLE :
           (environment & BUSTER_CHECK_FP_EVAL_DOUBLE) != 0 ? BUSTER_TYPE_DOUBLE : 0;
}

// Keep the ordinary promotion path unchanged for source evaluation. The promoted type is a builtin, including
// its complex counterpart, so this needs no interning or new value payload. Vectors keep their declared element type.
static void check_action_fp_promote(const TypeTable* table, CheckValue* value, CheckConversion* conversion, uint32_t node, uint32_t environment)
{
    const TypeId evaluation = check_action_fp_evaluation_type(environment);
    if (evaluation != 0)
    {
        const TypeId plain = type_unqualified(type_strip_aligned(table, value->type));
        const uint8_t kind = type_kind(table, plain);
        const uint32_t element = type_floating_element(table, plain);
        const uint32_t code = type_index(evaluation);
        if ((kind == BUSTER_TYPE_KIND_FLOATING || kind == BUSTER_TYPE_KIND_COMPLEX) && element != 0 && element < code)
        {
            const TypeId target = kind == BUSTER_TYPE_KIND_COMPLEX ? BUSTER_TYPE_COMPLEX(code) : evaluation;
            check_action_conversion(table, value, target, BUSTER_CHECK_CONVERT_PROMOTION, conversion, node);
        }
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
    check_action_guard_unknown(action);
    const TypeAnswer answer = left == right ? TYPE_ANSWER_YES :
                              type_compatible_at(table, left, right, action->context.transparent_unions, action->context.tag_completion_count, action->context.source_position);
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
// both sides differ only in their levels' qualifiers (char ** to const char **), which it warns about — but an address
// space the target's doesn't include at any level the walk reaches is clang's "changes address space of nested
// pointer" error, whether or not the levels' types otherwise agree.
static bool check_action_nested_qualifiers(const TypeTable* table, TypeId left, TypeId right, CheckActionResult* result, bool strict)
{
    TypeId l = type_strip_aligned(table, left);
    TypeId r = type_strip_aligned(table, right);
    const bool nested = type_kind(table, l) == BUSTER_TYPE_KIND_POINTER && type_kind(table, r) == BUSTER_TYPE_KIND_POINTER;
    bool space_conflict = false;
    while (type_kind(table, l) == BUSTER_TYPE_KIND_POINTER && type_kind(table, r) == BUSTER_TYPE_KIND_POINTER)
    {
        const TypeId le = type_element(table, l);
        const TypeId re = type_element(table, r);
        space_conflict |= !type_space_includes(type_address_space(type_object_qualifiers(table, le)),
                                               type_address_space(type_object_qualifiers(table, re)));
        l = type_strip_aligned(table, le);
        r = type_strip_aligned(table, re);
    }
    const TypeAnswer answer = nested ? type_same_canonical(table, type_unqualified(l), type_unqualified(r)) : TYPE_ANSWER_NO;
    if (answer == TYPE_ANSWER_FULL)
    {
        check_action_diagnose(result, CHECK_DIAG_RESOURCE, 0);
    }
    else if (space_conflict)
    {
        if (!strict)
        {
            check_action_diagnose(result, CHECK_DIAG_ADDRESS_SPACE_NESTED, UINT32_MAX);
        }
    }
    return answer == TYPE_ANSWER_YES && !space_conflict;
}

// check_action_assignable_mode's pointees of other extended qualifiers (#1447), as clang's compatiblyIncludes takes them:
// an address space the target's doesn't include is an error (REFUSED, diagnosed but where `strict`); a garbage
// collection qualifier may be added or dropped, but changed only with the qualifiers' warning, which a void pointee's
// conversion doesn't give. Pointees of one type but those qualifiers are ACCEPTED (clang compares them without them);
// any other conversion goes on as the qualifiers' (SAME).
#define CHECK_EXTENDED_SAME 0u
#define CHECK_EXTENDED_REFUSED 1u
#define CHECK_EXTENDED_ACCEPTED 2u
static __attribute__((noinline)) uint32_t check_action_extended_pointees(const TypeTable* table, TypeId to, TypeId from, uint32_t to_word, uint32_t from_word,
                                                                         bool strict, const CheckAction* action, CheckActionResult* result, uint32_t* warning)
{
    const bool to_void = check_action_kind(table, to) == BUSTER_TYPE_KIND_VOID;
    const bool from_void = check_action_kind(table, from) == BUSTER_TYPE_KIND_VOID;
    const uint32_t to_gc = to_word & BUSTER_TYPE_GC_MASK;
    const uint32_t from_gc = from_word & BUSTER_TYPE_GC_MASK;
    uint32_t verdict = CHECK_EXTENDED_SAME;
    *warning |= to_gc != 0 && from_gc != 0 && to_gc != from_gc && !to_void && !from_void ? CHECK_WARNING_DISCARDED_QUALIFIERS : 0;
    if (!type_space_includes(type_address_space(to_word), type_address_space(from_word)))
    {
        verdict = CHECK_EXTENDED_REFUSED;
        if (!strict)
        {
            check_action_diagnose(result, CHECK_DIAG_ADDRESS_SPACE, UINT32_MAX);
        }
    }
    else if (!to_void && !from_void &&
             check_action_compatible_at(table, type_unqualified(type_strip_aligned(table, to)), type_unqualified(type_strip_aligned(table, from)), action, result))
    {
        verdict = CHECK_EXTENDED_ACCEPTED;
    }
    return verdict;
}

// Pointer assignment as clang's checkPointerTypesForAssignment decides it in every language mode: the pointees'
// qualifiers may only be added, but losing one only warns, and so do pointees that differ only in signedness and
// pointers to pointers whose levels' qualifiers differ. void * converts to and from any object pointer, and to and
// from a function pointer as a pedantic extension, without the qualifier warning. strict takes clang's Compatible
// alone, as transparent-union selection does: no warning, and no function pointer through void *.
// Pointees are explicit so diagnostic rendering can classify an array's implicit decay without interning a type.
// `nested` is optional source wording, not a semantic type property.
static inline __attribute__((always_inline)) bool check_action_pointer_assignable(const TypeTable* table, TypeId to, TypeId from, const CheckAction* action,
                                                                                  CheckActionResult* result, bool strict, bool* nested)
{
    const uint8_t tk = check_action_kind(table, to);
    const uint8_t fk = check_action_kind(table, from);
    const bool function = tk == BUSTER_TYPE_KIND_FUNCTION || fk == BUSTER_TYPE_KIND_FUNCTION;
    uint32_t warning = (type_qualifiers(from) & ~type_qualifiers(to)) == 0 ? 0 : CHECK_WARNING_DISCARDED_QUALIFIERS;
    bool accepted = false;
    bool extension = false;
    if (nested != NULL)
    {
        *nested = false;
    }
    // The pointees' extended qualifiers (#1447), an array's its element's: where they differ, out of line
    // (check_action_extended_pointees).
    const uint32_t to_word = type_object_qualifiers(table, to);
    const uint32_t from_word = type_object_qualifiers(table, from);
    const uint32_t extended = __builtin_expect(to_word != from_word, 0) ?
                              check_action_extended_pointees(table, to, from, to_word, from_word, strict, action, result, &warning) : CHECK_EXTENDED_SAME;
    if (extended == CHECK_EXTENDED_REFUSED)
    {
    }
    else if (extended == CHECK_EXTENDED_ACCEPTED)
    {
        accepted = true;
    }
    else if (tk == BUSTER_TYPE_KIND_VOID || fk == BUSTER_TYPE_KIND_VOID)
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
        // In C the signedness warning takes precedence over a compatible qualifier discard.
        warning = CHECK_WARNING_POINTER_SIGN;
    }
    else if (result->diagnostic == CHECK_DIAG_NONE && check_action_nested_qualifiers(table, to, from, result, strict))
    {
        accepted = true;
        warning = CHECK_WARNING_DISCARDED_QUALIFIERS;
        if (nested != NULL)
        {
            *nested = true;
        }
    }
    accepted = accepted && !(strict && (warning != 0 || extension));
    result->warnings |= accepted ? warning : 0;
    return accepted;
}

// Always inlined (#880), as profile-guided builds do; hot caller: check_action_assignable.
static inline __attribute__((always_inline)) bool check_action_assignable_mode(const TypeTable* table, TypeId destination, const CheckValue* source, const CheckAction* action, CheckActionResult* result, bool strict)
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
    else if (target_kind == BUSTER_TYPE_KIND_POINTER && check_action_null(table, source, action->context.extensions))
    {
        accepted = true;
    }
    else if (target_kind == BUSTER_TYPE_KIND_POINTER && input_kind == BUSTER_TYPE_KIND_POINTER)
    {
        accepted = check_action_pointer_assignable(table, type_element(table, target), type_element(table, input), action, result, strict, NULL);
    }
    else if ((target_kind == BUSTER_TYPE_KIND_STRUCT || target_kind == BUSTER_TYPE_KIND_UNION || target_kind == BUSTER_TYPE_KIND_NULLPTR ||
              target_kind == BUSTER_TYPE_KIND_ATOMIC) && target_kind == input_kind)
    {
        accepted = check_action_compatible_at(table, target, input, action, result);
    }
    if (!accepted && !strict && (action->context.extensions & BUSTER_CHECK_EXTENSION_SYSTEM_HEADER) != 0 && result->diagnostic == CHECK_DIAG_NONE &&
        !check_vector_type(table, target) && !check_vector_type(table, input))
    {
        accepted = (target_kind == BUSTER_TYPE_KIND_POINTER && (input_kind == BUSTER_TYPE_KIND_POINTER || check_action_is_integer(table, input))) ||
                   (input_kind == BUSTER_TYPE_KIND_POINTER && check_action_is_integer(table, target));
    }
    return accepted;
}

// Always inlined (#880), as profile-guided builds do; hot callers: check_action_assignment and check_action_call.
static inline __attribute__((always_inline)) bool check_action_assignable(const TypeTable* table, TypeId destination, const CheckValue* source, const CheckAction* action, CheckActionResult* result)
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

#include "c_check_wide.c"

// A __float128 operation's bits (#1250): the host's binary128 arithmetic, rounded once to nearest, ties to even, as
// APFloat's, with its NaNs (check_action_fold_nan's rule): the first NaN operand's bits, quieted, or for an invalid
// operation on numbers the positive canonical NaN. `op` is +, -, * or /, the only ones C applies to a real floating
// type.
static __attribute__((noinline)) unsigned __int128 check_quad_arithmetic(CheckOp op, unsigned __int128 left, unsigned __int128 right)
{
    unsigned __int128 result = 0;
    if (check_quad_is_nan(left) || check_quad_is_nan(right))
    {
        result = (check_quad_is_nan(left) ? left : right) | BUSTER_CHECK_QUAD_QUIET;
    }
    else
    {
        const __float128 a = check_quad_host(left);
        const __float128 b = check_quad_host(right);
        const __float128 value = op == CHECK_OP_ADD ? a + b : op == CHECK_OP_SUBTRACT ? a - b : op == CHECK_OP_MULTIPLY ? a * b : a / b;
        result = check_quad_from_host(value);
        result = check_quad_is_nan(result) ? BUSTER_CHECK_QUAD_EXPONENT | BUSTER_CHECK_QUAD_QUIET : result;
    }
    return result;
}

// A folded operation's NaN as clang's APFloat makes it, in every floating format (#1473): the first NaN operand's sign
// and payload, quieted, or for an invalid operation on non-NaNs (inf + -inf, 0 * inf, 0 / 0, inf / inf) the positive
// canonical NaN. The host's SSE and x87 give their negative indefinite NaN instead, and may take either NaN operand.
static long double check_action_fold_nan(const CheckValue* a, const CheckValue* b)
{
    long double result = isnan(a->floating) ? a->floating : isnan(b->floating) ? b->floating : __builtin_nanl("");
    uint64_t significand = 0;
    memcpy(&significand, &result, sizeof(significand));
    significand |= UINT64_C(1) << 62;
    memcpy(&result, &significand, sizeof(significand));
    return result;
}

static void check_action_fold_floating(CheckOp op, const CheckValue* a, const CheckValue* b, CheckValue* value, uint32_t node, bool constant_evaluated)
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
    // A unary + or - of a NaN, or of a __float128 constant, is its sign alone, as APFloat's changeSign makes it:
    // converting a NaN to its format first would quiet a signaling one. A __float128's sign is its bits' and its
    // mirror's.
    const bool quad = a->type == BUSTER_TYPE_FLOAT128;
    const bool sign = (op == CHECK_OP_POSITIVE || op == CHECK_OP_NEGATIVE) && (isnan(a->floating) || quad);
    unsigned __int128 quad_bits = a->integer;
    if (sign)
    {
        uint8_t bytes[16] = { 0 };
        memcpy(bytes, &a->floating, 10);
        bytes[9] ^= op == CHECK_OP_NEGATIVE ? 0x80u : 0;
        memcpy(&number, bytes, sizeof(number));
        quad_bits ^= op == CHECK_OP_NEGATIVE ? BUSTER_CHECK_QUAD_SIGN : 0;
    }
    else if (quad && integer)
    {
        // A __float128 comparison, of its bits as the host's binary128 (an unordered NaN compares false but for !=).
        const bool unordered = check_quad_is_nan(a->integer) || check_quad_is_nan(b->integer);
        const __float128 left = unordered ? 0 : check_quad_host(a->integer);
        const __float128 right = unordered ? 0 : check_quad_host(b->integer);
        switch (op)
        {
            case CHECK_OP_LESS: number = !unordered && left < right; break;
            case CHECK_OP_LESS_EQUAL: number = !unordered && left <= right; break;
            case CHECK_OP_GREATER: number = !unordered && left > right; break;
            case CHECK_OP_GREATER_EQUAL: number = !unordered && left >= right; break;
            case CHECK_OP_EQUAL: number = !unordered && left == right; break;
            default: number = unordered || left != right; break;
        }
    }
    else if (quad && op >= CHECK_OP_ADD && op <= CHECK_OP_DIVIDE)
    {
        quad_bits = check_quad_arithmetic(op, a->integer, b->integer);
    }
    else if (a->type == BUSTER_TYPE_FLOAT)
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
    else if (check_action_half_format(a->type) != CHECK_HALF_NONE && (op == CHECK_OP_POSITIVE || op == CHECK_OP_NEGATIVE || integer || constant_evaluated))
    {
        // Only required constant evaluation rounds each arithmetic node to its half format. Runtime chains keep
        // their excess precision. x87's precision and exponent range suffice for both formats' finite operations.
        BUSTER_CHECK_REAL_OPERATION(long double);
        number = integer ? number : check_action_round_float(a->type, number);
    }
    else
    {
        supported = false;
    }
#undef BUSTER_CHECK_REAL_OPERATION
    if (supported && !sign && !integer && isnan(number))
    {
        number = check_action_fold_nan(a, b);
    }
    if (!supported)
    {
        check_action_set_fault(value, CHECK_FAULT_UNSUPPORTED_CONSTANT, node);
    }
    else if (integer)
    {
        value->constant_kind = CHECK_CONSTANT_INTEGER;
        value->integer = number != 0;
    }
    else if (quad)
    {
        check_quad_set(value, quad_bits);
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
        result->flags |= BUSTER_CHECK_STATIC_OBJECT | (address->flags & BUSTER_CHECK_NONNULL_ADDRESS);
        result->flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM);
    }
    return valid;
}

static void check_action_arithmetic(TypeTable* table, const CheckAction* action, CheckActionResult* result, TypeId common_hint, CheckIntegerFold* pending, bool compound)
{
    const CheckOp op = action->op;
    const bool unary = op == CHECK_OP_POSITIVE || op == CHECK_OP_NEGATIVE || op == CHECK_OP_BIT_NOT;
    const bool shifts = op == CHECK_OP_SHIFT_LEFT || op == CHECK_OP_SHIFT_RIGHT;
    const bool bits = op == CHECK_OP_BIT_AND || op == CHECK_OP_BIT_OR || op == CHECK_OP_BIT_XOR || op == CHECK_OP_REMAINDER || op == CHECK_OP_BIT_NOT || shifts;
    const bool comparison = op >= CHECK_OP_LESS && op <= CHECK_OP_NOT_EQUAL;
    CheckValue left = check_action_operand(table, action, result, 0);
    CheckValue right = unary ? left : check_action_operand(table, action, result, 1);
    if (__builtin_expect(check_action_kind(table, left.type) == BUSTER_TYPE_KIND_ATOMIC, 0) && !unary && !shifts && !comparison)
    {
        // An atomic rvalue, a cast's (#2230): clang's UsualArithmeticConversions take the left operand's value type, but
        // not the right's, whose atomic type the operator refuses.
        check_action_conversion(table, &left, type_conversion_operand(table, left.type), BUSTER_CHECK_CONVERT_ARITHMETIC, &result->conversions[0], action->node);
    }
    const bool left_ok = bits ? check_action_is_integer(table, left.type) : check_action_is_arithmetic(table, left.type);
    const bool right_ok = bits ? check_action_is_integer(table, right.type) : check_action_is_arithmetic(table, right.type);
    result->warnings |= check_action_kind(table, left.type) == BUSTER_TYPE_KIND_ENUM || check_action_kind(table, right.type) == BUSTER_TYPE_KIND_ENUM ?
                        CHECK_WARNING_ENUM_OPERAND : 0;
    if (!left_ok || !right_ok)
    {
        check_action_diagnose(result, bits ? CHECK_DIAG_INTEGER_REQUIRED : CHECK_DIAG_ARITHMETIC_REQUIRED, left_ok ? 1 : 0);
    }
    else
    {
        check_action_promote(table, &left, &result->conversions[0], action->node);
        if (!compound && (action->context.fp_environment & BUSTER_CHECK_FP_EVAL_MASK) != 0)
        {
            check_action_fp_promote(table, &left, &result->conversions[0], action->node, action->context.fp_environment);
        }
        if (!unary)
        {
            check_action_promote(table, &right, &result->conversions[1], action->node);
            if ((action->context.fp_environment & BUSTER_CHECK_FP_EVAL_MASK) != 0)
            {
                check_action_fp_promote(table, &right, &result->conversions[1], action->node, action->context.fp_environment);
            }
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
                        .extensions = action->context.extensions,
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
                    check_action_fold_floating(op, &left, &right, &result->value, action->node, action->context.constant_evaluated);
                }
                else if (left.constant_kind == CHECK_CONSTANT_COMPLEX && (unary || right.constant_kind == CHECK_CONSTANT_COMPLEX))
                {
                    // The common type's conversions erase a real operand's domain. Take it from the original source
                    // operands here, where constant complex arithmetic alone needs it: an imaginary +0.0 changes a
                    // mixed expression's signed zero, and its zero products can introduce a spurious NaN.
                    const TypeId original_left = type_conversion_operand(table, action->operands[0].type);
                    const TypeId original_right = unary ? original_left : type_conversion_operand(table, action->operands[1].type);
                    const uint32_t domains = (check_action_kind(table, original_left) == BUSTER_TYPE_KIND_COMPLEX ? 1u : 0u) |
                                             (check_action_kind(table, original_right) == BUSTER_TYPE_KIND_COMPLEX ? 2u : 0u);
                    result->warnings |= check_complex_fold(table, op, &left, &right, domains, &result->value, action->node, action->context.extensions,
                                                              action->context.constant_evaluated);
                }
                else if (__builtin_expect(left.constant_kind == CHECK_CONSTANT_INTEGER && (unary || right.constant_kind == CHECK_CONSTANT_INTEGER), 0) &&
                         check_wide_fold(table, op, &left, &right, &result->value, action->node, action->context.extensions, action->scalar, &result->warnings))
                {
                    // A _BitInt past 128 bits (#1447, c_check_wide.c).
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
    CheckValue left = check_action_operand(table, action, result, 0);
    CheckValue right = unary ? left : check_action_operand(table, action, result, 1);
    if (!unary && (action->context.fp_environment & BUSTER_CHECK_FP_EVAL_MASK) != 0)
    {
        check_action_fp_promote(table, &left, &result->conversions[0], action->node, action->context.fp_environment);
        check_action_fp_promote(table, &right, &result->conversions[1], action->node, action->context.fp_environment);
    }
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

// check_action_pointer_common's pointers to types of extended qualifiers (#1447), as clang's
// checkConditionalObjectPointersCompatibility and checkConditionalPointerCompatibility take them: one type is itself; a
// void pointee takes the other's qualifiers, its address space too; pointees whose address spaces neither includes the
// other's are a conditional's error (a comparison's 0, which its caller compares as clang's CheckCompareOperands does); else
// the pointees' composite without their address spaces (in C, which clang doesn't put back), and for incompatible pointees
// a pointer to void in the space that includes the other, with the mismatch's warning.
static __attribute__((noinline)) TypeId check_action_pointer_common_extended(TypeTable* table, const CheckValue* left, const CheckValue* right, const CheckAction* action,
                                                                              CheckActionResult* result)
{
    const bool conditional = action->op == CHECK_OP_CONDITIONAL;
    const TypeId l = type_element(table, left->type);
    const TypeId r = type_element(table, right->type);
    const uint8_t lk = check_action_kind(table, l);
    const uint8_t rk = check_action_kind(table, r);
    const uint32_t lw = type_object_qualifiers(table, l);
    const uint32_t rw = type_object_qualifiers(table, r);
    const uint32_t ls = type_address_space(lw);
    const uint32_t rs = type_address_space(rw);
    const uint32_t qualifiers = type_qualifiers(l) | type_qualifiers(r);
    TypeId pointee = 0;
    TypeId common = 0;
    if (type_unqualified(type_strip_alignment(table, left->type)) == type_unqualified(type_strip_alignment(table, right->type)))
    {
        common = left->type;
    }
    else if ((lk == BUSTER_TYPE_KIND_VOID && rk != BUSTER_TYPE_KIND_FUNCTION) || (rk == BUSTER_TYPE_KIND_VOID && lk != BUSTER_TYPE_KIND_FUNCTION))
    {
        // The void pointee with the other's qualifiers added.
        const bool left_void = lk == BUSTER_TYPE_KIND_VOID;
        pointee = type_extend(table, type_extend(table, BUSTER_TYPE_VOID, left_void ? lw : rw), left_void ? rw : lw);
        pointee = pointee != 0 ? type_qualify(table, pointee, qualifiers) : 0;
    }
    else if (!type_space_includes(ls, rs) && !type_space_includes(rs, ls))
    {
        if (conditional)
        {
            check_action_diagnose(result, CHECK_DIAG_ADDRESS_SPACE_OVERLAP, 1);
        }
    }
    else if (!conditional && ls != rs)
    {
        // (A comparison's pointers of two spaces compare as they are.)
    }
    else
    {
        const TypeId lb = type_unextend(table, type_unqualified(l), BUSTER_TYPE_SPACE_MASK);
        const TypeId rb = type_unextend(table, type_unqualified(r), BUSTER_TYPE_SPACE_MASK);
        if (lb != 0 && rb != 0 && check_action_compatible_at(table, lb, rb, action, result))
        {
            pointee = type_composite_at(table, lb, rb, action->context.transparent_unions, action->context.tag_completion_count, action->context.source_position);
            pointee = pointee != 0 ? type_qualify(table, pointee, qualifiers) : 0;
        }
        else if (conditional && lb != 0 && rb != 0 && result->diagnostic == CHECK_DIAG_NONE)
        {
            pointee = type_extend(table, BUSTER_TYPE_VOID, type_space_includes(ls, rs) ? ls : rs);
            result->warnings |= CHECK_WARNING_CONDITIONAL_POINTER_TYPES;
        }
    }
    if (common == 0 && pointee != 0)
    {
        common = type_pointer(table, pointee);
    }
    if (common == 0 && pointee != 0 && result->diagnostic == CHECK_DIAG_NONE)
    {
        check_action_diagnose(result, CHECK_DIAG_RESOURCE, 0);
    }
    return common;
}

// The common type of two pointer operands (C 6.5.15p6): the other operand's for a null pointer constant, void with
// both pointees' qualifiers for void and an object type, else the composite of compatible pointees with both
// qualifiers. 0 for other pointees (void and a function among them), which each caller takes as clang does.
static TypeId check_action_pointer_common(TypeTable* table, const CheckValue* left, const CheckValue* right, const CheckAction* action, CheckActionResult* result)
{
    TypeId common = 0;
    const bool lp = check_action_kind(table, left->type) == BUSTER_TYPE_KIND_POINTER;
    const bool rp = check_action_kind(table, right->type) == BUSTER_TYPE_KIND_POINTER;
    if (lp && check_action_null(table, right, action->context.extensions))
    {
        common = left->type;
    }
    else if (rp && check_action_null(table, left, action->context.extensions))
    {
        common = right->type;
    }
    else if (lp && rp && __builtin_expect((type_object_qualifiers(table, type_element(table, left->type)) |
                                           type_object_qualifiers(table, type_element(table, right->type))) != 0, 0))
    {
        common = check_action_pointer_common_extended(table, left, right, action, result);
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
            pointee = type_composite_at(table, type_unqualified(l), type_unqualified(r), action->context.transparent_unions, action->context.tag_completion_count, action->context.source_position);
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
    CheckValue condition = check_action_operand(table, action, result, 0);
    if ((action->context.fp_environment & BUSTER_CHECK_FP_EVAL_MASK) != 0)
    {
        check_action_fp_promote(table, &condition, &result->conversions[0], action->node, action->context.fp_environment);
    }
    CheckValue left = check_action_operand(table, action, result, 1);
    CheckValue right = check_action_operand(table, action, result, 2);
    TypeId common = 0;
    result->warnings |= check_action_kind(table, left.type) == BUSTER_TYPE_KIND_ENUM || check_action_kind(table, right.type) == BUSTER_TYPE_KIND_ENUM ?
                        CHECK_WARNING_ENUM_OPERAND : 0;
    if (!check_action_is_scalar(table, condition.type))
    {
        check_action_diagnose(result, CHECK_DIAG_SCALAR_REQUIRED, 0);
    }
    if (check_action_is_arithmetic(table, left.type) && check_action_is_arithmetic(table, right.type))
    {
        check_action_promote(table, &left, &result->conversions[1], action->node);
        check_action_promote(table, &right, &result->conversions[2], action->node);
        if ((action->context.fp_environment & BUSTER_CHECK_FP_EVAL_MASK) != 0)
        {
            check_action_fp_promote(table, &left, &result->conversions[1], action->node, action->context.fp_environment);
            check_action_fp_promote(table, &right, &result->conversions[2], action->node, action->context.fp_environment);
        }
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
        check_action_conditional_provenance(table, &result->value, common, &condition, &left, &right, known, truth);
    }
}

// Always inlined, as it was before #1447 grew the type readers it inlines past clang's threshold.
static inline __attribute__((always_inline)) bool check_action_object_pointer(const TypeTable* table, TypeId pointer, const CheckAction* action, CheckActionResult* result)
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

static uint64_t check_action_pointer_stride(const TypeTable* table, TypeId pointer)
{
    const TypeId element = type_element(table, type_strip_aligned(table, pointer));
    const uint8_t kind = check_action_kind(table, element);
    const bool gnu_byte = kind == BUSTER_TYPE_KIND_VOID || kind == BUSTER_TYPE_KIND_FUNCTION;
    return gnu_byte ? 1 : type_size(table, element);
}

// The low 64 bits of an integer constant, sign-extended when the type is narrower than 64 bits and negative.
// A relocation addend is those bits times the stride, modulo 2^64: a value that does not fit in signed int64
// still has a 64-bit addend (1ul << 63, and 1ul << 62 scaled by an int's stride, which is 0 modulo 2^64).
static bool check_action_address_index(const TypeTable* table, const CheckValue* value, uint64_t* bits)
{
    uint32_t width = 0;
    bool is_signed = false;
    const bool integer = value->constant_kind == CHECK_CONSTANT_INTEGER && value->fault == CHECK_FAULT_NONE &&
                         check_action_integer_type(table, value->type, &width, &is_signed) && width != 0 && width <= 128;
    *bits = 0;
    if (integer)
    {
        const unsigned __int128 mask = check_action_mask(width);
        const unsigned __int128 stored = value->integer & mask;
        const bool narrow_negative = is_signed && width < 64 && (stored >> (width - 1)) != 0;
        const uint64_t narrow = width >= 64 ? UINT64_MAX : ((uint64_t)1 << width) - 1;
        *bits = narrow_negative ? (uint64_t)stored | ~narrow : (uint64_t)stored;
    }
    return integer;
}

static bool check_action_i64(const TypeTable* table, const CheckValue* value, int64_t* number)
{
    uint32_t width = 0;
    bool is_signed = false;
    const bool constant = value->constant_kind == CHECK_CONSTANT_INTEGER && value->fault == CHECK_FAULT_NONE;
    bool fits = constant && check_action_integer_type(table, value->type, &width, &is_signed);
    *number = 0;
    if (__builtin_expect(constant && !fits, 0))
    {
        // A _BitInt past 128 bits (#1447, c_check_wide.c).
        fits = check_wide_i64(table, value, number);
    }
    else if (fits)
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
        // Pointers to address spaces neither of which includes the other's (#1447), then pointees compatible but for
        // their qualifiers, extended ones too (clang's getUnqualifiedType).
        const uint32_t lw = type_object_qualifiers(table, le);
        const uint32_t rw = type_object_qualifiers(table, re);
        const bool extended = __builtin_expect((lw | rw) != 0, 0);
        const bool overlap = !extended || type_space_includes(type_address_space(lw), type_address_space(rw)) ||
                             type_space_includes(type_address_space(rw), type_address_space(lw));
        if (!overlap)
        {
            check_action_diagnose(result, CHECK_DIAG_ADDRESS_SPACE_OVERLAP, 1);
        }
        else if (!check_action_compatible_at(table, extended ? type_unextend(table, type_unqualified(le), BUSTER_TYPE_SPACE_MASK | BUSTER_TYPE_GC_MASK) : type_unqualified(le),
                                             extended ? type_unextend(table, type_unqualified(re), BUSTER_TYPE_SPACE_MASK | BUSTER_TYPE_GC_MASK) : type_unqualified(re),
                                             action, result))
        {
            check_action_diagnose(result, CHECK_DIAG_INCOMPATIBLE, 1);
        }
        result->value.type = BUSTER_TYPE_LONG;
        if (complete && result->diagnostic == CHECK_DIAG_NONE && check_action_foldable(&left) && check_action_foldable(&right) && left.constant_kind == CHECK_CONSTANT_ADDRESS && right.constant_kind == CHECK_CONSTANT_ADDRESS && left.object != 0 && left.object == right.object)
        {
            const uint64_t stride = check_action_pointer_stride(table, left.type);
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
            result->value.flags |= pointer.flags & (BUSTER_CHECK_STATIC_OBJECT | BUSTER_CHECK_INACTIVE_UNION_ADDRESS | BUSTER_CHECK_CONST_READ_RESTRICTED | BUSTER_CHECK_NONNULL_ADDRESS);
            result->value.flags |= action->op == CHECK_OP_SUBSCRIPT && (pointer.flags & BUSTER_CHECK_INACTIVE_UNION_ADDRESS) != 0 ? BUSTER_CHECK_INACTIVE_UNION_READ : 0;
            // A subscript of a register array its decay didn't reject (BUSTER_CHECK_REGISTER_NAME) is a register object's
            // element, whose address clang's & operator rejects, as getPrimaryDecl climbs an array's subscripts.
            const CheckValue* array = action->operands + (reverse ? 1 : 0);
            result->value.flags |= action->op == CHECK_OP_SUBSCRIPT && (array->flags & (BUSTER_CHECK_REGISTER | BUSTER_CHECK_REGISTER_NAME)) == BUSTER_CHECK_REGISTER &&
                                   type_kind_is_array(check_action_kind(table, array->type)) ? BUSTER_CHECK_REGISTER : 0;
            uint64_t index_bits = 0;
            const uint64_t stride = check_action_pointer_stride(table, pointer.type);
            if (!check_action_runtime_size(table, type_element(table, pointer.type)) && pointer.constant_kind == CHECK_CONSTANT_ADDRESS &&
                pointer.fault == CHECK_FAULT_NONE && check_action_address_index(table, &index, &index_bits))
            {
                // The addend is the low 64 bits, the same bits a relocation stores. Signed overflow is a wrap, not a
                // reason to drop the constant: 2^63 is the addend -2^63, and 2^62 elements of an int are zero bytes.
                const uint64_t delta = index_bits * stride;
                const uint64_t base = (uint64_t)pointer.offset;
                const uint64_t addend = action->op == CHECK_OP_SUBTRACT ? base - delta : base + delta;
                result->value.constant_kind = CHECK_CONSTANT_ADDRESS;
                result->value.object = pointer.object;
                result->value.offset = (int64_t)addend;
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
    const bool left_null = check_action_null(table, &left, action->context.extensions);
    const bool right_null = check_action_null(table, &right, action->context.extensions);
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
        // The pointees without their qualifiers, extended ones too (#1447: clang's getUnqualifiedType).
        const bool extended = __builtin_expect((type_object_qualifiers(table, le) | type_object_qualifiers(table, re)) != 0, 0);
        const TypeId lu = extended ? type_unextend(table, type_unqualified(le), BUSTER_TYPE_SPACE_MASK | BUSTER_TYPE_GC_MASK) : type_unqualified(le);
        const TypeId ru = extended ? type_unextend(table, type_unqualified(re), BUSTER_TYPE_SPACE_MASK | BUSTER_TYPE_GC_MASK) : type_unqualified(re);
        if (!check_action_compatible_at(table, lu, ru, action, result) && !(equality && void_pointee))
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

// Check every union along an anonymous-member path. Equal byte offsets and equal scalar types cannot distinguish
// two union members; the initializer's active physical member is part of a constant-read proof.
static bool check_action_const_member(const TypeTable* table, const CheckAction* action, const CheckValue* base, TypeId record, uint32_t member)
{
    bool allowed = true;
    bool searching = action->context.constant_evaluated && action->context.const_values != NULL && base->constant_kind == CHECK_CONSTANT_ADDRESS &&
                     base->object != 0 && base->offset >= 0 && base->object_bit_offset < 8 && (uint64_t)base->offset <= UINT64_MAX / 8;
    uint64_t at = searching ? (uint64_t)base->offset * 8 + base->object_bit_offset : 0;
    TypeId current = type_unqualified(type_strip_aligned(table, record));
    for (uint32_t depth = 0; searching && depth < table->tags.count; depth += 1)
    {
        const uint32_t tag = table->types.index[type_index(current)];
        const uint32_t first = table->tags.member_begin[tag];
        const uint32_t count = table->tags.member_count[tag];
        uint32_t selected = member >= first && member - first < count ? member : 0;
        for (uint32_t i = 0; selected == 0 && i < count; i += 1)
        {
            const uint32_t candidate = first + i;
            const TypeId child = type_unqualified(type_strip_aligned(table, table->members.type[candidate]));
            const uint8_t kind = type_kind(table, child);
            if (table->members.name[candidate] == 0 && (table->members.flags[candidate] & BUSTER_TYPE_MEMBER_BIT_FIELD) == 0 &&
                (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION))
            {
                uint64_t ignored_offset = 0;
                uint32_t ignored_qualifiers = 0;
                const uint32_t found = action->scalar ? type_lookup_member_scalar(table, child, action->member, &ignored_offset, &ignored_qualifiers) :
                                                       type_lookup_member(table, child, action->member, &ignored_offset, &ignored_qualifiers);
                selected = found == member ? candidate : 0;
            }
        }
        allowed = selected != 0;
        if (allowed && type_kind(table, current) == BUSTER_TYPE_KIND_UNION)
        {
            allowed = check_const_union_allows(table, action->context.const_values, base->object, current, at, selected - first + 1,
                                               action->context.source_position);
        }
        searching = allowed && selected != member;
        if (searching)
        {
            allowed = !__builtin_add_overflow(at, table->members.offset[selected], &at);
            current = type_unqualified(type_strip_aligned(table, table->members.type[selected]));
            searching = allowed;
        }
    }
    return allowed && !searching;
}

// Always inlined (#880), as profile-guided builds do; hot caller: check_action_compute.
static inline __attribute__((always_inline)) void check_action_member(TypeTable* table, const CheckAction* action, CheckActionResult* result)
{
    const bool arrow = action->op == CHECK_OP_ARROW;
    CheckValue base = arrow ? check_action_operand(table, action, result, 0) : action->operands[0];
    TypeId record = type_strip_aligned(table, base.type);
    // The record's address space, which its member's lvalue takes (#1447, clang's BuildFieldReferenceExpr: not its
    // garbage collection qualifier, which members never pick up).
    uint32_t space = arrow ? 0 : type_address_space(type_extended_qualifiers(table, base.type));
    if (arrow)
    {
        if (type_kind(table, record) != BUSTER_TYPE_KIND_POINTER)
        {
            check_action_diagnose(result, CHECK_DIAG_POINTER_REQUIRED, 0);
        }
        else
        {
            space = type_address_space(type_extended_qualifiers(table, type_element(table, record)));
            record = type_strip_aligned(table, type_element(table, record));
        }
    }
    uint8_t kind = type_kind(table, record);
    if (__builtin_expect(kind == BUSTER_TYPE_KIND_ATOMIC, 0))
    {
        // The access still has its member's type. Its runtime-behavior diagnostic is deferred independently
        // (-Watomic-access), so unevaluated/suppressed accesses and recovery use the same member lookup.
        const TypeId element = type_strip_aligned(table, type_element(table, record));
        const uint8_t element_kind = type_kind(table, element);
        if (element_kind == BUSTER_TYPE_KIND_STRUCT || element_kind == BUSTER_TYPE_KIND_UNION)
        {
            record = element;
            kind = element_kind;
        }
    }
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
            if (__builtin_expect(space != 0, 0))
            {
                result->value.type = type_extend(table, result->value.type, space);
                if (result->value.type == 0)
                {
                    check_action_diagnose(result, CHECK_DIAG_RESOURCE, 0);
                }
            }
            result->value.category = arrow || base.category == CHECK_LVALUE ? CHECK_LVALUE : CHECK_RVALUE;
            check_action_inherit(&result->value, &base);
            result->value.flags |= base.flags & (BUSTER_CHECK_STATIC_OBJECT | BUSTER_CHECK_CONST_READ_RESTRICTED | BUSTER_CHECK_NONNULL_ADDRESS);
            result->value.flags |= type_kind(table, type_strip_aligned(table, result->value.type)) == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY ?
                                   BUSTER_CHECK_CONST_READ_RESTRICTED : 0;
            result->value.flags |= arrow && (base.flags & BUSTER_CHECK_INACTIVE_UNION_ADDRESS) != 0 ? BUSTER_CHECK_INACTIVE_UNION_READ : 0;
            if (!check_action_const_member(table, action, &base, record, member))
            {
                result->value.flags |= BUSTER_CHECK_INACTIVE_UNION_READ;
            }
            if ((table->members.flags[member] & BUSTER_TYPE_MEMBER_BIT_FIELD) != 0)
            {
                result->value.flags |= BUSTER_CHECK_BIT_FIELD;
                result->value.bit_width = table->members.bit_width[member];
            }
            if (!arrow)
            {
                result->value.flags |= base.flags & BUSTER_CHECK_REGISTER;
            }
            uint64_t bits = 0;
            if (base.constant_kind == CHECK_CONSTANT_ADDRESS && !__builtin_add_overflow(offset, base.object_bit_offset, &bits) && bits / 8 <= INT64_MAX)
            {
                int64_t addend = 0;
                if (!__builtin_add_overflow(base.offset, (int64_t)(bits / 8), &addend))
                {
                    result->value.constant_kind = bits % 8 == 0 ? CHECK_CONSTANT_ADDRESS : CHECK_CONSTANT_NONE;
                    result->value.object = base.object;
                    result->value.offset = addend;
                    result->value.object_bit_offset = (uint32_t)(bits % 8);
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

// Always inlined (#880), as profile-guided builds do; hot caller: check_action_assignment.
static inline __attribute__((always_inline)) bool check_action_modifiable(const TypeTable* table, const CheckValue* value, const CheckAction* action, CheckActionResult* result)
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

// Always inlined (#880), as profile-guided builds do; hot caller: check_action_assignment.
static inline __attribute__((always_inline)) void check_action_bit_field_store(const TypeTable* table, const CheckValue* target, CheckValue* value)
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

// Always inlined (#880), as profile-guided builds do; hot caller: check_action_compute.
static inline __attribute__((always_inline)) void check_action_assignment(TypeTable* table, const CheckAction* action, CheckActionResult* result)
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
        const bool atomic = check_action_kind(table, target) == BUSTER_TYPE_KIND_ATOMIC && check_action_kind(table, source.type) != BUSTER_TYPE_KIND_ATOMIC;
        target = check_action_kind(table, source.type) == BUSTER_TYPE_KIND_ATOMIC ? type_unqualified(type_strip_aligned(table, target))
                                                                               : type_conversion_operand(table, target);
        const bool void_return = action->op == CHECK_OP_CONVERT && check_action_kind(table, target) == BUSTER_TYPE_KIND_VOID &&
                                 (check_action_kind(table, source.type) == BUSTER_TYPE_KIND_VOID || (action->context.extensions & BUSTER_CHECK_EXTENSION_SYSTEM_HEADER) != 0);
        if (!void_return && !check_action_assignable(table, target, &source, action, result))
        {
            check_action_diagnose(result, CHECK_DIAG_INCOMPATIBLE, slot);
        }
        else
        {
            const uint32_t conversion = BUSTER_CHECK_CONVERT_ASSIGNMENT | (check_action_kind(table, target) == BUSTER_TYPE_KIND_POINTER && check_action_null(table, &source, action->context.extensions) ? BUSTER_CHECK_CONVERT_NULL : 0) |
                                        (atomic ? BUSTER_CHECK_CONVERT_ATOMIC : 0);
            check_action_conversion(table, &source, target, conversion, &result->conversions[slot], action->node);
            result->value = source;
            result->value.category = CHECK_RVALUE;
            if (store)
            {
                check_action_bit_field_store(table, &action->operands[0], &result->value);
                result->value.flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_ICE_UNEVALUATED);
                result->value.flags |= BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_MODIFICATION;
                check_action_inherit(&result->value, &action->operands[0]);
            }
        }
    }
    else
    {
        check_action_diagnose(result, CHECK_DIAG_INVALID_TYPE, slot);
    }
}

// Whether clang's CheckIncrementDecrementOperand takes an operand of `type` (its _Atomic's value type) in C: a real
// type (a complete enumeration's), a pointer or a complex type. Another, a vector's, a record's, an array's, a
// function's or void, is "cannot increment value of type" (c_check_semantic_diagnostic.c), before the operand's
// modifiability is tested.
static bool check_action_steppable(const TypeTable* table, TypeId type)
{
    const TypeId value = type_conversion_operand(table, type);
    const uint8_t kind = check_action_kind(table, value);
    return kind == BUSTER_TYPE_KIND_INTEGER || kind == BUSTER_TYPE_KIND_FLOATING || kind == BUSTER_TYPE_KIND_BIT_INT || kind == BUSTER_TYPE_KIND_POINTER ||
           kind == BUSTER_TYPE_KIND_COMPLEX || (kind == BUSTER_TYPE_KIND_ENUM && type_is_complete(table, value));
}

static void check_action_update(TypeTable* table, const CheckAction* action, CheckActionResult* result)
{
    const CheckValue target = action->operands[0];
    const bool increment = action->op >= CHECK_OP_PRE_INCREMENT && action->op <= CHECK_OP_POST_DECREMENT;
    const bool postfix = action->op == CHECK_OP_POST_INCREMENT || action->op == CHECK_OP_POST_DECREMENT;
    const uint8_t kind = check_action_kind(table, type_conversion_operand(table, target.type));
    if (increment && !check_action_steppable(table, target.type))
    {
        check_action_diagnose(result, CHECK_DIAG_ARITHMETIC_REQUIRED, 0);
    }
    else if (check_action_modifiable(table, &target, action, result))
    {
        // A complex operand's `++` or `--` adds or subtracts one of its real part (#147: C2y's, clang's in every mode).
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
            // Increment/decrement operate in the lvalue's type. Clang does not apply usual unary floating
            // conversions here; compound assignments below do use the current evaluation method.
            arithmetic.context.fp_environment &= ~BUSTER_CHECK_FP_EVAL_MASK;
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
            check_action_arithmetic(table, &arithmetic, result, 0, NULL, !increment);
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
            result->value.flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_NULL_POINTER | BUSTER_CHECK_REGISTER | BUSTER_CHECK_REGISTER_NAME);
            result->value.flags |= BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_MODIFICATION;
            if (result->value.fault != CHECK_FAULT_NONE)
            {
                result->value.constant_kind = CHECK_CONSTANT_NONE;
            }
        }
    }
}

// Always inlined (#880), as profile-guided builds do; hot caller: check_action_compute.
// The member a cast to union `target` from a value of type `source` initializes (#2301, GCC's extension, clang's
// CastExpr::getTargetFieldForToUnionCast): its first member of the source's unqualified type, but an unnamed bit-field.
// 0 for none. (A named bit-field is such a member too.)
static TypeId check_action_union_cast_member(const TypeTable* table, TypeId target, TypeId source)
{
    TypeId result = 0;
    const uint32_t tag = table->types.index[type_index(target)];
    const uint32_t first = table->tags.member_begin[tag];
    const uint32_t count = table->tags.member_count[tag];
    const TypeId plain = type_unqualified(type_strip_aligned(table, source));
    for (uint32_t i = 0; result == 0 && i < count; i += 1)
    {
        const uint32_t member = first + i;
        const TypeId type = table->members.type[member];
        const bool unnamed_bit_field = table->members.name[member] == 0 && (table->members.flags[member] & BUSTER_TYPE_MEMBER_BIT_FIELD) != 0;
        result = !unnamed_bit_field && type_unqualified(type_strip_aligned(table, type)) == plain ? type : 0;
    }
    return result;
}

static inline __attribute__((always_inline)) void check_action_cast(TypeTable* table, const CheckAction* action, CheckActionResult* result)
{
    CheckValue source = check_action_operand(table, action, result, 0);
    // A cast to an atomic type (#2230) converts to its value type, and its rvalue keeps the atomic type, as clang's
    // CheckCStyleCast types it: `(_Atomic int)x` is an `_Atomic(int)` rvalue, which an `_Atomic` object takes, and which
    // the usual arithmetic conversions take as their left operand alone (check_action_arithmetic).
    const TypeId cast_type = type_unqualified(type_strip_aligned(table, action->type));
    const bool atomic_target = __builtin_expect(type_kind(table, cast_type) == BUSTER_TYPE_KIND_ATOMIC, 0);
    const TypeId target = atomic_target ? type_conversion_operand(table, cast_type) : cast_type;
    const uint8_t target_kind = type_kind(table, target);
    const uint8_t input_kind = check_action_kind(table, source.type);
    const bool target_pointer = target_kind == BUSTER_TYPE_KIND_POINTER;
    const bool source_pointer = input_kind == BUSTER_TYPE_KIND_POINTER;
    const bool pointer_float = (target_pointer && !check_action_is_integer(table, source.type) && !source_pointer && input_kind != BUSTER_TYPE_KIND_NULLPTR) || (source_pointer && !target_pointer && !check_action_is_integer(table, target));
    const bool scalar = check_action_is_scalar(table, target) && check_action_is_scalar(table, source.type) && !pointer_float;
    const bool record = (target_kind == BUSTER_TYPE_KIND_STRUCT || target_kind == BUSTER_TYPE_KIND_UNION) &&
                        target_kind == input_kind && check_action_compatible_at(table, target, type_unqualified(source.type), action, result);
    const bool complete = !(target_kind == BUSTER_TYPE_KIND_ENUM || target_kind == BUSTER_TYPE_KIND_STRUCT || target_kind == BUSTER_TYPE_KIND_UNION) ||
                          check_action_source_complete(table, target, action);
    // GCC's cast to a union from a member's type (#2301): the member it initializes, which clang's CheckCStyleCast takes
    // once the union is no type compatible with the operand's.
    const TypeId member = complete && target_kind == BUSTER_TYPE_KIND_UNION && !record ? check_action_union_cast_member(table, target, source.type) : 0;
    if (!complete)
    {
        check_action_diagnose(result, CHECK_DIAG_INCOMPLETE, 0);
    }
    else if (member != 0)
    {
        check_action_conversion(table, &source, type_unqualified(type_strip_aligned(table, member)), BUSTER_CHECK_CONVERT_CAST | BUSTER_CHECK_CONVERT_TO_UNION,
                                &result->conversions[0], action->node);
        // An rvalue of the union's type: neither a constant of the member's type nor an ICE.
        result->value = source;
        result->value.type = target;
        result->value.category = CHECK_RVALUE;
        result->value.constant_kind = CHECK_CONSTANT_NONE;
        result->value.flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_FLOAT_LITERAL);
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
        const bool null = target_pointer && check_action_kind(table, type_element(table, target)) == BUSTER_TYPE_KIND_VOID && check_action_null(table, &source, action->context.extensions);
        // A cast to void discards its operand: whether clang's evaluator evaluates it, before the conversion makes it void.
        // A constant evaluation's alone (BUSTER_CHECK_DISCARDED_UNEVALUABLE).
        const bool discarded = target_kind != BUSTER_TYPE_KIND_VOID || !action->context.constant_evaluated || check_action_discard_evaluable(table, &source);
        check_action_conversion(table, &source, target, BUSTER_CHECK_CONVERT_CAST, &result->conversions[0], action->node);
        result->value = source;
        check_action_cast_provenance(&result->value, ice, gnu_ice, null, target_kind == BUSTER_TYPE_KIND_VOID, discarded);
        if (atomic_target)
        {
            // A constant, as clang's evaluator folds CK_NonAtomicToAtomic, but no integer constant expression, which
            // needs an integer type.
            result->value.type = cast_type;
            result->value.flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_NULL_POINTER);
        }
    }
}

static void check_action_query(TypeTable* table, const CheckAction* action, CheckActionResult* result)
{
    const TypeId queried = action->type != 0 ? action->type : action->operands[0].type;
    // An array's alignment is its element's (C11 6.5.3.4p3), which clang takes of an array of unknown size too (#1447).
    const bool element = action->op == CHECK_OP_ALIGNOF && queried != 0 && type_kind(table, type_strip_aligned(table, queried)) == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY;
    const TypeId target = element ? type_element(table, type_strip_aligned(table, queried)) : queried;
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
        check_action_guard_unknown(action);
        const bool active = action->context.transparent_unions == NULL ||
                            (tag < action->context.tag_completion_count && action->context.transparent_unions[tag] != 0 &&
                             action->context.transparent_unions[tag] <= action->context.source_position);
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
            CheckActionResult converted = { 0 };
            if (!type_is_valid(table, action->arguments[i].type))
            {
                check_action_diagnose(result, CHECK_DIAG_INVALID_TYPE, i + 1);
                action->argument_conversions[i] = (CheckConversion){ 0 };
            }
            else
            {
                CheckValue value = check_action_operand_value(table, action, &action->arguments[i], &converted, 0);
                const TypeId original = type_unqualified(type_strip_aligned(table, value.type));
                const bool parameter = (prototype || kr) && i < count;
                if (__builtin_expect(prototype && !parameter && type_kind(table, original) == BUSTER_TYPE_KIND_ATOMIC, 0))
                {
                    // An atomic rvalue, a cast's (#2230), as a variadic argument: clang's -Wnon-pod-varargs error, refused
                    // by name. (An unprototyped call's is promoted, as clang's is.)
                    check_action_diagnose(result, CHECK_DIAG_UNSUPPORTED, i + 1);
                }
                if (!parameter)
                {
                    check_action_promote(table, &value, &converted.conversions[0], action->node);
                    if ((action->context.fp_environment & BUSTER_CHECK_FP_EVAL_MASK) != 0)
                    {
                        check_action_fp_promote(table, &value, &converted.conversions[0], action->node, action->context.fp_environment);
                    }
                }
                TypeId destination = parameter ? parameters[i] : type_default_argument_promote(table, value.type);
                // Default argument promotion remembers the original type: float and __fp16 are passed as double
                // even when usual unary evaluation first widened them to long double. Other floating types keep
                // their evaluation promotion, including a double argument under extended evaluation.
                if (!parameter && (original == BUSTER_TYPE_FLOAT || original == BUSTER_TYPE_HALF))
                {
                    destination = BUSTER_TYPE_DOUBLE;
                }
                const bool atomic = check_action_kind(table, destination) == BUSTER_TYPE_KIND_ATOMIC && check_action_kind(table, value.type) != BUSTER_TYPE_KIND_ATOMIC;
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
                                                (transparent ? BUSTER_CHECK_CONVERT_TRANSPARENT_UNION : 0) | (atomic ? BUSTER_CHECK_CONVERT_ATOMIC : 0);
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

// Always inlined (#880), as profile-guided builds do; hot caller: check_action_compute, once per action.
static inline __attribute__((always_inline)) uint32_t check_action_operand_count(const CheckAction* action)
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
    if ((action->context.fp_environment & BUSTER_CHECK_FP_ENV_ACCESS) != 0 && result->diagnostic == CHECK_DIAG_NONE)
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

// Whether an action's operands hold a pointer, an array, a function or nullptr: pointer arithmetic or a pointer comparison
// rather than arithmetic. check_action_compute asks it in those operators' cases alone, so that the others, most of the
// checker's nodes, look no operand's kind up for it (#1050).
// Always inlined (#880), as profile-guided builds do; hot caller: check_action_compute.
static inline __attribute__((always_inline)) bool check_action_pointer_pair(const TypeTable* table, const CheckAction* action, uint32_t count)
{
    const uint8_t left_kind = count != 0 ? check_action_kind(table, action->operands[0].type) : BUSTER_TYPE_KIND_NONE;
    const uint8_t right_kind = count >= 2 ? check_action_kind(table, action->operands[1].type) : BUSTER_TYPE_KIND_NONE;
    return left_kind == BUSTER_TYPE_KIND_POINTER || right_kind == BUSTER_TYPE_KIND_POINTER || type_kind_is_array(left_kind) || type_kind_is_array(right_kind) ||
           left_kind == BUSTER_TYPE_KIND_FUNCTION || right_kind == BUSTER_TYPE_KIND_FUNCTION || left_kind == BUSTER_TYPE_KIND_NULLPTR || right_kind == BUSTER_TYPE_KIND_NULLPTR;
}

// Always inlined (#880), as profile-guided builds do; hot caller: check_graph_node_action, once per action. Its checks of
// the operands are for errors, and say so (#1175): without it, clang jumped around each check's report.
static inline __attribute__((always_inline)) void check_action_compute(TypeTable* table, const CheckAction* action, CheckActionResult* result, TypeId common_hint, CheckIntegerFold* pending)
{
    memset(result, 0, sizeof(*result));
    if (pending != NULL)
    {
        memset(pending, 0, sizeof(*pending));
    }
    if (__builtin_expect(action->op >= CHECK_OP_COUNT, 0))
    {
        check_action_diagnose(result, CHECK_DIAG_UNSUPPORTED, 0);
    }
    const uint32_t count = check_action_operand_count(action);
    for (uint32_t i = 0; i < count; i += 1)
    {
        if (__builtin_expect(!type_is_valid(table, action->operands[i].type), 0))
        {
            check_action_diagnose(result, CHECK_DIAG_INVALID_TYPE, i);
        }
        if (__builtin_expect(action->operands[i].overload_head != 0, 0) && action->op != CHECK_OP_VALUE && action->op != CHECK_OP_SELECT && action->op != CHECK_OP_ADDRESS)
        {
            check_action_diagnose(result, CHECK_DIAG_INCOMPATIBLE, i);
        }
        result->conversions[i].type = action->operands[i].type;
    }
    for (uint32_t i = 0; action->arguments != 0 && i < action->argument_count; i += 1)
    {
        if (__builtin_expect(action->arguments[i].overload_head != 0, 0))
        {
            check_action_diagnose(result, CHECK_DIAG_INCOMPATIBLE, i + 1);
        }
    }
    if ((action->op == CHECK_OP_CAST || action->op == CHECK_OP_INITIALIZE || action->op == CHECK_OP_CONVERT || action->op == CHECK_OP_SIZEOF_VLA || ((action->op == CHECK_OP_SIZEOF || action->op == CHECK_OP_ALIGNOF) && action->type != 0)) && __builtin_expect(!type_is_valid(table, action->type), 0))
    {
        check_action_diagnose(result, CHECK_DIAG_INVALID_TYPE, 0);
    }
    if (result->diagnostic == CHECK_DIAG_NONE && !check_vector_action(table, action, result))
    {
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
            case CHECK_OP_BIT_XOR: check_action_arithmetic(table, action, result, common_hint, pending, false); break;
            case CHECK_OP_BIT_NOT:
            {
                if (check_action_kind(table, action->operands[0].type) == BUSTER_TYPE_KIND_COMPLEX)
                {
                    check_complex_conjugate(table, action, result);
                }
                else
                {
                    check_action_arithmetic(table, action, result, common_hint, pending, false);
                }
            } break;
            case CHECK_OP_REAL:
            case CHECK_OP_IMAGINARY: check_complex_part(table, action, result); break;
            case CHECK_OP_ADD:
            case CHECK_OP_SUBTRACT:
            {
                if (check_action_pointer_pair(table, action, count))
                {
                    check_action_pointer_arithmetic(table, action, result);
                }
                else
                {
                    check_action_arithmetic(table, action, result, common_hint, pending, false);
                }
            } break;
            case CHECK_OP_LESS:
            case CHECK_OP_LESS_EQUAL:
            case CHECK_OP_GREATER:
            case CHECK_OP_GREATER_EQUAL:
            case CHECK_OP_EQUAL:
            case CHECK_OP_NOT_EQUAL:
            {
                if (check_action_pointer_pair(table, action, count))
                {
                    check_action_pointer_compare(table, action, result);
                }
                else
                {
                    check_action_arithmetic(table, action, result, common_hint, pending, false);
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
                check_action_comma_provenance(table, &result->value, &left, &right, action->context.constant_evaluated);
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
                    check_action_address_provenance(&result->value, action->context.constant_evaluated);
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
                    check_action_dereference_provenance(&result->value);
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
    // Keep the usual ACCESS-off action out of the environment walk and its stack frame.
    if ((action->context.fp_environment & BUSTER_CHECK_FP_ENV_ACCESS) != 0)
    {
        check_action_fp_environment(table, action, result);
    }
    result->value.flags = check_value_source_flags(action->op, result->value.flags);
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

// The operators whose integer folds a batch takes 8 lanes at a time (check_action_fold_vector).
static bool check_action_fold_operator(CheckOp op)
{
    return op == CHECK_OP_POSITIVE || op == CHECK_OP_NEGATIVE || op == CHECK_OP_BIT_NOT || op == CHECK_OP_ADD || op == CHECK_OP_SUBTRACT ||
           (op >= CHECK_OP_SHIFT_LEFT && op <= CHECK_OP_BIT_XOR);
}

static bool check_action_vector_eligible(const TypeTable* table, const CheckAction* action)
{
    const CheckOp op = action->op;
    bool result = check_action_fold_operator(op);
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

#include "c_check_warning_semantics.c"

#endif
