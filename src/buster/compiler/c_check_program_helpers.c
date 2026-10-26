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
//
// An atomic access's object must be aligned to its size, as clang's code generation gives its address's alignment
// (c_checker.c's check_graph_atomic_alignment): a misaligned one, which clang calls libatomic for, is refused.
#ifndef BUSTER_C_CHECK_PROGRAM_HELPERS_INCLUDED
#define BUSTER_C_CHECK_PROGRAM_HELPERS_INCLUDED

#define BUSTER_CHECK_PROGRAM_HELPER_FIXED 28u
// A read/modify/write signature's key follows the others': its rule's, its width's and its format's.
#define BUSTER_CHECK_PROGRAM_HELPER_SIGNATURES (BUSTER_CHECK_PROGRAM_HELPER_FIXED + (uint32_t)BUSTER_ARRAY_LENGTH(check_builtin_atomic_rmw_rules) * 16u)

typedef struct CheckProgramHelper CheckProgramHelper;
struct CheckProgramHelper
{
    CBuiltinId id;
    uint32_t bytes;
    SymbolId symbol;
    Tier0Code code;
    uint32_t parameter_count;
    TypeId parameter_types[6];
    TypeId result_type;
    // A read/modify/write leaf's (c_builtin_atomic_rmw_helpers.c) value format.
    CBuiltinAtomicRmwFormat rmw_format;
    bool rmw;
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

static uint32_t check_program_helper_key(CBuiltinId id, uint64_t bytes, bool rmw, CBuiltinAtomicRmwFormat format)
{
    uint32_t key = UINT32_MAX;
    const CheckBuiltinAtomicRmwRule* rule = rmw ? check_builtin_atomic_rmw_rule(id) : NULL;
    const uint32_t width = bytes == 1 ? 0 : bytes == 2 ? 1 : bytes == 4 ? 2 : 3;
    // A floating format's width is its type's.
    const uint32_t layout = format == CBUILTIN_ATOMIC_RMW_INTEGER_BITS ? 0 : format == CBUILTIN_ATOMIC_RMW_BOOL_BITS ? 1 :
                            format == CBUILTIN_ATOMIC_RMW_SIGNED_BITS ? 2 :
                            format == CBUILTIN_ATOMIC_RMW_F32_BITS || format == CBUILTIN_ATOMIC_RMW_F64_BITS ? 3 : UINT32_MAX;
    if (rule != NULL && (bytes == 1 || bytes == 2 || bytes == 4 || bytes == 8) && layout != UINT32_MAX)
    {
        key = BUSTER_CHECK_PROGRAM_HELPER_FIXED + ((uint32_t)(rule - check_builtin_atomic_rmw_rules) * 4 + width) * 4 + layout;
    }
    else if (!rmw && (id == CBUILTIN_ATOMIC_LOAD_N || id == CBUILTIN_ATOMIC_STORE_N) &&
             (bytes == 1 || bytes == 2 || bytes == 4 || bytes == 8))
    {
        key = (id == CBUILTIN_ATOMIC_STORE_N ? 4 : 0) + width;
    }
    else if (!rmw && bytes == 0)
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

// What an atomic call's unsupported bits, or its object's alignment, lack, for the refusal.
static const char* check_program_helper_atomic_reason(const CheckBuiltinAtomicEffect* effect)
{
    const uint32_t bits = effect->unsupported;
    const char* reason = NULL;
    if ((bits & BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_WIDTH) != 0)
    {
        reason = "atomic objects of other than 1, 2, 4 or 8 bytes";
    }
    else if ((bits & BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_UNKNOWN_ALIGNMENT) != 0 || !effect->alignment_known)
    {
        reason = "an atomic object whose address's alignment this doesn't work out";
    }
    else if ((bits & BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_ALIGNMENT) != 0 || effect->minimum_alignment < effect->object_size)
    {
        reason = "a misaligned atomic object, which clang calls libatomic for";
    }
    else if ((bits & (BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_FLOAT_FORMAT | BUSTER_CHECK_BUILTIN_ATOMIC_RMW_UNSUPPORTED_FP_RMW)) != 0)
    {
        reason = "atomic arithmetic, minimum or maximum of floating values";
    }
    else if ((bits & BUSTER_CHECK_BUILTIN_ATOMIC_RMW_UNSUPPORTED_BOOL_RMW) != 0)
    {
        reason = "atomic arithmetic or NAND of bool";
    }
    else if ((bits & BUSTER_CHECK_BUILTIN_ATOMIC_RMW_UNSUPPORTED_SUBBYTE_INTEGER) != 0)
    {
        reason = "atomic read-modify-write of an integer narrower than a byte";
    }
    else if ((bits & BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_CONVERSION) != 0)
    {
        reason = "an atomic builtin's complex or extended floating value";
    }
    return reason;
}

// A read/modify/write's leaf (c_builtin_atomic_rmw_helpers.c), of its builtin, width and value format: integer and pointer
// bits, bool's 0 or 1, for min and max a signed integer's, and for GNU's exchange a float's or a double's (the lowering
// passes and takes their bits, check_lower_helper). A call passes the object's address and its fixed arguments
// (GNU's compare-exchange the expected object's address), the orders and GNU's weak flag as ints, and takes the old or new
// bits, or whether a compare-exchange stored (a legacy release nothing). A legacy call's protected variables aren't taken.
static CheckProgramStatus check_program_helper_rmw(const CheckProgram* program, const CheckBuiltinAtomicEffect* effect, const CheckBuiltinAtomicRmwRule* rule,
                                                    uint32_t arguments, CheckProgramHelper* helper, const char** reason)
{
    const TypeTable* types = program->types;
    CheckProgramStatus status = CHECK_PROGRAM_OK;
    const bool compare = rule->operation == CHECK_BUILTIN_ATOMIC_RMW_COMPARE_EXCHANGE;
    const bool ordered = rule->operation == CHECK_BUILTIN_ATOMIC_RMW_MIN || rule->operation == CHECK_BUILTIN_ATOMIC_RMW_MAX;
    const bool valid_type = types != NULL && type_is_valid(types, effect->value_type);
    uint32_t width = 0;
    bool is_signed = false;
    const bool integer = valid_type && check_action_integer_type(types, effect->value_type, &width, &is_signed);
    const bool pointer = valid_type && check_action_kind(types, effect->value_type) == BUSTER_TYPE_KIND_POINTER;
    const TypeId plain = valid_type ? type_unqualified(type_strip_aligned(types, effect->value_type)) : 0;
    const bool floating = rule->id == CBUILTIN_ATOMIC_EXCHANGE_N && (plain == BUSTER_TYPE_FLOAT || plain == BUSTER_TYPE_DOUBLE);
    // A legacy release is a store, no read/modify/write of its object.
    const bool release = rule->operation == CHECK_BUILTIN_ATOMIC_RMW_RELEASE;
    if (effect->runtime_builtin != rule->id || !effect->compiler_barrier || effect->rmw == release || !valid_type ||
        effect->evaluated_operands != (UINT32_C(1) << rule->fixed_count) - 1 || arguments == BUSTER_CHECK_NONE)
    {
        status = CHECK_PROGRAM_INVALID;
    }
    else if (arguments != rule->fixed_count)
    {
        status = CHECK_PROGRAM_UNSUPPORTED;
        *reason = "a legacy atomic builtin's protected variables";
    }
    else if (effect->unsupported != 0 || !effect->alignment_known || effect->minimum_alignment < effect->object_size)
    {
        status = CHECK_PROGRAM_UNSUPPORTED;
        *reason = check_program_helper_atomic_reason(effect);
    }
    else if (!integer && !pointer && !floating)
    {
        status = CHECK_PROGRAM_UNSUPPORTED;
        *reason = "atomic read-modify-write of floating values but GNU's exchange";
    }
    if (status == CHECK_PROGRAM_OK)
    {
        const bool boolean = plain == BUSTER_TYPE_BOOL;
        *helper = (CheckProgramHelper){
            .id = rule->id,
            .bytes = (uint32_t)effect->object_size,
            .parameter_count = rule->fixed_count,
            .result_type = release ? BUSTER_TYPE_VOID : BUSTER_TYPE_ULONG,
            .compiler_barrier = true,
            .required_alignment = (uint32_t)effect->object_size,
            .rmw_format = boolean ? CBUILTIN_ATOMIC_RMW_BOOL_BITS : plain == BUSTER_TYPE_FLOAT && floating ? CBUILTIN_ATOMIC_RMW_F32_BITS :
                          floating ? CBUILTIN_ATOMIC_RMW_F64_BITS : ordered && is_signed ? CBUILTIN_ATOMIC_RMW_SIGNED_BITS : CBUILTIN_ATOMIC_RMW_INTEGER_BITS,
            .rmw = true,
        };
        const uint32_t first_int = rule->legacy ? rule->fixed_count : compare ? 3 : 2;
        for (uint32_t i = 0; i < rule->fixed_count; i += 1)
        {
            helper->parameter_types[i] = i >= first_int ? BUSTER_TYPE_INT : BUSTER_TYPE_ULONG;
        }
    }
    return status;
}

static CheckProgramStatus check_program_helper_atomic(const CheckProgram* program, const CheckBuiltinAtomicEffect* effect, CBuiltinId id, uint32_t arguments,
                                                       CheckProgramHelper* helper, const char** reason)
{
    CheckProgramStatus status = CHECK_PROGRAM_OK;
    const CheckBuiltinAtomicRmwRule* rule = check_builtin_atomic_rmw_rule(id);
    const bool memory = id == CBUILTIN_ATOMIC_LOAD_N || id == CBUILTIN_ATOMIC_STORE_N;
    const bool load = id == CBUILTIN_ATOMIC_LOAD_N;
    const bool store = id == CBUILTIN_ATOMIC_STORE_N;
    const uint32_t parameters = load ? 2 : store ? 3 : id == CBUILTIN_SYNC_SYNCHRONIZE ? 0 : 1;
    if (rule != NULL)
    {
        status = check_program_helper_rmw(program, effect, rule, arguments, helper, reason);
    }
    else if (effect->runtime_builtin != id || !effect->compiler_barrier ||
             effect->evaluated_operands != (UINT32_C(1) << parameters) - 1 || effect->rmw)
    {
        status = CHECK_PROGRAM_INVALID;
    }
    else if (effect->unsupported != 0 || check_program_helper_key(id, effect->object_size, false, CBUILTIN_ATOMIC_RMW_INTEGER_BITS) == UINT32_MAX ||
             (memory && (!effect->alignment_known || effect->minimum_alignment < effect->object_size)))
    {
        status = CHECK_PROGRAM_UNSUPPORTED;
        *reason = check_program_helper_atomic_reason(effect);
    }
    else if ((memory && (effect->required_alignment != effect->object_size || effect->minimum_alignment == 0 ||
                         (effect->minimum_alignment & (effect->minimum_alignment - 1)) != 0)) ||
             (!memory && effect->required_alignment != 0))
    {
        status = CHECK_PROGRAM_INVALID;
    }
    if (rule == NULL && status == CHECK_PROGRAM_OK)
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
    uint32_t keys[BUSTER_CHECK_PROGRAM_HELPER_SIGNATURES] = { 0 };
    // At most a signature per builtin node, and per key.
    const uint32_t signatures = valid && program->builtin_count < BUSTER_CHECK_PROGRAM_HELPER_SIGNATURES ? program->builtin_count : BUSTER_CHECK_PROGRAM_HELPER_SIGNATURES;
    CheckProgramHelperUse* uses = valid && program->builtin_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckProgramHelperUse, program->builtin_count) : 0;
    CheckProgramHelper* planned = uses != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckProgramHelper, signatures) : 0;
    uint32_t* first_nodes = planned != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, signatures) : 0;
    if (valid && program->builtin_count != 0 && first_nodes == 0)
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
                        const uint32_t arguments = graph->argument_counts != 0 ? graph->argument_counts[node] : BUSTER_CHECK_NONE;
                        result->status = check_program_helper_atomic(program, stream->atomics + atomic, id, arguments, &helper, &result->reason);
                    }
                }
                if (result->status == CHECK_PROGRAM_OK && !folded)
                {
                    const uint32_t key = check_program_helper_key(helper.id, helper.bytes, helper.rmw, helper.rmw_format);
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
        else if (helper->rmw)
        {
            const CBuiltinAtomicRmwHelper native = cbuiltin_atomic_rmw_helper_emit(helper->id, helper->bytes, helper->rmw_format, output);
            helper->code = native.code;
            bool classes = native.parameter_count == helper->parameter_count &&
                           native.result_class == (helper->result_type == BUSTER_TYPE_VOID ? BUSTER_TYPED_CLASS_VOID : BUSTER_TYPED_CLASS_U64);
            for (uint32_t a = 0; a < helper->parameter_count && a < 6; a += 1)
            {
                classes &= native.parameter_classes[a] == (helper->parameter_types[a] == BUSTER_TYPE_INT ? BUSTER_TYPED_CLASS_I32 : BUSTER_TYPED_CLASS_U64);
            }
            if (native.code.error == TIER0_OK && !classes)
            {
                result->status = CHECK_PROGRAM_INVALID;
                result->token = program->input->graph.tokens[first_nodes[i]];
            }
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
