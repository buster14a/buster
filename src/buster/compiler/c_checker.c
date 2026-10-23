// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Typed postorder expressions (#133). The source adapter elaborates declarations and supplies this bounded,
// topologically ordered graph. Both schedulers retain node identities and explicit operand conversions. The
// vector scheduler derives dependency heights from operand edges, never from the parser's bracket depths.
// Include after c_types.c, c_check_actions.c and c_check_builtin.c. All storage belongs to caller-owned arenas;
// no padding is read.
#ifndef BUSTER_C_CHECKER_INCLUDED
#define BUSTER_C_CHECKER_INCLUDED

#define BUSTER_CHECK_NONE UINT32_MAX
#define BUSTER_CHECK_BATCH 16u
#define BUSTER_CHECK_CONTEXT_ICE 0x01u
#define BUSTER_CHECK_CONTEXT_ASSERT 0x02u
#define BUSTER_CHECK_CONTEXT_STATIC 0x04u
// A source-dialect extension may accept a foldable integer where a strict ICE is required. Keep this request
// separate from the value's strict ICE flag; discarded operands must not make a nonconstant evaluated path valid.
#define BUSTER_CHECK_CONTEXT_GNU_CONSTANT 0x08u

typedef struct CheckVarargsCall CheckVarargsCall;
struct CheckVarargsCall
{
    uint32_t node;
    uint32_t second_argument_declaration;
    CheckVarargsContext context;
};

typedef struct CheckBuiltinCall CheckBuiltinCall;
struct CheckBuiltinCall
{
    // Optional source witnesses, sorted by node. Absence means unknown; no pointer/type guess grants a witness.
    uint32_t node;
    const uint8_t* nan_payload;
    uint32_t nan_payload_length;
    bool nan_payload_known;
    bool string_literal_start;
    bool absolute_address_constant;
    bool object_size_known[4];
    uint64_t object_sizes[4];
    uint64_t minimum_alignment;
    bool alignment_known;
};

typedef struct CheckGraph CheckGraph;
struct CheckGraph
{
    // Every column has count logical entries. Operands are earlier nodes or BUSTER_CHECK_NONE. Arrays are not
    // padded; optional columns may be null. VALUE uses leaves[node]. Other nodes use types/member as action data.
    CheckOp* ops;
    uint32_t* tokens;
    uint32_t* operand0;
    uint32_t* operand1;
    uint32_t* operand2;
    CheckValue* leaves;
    TypeId* types;
    uint32_t* members;
    uint8_t* contexts;
    // Optional source-time tag history. Each node's cutoff is the exclusive end of its operand/type-name,
    // not the prefix operator token. A missing history preserves the direct action API's current-type view.
    const uint32_t* tag_completions;
    uint32_t tag_completion_count;
    uint32_t* source_positions;
    uint32_t extensions;
    uint32_t count;
    uint32_t capacity;
    // CALL's arguments are contiguous, disjoint slices appended to argument_nodes in call postorder, consuming
    // argument_count entries exactly; offsets/counts have count logical entries. Argument nodes are earlier than
    // their call. The callee is operand0; operand1 and operand2 are absent. Each argument owns its conversion slot.
    uint32_t* argument_offsets;
    uint32_t* argument_counts;
    uint32_t* argument_nodes;
    uint32_t argument_count;
    uint32_t argument_capacity;
    // VARARGS reuses argument_offsets as the index into this compact source-ordered context list and
    // argument_counts as the raw source count. It consumes no argument_nodes entries. The first two value
    // arguments use operand0/1; va_arg's type-name is types[node], so it has only operand0.
    CheckVarargsCall* varargs_calls;
    uint32_t varargs_count;
    uint32_t varargs_capacity;
    CheckBuiltinCall* builtin_calls;
    uint32_t builtin_count;
    uint32_t builtin_capacity;
};

typedef enum CheckStatus
{
    CHECK_STATUS_OK,
    CHECK_STATUS_INVALID_GRAPH,
    CHECK_STATUS_RESOURCE,
} CheckStatus;

typedef enum CheckMessage
{
    CHECK_MESSAGE_ACTION,
    CHECK_MESSAGE_CONSTANT,
    CHECK_MESSAGE_ICE,
    CHECK_MESSAGE_STATIC_ASSERT,
    CHECK_MESSAGE_STATIC_INITIALIZER,
} CheckMessage;

typedef struct CheckReport CheckReport;
struct CheckReport
{
    uint32_t node;
    uint32_t token;
    CheckMessage message;
    CheckDiagnostic diagnostic;
    CheckFault fault;
};

typedef struct CheckNodeConversions CheckNodeConversions;
struct CheckNodeConversions
{
    CheckConversion operands[3];
    // Compound updates retain the LHS read conversion above and a separate conversion back to its stored type.
    CheckConversion store;
};

typedef struct CheckStatistics CheckStatistics;
struct CheckStatistics
{
    uint32_t nodes;
    uint32_t levels;
    uint32_t batches;
    uint32_t scalar_nodes;
    uint32_t vector_nodes;
    uint64_t output_bytes;
    uint64_t scratch_bytes;
};

typedef struct CheckStoreEffect CheckStoreEffect;
struct CheckStoreEffect
{
    uint32_t node;
    CheckValue value;
    CheckValue destination;
};

typedef struct CheckVarargsEffect CheckVarargsEffect;
struct CheckVarargsEffect
{
    uint32_t node;
    TypeId requested_type;
    uint32_t evaluated_operands;
    uint32_t warnings;
    bool mutates_list;
};

