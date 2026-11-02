// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Typed integer builtin actions for #148. Include after c_builtin.c and c_check_actions.c. The source adapter owns
// name binding and graph dependencies; check_builtins consumes already checked operand values and never executes
// an input program's store. Overflow keeps its destination, stored value and boolean separate even after folding.
// These actions alone do not establish source or selector support, and do not authorize __has_builtin advertising.
//
// Each input/output slice has count logical records, requires ordinary C alignment and no padding, and is disjoint
// from the other slice and the type table. Zero count permits null slices. The type table may intern array/function
// decay types in input order. Eight-row local columns feed the existing fold kernel; partial batches flush at this
// call's end. No work survives the call and different legal partitions produce identical semantic results.
#ifndef BUSTER_C_CHECK_BUILTIN_INCLUDED
#define BUSTER_C_CHECK_BUILTIN_INCLUDED

#define BUSTER_CHECK_BUILTIN_NOT_CONSTANT UINT32_MAX

typedef struct CheckBuiltinAction CheckBuiltinAction;
struct CheckBuiltinAction
{
    CBuiltinId id;
    CheckValue operands[3];
    uint32_t argument_count;
    uint32_t node;
    // Optional immutable tag-completion token positions; null uses the current table for direct action callers.
    // source_position is the exclusive expression end, independently of the diagnostic node identity.
    const uint32_t* tag_completions;
    uint32_t tag_completion_count;
    uint32_t source_position;
    uint32_t extensions;
    // The unit's constant objects' receipts and whether the action is in a constant context, whose operand
    // conversions' lvalue read loads a const object's constant initializer as clang's evaluator does (a builtin
    // call it folds to an integer constant without side effects is an integer constant expression there); an
    // ordinary call's runtime view doesn't request the read and loads none.
    const CheckConstValues* const_values;
    bool constant_evaluated;
};

typedef struct CheckBuiltinResult CheckBuiltinResult;
struct CheckBuiltinResult
{
    // Operand conversions retain the original integer shapes for overflow; they never apply usual arithmetic
    // conversions there. store_conversion.type retains the pointed-to qualifiers for a volatile destination.
    CheckActionResult action;
    CheckValue store_value;
    CheckValue store_destination;
    // CBuiltinFoldStatus for an attempted fold or invalid action; NOT_CONSTANT for a valid runtime action.
    uint32_t fold_status;
    // A valid overflow action always requires this store, including when its boolean and stored bits are known.
    bool store;
};

typedef struct CheckBuiltinBatch CheckBuiltinBatch;
struct CheckBuiltinBatch
{
    uint32_t ids[BUSTER_CBUILTIN_LANES];
    uint64_t a[BUSTER_CBUILTIN_LANES];
    uint64_t b[BUSTER_CBUILTIN_LANES];
    uint32_t a_types[BUSTER_CBUILTIN_LANES];
    uint32_t b_types[BUSTER_CBUILTIN_LANES];
    uint32_t result_types[BUSTER_CBUILTIN_LANES];
    uint64_t values[BUSTER_CBUILTIN_LANES];
    uint32_t overflows[BUSTER_CBUILTIN_LANES];
    uint32_t statuses[BUSTER_CBUILTIN_LANES];
};

static bool check_builtin_overflow(CBuiltinId id)
{
    return id >= CBUILTIN_ADD_OVERFLOW && id <= CBUILTIN_MUL_OVERFLOW;
}

static TypeId check_builtin_parameter(CBuiltinId id)
{
    TypeId result = BUSTER_TYPE_UINT;
    switch (id)
    {
        case CBUILTIN_BSWAP16: result = BUSTER_TYPE_USHORT; break;
        case CBUILTIN_BSWAP64:
        case CBUILTIN_CLZL:
        case CBUILTIN_CTZL:
        case CBUILTIN_POPCOUNTL:
        case CBUILTIN_PARITYL: result = BUSTER_TYPE_ULONG; break;
        case CBUILTIN_CLZLL:
        case CBUILTIN_CTZLL:
        case CBUILTIN_POPCOUNTLL:
        case CBUILTIN_PARITYLL: result = BUSTER_TYPE_ULLONG; break;
        case CBUILTIN_FFS: result = BUSTER_TYPE_INT; break;
        case CBUILTIN_FFSL: result = BUSTER_TYPE_LONG; break;
        case CBUILTIN_FFSLL: result = BUSTER_TYPE_LLONG; break;
        default: break;
    }
    return result;
}

static bool check_builtin_source_complete(const TypeTable* table, TypeId type, const CheckBuiltinAction* action)
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

