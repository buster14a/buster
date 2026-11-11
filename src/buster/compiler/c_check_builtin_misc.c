// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #148 miscellaneous builtin semantic actions. Include after c_builtin_catalog.c and c_check_actions.c.
// Arguments are already elaborated source values. The caller retains their graph identities and evaluation order;
// evaluated_operands is a bit mask (all ones also takes the arguments past its 32), never permission to evaluate an operand twice. No action reads source memory.
// Tag completions are immutable source positions, separate from diagnostic node identities. A null completion
// slice uses the current table for direct action callers. Batches run in order because decay types may be interned.
// Input/output slices require ordinary C alignment, no padding, and must be disjoint from each other/type storage.
// Zero count permits null slices. These actions establish no catalog SOURCE or selector acceptance by themselves.
#ifndef BUSTER_C_CHECK_BUILTIN_MISC_INCLUDED
#define BUSTER_C_CHECK_BUILTIN_MISC_INCLUDED

#define BUSTER_CHECK_BUILTIN_MISC_WARN_QUALIFIERS 0x01u
#define BUSTER_CHECK_BUILTIN_MISC_WARN_POINTER_SIGN 0x02u
// CheckBuiltinMiscAction.counted_member's answers (#148).
#define BUSTER_CHECK_COUNTED_NONE 0u
#define BUSTER_CHECK_COUNTED_MEMBER 1u
#define BUSTER_CHECK_COUNTED_COUNTED 2u
#define BUSTER_CHECK_BUILTIN_MISC_WARN_FUNCTION_POINTER 0x04u
#define BUSTER_CHECK_BUILTIN_MISC_WARN_ALIGNMENT 0x08u
#define BUSTER_CHECK_BUILTIN_MISC_WARN_FRAME_LEVEL 0x10u
#define BUSTER_CHECK_BUILTIN_MISC_WARN_ALIGNED_TO_ONE 0x20u

// An os_log buffer's item (#148, clang/include/clang/AST/OSLog.h's OSLogBufferItem): its kind, the descriptor byte's high
// nibble; its privacy flags, the low nibble; where its data comes from (none, an errno's; a data argument's value,
// from the first after the format; or a constant: a precision's count of 4 bytes, or a mask's 8 bytes of its name).
#define BUSTER_CHECK_OS_LOG_SCALAR 0u
#define BUSTER_CHECK_OS_LOG_COUNT 1u
#define BUSTER_CHECK_OS_LOG_STRING 2u
#define BUSTER_CHECK_OS_LOG_POINTER 3u
#define BUSTER_CHECK_OS_LOG_OBJC 4u
#define BUSTER_CHECK_OS_LOG_WIDE_STRING 5u
#define BUSTER_CHECK_OS_LOG_ERRNO 6u
#define BUSTER_CHECK_OS_LOG_MASK 7u
#define BUSTER_CHECK_OS_LOG_PRIVATE 0x1u
#define BUSTER_CHECK_OS_LOG_PUBLIC 0x2u
#define BUSTER_CHECK_OS_LOG_SENSITIVE 0x5u
#define BUSTER_CHECK_OS_LOG_NONE 0u
#define BUSTER_CHECK_OS_LOG_ARGUMENT 1u
#define BUSTER_CHECK_OS_LOG_CONSTANT 2u

typedef struct CheckOSLogItem CheckOSLogItem;
struct CheckOSLogItem
{
    uint8_t kind;
    uint8_t flags;
    uint8_t source;
    uint32_t argument;
    uint64_t constant;
};

#define BUSTER_CHECK_BUILTIN_MISC_UNSUPPORTED_FRAME_LEVEL 0x01u
#define BUSTER_CHECK_BUILTIN_MISC_UNSUPPORTED_ALIGNMENT 0x02u
#define BUSTER_CHECK_BUILTIN_MISC_UNSUPPORTED_COMPLEX 0x04u

typedef enum CheckBuiltinMiscConstraint
{
    CHECK_BUILTIN_MISC_CONSTRAINT_NONE,
    CHECK_BUILTIN_MISC_CONSTRAINT_CONSTANT,
    CHECK_BUILTIN_MISC_CONSTRAINT_RANGE,
    CHECK_BUILTIN_MISC_CONSTRAINT_ALIGNMENT,
    // An alignment builtin's alignment below 1 (#2221): RANGE is then its alignment past the source's maximum.
    CHECK_BUILTIN_MISC_CONSTRAINT_MINIMUM,
} CheckBuiltinMiscConstraint;

typedef struct CheckBuiltinMiscAction CheckBuiltinMiscAction;
struct CheckBuiltinMiscAction
{
    CBuiltinId id;
    const CheckValue* arguments;
    uint32_t argument_count;
    uint32_t node;
    CheckContext context;
    // An alignment builtin's first argument's object, where its source names one (CheckBuiltinCall.base_object), and that
    // object's declaration alignment as clang gives it, its log2 plus 1, 0 when unknown: what folds an address.
    uint32_t pointer_object;
    uint8_t pointer_alignment;
    // An alignment builtin's alignment as clang's evaluator reads it, where the call's evidence has it
    // (CheckBuiltinCall.alignment_evaluated), with whether it has side effects: what its checks and folds read in place
    // of the runtime view's value, which reads no const object's initializer.
    bool alignment_evaluated;
    bool alignment_side_effects;
    unsigned __int128 alignment_value;
    // A _BitInt past 128 bits' evaluated alignment's limbs (#1447, CheckBuiltinCall.alignment_limbs), 0 for a narrower one.
    uint64_t alignment_limbs;
    // allow_runtime_check's and allow_sanitize_check's argument, and annotation's second, where the source writes a narrow
    // string literal (the call's evidence: CheckBuiltinCall.nan_payload): its bytes, without the terminator.
    bool string_known;
    const uint8_t* string_bytes;
    uint32_t string_length;
    // Whether that literal is a UTF-8 one (u8"..."), which the CFString builtins refuse (clang's isOrdinary).
    bool string_utf8;
    // __builtin_infer_alloc_token's token, where the call's arguments give one (CheckBuiltinCall.alloc_token).
    bool alloc_token_known;
    uint64_t alloc_token;
    // A constant CFString's object (#148): its call's own, its `)`'s token plus one, whose address its value is
    // (check_data_cfstring_keys makes it, the first of each literal's for them all).
    uint32_t cfstring_object;
    // Whether __builtin_call_with_static_chain's first argument is a call as written (CheckBuiltinCall.static_chain_call).
    bool static_chain_call;
    // The os_log builtins' format's layout (CheckBuiltinCall.os_log_items), whether its format is a literal, whether a
    // mask's size is invalid there, and the data arguments its specifiers take (the format checks').
    const CheckOSLogItem* os_log_items;
    uint32_t os_log_count;
    bool os_log_known;
    bool os_log_invalid_mask;
    uint64_t os_log_taken[4];
    // __builtin_counted_by_ref's argument (#148, check_graph_counted_member): BUSTER_CHECK_COUNTED_NONE when it is no
    // member access as written (past parentheses), _MEMBER for one of no counted_by count, _COUNTED for one with it, and
    // then the count's member's type.
    uint8_t counted_member;
    TypeId counted_type;
    // Whether an explicit cast was stripped before that literal, which the literal-only arguments refuse, and the objects of
    // the functions the first argument names (CheckBuiltinCall's, #1126's review).
    bool string_cast;
    const uint32_t* function_objects;
    uint32_t function_object_count;
    // The unit's __NSConstantString, which CFString's and NSString's constant strings point to (CheckGraph's), or 0.
    TypeId ns_constant_string;
};

typedef struct CheckBuiltinMiscResult CheckBuiltinMiscResult;
struct CheckBuiltinMiscResult
{
    CheckActionResult action;
    // A separate source constant-evaluation result. A nonzero mask names the only arguments that evaluator
    // visits; ordinary runtime evaluation still uses evaluated_operands and action.value, including all effects.
    CheckValue constant_value;
    uint32_t constant_evaluated_operands;
    // A carry chain's four (c_check_builtin_bits.c); every other's at most three.
    CheckConversion argument_conversions[4];
    uint32_t evaluated_operands;
    uint32_t warnings;
    uint32_t unsupported;
    uint32_t frame_level;
    uint64_t alignment;
    uint64_t alignment_offset;
    CBuiltinId runtime_builtin;
    CheckBuiltinMiscConstraint constraint;
    uint32_t warning_operands;
    uint32_t prefetch_rw;
    uint32_t prefetch_locality;
    bool terminates;
    bool reads_memory;
    bool writes_memory;
    bool allocates_stack;
    bool alignment_offset_known;
    // __builtin_cpu_supports's test, its word (0 __cpu_model.features[0], 1 to 3 __cpu_features2's) in the high 32 bits
    // and its bit's mask in the low ones, 0 for a feature clang doesn't know, whose call is 0; or __builtin_cpu_is's, its
    // field of __cpu_model (0 vendor, 1 type, 2 subtype) and the value it must have (c_builtin_cpu.inc).
    uint64_t cpu_test;
};

static bool check_builtin_misc_source_complete(const TypeTable* table, TypeId type, const CheckBuiltinMiscAction* action)
{
    const bool complete = type_is_complete(table, type) && check_context_tag_visible(table, type, &action->context);
    return complete;
}

static CheckValue check_builtin_misc_argument(TypeTable* table, const CheckBuiltinMiscAction* input, CheckBuiltinMiscResult* output, uint32_t index)
{
    CheckValue value = input->arguments[index];
    CheckActionResult converted = { 0 };
    const uint8_t kind = type_is_valid(table, value.type) ? check_action_kind(table, value.type) : 0;
    if (!type_is_valid(table, value.type) || value.category > CHECK_FUNCTION)
    {
        check_action_diagnose(&output->action, CHECK_DIAG_INVALID_TYPE, index);
    }
    else if ((kind == BUSTER_TYPE_KIND_ENUM || (value.category == CHECK_LVALUE && (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION))) &&
             !check_builtin_misc_source_complete(table, value.type, input))
    {
        check_action_diagnose(&output->action, CHECK_DIAG_INCOMPLETE, index);
    }
    else
    {
        const CheckAction action = {
            .operands = { value },
            .node = input->node,
            .context = input->context,
        };
        value = check_action_operand(table, &action, &converted, 0);
        if (converted.diagnostic != CHECK_DIAG_NONE)
        {
            check_action_diagnose(&output->action, converted.diagnostic, index);
        }
    }
    output->argument_conversions[index] = converted.conversions[0];
    return value;
}

static bool check_builtin_misc_convert(TypeTable* table, const CheckBuiltinMiscAction* input, CheckBuiltinMiscResult* output, CheckValue* value, uint32_t index, TypeId destination)
{
    const CheckAction context = {
        .node = input->node,
        .context = input->context,
    };
    bool accepted = destination != 0 && output->action.diagnostic == CHECK_DIAG_NONE;
    if (destination == 0)
    {
        check_action_diagnose(&output->action, CHECK_DIAG_RESOURCE, index);
    }
    else if (accepted && check_action_kind(table, value->type) == BUSTER_TYPE_KIND_COMPLEX)
    {
        output->unsupported |= BUSTER_CHECK_BUILTIN_MISC_UNSUPPORTED_COMPLEX;
        check_action_diagnose(&output->action, CHECK_DIAG_UNSUPPORTED, index);
        accepted = false;
    }
    else if (accepted && check_action_kind(table, destination) == BUSTER_TYPE_KIND_POINTER &&
             check_action_kind(table, value->type) == BUSTER_TYPE_KIND_POINTER)
    {
        const TypeId to = type_element(table, type_strip_aligned(table, destination));
        const TypeId from = type_element(table, type_strip_aligned(table, value->type));
        const TypeId to_base = type_unqualified(type_strip_aligned(table, to));
        const TypeId from_base = type_unqualified(type_strip_aligned(table, from));
        const uint8_t to_kind = check_action_kind(table, to);
        const uint8_t from_kind = check_action_kind(table, from);
        const bool void_pointer = to_kind == BUSTER_TYPE_KIND_VOID || from_kind == BUSTER_TYPE_KIND_VOID;
        const bool function_pointer = to_kind == BUSTER_TYPE_KIND_FUNCTION || from_kind == BUSTER_TYPE_KIND_FUNCTION;
        const bool char_pair = (to_base == BUSTER_TYPE_CHAR || to_base == BUSTER_TYPE_SCHAR || to_base == BUSTER_TYPE_UCHAR) &&
                               (from_base == BUSTER_TYPE_CHAR || from_base == BUSTER_TYPE_SCHAR || from_base == BUSTER_TYPE_UCHAR);
        if (void_pointer || char_pair || check_action_compatible(table, to_base, from_base, &output->action))
        {
            if ((type_qualifiers(from) & ~type_qualifiers(to)) != 0)
            {
                output->warnings |= BUSTER_CHECK_BUILTIN_MISC_WARN_QUALIFIERS;
                output->warning_operands |= UINT32_C(1) << index;
            }
            if (void_pointer && function_pointer)
            {
                // Clang accepts function/void pointer conversion as an extension (pedantic diagnostic).
                output->warnings |= BUSTER_CHECK_BUILTIN_MISC_WARN_FUNCTION_POINTER;
                output->warning_operands |= UINT32_C(1) << index;
            }
            if (!void_pointer && char_pair && to_base != from_base)
            {
                output->warnings |= BUSTER_CHECK_BUILTIN_MISC_WARN_POINTER_SIGN;
                output->warning_operands |= UINT32_C(1) << index;
            }
        }
        else
        {
            check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, index);
            accepted = false;
        }
    }
    else if (accepted && !check_action_assignable(table, destination, value, &context, &output->action))
    {
        check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, index);
        accepted = false;
    }
    if (accepted)
    {
        check_action_conversion(table, value, destination, BUSTER_CHECK_CONVERT_ASSIGNMENT, output->argument_conversions + index, input->node);
    }
    return accepted;
}