typedef struct CheckBuiltinMiscEffect CheckBuiltinMiscEffect;
struct CheckBuiltinMiscEffect
{
    uint32_t node;
    CheckValue constant_value;
    uint32_t constant_evaluated_operands;
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

typedef struct CheckBuiltinFloatEffect CheckBuiltinFloatEffect;
struct CheckBuiltinFloatEffect
{
    uint32_t node;
    TypeId floating_type;
    uint32_t evaluated_operands;
    uint32_t definite_operands;
    uint32_t selected_selector;
    uint32_t unsupported;
    uint32_t warnings;
    CBuiltinId runtime_builtin;
    CheckBuiltinFloatEvaluation evaluation;
    bool folded;
    bool reads_memory;
};

typedef struct CheckBuiltinQueryEffect CheckBuiltinQueryEffect;
struct CheckBuiltinQueryEffect
{
    uint32_t node;
    uint32_t evaluated_operands;
    uint32_t warnings;
    uint32_t warning_operands;
    uint32_t mode;
    bool known;
    CheckBuiltinMiscConstraint constraint;
};

typedef struct CheckBuiltinAtomicEffect CheckBuiltinAtomicEffect;
struct CheckBuiltinAtomicEffect
{
    uint32_t node;
    CheckValue object_address;
    CheckValue store_value;
    TypeId object_type;
    TypeId value_type;
    uint64_t object_size;
    uint64_t required_alignment;
    uint64_t minimum_alignment;
    uint32_t evaluated_operands;
    uint32_t success_order;
    uint32_t failure_order;
    uint32_t known_orders;
    uint32_t warnings;
    uint32_t warning_operands;
    uint32_t unsupported;
    CBuiltinId runtime_builtin;
    CheckBuiltinAtomicFenceScope fence_scope;
    bool alignment_known;
    bool volatile_access;
    bool reads_memory;
    bool writes_memory;
    bool rmw;
    bool compiler_barrier;
};

typedef struct CheckStream CheckStream;
struct CheckStream
{
    // Node-indexed typed postorder output. The graph supplies operator, operand and source identity.
    CheckValue* values;
    CheckNodeConversions* conversions;
    CheckConversion* argument_conversions;
    CheckDiagnostic* diagnostics;
    uint32_t* warnings;
    CheckReport* reports;
    // Valid overflow stores in increasing graph-node order. This is a semantic descriptor stream: consumers
    // execute only effects belonging to reached expression nodes, never stores in an unselected branch.
    CheckStoreEffect* effects;
    CheckVarargsEffect* varargs;
    CheckBuiltinMiscEffect* misc;
    CheckBuiltinFloatEffect* floating;
    CheckBuiltinQueryEffect* queries;
    CheckBuiltinAtomicEffect* atomics;
    uint32_t count;
    uint32_t report_count;
    uint32_t effect_count;
    uint32_t varargs_count;
    uint32_t misc_count;
    uint32_t floating_count;
    uint32_t query_count;
    uint32_t atomic_count;
    CheckStatus status;
    uint32_t failed_node;
    CheckStatistics statistics;
};

typedef struct CheckOptions CheckOptions;
struct CheckOptions
{
    bool scalar;
    // 0 chooses BUSTER_CHECK_BATCH. Values 1..BUSTER_CHECK_BATCH permit differential partition testing.
    uint32_t batch_size;
};

#include "c_check_builtin_graph_metadata.c"

static bool check_graph_initialize(CheckGraph* graph, Arena* arena, uint32_t capacity, uint32_t argument_capacity);
static uint32_t check_graph_add(CheckGraph* graph, CheckOp op, uint32_t token, uint32_t operand0, uint32_t operand1, uint32_t operand2);
static bool check_graph_run(TypeTable* types, const CheckGraph* graph, Arena* output, Arena* scratch, const CheckOptions* options, CheckStream* stream);

static bool check_graph_initialize(CheckGraph* graph, Arena* arena, uint32_t capacity, uint32_t argument_capacity)
{
    const uint64_t start = arena->length;
    *graph = (CheckGraph){ 0 };
    bool result = capacity <= (1u << 30) && argument_capacity <= (1u << 30);
    if (result && capacity != 0)
    {
        graph->ops = BUSTER_ARENA_PUSH_ARRAY(arena, CheckOp, capacity);
        graph->tokens = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, capacity);
        graph->operand0 = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, capacity);
        graph->operand1 = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, capacity);
        graph->operand2 = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, capacity);
        graph->leaves = BUSTER_ARENA_PUSH_ARRAY(arena, CheckValue, capacity);
        graph->types = BUSTER_ARENA_PUSH_ARRAY(arena, TypeId, capacity);
        graph->members = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, capacity);
        graph->contexts = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, capacity);
        graph->source_positions = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, capacity);
        graph->argument_offsets = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, capacity);
        graph->argument_counts = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, capacity);
        result = graph->ops != 0 && graph->tokens != 0 && graph->operand0 != 0 && graph->operand1 != 0 && graph->operand2 != 0 && graph->leaves != 0 && graph->types != 0 && graph->members != 0 && graph->contexts != 0 && graph->source_positions != 0 && graph->argument_offsets != 0 && graph->argument_counts != 0;
    }
    if (result && argument_capacity != 0)
    {
        graph->argument_nodes = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, argument_capacity);
        result = graph->argument_nodes != 0;
    }
    if (result)
    {
        graph->capacity = capacity;
        graph->argument_capacity = argument_capacity;
    }
    else
    {
        arena_pop_to(arena, start);
        *graph = (CheckGraph){ 0 };
    }
    return result;
}

static uint32_t check_graph_add(CheckGraph* graph, CheckOp op, uint32_t token, uint32_t operand0, uint32_t operand1, uint32_t operand2)
{
    uint32_t result = BUSTER_CHECK_NONE;
    const uint32_t node = graph->count;
    if (node < graph->capacity && op < CHECK_OP_COUNT && (operand0 == BUSTER_CHECK_NONE || operand0 < node) && (operand1 == BUSTER_CHECK_NONE || operand1 < node) && (operand2 == BUSTER_CHECK_NONE || operand2 < node))
    {
        graph->ops[node] = op;
        graph->tokens[node] = token;
        graph->operand0[node] = operand0;
        graph->operand1[node] = operand1;
        graph->operand2[node] = operand2;
        graph->leaves[node] = (CheckValue){ 0 };
        graph->types[node] = 0;
        graph->members[node] = 0;
        graph->contexts[node] = 0;
        if (graph->source_positions != 0)
        {
            graph->source_positions[node] = BUSTER_CHECK_NONE;
        }
        graph->argument_offsets[node] = 0;
        graph->argument_counts[node] = 0;
        graph->count += 1;
        result = node;
    }
    return result;
}