// An overflow builtin's operand's descriptor for the 64-bit fold, or 0 with a diagnostic. One of 65 to 128 bits (an
// __int128, a _BitInt) is valid but can't enter the fold by truncation: `wide` says so, and its action is a runtime one,
// never folded, which C allows, since the builtin's store keeps it out of every constant expression.
static uint32_t check_builtin_descriptor(const TypeTable* table, TypeId type, const CheckBuiltinAction* action, CheckActionResult* result, uint32_t operand,
                                         bool* wide)
{
    uint32_t descriptor = 0;
    uint32_t width = 0;
    bool is_signed = false;
    if (!check_action_is_integer(table, type))
    {
        check_action_diagnose(result, CHECK_DIAG_INTEGER_REQUIRED, operand);
    }
    else if (!check_builtin_source_complete(table, type, action))
    {
        check_action_diagnose(result, CHECK_DIAG_INCOMPLETE, operand);
    }
    else if (!check_action_integer_type(table, type, &width, &is_signed) || width > 128)
    {
        check_action_diagnose(result, CHECK_DIAG_UNSUPPORTED, operand);
    }
    else
    {
        *wide |= width > 64;
        descriptor = width > 64 ? 0 : width | (is_signed ? BUSTER_CBUILTIN_SIGNED : 0);
    }
    return descriptor;
}

static bool check_builtin_prepare(TypeTable* table, const CheckBuiltinAction* input, CheckBuiltinResult* output, CheckBuiltinBatch* batch, uint32_t lane)
{
    *output = (CheckBuiltinResult){
        .fold_status = BUSTER_CHECK_BUILTIN_NOT_CONSTANT,
    };
    CheckActionResult* result = &output->action;
    const bool overflow = check_builtin_overflow(input->id);
    const uint32_t count = overflow ? 3 : 1;
    bool ready = false;
    if (input->id <= CBUILTIN_NONE || input->id >= CBUILTIN_COUNT)
    {
        check_action_diagnose(result, CHECK_DIAG_UNSUPPORTED, 0);
    }
    else if (input->argument_count != count)
    {
        check_action_diagnose(result, CHECK_DIAG_ARGUMENT_COUNT, 0);
    }
    CheckAction action = {
        .node = input->node,
        .tag_completions = input->tag_completions,
        .tag_completion_count = input->tag_completion_count,
        .source_position = input->source_position,
        .extensions = input->extensions,
        .const_values = input->const_values,
        .constant_evaluated = input->constant_evaluated,
    };
    for (uint32_t i = 0; i < count && result->diagnostic == CHECK_DIAG_NONE; i += 1)
    {
        action.operands[i] = input->operands[i];
        if (!type_is_valid(table, input->operands[i].type) || input->operands[i].category > CHECK_FUNCTION)
        {
            check_action_diagnose(result, CHECK_DIAG_INVALID_TYPE, i);
        }
    }
    if (result->diagnostic == CHECK_DIAG_NONE)
    {
        result->value.type = overflow ? BUSTER_TYPE_BOOL : input->id <= CBUILTIN_BSWAP64 ? check_builtin_parameter(input->id) : BUSTER_TYPE_INT;
        result->value.category = CHECK_RVALUE;
        CheckValue values[3] = { 0 };
        for (uint32_t i = 0; i < count; i += 1)
        {
            values[i] = check_action_operand(table, &action, result, i);
            check_action_inherit(&result->value, &values[i]);
        }
        if (!overflow)
        {
            const uint8_t kind = check_action_kind(table, values[0].type);
            if (kind == BUSTER_TYPE_KIND_ENUM && !check_builtin_source_complete(table, values[0].type, input))
            {
                check_action_diagnose(result, CHECK_DIAG_INCOMPLETE, 0);
            }
            else if (!check_action_is_arithmetic(table, values[0].type))
            {
                check_action_diagnose(result, CHECK_DIAG_ARITHMETIC_REQUIRED, 0);
            }
            else if (kind == BUSTER_TYPE_KIND_COMPLEX || (kind == BUSTER_TYPE_KIND_FLOATING && !check_action_float_supported(table, values[0].type)))
            {
                // CheckValue does not yet carry a complex constant or the real-component runtime conversion.
                check_action_diagnose(result, CHECK_DIAG_UNSUPPORTED, 0);
            }
            else if (check_action_is_integer(table, values[0].type) && !type_is_complete(table, values[0].type))
            {
                check_action_diagnose(result, CHECK_DIAG_INCOMPLETE, 0);
            }
            else
            {
                check_action_conversion(table, &values[0], check_builtin_parameter(input->id), BUSTER_CHECK_CONVERT_ASSIGNMENT, &result->conversions[0], input->node);
                check_action_inherit(&result->value, &values[0]);
                ready = values[0].constant_kind == CHECK_CONSTANT_INTEGER && result->value.fault == CHECK_FAULT_NONE;
                batch->a[lane] = (uint64_t)values[0].integer;
                // Clang's builtin constant-expression rule admits folded arguments, including comma expressions
                // and converted floating constants. Their original expressions still retain all effects/faults.
                if (ready && (result->value.flags & BUSTER_CHECK_SIDE_EFFECT) == 0)
                {
                    result->value.flags |= BUSTER_CHECK_ICE;
                }
            }
        }
        else
        {
            result->value.flags |= BUSTER_CHECK_SIDE_EFFECT;
            bool wide = false;
            batch->a_types[lane] = check_builtin_descriptor(table, values[0].type, input, result, 0, &wide);
            batch->b_types[lane] = check_builtin_descriptor(table, values[1].type, input, result, 1, &wide);
            if (check_action_kind(table, values[2].type) != BUSTER_TYPE_KIND_POINTER)
            {
                check_action_diagnose(result, CHECK_DIAG_POINTER_REQUIRED, 2);
            }
            else
            {
                const TypeId destination = type_strip_aligned(table, type_element(table, values[2].type));
                if ((type_qualifiers(destination) & BUSTER_TYPE_CONST) != 0)
                {
                    check_action_diagnose(result, CHECK_DIAG_MODIFIABLE_REQUIRED, 2);
                }
                batch->result_types[lane] = check_builtin_descriptor(table, destination, input, result, 2, &wide);
                output->store_value.type = type_unqualified(destination);
                output->store_value.category = CHECK_RVALUE;
                output->store_destination = values[2];
                result->store_conversion = (CheckConversion){
                    .type = destination,
                    .flags = BUSTER_CHECK_CONVERT_ASSIGNMENT,
                };
            }
            output->store = result->diagnostic == CHECK_DIAG_NONE;
            ready = output->store && !wide && values[0].constant_kind == CHECK_CONSTANT_INTEGER && values[1].constant_kind == CHECK_CONSTANT_INTEGER &&
                    result->value.fault == CHECK_FAULT_NONE;
            batch->a[lane] = (uint64_t)values[0].integer;
            batch->b[lane] = (uint64_t)values[1].integer;
        }
    }
    if (result->diagnostic != CHECK_DIAG_NONE)
    {
        output->fold_status = result->diagnostic == CHECK_DIAG_UNSUPPORTED ? CBUILTIN_UNSUPPORTED : CBUILTIN_INVALID_TYPE;
        result->value.constant_kind = CHECK_CONSTANT_NONE;
        result->value.flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_ICE_UNEVALUATED);
        ready = false;
        output->store = false;
    }
    if (ready)
    {
        batch->ids[lane] = input->id;
    }
    return ready;
}