// An argument as clang's evaluator reads it, converted to `destination`: the source argument read as in a constant
// context, which reads a const object's constant initializer (`const long k = 9;`'s 9), where a call's runtime view of it
// doesn't. An action that is constant evaluated already has it.
static CheckValue check_builtin_misc_evaluated(TypeTable* table, const CheckBuiltinMiscAction* input, const CheckValue* value, uint32_t index, TypeId destination)
{
    CheckValue result = *value;
    if (!input->context.constant_evaluated)
    {
        CheckContext evaluation = input->context;
        evaluation.constant_evaluated = true;
        const CheckAction context = {
            .operands = { input->arguments[index] },
            .node = input->node,
            .context = evaluation,
        };
        CheckActionResult read = { 0 };
        result = check_action_operand(table, &context, &read, 0);
        if (read.diagnostic == CHECK_DIAG_NONE)
        {
            check_action_convert_value(table, &result, destination, input->node);
        }
        else
        {
            result.constant_kind = CHECK_CONSTANT_NONE;
        }
    }
    return result;
}

// expect_with_probability's probability (clang's check after its prototype's conversion): converted to double, it must be
// a constant floating expression without side effects or faults (a const object's constant initializer is one, a variable
// or a call isn't), in [0, 1], which a NaN isn't. Clang evaluates it as a constant expression, so its value is the
// evaluator's view (check_builtin_misc_evaluated), and a discarded operand that evaluator can't evaluate fails it too
// (BUSTER_CHECK_DISCARDED_UNEVALUABLE: `(s, 0.75)`). It is never evaluated at run time.
static bool check_builtin_misc_probability(TypeTable* table, const CheckBuiltinMiscAction* input, CheckBuiltinMiscResult* output, CheckValue* value)
{
    bool accepted = check_builtin_misc_convert(table, input, output, value, 2, BUSTER_TYPE_DOUBLE);
    const CheckValue probability = accepted ? check_builtin_misc_evaluated(table, input, value, 2, BUSTER_TYPE_DOUBLE) : *value;
    const bool constant = probability.fault == CHECK_FAULT_NONE &&
                          (probability.flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_DISCARDED_UNEVALUABLE)) == 0 &&
                          probability.constant_kind == CHECK_CONSTANT_FLOATING;
    if (accepted && !constant)
    {
        output->constraint = CHECK_BUILTIN_MISC_CONSTRAINT_CONSTANT;
        check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, 2);
        accepted = false;
    }
    else if (accepted && !((double)probability.floating >= 0.0 && (double)probability.floating <= 1.0))
    {
        output->constraint = CHECK_BUILTIN_MISC_CONSTRAINT_RANGE;
        check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, 2);
        accepted = false;
    }
    return accepted;
}

// expect and expect_with_probability: their two longs, and the latter's probability (check_builtin_misc_probability).
// Clang's evaluator visits the converted first argument alone, reading a const object's constant initializer
// (check_builtin_misc_evaluated), and its code generation emits a call that evaluates to a constant as that constant: a
// first argument that the evaluator reads as an integer constant without side effects is the call's value, and neither
// argument runs, the expected value's effects (a call, a volatile read) dropped. Otherwise both run, in order, and the
// first is the value. constant_value is the evaluator's view, the first argument alone.
static void check_builtin_misc_expect(TypeTable* table, const CheckBuiltinMiscAction* input, CheckBuiltinMiscResult* output, CheckValue* values)
{
    const bool first = check_builtin_misc_convert(table, input, output, values, 0, BUSTER_TYPE_LONG);
    const bool second = check_builtin_misc_convert(table, input, output, values + 1, 1, BUSTER_TYPE_LONG);
    const bool probability = input->id != CBUILTIN_EXPECT_WITH_PROBABILITY || check_builtin_misc_probability(table, input, output, values + 2);
    if (first && second && probability)
    {
        output->constant_evaluated_operands = 1;
        output->constant_value = check_builtin_misc_evaluated(table, input, values, 0, BUSTER_TYPE_LONG);
        output->constant_value.category = CHECK_RVALUE;
        output->constant_value.flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_NULL_POINTER);
        const bool folded = output->constant_value.constant_kind == CHECK_CONSTANT_INTEGER && output->constant_value.fault == CHECK_FAULT_NONE &&
                            (output->constant_value.flags & BUSTER_CHECK_SIDE_EFFECT) == 0;
        if (output->constant_value.fault != CHECK_FAULT_NONE)
        {
            output->constant_value.constant_kind = CHECK_CONSTANT_NONE;
        }
        else if (folded)
        {
            check_action_builtin_ice(&output->constant_value);
        }
        output->action.value = folded ? output->constant_value : values[0];
        output->action.value.category = CHECK_RVALUE;
        if (!folded)
        {
            // Both run: the expected value's effects, a nonconstant one's evaluation. The probability never runs. Neither
            // parameter requires an ICE.
            output->action.value.flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_NULL_POINTER);
            check_action_inherit(&output->action.value, values + 1);
            if (output->action.value.fault != CHECK_FAULT_NONE)
            {
                output->action.value.constant_kind = CHECK_CONSTANT_NONE;
            }
            output->evaluated_operands = 3;
        }
        else
        {
            output->evaluated_operands = 0;
        }
    }
}

static bool check_builtin_misc_constant(TypeTable* table, const CheckValue* value, uint32_t operand, CheckBuiltinMiscResult* output)
{
    bool constant = false;
    if (!check_action_is_integer(table, value->type))
    {
        check_action_diagnose(&output->action, CHECK_DIAG_INTEGER_REQUIRED, operand);
    }
    else if (value->fault != CHECK_FAULT_NONE || value->constant_kind != CHECK_CONSTANT_INTEGER ||
             (value->flags & BUSTER_CHECK_SIDE_EFFECT) != 0)
    {
        output->constraint = CHECK_BUILTIN_MISC_CONSTRAINT_CONSTANT;
        check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, operand);
    }
    else if ((value->flags & BUSTER_CHECK_ICE) == 0)
    {
        // A folded value alone cannot certify the AST's ICE/extension rule (for example a comma expression).
        output->constraint = CHECK_BUILTIN_MISC_CONSTRAINT_CONSTANT;
        const CheckDiagnostic diagnostic = (value->flags & BUSTER_CHECK_FLOAT_LITERAL) != 0 ? CHECK_DIAG_INCOMPATIBLE : CHECK_DIAG_UNSUPPORTED;
        check_action_diagnose(&output->action, diagnostic, operand);
    }
    else
    {
        constant = true;
    }
    return constant;
}

static TypeId check_builtin_misc_pointer(TypeTable* table, TypeId element, uint32_t qualifiers)
{
    return type_pointer(table, type_qualify(table, element, qualifiers));
}

static void check_builtin_misc_assume_aligned(TypeTable* table, const CheckBuiltinMiscAction* input, CheckBuiltinMiscResult* output, CheckValue* values)
{
    if (check_action_kind(table, values[0].type) != BUSTER_TYPE_KIND_POINTER)
    {
        // Unlike an ordinary void* parameter, the custom checker does not convert an integer zero to a pointer.
        check_action_diagnose(&output->action, CHECK_DIAG_POINTER_REQUIRED, 0);
    }
    else if (check_builtin_misc_constant(table, values + 1, 1, output))
    {
        uint32_t width = 0;
        bool is_signed = false;
        const bool integer = check_action_integer_type(table, values[1].type, &width, &is_signed);
        const unsigned __int128 bits = integer ? values[1].integer & check_action_mask(width) : 0;
        if (!integer || bits == 0 || (bits & (bits - 1)) != 0)
        {
            output->constraint = CHECK_BUILTIN_MISC_CONSTRAINT_ALIGNMENT;
            check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, 1);
        }
        else
        {
            // Clang's power test uses raw bits, so a signed minimum with one set bit is not rejected as negative.
            const bool negative = is_signed && (bits >> (width - 1)) != 0;
            if (!negative && bits > (UINT64_C(1) << 32))
            {
                output->warnings |= BUSTER_CHECK_BUILTIN_MISC_WARN_ALIGNMENT;
                output->warning_operands |= 2;
            }
            if (bits > UINT64_MAX)
            {
                output->unsupported |= BUSTER_CHECK_BUILTIN_MISC_UNSUPPORTED_ALIGNMENT;
            }
            else
            {
                output->alignment = (uint64_t)bits;
            }
        }
    }
    output->action.value.type = check_builtin_misc_pointer(table, BUSTER_TYPE_VOID, 0);
    if (output->action.value.type == 0)
    {
        check_action_diagnose(&output->action, CHECK_DIAG_RESOURCE, 0);
    }
    output->alignment_offset_known = input->argument_count == 2;
    if (input->argument_count == 3 && check_builtin_misc_convert(table, input, output, values + 2, 2, BUSTER_TYPE_ULONG))
    {
        output->alignment_offset_known = values[2].constant_kind == CHECK_CONSTANT_INTEGER && values[2].fault == CHECK_FAULT_NONE;
        output->alignment_offset = output->alignment_offset_known ? (uint64_t)values[2].integer : 0;
        check_action_inherit(&output->action.value, values + 2);
    }
    // A pointer identity is not enough to prove its asserted alignment during source constant evaluation.
    // Keep the runtime identity and promise metadata; no memory bytes or symbolic-object alignment are invented.
}

// Whether an alignment builtin's operand type is one of clang's (BuiltinAlignment's IsValidIntegerType): an integer type,
// _BitInt's among them, but bool and the enumerations; an _Atomic one isn't.
static bool check_builtin_misc_align_integer(const TypeTable* table, TypeId type)
{
    const uint8_t kind = type_kind(table, type_strip_aligned(table, type));
    const bool result = (kind == BUSTER_TYPE_KIND_INTEGER || kind == BUSTER_TYPE_KIND_BIT_INT) &&
                        type_unqualified(type_strip_aligned(table, type)) != BUSTER_TYPE_BOOL;
    return result;
}