static bool check_graph_has_arguments(CheckOp op)
{
    return op == CHECK_OP_CALL || op == CHECK_OP_BUILTIN_MISC || op == CHECK_OP_BUILTIN_FLOAT || op == CHECK_OP_BUILTIN_QUERY || op == CHECK_OP_BUILTIN_ATOMIC;
}

static __attribute__((unused)) CheckOp check_graph_builtin_op(CBuiltinId id)
{
    CheckOp op = CHECK_OP_COUNT;
    if ((id >= CBUILTIN_EXPECT && id <= CBUILTIN_PREFETCH) || (id >= CBUILTIN_MEMCPY && id <= CBUILTIN_ALLOCA) ||
        id == CBUILTIN_FRAME_ADDRESS || id == CBUILTIN_RETURN_ADDRESS || id == CBUILTIN_EXTRACT_RETURN_ADDR || id == CBUILTIN_FROB_RETURN_ADDR)
    {
        op = CHECK_OP_BUILTIN_MISC;
    }
    else if (id >= CBUILTIN_HUGE_VAL && id <= CBUILTIN_FPCLASSIFY)
    {
        op = CHECK_OP_BUILTIN_FLOAT;
    }
    else if (id == CBUILTIN_CONSTANT_P || id == CBUILTIN_OBJECT_SIZE)
    {
        op = CHECK_OP_BUILTIN_QUERY;
    }
    else if (id == CBUILTIN_ATOMIC_LOAD_N || id == CBUILTIN_ATOMIC_STORE_N || id == CBUILTIN_SYNC_SYNCHRONIZE ||
             id == CBUILTIN_ATOMIC_THREAD_FENCE || id == CBUILTIN_ATOMIC_SIGNAL_FENCE)
    {
        op = CHECK_OP_BUILTIN_ATOMIC;
    }
    return op;
}

static uint32_t check_graph_arity(CheckOp op, TypeId type)
{
    uint32_t result = 1;
    if (op == CHECK_OP_VALUE || op == CHECK_OP_VLA_EXTENT || (check_graph_has_arguments(op) && op != CHECK_OP_CALL) ||
        ((op == CHECK_OP_SIZEOF || op == CHECK_OP_ALIGNOF) && type != 0))
    {
        result = 0;
    }
    else if (op == CHECK_OP_CONDITIONAL || op == CHECK_OP_BUILTIN_OVERFLOW)
    {
        result = 3;
    }
    else if ((op >= CHECK_OP_ADD && op <= CHECK_OP_LOGICAL_OR) || (op >= CHECK_OP_ADD_ASSIGN && op <= CHECK_OP_BIT_XOR_ASSIGN) || op == CHECK_OP_ASSIGN || op == CHECK_OP_COMMA || op == CHECK_OP_SUBSCRIPT)
    {
        result = 2;
    }
    return result;
}

static bool check_graph_builtin(TypeId type)
{
    const uint32_t index = type_index(type);
    return index >= BUSTER_TYPE_CODE_BOOL && index <= BUSTER_TYPE_CODE_LONG_DOUBLE;
}

// Only regions known to use builtin arithmetic types can be reordered. An address, decay, composite, or another
// action that can intern a type is a source-order barrier. This preserves identical TypeIds in fresh tables, not
// merely in a table already populated by the scalar oracle. A non-builtin value conservatively ends a region.
static bool check_graph_pure(const CheckGraph* graph, uint32_t node, const uint8_t* pure)
{
    const CheckOp op = graph->ops[node];
    const TypeId requested = graph->types != 0 ? graph->types[node] : 0;
    bool result = false;
    if (op == CHECK_OP_VALUE)
    {
        result = graph->leaves != 0 && check_graph_builtin(graph->leaves[node].type);
    }
    else if (op == CHECK_OP_SIZEOF || op == CHECK_OP_ALIGNOF)
    {
        result = true;
    }
    else if ((op >= CHECK_OP_POSITIVE && op <= CHECK_OP_CONDITIONAL) || (op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_BIT_XOR_ASSIGN) || op == CHECK_OP_COMMA || op == CHECK_OP_ASSIGN || op == CHECK_OP_CONDITION || op == CHECK_OP_CAST || op == CHECK_OP_INITIALIZE || op == CHECK_OP_CONVERT || op == CHECK_OP_SELECT)
    {
        result = op != CHECK_OP_CAST && op != CHECK_OP_INITIALIZE && op != CHECK_OP_CONVERT;
        result = result || check_graph_builtin(requested) || requested == BUSTER_TYPE_VOID;
        const uint32_t operands[] =
        {
            graph->operand0[node],
            graph->operand1[node],
            graph->operand2[node],
        };
        for (uint32_t i = 0; i < 3; i += 1)
        {
            if (operands[i] != BUSTER_CHECK_NONE)
            {
                result = result && pure[operands[i]] != 0;
            }
        }
    }
    return result;
}

