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

#include "c_check_overload.c"

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
    bool complex_constant;
    bool constant_p_side_effects;
    const uint8_t* shuffle_index_ice;
    uint32_t shuffle_index_ice_count;
    CheckBuiltinBitCastProof bit_cast_proof;
    const uint8_t* bit_cast_bytes;
    uint32_t bit_cast_byte_count;
    bool object_size_known[4];
    bool object_size_source_constant[4];
    uint64_t object_sizes[4];
    uint64_t minimum_alignment;
    bool alignment_known;
    // The first argument's leftmost identifier past its prefix operators, casts and parentheses, and its declaration's
    // alignment as clang gives it (check_input_declaration_alignment), its log2 plus 1: 0 when it names no object. An
    // atomic's address takes it where its lvalue path starts at that identifier (check_graph_atomic_alignment).
    uint32_t base_token;
    uint8_t base_alignment;
};

typedef struct CheckGraph CheckGraph;
typedef struct CheckX86Call CheckX86Call;
struct CheckX86Call
{
    uint32_t node;
    const BuiltinIntrinsicInfo* info;
    uint64_t required_ice_arguments;
    uint64_t ice_arguments;
    CheckBuiltinX86Reachability reachability;
    // Unselected source arms diagnose eagerly, independently of executable reachability. An enclosing
    // unevaluated context still takes precedence through runtime_diagnostic_suppressed.
    bool unselected_context;
    bool constant_evaluated_context;
    bool runtime_diagnostic_suppressed;
};

typedef struct CheckVectorCall CheckVectorCall;
struct CheckVectorCall
{
    uint32_t node;
    CheckVectorMember member;
};

typedef struct CheckOverloadCall CheckOverloadCall;
struct CheckOverloadCall
{
    uint32_t node;
    uint32_t head;
};

typedef enum CheckRegionProofOp
{
    CHECK_REGION_EVAL,
    CHECK_REGION_BRANCH,
    CHECK_REGION_CASE,
    CHECK_REGION_JUMP,
    CHECK_REGION_END,
} CheckRegionProofOp;

#define CHECK_REGION_UNSUPPORTED 0x01u

typedef struct CheckRegionProofStep CheckRegionProofStep;
struct CheckRegionProofStep
{
    CheckRegionProofOp op;
    uint32_t node;
    // CASE's integer constant; other instructions use BUSTER_CHECK_NONE.
    uint32_t operand;
    // Relative to the owning region's first step. A branch's false edge jumps; its true edge falls through.
    uint32_t target;
};

typedef struct CheckRegionProof CheckRegionProof;
struct CheckRegionProof
{
    uint32_t node;
    uint32_t first;
    uint32_t count;
    uint32_t flags;
};

struct CheckGraph
{
    // Every column has count logical entries. Operands are earlier nodes or BUSTER_CHECK_NONE. Arrays are not
    // padded; optional columns may be null. VALUE uses its leaf (check_graph_leaf_of). Other nodes use types/member as
    // action data.
    CheckOp* ops;
    uint32_t* tokens;
    uint32_t* operand0;
    uint32_t* operand1;
    uint32_t* operand2;
    // The VALUE nodes' values, leaf_count of them in node order: a VALUE node's is leaves[leaf_slots[node]], and every
    // other node's slot is BUSTER_CHECK_NONE. Only a VALUE node reads a value, and in a column of every node's the 96
    // bytes of the others' were most of the graph's memory (#572).
    CheckValue* leaves;
    uint32_t* leaf_slots;
    TypeId* types;
    uint32_t* members;
    uint8_t* contexts;
    // Optional lexical FENV_ACCESS state. Zero/null means the translation environment; source constants remain
    // separate from the runtime dependence carried by BUSTER_CHECK_FP_ENVIRONMENT on checked values.
    uint8_t* fp_environments;
    // Runtime arithmetic warning suppression for semantically checked source branches disconnected by SELECT.
    // Only integer overflow, shift counts and negative shifted values may be suppressed; constraints survive.
    uint8_t* warning_suppressed;
    // Optional, system_token_count entries: nonzero for a source token written in a system header. A node at such a
    // token takes BUSTER_CHECK_EXTENSION_SYSTEM_HEADER (clang's suppressed default-error warnings). Null means none.
    const uint8_t* system_tokens;
    uint32_t system_token_count;
    // Optional source-time tag history. Each node's cutoff is the exclusive end of its operand/type-name,
    // not the prefix operator token. A missing history preserves the direct action API's current-type view.
    const uint32_t* tag_completions;
    const uint32_t* transparent_unions;
    uint32_t tag_completion_count;
    uint32_t* source_positions;
    uint32_t extensions;
    uint32_t count;
    uint32_t capacity;
    uint32_t leaf_count;
    // CALL's arguments are contiguous, disjoint slices appended to argument_nodes in call postorder, consuming
    // argument_count entries exactly; offsets/counts have count logical entries. Argument nodes are earlier than
    // their call. The callee is operand0; operand1 and operand2 are absent. Each argument owns its conversion slot.
    uint32_t* argument_offsets;
    uint32_t* argument_counts;
    uint32_t* argument_nodes;
    // Optional, parallel to argument_nodes: each argument's first source token, where clang reports a CALL's
    // argument conversion and the first argument past a prototype's parameters. Null reports them at the call.
    uint32_t* argument_tokens;
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
    CheckVectorCall* vector_members;
    uint32_t vector_member_count;
    uint32_t vector_member_capacity;
    CheckX86Call* x86_calls;
    uint32_t x86_count;
    uint32_t x86_capacity;
    const CheckOverloadCandidate* overload_candidates;
    uint32_t overload_candidate_count;
    CheckOverloadCall* overload_calls;
    uint32_t overload_count;
    uint32_t overload_capacity;
    // Optional typed prefix control for statement expressions. Sorted owners consume contiguous step slices;
    // every expression dependency precedes the owning STATEMENT_VALUE. Runtime execution still uses the body.
    CheckRegionProof* region_proofs;
    uint32_t region_proof_count;
    uint32_t region_proof_capacity;
    CheckRegionProofStep* region_steps;
    uint32_t region_step_count;
    uint32_t region_step_capacity;
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
    // The nodes each path's action computed, and those the vector path completed without one: its leaves
    // (check_graph_leaf) and its batches' vector-eligible arithmetic.
    uint32_t scalar_nodes;
    uint32_t vector_nodes;
    // Separate source/speculative/emitted lane visits and the ordered publication replay.
    uint64_t query_proof_nodes;
    uint64_t query_replay_nodes;
    uint64_t output_bytes;
    uint64_t scratch_bytes;
};

typedef struct CheckStoreEffect CheckStoreEffect;
struct CheckStoreEffect
{
    uint32_t node;
    CheckValue value;
    CheckValue destination;
    CBuiltinId runtime_builtin;
    uint32_t evaluated_operands;
    uint32_t warnings;
    uint32_t warning_operands;
    uint32_t unsupported;
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
    CheckValue converted_arguments[2];
    CheckBuiltinMathKind math_kind;
    TypeId errno_pointer_type;
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
    bool compiler_barrier;
    bool reads_fp_environment;
    bool may_raise_fp_exceptions;
    bool may_raise_inexact;
    bool may_set_errno;
};

typedef struct CheckBuiltinQueryEffect CheckBuiltinQueryEffect;
struct CheckBuiltinQueryEffect
{
    uint32_t node;
    CheckValue runtime_value;
    uint32_t evaluated_operands;
    uint32_t warnings;
    uint32_t warning_operands;
    uint32_t mode;
    bool known;
    CheckBuiltinMiscConstraint constraint;
    CheckBuiltinQueryProof source_proof;
    CheckBuiltinQueryRuntimePlan runtime_plan;
    CheckBuiltinQueryEffects runtime_effects;
    uint32_t unsupported;
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
typedef struct CheckBuiltinVectorEffect CheckBuiltinVectorEffect;
struct CheckBuiltinVectorEffect
{
    uint32_t node;
    CheckValue converted_arguments[3];
    TypeId element_type;
    TypeId memory_type;
    uint32_t lane_count;
    uint32_t evaluated_argument_count;
    uint32_t warnings;
    uint32_t warning_operands;
    uint32_t unsupported;
    CBuiltinId runtime_builtin;
    CheckBuiltinVectorConstraint constraint;
    CheckBuiltinVectorStoreConversion store_conversion;
    bool reads_memory;
    bool writes_memory;
    bool nondeterministic;
    bool constant_evaluation_unmodeled;
    bool reads_fp_environment;
    bool may_raise_fp_exceptions;
    bool bool_splat_signed_true;
};

typedef struct CheckBuiltinX86Effect CheckBuiltinX86Effect;
struct CheckBuiltinX86Effect
{
    uint32_t node;
    const BuiltinIntrinsicInfo* info;
    CheckBuiltinX86Status status;
    CheckBuiltinX86Constraint constraint;
    uint64_t checked_ice_arguments;
    uint32_t matched_rules;
    uint32_t unsupported;
    uint32_t range_event_count;
    CheckBuiltinX86RangeEvent range_events[BUSTER_CHECK_BUILTIN_X86_EVENTS];
};

typedef struct CheckBuiltinBitCastEffect CheckBuiltinBitCastEffect;
struct CheckBuiltinBitCastEffect
{
    uint32_t node;
    TypeId source_type;
    TypeId representation_type;
    uint64_t byte_count;
    uint32_t evaluated_operands;
    uint32_t unsupported;
    CBuiltinId runtime_builtin;
    uint8_t bytes[BUSTER_CHECK_BUILTIN_BIT_CAST_BYTES];
    uint32_t payload_count;
    bool representation_known;
    bool folded;
    bool runtime_required;
    bool volatile_read;
};

typedef struct CheckOverloadEffect CheckOverloadEffect;
struct CheckOverloadEffect
{
    uint32_t node;
    CheckOverloadResult selected;
};

typedef struct CheckOverloadAddressEffect CheckOverloadAddressEffect;
struct CheckOverloadAddressEffect
{
    uint32_t node;
    // Fixed operand zero through two, or three plus the argument index.
    uint32_t operand;
    CheckOverloadResult selected;
};

struct CheckStream
{
    const CheckGraph* graph;
    // Node-indexed typed postorder output. The graph supplies operator, operand and source identity.
    CheckValue* values;
    CheckNodeConversions* conversions;
    CheckConversion* argument_conversions;
    CheckDiagnostic* diagnostics;
    // The operand each diagnostic names, as CheckActionResult.diagnostic_operand has it (a CALL's argument i is 1 + i).
    uint32_t* diagnostic_operands;
    // A MEMBER or ARROW node's member's offset in bits from its record's start (CheckActionResult.member_offset), which
    // the lowering takes rather than looking the member up again (#591); 0 for every other node.
    uint64_t* member_offsets;
    uint32_t* warnings;
    // Warning kinds whose default severity is ignored; an explicit source policy may enable them.
    uint32_t* warning_ignored;
    CheckReport* reports;
    // Valid overflow stores in increasing graph-node order. This is a semantic descriptor stream: consumers
    // execute only effects belonging to reached expression nodes, never stores in an unselected branch.
    CheckStoreEffect* effects;
    CheckVarargsEffect* varargs;
    CheckBuiltinMiscEffect* misc;
    CheckBuiltinFloatEffect* floating;
    CheckBuiltinQueryEffect* queries;
    CheckBuiltinAtomicEffect* atomics;
    CheckBuiltinVectorEffect* vectors;
    CheckBuiltinX86Effect* x86;
    CheckBuiltinBitCastEffect* bit_casts;
    CheckOverloadEffect* overloads;
    CheckOverloadAddressEffect* overload_addresses;
    uint32_t count;
    uint32_t report_count;
    uint32_t effect_count;
    uint32_t varargs_count;
    uint32_t misc_count;
    uint32_t floating_count;
    uint32_t query_count;
    uint32_t atomic_count;
    uint32_t vector_count;
    uint32_t x86_count;
    uint32_t bit_cast_count;
    uint32_t overload_count;
    uint32_t overload_address_count;
    uint32_t overload_address_capacity;
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

// A VALUE node's leaf (CheckGraph.leaves).
static CheckValue* check_graph_leaf_of(const CheckGraph* graph, uint32_t node)
{
    return graph->leaves + graph->leaf_slots[node];
}

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
        graph->leaf_slots = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, capacity);
        graph->types = BUSTER_ARENA_PUSH_ARRAY(arena, TypeId, capacity);
        graph->members = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, capacity);
        graph->contexts = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, capacity);
        graph->source_positions = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, capacity);
        graph->argument_offsets = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, capacity);
        graph->argument_counts = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, capacity);
        result = graph->ops != 0 && graph->tokens != 0 && graph->operand0 != 0 && graph->operand1 != 0 && graph->operand2 != 0 && graph->leaves != 0 && graph->leaf_slots != 0 && graph->types != 0 && graph->members != 0 && graph->contexts != 0 && graph->source_positions != 0 && graph->argument_offsets != 0 && graph->argument_counts != 0;
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
        // A VALUE node takes the next leaf, cleared for its maker to fill.
        const bool leaf = op == CHECK_OP_VALUE;
        graph->leaf_slots[node] = leaf ? graph->leaf_count : BUSTER_CHECK_NONE;
        if (leaf)
        {
            graph->leaves[graph->leaf_count] = (CheckValue){ 0 };
            graph->leaf_count += 1;
        }
        graph->types[node] = 0;
        graph->members[node] = 0;
        graph->contexts[node] = 0;
        if (graph->fp_environments != NULL)
        {
            graph->fp_environments[node] = 0;
        }
        if (graph->warning_suppressed != NULL)
        {
            graph->warning_suppressed[node] = 0;
        }
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
    return op == CHECK_OP_CALL || op == CHECK_OP_BUILTIN_MISC || op == CHECK_OP_BUILTIN_FLOAT || op == CHECK_OP_BUILTIN_QUERY ||
           op == CHECK_OP_BUILTIN_ATOMIC || op == CHECK_OP_BUILTIN_VECTOR || op == CHECK_OP_BUILTIN_X86 || op == CHECK_OP_BUILTIN_BIT_CAST;
}