// The alignment builtins (cbuiltin_align, clang's BuiltinAlignment), of the raw arguments' types, before their lvalue
// conversions: a source of an integer type (check_builtin_misc_align_integer) or an object pointer, an array decayed to its
// element's pointer, a function and a function pointer neither, of any width; then an alignment of such an integer type,
// of any width too (#1447: past 128 bits by its constant's limbs, c_check_wide.c). A constant alignment, which clang evaluates past its side effects and through const objects' constant initializers
// (CheckBuiltinMiscAction.alignment_evaluated), must be at least 1, at most 2 to the source's width less 1 (a pointer's
// 64), and a power of 2, in that order; 1 draws -Wtautological-compare (recorded, not reported). align_up and align_down
// are of the source's type, its qualifiers kept, and is_aligned is a bool. Each folds, as an integer constant expression,
// where clang's evaluator does: of a source and an alignment that are constants without side effects, in the source's
// width ((x + (a - 1)) & ~(a - 1), x & ~(a - 1) and (x & (a - 1)) == 0); and of an address constant: with an alignment of
// 1, the same address or 1; else where the call's evidence gives its object's declaration alignment
// (CheckBuiltinMiscAction.pointer_alignment), the same address or 1 where its alignment at the offset is at least the
// alignment, the offset rounded or 0 where the object's is, and not at all otherwise. A folded integer evaluates neither
// argument, as clang's code doesn't; an address still evaluates both, as clang computes it at run time.
static void check_builtin_misc_align(TypeTable* table, const CheckBuiltinMiscAction* input, CheckBuiltinMiscResult* output, CheckValue* values)
{
    const CheckValue* raw = input->arguments;
    const bool decayed = type_kind_is_array(check_action_kind(table, raw[0].type));
    const TypeId source = decayed ? values[0].type : raw[0].type;
    const uint8_t kind = check_action_kind(table, source);
    const bool pointer = kind == BUSTER_TYPE_KIND_POINTER &&
                         check_action_kind(table, type_element(table, type_strip_aligned(table, source))) != BUSTER_TYPE_KIND_FUNCTION;
    const bool integer = check_builtin_misc_align_integer(table, source);
    uint32_t width = 64;
    bool is_signed = false;
    uint32_t alignment_width = 0;
    bool alignment_signed = false;
    unsigned __int128 alignment = 0;
    uint32_t exponent = 0;
    bool known = false;
    if (!pointer && !integer)
    {
        check_action_diagnose(&output->action, CHECK_DIAG_SCALAR_REQUIRED, 0);
    }
    else if (!check_builtin_misc_align_integer(table, raw[1].type))
    {
        check_action_diagnose(&output->action, CHECK_DIAG_INTEGER_REQUIRED, 1);
    }
    else
    {
        // A source past 128 bits (#1447), which clang takes: its width, and its constants' limbs (c_check_wide.c).
        if (integer && !check_action_integer_type(table, source, &width, &is_signed))
        {
            uint64_t rank = 0;
            uint64_t bits = 0;
            type_integer_properties(table, type_unqualified(type_strip_aligned(table, source)), &rank, &bits, &is_signed);
            width = (uint32_t)bits;
        }
        // The alignment as clang's evaluator reads it where the call's evidence has it, else the runtime view's constant.
        // A modification's value (`(s = 3)`) is no constant to it at all (BUSTER_CHECK_MODIFICATION), which leaves the
        // alignment unchecked. One past 128 bits (#1447) is read by its shape (check_wide_alignment_shape): its sign,
        // zero, significant bits and whether it is a power of 2, which are all its checks and folds need.
        const bool evaluated = input->alignment_evaluated;
        const bool constant = evaluated || (values[1].constant_kind == CHECK_CONSTANT_INTEGER && values[1].fault == CHECK_FAULT_NONE &&
                                            (values[1].flags & BUSTER_CHECK_MODIFICATION) == 0);
        const bool narrow = check_action_integer_type(table, raw[1].type, &alignment_width, &alignment_signed);
        const bool effects = evaluated ? input->alignment_side_effects : (values[1].flags & BUSTER_CHECK_SIDE_EFFECT) != 0;
        bool negative = false;
        bool zero = false;
        bool power = false;
        uint32_t significant = 0;
        bool shaped = constant;
        if (constant && narrow)
        {
            const unsigned __int128 bits = (evaluated ? input->alignment_value : values[1].integer) & check_action_mask(alignment_width);
            negative = alignment_signed && (bits >> (alignment_width - 1)) != 0;
            zero = bits == 0;
            power = bits != 0 && (bits & (bits - 1)) == 0;
            const uint64_t high = (uint64_t)(bits >> 64);
            significant = high != 0 ? 128u - (uint32_t)__builtin_clzll(high) : (uint64_t)bits != 0 ? 64u - (uint32_t)__builtin_clzll((uint64_t)bits) : 0;
        }
        else if (constant)
        {
            CheckValue wide = values[1];
            if (evaluated)
            {
                wide.constant_kind = CHECK_CONSTANT_INTEGER;
                wide.integer = input->alignment_value;
                wide.floating_storage[0] = input->alignment_limbs;
            }
            shaped = check_wide_alignment_shape(table, &wide, &negative, &zero, &significant, &power);
        }
        // At least 1, at most 2^(width - 1) (a value past it has more significant bits, or as many and isn't that power),
        // and a power of 2, in clang's order.
        if (shaped && (negative || zero || significant > width || (significant == width && !power)))
        {
            output->constraint = negative || zero ? CHECK_BUILTIN_MISC_CONSTRAINT_MINIMUM : CHECK_BUILTIN_MISC_CONSTRAINT_RANGE;
            check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, 1);
        }
        else if (shaped && !power)
        {
            output->constraint = CHECK_BUILTIN_MISC_CONSTRAINT_ALIGNMENT;
            check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, 1);
        }
        else if (shaped)
        {
            exponent = significant - 1;
            alignment = exponent < 128 ? (unsigned __int128)1 << exponent : 0;
            known = !effects;
            output->warnings |= significant == 1 ? BUSTER_CHECK_BUILTIN_MISC_WARN_ALIGNED_TO_ONE : 0;
            output->warning_operands |= significant == 1 ? 2 : 0;
        }
    }
    const bool aligned = input->id == CBUILTIN_IS_ALIGNED;
    output->action.value.type = aligned ? BUSTER_TYPE_BOOL : source;
    output->action.value.category = CHECK_RVALUE;
    const bool clean = known && output->action.diagnostic == CHECK_DIAG_NONE && values[0].fault == CHECK_FAULT_NONE &&
                       (values[0].flags & BUSTER_CHECK_SIDE_EFFECT) == 0;
    const unsigned __int128 mask = alignment - 1;
    if (clean && integer && width > 128 && values[0].constant_kind == CHECK_CONSTANT_INTEGER)
    {
        // A source past 128 bits (#1447): its limbs' fold, by the alignment's exponent.
        const uint32_t kind = aligned ? 2 : input->id == CBUILTIN_ALIGN_UP ? 1 : 0;
        CheckValue folded = output->action.value;
        if (check_wide_align_fold(table, values, exponent, kind, &folded))
        {
            output->action.value = folded;
            output->action.value.flags |= BUSTER_CHECK_ICE;
            output->evaluated_operands = 0;
        }
    }
    else if (clean && integer && width <= 128 && values[0].constant_kind == CHECK_CONSTANT_INTEGER)
    {
        const unsigned __int128 all = check_action_mask(width);
        const unsigned __int128 x = values[0].integer & all;
        output->action.value.constant_kind = CHECK_CONSTANT_INTEGER;
        output->action.value.integer = aligned ? (x & mask) == 0 : input->id == CBUILTIN_ALIGN_UP ? (x + mask) & ~mask & all : x & ~mask;
        output->action.value.flags |= BUSTER_CHECK_ICE;
        output->evaluated_operands = 0;
    }
    else if (clean && pointer && values[0].constant_kind == CHECK_CONSTANT_ADDRESS && values[0].object != 0)
    {
        // Clang's getBaseAlignment and alignmentAtOffset: the object's declaration alignment, and the lowest set bit of it
        // and the offset.
        const bool witnessed = input->pointer_alignment != 0 && input->pointer_object == values[0].object;
        const uint64_t base = alignment == 1 ? 1 : witnessed ? UINT64_C(1) << (input->pointer_alignment - 1) : 0;
        const uint64_t offset = (uint64_t)values[0].offset;
        const uint64_t at = base == 0 ? 0 : (base | offset) & (0 - (base | offset));
        const bool same = base != 0 && at >= alignment;
        const bool rounded = !same && base >= alignment;
        if (aligned && (same || rounded))
        {
            output->action.value.constant_kind = CHECK_CONSTANT_INTEGER;
            output->action.value.integer = same;
            output->action.value.flags |= BUSTER_CHECK_ICE;
            output->evaluated_operands = 0;
        }
        else if (same || rounded)
        {
            const uint64_t step = (uint64_t)mask;
            output->action.value.constant_kind = CHECK_CONSTANT_ADDRESS;
            output->action.value.object = values[0].object;
            output->action.value.flags |= values[0].flags & BUSTER_CHECK_STATIC_OBJECT;
            output->action.value.offset = same ? values[0].offset : (int64_t)(input->id == CBUILTIN_ALIGN_UP ? (offset + step) & ~step : offset & ~step);
        }
    }
}

static void check_builtin_misc_prefetch(TypeTable* table, const CheckBuiltinMiscAction* input, CheckBuiltinMiscResult* output, CheckValue* values)
{
    const TypeId pointer = check_builtin_misc_pointer(table, BUSTER_TYPE_VOID, BUSTER_TYPE_CONST);
    check_builtin_misc_convert(table, input, output, values, 0, pointer);
    output->action.value.type = BUSTER_TYPE_VOID;
    for (uint32_t i = 1; i < input->argument_count && output->action.diagnostic == CHECK_DIAG_NONE; i += 1)
    {
        // Optional arguments are variadic: use default promotions, not a guessed int formal parameter.
        check_action_promote(table, values + i, output->argument_conversions + i, input->node);
        if (check_builtin_misc_constant(table, values + i, i, output))
        {
            uint32_t width = 0;
            bool is_signed = false;
            const bool integer = check_action_integer_type(table, values[i].type, &width, &is_signed);
            const unsigned __int128 bits = integer ? values[i].integer & check_action_mask(width) : 0;
            const uint32_t maximum = i == 1 ? 1 : 3;
            if (!integer || bits > maximum)
            {
                output->constraint = CHECK_BUILTIN_MISC_CONSTRAINT_RANGE;
                check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, i);
            }
            else if (i == 1)
            {
                output->prefetch_rw = (uint32_t)bits;
            }
            else
            {
                output->prefetch_locality = (uint32_t)bits;
            }
        }
    }
    for (uint32_t i = 0; i < input->argument_count; i += 1)
    {
        check_action_inherit(&output->action.value, values + i);
    }
}

static void check_builtin_misc_memory(TypeTable* table, const CheckBuiltinMiscAction* input, CheckBuiltinMiscResult* output, CheckValue* values)
{
    TypeId parameters[3] = { 0, 0, BUSTER_TYPE_ULONG };
    if (input->id == CBUILTIN_STRLEN)
    {
        parameters[0] = check_builtin_misc_pointer(table, BUSTER_TYPE_CHAR, BUSTER_TYPE_CONST);
        output->action.value.type = BUSTER_TYPE_ULONG;
    }
    else if (input->id == CBUILTIN_MEMCMP)
    {
        parameters[0] = check_builtin_misc_pointer(table, BUSTER_TYPE_VOID, BUSTER_TYPE_CONST);
        parameters[1] = parameters[0];
        output->action.value.type = BUSTER_TYPE_INT;
    }
    else
    {
        parameters[0] = input->id == CBUILTIN_ALLOCA ? BUSTER_TYPE_ULONG : check_builtin_misc_pointer(table, BUSTER_TYPE_VOID, 0);
        output->action.value.type = check_builtin_misc_pointer(table, BUSTER_TYPE_VOID, 0);
        if (input->id == CBUILTIN_MEMCPY || input->id == CBUILTIN_MEMMOVE)
        {
            parameters[1] = check_builtin_misc_pointer(table, BUSTER_TYPE_VOID, BUSTER_TYPE_CONST);
        }
    }
    if (input->id == CBUILTIN_MEMSET)
    {
        parameters[1] = BUSTER_TYPE_INT;
    }
    if (output->action.value.type == 0)
    {
        check_action_diagnose(&output->action, CHECK_DIAG_RESOURCE, 0);
    }
    for (uint32_t i = 0; i < input->argument_count && output->action.diagnostic == CHECK_DIAG_NONE; i += 1)
    {
        check_builtin_misc_convert(table, input, output, values + i, i, parameters[i]);
        check_action_inherit(&output->action.value, values + i);
    }
    output->writes_memory = input->id == CBUILTIN_MEMCPY || input->id == CBUILTIN_MEMMOVE || input->id == CBUILTIN_MEMSET;
    output->reads_memory = input->id == CBUILTIN_MEMCPY || input->id == CBUILTIN_MEMMOVE || input->id == CBUILTIN_MEMCMP || input->id == CBUILTIN_STRLEN;
    output->allocates_stack = input->id == CBUILTIN_ALLOCA;
    if (output->writes_memory || output->allocates_stack)
    {
        output->action.value.flags |= BUSTER_CHECK_SIDE_EFFECT;
    }
    // The opaque memory state belongs to the source/runtime consumer, not a CheckValue. No strlen/memcmp result,
    // copied bytes, allocation address or successful zero-length access is fabricated by this checking action.
}

static void check_builtin_misc_frame(TypeTable* table, const CheckBuiltinMiscAction* input, CheckBuiltinMiscResult* output, CheckValue* values)
{
    const TypeId pointer = check_builtin_misc_pointer(table, BUSTER_TYPE_VOID, 0);
    output->action.value.type = pointer;
    if (pointer == 0)
    {
        check_action_diagnose(&output->action, CHECK_DIAG_RESOURCE, 0);
    }
    else if (input->id == CBUILTIN_FRAME_ADDRESS || input->id == CBUILTIN_RETURN_ADDRESS)
    {
        if (check_builtin_misc_convert(table, input, output, values, 0, BUSTER_TYPE_UINT) &&
            check_builtin_misc_constant(table, values, 0, output))
        {
            const uint32_t level = (uint32_t)values[0].integer;
            if (level > 65535)
            {
                output->constraint = CHECK_BUILTIN_MISC_CONSTRAINT_RANGE;
                check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, 0);
            }
            else
            {
                // The level, an integer constant expression without effects, is the builtin's own: clang's code computes
                // nothing for it.
                output->frame_level = level;
                output->reads_memory = true;
                output->evaluated_operands = 0;
                if (level != 0)
                {
                    output->warnings |= BUSTER_CHECK_BUILTIN_MISC_WARN_FRAME_LEVEL;
                    output->warning_operands |= 1;
                    output->unsupported |= BUSTER_CHECK_BUILTIN_MISC_UNSUPPORTED_FRAME_LEVEL;
                }
            }
        }
    }
    else
    {
        check_builtin_misc_convert(table, input, output, values, 0, pointer);
    }
    check_action_inherit(&output->action.value, values);
}