static bool check_graph_validate(const CheckGraph* graph, uint8_t* pure, uint32_t* maximum_arguments, uint32_t* maximum_effects, uint32_t builtins[4], uint32_t* failed)
{
    bool result = graph->count <= graph->capacity && graph->count <= (1u << 30) && graph->argument_count <= graph->argument_capacity && graph->argument_count <= (1u << 30);
    result = result && graph->varargs_count <= graph->varargs_capacity && graph->varargs_count <= graph->count &&
             (graph->varargs_count == 0 || graph->varargs_calls != NULL);
    result = result && graph->builtin_count <= graph->builtin_capacity && graph->builtin_count <= graph->count &&
             (graph->builtin_count == 0 || graph->builtin_calls != NULL);
    result = result && (graph->tag_completions != 0 ? graph->source_positions != 0 : graph->tag_completion_count == 0);
    result = result && (graph->extensions & ~BUSTER_CHECK_EXTENSION_GNU) == 0;
    *maximum_arguments = 0;
    *maximum_effects = 0;
    memset(builtins, 0, 4 * sizeof(*builtins));
    *failed = BUSTER_CHECK_NONE;
    uint32_t consumed_arguments = 0;
    uint32_t consumed_varargs = 0;
    uint32_t consumed_builtins = 0;
    if (graph->count != 0)
    {
        result = result && graph->ops != 0 && graph->operand0 != 0 && graph->operand1 != 0 && graph->operand2 != 0;
    }
    if (graph->argument_count != 0)
    {
        result = result && graph->argument_nodes != 0;
    }
    for (uint32_t node = 0; result && node < graph->count; node += 1)
    {
        const CheckOp op = graph->ops[node];
        const TypeId requested = graph->types != 0 ? graph->types[node] : 0;
        const uint32_t offset = graph->argument_offsets != 0 ? graph->argument_offsets[node] : 0;
        const uint32_t count = graph->argument_counts != 0 ? graph->argument_counts[node] : 0;
        const bool varargs = op == CHECK_OP_VARARGS;
        const bool type_argument = varargs && graph->members != NULL && graph->members[node] == CBUILTIN_VA_ARG;
        const uint32_t arity = varargs ? (type_argument ? count != 0 : count < 2 ? count : 2) : check_graph_arity(op, requested);
        const uint32_t operands[] =
        {
            graph->operand0[node],
            graph->operand1[node],
            graph->operand2[node],
        };
        result = op >= CHECK_OP_VALUE && op < CHECK_OP_COUNT;
        result = result && (graph->tag_completions == 0 || graph->source_positions[node] != BUSTER_CHECK_NONE);
        for (uint32_t i = 0; i < 3; i += 1)
        {
            result = result && (i < arity ? operands[i] < node : operands[i] == BUSTER_CHECK_NONE);
        }
        result = result && (op != CHECK_OP_VALUE || graph->leaves != 0);
        result = result && (op != CHECK_OP_STATEMENT_VALUE || graph->members == 0 || (graph->members[node] & ~BUSTER_CHECK_SIDE_EFFECT) == 0);
        result = result && ((op != CHECK_OP_VLA_BOUND && op != CHECK_OP_VLA_EXTENT) || (graph->members != NULL && graph->members[node] != 0));
        result = result && (graph->contexts == 0 || (graph->contexts[node] & ~(BUSTER_CHECK_CONTEXT_ICE | BUSTER_CHECK_CONTEXT_ASSERT | BUSTER_CHECK_CONTEXT_STATIC | BUSTER_CHECK_CONTEXT_GNU_CONSTANT)) == 0);
        if (varargs)
        {
            result = result && graph->argument_offsets != NULL && graph->argument_counts != NULL && graph->members != NULL &&
                     offset == consumed_varargs && offset < graph->varargs_count && graph->varargs_calls[offset].node == node;
        }
        else
        {
            result = result && offset <= graph->argument_count && count <= graph->argument_count - offset && (count == 0 || check_graph_has_arguments(op));
            result = result && (count == 0 || offset == consumed_arguments);
            for (uint32_t i = 0; result && i < count; i += 1)
            {
                result = graph->argument_nodes[offset + i] < node;
            }
        }
        if (result && consumed_builtins < graph->builtin_count && graph->builtin_calls[consumed_builtins].node <= node)
        {
            result = graph->builtin_calls[consumed_builtins].node == node &&
                     (op == CHECK_OP_BUILTIN_FLOAT || op == CHECK_OP_BUILTIN_QUERY || op == CHECK_OP_BUILTIN_ATOMIC);
            consumed_builtins += 1;
        }
        if (result)
        {
            pure[node] = (uint8_t)check_graph_pure(graph, node, pure);
            *maximum_arguments = !varargs && count > *maximum_arguments ? count : *maximum_arguments;
            *maximum_effects += op == CHECK_OP_BUILTIN_OVERFLOW;
            consumed_arguments += varargs ? 0 : count;
            consumed_varargs += varargs;
            builtins[0] += op == CHECK_OP_BUILTIN_MISC;
            builtins[1] += op == CHECK_OP_BUILTIN_FLOAT;
            builtins[2] += op == CHECK_OP_BUILTIN_QUERY;
            builtins[3] += op == CHECK_OP_BUILTIN_ATOMIC;
        }
        else
        {
            *failed = node;
        }
    }
    result = result && consumed_arguments == graph->argument_count && consumed_varargs == graph->varargs_count && consumed_builtins == graph->builtin_count;
    return result;
}

static void check_graph_action(const CheckGraph* graph, const CheckStream* stream, uint32_t node, CheckValue* arguments, CheckAction* action)
{
    *action = (CheckAction){
        .op = graph->ops[node],
        .type = graph->types != 0 ? graph->types[node] : 0,
        .member = graph->members != 0 ? graph->members[node] : 0,
        .node = node,
        .tag_completions = graph->tag_completions,
        .tag_completion_count = graph->tag_completion_count,
        .source_position = graph->source_positions != 0 ? graph->source_positions[node] : BUSTER_CHECK_NONE,
        .extensions = graph->extensions,
    };
    const uint32_t operands[] =
    {
        graph->operand0[node],
        graph->operand1[node],
        graph->operand2[node],
    };
    for (uint32_t i = 0; i < 3; i += 1)
    {
        if (operands[i] != BUSTER_CHECK_NONE)
        {
            action->operands[i] = stream->values[operands[i]];
        }
    }
    if (action->op == CHECK_OP_VALUE)
    {
        action->operands[0] = graph->leaves[node];
    }
    if (check_graph_has_arguments(action->op) && graph->argument_counts != 0)
    {
        const uint32_t offset = graph->argument_offsets != 0 ? graph->argument_offsets[node] : 0;
        action->argument_count = graph->argument_counts[node];
        for (uint32_t i = 0; i < action->argument_count; i += 1)
        {
            arguments[i] = stream->values[graph->argument_nodes[offset + i]];
        }
        action->arguments = arguments;
        action->argument_conversions = action->argument_count != 0 ? stream->argument_conversions + offset : 0;
    }
}

