// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #148 miscellaneous builtin semantic actions. Include after c_builtin_catalog.c and c_check_actions.c.
// Arguments are already elaborated source values. The caller retains their graph identities and evaluation order;
// evaluated_operands is a bit mask, never permission to evaluate an operand twice. No action reads source memory.
// Tag completions are immutable source positions, separate from diagnostic node identities. A null completion
// slice uses the current table for direct action callers. Batches run in order because decay types may be interned.
// Input/output slices require ordinary C alignment, no padding, and must be disjoint from each other/type storage.
// Zero count permits null slices. These actions establish no catalog SOURCE or selector acceptance by themselves.
#ifndef BUSTER_C_CHECK_BUILTIN_MISC_INCLUDED
#define BUSTER_C_CHECK_BUILTIN_MISC_INCLUDED

#define BUSTER_CHECK_BUILTIN_MISC_WARN_QUALIFIERS 0x01u
#define BUSTER_CHECK_BUILTIN_MISC_WARN_POINTER_SIGN 0x02u
#define BUSTER_CHECK_BUILTIN_MISC_WARN_FUNCTION_POINTER 0x04u
#define BUSTER_CHECK_BUILTIN_MISC_WARN_ALIGNMENT 0x08u
#define BUSTER_CHECK_BUILTIN_MISC_WARN_FRAME_LEVEL 0x10u

#define BUSTER_CHECK_BUILTIN_MISC_UNSUPPORTED_FRAME_LEVEL 0x01u
#define BUSTER_CHECK_BUILTIN_MISC_UNSUPPORTED_ALIGNMENT 0x02u
#define BUSTER_CHECK_BUILTIN_MISC_UNSUPPORTED_COMPLEX 0x04u

typedef enum CheckBuiltinMiscConstraint
{
    CHECK_BUILTIN_MISC_CONSTRAINT_NONE,
    CHECK_BUILTIN_MISC_CONSTRAINT_CONSTANT,
    CHECK_BUILTIN_MISC_CONSTRAINT_RANGE,
    CHECK_BUILTIN_MISC_CONSTRAINT_ALIGNMENT,
} CheckBuiltinMiscConstraint;

typedef struct CheckBuiltinMiscAction CheckBuiltinMiscAction;
struct CheckBuiltinMiscAction
{
    CBuiltinId id;
    const CheckValue* arguments;
    uint32_t argument_count;
    uint32_t node;
    const uint32_t* tag_completions;
    uint32_t tag_completion_count;
    uint32_t source_position;
    uint32_t extensions;
};

typedef struct CheckBuiltinMiscResult CheckBuiltinMiscResult;
struct CheckBuiltinMiscResult
{
    CheckActionResult action;
    // A separate source constant-evaluation result. A nonzero mask names the only arguments that evaluator
    // visits; ordinary runtime evaluation still uses evaluated_operands and action.value, including all effects.
    CheckValue constant_value;
    uint32_t constant_evaluated_operands;
    CheckConversion argument_conversions[3];
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
};

static bool check_builtin_misc_source_complete(const TypeTable* table, TypeId type, const CheckBuiltinMiscAction* action)
{
    bool complete = type_is_complete(table, type);
    const TypeId base = type_strip_aligned(table, type);
    const uint8_t kind = type_kind(table, base);
    if (complete && action->tag_completions != NULL &&
        (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION || kind == BUSTER_TYPE_KIND_ENUM))
    {
        const uint32_t tag = table->types.index[type_index(base)];
        complete = tag < action->tag_completion_count && action->tag_completions[tag] <= action->source_position;
    }
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
    else if (accepted && !check_action_assignable(table, destination, value, &output->action))
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

static void check_builtin_misc_expect(TypeTable* table, const CheckBuiltinMiscAction* input, CheckBuiltinMiscResult* output, CheckValue* values)
{
    const bool first = check_builtin_misc_convert(table, input, output, values, 0, BUSTER_TYPE_LONG);
    const bool second = check_builtin_misc_convert(table, input, output, values + 1, 1, BUSTER_TYPE_LONG);
    if (first && second)
    {
        // Clang's source constant evaluator visits only the converted first argument. Type checking and both
        // conversions still apply. Keep this result separate from the runtime action, which evaluates both.
        output->constant_evaluated_operands = 1;
        output->constant_value = values[0];
        output->constant_value.category = CHECK_RVALUE;
        output->constant_value.flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_NULL_POINTER);
        if (output->constant_value.fault != CHECK_FAULT_NONE)
        {
            output->constant_value.constant_kind = CHECK_CONSTANT_NONE;
        }
        else if (output->constant_value.constant_kind == CHECK_CONSTANT_INTEGER && (output->constant_value.flags & BUSTER_CHECK_SIDE_EFFECT) == 0)
        {
            output->constant_value.flags |= BUSTER_CHECK_ICE;
        }
        output->action.value = values[0];
        output->action.value.category = CHECK_RVALUE;
        output->action.value.flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_NULL_POINTER);
        // Both expressions execute, including a nonconstant/effectful expected value. Returning a folded first
        // value must never remove the second expression or its volatile read. Neither parameter requires ICE.
        check_action_inherit(&output->action.value, values + 1);
        if (output->action.value.fault != CHECK_FAULT_NONE)
        {
            output->action.value.constant_kind = CHECK_CONSTANT_NONE;
        }
        else if (output->action.value.constant_kind == CHECK_CONSTANT_INTEGER && (output->action.value.flags & BUSTER_CHECK_SIDE_EFFECT) == 0)
        {
            output->action.value.flags |= BUSTER_CHECK_ICE;
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
                output->frame_level = level;
                output->reads_memory = true;
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
        case CBUILTIN_FROB_RETURN_ADDR: minimum = maximum = 1; break;
        default: check_action_diagnose(&output->action, CHECK_DIAG_UNSUPPORTED, 0); break;
    }
    if (output->action.diagnostic == CHECK_DIAG_NONE && (input->argument_count < minimum || input->argument_count > maximum))
    {
        check_action_diagnose(&output->action, CHECK_DIAG_ARGUMENT_COUNT, 0);
    }
    if (output->action.diagnostic == CHECK_DIAG_NONE && input->argument_count != 0 && input->arguments == NULL)
    {
        check_action_diagnose(&output->action, CHECK_DIAG_INVALID_TYPE, 0);
    }
    if (output->action.diagnostic == CHECK_DIAG_NONE)
    {
        CheckValue values[3] = { 0 };
        output->runtime_builtin = input->id;
        output->evaluated_operands = (UINT32_C(1) << input->argument_count) - 1;
        for (uint32_t i = 0; i < input->argument_count && output->action.diagnostic == CHECK_DIAG_NONE; i += 1)
        {
            values[i] = check_builtin_misc_argument(table, input, output, i);
            check_action_inherit(&output->action.value, values + i);
        }
        if (output->action.diagnostic == CHECK_DIAG_NONE)
        {
            switch (input->id)
            {
                case CBUILTIN_EXPECT: check_builtin_misc_expect(table, input, output, values); break;
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
                default: break;
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