// GCC's type class of a type, as clang's evaluator gives it in C (EvaluateBuiltinClassifyType): void 0, an integer or an
// enumeration 1, bool 4, a pointer, an array or a function (each decays) 5, a floating type 8, a complex one 9, a struct 12,
// a union 13, a _BitInt 18, a vector 19, and nullptr_t none (-1). An _Atomic type is its value type's class, and
// qualifiers and alignment don't count.
static int32_t check_builtin_misc_type_class(const TypeTable* table, TypeId type)
{
    TypeId base = type_strip_aligned(table, type);
    while (type_kind(table, base) == BUSTER_TYPE_KIND_ATOMIC)
    {
        base = type_strip_aligned(table, type_element(table, base));
    }
    int32_t result = -1;
    switch (type_kind(table, base))
    {
        case BUSTER_TYPE_KIND_VOID: result = 0; break;
        case BUSTER_TYPE_KIND_INTEGER: result = type_unqualified(base) == BUSTER_TYPE_BOOL ? 4 : 1; break;
        case BUSTER_TYPE_KIND_ENUM: result = 1; break;
        case BUSTER_TYPE_KIND_FLOATING: result = 8; break;
        case BUSTER_TYPE_KIND_COMPLEX: result = 9; break;
        case BUSTER_TYPE_KIND_POINTER:
        case BUSTER_TYPE_KIND_ARRAY:
        case BUSTER_TYPE_KIND_INCOMPLETE_ARRAY:
        case BUSTER_TYPE_KIND_VARIABLE_ARRAY:
        case BUSTER_TYPE_KIND_FUNCTION: result = 5; break;
        case BUSTER_TYPE_KIND_STRUCT: result = 12; break;
        case BUSTER_TYPE_KIND_UNION: result = 13; break;
        case BUSTER_TYPE_KIND_BIT_INT: result = 18; break;
        case BUSTER_TYPE_KIND_VECTOR:
        case BUSTER_TYPE_KIND_EXT_VECTOR: result = 19; break;
        default: result = -1; break;
    }
    return result;
}

// An int constant expression of `value`, as the generic builtins below fold: no operand runs.
// An os_log data argument's bytes (#148): its type's after the default argument promotions (DefaultVariadicArgumentPromotion),
// an array's or a function's as a pointer's, a float's and an __fp16's as a double's, an integer's as its promotion's.
// _Float16 and __bf16 aren't promoted: 2 bytes each, as clang's.
static uint64_t check_builtin_misc_os_log_size(const TypeTable* table, TypeId type)
{
    const TypeId bare = type_is_valid(table, type) ? type_unqualified(type_strip_aligned(table, type)) : BUSTER_TYPE_INT;
    const uint8_t kind = type_kind(table, bare);
    const bool pointer = type_kind_is_array(kind) || kind == BUSTER_TYPE_KIND_FUNCTION || kind == BUSTER_TYPE_KIND_POINTER;
    const bool narrow_floating = bare == BUSTER_TYPE_FLOAT || bare == BUSTER_TYPE_HALF;
    const bool integer = check_action_is_integer(table, bare);
    const uint64_t result = pointer || narrow_floating ? 8 : integer ? type_size(table, type_promote(table, bare)) : type_size(table, bare);
    return result;
}

// The bytes of an os_log buffer item's data (#148): a data argument's (check_builtin_misc_os_log_size), a count's 4, a
// mask's 8, an errno's none.
static uint64_t check_builtin_misc_os_log_item_size(const TypeTable* table, const CheckBuiltinMiscAction* input, const CheckOSLogItem* item, uint32_t data)
{
    const uint64_t result = item->source == BUSTER_CHECK_OS_LOG_ARGUMENT ? check_builtin_misc_os_log_size(table, input->arguments[data + item->argument].type) :
                            item->kind == BUSTER_CHECK_OS_LOG_MASK ? 8 : item->source == BUSTER_CHECK_OS_LOG_CONSTANT ? 4 : 0;
    return result;
}

static void check_builtin_misc_fold_int(CheckBuiltinMiscResult* output, int32_t value)
{
    output->action.value = (CheckValue){
        .type = BUSTER_TYPE_INT,
        .category = CHECK_RVALUE,
        .flags = BUSTER_CHECK_ICE,
        .constant_kind = CHECK_CONSTANT_INTEGER,
        .integer = (unsigned __int128)(__int128)value,
    };
    output->constant_value = output->action.value;
    output->constant_evaluated_operands = 0;
    output->evaluated_operands = 0;
}

// clang's generic builtins of #148 that its Sema checks itself (custom typing or a constant argument):
// - __builtin_classify_type(e): its operand's GCC type class (check_builtin_misc_type_class), an int constant expression;
//   the operand, which never runs, is any expression, of an incomplete type too, by its type before any conversion;
// - __builtin_eh_return_data_regno(n): the DWARF register of x86-64's n-th exception data register, rax's 0 and rdx's 1,
//   and -1 for any other n, an int constant expression of n, which must be an integer constant expression;
// - __builtin_dwarf_cfa(): the function's canonical frame address, the caller's rsp before its call, which clang -O0's code
//   computes as rbp + 16 (its frame pointer's);
// - __builtin_launder(p): p, of its type, which must be a pointer to a complete object type: not void's nor a function's.
static void check_builtin_misc_generic(TypeTable* table, const CheckBuiltinMiscAction* input, CheckBuiltinMiscResult* output, CheckValue* values)
{
    if (input->id == CBUILTIN_CLASSIFY_TYPE)
    {
        check_builtin_misc_fold_int(output, check_builtin_misc_type_class(table, input->arguments[0].type));
    }
    else if (input->id == CBUILTIN_EH_RETURN_DATA_REGNO)
    {
        if (check_builtin_misc_convert(table, input, output, values, 0, BUSTER_TYPE_INT) && check_builtin_misc_constant(table, values, 0, output))
        {
            const __int128 n = (__int128)values[0].integer;
            check_builtin_misc_fold_int(output, n == 0 ? 0 : n == 1 ? 1 : -1);
        }
    }
    else if (input->id == CBUILTIN_DWARF_CFA)
    {
        const TypeId pointer = check_builtin_misc_pointer(table, BUSTER_TYPE_VOID, 0);
        output->action.value.type = pointer;
        output->reads_memory = true;
        if (pointer == 0)
        {
            check_action_diagnose(&output->action, CHECK_DIAG_RESOURCE, 0);
        }
    }
    else
    {
        const uint8_t kind = check_action_kind(table, values[0].type);
        const TypeId element = kind == BUSTER_TYPE_KIND_POINTER ? type_element(table, type_strip_aligned(table, values[0].type)) : 0;
        const uint8_t pointee = element != 0 ? check_action_kind(table, element) : 0;
        if (kind != BUSTER_TYPE_KIND_POINTER || pointee == BUSTER_TYPE_KIND_VOID || pointee == BUSTER_TYPE_KIND_FUNCTION)
        {
            check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, 0);
        }
        else if (!check_builtin_misc_source_complete(table, element, input))
        {
            check_action_diagnose(&output->action, CHECK_DIAG_INCOMPLETE, 0);
        }
        else
        {
            output->action.value = values[0];
            output->action.value.category = CHECK_RVALUE;
            output->action.value.flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_NULL_POINTER);
        }
    }
}

// The sanitizers __builtin_allow_sanitize_check names, as clang 23's Sema takes them.
static bool check_builtin_misc_sanitizer(const uint8_t* bytes, uint32_t length)
{
    static const char* const names[] = { "address", "thread", "memory", "hwaddress", "kernel-address", "kernel-hwaddress", "kernel-memory" };
    bool found = false;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(names) && !found; i += 1)
    {
        found = strlen(names[i]) == length && memcmp(names[i], bytes, length) == 0;
    }
    return found;
}

// More of clang's generic builtins of #148 that its Sema checks itself:
// - __builtin_alloca_uninitialized(n): __builtin_alloca's, whose bytes clang never initializes anyway;
// - __builtin_alloca_with_align(n, a) and its _uninitialized form: n bytes at an alignment of a bits, which must be an
//   integer constant expression, a power of 2, 8 or more and at most INT32_MAX (alignment, in bytes); a never runs;
// - __builtin_allow_runtime_check(s): true, as clang -O0's code has it, of a string literal s, which never runs; no
//   constant expression;
// - __builtin_allow_sanitize_check(s): false, as no sanitizer is on, of a string literal naming a sanitizer clang takes;
// - __builtin_annotation(x, s): x, of its integer type, s a narrow string literal; no constant expression;
// - __builtin_addressof(e): &e, as the operator takes it, of an lvalue or a function designator (not a bit-field, a
//   register variable or a vector's element): its address constant where e has one;
// - __builtin_function_start(f): f's address, a void*, of a function designator that names a function.
static void check_builtin_misc_more(TypeTable* table, const CheckBuiltinMiscAction* input, CheckBuiltinMiscResult* output, CheckValue* values)
{
    const CBuiltinId id = input->id;
    if (id == CBUILTIN_ALLOCA_UNINITIALIZED || id == CBUILTIN_ALLOCA_WITH_ALIGN || id == CBUILTIN_ALLOCA_WITH_ALIGN_UNINITIALIZED)
    {
        const TypeId pointer = check_builtin_misc_pointer(table, BUSTER_TYPE_VOID, 0);
        output->action.value.type = pointer;
        bool accepted = pointer != 0 && check_builtin_misc_convert(table, input, output, values, 0, BUSTER_TYPE_ULONG);
        if (pointer == 0)
        {
            check_action_diagnose(&output->action, CHECK_DIAG_RESOURCE, 0);
        }
        if (accepted && id != CBUILTIN_ALLOCA_UNINITIALIZED)
        {
            accepted = check_builtin_misc_convert(table, input, output, values + 1, 1, BUSTER_TYPE_ULONG) && check_builtin_misc_constant(table, values + 1, 1, output);
            const uint64_t bits = accepted ? (uint64_t)values[1].integer : 0;
            if (accepted && ((bits & (bits - 1)) != 0 || bits < 8 || bits > INT32_MAX))
            {
                output->constraint = CHECK_BUILTIN_MISC_CONSTRAINT_ALIGNMENT;
                check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, 1);
            }
            else if (accepted)
            {
                output->alignment = bits / 8;
                output->evaluated_operands = 1;
            }
        }
        check_action_inherit(&output->action.value, values);
        output->allocates_stack = true;
        output->action.value.flags |= BUSTER_CHECK_SIDE_EFFECT;
    }
    else if (id == CBUILTIN_ALLOW_RUNTIME_CHECK || id == CBUILTIN_ALLOW_SANITIZE_CHECK)
    {
        // A string literal, parenthesized at most: clang refuses one in a cast (#1126's review).
        if (!input->string_known || input->string_cast)
        {
            check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, 0);
        }
        else if (id == CBUILTIN_ALLOW_SANITIZE_CHECK && !check_builtin_misc_sanitizer(input->string_bytes, input->string_length))
        {
            output->constraint = CHECK_BUILTIN_MISC_CONSTRAINT_RANGE;
            check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, 0);
        }
        else
        {
            output->action.value = (CheckValue){ .type = BUSTER_TYPE_BOOL, .category = CHECK_RVALUE };
            output->evaluated_operands = 0;
        }
    }
    else if (id == CBUILTIN_ANNOTATION)
    {
        if (!check_action_is_integer(table, values[0].type))
        {
            check_action_diagnose(&output->action, CHECK_DIAG_INTEGER_REQUIRED, 0);
        }
        else if (!input->string_known)
        {
            check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, 1);
        }
        else
        {
            output->action.value = values[0];
            output->action.value.category = CHECK_RVALUE;
            output->action.value.constant_kind = CHECK_CONSTANT_NONE;
            output->action.value.flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_FLOAT_LITERAL |
                                            BUSTER_CHECK_NULL_POINTER | BUSTER_CHECK_LITERAL_ZERO);
            output->evaluated_operands = 1;
        }
    }
    else
    {
        // addressof and function_start take their operand as it is, before any conversion, as the & operator does; and
        // function_start an address constant too, as clang's getAsBuiltinConstantDeclRef evaluates its operand to a
        // function at offset 0 (#1126's review): one of the functions the operand names (&f, (void *)&f, a cast of f,
        // `1 ? f : g`, `f + 0`), not an object's, a string's or a null pointer.
        const CheckValue operand = input->arguments[0];
        const bool start = id == CBUILTIN_FUNCTION_START;
        bool named = false;
        for (uint32_t i = 0; start && i < input->function_object_count; i += 1)
        {
            named |= operand.object != 0 && input->function_objects[i] == operand.object;
        }
        const bool designator = operand.category == CHECK_FUNCTION || (named && operand.category == CHECK_RVALUE && operand.offset == 0 && operand.object_bit_offset == 0);
        if (start && (!designator || operand.constant_kind != CHECK_CONSTANT_ADDRESS || operand.object == 0))
        {
            check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, 0);
        }
        else if ((operand.flags & BUSTER_CHECK_VECTOR_ELEMENT) != 0)
        {
            check_action_diagnose(&output->action, CHECK_DIAG_LVALUE_REQUIRED, 0);
        }
        else if ((operand.flags & BUSTER_CHECK_BIT_FIELD) != 0)
        {
            check_action_diagnose(&output->action, CHECK_DIAG_BIT_FIELD, 0);
        }
        else if ((operand.flags & BUSTER_CHECK_REGISTER) != 0)
        {
            check_action_diagnose(&output->action, CHECK_DIAG_REGISTER_ADDRESS, 0);
        }
        else if (operand.category != CHECK_LVALUE && !designator)
        {
            check_action_diagnose(&output->action, CHECK_DIAG_LVALUE_REQUIRED, 0);
        }
        else
        {
            output->action.value = operand;
            output->action.value.type = start ? check_builtin_misc_pointer(table, BUSTER_TYPE_VOID, 0) : type_pointer(table, operand.type);
            output->action.value.category = CHECK_RVALUE;
            output->action.value.flags |= (operand.flags & BUSTER_CHECK_INACTIVE_UNION_READ) != 0 ? BUSTER_CHECK_INACTIVE_UNION_ADDRESS : 0;
            output->action.value.flags &= ~(BUSTER_CHECK_INACTIVE_UNION_READ | BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_FLOAT_LITERAL |
                                            BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_BIT_FIELD | BUSTER_CHECK_REGISTER | BUSTER_CHECK_REGISTER_NAME | BUSTER_CHECK_VOLATILE_PART);
            if (!input->context.constant_evaluated && (operand.flags & BUSTER_CHECK_STATIC_OBJECT) == 0 && output->action.value.constant_kind == CHECK_CONSTANT_ADDRESS &&
                output->action.value.object != 0 && !designator)
            {
                output->action.value.constant_kind = CHECK_CONSTANT_NONE;
            }
            if (output->action.value.type == 0)
            {
                check_action_diagnose(&output->action, CHECK_DIAG_RESOURCE, 0);
            }
        }
    }
}