static __attribute__((unused)) uint64_t check_graph_x86_ice_mask(const BuiltinIntrinsicInfo* info, bool constant_evaluated_context)
{
    uint64_t mask = 0;
    uint32_t first = 0;
    uint32_t end = BUSTER_ARRAY_LENGTH(check_builtin_x86_rules);
    while (info != NULL && first < end)
    {
        const uint32_t middle = first + (end - first) / 2;
        if (check_builtin_x86_compare(info->name, info->name_length, check_builtin_x86_rules + middle) > 0)
        {
            first = middle + 1;
        }
        else
        {
            end = middle;
        }
    }
    for (uint32_t i = first; info != NULL && i < BUSTER_ARRAY_LENGTH(check_builtin_x86_rules) &&
                             check_builtin_x86_compare(info->name, info->name_length, check_builtin_x86_rules + i) == 0; i += 1)
    {
        const CheckBuiltinX86Rule* rule = check_builtin_x86_rules + i;
        if (rule->kind != CHECK_BUILTIN_X86_CONSTRAINT_RANGE || !constant_evaluated_context)
        {
            mask |= UINT64_C(1) << rule->argument;
        }
    }
    return mask;
}

static __attribute__((unused)) CheckOp check_graph_builtin_op(CBuiltinId id)
{
    CheckOp op = CHECK_OP_COUNT;
    if (id == CBUILTIN_BIT_CAST)
    {
        op = CHECK_OP_BUILTIN_BIT_CAST;
    }
    else if ((id >= CBUILTIN_EXPECT && id <= CBUILTIN_PREFETCH) || (id >= CBUILTIN_MEMCPY && id <= CBUILTIN_ALLOCA) ||
        id == CBUILTIN_FRAME_ADDRESS || id == CBUILTIN_RETURN_ADDRESS || id == CBUILTIN_EXTRACT_RETURN_ADDR || id == CBUILTIN_FROB_RETURN_ADDR)
    {
        op = CHECK_OP_BUILTIN_MISC;
    }
    else if ((id >= CBUILTIN_HUGE_VAL && id <= CBUILTIN_FLT_ROUNDS) || check_builtin_math_kind(id) != CHECK_BUILTIN_MATH_NONE)
    {
        op = CHECK_OP_BUILTIN_FLOAT;
    }
    else if (id == CBUILTIN_CONSTANT_P || id == CBUILTIN_OBJECT_SIZE)
    {
        op = CHECK_OP_BUILTIN_QUERY;
    }
    else if (id == CBUILTIN_ATOMIC_LOAD_N || id == CBUILTIN_ATOMIC_STORE_N || id == CBUILTIN_SYNC_SYNCHRONIZE ||
             id == CBUILTIN_ATOMIC_THREAD_FENCE || id == CBUILTIN_ATOMIC_SIGNAL_FENCE || check_builtin_atomic_rmw_rule(id) != NULL)
    {
        op = CHECK_OP_BUILTIN_ATOMIC;
    }
    else if (id == CBUILTIN_UMULL_OVERFLOW)
    {
        op = CHECK_OP_BUILTIN_OVERFLOW;
    }
    else if (id >= CBUILTIN_ELEMENTWISE_ABS && id <= CBUILTIN_NONTEMPORAL_STORE)
    {
        op = CHECK_OP_BUILTIN_VECTOR;
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

// The operators a region may reorder when their operands and results are of builtin arithmetic types (check_graph_pure).
static bool check_graph_reorderable(CheckOp op)
{
    return (op >= CHECK_OP_POSITIVE && op <= CHECK_OP_CONDITIONAL) || (op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_BIT_XOR_ASSIGN) || op == CHECK_OP_COMMA ||
           op == CHECK_OP_ASSIGN || op == CHECK_OP_CONDITION || op == CHECK_OP_CAST || op == CHECK_OP_INITIALIZE || op == CHECK_OP_CONVERT || op == CHECK_OP_SELECT;
}

// Those of them whose result is of the type the graph requests (types[node]).
static bool check_graph_typed(CheckOp op)
{
    return op == CHECK_OP_CAST || op == CHECK_OP_INITIALIZE || op == CHECK_OP_CONVERT;
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
        result = graph->leaves != 0 && check_graph_leaf_of(graph, node)->overload_head == 0 && check_graph_builtin(check_graph_leaf_of(graph, node)->type);
    }
    else if (op == CHECK_OP_SIZEOF || op == CHECK_OP_ALIGNOF)
    {
        result = graph->operand0[node] == BUSTER_CHECK_NONE || pure[graph->operand0[node]] != 0;
    }
    else if (check_graph_reorderable(op))
    {
        result = !check_graph_typed(op);
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

// What check_graph_validate counts as it goes: each argument slice's and sideband list's consumption, in node order, and the
// maxima and counts check_graph_run sizes its outputs by. Only a valid graph's are defined.
typedef struct CheckGraphValidation CheckGraphValidation;
struct CheckGraphValidation
{
    uint32_t maximum_arguments;
    uint32_t maximum_effects;
    uint32_t builtins[6];
    uint32_t arguments;
    uint32_t varargs;
    uint32_t builtin_calls;
    uint32_t vectors;
    uint32_t x86;
    uint32_t overloads;
    uint32_t leaves;
    // The blocks check_graph_validate_vector decided itself, without the exact path (0 on that path).
    uint32_t vector_blocks;
};

// The graph's own counts, its sideband lists and the columns every node needs, before any node.
static bool check_graph_validate_lists(const CheckGraph* graph)
{
    bool result = graph->count <= graph->capacity && graph->count <= (1u << 30) && graph->argument_count <= graph->argument_capacity && graph->argument_count <= (1u << 30);
    result = result && graph->varargs_count <= graph->varargs_capacity && graph->varargs_count <= graph->count &&
             (graph->varargs_count == 0 || graph->varargs_calls != NULL);
    result = result && graph->builtin_count <= graph->builtin_capacity && graph->builtin_count <= graph->count &&
             (graph->builtin_count == 0 || graph->builtin_calls != NULL);
    result = result && graph->vector_member_count <= graph->vector_member_capacity && graph->vector_member_count <= graph->count &&
             (graph->vector_member_count == 0 || graph->vector_members != NULL);
    result = result && graph->x86_count <= graph->x86_capacity && graph->x86_count <= graph->count &&
             (graph->x86_count == 0 || graph->x86_calls != NULL);
    result = result && graph->overload_count <= graph->overload_capacity && graph->overload_count <= graph->count &&
             (graph->overload_count == 0 || graph->overload_calls != NULL) && graph->overload_candidate_count <= (1u << 30) &&
             (graph->overload_candidate_count == 0 || graph->overload_candidates != NULL);
    result = result && graph->region_proof_count <= graph->region_proof_capacity && graph->region_proof_count <= graph->count &&
             (graph->region_proof_count == 0 || graph->region_proofs != NULL) &&
             graph->region_step_count <= graph->region_step_capacity && graph->region_step_count <= (1u << 30) &&
             (graph->region_step_count == 0 || graph->region_steps != NULL);
    uint32_t region_steps = 0;
    for (uint32_t i = 0; result && i < graph->region_proof_count; i += 1)
    {
        const CheckRegionProof* region = graph->region_proofs + i;
        result = region->node < graph->count && graph->ops != NULL && graph->ops[region->node] == CHECK_OP_STATEMENT_VALUE &&
                 (i == 0 || graph->region_proofs[i - 1].node < region->node) && region->first == region_steps &&
                 region->count != 0 && region->count <= graph->region_step_count - region_steps &&
                 (region->flags & ~CHECK_REGION_UNSUPPORTED) == 0;
        for (uint32_t j = 0; result && j < region->count; j += 1)
        {
            const CheckRegionProofStep* step = graph->region_steps + region->first + j;
            const bool expression = step->op == CHECK_REGION_EVAL || step->op == CHECK_REGION_BRANCH || step->op == CHECK_REGION_CASE;
            const bool target = step->op == CHECK_REGION_BRANCH || step->op == CHECK_REGION_CASE || step->op == CHECK_REGION_JUMP;
            result = (unsigned)step->op <= CHECK_REGION_END && (expression ? step->node < region->node : step->node == BUSTER_CHECK_NONE) &&
                     (step->op == CHECK_REGION_CASE ? step->operand < region->node : step->operand == BUSTER_CHECK_NONE) &&
                     (target ? step->target < region->count : step->target == BUSTER_CHECK_NONE) &&
                     (j + 1 < region->count || step->op == CHECK_REGION_END);
        }
        region_steps += result ? region->count : 0;
    }
    result = result && region_steps == graph->region_step_count;
    for (uint32_t i = 0; result && i < graph->overload_candidate_count; i += 1)
    {
        const CheckOverloadCandidate* candidate = graph->overload_candidates + i;
        result = (candidate->previous == BUSTER_CHECK_NONE || candidate->previous < i) && candidate->declaration != BUSTER_CHECK_NONE &&
                 candidate->canonical != BUSTER_CHECK_NONE &&
                 (candidate->flags & ~(BUSTER_CHECK_OVERLOADABLE | BUSTER_CHECK_OVERLOAD_UNAVAILABLE | BUSTER_CHECK_OVERLOAD_DEFINED)) == 0;
    }
    result = result && (graph->tag_completions != 0 ? graph->source_positions != 0 : graph->tag_completion_count == 0);
    result = result && (graph->transparent_unions == NULL || graph->tag_completions != NULL);
    result = result && (graph->extensions & ~(BUSTER_CHECK_EXTENSION_GNU | BUSTER_CHECK_EXTENSION_GNU_CONSTANT)) == 0;
    if (graph->count != 0)
    {
        result = result && graph->ops != 0 && graph->operand0 != 0 && graph->operand1 != 0 && graph->operand2 != 0;
    }
    if (graph->argument_count != 0)
    {
        result = result && graph->argument_nodes != 0;
    }
    return result;
}

// One node, every node before it valid: its operator, edges, columns and turn in each sideband list, then its purity and the
// counters. The exact path: check_graph_validate runs it on every node, check_graph_validate_vector on the blocks it leaves.
static bool check_graph_validate_node(const CheckGraph* graph, uint32_t node, uint8_t* pure, CheckGraphValidation* state)
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
    bool result = op >= CHECK_OP_VALUE && op < CHECK_OP_COUNT;
    result = result && (graph->tag_completions == 0 || graph->source_positions[node] != BUSTER_CHECK_NONE);
    for (uint32_t i = 0; i < 3; i += 1)
    {
        result = result && (i < arity ? operands[i] < node : operands[i] == BUSTER_CHECK_NONE);
    }
    // A VALUE node's leaf is the next one, and every other node has none.
    const uint32_t slot = graph->leaf_slots != 0 ? graph->leaf_slots[node] : BUSTER_CHECK_NONE;
    result = result && (op == CHECK_OP_VALUE ? graph->leaves != 0 && slot == state->leaves && slot < graph->leaf_count : slot == BUSTER_CHECK_NONE);
    if (result && op == CHECK_OP_VALUE)
    {
        result = graph->leaves[slot].overload_head <= graph->overload_candidate_count;
    }
    result = result && (op != CHECK_OP_STATEMENT_VALUE || graph->members == 0 || (graph->members[node] & ~BUSTER_CHECK_SIDE_EFFECT) == 0);
    result = result && ((op != CHECK_OP_VLA_BOUND && op != CHECK_OP_VLA_EXTENT) || (graph->members != NULL && graph->members[node] != 0));
    result = result && (graph->contexts == 0 || (graph->contexts[node] & ~(BUSTER_CHECK_CONTEXT_ICE | BUSTER_CHECK_CONTEXT_ASSERT | BUSTER_CHECK_CONTEXT_STATIC | BUSTER_CHECK_CONTEXT_GNU_CONSTANT)) == 0);
    result = result && (graph->fp_environments == NULL || (graph->fp_environments[node] & ~BUSTER_CHECK_FP_ENV_ACCESS) == 0);
    result = result && (graph->warning_suppressed == NULL ||
                        (graph->warning_suppressed[node] & ~(CHECK_WARNING_INTEGER_OVERFLOW | CHECK_WARNING_SHIFT_COUNT_NEGATIVE |
                                                           CHECK_WARNING_SHIFT_COUNT_OVERFLOW | CHECK_WARNING_SHIFT_NEGATIVE_VALUE)) == 0);
    if (varargs)
    {
        result = result && graph->argument_offsets != NULL && graph->argument_counts != NULL && graph->members != NULL &&
                 offset == state->varargs && offset < graph->varargs_count && graph->varargs_calls[offset].node == node;
    }
    else
    {
        result = result && offset <= graph->argument_count && count <= graph->argument_count - offset && (count == 0 || check_graph_has_arguments(op));
        result = result && (count == 0 || offset == state->arguments);
        for (uint32_t i = 0; result && i < count; i += 1)
        {
            result = graph->argument_nodes[offset + i] < node;
        }
    }
    if (result && state->builtin_calls < graph->builtin_count && graph->builtin_calls[state->builtin_calls].node <= node)
    {
        const CheckBuiltinCall* call = graph->builtin_calls + state->builtin_calls;
        result = call->node == node &&
                 (op == CHECK_OP_BUILTIN_FLOAT || op == CHECK_OP_BUILTIN_QUERY || op == CHECK_OP_BUILTIN_ATOMIC ||
                  op == CHECK_OP_BUILTIN_VECTOR || op == CHECK_OP_BUILTIN_BIT_CAST);
        const bool shuffle = op == CHECK_OP_BUILTIN_VECTOR && graph->members != NULL && graph->members[node] == CBUILTIN_SHUFFLEVECTOR;
        result = result && (call->shuffle_index_ice != NULL ? shuffle && count >= 2 && call->shuffle_index_ice_count == count - 2 : call->shuffle_index_ice_count == 0);
        for (uint32_t i = 0; result && i < call->shuffle_index_ice_count; i += 1)
        {
            result = call->shuffle_index_ice[i] <= CHECK_BUILTIN_VECTOR_INDEX_ICE_GNU;
        }
        result = result && (unsigned)call->bit_cast_proof <= CHECK_BUILTIN_BIT_CAST_PROOF_ICE &&
                 (call->bit_cast_bytes != NULL || call->bit_cast_byte_count == 0) &&
                 (op == CHECK_OP_BUILTIN_BIT_CAST || (call->bit_cast_proof == CHECK_BUILTIN_BIT_CAST_PROOF_NONE &&
                  call->bit_cast_bytes == NULL && call->bit_cast_byte_count == 0));
        state->builtin_calls += 1;
    }
    if (result && state->vectors < graph->vector_member_count && graph->vector_members[state->vectors].node <= node)
    {
        const CheckVectorCall* vector = graph->vector_members + state->vectors;
        result = vector->node == node && op == CHECK_OP_MEMBER && vector->member.kind <= CHECK_VECTOR_MEMBER_ODD &&
                 vector->member.count <= (1u << 30) && (vector->member.count == 0 || vector->member.lanes != NULL);
        state->vectors += 1;
    }
    if (result && op == CHECK_OP_BUILTIN_X86)
    {
        result = state->x86 < graph->x86_count && graph->x86_calls[state->x86].node == node &&
                 graph->x86_calls[state->x86].info != NULL && graph->types != NULL;
        state->x86 += result;
    }
    if (result && state->overloads < graph->overload_count && graph->overload_calls[state->overloads].node <= node)
    {
        const CheckOverloadCall* call = graph->overload_calls + state->overloads;
        result = call->node == node && op == CHECK_OP_CALL && call->head < graph->overload_candidate_count;
        state->overloads += 1;
    }
    if (result)
    {
        pure[node] = (uint8_t)check_graph_pure(graph, node, pure);
        state->maximum_arguments = !varargs && count > state->maximum_arguments ? count : state->maximum_arguments;
        state->maximum_effects += op == CHECK_OP_BUILTIN_OVERFLOW;
        state->arguments += varargs ? 0 : count;
        state->varargs += varargs;
        state->leaves += op == CHECK_OP_VALUE;
        state->builtins[0] += op == CHECK_OP_BUILTIN_MISC;
        state->builtins[1] += op == CHECK_OP_BUILTIN_FLOAT;
        state->builtins[2] += op == CHECK_OP_BUILTIN_QUERY;
        state->builtins[3] += op == CHECK_OP_BUILTIN_ATOMIC;
        state->builtins[4] += op == CHECK_OP_BUILTIN_VECTOR;
        state->builtins[5] += op == CHECK_OP_BUILTIN_BIT_CAST;
    }
    return result;
}

// Every node consumed every argument slot and sideband entry.
static bool check_graph_validate_consumed(const CheckGraph* graph, const CheckGraphValidation* state)
{
    return state->arguments == graph->argument_count && state->varargs == graph->varargs_count && state->builtin_calls == graph->builtin_count &&
           state->vectors == graph->vector_member_count && state->x86 == graph->x86_count && state->overloads == graph->overload_count &&
           state->leaves == graph->leaf_count;
}

// The scalar twin of check_graph_validate_vector: every node in order, up to the first invalid one, which `failed` names.
static bool check_graph_validate(const CheckGraph* graph, uint8_t* pure, CheckGraphValidation* state, uint32_t* failed)
{
    *state = (CheckGraphValidation){ 0 };
    *failed = BUSTER_CHECK_NONE;
    bool result = check_graph_validate_lists(graph);
    for (uint32_t node = 0; result && node < graph->count; node += 1)
    {
        result = check_graph_validate_node(graph, node, pure, state);
        *failed = result ? BUSTER_CHECK_NONE : node;
    }
    result = result && check_graph_validate_consumed(graph, state);
    return result;
}

// check_graph_validate_vector's classes of operators, a byte each, from the exact path's rules (check_graph_classes): the
// arity (a query's that names no type), whether it takes an argument slice, whether a region may reorder it and with the
// requested type, whether it is sizeof or _Alignof, and whether only the exact path checks it.
#define BUSTER_CHECK_CLASS_ARITY 0x03u
#define BUSTER_CHECK_CLASS_ARGUMENTS 0x04u
#define BUSTER_CHECK_CLASS_REORDERABLE 0x08u
#define BUSTER_CHECK_CLASS_TYPED 0x10u
#define BUSTER_CHECK_CLASS_QUERY 0x20u
#define BUSTER_CHECK_CLASS_EXACT 0x40u
// vpermi2b's table: an operator's low seven bits index it. Operators past CHECK_OP_COUNT fail before their class counts.
#define BUSTER_CHECK_CLASS_OPERATORS 128u
#define BUSTER_CHECK_VALIDATE_LANES 16u

_Static_assert(sizeof(CheckOp) == sizeof(uint32_t), "the vector validator loads operators as 32-bit lanes");
_Static_assert(CHECK_OP_COUNT <= BUSTER_CHECK_CLASS_OPERATORS, "every operator has a class byte");

static void check_graph_classes(uint8_t* classes)
{
    for (uint32_t i = 0; i < BUSTER_CHECK_CLASS_OPERATORS; i += 1)
    {
        const CheckOp op = (CheckOp)i;
        const bool query = op == CHECK_OP_SIZEOF || op == CHECK_OP_ALIGNOF;
        // VARARGS reads its own list and an x86 builtin its calls' rules, in check_graph_validate_node alone.
        const bool exact = op == CHECK_OP_VARARGS || op == CHECK_OP_BUILTIN_X86;
        classes[i] = (uint8_t)(check_graph_arity(op, 0) | (check_graph_has_arguments(op) ? BUSTER_CHECK_CLASS_ARGUMENTS : 0) |
                               (check_graph_reorderable(op) ? BUSTER_CHECK_CLASS_REORDERABLE : 0) | (check_graph_typed(op) ? BUSTER_CHECK_CLASS_TYPED : 0) |
                               (query ? BUSTER_CHECK_CLASS_QUERY : 0) | (exact ? BUSTER_CHECK_CLASS_EXACT : 0));
    }
}

// The sixteen nodes from `node`, every node before them valid: true with their purity and the counters as
// check_graph_validate_node gives them, or false with neither, for the exact path to decide them. That path takes a block
// with an invalid node, a VARARGS or x86 builtin or a sideband entry; the block checks each leaf's overload head, each
// column's rule in its lanes and each argument slice's nodes before it commits anything. `classes` is check_graph_classes'
// table in two vectors; pure[graph->count] is 1, an absent operand's purity.
static bool check_graph_validate_block(const CheckGraph* graph, uint32_t node, uint8_t* pure, CheckGraphValidation* state, __m512i classes_low, __m512i classes_high)
{
    const __m512i zero = _mm512_setzero_si512();
    const __m512i none = _mm512_set1_epi32((int)BUSTER_CHECK_NONE);
    const __m512i nodes = _mm512_add_epi32(_mm512_set1_epi32((int)node), _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15));
    const __m512i ops = _mm512_loadu_si512(graph->ops + node);
    // A leaf's slot, the next one's, its overload head, and whether a region may reorder it: first, while little else is
    // live, since each leaf is two cache lines of its record and these are most of the block's time.
    const __mmask16 leaves = _mm512_cmpeq_epi32_mask(ops, _mm512_set1_epi32(CHECK_OP_VALUE));
    const bool readable = graph->leaves != 0 && graph->leaf_slots != 0;
    bool heads = true;
    uint32_t reorderable_leaves = 0;
    uint32_t expected = state->leaves;
    for (uint32_t lanes = readable ? leaves : 0; lanes != 0; lanes &= lanes - 1)
    {
        const uint32_t lane = (uint32_t)__builtin_ctz(lanes);
        const uint32_t slot = graph->leaf_slots[node + lane];
        const bool next = slot == expected && slot < graph->leaf_count;
        heads &= next;
        expected += 1;
        if (next)
        {
            const CheckValue* leaf = graph->leaves + slot;
            heads &= leaf->overload_head <= graph->overload_candidate_count;
            reorderable_leaves |= (uint32_t)(leaf->overload_head == 0 && check_graph_builtin(leaf->type)) << lane;
        }
    }
    const __m512i requested = _mm512_loadu_si512(graph->types + node);
    const __m512i offsets = _mm512_loadu_si512(graph->argument_offsets + node);
    const __m512i counts = _mm512_loadu_si512(graph->argument_counts + node);
    const __m512i members = _mm512_loadu_si512(graph->members + node);
    const __m512i operands[3] =
    {
        _mm512_loadu_si512(graph->operand0 + node),
        _mm512_loadu_si512(graph->operand1 + node),
        _mm512_loadu_si512(graph->operand2 + node),
    };
    const __m128i op_bytes = _mm512_cvtepi32_epi8(ops);
    const __m512i classes = _mm512_cvtepu8_epi32(_mm512_castsi512_si128(_mm512_permutex2var_epi8(classes_low, _mm512_zextsi128_si512(op_bytes), classes_high)));
    const __mmask16 query = _mm512_test_epi32_mask(classes, _mm512_set1_epi32(BUSTER_CHECK_CLASS_QUERY));
    const __m512i arity = _mm512_mask_mov_epi32(_mm512_and_si512(classes, _mm512_set1_epi32(BUSTER_CHECK_CLASS_ARITY)), query & _mm512_test_epi32_mask(requested, requested), zero);
    __mmask16 valid = _mm512_cmplt_epu32_mask(ops, _mm512_set1_epi32(CHECK_OP_COUNT));
    for (uint32_t i = 0; i < 3; i += 1)
    {
        const __mmask16 used = _mm512_cmpgt_epu32_mask(arity, _mm512_set1_epi32((int)i));
        valid &= (used & _mm512_cmplt_epu32_mask(operands[i], nodes)) | ((__mmask16)~used & _mm512_cmpeq_epi32_mask(operands[i], none));
    }
    if (graph->tag_completions != 0)
    {
        valid &= _mm512_cmpneq_epi32_mask(_mm512_loadu_si512(graph->source_positions + node), none);
    }
    valid &= readable ? (__mmask16)0xffff : (__mmask16)~leaves;
    if (graph->leaf_slots != 0)
    {
        valid &= leaves | _mm512_cmpeq_epi32_mask(_mm512_loadu_si512(graph->leaf_slots + node), none);
    }
    valid &= (__mmask16)~_mm512_cmpeq_epi32_mask(ops, _mm512_set1_epi32(CHECK_OP_STATEMENT_VALUE)) | _mm512_testn_epi32_mask(members, _mm512_set1_epi32((int)~BUSTER_CHECK_SIDE_EFFECT));
    const __mmask16 bounds = _mm512_cmpeq_epi32_mask(ops, _mm512_set1_epi32(CHECK_OP_VLA_BOUND)) | _mm512_cmpeq_epi32_mask(ops, _mm512_set1_epi32(CHECK_OP_VLA_EXTENT));
    valid &= (__mmask16)~bounds | _mm512_test_epi32_mask(members, members);
    if (graph->contexts != 0)
    {
        const __m512i contexts = _mm512_cvtepu8_epi32(_mm_loadu_si128((const __m128i*)(graph->contexts + node)));
        valid &= _mm512_testn_epi32_mask(contexts, _mm512_set1_epi32((int)~(BUSTER_CHECK_CONTEXT_ICE | BUSTER_CHECK_CONTEXT_ASSERT | BUSTER_CHECK_CONTEXT_STATIC | BUSTER_CHECK_CONTEXT_GNU_CONSTANT)));
    }
    if (graph->fp_environments != NULL)
    {
        const __m512i environments = _mm512_cvtepu8_epi32(_mm_loadu_si128((const __m128i*)(graph->fp_environments + node)));
        valid &= _mm512_testn_epi32_mask(environments, _mm512_set1_epi32((int)~BUSTER_CHECK_FP_ENV_ACCESS));
    }
    if (graph->warning_suppressed != NULL)
    {
        const __m512i suppressed = _mm512_cvtepu8_epi32(_mm_loadu_si128((const __m128i*)(graph->warning_suppressed + node)));
        valid &= _mm512_testn_epi32_mask(suppressed, _mm512_set1_epi32((int)~(CHECK_WARNING_INTEGER_OVERFLOW | CHECK_WARNING_SHIFT_COUNT_NEGATIVE |
                                                                              CHECK_WARNING_SHIFT_COUNT_OVERFLOW | CHECK_WARNING_SHIFT_NEGATIVE_VALUE)));
    }
    // Argument slices within the arguments, an operator's that takes them, each starting where the block's earlier ones end:
    // an exclusive prefix sum of the counts. While every lane holds, the sums stay within argument_count, below 2^31.
    const __m512i total = _mm512_set1_epi32((int)graph->argument_count);
    valid &= _mm512_cmple_epu32_mask(offsets, total) & _mm512_cmple_epu32_mask(counts, _mm512_sub_epi32(total, offsets));
    const __mmask16 sliced = _mm512_test_epi32_mask(counts, counts);
    valid &= (__mmask16)~sliced | _mm512_test_epi32_mask(classes, _mm512_set1_epi32(BUSTER_CHECK_CLASS_ARGUMENTS));
    __m512i ends = counts;
    ends = _mm512_add_epi32(ends, _mm512_alignr_epi32(ends, zero, 15));
    ends = _mm512_add_epi32(ends, _mm512_alignr_epi32(ends, zero, 14));
    ends = _mm512_add_epi32(ends, _mm512_alignr_epi32(ends, zero, 12));
    ends = _mm512_add_epi32(ends, _mm512_alignr_epi32(ends, zero, 8));
    const __m512i starts = _mm512_add_epi32(_mm512_sub_epi32(ends, counts), _mm512_set1_epi32((int)state->arguments));
    valid &= (__mmask16)~sliced | _mm512_cmpeq_epi32_mask(offsets, starts);
    const uint32_t last = node + BUSTER_CHECK_VALIDATE_LANES - 1;
    bool result = valid == 0xffff && heads && _mm512_test_epi32_mask(classes, _mm512_set1_epi32(BUSTER_CHECK_CLASS_EXACT)) == 0 &&
                  (state->builtin_calls == graph->builtin_count || graph->builtin_calls[state->builtin_calls].node > last) &&
                  (state->vectors == graph->vector_member_count || graph->vector_members[state->vectors].node > last) &&
                  (state->overloads == graph->overload_count || graph->overload_calls[state->overloads].node > last);
    for (uint32_t lanes = result ? sliced : 0; lanes != 0; lanes &= lanes - 1)
    {
        const uint32_t lane = (uint32_t)__builtin_ctz(lanes);
        const uint32_t first = graph->argument_offsets[node + lane];
        const uint32_t count = graph->argument_counts[node + lane];
        for (uint32_t i = 0; i < count; i += 1)
        {
            result &= graph->argument_nodes[first + i] < node + lane;
        }
    }
    if (result)
    {
        // check_graph_pure: a reorderable leaf, a query, or an operator a region may reorder (with a builtin or void requested
        // type, if its result has it), and every operand present reorderable. An absent operand reads pure[count]'s 1.
        const __mmask16 reorderable = _mm512_test_epi32_mask(classes, _mm512_set1_epi32(BUSTER_CHECK_CLASS_REORDERABLE));
        const __mmask16 typed = _mm512_test_epi32_mask(classes, _mm512_set1_epi32(BUSTER_CHECK_CLASS_TYPED));
        const __m512i code = _mm512_sub_epi32(_mm512_srli_epi32(requested, BUSTER_TYPE_INDEX_SHIFT), _mm512_set1_epi32(BUSTER_TYPE_CODE_BOOL));
        const __mmask16 builtin = _mm512_cmple_epu32_mask(code, _mm512_set1_epi32(BUSTER_TYPE_CODE_LONG_DOUBLE - BUSTER_TYPE_CODE_BOOL));
        const __mmask16 type = builtin | _mm512_cmpeq_epi32_mask(requested, _mm512_set1_epi32((int)BUSTER_TYPE_VOID));
        const uint32_t local = (uint32_t)(query | (reorderable & ((__mmask16)~typed | type))) | reorderable_leaves;
        _Alignas(64) uint32_t edges[3][BUSTER_CHECK_VALIDATE_LANES];
        const __m512i absent = _mm512_set1_epi32((int)graph->count);
        for (uint32_t i = 0; i < 3; i += 1)
        {
            _mm512_store_si512(edges[i], _mm512_min_epu32(operands[i], absent));
        }
        // In node order: an operand may be an earlier lane.
        for (uint32_t lane = 0; lane < BUSTER_CHECK_VALIDATE_LANES; lane += 1)
        {
            pure[node + lane] = (uint8_t)((local >> lane) & 1u & pure[edges[0][lane]] & pure[edges[1][lane]] & pure[edges[2][lane]]);
        }
        const uint32_t widest = _mm512_reduce_max_epu32(counts);
        state->maximum_arguments = widest > state->maximum_arguments ? widest : state->maximum_arguments;
        state->arguments += (uint32_t)_mm512_reduce_add_epi32(counts);
        state->leaves += (uint32_t)__builtin_popcount(leaves);
        state->maximum_effects += (uint32_t)__builtin_popcount(_mm512_cmpeq_epi32_mask(ops, _mm512_set1_epi32(CHECK_OP_BUILTIN_OVERFLOW)));
        const CheckOp counted[6] =
        {
            CHECK_OP_BUILTIN_MISC,
            CHECK_OP_BUILTIN_FLOAT,
            CHECK_OP_BUILTIN_QUERY,
            CHECK_OP_BUILTIN_ATOMIC,
            CHECK_OP_BUILTIN_VECTOR,
            CHECK_OP_BUILTIN_BIT_CAST,
        };
        for (uint32_t i = 0; i < 6; i += 1)
        {
            state->builtins[i] += (uint32_t)__builtin_popcount(_mm512_cmpeq_epi32_mask(ops, _mm512_set1_epi32(counted[i])));
        }
    }
    return result;
}

// check_graph_validate's result, failed node and, for a valid graph, its purity and counters, sixteen nodes at a time
// (check_graph_validate_block) where the optional columns the blocks read are present. `pure` has graph->count + 1 bytes.
static bool check_graph_validate_vector(const CheckGraph* graph, uint8_t* pure, CheckGraphValidation* state, uint32_t* failed)
{
    *state = (CheckGraphValidation){ 0 };
    *failed = BUSTER_CHECK_NONE;
    bool result = check_graph_validate_lists(graph);
    const uint32_t count = graph->count;
    const bool columns = graph->types != NULL && graph->argument_offsets != NULL && graph->argument_counts != NULL && graph->members != NULL;
    _Alignas(64) uint8_t table[BUSTER_CHECK_CLASS_OPERATORS];
    check_graph_classes(table);
    const __m512i classes_low = _mm512_load_si512(table);
    const __m512i classes_high = _mm512_load_si512(table + 64);
    if (count != 0)
    {
        pure[count] = 1;
    }
    uint32_t node = 0;
    for (; result && columns && count - node >= BUSTER_CHECK_VALIDATE_LANES; node += BUSTER_CHECK_VALIDATE_LANES)
    {
        const bool block = check_graph_validate_block(graph, node, pure, state, classes_low, classes_high);
        state->vector_blocks += block;
        for (uint32_t lane = 0; result && !block && lane < BUSTER_CHECK_VALIDATE_LANES; lane += 1)
        {
            result = check_graph_validate_node(graph, node + lane, pure, state);
            *failed = result ? BUSTER_CHECK_NONE : node + lane;
        }
    }
    for (; result && node < count; node += 1)
    {
        result = check_graph_validate_node(graph, node, pure, state);
        *failed = result ? BUSTER_CHECK_NONE : node;
    }
    result = result && check_graph_validate_consumed(graph, state);
    return result;
}

static bool check_graph_system_token(const CheckGraph* graph, uint32_t token)
{
    const bool system = graph->system_tokens != NULL && token < graph->system_token_count && graph->system_tokens[token] != 0;
    return system;
}

static void check_graph_action(const CheckGraph* graph, const CheckStream* stream, uint32_t node, CheckValue* arguments, CheckAction* action)
{
    *action = (CheckAction){
        .op = graph->ops[node],
        .type = graph->types != 0 ? graph->types[node] : 0,
        .member = graph->members != 0 ? graph->members[node] : 0,
        .node = node,
        .tag_completions = graph->tag_completions,
        .transparent_unions = graph->transparent_unions,
        .tag_completion_count = graph->tag_completion_count,
        .source_position = graph->source_positions != 0 ? graph->source_positions[node] : BUSTER_CHECK_NONE,
        .extensions = graph->extensions | (check_graph_system_token(graph, graph->tokens != NULL ? graph->tokens[node] : BUSTER_CHECK_NONE) ? BUSTER_CHECK_EXTENSION_SYSTEM_HEADER : 0),
        .fp_environment = graph->fp_environments != NULL ? graph->fp_environments[node] : 0,
    };
    if (action->op == CHECK_OP_MEMBER && graph->vector_member_count != 0)
    {
        uint32_t first = 0;
        uint32_t last = graph->vector_member_count;
        while (first < last)
        {
            const uint32_t middle = first + (last - first) / 2;
            if (graph->vector_members[middle].node < node)
            {
                first = middle + 1;
            }
            else
            {
                last = middle;
            }
        }
        if (first < graph->vector_member_count && graph->vector_members[first].node == node)
        {
            action->vector_member = &graph->vector_members[first].member;
        }
    }
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
        action->operands[0] = *check_graph_leaf_of(graph, node);
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
    stream->diagnostic_operands[node] = result->diagnostic_operand;
    stream->warnings[node] = result->warnings;
    stream->member_offsets[node] = result->member_offset;
}

// check_graph_run's marks for the vector scheduler, in its `pure` column: a node a region may reorder (check_graph_pure),
// and a leaf check_graph_region completed before it scheduled the rest.
#define BUSTER_CHECK_MARK_PURE 0x01u
#define BUSTER_CHECK_MARK_DONE 0x02u

// Whether the vector path completes `node` as a leaf (check_graph_leaf): a VALUE without an overload set, about 45% of
// sqlite3.c's body nodes.
static bool check_graph_is_leaf(const CheckGraph* graph, uint32_t node)
{
    return graph->ops[node] == CHECK_OP_VALUE && check_graph_leaf_of(graph, node)->overload_head == 0;
}

// Such a leaf's record without its action: what check_graph_scalar_node gives it, which its own leaf decides alone, since a
// VALUE action reads no operand and interns no type. That is the leaf with its type as operand 0's conversion, or for an
// invalid type, that type's diagnostic and a zero value (check_action_compute skips the operator then); its diagnostic
// operand is the run's zero. The vector path writes it in node order, beside its neighbours' records, whose cache lines a
// separate pass of leaves would write a second time (on sqlite3.c, a sixth of the graph's time).
static void check_graph_leaf(const TypeTable* types, const CheckGraph* graph, CheckStream* stream, uint32_t node)
{
    const CheckValue* leaf = check_graph_leaf_of(graph, node);
    const bool valid = type_is_valid(types, leaf->type);
    stream->values[node] = valid ? *leaf : (CheckValue){ 0 };
    stream->conversions[node] = (CheckNodeConversions){ .operands = { { .type = leaf->type } } };
    stream->diagnostics[node] = valid ? CHECK_DIAG_NONE : CHECK_DIAG_INVALID_TYPE;
    stream->warnings[node] = 0;
    stream->member_offsets[node] = 0;
    stream->statistics.vector_nodes += 1;
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

static void check_x86_action(TypeTable* types, const CheckX86Call* call, const CheckAction* action, CheckBuiltinX86Result* checked)
{
    const uint32_t node = action->node;
    CheckActionResult ordinary_result;
    CheckActionResult* result = &ordinary_result;
    CheckAction ordinary = *action;
    ordinary.op = CHECK_OP_CALL;
    ordinary.operands[0] = (CheckValue){ .type = action->type, .category = CHECK_FUNCTION };
    *result = (CheckActionResult){ 0 };
    if (!type_is_valid(types, action->type) || check_action_kind(types, action->type) != BUSTER_TYPE_KIND_FUNCTION)
    {
        check_action_diagnose(result, CHECK_DIAG_INVALID_TYPE, 0);
    }
    else
    {
        check_action_call(types, &ordinary, result);
        result->conversions[0] = (CheckConversion){ 0 };
    }
    CheckValue converted[BUSTER_CHECK_BUILTIN_X86_ARGUMENTS];
    for (uint32_t i = 0; result->diagnostic == CHECK_DIAG_NONE && i < action->argument_count &&
                         i < BUSTER_CHECK_BUILTIN_X86_ARGUMENTS; i += 1)
    {
        CheckAction operand = *action;
        operand.operands[0] = action->arguments[i];
        CheckActionResult temporary = { 0 };
        converted[i] = check_action_operand(types, &operand, &temporary, 0);
        check_action_conversion(types, converted + i, action->argument_conversions[i].type, 0,
                                temporary.conversions, action->node);
        if (temporary.diagnostic != CHECK_DIAG_NONE)
        {
            check_action_diagnose(result, temporary.diagnostic, i);
        }
    }
    const CheckBuiltinX86Action input = {
        .name = call->info->name,
        .name_bytes = call->info->name_length,
        .arguments = converted,
        .argument_count = action->argument_count,
        .required_ice_arguments = call->required_ice_arguments,
        .ice_arguments = call->ice_arguments,
        .node = node,
        .typed_call = *result,
        .reachability = call->unselected_context ? CHECK_BUILTIN_X86_REACHABLE : call->reachability,
        // Exact constraint-scope attestation is supplied by the frozen catalogue adapter below.
        .name_in_scope = builtin_intrinsic_own_main_prototype_in_scope(call->info->name, call->info->name_length),
        .constant_evaluated_context = call->constant_evaluated_context,
        .runtime_diagnostic_suppressed = call->runtime_diagnostic_suppressed,
    };
    check_builtin_x86(types, &input, checked, 1);
}

static void check_graph_x86_action(TypeTable* types, const CheckGraph* graph, CheckStream* stream, const CheckAction* action, CheckActionResult* result)
{
    const uint32_t node = action->node;
    const CheckX86Call* call = graph->x86_calls + stream->x86_count;
    CheckBuiltinX86Result checked;
    check_x86_action(types, call, action, &checked);
    *result = checked.action;
    CheckBuiltinX86Effect* effect = stream->x86 + stream->x86_count++;
    *effect = (CheckBuiltinX86Effect){
        .node = node,
        .info = call->info,
        .status = checked.status,
        .constraint = checked.constraint,
        .checked_ice_arguments = checked.checked_ice_arguments,
        .matched_rules = checked.matched_rules,
        .unsupported = checked.unsupported,
        .range_event_count = checked.range_event_count,
    };
    memcpy(effect->range_events, checked.range_events, sizeof(effect->range_events));
}

#define BUSTER_CHECK_ALIGNMENT_STEPS 32u

// A step of an atomic object's address, from its base out: a member's or an element's byte offset (a member's also makes
// the alignment a declaration's, as getFieldAlignmentSource does), or an explicit cast to a pointer type.
typedef struct CheckAlignmentStep CheckAlignmentStep;
struct CheckAlignmentStep
{
    uint64_t offset;
    TypeId cast;
    bool member;
};

// The alignment clang's code generation gives an atomic builtin's object (EmitPointerWithAlignment, CGExpr.cpp), from the
// pointer argument's subgraph. A pointer value gets its pointee type's alignment (an aligned typedef's, which may be less);
// a cast between pointer types its operand's, but an explicit cast its own pointee type's where the operand's came from a
// type rather than a declaration; pointer arithmetic its pointer's at the index's offset, or at the element's size for an
// index that isn't a constant (emitPointerArithmetic); `&` of an lvalue and an array's decay the lvalue's address's: an
// object's its declaration's (getDeclAlign, from the witness), `*p` the pointer's, a member its base's at the member's
// offset (a packed record's is 1), and an element its array's or its pointer's as pointer arithmetic has it. A member's or
// an element's alignment is a declaration's (getFieldAlignmentSource). The steps are read down to the base, then applied
// from it out. A member's offset is its whole offset from its record, where clang applies each anonymous member's on the
// way. Returns the alignment's log2, or UINT32_MAX for a step it doesn't model, an object the witness doesn't give, or more
// than BUSTER_CHECK_ALIGNMENT_STEPS steps.
static uint32_t check_graph_atomic_alignment(const TypeTable* types, const CheckGraph* graph, const CheckStream* stream, uint32_t node, const CheckBuiltinCall* witness)
{
    CheckAlignmentStep steps[BUSTER_CHECK_ALIGNMENT_STEPS];
    uint32_t count = 0;
    uint32_t result = UINT32_MAX;
    bool lvalue = false;
    bool declaration = false;
    bool done = false;
    while (!done && node < stream->count && count < BUSTER_CHECK_ALIGNMENT_STEPS)
    {
        const CheckOp op = (CheckOp)graph->ops[node];
        const TypeId type = stream->values[node].type;
        const uint8_t kind = check_action_kind(types, type);
        const uint32_t operand = graph->operand0[node];
        const uint32_t second = graph->operand1[node];
        if (!lvalue && type_kind_is_array(kind))
        {
            lvalue = true;
        }
        else if (op == CHECK_OP_SELECT && operand < node)
        {
            node = operand;
        }
        else if (!lvalue && kind != BUSTER_TYPE_KIND_POINTER)
        {
            done = true;
        }
        else if (!lvalue && op == CHECK_OP_ADDRESS && operand < node)
        {
            node = operand;
            lvalue = true;
        }
        else if (!lvalue && op == CHECK_OP_CAST && operand < node)
        {
            const TypeId inner = stream->values[operand].type;
            const uint8_t inner_kind = check_action_kind(types, inner);
            const bool object = inner_kind == BUSTER_TYPE_KIND_POINTER &&
                                check_action_kind(types, type_element(types, type_strip_aligned(types, inner))) != BUSTER_TYPE_KIND_VOID;
            if (object || type_kind_is_array(inner_kind))
            {
                steps[count++] = (CheckAlignmentStep){ .cast = type };
                node = operand;
            }
            else
            {
                // From void *, an integer or anything else, the cast's own pointee type's.
                result = type_alignment_log2(types, type_element(types, type_strip_aligned(types, type)));
                done = true;
            }
        }
        else if (((lvalue && op == CHECK_OP_SUBSCRIPT) || (!lvalue && (op == CHECK_OP_ADD || op == CHECK_OP_SUBTRACT))) && operand < node && second < node)
        {
            // The pointer is a subtraction's left operand, and either of an addition's or a subscript's.
            const uint8_t left = check_action_kind(types, stream->values[operand].type);
            const bool reverse = op != CHECK_OP_SUBTRACT && left != BUSTER_TYPE_KIND_POINTER && !type_kind_is_array(left);
            const uint32_t base = reverse ? second : operand;
            const CheckValue* index = stream->values + (reverse ? operand : second);
            const TypeId element = op == CHECK_OP_SUBSCRIPT ? type : type_element(types, type_strip_aligned(types, type));
            const uint8_t element_kind = check_action_kind(types, element);
            const uint64_t size = element_kind == BUSTER_TYPE_KIND_VOID || element_kind == BUSTER_TYPE_KIND_FUNCTION ? 1 : type_size(types, element);
            int64_t number = 0;
            uint64_t offset = size;
            if (check_action_i64(types, index, &number))
            {
                offset = size * (number < 0 ? 0 - (uint64_t)number : (uint64_t)number);
            }
            steps[count++] = (CheckAlignmentStep){ .offset = offset };
            node = base;
            lvalue = type_kind_is_array(check_action_kind(types, stream->values[base].type));
        }
        else if (!lvalue)
        {
            result = type_alignment_log2(types, type_element(types, type_strip_aligned(types, type)));
            done = true;
        }
        else if (op == CHECK_OP_VALUE)
        {
            const bool witnessed = witness != NULL && witness->base_alignment != 0 && graph->tokens != NULL && graph->tokens[node] == witness->base_token;
            result = witnessed ? (uint32_t)witness->base_alignment - 1 : UINT32_MAX;
            declaration = true;
            done = true;
        }
        else if (op == CHECK_OP_DEREFERENCE && operand < node)
        {
            node = operand;
            lvalue = false;
        }
        else if ((op == CHECK_OP_MEMBER || op == CHECK_OP_ARROW) && operand < node && (stream->values[node].flags & BUSTER_CHECK_BIT_FIELD) == 0)
        {
            TypeId record = type_strip_aligned(types, stream->values[operand].type);
            record = op == CHECK_OP_ARROW && check_action_kind(types, record) == BUSTER_TYPE_KIND_POINTER ?
                     type_strip_aligned(types, type_element(types, record)) : record;
            uint64_t offset = 0;
            uint32_t qualifiers = 0;
            CheckActionResult found = { 0 };
            const uint8_t record_kind = type_kind(types, record);
            const uint32_t member = record_kind == BUSTER_TYPE_KIND_STRUCT || record_kind == BUSTER_TYPE_KIND_UNION ?
                                    check_action_member_lookup(types, record, graph->members[node], &offset, &qualifiers, &found) : 0;
            done = member == 0 || offset % 8 != 0;
            steps[count] = (CheckAlignmentStep){ .offset = offset / 8, .member = true };
            count += !done;
            node = operand;
            lvalue = op == CHECK_OP_MEMBER;
        }
        else
        {
            done = true;
        }
    }
    for (uint32_t i = count; result != UINT32_MAX && i-- > 0;)
    {
        if (steps[i].cast != 0 && !declaration)
        {
            result = type_alignment_log2(types, type_element(types, type_strip_aligned(types, steps[i].cast)));
        }
        else if (steps[i].offset != 0)
        {
            const uint32_t low = (uint32_t)__builtin_ctzll(steps[i].offset);
            result = low < result ? low : result;
        }
        declaration |= steps[i].member;
    }
    return result;
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
        result->warnings |= (checked.warnings & BUSTER_CHECK_BUILTIN_MISC_WARN_QUALIFIERS) != 0 ? CHECK_WARNING_DISCARDED_QUALIFIERS : 0;
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
        const CheckBuiltinMathKind math_kind = check_builtin_math_kind((CBuiltinId)action->member);
        if (math_kind != CHECK_BUILTIN_MATH_NONE)
        {
            const CheckBuiltinMathAction input = {
                .id = (CBuiltinId)action->member,
                .arguments = action->arguments,
                .argument_count = action->argument_count,
                .node = node,
                .tag_completions = action->tag_completions,
                .tag_completion_count = action->tag_completion_count,
                .source_position = action->source_position,
                .extensions = action->extensions,
            };
            CheckBuiltinMathResult checked;
            check_builtin_math(types, &input, &checked, 1);
            *result = checked.action;
            const TypeId errno_pointer = checked.may_set_errno && result->diagnostic == CHECK_DIAG_NONE ? type_pointer(types, BUSTER_TYPE_INT) : 0;
            if (checked.may_set_errno && result->diagnostic == CHECK_DIAG_NONE && errno_pointer == 0)
            {
                check_action_diagnose(result, CHECK_DIAG_RESOURCE, 0);
            }
            bool fp_conversion = false;
            for (uint32_t i = 0; i < action->argument_count && i < 2; i += 1)
            {
                action->argument_conversions[i] = checked.argument_conversions[i];
                fp_conversion |= (action->fp_environment & BUSTER_CHECK_FP_ENV_ACCESS) != 0 &&
                                 check_action_fp_conversion(types, action->arguments[i].type, checked.argument_conversions[i].type);
            }
            if (fp_conversion)
            {
                result->value.flags |= BUSTER_CHECK_FP_ENVIRONMENT;
            }
            const bool dynamic = (result->value.flags & BUSTER_CHECK_FP_ENVIRONMENT) != 0;
            stream->floating[stream->floating_count++] = (CheckBuiltinFloatEffect){
                .node = node,
                .converted_arguments = { checked.converted_arguments[0], checked.converted_arguments[1] },
                .math_kind = math_kind,
                .errno_pointer_type = errno_pointer,
                .floating_type = checked.floating_type,
                .evaluated_operands = checked.evaluated_operands,
                .definite_operands = checked.evaluated_operands,
                .unsupported = checked.unsupported,
                .runtime_builtin = dynamic ? input.id : checked.runtime_builtin,
                .folded = checked.folded && !dynamic,
                .compiler_barrier = checked.compiler_barrier || fp_conversion,
                .reads_fp_environment = checked.reads_fp_environment || fp_conversion,
                .may_raise_fp_exceptions = checked.may_raise_fp_exceptions || fp_conversion,
                .may_raise_inexact = checked.may_raise_inexact || fp_conversion,
                .may_set_errno = checked.may_set_errno,
            };
        }
        else if (action->member == CBUILTIN_FLT_ROUNDS)
        {
            const CheckBuiltinRoundsAction input = {
                .id = (CBuiltinId)action->member,
                .arguments = action->arguments,
                .argument_count = action->argument_count,
                .node = node,
            };
            CheckBuiltinRoundsResult checked;
            check_builtin_rounds(&input, &checked, 1);
            *result = checked.action;
            stream->floating[stream->floating_count++] = (CheckBuiltinFloatEffect){
                .node = node,
                .evaluated_operands = checked.evaluated_operands,
                .definite_operands = checked.evaluated_operands,
                .runtime_builtin = checked.runtime_builtin,
                .compiler_barrier = checked.compiler_barrier,
                .reads_fp_environment = checked.reads_fp_environment,
            };
        }
        else
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
            result->warnings |= (checked.warnings & BUSTER_CHECK_BUILTIN_FLOAT_WARN_QUALIFIERS) != 0 ? CHECK_WARNING_DISCARDED_QUALIFIERS : 0;
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
    }
    else if (action->op == CHECK_OP_BUILTIN_ATOMIC)
    {
        const uint32_t walked = action->argument_count != 0 ?
                                check_graph_atomic_alignment(types, graph, stream, graph->argument_nodes[graph->argument_offsets[node]], witness) : UINT32_MAX;
        const CheckBuiltinAtomicAction input = {
            .id = (CBuiltinId)action->member,
            .arguments = action->arguments,
            .argument_count = action->argument_count,
            .node = node,
            .tag_completions = action->tag_completions,
            .tag_completion_count = action->tag_completion_count,
            .source_position = action->source_position,
            .extensions = action->extensions,
            .minimum_alignment = walked < 64 ? UINT64_C(1) << walked : 0,
            .alignment_known = walked < 64,
        };
        CheckBuiltinAtomicResult checked;
        if (check_builtin_atomic_rmw_rule(input.id) != NULL)
        {
            // A read/modify/write's own rules (the orders' ICE proofs from the source's analysis); its atomic part is what
            // the effect records.
            const CheckBuiltinAtomicRmwAction rmw = {
                .source = input,
                .order_ice_arguments = check_builtin_atomic_rmw_order_ice(input.id, action->arguments, action->argument_count),
            };
            CheckBuiltinAtomicRmwResult full;
            check_builtin_atomic_rmw(types, &rmw, &full, 1);
            checked = full.atomic;
        }
        else
        {
            check_builtin_atomic(types, &input, &checked, 1);
        }
        *result = checked.action;
        result->warnings |= (checked.warnings & BUSTER_CHECK_BUILTIN_ATOMIC_WARN_QUALIFIERS) != 0 ? CHECK_WARNING_DISCARDED_QUALIFIERS : 0;
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
    else if (action->op == CHECK_OP_BUILTIN_VECTOR)
    {
        const CheckBuiltinVectorAction input = {
            .id = (CBuiltinId)action->member,
            .arguments = action->arguments,
            .argument_count = action->argument_count,
            .destination_type = action->type,
            .node = node,
            .tag_completions = action->tag_completions,
            .tag_completion_count = action->tag_completion_count,
            .source_position = action->source_position,
            .extensions = action->extensions,
            .shuffle_index_ice = witness != NULL ? witness->shuffle_index_ice : NULL,
            .shuffle_index_ice_count = witness != NULL ? witness->shuffle_index_ice_count : 0,
            .allow_gnu_index_ice = (action->extensions & BUSTER_CHECK_EXTENSION_GNU) != 0,
        };
        CheckBuiltinVectorResult checked;
        check_builtin_vector(types, &input, &checked, 1);
        *result = checked.action;
        result->warnings |= (checked.warnings & BUSTER_CHECK_BUILTIN_VECTOR_WARN_QUALIFIERS) != 0 ? CHECK_WARNING_DISCARDED_QUALIFIERS : 0;
        for (uint32_t i = 0; i < action->argument_count && i < 3; i += 1)
        {
            action->argument_conversions[i] = checked.argument_conversions[i];
        }
        CheckBuiltinVectorEffect* effect = stream->vectors + stream->vector_count++;
        *effect = (CheckBuiltinVectorEffect){
            .node = node,
            .element_type = checked.element_type,
            .memory_type = checked.memory_type,
            .lane_count = checked.lane_count,
            .evaluated_argument_count = checked.evaluated_argument_count,
            .warnings = checked.warnings,
            .warning_operands = checked.warning_operands,
            .unsupported = checked.unsupported,
            .runtime_builtin = checked.runtime_builtin,
            .constraint = checked.constraint,
            .store_conversion = checked.store_conversion,
            .reads_memory = checked.reads_memory,
            .writes_memory = checked.writes_memory,
            .nondeterministic = checked.nondeterministic,
            .constant_evaluation_unmodeled = checked.constant_evaluation_unmodeled,
            .reads_fp_environment = checked.reads_fp_environment,
            .may_raise_fp_exceptions = checked.may_raise_fp_exceptions,
            .bool_splat_signed_true = checked.bool_splat_signed_true,
        };
        memcpy(effect->converted_arguments, checked.converted_arguments, sizeof(effect->converted_arguments));
        if (checked.reads_fp_environment || checked.may_raise_fp_exceptions)
        {
            result->value.flags |= BUSTER_CHECK_FP_ENVIRONMENT;
        }
    }
    else if (action->op == CHECK_OP_BUILTIN_BIT_CAST)
    {
        const CheckBuiltinBitCastAction input = { .id = (CBuiltinId)action->member, .destination_type = action->type,
            .arguments = action->arguments, .argument_count = action->argument_count, .node = node,
            .tag_completions = action->tag_completions, .tag_completion_count = action->tag_completion_count,
            .source_position = action->source_position,
            .source_proof = witness != NULL ? witness->bit_cast_proof : CHECK_BUILTIN_BIT_CAST_PROOF_NONE,
            .source_bytes = witness != NULL ? witness->bit_cast_bytes : NULL,
            .source_byte_count = witness != NULL ? witness->bit_cast_byte_count : 0 };
        CheckBuiltinBitCastResult checked;
        check_builtin_bit_cast(types, &input, &checked, 1);
        *result = checked.action;
        CheckBuiltinBitCastEffect* effect = stream->bit_casts + stream->bit_cast_count++;
        *effect = (CheckBuiltinBitCastEffect){ .node = node, .source_type = checked.source_type,
            .representation_type = checked.representation_type, .byte_count = checked.byte_count,
            .evaluated_operands = checked.evaluated_operands, .unsupported = checked.unsupported,
            .runtime_builtin = checked.runtime_builtin, .payload_count = checked.payload_count,
            .representation_known = checked.representation_known, .folded = checked.folded,
            .runtime_required = checked.runtime_required, .volatile_read = checked.volatile_read };
        memcpy(effect->bytes, checked.bytes, sizeof(effect->bytes));
        if ((checked.unsupported & BUSTER_CHECK_BUILTIN_BIT_CAST_RESULT_TYPE) != 0)
        {
            // The module cannot recover which qualified array spelling produced this canonical TypeId.
            // Retain its representation descriptor, but do not invent a type for subsequent constraints.
            check_action_diagnose(result, CHECK_DIAG_UNSUPPORTED, 1);
        }
    }
    else if (action->op == CHECK_OP_BUILTIN_X86)
    {
        check_graph_x86_action(types, graph, stream, action, result);
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
            .complex_constant = witness != NULL && witness->complex_constant,
        };
        input.constant_p_context_sensitive = input.id == CBUILTIN_CONSTANT_P &&
            (action->fp_environment != 0 || (action->argument_count == 1 && (action->arguments[0].flags & BUSTER_CHECK_FP_ENVIRONMENT) != 0));
        if (action->argument_count != 0)
        {
            const CheckValue* value = action->arguments;
            input.complex_constant |= value->category == CHECK_RVALUE && value->constant_kind == CHECK_CONSTANT_COMPLEX &&
                                      value->fault == CHECK_FAULT_NONE && (value->flags & BUSTER_CHECK_SIDE_EFFECT) == 0;
        }
        if (witness != NULL)
        {
            memcpy(input.object_size_known, witness->object_size_known, sizeof(input.object_size_known));
            memcpy(input.object_size_source_constant, witness->object_size_source_constant, sizeof(input.object_size_source_constant));
            memcpy(input.object_sizes, witness->object_sizes, sizeof(input.object_sizes));
        }
        CheckBuiltinQueryResult checked;
        check_builtin_query(types, &input, &checked, 1);
        *result = checked.action;
        result->warnings |= (checked.warnings & BUSTER_CHECK_BUILTIN_MISC_WARN_QUALIFIERS) != 0 ? CHECK_WARNING_DISCARDED_QUALIFIERS : 0;
        for (uint32_t i = 0; i < action->argument_count && i < 2; i += 1)
        {
            action->argument_conversions[i] = checked.argument_conversions[i];
        }
        stream->queries[stream->query_count++] = (CheckBuiltinQueryEffect){
            .node = node,
            .runtime_value = checked.runtime_value,
            .evaluated_operands = checked.evaluated_operands,
            .warnings = checked.warnings,
            .warning_operands = checked.warning_operands,
            .mode = checked.mode,
            .known = checked.known,
            .constraint = checked.constraint,
            .source_proof = checked.source_proof,
            .runtime_plan = checked.runtime_plan,
            .runtime_effects = checked.runtime_effects,
            .unsupported = checked.unsupported,
        };
    }
    if (result->diagnostic == CHECK_DIAG_NONE)
    {
        uint32_t evaluated = action->op == CHECK_OP_BUILTIN_MISC ? stream->misc[stream->misc_count - 1].evaluated_operands :
                             action->op == CHECK_OP_BUILTIN_FLOAT ? stream->floating[stream->floating_count - 1].evaluated_operands :
                             action->op == CHECK_OP_BUILTIN_ATOMIC ? stream->atomics[stream->atomic_count - 1].evaluated_operands :
                             action->op == CHECK_OP_BUILTIN_VECTOR ? (1u << stream->vectors[stream->vector_count - 1].evaluated_argument_count) - 1 :
                             action->op == CHECK_OP_BUILTIN_X86 ? UINT32_MAX :
                             action->op == CHECK_OP_BUILTIN_BIT_CAST ? stream->bit_casts[stream->bit_cast_count - 1].evaluated_operands :
                             stream->queries[stream->query_count - 1].evaluated_operands;
        const bool strict_comparison = action->op == CHECK_OP_BUILTIN_FLOAT &&
                                       (action->fp_environment & BUSTER_CHECK_FP_ENV_ACCESS) != 0 &&
                                       (action->member == CBUILTIN_FPCLASSIFY || action->member == CBUILTIN_ISINF_SIGN);
        if (strict_comparison)
        {
            result->value.flags |= BUSTER_CHECK_FP_ENVIRONMENT;
            stream->floating[stream->floating_count - 1].may_raise_fp_exceptions = true;
        }
        if (action->op == CHECK_OP_BUILTIN_FLOAT && action->member == CBUILTIN_FPCLASSIFY && action->argument_count == 6 &&
            (strict_comparison || (action->arguments[5].flags & BUSTER_CHECK_FP_ENVIRONMENT) != 0))
        {
            CheckBuiltinFloatEffect* effect = stream->floating + stream->floating_count - 1;
            effect->selected_selector = BUSTER_CHECK_BUILTIN_FLOAT_NO_SELECTOR;
            effect->evaluation = CHECK_BUILTIN_FLOAT_EVALUATION_FPCLASSIFY_RUNTIME;
            effect->evaluated_operands = 63;
            effect->definite_operands = 48;
            evaluated = 63;
        }
        bool fp_conversion = false;
        for (uint32_t i = 0; i < action->argument_count; i += 1)
        {
            if (action->op == CHECK_OP_BUILTIN_X86 || (i < 32 && (evaluated & (1u << i)) != 0))
            {
                result->value.flags |= action->arguments[i].flags & BUSTER_CHECK_FP_ENVIRONMENT;
                fp_conversion |= (action->fp_environment & BUSTER_CHECK_FP_ENV_ACCESS) != 0 &&
                                 check_action_fp_conversion(types, action->arguments[i].type, action->argument_conversions[i].type);
            }
        }
        result->value.flags |= fp_conversion ? BUSTER_CHECK_FP_ENVIRONMENT : 0;
        if (action->op == CHECK_OP_BUILTIN_FLOAT && (result->value.flags & BUSTER_CHECK_FP_ENVIRONMENT) != 0)
        {
            CheckBuiltinFloatEffect* effect = stream->floating + stream->floating_count - 1;
            effect->folded = false;
            effect->runtime_builtin = (CBuiltinId)action->member;
            effect->compiler_barrier = true;
            effect->reads_fp_environment = true;
            effect->may_raise_fp_exceptions |= fp_conversion;
            effect->may_raise_inexact |= fp_conversion;
        }
    }
}

#include "c_check_overload_graph.c"
#include "c_check_region_graph.c"

static void check_graph_scalar_node(TypeTable* types, const CheckGraph* graph, CheckStream* stream, uint32_t node, CheckValue* arguments, Arena* scratch, bool scalar)
{
    (void)scalar;

    CheckAction action;
    CheckActionResult result;
    check_graph_action(graph, stream, node, arguments, &action);
    action.scalar = scalar;
    // Without overload candidates every value's overload head is zero (check_graph_validate checks the leaves', and the
    // actions only preserve their operands'): no call is contextual, and no operand takes an overload (#530).
    const bool overloads = graph->overload_candidate_count != 0;
    const bool overload = action.op == CHECK_OP_CALL && stream->overload_count < graph->overload_count && graph->overload_calls[stream->overload_count].node == node;
    bool contextual_call = overload || (action.op == CHECK_OP_CALL && action.operands[0].overload_head != 0);
    for (uint32_t i = 0; overloads && action.op == CHECK_OP_CALL && i < action.argument_count; i += 1)
    {
        contextual_call |= action.arguments[i].overload_head != 0;
    }
    const CheckDiagnostic contextual = contextual_call || !overloads ? CHECK_DIAG_NONE : check_graph_overload_operands(types, graph, stream, &action, scratch);
    if (contextual != CHECK_DIAG_NONE)
    {
        result = (CheckActionResult){ .diagnostic = contextual };
    }
    else if (contextual_call)
    {
        const uint32_t head = overload ? graph->overload_calls[stream->overload_count].head :
                              action.operands[0].overload_head != 0 ? action.operands[0].overload_head - 1 : BUSTER_CHECK_NONE;
        const bool captured = action.operands[0].overload_head == 0 ||
                              (overload && action.operands[0].overload_head == head + 1);
        if (!captured)
        {
            result = (CheckActionResult){ .diagnostic = CHECK_DIAG_INVALID_TYPE };
        }
        else
        {
            const CheckOverloadResult selected = check_graph_overload_call(types, graph, stream, &action, head, scratch, BUSTER_SCALAR_PATH(scalar), &result);
            if (overload)
            {
                stream->overloads[stream->overload_count++] = (CheckOverloadEffect){ .node = node, .selected = selected };
            }
        }
    }
    else if (action.op == CHECK_OP_BUILTIN_UNARY || action.op == CHECK_OP_BUILTIN_OVERFLOW)
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
        CheckBuiltinTypedOverflowResult typed = { 0 };
        if (builtin.id == CBUILTIN_UMULL_OVERFLOW)
        {
            check_builtin_typed_overflow(types, &builtin, &typed, 1, true);
            checked = typed.builtin;
        }
        else
        {
            check_builtins(types, &builtin, &checked, 1, true);
        }
        result = checked.action;
        check_action_fp_environment(types, &action, &result);
        result.warnings |= (typed.warnings & BUSTER_CHECK_BUILTIN_TYPED_OVERFLOW_WARN_QUALIFIERS) != 0 ? CHECK_WARNING_DISCARDED_QUALIFIERS : 0;
        if (checked.store && result.diagnostic == CHECK_DIAG_NONE)
        {
            stream->effects[stream->effect_count++] = (CheckStoreEffect){
                .node = node,
                .value = checked.store_value,
                .destination = checked.store_destination,
                .runtime_builtin = builtin.id == CBUILTIN_UMULL_OVERFLOW ? typed.runtime_builtin : builtin.id,
                .evaluated_operands = builtin.id == CBUILTIN_UMULL_OVERFLOW ? typed.evaluated_operands : 7,
                .warnings = typed.warnings,
                .warning_operands = typed.warning_operands,
                .unsupported = typed.unsupported,
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
    if (action.op == CHECK_OP_STATEMENT_VALUE)
    {
        check_graph_region_effects(types, graph, stream, node, scratch, &result);
    }
    check_graph_store(stream, node, &result);
    stream->statistics.scalar_nodes += 1;
}

// Count/place/fill a stable level order inside one mutation-free postorder region. Dependencies before begin are
// already complete. The region's leaves complete first, in node order (check_graph_leaf), and `marks` keeps them out of
// the schedule. Relative heights keep all histogram clears together linear even for many tiny regions.
static void check_graph_region(TypeTable* types, const CheckGraph* graph, CheckStream* stream, uint8_t* marks, uint32_t begin, uint32_t end, uint32_t batch_size,
                               uint32_t* levels, uint32_t* counts, uint32_t* order)
{
    uint32_t maximum = 0;
    for (uint32_t node = begin; node < end; node += 1)
    {
        // A pure leaf has no overload set (check_graph_pure).
        if (graph->ops[node] == CHECK_OP_VALUE)
        {
            check_graph_leaf(types, graph, stream, node);
            marks[node] |= BUSTER_CHECK_MARK_DONE;
        }
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
            if (operand != BUSTER_CHECK_NONE && operand >= begin && (marks[operand] & BUSTER_CHECK_MARK_DONE) == 0)
            {
                const uint32_t candidate = levels[operand] + 1;
                level = candidate > level ? candidate : level;
            }
        }
        levels[node] = level;
        maximum = level > maximum ? level : maximum;
    }
    memset(counts, 0, (size_t)(maximum + 2) * sizeof(*counts));
    uint32_t scheduled = 0;
    for (uint32_t node = begin; node < end; node += 1)
    {
        const uint32_t waiting = (marks[node] & BUSTER_CHECK_MARK_DONE) == 0;
        counts[levels[node] + 1] += waiting;
        scheduled += waiting;
    }
    for (uint32_t level = 1; level <= maximum + 1; level += 1)
    {
        counts[level] += counts[level - 1];
    }
    // Fill backwards, decrementing ends, so the resulting starts remain available and each level stays stable.
    for (uint32_t node = end; node != begin;)
    {
        node -= 1;
        if ((marks[node] & BUSTER_CHECK_MARK_DONE) == 0)
        {
            const uint32_t level = levels[node];
            counts[level + 1] -= 1;
            order[counts[level + 1]] = node;
        }
    }
    for (uint32_t level = 0; level <= maximum; level += 1)
    {
        const uint32_t first = counts[level + 1];
        const uint32_t last = level == maximum ? scheduled : counts[level + 2];
        for (uint32_t at = first; at < last;)
        {
            const uint32_t count = last - at < batch_size ? last - at : batch_size;
            if (count == 1)
            {
                // A singleton has no parallel work. Avoid batch packing and wide table lookups on chains.
                check_graph_scalar_node(types, graph, stream, order[at], 0, 0, false);
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
    stream->statistics.levels += scheduled != 0 ? maximum + 1 : 0;
}

static void check_graph_report(const CheckGraph* graph, CheckStream* stream, uint32_t node, CheckMessage message, CheckDiagnostic diagnostic, CheckFault fault)
{
    uint32_t token = graph->tokens != 0 ? graph->tokens[node] : node;
    const uint32_t operand = message == CHECK_MESSAGE_ACTION ? stream->diagnostic_operands[node] : 0;
    if (graph->ops[node] == CHECK_OP_CALL && graph->argument_tokens != NULL && operand != 0 && operand <= graph->argument_counts[node])
    {
        token = graph->argument_tokens[graph->argument_offsets[node] + operand - 1];
    }
    stream->reports[stream->report_count] = (CheckReport){
        .node = node,
        .token = token,
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

// Whether `node` has a warning or a GNU constant where an ICE is required, which check_graph_warning_contexts must place.
static bool check_graph_warning_needed(const CheckGraph* graph, const CheckStream* stream, uint32_t node)
{
    const uint32_t context = graph->contexts != NULL ? graph->contexts[node] : 0;
    return stream->warnings[node] != 0 ||
           ((context & (BUSTER_CHECK_CONTEXT_ICE | BUSTER_CHECK_CONTEXT_ASSERT)) != 0 &&
            (context & BUSTER_CHECK_CONTEXT_GNU_CONSTANT) != 0 && (stream->values[node].flags & BUSTER_CHECK_ICE) == 0 &&
            check_action_integer_constant(stream->values + node, BUSTER_CHECK_EXTENSION_GNU_CONSTANT));
}

static void check_graph_warning_contexts(const TypeTable* types, const CheckGraph* graph, CheckStream* stream, uint8_t* states, bool scalar)
{
    (void)scalar;

    bool needed = false;
    uint32_t node = 0;
    // The vector path reads only the warnings and the contexts, 16 at a time, and the values of the nodes whose GNU context
    // may need them (#530); the scalar path tests every node.
    for (; !BUSTER_SCALAR_PATH(scalar) && !needed && graph->count - node >= BUSTER_CHECK_VALIDATE_LANES; node += BUSTER_CHECK_VALIDATE_LANES)
    {
        __mmask16 candidates = _mm512_test_epi32_mask(_mm512_loadu_si512(stream->warnings + node), _mm512_set1_epi32(-1));
        if (graph->contexts != NULL)
        {
            const __m128i contexts = _mm_loadu_si128((const __m128i*)(graph->contexts + node));
            candidates |= _mm_test_epi8_mask(contexts, _mm_set1_epi8((char)(BUSTER_CHECK_CONTEXT_ICE | BUSTER_CHECK_CONTEXT_ASSERT))) &
                          _mm_test_epi8_mask(contexts, _mm_set1_epi8((char)BUSTER_CHECK_CONTEXT_GNU_CONSTANT));
        }
        for (uint32_t lanes = candidates; !needed && lanes != 0; lanes &= lanes - 1)
        {
            needed = check_graph_warning_needed(graph, stream, node + (uint32_t)__builtin_ctz(lanes));
        }
    }
    for (; !needed && node < graph->count; node += 1)
    {
        needed = check_graph_warning_needed(graph, stream, node);
    }
    if (!needed)
    {
        return;
    }
    // The scheduler's byte scratch is dead here. Reuse it for referenced/evaluated/required-ICE bits, so a
    // reverse postorder pass can distinguish contextual GNU folding from warnings in evaluated operations.
    memset(states, 0, graph->count);
    for (uint32_t node = 0; node < graph->count; node += 1)
    {
        const uint32_t operands[] = { graph->operand0[node], graph->operand1[node], graph->operand2[node] };
        for (uint32_t i = 0; i < 3; i += 1)
        {
            if (operands[i] != BUSTER_CHECK_NONE)
            {
                states[operands[i]] |= 1;
            }
        }
        if (check_graph_has_arguments(graph->ops[node]) && graph->argument_counts != NULL)
        {
            const uint32_t first = graph->argument_offsets != NULL ? graph->argument_offsets[node] : 0;
            for (uint32_t i = 0; i < graph->argument_counts[node]; i += 1)
            {
                states[graph->argument_nodes[first + i]] |= 1;
            }
        }
    }
    for (uint32_t node = graph->count; node-- > 0;)
    {
        const uint32_t context = graph->contexts != NULL ? graph->contexts[node] : 0;
        states[node] |= (states[node] & 1) == 0 ? 2 : 0;
        states[node] |= (context & (BUSTER_CHECK_CONTEXT_ICE | BUSTER_CHECK_CONTEXT_ASSERT)) != 0 ? 4 : 0;
        const uint32_t unevaluated = CHECK_WARNING_INTEGER_OVERFLOW | CHECK_WARNING_SHIFT_COUNT_NEGATIVE |
                                     CHECK_WARNING_SHIFT_COUNT_OVERFLOW | CHECK_WARNING_SHIFT_NEGATIVE_VALUE;
        const uint32_t required = CHECK_WARNING_SHIFT_COUNT_NEGATIVE | CHECK_WARNING_SHIFT_COUNT_OVERFLOW | CHECK_WARNING_SHIFT_NEGATIVE_VALUE;
        stream->warnings[node] &= ~((states[node] & 2) == 0 ? unevaluated : (states[node] & 4) != 0 ? required : 0);
        stream->warnings[node] &= ~(graph->warning_suppressed != NULL ? graph->warning_suppressed[node] : 0);
        const CheckValue* value = stream->values + node;
        if ((context & (BUSTER_CHECK_CONTEXT_ICE | BUSTER_CHECK_CONTEXT_ASSERT)) != 0 &&
            (context & BUSTER_CHECK_CONTEXT_GNU_CONSTANT) != 0 && (value->flags & BUSTER_CHECK_ICE) == 0 &&
            check_action_integer_constant(value, BUSTER_CHECK_EXTENSION_GNU_CONSTANT))
        {
            stream->warnings[node] |= CHECK_WARNING_GNU_CONSTANT;
            stream->warning_ignored[node] |= CHECK_WARNING_GNU_CONSTANT;
        }
        const CheckOp op = graph->ops[node];
        uint32_t mask = UINT32_MAX;
        bool truth = false;
        if (op == CHECK_OP_SIZEOF || op == CHECK_OP_ALIGNOF)
        {
            mask = 0;
        }
        else if ((op == CHECK_OP_CONDITIONAL || op == CHECK_OP_LOGICAL_AND || op == CHECK_OP_LOGICAL_OR) &&
                 check_action_truth(types, stream->values + graph->operand0[node], &truth))
        {
            mask = op == CHECK_OP_CONDITIONAL ? (truth ? 3 : 5) :
                   ((op == CHECK_OP_LOGICAL_AND && !truth) || (op == CHECK_OP_LOGICAL_OR && truth)) ? 1 : 3;
        }
        const uint32_t operands[] = { graph->operand0[node], graph->operand1[node], graph->operand2[node] };
        for (uint32_t i = 0; i < 3; i += 1)
        {
            if (operands[i] != BUSTER_CHECK_NONE)
            {
                states[operands[i]] |= (states[node] & 4) | ((mask & (1u << i)) != 0 ? states[node] & 2 : 0);
            }
        }
        if (check_graph_has_arguments(op) && graph->argument_counts != NULL)
        {
            const uint32_t first = graph->argument_offsets != NULL ? graph->argument_offsets[node] : 0;
            for (uint32_t i = 0; i < graph->argument_counts[node]; i += 1)
            {
                states[graph->argument_nodes[first + i]] |= (states[node] & 4) | ((i >= 32 || (mask & (1u << i)) != 0) ? states[node] & 2 : 0);
            }
        }
    }
}

// A node's report: its action's diagnostic, or in a context, its value's fault or what the context requires of it.
static void check_graph_report_node(const CheckGraph* graph, CheckStream* stream, uint32_t node)
{
    const CheckValue* value = stream->values + node;
    const uint8_t context = graph->contexts != 0 ? graph->contexts[node] : 0;
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
        const uint32_t extensions = (context & BUSTER_CHECK_CONTEXT_GNU_CONSTANT) != 0 ? BUSTER_CHECK_EXTENSION_GNU_CONSTANT : 0;
        if (value->fault != CHECK_FAULT_NONE)
        {
            const uint32_t source = value->fault_node < graph->count ? value->fault_node : node;
            check_graph_report(graph, stream, source, CHECK_MESSAGE_CONSTANT, CHECK_DIAG_NONE, value->fault);
        }
        else if ((context & (BUSTER_CHECK_CONTEXT_ICE | BUSTER_CHECK_CONTEXT_ASSERT)) != 0 && !check_action_integer_constant(value, extensions))
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

static void check_graph_reports(const CheckGraph* graph, CheckStream* stream, CheckReport* temporary, bool scalar)
{
    (void)scalar;

    // Only a node with a diagnostic or a context reports: the vector path finds those 16 at a time and reads no other
    // node's value (a sixth of the graph's bytes, #530); the scalar path takes every node.
    uint32_t node = 0;
    for (; !BUSTER_SCALAR_PATH(scalar) && graph->count - node >= BUSTER_CHECK_VALIDATE_LANES; node += BUSTER_CHECK_VALIDATE_LANES)
    {
        __mmask16 reported = _mm512_cmpneq_epi32_mask(_mm512_loadu_si512(stream->diagnostics + node), _mm512_set1_epi32(CHECK_DIAG_NONE));
        if (graph->contexts != 0)
        {
            reported |= _mm_test_epi8_mask(_mm_loadu_si128((const __m128i*)(graph->contexts + node)), _mm_set1_epi8(-1));
        }
        for (uint32_t lanes = reported; lanes != 0; lanes &= lanes - 1)
        {
            check_graph_report_node(graph, stream, node + (uint32_t)__builtin_ctz(lanes));
        }
    }
    for (; node < graph->count; node += 1)
    {
        check_graph_report_node(graph, stream, node);
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

#include "c_check_query_runtime.c"

_Static_assert(sizeof(CheckDiagnostic) == sizeof(uint32_t), "the vector scans load diagnostics as 32-bit lanes");

// The first node whose action ran out of room, or BUSTER_CHECK_NONE: AVX-512 compares 16 diagnostics at a time, the rest and
// the scalar path one at a time.
static uint32_t check_graph_resource_node(const CheckStream* stream, uint32_t count, bool scalar)
{
    (void)scalar;

    uint32_t found = BUSTER_CHECK_NONE;
    uint32_t node = 0;
    for (; !BUSTER_SCALAR_PATH(scalar) && found == BUSTER_CHECK_NONE && count - node >= BUSTER_CHECK_VALIDATE_LANES; node += BUSTER_CHECK_VALIDATE_LANES)
    {
        const __mmask16 exhausted = _mm512_cmpeq_epi32_mask(_mm512_loadu_si512(stream->diagnostics + node), _mm512_set1_epi32(CHECK_DIAG_RESOURCE));
        found = exhausted != 0 ? node + (uint32_t)__builtin_ctz(exhausted) : BUSTER_CHECK_NONE;
    }
    for (; found == BUSTER_CHECK_NONE && node < count; node += 1)
    {
        found = stream->diagnostics[node] == CHECK_DIAG_RESOURCE ? node : BUSTER_CHECK_NONE;
    }
    return found;
}

static bool check_graph_run(TypeTable* types, const CheckGraph* graph, Arena* output, Arena* scratch, const CheckOptions* options, CheckStream* stream)
{
#if !BUSTER_HOST_VECTOR
    CheckOptions host_options = *options;
    host_options.scalar = true;
    options = &host_options;
#endif
    const uint64_t output_start = output->length;
    const uint64_t scratch_start = scratch->length;
    *stream = (CheckStream){
        .failed_node = BUSTER_CHECK_NONE,
    };
    const uint32_t count = graph->count;
    const uint32_t batch_size = options->batch_size == 0 ? BUSTER_CHECK_BATCH : options->batch_size;
    bool result = output != scratch && count <= graph->capacity && count <= (1u << 30) && batch_size <= BUSTER_CHECK_BATCH;
    stream->status = result ? CHECK_STATUS_OK : CHECK_STATUS_INVALID_GRAPH;
    // The scheduler's marks (BUSTER_CHECK_MARK_PURE and _DONE), with check_graph_validate_vector's absent operand after them.
    uint8_t* pure = result && count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)count + 1) : 0;
    uint32_t* levels = result && count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, count) : 0;
    uint32_t* counts = result && count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 2) : 0;
    uint32_t* order = result && count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, count) : 0;
    CheckReport* reports = result && count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckReport, count) : 0;
    if (result && count != 0 && (pure == 0 || levels == 0 || counts == 0 || order == 0 || reports == 0))
    {
        result = false;
        stream->status = CHECK_STATUS_RESOURCE;
    }
    CheckGraphValidation validation = { 0 };
    if (result)
    {
        result = BUSTER_SCALAR_PATH(options->scalar) ? check_graph_validate(graph, pure, &validation, &stream->failed_node) :
                                   check_graph_validate_vector(graph, pure, &validation, &stream->failed_node);
        stream->status = result ? CHECK_STATUS_OK : CHECK_STATUS_INVALID_GRAPH;
    }
    const uint32_t maximum_arguments = validation.maximum_arguments;
    const uint32_t maximum_effects = validation.maximum_effects;
    const uint32_t* maximum_builtins = validation.builtins;
    CheckValue* arguments = result && maximum_arguments != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckValue, maximum_arguments) : 0;
    if (result && count != 0)
    {
        stream->values = BUSTER_ARENA_PUSH_ARRAY(output, CheckValue, count);
        stream->conversions = BUSTER_ARENA_PUSH_ARRAY(output, CheckNodeConversions, count);
        stream->diagnostics = BUSTER_ARENA_PUSH_ARRAY(output, CheckDiagnostic, count);
        stream->diagnostic_operands = BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, count);
        stream->member_offsets = BUSTER_ARENA_PUSH_ARRAY(output, uint64_t, count);
        stream->warnings = BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, count);
        stream->warning_ignored = BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, count);
        stream->reports = BUSTER_ARENA_PUSH_ARRAY(output, CheckReport, count);
        stream->argument_conversions = graph->argument_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckConversion, graph->argument_count) : 0;
        stream->effects = maximum_effects != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckStoreEffect, maximum_effects) : 0;
        stream->varargs = graph->varargs_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckVarargsEffect, graph->varargs_count) : 0;
        stream->misc = maximum_builtins[0] != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckBuiltinMiscEffect, maximum_builtins[0]) : 0;
        stream->floating = maximum_builtins[1] != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckBuiltinFloatEffect, maximum_builtins[1]) : 0;
        stream->queries = maximum_builtins[2] != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckBuiltinQueryEffect, maximum_builtins[2]) : 0;
        stream->atomics = maximum_builtins[3] != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckBuiltinAtomicEffect, maximum_builtins[3]) : 0;
        stream->vectors = maximum_builtins[4] != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckBuiltinVectorEffect, maximum_builtins[4]) : 0;
        stream->bit_casts = maximum_builtins[5] != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckBuiltinBitCastEffect, maximum_builtins[5]) : 0;
        stream->x86 = graph->x86_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckBuiltinX86Effect, graph->x86_count) : 0;
        stream->overloads = graph->overload_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckOverloadEffect, graph->overload_count) : 0;
        const bool overload_capacity = check_graph_overload_capacity(graph, scratch, &stream->overload_address_capacity, &stream->statistics.scratch_bytes);
        stream->overload_addresses = stream->overload_address_capacity != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckOverloadAddressEffect, stream->overload_address_capacity) : 0;
        result = stream->values != 0 && stream->conversions != 0 && stream->diagnostics != 0 && stream->diagnostic_operands != 0 && stream->member_offsets != 0 &&
                 stream->warnings != 0 &&
                 stream->warning_ignored != 0 && stream->reports != 0 &&
                 (maximum_arguments == 0 || arguments != 0) && (graph->argument_count == 0 || stream->argument_conversions != 0) &&
                 (maximum_effects == 0 || stream->effects != 0) && (graph->varargs_count == 0 || stream->varargs != 0) &&
                 (maximum_builtins[0] == 0 || stream->misc != 0) && (maximum_builtins[1] == 0 || stream->floating != 0) &&
                 (maximum_builtins[2] == 0 || stream->queries != 0) && (maximum_builtins[3] == 0 || stream->atomics != 0) &&
                 (maximum_builtins[4] == 0 || stream->vectors != 0) && (graph->x86_count == 0 || stream->x86 != NULL) &&
                 (maximum_builtins[5] == 0 || stream->bit_casts != NULL) &&
                 (graph->overload_count == 0 || stream->overloads != NULL) && overload_capacity &&
                 (stream->overload_address_capacity == 0 || stream->overload_addresses != NULL);
        stream->status = result ? CHECK_STATUS_OK : CHECK_STATUS_RESOURCE;
    }
    if (result)
    {
        stream->count = count;
        stream->graph = graph;
        stream->statistics.nodes = count;
        stream->statistics.output_bytes = output->length - output_start;
        if (stream->statistics.scratch_bytes < scratch->length)
        {
            stream->statistics.scratch_bytes = scratch->length;
        }
        if (count != 0)
        {
            memset(stream->warning_ignored, 0, (size_t)count * sizeof(*stream->warning_ignored));
            memset(stream->diagnostic_operands, 0, (size_t)count * sizeof(*stream->diagnostic_operands));
        }
        if (stream->argument_conversions != 0)
        {
            memset(stream->argument_conversions, 0, (size_t)graph->argument_count * sizeof(*stream->argument_conversions));
        }
        // In node order: the vector path's impure leaves without their actions, its other impure nodes one at a time, and each
        // run of pure nodes as a region; the scalar path's nodes one at a time.
        uint32_t node = 0;
        while (node < count)
        {
            if (!BUSTER_SCALAR_PATH(options->scalar) && pure[node] == 0 && check_graph_is_leaf(graph, node))
            {
                check_graph_leaf(types, graph, stream, node);
                node += 1;
            }
            else if (BUSTER_SCALAR_PATH(options->scalar) || pure[node] == 0)
            {
                check_graph_scalar_node(types, graph, stream, node, arguments, scratch, BUSTER_SCALAR_PATH(options->scalar));
                node += 1;
            }
            else
            {
                const uint32_t begin = node;
                while (node < count && pure[node] != 0)
                {
                    node += 1;
                }
                check_graph_region(types, graph, stream, pure, begin, node, batch_size, levels, counts, order);
            }
        }
        for (uint32_t pass = 0; result && pass < 2; pass += 1)
        {
            const uint32_t exhausted = check_graph_resource_node(stream, count, BUSTER_SCALAR_PATH(options->scalar));
            if (exhausted != BUSTER_CHECK_NONE)
            {
                result = false;
                stream->status = CHECK_STATUS_RESOURCE;
                stream->failed_node = exhausted;
            }
            if (result && pass == 0)
            {
                result = check_graph_query_runtime(types, graph, stream, scratch, pure, levels, arguments, maximum_arguments, BUSTER_SCALAR_PATH(options->scalar));
            }
        }
        if (stream->statistics.scratch_bytes < scratch->length)
        {
            stream->statistics.scratch_bytes = scratch->length;
        }
        stream->statistics.scratch_bytes -= scratch_start;
        if (result)
        {
            if (count != 0)
            {
                check_graph_warning_contexts(types, graph, stream, pure, BUSTER_SCALAR_PATH(options->scalar));
            }
            check_graph_reports(graph, stream, reports, BUSTER_SCALAR_PATH(options->scalar));
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