static void check_builtins(TypeTable* table, const CheckBuiltinAction* actions, CheckBuiltinResult* results, uint32_t count, bool scalar)
{
    (void)scalar;

    for (uint32_t begin = 0; begin < count;)
    {
        const uint32_t lanes = count - begin < BUSTER_CBUILTIN_LANES ? count - begin : BUSTER_CBUILTIN_LANES;
        CheckBuiltinBatch batch = { 0 };
        bool any = false;
        for (uint32_t lane = 0; lane < lanes; lane += 1)
        {
            any |= check_builtin_prepare(table, actions + begin + lane, results + begin + lane, &batch, lane);
        }
        if (any)
        {
            const CBuiltinIntegers input = {
                .id = batch.ids,
                .a = batch.a,
                .b = batch.b,
                .a_type = batch.a_types,
                .b_type = batch.b_types,
                .result_type = batch.result_types,
            };
            const CBuiltinIntegerResults output = {
                .value = batch.values,
                .overflow = batch.overflows,
                .status = batch.statuses,
            };
            if (BUSTER_SCALAR_PATH(scalar))
            {
                cbuiltin_fold_scalar(input, output, lanes);
            }
            else
            {
                cbuiltin_fold(input, output, lanes);
            }
            for (uint32_t lane = 0; lane < lanes; lane += 1)
            {
                if (batch.ids[lane] != CBUILTIN_NONE)
                {
                    CheckBuiltinResult* result = results + begin + lane;
                    result->fold_status = batch.statuses[lane];
                    if (batch.statuses[lane] == CBUILTIN_FOLDED)
                    {
                        result->action.value.constant_kind = CHECK_CONSTANT_INTEGER;
                        result->action.value.integer = result->store ? batch.overflows[lane] : batch.values[lane];
                        if (result->store)
                        {
                            result->store_value.constant_kind = CHECK_CONSTANT_INTEGER;
                            result->store_value.integer = batch.values[lane];
                        }
                    }
                    else
                    {
                        const bool unevaluated = batch.statuses[lane] == CBUILTIN_UNDEFINED && (result->action.value.flags & BUSTER_CHECK_ICE) != 0;
                        check_action_set_fault(&result->action.value, CHECK_FAULT_UNSUPPORTED_CONSTANT, actions[begin + lane].node);
                        result->action.value.flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_ICE_UNEVALUATED);
                        result->action.value.flags |= unevaluated ? BUSTER_CHECK_ICE_UNEVALUATED : 0;
                    }
                }
            }
        }
        begin += lanes;
    }
}

#endif