// __builtin_complex(re, im): the complex number re + im i, of type _Complex T, where re and im are of one real floating
// type T (clang 23 takes _Float16, __bf16 and __float128 too) once lvalue-converted, and are never converted otherwise, as
// clang's Sema checks them: each argument's type before its conversion first, so that an _Atomic one is refused (clang's
// "argument type is not a real floating point type", INVALID_TYPE), then the two converted types (its "arguments are of
// different types", INCOMPATIBLE), which check_semantic_expression words as clang's. It folds where both arguments are
// floating constants of a type whose complex constants the folds, the lowering and the data keep (float, double, long
// double, the half formats, and __float128's by their bits, #1250), as clang's evaluator folds it: what glibc's CMPLX of
// a static initializer reads.
static void check_builtin_misc_complex(TypeTable* table, const CheckBuiltinMiscAction* input, CheckBuiltinMiscResult* output, const CheckValue* values)
{
    const TypeId real = type_unqualified(type_strip_aligned(table, values[0].type));
    const TypeId imaginary = type_unqualified(type_strip_aligned(table, values[1].type));
    if (check_action_kind(table, input->arguments[0].type) != BUSTER_TYPE_KIND_FLOATING)
    {
        check_action_diagnose(&output->action, CHECK_DIAG_INVALID_TYPE, 0);
    }
    else if (check_action_kind(table, input->arguments[1].type) != BUSTER_TYPE_KIND_FLOATING)
    {
        check_action_diagnose(&output->action, CHECK_DIAG_INVALID_TYPE, 1);
    }
    else if (real != imaginary)
    {
        check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, 0);
    }
    else
    {
        const TypeId complex = type_complex(table, real);
        const bool quad = real == BUSTER_TYPE_FLOAT128;
        const bool folds = values[0].constant_kind == CHECK_CONSTANT_FLOATING && values[1].constant_kind == CHECK_CONSTANT_FLOATING &&
                           (check_action_float_supported(table, real) || check_action_half_format(real) != CHECK_HALF_NONE || quad);
        output->action.value.type = complex;
        output->action.value.category = CHECK_RVALUE;
        output->action.value.constant_kind = folds ? CHECK_CONSTANT_COMPLEX : CHECK_CONSTANT_NONE;
        output->action.value.floating = folds ? values[0].floating : 0.0L;
        output->action.value.imaginary = folds ? values[1].floating : 0.0L;
        if (folds && quad)
        {
            // A _Complex __float128's parts are its bits (#1250, check_quad_complex_set).
            check_quad_complex_set(&output->action.value, values[0].integer, values[1].integer);
        }
        output->action.value.flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_ICE_UNEVALUATED |
                                        BUSTER_CHECK_NULL_POINTER | BUSTER_CHECK_LITERAL_ZERO);
        if (complex == 0)
        {
            check_action_diagnose(&output->action, CHECK_DIAG_RESOURCE, 0);
        }
    }
}

// The arity of a pointer authentication builtin (#148), which clang's Sema checks before it refuses the call: x86 has no
// pointer authentication ("this target does not support pointer authentication").
static uint32_t check_builtin_misc_ptrauth_arity(CBuiltinId id)
{
    uint32_t arity = 3;
    switch (id)
    {
        case CBUILTIN_PTRAUTH_AUTH_AND_RESIGN: arity = 5; break;
        case CBUILTIN_PTRAUTH_AUTH_LOAD_RELATIVE_AND_SIGN:
        case CBUILTIN_PTRAUTH_AUTH_WITH_PC_AND_RESIGN: arity = 6; break;
        case CBUILTIN_PTRAUTH_BLEND_DISCRIMINATOR:
        case CBUILTIN_PTRAUTH_SIGN_GENERIC_DATA:
        case CBUILTIN_PTRAUTH_STRIP: arity = 2; break;
        case CBUILTIN_PTRAUTH_STRING_DISCRIMINATOR: arity = 1; break;
        default: break;
    }
    return arity;
}

// Whether a type is one __builtin_trivially_relocate moves in C: an element (through arrays) that is a scalar, a complex
// number or a vector, as clang's Sema has C's relocatable types; no struct or union, _Atomic or function.
static bool check_builtin_misc_relocatable(const TypeTable* table, TypeId type)
{
    TypeId element = type_strip_aligned(table, type);
    for (uint32_t depth = 0; depth < 64 && type_kind_is_array(type_kind(table, element)); depth += 1)
    {
        element = type_strip_aligned(table, type_element(table, element));
    }
    const uint8_t kind = type_kind(table, element);
    return kind == BUSTER_TYPE_KIND_INTEGER || kind == BUSTER_TYPE_KIND_FLOATING || kind == BUSTER_TYPE_KIND_POINTER || kind == BUSTER_TYPE_KIND_ENUM ||
           kind == BUSTER_TYPE_KIND_BIT_INT || kind == BUSTER_TYPE_KIND_COMPLEX || kind == BUSTER_TYPE_KIND_VECTOR || kind == BUSTER_TYPE_KIND_EXT_VECTOR ||
           kind == BUSTER_TYPE_KIND_NULLPTR;
}

// The names of __builtin_cpu_supports and __builtin_cpu_is (c_builtin_cpu.inc), each with its test of libgcc's model.
typedef struct CheckBuiltinCpuName CheckBuiltinCpuName;
struct CheckBuiltinCpuName
{
    const char* name;
    uint8_t place;
    uint32_t value;
};

static const CheckBuiltinCpuName check_builtin_cpu_features[] =
{
#define BUSTER_CBUILTIN_CPU_FEATURE(name, word, bit) { name, word, UINT32_C(1) << (bit) },
#define BUSTER_CBUILTIN_CPU_NAME(name, field, value)
#include "c_builtin_cpu.inc"
#undef BUSTER_CBUILTIN_CPU_NAME
#undef BUSTER_CBUILTIN_CPU_FEATURE
};

static const CheckBuiltinCpuName check_builtin_cpu_names[] =
{
#define BUSTER_CBUILTIN_CPU_FEATURE(name, word, bit)
#define BUSTER_CBUILTIN_CPU_NAME(name, field, value) { name, field, value },
#include "c_builtin_cpu.inc"
#undef BUSTER_CBUILTIN_CPU_NAME
#undef BUSTER_CBUILTIN_CPU_FEATURE
};

// The test of a name `bytes` of `length` among `names`' `count`: its place in the high 32 bits and its value in the low
// ones, or 0 for a name none has.
static uint64_t check_builtin_cpu_lookup(const CheckBuiltinCpuName* names, uint32_t count, const uint8_t* bytes, uint32_t length)
{
    uint64_t result = 0;
    for (uint32_t i = 0; i < count && result == 0; i += 1)
    {
        const bool same = strlen(names[i].name) == length && memcmp(names[i].name, bytes, length) == 0;
        result = same ? (uint64_t)names[i].place << 32 | names[i].value : 0;
    }
    return result;
}

// Whether __builtin_dump_struct of a record of `record` dumps a bit-field that no format prints (#148): a named one of
// a _BitInt or a 128-bit integer type, among its fields, its anonymous members' and its fields' records'. A frame per
// record being walked, its tag and its next member, so the stack's bound is the records' nesting; past 64 levels the
// walk stops, and the lowering refuses such a dump by name (check_lower_dump_record).
static bool check_builtin_misc_dump_bit_field(const TypeTable* table, TypeId record)
{
    uint32_t tags[64];
    uint32_t next[64];
    uint32_t depth = 0;
    bool found = false;
    tags[depth] = table->types.index[type_index(record)];
    next[depth++] = 0;
    while (!found && depth != 0)
    {
        const uint32_t tag = tags[depth - 1];
        if (next[depth - 1] >= table->tags.member_count[tag])
        {
            depth -= 1;
        }
        else
        {
            const uint32_t member = table->tags.member_begin[tag] + next[depth - 1];
            next[depth - 1] += 1;
            const TypeId bare = type_unqualified(type_strip_aligned(table, table->members.type[member]));
            const uint8_t kind = type_kind(table, bare);
            const bool bits = (table->members.flags[member] & BUSTER_TYPE_MEMBER_BIT_FIELD) != 0;
            found = bits && table->members.name[member] != 0 &&
                    (kind == BUSTER_TYPE_KIND_BIT_INT || bare == BUSTER_TYPE_INT128 || bare == BUSTER_TYPE_UINT128);
            if (!bits && (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION) && depth < 64)
            {
                tags[depth] = table->types.index[type_index(bare)];
                next[depth++] = 0;
            }
        }
    }
    return found;
}

