// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Per-unit native helpers for reached atomic, terminal and floating-environment builtin nodes. Include after CheckProgram and the
// standalone builtin helper emitters. The program's sorted active-node list is the execution consumer's input;
// unselected source expressions never reserve helpers. Signatures are deduplicated in first-use order, and
// symbols follow the program's ordinary symbols. This planner never mutates Link or publishes code atoms.
//
// Calls use the helpers' raw SysV integer ABI: addresses/value bits are unsigned long, orders are int, and loads
// return unsigned long. The caller applies checked argument conversions and converts a load result to its C
// value type. Atomic/environment calls remain unknown-effect compiler barriers; terminal calls have no successor.
// Output and scratch are disjoint stable-address arenas. Failure restores both lengths and clears the result.
#ifndef BUSTER_C_CHECK_PROGRAM_HELPERS_INCLUDED
#define BUSTER_C_CHECK_PROGRAM_HELPERS_INCLUDED

#define BUSTER_CHECK_PROGRAM_HELPER_SIGNATURES 28u

typedef struct CheckProgramHelper CheckProgramHelper;
struct CheckProgramHelper
{
    CBuiltinId id;
    uint32_t bytes;
    SymbolId symbol;
    Tier0Code code;
    uint32_t parameter_count;
    TypeId parameter_types[3];
    TypeId result_type;
    CheckBuiltinMathKind math_kind;
    TypeId floating_type;
    bool terminates;
    bool compiler_barrier;
    bool reads_fp_environment;
    bool may_raise_fp_exceptions;
    bool may_raise_inexact;
    bool requires_errno_address;
    uint32_t required_alignment;
};

typedef struct CheckProgramHelperUse CheckProgramHelperUse;
struct CheckProgramHelperUse
{
    uint32_t node;
    uint32_t helper;
};

typedef struct CheckProgramHelpers CheckProgramHelpers;
struct CheckProgramHelpers
{
    CheckProgramHelper* items;
    uint32_t count;
    CheckProgramHelperUse* uses;
    uint32_t use_count;
    CheckProgramStatus status;
    uint32_t token;
    // What an UNSUPPORTED status lacks, or NULL.
    const char* reason;
};

static uint32_t check_program_helper_key(CBuiltinId id, uint64_t bytes)
{
    uint32_t key = UINT32_MAX;
    if ((id == CBUILTIN_ATOMIC_LOAD_N || id == CBUILTIN_ATOMIC_STORE_N) &&
        (bytes == 1 || bytes == 2 || bytes == 4 || bytes == 8))
    {
        key = (id == CBUILTIN_ATOMIC_STORE_N ? 4 : 0) + (bytes == 1 ? 0 : bytes == 2 ? 1 : bytes == 4 ? 2 : 3);
    }
    else if (bytes == 0)
    {
        switch (id)
        {
            case CBUILTIN_ATOMIC_THREAD_FENCE: key = 8; break;
            case CBUILTIN_ATOMIC_SIGNAL_FENCE: key = 9; break;
            case CBUILTIN_SYNC_SYNCHRONIZE: key = 10; break;
            case CBUILTIN_TRAP: key = 11; break;
            case CBUILTIN_UNREACHABLE: key = 12; break;
            case CBUILTIN_FLT_ROUNDS: key = 13; break;
            default: break;
        }
        if (id >= CBUILTIN_SQRT && id <= CBUILTIN_RINTF)
        {
            key = 14 + (uint32_t)(id - CBUILTIN_SQRT);
        }
    }
    return key;
}