static void check_graph_store(CheckStream* stream, uint32_t node, const CheckActionResult* result)
{
    stream->values[node] = result->value;
    memcpy(stream->conversions[node].operands, result->conversions, sizeof(result->conversions));
    stream->conversions[node].store = result->store_conversion;
    stream->diagnostics[node] = result->diagnostic;
    stream->warnings[node] = result->warnings;
}

static const CheckBuiltinCall* check_graph_builtin_call(const CheckGraph* graph, uint32_t node)
{
    uint32_t first = 0;
    uint32_t last = graph->builtin_count;
    while (first < last)
    {
        const uint32_t middle = first + (last - first) / 2;
        if (graph->builtin_calls[middle].node < node)
        {
            first = middle + 1;
        }
        else
        {
            last = middle;
        }
    }
    return first < graph->builtin_count && graph->builtin_calls[first].node == node ? graph->builtin_calls + first : NULL;
}

static void check_graph_special_action(TypeTable* types, const CheckGraph* graph, CheckStream* stream, const CheckAction* action, CheckActionResult* result)
{
    const uint32_t node = action->node;
    const CheckBuiltinCall* witness = check_graph_builtin_call(graph, node);
    if (action->op == CHECK_OP_BUILTIN_MISC)
    {
        const CheckBuiltinMiscAction input = {
            .id = (CBuiltinId)action->member,
            .arguments = action->arguments,
            .argument_count = action->argument_count,
            .node = node,
            .tag_completions = action->tag_completions,
            .tag_completion_count = action->tag_completion_count,
            .source_position = action->source_position,
            .extensions = action->extensions,
        };
        CheckBuiltinMiscResult checked;
        check_builtin_misc(types, &input, &checked, 1);
        *result = checked.action;
        for (uint32_t i = 0; i < action->argument_count && i < 3; i += 1)
        {
            action->argument_conversions[i] = checked.argument_conversions[i];
        }
        stream->misc[stream->misc_count++] = (CheckBuiltinMiscEffect){
            .node = node,
            .constant_value = checked.constant_value,
            .constant_evaluated_operands = checked.constant_evaluated_operands,
            .evaluated_operands = checked.evaluated_operands,
            .warnings = checked.warnings,
            .unsupported = checked.unsupported,
            .frame_level = checked.frame_level,
            .alignment = checked.alignment,
            .alignment_offset = checked.alignment_offset,
            .runtime_builtin = checked.runtime_builtin,
            .constraint = checked.constraint,
            .warning_operands = checked.warning_operands,
            .prefetch_rw = checked.prefetch_rw,
            .prefetch_locality = checked.prefetch_locality,
            .terminates = checked.terminates,
            .reads_memory = checked.reads_memory,
            .writes_memory = checked.writes_memory,
            .allocates_stack = checked.allocates_stack,
            .alignment_offset_known = checked.alignment_offset_known,
        };
    }
    else if (action->op == CHECK_OP_BUILTIN_FLOAT)
    {
        const CheckBuiltinFloatAction input = {
            .id = (CBuiltinId)action->member,
            .arguments = action->arguments,
            .argument_count = action->argument_count,
            .node = node,
            .tag_completions = action->tag_completions,
            .tag_completion_count = action->tag_completion_count,
            .source_position = action->source_position,
            .extensions = action->extensions,
            .nan_payload = witness != NULL ? witness->nan_payload : NULL,
            .nan_payload_length = witness != NULL ? witness->nan_payload_length : 0,
            .nan_payload_known = witness != NULL && witness->nan_payload_known,
        };
        CheckBuiltinFloatResult checked;
        check_builtin_float(types, &input, &checked, 1);
        *result = checked.action;
        for (uint32_t i = 0; i < action->argument_count && i < 6; i += 1)
        {
            action->argument_conversions[i] = checked.argument_conversions[i];
        }
        stream->floating[stream->floating_count++] = (CheckBuiltinFloatEffect){
            .node = node,
            .floating_type = checked.floating_type,
            .evaluated_operands = checked.evaluated_operands,
            .definite_operands = checked.definite_operands,
            .selected_selector = checked.selected_selector,
            .unsupported = checked.unsupported,
            .warnings = checked.warnings,
            .runtime_builtin = checked.runtime_builtin,
            .evaluation = checked.evaluation,
            .folded = checked.folded,
            .reads_memory = checked.reads_memory,
        };
    }
    else if (action->op == CHECK_OP_BUILTIN_ATOMIC)
    {
        const CheckBuiltinAtomicAction input = {
            .id = (CBuiltinId)action->member,
            .arguments = action->arguments,
            .argument_count = action->argument_count,
            .node = node,
            .tag_completions = action->tag_completions,
            .tag_completion_count = action->tag_completion_count,
            .source_position = action->source_position,
            .extensions = action->extensions,
            .minimum_alignment = witness != NULL ? witness->minimum_alignment : 0,
            .alignment_known = witness != NULL && witness->alignment_known,
        };
        CheckBuiltinAtomicResult checked;
        check_builtin_atomic(types, &input, &checked, 1);
        *result = checked.action;
        for (uint32_t i = 0; i < action->argument_count && i < 6; i += 1)
        {
            action->argument_conversions[i] = checked.argument_conversions[i];
        }
        stream->atomics[stream->atomic_count++] = (CheckBuiltinAtomicEffect){
            .node = node,
            .object_address = checked.object_address,
            .store_value = checked.store_value,
            .object_type = checked.object_type,
            .value_type = checked.value_type,
            .object_size = checked.object_size,
            .required_alignment = checked.required_alignment,
            .minimum_alignment = checked.minimum_alignment,
            .evaluated_operands = checked.evaluated_operands,
            .success_order = checked.success_order,
            .failure_order = checked.failure_order,
            .known_orders = checked.known_orders,
            .warnings = checked.warnings,
            .warning_operands = checked.warning_operands,
            .unsupported = checked.unsupported,
            .runtime_builtin = checked.runtime_builtin,
            .fence_scope = checked.fence_scope,
            .alignment_known = checked.alignment_known,
            .volatile_access = checked.volatile_access,
            .reads_memory = checked.reads_memory,
            .writes_memory = checked.writes_memory,
            .rmw = checked.rmw,
            .compiler_barrier = checked.compiler_barrier,
        };
    }
    else
    {
        CheckBuiltinQueryAction input = {
            .id = (CBuiltinId)action->member,
            .arguments = action->arguments,
            .argument_count = action->argument_count,
            .node = node,
            .tag_completions = action->tag_completions,
            .tag_completion_count = action->tag_completion_count,
            .source_position = action->source_position,
            .extensions = action->extensions,
            .string_literal_start = witness != NULL && witness->string_literal_start,
            .absolute_address_constant = witness != NULL && witness->absolute_address_constant,
        };
        if (witness != NULL)
        {
            memcpy(input.object_size_known, witness->object_size_known, sizeof(input.object_size_known));
            memcpy(input.object_sizes, witness->object_sizes, sizeof(input.object_sizes));
        }
        CheckBuiltinQueryResult checked;
        check_builtin_query(types, &input, &checked, 1);
        *result = checked.action;
        for (uint32_t i = 0; i < action->argument_count && i < 2; i += 1)
        {
            action->argument_conversions[i] = checked.argument_conversions[i];
        }
        stream->queries[stream->query_count++] = (CheckBuiltinQueryEffect){
            .node = node,
            .evaluated_operands = checked.evaluated_operands,
            .warnings = checked.warnings,
            .warning_operands = checked.warning_operands,
            .mode = checked.mode,
            .known = checked.known,
            .constraint = checked.constraint,
        };
    }
}

