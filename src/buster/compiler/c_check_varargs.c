// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Typed SysV C varargs actions (#148). Include after c_types.c, c_builtin_catalog.c and c_check_actions.c. The
// caller supplies the target's canonical array va_list and the actual enclosing function/parameter identities;
// no ordinary function prototype stands in for va_arg's type operand. This module neither assigns ABI locations
// nor rejects a semantically valid requested type merely because a selector cannot handle its machine class.
//
// Actions/results are disjoint count-record slices with ordinary C alignment and no readable padding. A zero
// count accepts null pointers, including context/table. Argument counts describe raw source arguments; operands
// has only two value slots, and va_arg's second source argument instead lives in requested_type. Excess arguments
// are diagnosed before accessing any slot. va_start's second expression is checked by the source producer but
// never evaluated here: its effects/faults do not propagate. evaluated_operands records that distinction explicitly.
//
// check_varargs preserves source order while interning decay-pointer types and running irregular compatibility
// checks. Both graph schedules consume this bounded action API at effect barriers. Different legal call partitions
// preserve types, diagnostics, warnings and effects; no buffered work or storage survives a call. Source/backend
// integration and target measurements remain separate acceptance work; this helper alone grants no capability.
#ifndef BUSTER_C_CHECK_VARARGS_INCLUDED
#define BUSTER_C_CHECK_VARARGS_INCLUDED

#define BUSTER_CHECK_VARARGS_NO_PARAMETER UINT32_MAX
#define BUSTER_CHECK_VARARGS_WARN_LAST_PARAMETER 0x01u
#define BUSTER_CHECK_VARARGS_WARN_LAST_PROMOTION 0x02u
#define BUSTER_CHECK_VARARGS_WARN_LAST_REGISTER 0x04u
#define BUSTER_CHECK_VARARGS_WARN_ARGUMENT_PROMOTION 0x08u
#define BUSTER_CHECK_VARARGS_WARN_ARRAY_RESULT 0x10u
#define BUSTER_CHECK_VARARGS_WARN_C17_COMPAT 0x20u
#define BUSTER_CHECK_VARARGS_WARN_QUALIFIERS 0x40u
#define BUSTER_CHECK_VARARGS_WARN_NON_POD 0x80u

typedef struct CheckVarargsContext CheckVarargsContext;
struct CheckVarargsContext
{
    TypeId va_list_type;
    TypeId function_type;
    uint32_t last_parameter;
    TypeId last_parameter_type;
    // BUSTER_CHECK_REGISTER describes the declaration, not a converted argument expression.
    uint32_t last_parameter_flags;
    // The repository's full C year, for example 2017 or 2023.
    uint32_t standard;
    bool variadic;
};

typedef struct CheckVarargsAction CheckVarargsAction;
struct CheckVarargsAction
{
    CBuiltinId id;
    CheckValue operands[2];
    uint32_t argument_count;
    TypeId requested_type;
    // Parameter declaration identity after stripping parens/casts, or NO_PARAMETER for another expression.
    uint32_t second_argument_declaration;
    uint32_t node;
    CheckContext context;
};

typedef struct CheckVarargsResult CheckVarargsResult;
struct CheckVarargsResult
{
    CheckActionResult action;
    TypeId requested_type;
    uint32_t evaluated_operands;
    uint32_t warnings;
    // va_end ends the list's semantic lifetime but does not fabricate a memory write.
    bool mutates_list;
};

static bool check_varargs_start(CBuiltinId id)
{
    return id == CBUILTIN_VA_START || id == CBUILTIN_C23_VA_START;
}

static bool check_varargs_source_complete(const TypeTable* table, TypeId type, const CheckVarargsAction* input)
{
    const bool complete = check_context_source_complete(table, type, &input->context);
    return complete;
}

static bool check_varargs_promoted(const TypeTable* table, TypeId type, bool argument)
{
    const TypeId plain = type_unqualified(type_strip_aligned(table, type));
    TypeId underlying = plain;
    if (type_kind(table, plain) == BUSTER_TYPE_KIND_ENUM && type_is_complete(table, plain))
    {
        underlying = type_unqualified(table->types.base[type_index(plain)]);
    }
    const TypeId promoted = type_promote(table, plain);
    bool result = plain == BUSTER_TYPE_FLOAT;
    if (check_action_is_integer(table, plain) && promoted != underlying)
    {
        result = true;
        if (argument && underlying != BUSTER_TYPE_BOOL)
        {
            uint32_t width = 0;
            uint32_t promoted_width = 0;
            bool is_signed = false;
            bool promoted_signed = false;
            const bool integer = check_action_integer_type(table, underlying, &width, &is_signed) &&
                                 check_action_integer_type(table, promoted, &promoted_width, &promoted_signed);
            // C permits corresponding signed/unsigned arguments when the value is representable in both.
            result = !(integer && width == promoted_width && is_signed != promoted_signed);
        }
    }
    return result;
}