// The last of clang's generic builtins of #148 that its Sema checks itself:
// - __arithmetic_fence(x): x, lvalue-converted, of a floating type, a complex one or a vector of either (its type before
//   conversion: an _Atomic one is refused), a constant where x is; at -O0 a fence keeps nothing apart;
// - __xray_customevent(p, n) and __xray_typedevent(t, p, n): nothing, their arguments converted to const char* and size_t
//   as a call's but never evaluated, as clang's code has them without -fxray-instrument;
// - __warn_memset_zero_len(): nothing;
// - __builtin_trivially_relocate(d, s, n): memmove(d, s, n * sizeof *d), whose value is d, where d is a pointer to a
//   relocatable type that isn't const and s of d's type, both as written (an array is no pointer), and n converted to
//   size_t;
// - __builtin_operator_new and __builtin_operator_delete, C++'s alone, and the pointer authentication builtins, which
//   every call refuses (after the latter's arity, as clang's Sema checks it);
// - __builtin_cpu_init(): libgcc's __cpu_indicator_init's call, which fills its CPU model;
// - __builtin_cpu_supports(s): whether the model has the feature the string literal s names, a bool: the bit of its
//   word, or false for a feature clang doesn't know (which clang warns of); __builtin_cpu_is(s), whether the model's
//   vendor, type or subtype is the one s names, a name clang must know. Neither is a constant expression.
static void check_builtin_misc_last(TypeTable* table, const CheckBuiltinMiscAction* input, CheckBuiltinMiscResult* output, CheckValue* values)
{
    const CBuiltinId id = input->id;
    if (id == CBUILTIN_ARITHMETIC_FENCE)
    {
        const TypeId raw = type_strip_aligned(table, input->arguments[0].type);
        const uint8_t kind = type_kind(table, raw);
        const bool aggregate = kind == BUSTER_TYPE_KIND_COMPLEX || kind == BUSTER_TYPE_KIND_VECTOR || kind == BUSTER_TYPE_KIND_EXT_VECTOR;
        const uint8_t element = aggregate ? check_action_kind(table, type_element(table, raw)) : kind;
        if (element != BUSTER_TYPE_KIND_FLOATING)
        {
            check_action_diagnose(&output->action, CHECK_DIAG_INVALID_TYPE, 0);
        }
        else
        {
            output->action.value = values[0];
            output->action.value.category = CHECK_RVALUE;
            output->action.value.flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_ICE_UNEVALUATED);
        }
    }
    else if (id == CBUILTIN_XRAY_CUSTOMEVENT || id == CBUILTIN_XRAY_TYPEDEVENT)
    {
        const TypeId string = check_builtin_misc_pointer(table, BUSTER_TYPE_CHAR, BUSTER_TYPE_CONST);
        const uint32_t text = id == CBUILTIN_XRAY_TYPEDEVENT ? 1 : 0;
        bool accepted = string != 0;
        for (uint32_t i = 0; accepted && i < input->argument_count; i += 1)
        {
            accepted = check_builtin_misc_convert(table, input, output, values + i, i, i == text ? string : BUSTER_TYPE_ULONG);
        }
        if (string == 0)
        {
            check_action_diagnose(&output->action, CHECK_DIAG_RESOURCE, 0);
        }
        output->action.value = (CheckValue){ .type = BUSTER_TYPE_VOID, .category = CHECK_RVALUE };
        output->evaluated_operands = 0;
    }
    else if (id == CBUILTIN_WARN_MEMSET_ZERO_LEN)
    {
        output->action.value = (CheckValue){ .type = BUSTER_TYPE_VOID, .category = CHECK_RVALUE };
    }
    else if (id == CBUILTIN_PRESERVE_ACCESS_INDEX)
    {
        // BPF's relocation marker (#148): its operand's value, converted, of its type (clang's custom check), never a
        // constant; clang's code generation refuses it without -g, and selects its member accesses for BPF alone.
        // A value of the operand's type alone, whatever constant the operand is (the operands' effects inherited).
        const uint32_t flags = output->action.value.flags & ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_FLOAT_LITERAL |
                                                             BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_NULL_POINTER);
        output->action.value = (CheckValue){ .type = values[0].type, .category = CHECK_RVALUE, .flags = flags };
    }
    else if (id == CBUILTIN_COUNTED_BY_REF)
    {
        // clang's BuiltinCountedByRef (#148): its argument, past parentheses, a pointer or an array without side effects,
        // and a member access, whose member's counted_by count (not sized_by's, nor an _or_null one's) makes the value a
        // pointer to the count's member, of its type as declared; any other such member's, a void *. Its argument never
        // runs: its code is the count's address (or a null pointer).
        const CheckValue* argument = &input->arguments[0];
        const uint8_t kind = type_kind(table, type_strip_aligned(table, argument->type));
        const bool addressable = kind == BUSTER_TYPE_KIND_POINTER || kind == BUSTER_TYPE_KIND_ARRAY || kind == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY ||
                                 kind == BUSTER_TYPE_KIND_VARIABLE_ARRAY;
        const TypeId pointee = input->counted_member == BUSTER_CHECK_COUNTED_COUNTED ? input->counted_type : BUSTER_TYPE_VOID;
        const TypeId pointer = type_pointer(table, pointee);
        if (!addressable || input->counted_member == BUSTER_CHECK_COUNTED_NONE)
        {
            check_action_diagnose(&output->action, CHECK_DIAG_INVALID_TYPE, 0);
        }
        else if ((argument->flags & BUSTER_CHECK_SIDE_EFFECT) != 0)
        {
            check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, 0);
        }
        else if (pointer == 0)
        {
            check_action_diagnose(&output->action, CHECK_DIAG_RESOURCE, 0);
        }
        output->action.value = (CheckValue){ .type = pointer, .category = CHECK_RVALUE };
        // A count's address reads the access's base, which this references; a null pointer reads nothing.
        output->evaluated_operands = input->counted_member == BUSTER_CHECK_COUNTED_COUNTED ? 1 : 0;
    }
    else if (id == CBUILTIN_AVAILABLE)
    {
        // clang's ObjCAvailabilityCheckExpr (#148): a _Bool, of no constant expression, which its code makes 1 for this
        // target (c_check_availability_input.c).
        output->action.value = (CheckValue){ .type = BUSTER_TYPE_BOOL, .category = CHECK_RVALUE };
    }
    else if (id == CBUILTIN_DUMP_STRUCT)
    {
        // clang's BuiltinDumpStruct (#148): a pointer, lvalue-converted, to a complete struct or union ("expected pointer
        // to struct as 1st argument"), and a function or a function pointer ("expected a callable expression as 2nd
        // argument"). clang then calls that function once per line, with the rest of the arguments, a format and its
        // operands: (rest..., format) for a brace's line and up to five operands more for a field's. A prototype must
        // take each of those calls: it is variadic, with no more parameters than the rest and the format, the rest
        // converting to its first parameters and the format, a char *, to the next ("too many" or "too few arguments"
        // otherwise). The calls' code is check_lower_dump_struct's, of check_lower_dump_prepare's plan.
        CheckValue record = check_builtin_misc_argument(table, input, output, 0);
        const TypeId pointer = type_strip_aligned(table, record.type);
        const bool is_pointer = output->action.diagnostic == CHECK_DIAG_NONE && check_action_kind(table, pointer) == BUSTER_TYPE_KIND_POINTER;
        const TypeId pointee = is_pointer ? type_unqualified(type_strip_aligned(table, type_element(table, pointer))) : 0;
        const uint8_t pointee_kind = pointee != 0 ? check_action_kind(table, pointee) : 0;
        const TypeId callable = type_strip_aligned(table, input->arguments[1].type);
        const uint8_t callable_kind = type_is_valid(table, callable) ? check_action_kind(table, callable) : 0;
        const TypeId function = callable_kind == BUSTER_TYPE_KIND_POINTER ? type_strip_aligned(table, type_element(table, callable)) : callable;
        const bool is_function = type_is_valid(table, function) && check_action_kind(table, function) == BUSTER_TYPE_KIND_FUNCTION;
        if (output->action.diagnostic == CHECK_DIAG_NONE && pointee_kind != BUSTER_TYPE_KIND_STRUCT && pointee_kind != BUSTER_TYPE_KIND_UNION)
        {
            check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, 0);
        }
        else if (output->action.diagnostic == CHECK_DIAG_NONE && !check_builtin_misc_source_complete(table, pointee, input))
        {
            check_action_diagnose(&output->action, CHECK_DIAG_INCOMPLETE, 0);
        }
        else if (output->action.diagnostic == CHECK_DIAG_NONE && !is_function)
        {
            check_action_diagnose(&output->action, CHECK_DIAG_CALL, 1);
        }
        const uint32_t flags = is_function ? table->types.flags[type_index(function)] : 0;
        const bool prototype = (flags & BUSTER_TYPE_FLAG_PROTOTYPE) != 0;
        uint32_t count = 0;
        const TypeId* parameters = is_function && prototype ? type_parameters(table, function, &count) : NULL;
        const uint32_t rest = input->argument_count - 2;
        if (output->action.diagnostic == CHECK_DIAG_NONE && prototype && ((flags & BUSTER_TYPE_FLAG_VARIADIC) == 0 || count > rest + 1))
        {
            check_action_diagnose(&output->action, CHECK_DIAG_ARGUMENT_COUNT, 1);
        }
        for (uint32_t i = 0; output->action.diagnostic == CHECK_DIAG_NONE && parameters != NULL && i < count; i += 1)
        {
            // The rest convert as the call's arguments do; the format's literal is a char * (its array decayed).
            CheckValue argument = i < rest ? input->arguments[2 + i] : (CheckValue){ .type = type_pointer(table, BUSTER_TYPE_CHAR), .category = CHECK_RVALUE };
            if (i < rest)
            {
                const CheckAction action = { .operands = { argument }, .node = input->node, .context = input->context };
                CheckActionResult converted = { 0 };
                argument = check_action_operand(table, &action, &converted, 0);
                if (converted.diagnostic != CHECK_DIAG_NONE)
                {
                    check_action_diagnose(&output->action, converted.diagnostic, 2 + i);
                }
            }
            // Into a scratch result: the calls' conversions take no slot of the four the call's own arguments have. The
            // rest convert as a call's arguments to its parameters (check_action_call, #1842): a complex value, an
            // _Atomic parameter's operand type, and a transparent union's member, whose code the calls make
            // (check_lower_dump_struct); the format as the other builtins' arguments.
            CheckBuiltinMiscResult scratch = { 0 };
            const CheckAction context = { .node = input->node, .context = input->context };
            if (i < rest)
            {
                const TypeId destination = check_action_kind(table, argument.type) != BUSTER_TYPE_KIND_ATOMIC ? type_conversion_operand(table, parameters[i]) :
                                                                                                                 parameters[i];
                CheckActionResult converted = { 0 };
                const bool assignable = check_action_assignable(table, destination, &argument, &context, &converted) ||
                                        (converted.diagnostic == CHECK_DIAG_NONE && check_action_transparent_parameter(table, destination, &argument, &context, &converted) != 0);
                if (converted.diagnostic != CHECK_DIAG_NONE || !assignable)
                {
                    check_action_diagnose(&scratch.action, converted.diagnostic != CHECK_DIAG_NONE ? converted.diagnostic : CHECK_DIAG_INCOMPATIBLE, 0);
                }
            }
            else
            {
                check_builtin_misc_convert(table, input, &scratch, &argument, 0, parameters[i]);
            }
            if (scratch.action.diagnostic != CHECK_DIAG_NONE)
            {
                check_action_diagnose(&output->action, scratch.action.diagnostic, i < rest ? 2 + i : 1);
            }
        }
        if (output->action.diagnostic == CHECK_DIAG_NONE && check_builtin_misc_dump_bit_field(table, pointee))
        {
            // A bit-field no format prints (a _BitInt's or an __int128's) prints its address, which clang's Sema refuses:
            // "address of bit-field requested".
            check_action_diagnose(&output->action, CHECK_DIAG_BIT_FIELD, 0);
        }
        if (output->action.diagnostic == CHECK_DIAG_NONE)
        {
            output->action.value = (CheckValue){ .type = BUSTER_TYPE_VOID, .category = CHECK_RVALUE, .flags = BUSTER_CHECK_SIDE_EFFECT };
            output->reads_memory = true;
            output->writes_memory = true;
        }
    }
    else if (id == CBUILTIN_OS_LOG_FORMAT_BUFFER_SIZE || id == CBUILTIN_OS_LOG_FORMAT)
    {
        // clang's BuiltinOSLogFormat (#148): __builtin_os_log_format's buffer converts to void * as a prototype's
        // parameter; the format is an ordinary or UTF-8 string literal past its parentheses and casts (os_log's "format
        // argument is not a string constant"); each data argument, promoted, is less than 256 bytes ("argument N is too
        // big"). __builtin_os_log_format's format checks then refuse a mask of other than 1 to 8 bytes and a struct or a
        // complex argument a specifier takes. The buffer's size is its layout's: a summary and a count byte, then each
        // item's descriptor and size bytes and its data, a size_t integer constant expression whose data never runs; the
        // format's value is the buffer, its stores check_lower_os_log's.
        const bool format = id == CBUILTIN_OS_LOG_FORMAT;
        const uint32_t data = format ? 2 : 1;
        CheckValue buffer = { 0 };
        if (format)
        {
            buffer = check_builtin_misc_argument(table, input, output, 0);
            check_builtin_misc_convert(table, input, output, &buffer, 0, type_pointer(table, BUSTER_TYPE_VOID));
        }
        if (output->action.diagnostic == CHECK_DIAG_NONE && !input->os_log_known)
        {
            check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, data - 1);
        }
        for (uint32_t i = data; i < input->argument_count && output->action.diagnostic == CHECK_DIAG_NONE; i += 1)
        {
            const TypeId type = input->arguments[i].type;
            if (!type_is_valid(table, type) || input->arguments[i].category > CHECK_FUNCTION || check_action_kind(table, type) == BUSTER_TYPE_KIND_VOID)
            {
                check_action_diagnose(&output->action, CHECK_DIAG_INVALID_TYPE, i);
            }
            else if (check_builtin_misc_os_log_size(table, type) >= 256)
            {
                check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, i);
            }
        }
        if (format && output->action.diagnostic == CHECK_DIAG_NONE && input->os_log_invalid_mask)
        {
            check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, 1);
        }
        for (uint32_t i = data; format && i < input->argument_count && output->action.diagnostic == CHECK_DIAG_NONE; i += 1)
        {
            const uint32_t argument = i - data;
            const uint8_t kind = check_action_kind(table, input->arguments[i].type);
            const bool taken = argument < 256 && (input->os_log_taken[argument / 64] >> (argument % 64) & 1) != 0;
            if (taken && (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION || kind == BUSTER_TYPE_KIND_COMPLEX))
            {
                check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, i);
            }
        }
        if (output->action.diagnostic == CHECK_DIAG_NONE && !format)
        {
            uint64_t size = 2;
            for (uint32_t i = 0; i < input->os_log_count; i += 1)
            {
                size += 2 + check_builtin_misc_os_log_item_size(table, input, input->os_log_items + i, data);
            }
            output->evaluated_operands = 0;
            output->action.value = (CheckValue){ .type = BUSTER_TYPE_ULONG, .category = CHECK_RVALUE, .flags = BUSTER_CHECK_ICE,
                                                 .constant_kind = CHECK_CONSTANT_INTEGER, .integer = size };
            output->constant_value = output->action.value;
            output->constant_evaluated_operands = 0;
        }
        else if (output->action.diagnostic == CHECK_DIAG_NONE)
        {
            output->action.value = (CheckValue){ .type = type_pointer(table, BUSTER_TYPE_VOID), .category = CHECK_RVALUE, .flags = BUSTER_CHECK_SIDE_EFFECT };
            output->writes_memory = true;
        }
    }
    else if (id == CBUILTIN_CALL_WITH_STATIC_CHAIN)
    {
        // The call's value (#148), its first argument a call as written of no builtin (CheckBuiltinMiscAction.static_chain_call)
        // and its second a pointer, as clang's BuiltinCallWithStaticChain checks them. The chain is passed in r10, as clang's
        // code passes it (check_lower_call_body).
        const TypeId chain = type_strip_aligned(table, input->arguments[1].type);
        const uint8_t kind = type_is_valid(table, chain) ? check_action_kind(table, chain) : BUSTER_TYPE_KIND_VOID;
        if (!input->static_chain_call)
        {
            check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, 0);
        }
        else if (kind != BUSTER_TYPE_KIND_POINTER && !type_kind_is_array(kind) && kind != BUSTER_TYPE_KIND_FUNCTION)
        {
            check_action_diagnose(&output->action, CHECK_DIAG_POINTER_REQUIRED, 1);
        }
        else
        {
            output->action.value = input->arguments[0];
            output->action.value.category = CHECK_RVALUE;
            output->action.value.constant_kind = CHECK_CONSTANT_NONE;
            output->action.value.flags = (input->arguments[0].flags | input->arguments[1].flags) & BUSTER_CHECK_SIDE_EFFECT;
            output->action.value.flags |= BUSTER_CHECK_SIDE_EFFECT;
        }
    }
    else if (id == CBUILTIN_INFER_ALLOC_TOKEN)
    {
        // A size_t of arguments that never run (#148): its type's token (CheckBuiltinMiscAction.alloc_token), an integer
        // constant expression, where they give one; else no constant, as clang's evaluator gives none, and 0 in code, the
        // AllocToken pass's fallback token.
        output->evaluated_operands = 0;
        output->action.value = (CheckValue){ .type = BUSTER_TYPE_ULONG, .category = CHECK_RVALUE };
        // Its arguments are any expressions but void ones ("argument may not have 'void' type").
        for (uint32_t i = 0; i < input->argument_count && output->action.diagnostic == CHECK_DIAG_NONE; i += 1)
        {
            if (!type_is_valid(table, input->arguments[i].type) || input->arguments[i].category > CHECK_FUNCTION ||
                check_action_kind(table, input->arguments[i].type) == BUSTER_TYPE_KIND_VOID)
            {
                check_action_diagnose(&output->action, CHECK_DIAG_INVALID_TYPE, i);
            }
        }
        if (input->alloc_token_known && output->action.diagnostic == CHECK_DIAG_NONE)
        {
            output->action.value.flags = BUSTER_CHECK_ICE;
            output->action.value.constant_kind = CHECK_CONSTANT_INTEGER;
            output->action.value.integer = input->alloc_token;
            output->constant_value = output->action.value;
            output->constant_evaluated_operands = 0;
        }
    }
    else if (id == CBUILTIN_EH_RETURN)
    {
        // void(size_t, void*), which doesn't return (#148): clang's code generation unwinds to the handler, which this
        // compiler's doesn't (check_lower_unsupported).
        check_builtin_misc_convert(table, input, output, values + 0, 0, BUSTER_TYPE_ULONG);
        check_builtin_misc_convert(table, input, output, values + 1, 1, type_pointer(table, BUSTER_TYPE_VOID));
        output->action.value.type = BUSTER_TYPE_VOID;
        output->action.value.category = CHECK_RVALUE;
        output->action.value.flags |= BUSTER_CHECK_SIDE_EFFECT;
        output->terminates = true;
    }
    else if (id == CBUILTIN_CFSTRING_MAKE_CONSTANT_STRING || id == CBUILTIN_NSSTRING_MAKE_CONSTANT_STRING)
    {
        // A constant CFString (#148), a const __NSConstantString *: CFString's of an ordinary string literal (clang's
        // CheckObjCString: "CFString literal is not a string constant" otherwise), NSString's of any const char * in C,
        // which clang checks in Objective-C alone. Of a literal, clang's evaluator takes either call as an address
        // constant (IsOpaqueConstantCall), whose code is its object's address and which no operand's evaluation runs:
        // here its call's object (CheckBuiltinMiscAction.cfstring_object), a static one; NSString's of what is no literal
        // too, whose object clang's code makes from the literal its argument isn't, which the static data refuses.
        // Its argument converts to const char * as a prototype's, and past its parentheses and casts is an ordinary
        // literal, never a UTF-8 one (u8"x", a char array before C23).
        check_builtin_misc_convert(table, input, output, values + 0, 0, type_pointer(table, type_qualify(table, BUSTER_TYPE_CHAR, BUSTER_TYPE_CONST)));
        const TypeId record = input->ns_constant_string != 0 ? type_qualify(table, input->ns_constant_string, BUSTER_TYPE_CONST) : 0;
        const TypeId result = record != 0 ? type_pointer(table, record) : 0;
        const bool ordinary = id == CBUILTIN_NSSTRING_MAKE_CONSTANT_STRING || (input->string_known && !input->string_utf8);
        // NSString's of what is no literal is an address constant too, as clang's evaluator takes any call of either
        // (IsOpaqueConstantCall), whatever its argument: its object the static data refuses by name (#1785,
        // check_data_cfstring_keys), clang's code making it from the literal its argument isn't.
        const bool literal = (input->string_known || id == CBUILTIN_NSSTRING_MAKE_CONSTANT_STRING) && input->cfstring_object != 0;
        if (output->action.diagnostic == CHECK_DIAG_NONE && (!ordinary || result == 0))
        {
            check_action_diagnose(&output->action, result == 0 && ordinary ? CHECK_DIAG_RESOURCE : CHECK_DIAG_INCOMPATIBLE, 0);
        }
        else if (output->action.diagnostic == CHECK_DIAG_NONE && literal)
        {
            output->action.value = (CheckValue){
                .type = result,
                .category = CHECK_RVALUE,
                .flags = BUSTER_CHECK_STATIC_OBJECT,
                .constant_kind = CHECK_CONSTANT_ADDRESS,
                .object = input->cfstring_object,
            };
            output->constant_value = output->action.value;
            output->constant_evaluated_operands = 0;
        }
        else if (output->action.diagnostic == CHECK_DIAG_NONE)
        {
            output->action.value = (CheckValue){ .type = result, .category = CHECK_RVALUE, .flags = output->action.value.flags };
        }
    }
    else if (id == CBUILTIN_CLEAR_PADDING)
    {
        // clang's void(void*) of custom checks (#148): a pointer as written (an array isn't one), to a complete object type
        // that isn't const ("read-only variable is not assignable"), whose padding bits its code zeroes
        // (c_check_program_padding.c). An incomplete pointee (void's among them) is clang's "variable has incomplete type".
        const TypeId written = type_strip_aligned(table, input->arguments[0].type);
        const TypeId pointee = check_action_kind(table, written) == BUSTER_TYPE_KIND_POINTER ? type_element(table, written) : 0;
        const uint8_t kind = pointee != 0 ? check_action_kind(table, pointee) : 0;
        if (pointee == 0 || kind == BUSTER_TYPE_KIND_FUNCTION)
        {
            check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, 0);
        }
        else if (kind == BUSTER_TYPE_KIND_VOID || !check_builtin_misc_source_complete(table, pointee, input))
        {
            check_action_diagnose(&output->action, CHECK_DIAG_INCOMPLETE, 0);
        }
        else if ((type_qualifiers(pointee) & BUSTER_TYPE_CONST) != 0)
        {
            check_action_diagnose(&output->action, CHECK_DIAG_INVALID_TYPE, 0);
        }
        output->action.value.type = BUSTER_TYPE_VOID;
        output->action.value.category = CHECK_RVALUE;
        output->action.value.flags |= BUSTER_CHECK_SIDE_EFFECT;
    }
    else if (id == CBUILTIN_HLSL_RESOURCE_GETPOINTER_TYPED)
    {
        // HLSL's, which C sees as void(...) (#148): any arguments but void ones, as a variadic call's ("argument type 'void'
        // is incomplete"), and no value; clang's code generation refuses every call.
        for (uint32_t i = 0; i < input->argument_count && output->action.diagnostic == CHECK_DIAG_NONE; i += 1)
        {
            const CheckValue argument = input->arguments[i];
            const bool usable = type_is_valid(table, argument.type) && argument.category <= CHECK_FUNCTION;
            if (!usable || check_action_kind(table, argument.type) == BUSTER_TYPE_KIND_VOID)
            {
                check_action_diagnose(&output->action, usable ? CHECK_DIAG_INCOMPLETE : CHECK_DIAG_INVALID_TYPE, i);
            }
            check_action_inherit(&output->action.value, &argument);
        }
        output->action.value.type = BUSTER_TYPE_VOID;
        output->action.value.category = CHECK_RVALUE;
        output->action.value.flags |= BUSTER_CHECK_SIDE_EFFECT;
    }
    else if (id == CBUILTIN_CPU_INIT)
    {
        output->action.value = (CheckValue){ .type = BUSTER_TYPE_VOID, .category = CHECK_RVALUE, .flags = BUSTER_CHECK_SIDE_EFFECT };
        output->writes_memory = true;
    }
    else if (id == CBUILTIN_CPU_SUPPORTS || id == CBUILTIN_CPU_IS)
    {
        const bool supports = id == CBUILTIN_CPU_SUPPORTS;
        output->cpu_test = !input->string_known ? 0 : supports ? check_builtin_cpu_lookup(check_builtin_cpu_features, BUSTER_ARRAY_LENGTH(check_builtin_cpu_features),
                                                                                       input->string_bytes, input->string_length) :
                           check_builtin_cpu_lookup(check_builtin_cpu_names, BUSTER_ARRAY_LENGTH(check_builtin_cpu_names), input->string_bytes, input->string_length);
        if (!input->string_known || input->string_cast || (!supports && output->cpu_test == 0))
        {
            check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, 0);
        }
        else
        {
            output->action.value = (CheckValue){ .type = BUSTER_TYPE_BOOL, .category = CHECK_RVALUE };
            output->evaluated_operands = 0;
            output->reads_memory = true;
        }
    }
    else if (id == CBUILTIN_TRIVIALLY_RELOCATE)
    {
        const TypeId raw = type_strip_aligned(table, input->arguments[0].type);
        const TypeId element = type_kind(table, raw) == BUSTER_TYPE_KIND_POINTER ? type_element(table, raw) : 0;
        // clang compares the two arguments' types as they are written, before any conversion (an array isn't a pointer).
        const TypeId source = type_strip_aligned(table, input->arguments[1].type);
        if (element == 0 || (type_qualifiers(type_strip_aligned(table, element)) & BUSTER_TYPE_CONST) != 0 || !check_builtin_misc_relocatable(table, element))
        {
            check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, 0);
        }
        else if (!check_builtin_misc_source_complete(table, element, input))
        {
            check_action_diagnose(&output->action, CHECK_DIAG_INCOMPLETE, 0);
        }
        else if (raw != source)
        {
            check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, 1);
        }
        else if (check_builtin_misc_convert(table, input, output, values + 2, 2, BUSTER_TYPE_ULONG))
        {
            output->action.value = values[0];
            output->action.value.category = CHECK_RVALUE;
            output->action.value.constant_kind = CHECK_CONSTANT_NONE;
            output->action.value.flags |= BUSTER_CHECK_SIDE_EFFECT;
            output->action.value.flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_NULL_POINTER);
            output->reads_memory = true;
            output->writes_memory = true;
        }
    }
    else
    {
        check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, 0);
    }
}