static CheckProgramStatus check_program_helper_math(const CheckProgram* program, const CheckBuiltinFloatEffect* effect, CBuiltinId id, CheckProgramHelper* helper)
{
    const CheckBuiltinMathKind kind = check_builtin_math_kind(id);
    const bool copy = kind == CHECK_BUILTIN_MATH_COPYSIGN;
    const bool square_root = kind == CHECK_BUILTIN_MATH_SQRT;
    const uint32_t arguments = copy ? 2 : 1;
    const uint32_t mask = (1u << arguments) - 1;
    const TypeId floating = check_builtin_math_parameter(id);
    CheckProgramStatus status = CHECK_PROGRAM_OK;
    if (effect == NULL || effect->runtime_builtin != id || effect->math_kind != kind || kind == CHECK_BUILTIN_MATH_NONE ||
        effect->floating_type != floating || effect->evaluated_operands != mask || effect->definite_operands != mask ||
        effect->folded || effect->may_set_errno != square_root ||
        (!copy && (!effect->compiler_barrier || !effect->reads_fp_environment || !effect->may_raise_fp_exceptions)) ||
        ((square_root || kind == CHECK_BUILTIN_MATH_RINT) && !effect->may_raise_inexact))
    {
        status = CHECK_PROGRAM_INVALID;
    }
    else if (effect->unsupported != 0)
    {
        status = CHECK_PROGRAM_UNSUPPORTED;
    }
    else if (square_root && (program->types == NULL || !type_is_valid(program->types, effect->errno_pointer_type) ||
                            check_action_kind(program->types, effect->errno_pointer_type) != BUSTER_TYPE_KIND_POINTER ||
                            type_element(program->types, effect->errno_pointer_type) != BUSTER_TYPE_INT))
    {
        status = CHECK_PROGRAM_INVALID;
    }
    for (uint32_t i = 0; status == CHECK_PROGRAM_OK && i < arguments; i += 1)
    {
        if (effect->converted_arguments[i].type != floating || effect->converted_arguments[i].category != CHECK_RVALUE)
        {
            status = CHECK_PROGRAM_INVALID;
        }
    }
    if (status == CHECK_PROGRAM_OK)
    {
        *helper = (CheckProgramHelper){
            .id = id,
            .parameter_count = copy || square_root ? 2 : 1,
            .parameter_types = {
                BUSTER_TYPE_ULONG,
                copy || square_root ? BUSTER_TYPE_ULONG : BUSTER_TYPE_VOID,
                BUSTER_TYPE_VOID,
            },
            .result_type = BUSTER_TYPE_ULONG,
            .math_kind = kind,
            .floating_type = floating,
            .compiler_barrier = true,
            .reads_fp_environment = effect->reads_fp_environment,
            .may_raise_fp_exceptions = effect->may_raise_fp_exceptions,
            .may_raise_inexact = effect->may_raise_inexact,
            .requires_errno_address = square_root,
        };
    }
    return status;
}

static CheckProgramStatus check_program_helper_atomic(const CheckBuiltinAtomicEffect* effect, CBuiltinId id, CheckProgramHelper* helper)
{
    CheckProgramStatus status = CHECK_PROGRAM_OK;
    const bool memory = id == CBUILTIN_ATOMIC_LOAD_N || id == CBUILTIN_ATOMIC_STORE_N;
    const bool load = id == CBUILTIN_ATOMIC_LOAD_N;
    const bool store = id == CBUILTIN_ATOMIC_STORE_N;
    const uint32_t parameters = load ? 2 : store ? 3 : id == CBUILTIN_SYNC_SYNCHRONIZE ? 0 : 1;
    if (check_builtin_atomic_rmw_rule(id) != NULL && effect->runtime_builtin == id && effect->compiler_barrier)
    {
        // The read/modify/write leaves (c_builtin_atomic_rmw_helpers.c) have no planned signature here yet.
        status = CHECK_PROGRAM_UNSUPPORTED;
    }
    else if (effect->runtime_builtin != id || !effect->compiler_barrier ||
             effect->evaluated_operands != (UINT32_C(1) << parameters) - 1 || effect->rmw)
    {
        status = CHECK_PROGRAM_INVALID;
    }
    else if (effect->unsupported != 0 || check_program_helper_key(id, effect->object_size) == UINT32_MAX)
    {
        status = CHECK_PROGRAM_UNSUPPORTED;
    }
    else if (memory && (!effect->alignment_known || effect->minimum_alignment < effect->object_size))
    {
        status = CHECK_PROGRAM_UNSUPPORTED;
    }
    else if ((memory && (effect->required_alignment != effect->object_size || effect->minimum_alignment == 0 ||
                         (effect->minimum_alignment & (effect->minimum_alignment - 1)) != 0)) ||
             (!memory && effect->required_alignment != 0))
    {
        status = CHECK_PROGRAM_INVALID;
    }
    if (status == CHECK_PROGRAM_OK)
    {
        *helper = (CheckProgramHelper){
            .id = id,
            .bytes = (uint32_t)effect->object_size,
            .parameter_count = parameters,
            .parameter_types = {
                memory ? BUSTER_TYPE_ULONG : parameters != 0 ? BUSTER_TYPE_INT : BUSTER_TYPE_VOID,
                load ? BUSTER_TYPE_INT : store ? BUSTER_TYPE_ULONG : BUSTER_TYPE_VOID,
                store ? BUSTER_TYPE_INT : BUSTER_TYPE_VOID,
            },
            .result_type = load ? BUSTER_TYPE_ULONG : BUSTER_TYPE_VOID,
            .compiler_barrier = true,
            .required_alignment = memory ? (uint32_t)effect->object_size : 0,
        };
    }
    return status;
}