static bool check_varargs_list_complete(const TypeTable* table, TypeId type, const CheckVarargsAction* input)
{
    TypeId loaded = type_strip_aligned(table, type);
    if (type_kind_is_array(type_kind(table, loaded)))
    {
        // Array decay needs the element's source-time type, not a known outer bound. An extern array of the
        // canonical list record can supply the very same pointer as the target's one-element va_list array.
        loaded = type_element(table, loaded);
    }
    return check_varargs_source_complete(table, loaded, input);
}

static void check_varargs_list(TypeTable* table, const CheckVarargsAction* input, CheckVarargsResult* output, TypeId expected, uint32_t index, bool exact)
{
    CheckAction action = {
        .node = input->node,
        .context = input->context,
    };
    action.operands[index] = input->operands[index];
    CheckActionResult* result = &output->action;
    if (!type_is_valid(table, input->operands[index].type) || input->operands[index].category > CHECK_FUNCTION)
    {
        check_action_diagnose(result, CHECK_DIAG_INVALID_TYPE, index);
    }
    else if (input->operands[index].category == CHECK_LVALUE && !check_varargs_list_complete(table, input->operands[index].type, input))
    {
        check_action_diagnose(result, CHECK_DIAG_INCOMPLETE, index);
    }
    else
    {
        CheckValue value = check_action_operand(table, &action, result, index);
        check_action_inherit(&result->value, &value);
        const TypeId source = type_unqualified(type_strip_aligned(table, value.type));
        if (exact)
        {
            bool same = false;
            if (check_action_kind(table, source) == BUSTER_TYPE_KIND_POINTER)
            {
                const TypeId from = type_strip_aligned(table, type_element(table, source));
                const TypeId to = type_strip_aligned(table, type_element(table, expected));
                // Alignment typedef wrappers do not change canonical C type. Pointed-to qualifiers do.
                same = from == to;
            }
            if (!same)
            {
                check_action_diagnose(result, CHECK_DIAG_INCOMPATIBLE, index);
            }
        }
        else
        {
            bool compatible = check_action_assignable(table, expected, &value, &action, result);
            if ((result->warnings & CHECK_WARNING_DISCARDED_QUALIFIERS) != 0)
            {
                output->warnings |= BUSTER_CHECK_VARARGS_WARN_QUALIFIERS;
            }
            if (!compatible && check_action_kind(table, source) == BUSTER_TYPE_KIND_POINTER)
            {
                const TypeId from = type_strip_aligned(table, type_element(table, source));
                const TypeId to = type_strip_aligned(table, type_element(table, expected));
                const TypeAnswer answer = type_compatible(table, type_unqualified(from), type_unqualified(to));
                if (answer == TYPE_ANSWER_FULL)
                {
                    check_action_diagnose(result, CHECK_DIAG_RESOURCE, index);
                }
                const bool same = answer == TYPE_ANSWER_YES || check_action_kind(table, from) == BUSTER_TYPE_KIND_VOID;
                if (same && (type_qualifiers(from) & ~type_qualifiers(to)) != 0)
                {
                    compatible = true;
                    output->warnings |= BUSTER_CHECK_VARARGS_WARN_QUALIFIERS;
                }
            }
            if (!compatible)
            {
                check_action_diagnose(result, CHECK_DIAG_INCOMPATIBLE, index);
            }
            else
            {
                check_action_conversion(table, &value, expected, BUSTER_CHECK_CONVERT_ASSIGNMENT, &result->conversions[index], input->node);
            }
        }
    }
}