static void check_graph_scalar_node(TypeTable* types, const CheckGraph* graph, CheckStream* stream, uint32_t node, CheckValue* arguments)
{
    CheckAction action;
    CheckActionResult result;
    check_graph_action(graph, stream, node, arguments, &action);
    if (action.op == CHECK_OP_BUILTIN_UNARY || action.op == CHECK_OP_BUILTIN_OVERFLOW)
    {
        const uint32_t count = action.op == CHECK_OP_BUILTIN_UNARY ? 1 : 3;
        const CheckBuiltinAction builtin = {
            .id = (CBuiltinId)action.member,
            .operands = { action.operands[0], action.operands[1], action.operands[2] },
            .argument_count = count,
            .node = node,
            .tag_completions = action.tag_completions,
            .tag_completion_count = action.tag_completion_count,
            .source_position = action.source_position,
            .extensions = action.extensions,
        };
        CheckBuiltinResult checked;
        check_builtins(types, &builtin, &checked, 1, true);
        result = checked.action;
        if (checked.store && result.diagnostic == CHECK_DIAG_NONE)
        {
            stream->effects[stream->effect_count++] = (CheckStoreEffect){
                .node = node,
                .value = checked.store_value,
                .destination = checked.store_destination,
            };
        }
    }
    else if (action.op == CHECK_OP_VARARGS)
    {
        const CheckVarargsCall* call = &graph->varargs_calls[graph->argument_offsets[node]];
        const CheckVarargsAction builtin = {
            .id = (CBuiltinId)action.member,
            .operands = { action.operands[0], action.operands[1] },
            .argument_count = graph->argument_counts[node],
            .requested_type = action.type,
            .second_argument_declaration = call->second_argument_declaration,
            .node = node,
            .tag_completions = action.tag_completions,
            .tag_completion_count = action.tag_completion_count,
            .source_position = action.source_position,
            .extensions = action.extensions,
        };
        CheckVarargsResult checked;
        check_varargs(types, &call->context, &builtin, &checked, 1);
        result = checked.action;
        stream->varargs[stream->varargs_count++] = (CheckVarargsEffect){
            .node = node,
            .requested_type = checked.requested_type,
            .evaluated_operands = checked.evaluated_operands,
            .warnings = checked.warnings,
            .mutates_list = checked.mutates_list,
        };
    }
    else if (check_graph_has_arguments(action.op) && action.op != CHECK_OP_CALL)
    {
        check_graph_special_action(types, graph, stream, &action, &result);
    }
    else
    {
        check_action_scalar(types, &action, &result);
    }
    check_graph_store(stream, node, &result);
    stream->statistics.scalar_nodes += 1;
}