static bool check_program_helpers(CheckProgramHelpers* result, const CheckProgram* program, Arena* output, Arena* scratch)
{
    *result = (CheckProgramHelpers){ .token = BUSTER_CHECK_NONE };
    const uint64_t mark = output != 0 ? output->length : 0;
    const uint64_t temporary = scratch != 0 ? scratch->length : 0;
    bool valid = program != 0 && program->status == CHECK_PROGRAM_OK && program->input != 0 && program->stream != 0 &&
                 output != 0 && scratch != 0 && output != scratch && output->length <= output->capacity && scratch->length <= scratch->capacity &&
                 (output->base != 0 || output->capacity == 0) && (scratch->base != 0 || scratch->capacity == 0);
    if (valid)
    {
        const CheckGraph* graph = &program->input->graph;
        const CheckStream* stream = program->stream;
        valid = stream->status == CHECK_STATUS_OK && stream->report_count == 0 && stream->count == graph->count &&
                (program->builtin_count == 0 || (program->builtin_nodes != 0 && graph->ops != 0 && graph->members != 0 && graph->tokens != 0)) &&
                (stream->atomic_count == 0 || stream->atomics != 0) && (stream->misc_count == 0 || stream->misc != 0) &&
                (stream->floating_count == 0 || stream->floating != 0);
    }
    if (!valid)
    {
        result->status = CHECK_PROGRAM_INVALID;
    }
    CheckProgramHelper planned[BUSTER_CHECK_PROGRAM_HELPER_SIGNATURES] = { 0 };
    uint32_t first_nodes[BUSTER_CHECK_PROGRAM_HELPER_SIGNATURES] = { 0 };
    uint32_t keys[BUSTER_CHECK_PROGRAM_HELPER_SIGNATURES] = { 0 };
    CheckProgramHelperUse* uses = valid && program->builtin_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckProgramHelperUse, program->builtin_count) : 0;
    if (valid && program->builtin_count != 0 && uses == 0)
    {
        result->status = CHECK_PROGRAM_RESOURCE;
    }
    uint32_t atomic = 0;
    uint32_t misc = 0;
    uint32_t floating = 0;
    for (uint32_t i = 0; result->status == CHECK_PROGRAM_OK && i < program->builtin_count; i += 1)
    {
        const CheckGraph* graph = &program->input->graph;
        const CheckStream* stream = program->stream;
        const uint32_t node = program->builtin_nodes[i];
        if (node >= graph->count || (i != 0 && node <= program->builtin_nodes[i - 1]))
        {
            result->status = CHECK_PROGRAM_INVALID;
        }
        else
        {
            const CheckOp op = graph->ops[node];
            const CBuiltinId id = (CBuiltinId)graph->members[node];
            const bool terminal = op == CHECK_OP_BUILTIN_MISC && (id == CBUILTIN_TRAP || id == CBUILTIN_UNREACHABLE);
            const bool rounds = op == CHECK_OP_BUILTIN_FLOAT && id == CBUILTIN_FLT_ROUNDS;
            const bool math = op == CHECK_OP_BUILTIN_FLOAT && check_builtin_math_kind(id) != CHECK_BUILTIN_MATH_NONE;
            if (op == CHECK_OP_BUILTIN_ATOMIC || terminal || rounds || math)
            {
                CheckProgramHelper helper = { 0 };
                bool folded = false;
                if (terminal)
                {
                    while (misc < stream->misc_count && stream->misc[misc].node < node)
                    {
                        misc += 1;
                    }
                    if (misc == stream->misc_count || stream->misc[misc].node != node || stream->misc[misc].runtime_builtin != id ||
                        !stream->misc[misc].terminates || stream->misc[misc].evaluated_operands != 0 || stream->misc[misc].unsupported != 0)
                    {
                        result->status = CHECK_PROGRAM_INVALID;
                    }
                    else
                    {
                        helper = (CheckProgramHelper){
                            .id = id,
                            .result_type = BUSTER_TYPE_VOID,
                            .terminates = true,
                        };
                    }
                }
                else if (rounds || math)
                {
                    while (floating < stream->floating_count && stream->floating[floating].node < node)
                    {
                        floating += 1;
                    }
                    const CheckBuiltinFloatEffect* effect = floating < stream->floating_count ? stream->floating + floating : 0;
                    if (math)
                    {
                        if (effect == NULL || effect->node != node)
                        {
                            result->status = CHECK_PROGRAM_INVALID;
                        }
                        else if (effect->folded && effect->runtime_builtin == CBUILTIN_NONE && effect->math_kind == CHECK_BUILTIN_MATH_COPYSIGN &&
                                 effect->floating_type == check_builtin_math_parameter(id) && effect->unsupported == 0 && effect->evaluated_operands == 3 &&
                                 effect->definite_operands == 3 && !effect->compiler_barrier && !effect->reads_fp_environment && !effect->may_set_errno)
                        {
                            folded = true;
                        }
                        else
                        {
                            result->status = check_program_helper_math(program, effect, id, &helper);
                        }
                    }
                    else if (effect == 0 || effect->node != node || effect->runtime_builtin != id || !effect->compiler_barrier ||
                        !effect->reads_fp_environment || effect->folded || effect->evaluated_operands != 0 ||
                        effect->definite_operands != 0 || effect->floating_type != 0 || effect->unsupported != 0)
                    {
                        result->status = CHECK_PROGRAM_INVALID;
                    }
                    else
                    {
                        helper = (CheckProgramHelper){
                            .id = id,
                            .result_type = BUSTER_TYPE_INT,
                            .compiler_barrier = true,
                            .reads_fp_environment = true,
                        };
                    }
                }
                else
                {
                    while (atomic < stream->atomic_count && stream->atomics[atomic].node < node)
                    {
                        atomic += 1;
                    }
                    if (atomic == stream->atomic_count || stream->atomics[atomic].node != node)
                    {
                        result->status = CHECK_PROGRAM_INVALID;
                    }
                    else
                    {
                        result->status = check_program_helper_atomic(stream->atomics + atomic, id, &helper);
                        result->reason = result->status == CHECK_PROGRAM_UNSUPPORTED && check_builtin_atomic_rmw_rule(id) != NULL ?
                                         "atomic read-modify-write builtins" : NULL;
                    }
                }
                if (result->status == CHECK_PROGRAM_OK && !folded)
                {
                    const uint32_t key = check_program_helper_key(helper.id, helper.bytes);
                    if (key >= BUSTER_CHECK_PROGRAM_HELPER_SIGNATURES)
                    {
                        result->status = CHECK_PROGRAM_UNSUPPORTED;
                    }
                    else
                    {
                        if (keys[key] == 0)
                        {
                            first_nodes[result->count] = node;
                            planned[result->count] = helper;
                            result->count += 1;
                            keys[key] = result->count;
                        }
                        else
                        {
                            CheckProgramHelper* retained = planned + keys[key] - 1;
                            retained->reads_fp_environment |= helper.reads_fp_environment;
                            retained->may_raise_fp_exceptions |= helper.may_raise_fp_exceptions;
                            retained->may_raise_inexact |= helper.may_raise_inexact;
                        }
                        uses[result->use_count++] = (CheckProgramHelperUse){
                            .node = node,
                            .helper = keys[key] - 1,
                        };
                    }
                }
            }
            if (result->status != CHECK_PROGRAM_OK)
            {
                result->token = graph->tokens[node];
            }
        }
    }
    if (result->status == CHECK_PROGRAM_OK && program->symbol_count > UINT32_MAX - result->count - 1)
    {
        result->status = CHECK_PROGRAM_RESOURCE;
    }
    if (result->status == CHECK_PROGRAM_OK)
    {
        result->items = result->count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckProgramHelper, result->count) : 0;
        result->uses = result->use_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckProgramHelperUse, result->use_count) : 0;
        if ((result->count != 0 && result->items == 0) || (result->use_count != 0 && result->uses == 0))
        {
            result->status = CHECK_PROGRAM_RESOURCE;
        }
    }
    for (uint32_t i = 0; result->status == CHECK_PROGRAM_OK && i < result->count; i += 1)
    {
        CheckProgramHelper* helper = planned + i;
        if (helper->terminates)
        {
            const CBuiltinTerminalHelper native = cbuiltin_terminal_helper(helper->id, output);
            helper->code = native.code;
        }
        else if (helper->math_kind != CHECK_BUILTIN_MATH_NONE)
        {
            const CBuiltinMathHelper native = cbuiltin_math_helper(helper->id, output);
            helper->code = native.code;
            if (native.code.error == TIER0_OK &&
                (native.parameter_count != helper->parameter_count || native.result_class != BUSTER_TYPED_CLASS_U64 ||
                 native.parameter_classes[0] != BUSTER_TYPED_CLASS_U64 ||
                 (native.parameter_count > 1 && native.parameter_classes[1] != BUSTER_TYPED_CLASS_U64) ||
                 native.requires_errno_address != helper->requires_errno_address))
            {
                result->status = CHECK_PROGRAM_INVALID;
                result->token = program->input->graph.tokens[first_nodes[i]];
            }
            helper->compiler_barrier |= native.compiler_barrier;
            helper->reads_fp_environment |= native.reads_fp_environment;
            helper->may_raise_fp_exceptions |= native.may_raise_fp_exceptions;
            helper->may_raise_inexact |= native.may_raise_inexact;
        }
        else if (helper->id == CBUILTIN_FLT_ROUNDS)
        {
            const CBuiltinRoundsHelper native = cbuiltin_rounds_helper(helper->id, output);
            helper->code = native.code;
        }
        else
        {
            const CBuiltinAtomicHelper native = cbuiltin_atomic_helper_emit(helper->id, helper->bytes, output);
            helper->code = native.code;
        }
        if (helper->code.error != TIER0_OK)
        {
            result->status = helper->code.error == TIER0_ERROR_MEMORY ? CHECK_PROGRAM_RESOURCE : CHECK_PROGRAM_BACKEND;
            result->token = program->input->graph.tokens[first_nodes[i]];
        }
        else
        {
            helper->symbol = program->symbol_count + 1 + i;
            result->items[i] = *helper;
        }
    }
    valid = result->status == CHECK_PROGRAM_OK;
    if (valid && result->use_count != 0)
    {
        memcpy(result->uses, uses, (uint64_t)result->use_count * sizeof(*uses));
    }
    if (!valid)
    {
        const CheckProgramStatus status = result->status;
        const uint32_t token = result->token;
        const char* reason = result->reason;
        if (output != 0)
        {
            arena_pop_to(output, mark);
        }
        *result = (CheckProgramHelpers){
            .status = status,
            .token = token,
            .reason = reason,
        };
    }
    if (scratch != 0)
    {
        arena_pop_to(scratch, temporary);
    }
    return valid;
}

#endif