static void check_varargs_one(TypeTable* table, const CheckVarargsContext* context, const CheckVarargsAction* input, CheckVarargsResult* output)
{
    *output = (CheckVarargsResult){ 0 };
    CheckActionResult* result = &output->action;
    const bool start = check_varargs_start(input->id);
    const bool argument = input->id == CBUILTIN_VA_ARG;
    const bool copy = input->id == CBUILTIN_VA_COPY;
    const bool end = input->id == CBUILTIN_VA_END;
    const bool known = start || argument || copy || end;
    const uint32_t minimum = input->id == CBUILTIN_C23_VA_START || end ? 1 : 2;
    const uint32_t maximum = end ? 1 : 2;
    if (!known)
    {
        check_action_diagnose(result, CHECK_DIAG_UNSUPPORTED, 0);
    }
    else if (input->argument_count < minimum || input->argument_count > maximum)
    {
        check_action_diagnose(result, CHECK_DIAG_ARGUMENT_COUNT, 0);
    }
    else if (context == NULL || !type_is_valid(table, context->va_list_type))
    {
        check_action_diagnose(result, CHECK_DIAG_INVALID_TYPE, 0);
    }
    else if (input->id == CBUILTIN_C23_VA_START && context->standard < 2023)
    {
        // Clang registers this spelling only in C23 and later language modes.
        check_action_diagnose(result, CHECK_DIAG_UNSUPPORTED, 0);
    }
    TypeId parameter = 0;
    if (result->diagnostic == CHECK_DIAG_NONE)
    {
        const TypeId list = type_unqualified(type_strip_aligned(table, context->va_list_type));
        if (!type_kind_is_array(type_kind(table, list)))
        {
            // The current input target is SysV x86-64. Do not guess another target's reference-like list rule.
            check_action_diagnose(result, CHECK_DIAG_UNSUPPORTED, 0);
        }
        else if (!check_varargs_source_complete(table, list, input))
        {
            check_action_diagnose(result, CHECK_DIAG_INCOMPLETE, 0);
        }
        else
        {
            parameter = type_pointer(table, type_element(table, list));
            if (parameter == 0)
            {
                check_action_diagnose(result, CHECK_DIAG_RESOURCE, 0);
            }
        }
    }
    if (result->diagnostic == CHECK_DIAG_NONE)
    {
        result->value.type = BUSTER_TYPE_VOID;
        result->value.category = CHECK_RVALUE;
        result->value.flags = BUSTER_CHECK_SIDE_EFFECT;
        output->evaluated_operands = copy ? 3 : 1;
        output->mutates_list = !end;
        check_varargs_list(table, input, output, parameter, 0, argument);
        if (copy)
        {
            check_varargs_list(table, input, output, parameter, 1, false);
        }
        if (start)
        {
            const bool function = type_is_valid(table, context->function_type) && check_action_kind(table, context->function_type) == BUSTER_TYPE_KIND_FUNCTION;
            if (!function || !context->variadic || (table->types.flags[type_index(type_strip_aligned(table, context->function_type))] & BUSTER_TYPE_FLAG_VARIADIC) == 0)
            {
                check_action_diagnose(result, CHECK_DIAG_CALL, 0);
            }
            else if (input->id == CBUILTIN_C23_VA_START && input->argument_count == 1)
            {
                output->warnings |= BUSTER_CHECK_VARARGS_WARN_C17_COMPAT;
            }
            else
            {
                const CheckValue* second = &input->operands[1];
                uint32_t width = 0;
                bool is_signed = false;
                const bool zero = input->id == CBUILTIN_VA_START && context->standard >= 2023 &&
                                  type_is_valid(table, second->type) && check_action_integer_type(table, second->type, &width, &is_signed) &&
                                  second->constant_kind == CHECK_CONSTANT_INTEGER && second->fault == CHECK_FAULT_NONE &&
                                  (second->flags & BUSTER_CHECK_ICE) != 0 && (second->flags & BUSTER_CHECK_SIDE_EFFECT) == 0 &&
                                  (second->integer & check_action_mask(width)) == 0;
                if (zero)
                {
                    output->warnings |= BUSTER_CHECK_VARARGS_WARN_C17_COMPAT;
                }
                else if (context->last_parameter == BUSTER_CHECK_VARARGS_NO_PARAMETER || input->second_argument_declaration != context->last_parameter)
                {
                    output->warnings |= BUSTER_CHECK_VARARGS_WARN_LAST_PARAMETER;
                }
                else
                {
                    if ((context->last_parameter_flags & BUSTER_CHECK_REGISTER) != 0)
                    {
                        output->warnings |= BUSTER_CHECK_VARARGS_WARN_LAST_REGISTER;
                    }
                    else if (type_is_valid(table, context->last_parameter_type) && check_varargs_promoted(table, context->last_parameter_type, false))
                    {
                        output->warnings |= BUSTER_CHECK_VARARGS_WARN_LAST_PROMOTION;
                    }
                }
            }
        }
        if (argument)
        {
            output->requested_type = input->requested_type;
            if (!type_is_valid(table, input->requested_type))
            {
                check_action_diagnose(result, CHECK_DIAG_INVALID_TYPE, 1);
            }
            else
            {
                const TypeId requested = type_unqualified(type_strip_aligned(table, input->requested_type));
                const uint8_t kind = type_kind(table, requested);
                result->value.type = requested;
                if (kind == BUSTER_TYPE_KIND_FUNCTION || kind == BUSTER_TYPE_KIND_ATOMIC)
                {
                    // Clang's non-pod-varargs warning is an error by default. The bit retains its warning origin
                    // for a future diagnostic-policy consumer that explicitly downgrades that group.
                    output->warnings |= BUSTER_CHECK_VARARGS_WARN_NON_POD;
                    check_action_diagnose(result, CHECK_DIAG_INVALID_TYPE, 1);
                }
                else if (kind == BUSTER_TYPE_KIND_VOID || !check_varargs_source_complete(table, requested, input))
                {
                    check_action_diagnose(result, CHECK_DIAG_INCOMPLETE, 1);
                }
                else
                {
                    if (type_kind_is_array(kind))
                    {
                        // Clang keeps an array rvalue here and warns; later ordinary array conversion is separate.
                        output->warnings |= BUSTER_CHECK_VARARGS_WARN_ARRAY_RESULT;
                    }
                    if (check_varargs_promoted(table, requested, true))
                    {
                        output->warnings |= BUSTER_CHECK_VARARGS_WARN_ARGUMENT_PROMOTION;
                    }
                }
            }
        }
    }
    if (result->diagnostic != CHECK_DIAG_NONE)
    {
        output->mutates_list = false;
    }
}

static void check_varargs(TypeTable* table, const CheckVarargsContext* context, const CheckVarargsAction* actions, CheckVarargsResult* results, uint32_t count)
{
    for (uint32_t i = 0; i < count; i += 1)
    {
        check_varargs_one(table, context, actions + i, results + i);
    }
}

#endif