// Count/place/fill a stable level order inside one mutation-free postorder region. Dependencies before begin are
// already complete. Relative heights keep all histogram clears together linear even for many tiny regions.
static void check_graph_region(TypeTable* types, const CheckGraph* graph, CheckStream* stream, uint32_t begin, uint32_t end, uint32_t batch_size, uint32_t* levels, uint32_t* counts, uint32_t* order)
{
    uint32_t maximum = 0;
    for (uint32_t node = begin; node < end; node += 1)
    {
        uint32_t level = 0;
        const uint32_t operands[] =
        {
            graph->operand0[node],
            graph->operand1[node],
            graph->operand2[node],
        };
        for (uint32_t i = 0; i < 3; i += 1)
        {
            const uint32_t operand = operands[i];
            if (operand != BUSTER_CHECK_NONE && operand >= begin)
            {
                const uint32_t candidate = levels[operand] + 1;
                level = candidate > level ? candidate : level;
            }
        }
        levels[node] = level;
        maximum = level > maximum ? level : maximum;
    }
    memset(counts, 0, (size_t)(maximum + 2) * sizeof(*counts));
    for (uint32_t node = begin; node < end; node += 1)
    {
        counts[levels[node] + 1] += 1;
    }
    for (uint32_t level = 1; level <= maximum + 1; level += 1)
    {
        counts[level] += counts[level - 1];
    }
    // Fill backwards, decrementing ends, so the resulting starts remain available and each level stays stable.
    for (uint32_t node = end; node != begin;)
    {
        node -= 1;
        const uint32_t level = levels[node];
        counts[level + 1] -= 1;
        order[counts[level + 1]] = node;
    }
    for (uint32_t level = 0; level <= maximum; level += 1)
    {
        const uint32_t first = counts[level + 1];
        const uint32_t last = level == maximum ? end - begin : counts[level + 2];
        for (uint32_t at = first; at < last;)
        {
            const uint32_t count = last - at < batch_size ? last - at : batch_size;
            if (count == 1)
            {
                // A singleton has no parallel work. Avoid batch packing and wide table lookups on chains.
                check_graph_scalar_node(types, graph, stream, order[at], 0);
            }
            else
            {
                CheckAction actions[BUSTER_CHECK_BATCH];
                CheckActionResult results[BUSTER_CHECK_BATCH];
                for (uint32_t i = 0; i < count; i += 1)
                {
                    check_graph_action(graph, stream, order[at + i], 0, &actions[i]);
                    const bool vector = check_action_vector_eligible(types, &actions[i]);
                    stream->statistics.vector_nodes += (uint32_t)vector;
                    stream->statistics.scalar_nodes += (uint32_t)!vector;
                }
                check_actions(types, actions, results, count);
                for (uint32_t i = 0; i < count; i += 1)
                {
                    check_graph_store(stream, order[at + i], &results[i]);
                }
            }
            at += count;
            stream->statistics.batches += 1;
        }
    }
    stream->statistics.levels += maximum + 1;
}

static void check_graph_report(const CheckGraph* graph, CheckStream* stream, uint32_t node, CheckMessage message, CheckDiagnostic diagnostic, CheckFault fault)
{
    stream->reports[stream->report_count] = (CheckReport){
        .node = node,
        .token = graph->tokens != 0 ? graph->tokens[node] : node,
        .message = message,
        .diagnostic = diagnostic,
        .fault = fault,
    };
    stream->report_count += 1;
}

static bool check_graph_inherited_error(const CheckGraph* graph, const CheckStream* stream, uint32_t node)
{
    bool result = false;
    const uint32_t operands[] =
    {
        graph->operand0[node],
        graph->operand1[node],
        graph->operand2[node],
    };
    for (uint32_t i = 0; i < 3; i += 1)
    {
        const uint32_t operand = operands[i];
        result |= operand != BUSTER_CHECK_NONE && stream->diagnostics[operand] != CHECK_DIAG_NONE && stream->values[operand].type == 0;
    }
    if (check_graph_has_arguments(graph->ops[node]) && graph->argument_counts != 0)
    {
        const uint32_t offset = graph->argument_offsets != 0 ? graph->argument_offsets[node] : 0;
        const uint32_t count = graph->argument_counts[node];
        for (uint32_t i = 0; i < count; i += 1)
        {
            const uint32_t operand = graph->argument_nodes[offset + i];
            result |= stream->diagnostics[operand] != CHECK_DIAG_NONE && stream->values[operand].type == 0;
        }
    }
    return result;
}

static void check_graph_reports(const CheckGraph* graph, CheckStream* stream, CheckReport* temporary)
{
    for (uint32_t node = 0; node < graph->count; node += 1)
    {
        const CheckValue* value = stream->values + node;
        const uint8_t context = graph->contexts != 0 ? graph->contexts[node] : 0;
        const bool folded_integer = (context & BUSTER_CHECK_CONTEXT_GNU_CONSTANT) != 0 &&
                                    value->constant_kind == CHECK_CONSTANT_INTEGER && (value->flags & BUSTER_CHECK_SIDE_EFFECT) == 0;
        if (stream->diagnostics[node] != CHECK_DIAG_NONE)
        {
            // Poisoned operands already have a primary error. Reporting their parents' invalid-type cascades
            // would move the first error to an earlier prefix/return token instead of the offending expression.
            if (stream->diagnostics[node] != CHECK_DIAG_INVALID_TYPE || !check_graph_inherited_error(graph, stream, node))
            {
                check_graph_report(graph, stream, node, CHECK_MESSAGE_ACTION, stream->diagnostics[node], CHECK_FAULT_NONE);
            }
        }
        else if (context != 0 && value->type != 0)
        {
            if (value->fault != CHECK_FAULT_NONE)
            {
                const uint32_t source = value->fault_node < graph->count ? value->fault_node : node;
                check_graph_report(graph, stream, source, CHECK_MESSAGE_CONSTANT, CHECK_DIAG_NONE, value->fault);
            }
            else if ((context & (BUSTER_CHECK_CONTEXT_ICE | BUSTER_CHECK_CONTEXT_ASSERT)) != 0 && !folded_integer && ((value->flags & BUSTER_CHECK_ICE) == 0 || value->constant_kind != CHECK_CONSTANT_INTEGER))
            {
                check_graph_report(graph, stream, node, CHECK_MESSAGE_ICE, CHECK_DIAG_NONE, CHECK_FAULT_NONE);
            }
            else if ((context & BUSTER_CHECK_CONTEXT_ASSERT) != 0 && value->integer == 0)
            {
                check_graph_report(graph, stream, node, CHECK_MESSAGE_STATIC_ASSERT, CHECK_DIAG_NONE, CHECK_FAULT_NONE);
            }
            else if ((context & BUSTER_CHECK_CONTEXT_STATIC) != 0 && (value->constant_kind == CHECK_CONSTANT_NONE || (value->flags & BUSTER_CHECK_SIDE_EFFECT) != 0))
            {
                check_graph_report(graph, stream, node, CHECK_MESSAGE_STATIC_INITIALIZER, CHECK_DIAG_NONE, CHECK_FAULT_NONE);
            }
        }
    }
    // Diagnostics are cold. Four stable radix passes restore source-token order without callbacks or recursion.
    CheckReport* source = stream->reports;
    CheckReport* destination = temporary;
    for (uint32_t shift = 0; stream->report_count > 1 && shift < 32; shift += 8)
    {
        uint32_t counts[256] = { 0 };
        for (uint32_t i = 0; i < stream->report_count; i += 1)
        {
            counts[(source[i].token >> shift) & 255] += 1;
        }
        uint32_t total = 0;
        for (uint32_t bucket = 0; bucket < 256; bucket += 1)
        {
            const uint32_t count = counts[bucket];
            counts[bucket] = total;
            total += count;
        }
        for (uint32_t i = 0; i < stream->report_count; i += 1)
        {
            const uint32_t bucket = (source[i].token >> shift) & 255;
            destination[counts[bucket]] = source[i];
            counts[bucket] += 1;
        }
        CheckReport* swap = source;
        source = destination;
        destination = swap;
    }
}