#include "c_check_builtin_bits.c"

static __attribute__((unused)) void check_builtin_misc_one(TypeTable* table, const CheckBuiltinMiscAction* input, CheckBuiltinMiscResult* output)
{
    *output = (CheckBuiltinMiscResult){
        .prefetch_locality = 3,
    };
    uint32_t minimum = 0;
    uint32_t maximum = 0;
    switch (input->id)
    {
        case CBUILTIN_EXPECT: minimum = maximum = 2; break;
        case CBUILTIN_EXPECT_WITH_PROBABILITY: minimum = maximum = 3; break;
        case CBUILTIN_UNREACHABLE:
        case CBUILTIN_TRAP: minimum = maximum = 0; break;
        case CBUILTIN_ASSUME_ALIGNED: minimum = 2; maximum = 3; break;
        case CBUILTIN_PREFETCH: minimum = 1; maximum = 3; break;
        case CBUILTIN_MEMCPY:
        case CBUILTIN_MEMMOVE:
        case CBUILTIN_MEMSET:
        case CBUILTIN_MEMCMP: minimum = maximum = 3; break;
        case CBUILTIN_STRLEN:
        case CBUILTIN_ALLOCA:
        case CBUILTIN_FRAME_ADDRESS:
        case CBUILTIN_RETURN_ADDRESS:
        case CBUILTIN_EXTRACT_RETURN_ADDR:
        case CBUILTIN_FROB_RETURN_ADDR:
        case CBUILTIN_CLASSIFY_TYPE:
        case CBUILTIN_EH_RETURN_DATA_REGNO:
        case CBUILTIN_LAUNDER:
        case CBUILTIN_ALLOCA_UNINITIALIZED:
        case CBUILTIN_ALLOW_RUNTIME_CHECK:
        case CBUILTIN_ALLOW_SANITIZE_CHECK:
        case CBUILTIN_ADDRESSOF:
        case CBUILTIN_FUNCTION_START: minimum = maximum = 1; break;
        case CBUILTIN_ALLOCA_WITH_ALIGN:
        case CBUILTIN_ALLOCA_WITH_ALIGN_UNINITIALIZED:
        case CBUILTIN_ANNOTATION:
        case CBUILTIN_COMPLEX:
        case CBUILTIN_XRAY_CUSTOMEVENT: minimum = maximum = 2; break;
        case CBUILTIN_XRAY_TYPEDEVENT:
        case CBUILTIN_TRIVIALLY_RELOCATE: minimum = maximum = 3; break;
        case CBUILTIN_ARITHMETIC_FENCE: minimum = maximum = 1; break;
        case CBUILTIN_DWARF_CFA:
        case CBUILTIN_WARN_MEMSET_ZERO_LEN:
        case CBUILTIN_CPU_INIT: minimum = maximum = 0; break;
        case CBUILTIN_CPU_IS:
        case CBUILTIN_CPU_SUPPORTS:
        case CBUILTIN_PRESERVE_ACCESS_INDEX:
        case CBUILTIN_CLEAR_PADDING:
        case CBUILTIN_CFSTRING_MAKE_CONSTANT_STRING:
        case CBUILTIN_NSSTRING_MAKE_CONSTANT_STRING: minimum = maximum = 1; break;
        case CBUILTIN_EH_RETURN: minimum = maximum = 2; break;
        // void(...), which clang checks as a prototype's variadic arguments outside HLSL: as many as the operands' mask holds.
        case CBUILTIN_HLSL_RESOURCE_GETPOINTER_TYPED: minimum = 0; maximum = 31; break;
        // size_t(...), at least one argument.
        case CBUILTIN_INFER_ALLOC_TOKEN: minimum = 1; maximum = 31; break;
        case CBUILTIN_CALL_WITH_STATIC_CHAIN: minimum = maximum = 2; break;
        // A format, past __builtin_os_log_format's buffer, and up to 255 data arguments, as clang's BuiltinOSLogFormat
        // takes them (its required arguments plus 0xff); data past the operands' mask is evaluated (evaluated_operands).
        case CBUILTIN_OS_LOG_FORMAT_BUFFER_SIZE: minimum = 1; maximum = 1 + 255; break;
        case CBUILTIN_OS_LOG_FORMAT: minimum = 2; maximum = 2 + 255; break;
        // A record's pointer, a printing function, and that function's leading arguments.
        case CBUILTIN_DUMP_STRUCT: minimum = 2; maximum = 31; break;
        case CBUILTIN_COUNTED_BY_REF: minimum = maximum = 1; break;
        // Its specs are no operands (c_check_availability_input.c).
        case CBUILTIN_AVAILABLE: minimum = maximum = 0; break;
        case CBUILTIN_OPERATOR_NEW:
        case CBUILTIN_OPERATOR_DELETE:
            // C++'s alone, refused with any arguments, before their count can reach the operands' 32-bit mask.
            check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, 0);
            break;
        default:
            if (cbuiltin_bits(input->id))
            {
                cbuiltin_bits_arity(input->id, &minimum, &maximum);
            }
            else if (cbuiltin_align(input->id))
            {
                minimum = maximum = 2;
            }
            else if (input->id >= CBUILTIN_PTRAUTH_AUTH && input->id <= CBUILTIN_PTRAUTH_STRIP)
            {
                minimum = maximum = check_builtin_misc_ptrauth_arity(input->id);
            }
            else
            {
                check_action_diagnose(&output->action, CHECK_DIAG_UNSUPPORTED, 0);
            }
            break;
    }
    if (output->action.diagnostic == CHECK_DIAG_NONE && (input->argument_count < minimum || input->argument_count > maximum))
    {
        // prefetch's prototype is variadic: its too few is clang's call's "expected at least".
        const bool variadic = input->id == CBUILTIN_PREFETCH && input->argument_count < minimum;
        check_action_diagnose_count(&output->action, input->argument_count, minimum, variadic ? UINT32_MAX : maximum);
    }
    if (output->action.diagnostic == CHECK_DIAG_NONE && input->argument_count != 0 && input->arguments == NULL)
    {
        check_action_diagnose(&output->action, CHECK_DIAG_INVALID_TYPE, 0);
    }
    if (output->action.diagnostic == CHECK_DIAG_NONE)
    {
        CheckValue values[4] = { 0 };
        output->runtime_builtin = input->id;
        // Every argument; a full mask holds those past its 32 bits too (os_log's data).
        output->evaluated_operands = input->argument_count >= 32 ? UINT32_MAX : (UINT32_C(1) << input->argument_count) - 1;
        // __builtin_classify_type's operand is its type alone, of any expression, which no conversion reaches, and
        // addressof's and function_start's are their designators. allow_runtime_check's and allow_sanitize_check's string
        // and annotation's second argument are their literals (CheckBuiltinMiscAction.string_known), never converted.
        // Every call of a builtin x86 refuses is refused before its arguments are read: pointer authentication's after
        // their arity, here, and operator_new's and operator_delete's, of any arity, before the mask above. trivially_relocate's
        // size and the xray events' arguments, lvalue-converted here, convert to their parameters (check_builtin_misc_last).
        const bool refused = input->id >= CBUILTIN_OPERATOR_NEW && input->id <= CBUILTIN_PTRAUTH_STRIP;
        const bool classify = input->id == CBUILTIN_CLASSIFY_TYPE || input->id == CBUILTIN_ADDRESSOF || input->id == CBUILTIN_FUNCTION_START ||
                              input->id == CBUILTIN_ALLOW_RUNTIME_CHECK || input->id == CBUILTIN_ALLOW_SANITIZE_CHECK || input->id == CBUILTIN_CPU_IS ||
                              input->id == CBUILTIN_CPU_SUPPORTS || input->id == CBUILTIN_INFER_ALLOC_TOKEN || input->id == CBUILTIN_CALL_WITH_STATIC_CHAIN ||
                              input->id == CBUILTIN_OS_LOG_FORMAT_BUFFER_SIZE || input->id == CBUILTIN_OS_LOG_FORMAT || input->id == CBUILTIN_DUMP_STRUCT ||
                              input->id == CBUILTIN_COUNTED_BY_REF || refused;
        if (classify && !refused && (!type_is_valid(table, input->arguments[0].type) || input->arguments[0].category > CHECK_FUNCTION))
        {
            check_action_diagnose(&output->action, CHECK_DIAG_INVALID_TYPE, 0);
        }
        // hlsl_resource_getpointer_typed's arguments are a variadic call's, each of a complete type (check_builtin_misc_last).
        const uint32_t converted = input->id == CBUILTIN_ANNOTATION ? 1 : input->id == CBUILTIN_HLSL_RESOURCE_GETPOINTER_TYPED ? 0 : input->argument_count;
        for (uint32_t i = 0; !classify && i < converted && output->action.diagnostic == CHECK_DIAG_NONE; i += 1)
        {
            values[i] = check_builtin_misc_argument(table, input, output, i);
            check_action_inherit(&output->action.value, values + i);
        }
        if (output->action.diagnostic == CHECK_DIAG_NONE)
        {
            switch (input->id)
            {
                case CBUILTIN_EXPECT:
                case CBUILTIN_EXPECT_WITH_PROBABILITY: check_builtin_misc_expect(table, input, output, values); break;
                case CBUILTIN_UNREACHABLE:
                case CBUILTIN_TRAP:
                    output->action.value.type = BUSTER_TYPE_VOID;
                    output->action.value.flags |= BUSTER_CHECK_SIDE_EFFECT;
                    output->terminates = true;
                    break;
                case CBUILTIN_ASSUME_ALIGNED: check_builtin_misc_assume_aligned(table, input, output, values); break;
                case CBUILTIN_PREFETCH: check_builtin_misc_prefetch(table, input, output, values); break;
                case CBUILTIN_MEMCPY:
                case CBUILTIN_MEMMOVE:
                case CBUILTIN_MEMSET:
                case CBUILTIN_MEMCMP:
                case CBUILTIN_STRLEN:
                case CBUILTIN_ALLOCA: check_builtin_misc_memory(table, input, output, values); break;
                case CBUILTIN_FRAME_ADDRESS:
                case CBUILTIN_RETURN_ADDRESS:
                case CBUILTIN_EXTRACT_RETURN_ADDR:
                case CBUILTIN_FROB_RETURN_ADDR: check_builtin_misc_frame(table, input, output, values); break;
                case CBUILTIN_CLASSIFY_TYPE:
                case CBUILTIN_EH_RETURN_DATA_REGNO:
                case CBUILTIN_DWARF_CFA:
                case CBUILTIN_LAUNDER: check_builtin_misc_generic(table, input, output, values); break;
                case CBUILTIN_ALLOCA_UNINITIALIZED:
                case CBUILTIN_ALLOCA_WITH_ALIGN:
                case CBUILTIN_ALLOCA_WITH_ALIGN_UNINITIALIZED:
                case CBUILTIN_ALLOW_RUNTIME_CHECK:
                case CBUILTIN_ALLOW_SANITIZE_CHECK:
                case CBUILTIN_ANNOTATION:
                case CBUILTIN_ADDRESSOF:
                case CBUILTIN_FUNCTION_START: check_builtin_misc_more(table, input, output, values); break;
                case CBUILTIN_COMPLEX: check_builtin_misc_complex(table, input, output, values); break;
                default:
                    if (cbuiltin_misc_last(input->id))
                    {
                        check_builtin_misc_last(table, input, output, values);
                    }
                    if (cbuiltin_bits(input->id))
                    {
                        check_builtin_bits(table, input, output, values);
                    }
                    else if (cbuiltin_align(input->id))
                    {
                        check_builtin_misc_align(table, input, output, values);
                    }
                    break;
            }
        }
    }
    for (uint32_t i = 0; i < 3; i += 1)
    {
        output->action.conversions[i] = output->argument_conversions[i];
    }
    if (output->action.diagnostic != CHECK_DIAG_NONE)
    {
        output->action.value.constant_kind = CHECK_CONSTANT_NONE;
        output->action.value.flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_ICE_UNEVALUATED);
        output->constant_value = (CheckValue){ 0 };
        output->constant_evaluated_operands = 0;
        output->evaluated_operands = 0;
        output->runtime_builtin = CBUILTIN_NONE;
    }
}

static __attribute__((unused)) void check_builtin_misc(TypeTable* table, const CheckBuiltinMiscAction* actions, CheckBuiltinMiscResult* results, uint32_t count)
{
    for (uint32_t i = 0; i < count; i += 1)
    {
        check_builtin_misc_one(table, actions + i, results + i);
    }
}

#endif