static bool check_graph_run(TypeTable* types, const CheckGraph* graph, Arena* output, Arena* scratch, const CheckOptions* options, CheckStream* stream)
{
    const uint64_t output_start = output->length;
    const uint64_t scratch_start = scratch->length;
    *stream = (CheckStream){
        .failed_node = BUSTER_CHECK_NONE,
    };
    const uint32_t count = graph->count;
    const uint32_t batch_size = options->batch_size == 0 ? BUSTER_CHECK_BATCH : options->batch_size;
    bool result = output != scratch && count <= graph->capacity && count <= (1u << 30) && batch_size <= BUSTER_CHECK_BATCH;
    stream->status = result ? CHECK_STATUS_OK : CHECK_STATUS_INVALID_GRAPH;
    uint8_t* pure = result && count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, count) : 0;
    uint32_t* levels = result && count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, count) : 0;
    uint32_t* counts = result && count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 2) : 0;
    uint32_t* order = result && count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, count) : 0;
    CheckReport* reports = result && count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckReport, count) : 0;
    if (result && count != 0 && (pure == 0 || levels == 0 || counts == 0 || order == 0 || reports == 0))
    {
        result = false;
        stream->status = CHECK_STATUS_RESOURCE;
    }
    uint32_t maximum_arguments = 0;
    uint32_t maximum_effects = 0;
    uint32_t maximum_builtins[4];
    if (result)
    {
        result = check_graph_validate(graph, pure, &maximum_arguments, &maximum_effects, maximum_builtins, &stream->failed_node);
        stream->status = result ? CHECK_STATUS_OK : CHECK_STATUS_INVALID_GRAPH;
    }
    CheckValue* arguments = result && maximum_arguments != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckValue, maximum_arguments) : 0;
    if (result && count != 0)
    {
        stream->values = BUSTER_ARENA_PUSH_ARRAY(output, CheckValue, count);
        stream->conversions = BUSTER_ARENA_PUSH_ARRAY(output, CheckNodeConversions, count);
        stream->diagnostics = BUSTER_ARENA_PUSH_ARRAY(output, CheckDiagnostic, count);
        stream->warnings = BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, count);
        stream->reports = BUSTER_ARENA_PUSH_ARRAY(output, CheckReport, count);
        stream->argument_conversions = graph->argument_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckConversion, graph->argument_count) : 0;
        stream->effects = maximum_effects != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckStoreEffect, maximum_effects) : 0;
        stream->varargs = graph->varargs_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckVarargsEffect, graph->varargs_count) : 0;
        stream->misc = maximum_builtins[0] != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckBuiltinMiscEffect, maximum_builtins[0]) : 0;
        stream->floating = maximum_builtins[1] != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckBuiltinFloatEffect, maximum_builtins[1]) : 0;
        stream->queries = maximum_builtins[2] != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckBuiltinQueryEffect, maximum_builtins[2]) : 0;
        stream->atomics = maximum_builtins[3] != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckBuiltinAtomicEffect, maximum_builtins[3]) : 0;
        result = stream->values != 0 && stream->conversions != 0 && stream->diagnostics != 0 && stream->warnings != 0 && stream->reports != 0 &&
                 (maximum_arguments == 0 || arguments != 0) && (graph->argument_count == 0 || stream->argument_conversions != 0) &&
                 (maximum_effects == 0 || stream->effects != 0) && (graph->varargs_count == 0 || stream->varargs != 0) &&
                 (maximum_builtins[0] == 0 || stream->misc != 0) && (maximum_builtins[1] == 0 || stream->floating != 0) &&
                 (maximum_builtins[2] == 0 || stream->queries != 0) && (maximum_builtins[3] == 0 || stream->atomics != 0);
        stream->status = result ? CHECK_STATUS_OK : CHECK_STATUS_RESOURCE;
    }
    if (result)
    {
        stream->count = count;
        stream->statistics.nodes = count;
        stream->statistics.output_bytes = output->length - output_start;
        stream->statistics.scratch_bytes = scratch->length - scratch_start;
        if (stream->argument_conversions != 0)
        {
            memset(stream->argument_conversions, 0, (size_t)graph->argument_count * sizeof(*stream->argument_conversions));
        }
        uint32_t node = 0;
        while (node < count)
        {
            if (options->scalar || pure[node] == 0)
            {
                check_graph_scalar_node(types, graph, stream, node, arguments);
                node += 1;
            }
            else
            {
                const uint32_t begin = node;
                while (node < count && pure[node] != 0)
                {
                    node += 1;
                }
                check_graph_region(types, graph, stream, begin, node, batch_size, levels, counts, order);
            }
        }
        for (uint32_t i = 0; result && i < count; i += 1)
        {
            if (stream->diagnostics[i] == CHECK_DIAG_RESOURCE)
            {
                result = false;
                stream->status = CHECK_STATUS_RESOURCE;
                stream->failed_node = i;
            }
        }
        if (result)
        {
            check_graph_reports(graph, stream, reports);
        }
    }
    if (!result)
    {
        arena_pop_to(output, output_start);
        const CheckStatus status = stream->status;
        const uint32_t failed = stream->failed_node;
        *stream = (CheckStream){
            .status = status,
            .failed_node = failed,
        };
    }
    arena_pop_to(scratch, scratch_start);
    return result;
}

#endif
