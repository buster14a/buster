// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Typed postorder expressions (#133). The source adapter elaborates declarations and supplies this bounded,
// topologically ordered graph. Both schedulers retain node identities and explicit operand conversions. The
// vector scheduler derives dependency heights from operand edges, never from the parser's bracket depths.
// Include after c_types.c, c_check_actions.c and c_check_builtin.c. All storage belongs to caller-owned arenas;
// no padding is read. Builtin tests share only check_graph_builtin_op; source consumers keep the complete graph.
#ifndef BUSTER_C_CHECKER_INCLUDED
#define BUSTER_C_CHECKER_INCLUDED
#if !defined(BUSTER_UNIT) || BUSTER_SOURCE_CONSUMER
#include "c_check_condition_eval.c"
#include "c_check_overload.c"
#include "c_check_builtin_atomic_c11.c"

#define BUSTER_CHECK_NONE UINT32_MAX
#define BUSTER_CHECK_BATCH 16u
#define BUSTER_CHECK_CONTEXT_ICE 0x01u
#define BUSTER_CHECK_CONTEXT_ASSERT 0x02u
#define BUSTER_CHECK_CONTEXT_STATIC 0x04u
// A source-dialect extension may accept a foldable integer where a strict ICE is required. Keep this request
// separate from the value's strict ICE flag; discarded operands must not make a nonconstant evaluated path valid.
#define BUSTER_CHECK_CONTEXT_GNU_CONSTANT 0x08u
// Source value evaluation without a new constraint report: initializer planning owns its diagnostics.
#define BUSTER_CHECK_CONTEXT_VALUE 0x10u

#include "c_check_sparse.c"

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
    // Whether an explicit cast was stripped before that literal: clang's literal-only arguments (allow_runtime_check's,
    // allow_sanitize_check's, cpu_supports' and cpu_is') refuse one, as annotation's doesn't (#1126's review).
    bool nan_payload_cast;
    // Whether that literal is a UTF-8 one (u8"..."), which the CFString builtins refuse, and its first token.
    bool nan_payload_utf8;
    uint32_t nan_payload_token;
    // A constant CFString's call's name (#148): its token and the `(` after it are the objects check_data_cfstring_keys
    // takes, where it makes them, for the class reference and for the characters' UTF-16 array.
    uint32_t cfstring_name;
    // __builtin_infer_alloc_token's token, where its arguments give one (check_input_alloc_token_evidence, #148).
    bool alloc_token_known;
    uint64_t alloc_token;
    // Whether __builtin_call_with_static_chain's first argument is a call as written, of no builtin (#148).
    bool static_chain_call;
    // The os_log builtins' format (check_input_os_log_evidence, #148): its layout's items, whether it is a literal, whether
    // a mask's size is invalid, and the data arguments its specifiers take.
    const CheckOSLogItem* os_log_items;
    uint32_t os_log_count;
    bool os_log_known;
    bool os_log_invalid_mask;
    uint64_t os_log_taken[4];
    // Whether __builtin_available's specs have an empty version (`0.0.0`, #148), which clang refuses without a diagnostic:
    // its code generation then refuses the call ("cannot compile this scalar expression yet").
    bool availability_silent;
    // The objects of the functions the first argument names (the first four): __builtin_function_start takes an address of
    // one at offset 0, as clang's getAsBuiltinConstantDeclRef evaluates its operand to a function (#1126's review).
    uint32_t function_objects[4];
    uint32_t function_object_count;
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
    // That declaration's object, which an address constant of the argument names (the alignment builtins' folds).
    uint32_t base_object;
    // An alignment builtin's alignment as clang's evaluator reads it (BuiltinAlignment's EvaluateAsInt, which reads a const
    // object's constant initializer wherever the expression holds one): the source evaluator's integer, where it gives one
    // without a fault, its halves keeping the record's alignment at 8, and whether the expression has side effects, which
    // the alignment's checks allow and a fold doesn't (check_input_misc_evidence).
    bool alignment_evaluated;
    bool alignment_side_effects;
    uint64_t alignment_low;
    uint64_t alignment_high;
    // A _BitInt past 128 bits' alignment (#1447): its limbs (c_check_wide.c), whose low 128 bits are the halves above;
    // 0 for a narrower one.
    uint64_t alignment_limbs;
};

typedef struct CheckGraph CheckGraph;

// A VALUE node's leaf as check_graph_initialize's graph holds it (#628): what most leaves have, an identifier's type,
// category, flags, object and overload set, and a literal's or an enumerator's integer up to 64 bits; and for a value
// with more (check_graph_leaf_rest), the index plus one of its row of CheckGraph.leaf_payloads, which holds it whole,
// else 0. Two to a cache line.
typedef struct CheckGraphLeaf CheckGraphLeaf;
struct CheckGraphLeaf
{
    TypeId type;
    uint32_t flags;
    uint32_t object;
    uint32_t overload_head;
    uint64_t integer;
    uint32_t row;
    uint8_t category;
    uint8_t constant_kind;
    uint16_t padding;
};

_Static_assert(sizeof(CheckGraphLeaf) == 32, "a graph's leaf record is half a cache line");

typedef struct CheckConstWarning CheckConstWarning;
struct CheckConstWarning
{
    uint32_t node;
    CheckWarningEvent event;
};
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

// The allocation/scheduling facts produced in node order. vector_blocks describes the independent validator,
// rather than the graph, and is deliberately excluded when comparing a reader's recorded facts.
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
    uint32_t vector_blocks;
};

// A struct's or union's member that a deprecated or unavailable attribute marks (#147): its index in the type table's
// members, its declaration's name, the attribute's first token (the deprecated note's place) and its message's string
// (BUSTER_CHECK_WARNING_NO_SOURCE for none). Its uses are check_input_member_uses' and the initializers' walk's.
typedef struct CheckInputMarkedMember CheckInputMarkedMember;
struct CheckInputMarkedMember
{
    uint32_t member;
    uint32_t name;
    uint32_t attribute;
    uint32_t message;
    bool unavailable;
};

// A token's availability context (CheckInput.availability_contexts, #147): in a deprecated or an unavailable function's
// body or declaration's declarator and initializer, where clang's ShouldDiagnoseAvailabilityInContext drops a use's
// diagnostic (a deprecated context a deprecated use's, an unavailable one either's).
#define BUSTER_CHECK_CONTEXT_DEPRECATED 0x01
#define BUSTER_CHECK_CONTEXT_UNAVAILABLE 0x02
// A deprecated use's or note's payload.data where its name is its payload.first_type's spelling (an anonymous tag's,
// `struct (unnamed at FILE:LINE:COL)` or its typedef's name), not payload.name.
#define BUSTER_CHECK_DEPRECATED_TYPE_NAME 1u

// The marked member of the type table's member `member` among `members` (`count`, ascending by index), or null.
static __attribute__((unused)) const CheckInputMarkedMember* check_marked_member(const CheckInputMarkedMember* members, uint32_t count, uint32_t member)
{
    uint32_t low = 0;
    uint32_t high = count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        const bool before = members[middle].member < member;
        low = before ? middle + 1 : low;
        high = before ? high : middle;
    }
    const CheckInputMarkedMember* result = low < count && members[low].member == member ? members + low : NULL;
    return result;
}

// Whether a use of a marked member at a token of `context` (BUSTER_CHECK_CONTEXT_*) is dropped, as clang's context rules
// drop it: a deprecated one's in a deprecated or unavailable context, an unavailable one's in an unavailable one.
static __attribute__((unused)) bool check_marked_dropped(const CheckInputMarkedMember* marked, uint8_t context)
{
    const bool result = (context & BUSTER_CHECK_CONTEXT_UNAVAILABLE) != 0 || (!marked->unavailable && (context & BUSTER_CHECK_CONTEXT_DEPRECATED) != 0);
    return result;
}

struct CheckGraph
{
    // The marked members and the tokens' availability contexts (CheckInput's, #147), for the initializers' walk; null
    // and 0 for none.
    const CheckInputMarkedMember* marked_members;
    uint32_t marked_member_count;
    const uint8_t* availability_contexts;
    // The unit's __NSConstantString record, which a CFString or NSString builtin's call makes the reader declare (#148), or 0;
    // and then the object of its file-scope declaration of __CFConstantStringClassReference with external linkage, or 0.
    TypeId ns_constant_string;
    uint32_t cfstring_class;
    // Every column has count logical entries. Operands are earlier nodes or BUSTER_CHECK_NONE. Arrays are not
    // padded; optional columns may be null. VALUE uses its leaf (check_graph_leaf). Other nodes use types/member as
    // action data.
    CheckOp* ops;
    uint32_t* tokens;
    uint32_t* operand0;
    uint32_t* operand1;
    uint32_t* operand2;
    // The VALUE nodes' values, leaf_count of them in node order: a VALUE node's is slot leaf_slots[node], and every other
    // node's slot is BUSTER_CHECK_NONE. Only a VALUE node reads a value, and in a column of every node's the 96 bytes of the
    // others' were most of the graph's memory (#572). A slot is a record of 32 bytes (CheckGraphLeaf, #628), and for a value
    // with more than it holds (check_graph_leaf_rest) the whole value as a row of `leaf_payloads`, the record's `row`; on
    // sqlite3.c, 80 of its 144,720 leaves, where 96 bytes a leaf took 14 MB. check_graph_initialize's graph has them; a
    // hand-built one may hold each leaf's whole value in `dense_leaves` instead. Readers and writers take them through
    // check_graph_leaf and check_graph_set_leaf, whichever the graph holds.
    CheckGraphLeaf* leaf_records;
    CheckValue* leaf_payloads;
    uint32_t leaf_payload_count;
    // The rows `leaf_payloads` has room for: the graph's capacity, or a deferred lane's part's leaves.
    uint32_t leaf_payload_capacity;
    CheckValue* dense_leaves;
    uint32_t* leaf_slots;
    TypeId* types;
    uint32_t* members;
    uint8_t* contexts;
    // Completed source initializer receipts. Null keeps ordinary graph checking free of receipt state.
    const CheckConstValues* const_values;
    const CheckConstWarning* const_warnings;
    uint32_t const_warning_count;
    // Optional lexical floating environment and evaluation-method state. Zero/null means source evaluation in the
    // translation environment. Evaluation methods also apply to constants; ACCESS alone retains runtime dependence.
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
    // The unit's enable_if and diagnose_if conditions (#1447), which overload resolution reads: the reader's
    // (check_condition_table), empty in a unit without them.
    CheckConditionTable conditions;
    // Optional typed prefix control for statement expressions. Sorted owners consume contiguous step slices;
    // every expression dependency precedes the owning STATEMENT_VALUE. Runtime execution still uses the body.
    CheckRegionProof* region_proofs;
    uint32_t region_proof_count;
    uint32_t region_proof_capacity;
    CheckRegionProofStep* region_steps;
    uint32_t region_step_count;
    uint32_t region_step_capacity;
    // Only the source reader publishes these facts, after completing every node and sideband list. The graph
    // is immutable after publication. Other producers leave recorded_pure null and always use validation.
    // Exactly capacity bytes, no padding; the scheduler copies count bytes because its marks are mutable.
    uint8_t* recorded_pure;
    CheckGraphValidation recorded;
    uint32_t recorded_nodes;
    bool recorded_ready;
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
    // The vector path's memoized transitions (#1050, c_check_transitions.c): the nodes an earlier node's answer completed
    // (hits, of which those found after the batch their operands waited in ran: flushed hits), those probed and not found
    // (misses), those whose operator memoizes but an operand or argument of which has no class, a fault or an overload set
    // (folds), the entries inserted and the table's entries at the end, and with CheckOptions.verify_transitions, the hits
    // checked by their actions too (verified) and those whose action gave another answer (mismatches).
    uint32_t transition_hits;
    uint32_t transition_flushed_hits;
    uint32_t transition_misses;
    uint32_t transition_folds;
    uint32_t transition_inserts;
    uint32_t transition_entries;
    uint32_t transition_verified;
    uint32_t transition_mismatches;
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
    // Whether its lists are Microsoft ones, whose addresses it takes (CheckVarargsResult.microsoft, #148).
    bool microsoft;
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
    // cpu_supports's word and bit, or cpu_is's field and value (CheckBuiltinMiscResult.cpu_test).
    uint64_t cpu_test;
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
    uint32_t class_mask;
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
    // A lock-free query whose answer in code is its function's emission's (CheckBuiltinAtomicResult.late_completion).
    bool late_completion;
    int8_t answers[2];
    uint64_t query_size;
    TypeId query_pointer;
    TypeId space_target;
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
    CheckWarningEvents warning_events;
    // Output-owned dependency closure of explicit ICE/assert/static contexts, only with const_values.
    // The borrowed graph and its designator leaves are never rewritten to publish this proof.
    uint8_t* constant_evaluated;
    // Node-indexed typed postorder output. The graph supplies operator, operand and source identity. Each node's value is
    // in columns (#628): its type, flags, category, constant kind and object, which most readers read alone, and for a
    // node whose value has more (a constant's payload, an address constant's offset, a bit-field's place, an overload set,
    // a fault: on sqlite3.c, 12% of the nodes, against 36% with an object) its whole value as a row of `payloads`, the
    // row's index plus one in `payload_rows` (0 for none). check_graph_run's stream has them; a hand-built one may hold
    // each node's whole value in `dense_values` instead. Readers take them through check_stream_value and its fields'
    // accessors, whichever the stream holds.
    CheckValue* dense_values;
    TypeId* value_types;
    uint32_t* value_flags;
    uint32_t* value_objects;
    uint8_t* value_categories;
    uint8_t* value_constants;
    uint32_t* payload_rows;
    CheckValue* payloads;
    uint32_t payload_count;
    CheckNodeConversions* conversions;
    CheckConversion* argument_conversions;
    CheckDiagnostic* diagnostics;
    // The operand each diagnostic names, as CheckActionResult.diagnostic_operand has it (a CALL's argument i is 1 + i).
    uint32_t* diagnostic_operands;
    // A MEMBER or ARROW node's member's offset in bits from its record's start (CheckActionResult.member_offset), which
    // the lowering takes rather than looking the member up again (#591); 0 for every other node.
    uint64_t* member_offsets;
    uint32_t* warnings;
    // Final nonzero warning rows, published by check_graph_warning_contexts. Hand-built streams leave the flag off.
    uint32_t warning_count;
    bool warnings_prepared;
    // Whether an action of the run had an operand of an enumeration type (CHECK_WARNING_ENUM_OPERAND).
    bool enum_operands;
    // Warning kinds whose default severity is ignored; an explicit source policy may enable them.
    uint32_t* warning_ignored;
    CheckReport* reports;
    // Whether the columns whose empty value is zero are zero wherever no write has reached them, and each write is its
    // node's first: check_graph_run's main loop, in an output arena whose bytes past its length are as the kernel mapped
    // them (CheckOptions.fresh_output). Its writes then leave a zero unwritten, so that the diagnostics, their operands,
    // the warnings and their ignored kinds, which few nodes have, take no memory where no node has one (#1050).
    bool zero_columns;
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
    // Validate reader-owned graphs too, and compare their retained purity/allocation facts with the oracle.
    bool verify;
    // With regions, 0 chooses BUSTER_CHECK_BATCH. Values 1..BUSTER_CHECK_BATCH permit differential partition testing.
    uint32_t batch_size;
    // The vector path's mutation-free regions: each run of pure nodes checked as a region, in postorder batches whose
    // integer folds take 8 lanes at a time (check_graph_region_postorder), rather than each node in node order, the vector
    // path's default since on sqlite3.c's -c a region's machinery cost more than its batches saved (#1050; docs/c-checker.md,
    // "Node order or regions"). The typed stream is the same.
    bool regions;
    // The regions by dependency level (check_graph_region) rather than in postorder batches (check_graph_region_postorder),
    // their order since the benchmark measured the two on the same actions and postorder won (#133's open question;
    // docs/c-checker.md, "Level order or postorder"); it takes the regions. The typed stream is the same.
    bool level_order;
    // The vector path by its memoized transitions (#1050, c_check_transitions.c) rather than in node order: opt in (the
    // driver's --check-transitions), since they don't save a large fraction of check-expressions, and the benchmark compares
    // them (docs/c-checker.md, "Memoized transitions"). Level order takes none.
    bool transitions;
    // Each transition hit's action run as well, its result stored and compared with the transition's
    // (CheckStatistics.transition_mismatches): the tests' check of the memo, node by node.
    bool verify_transitions;
    // The caller knows that no copy of `output` pushed past its length, so that arena_untouched tells whether its bytes
    // past it are as the kernel mapped them, zero (#628's fresh_output): the C driver's output arena, whose first user the
    // run is. The run then clears no column and writes no zero in its main loop (CheckStream.zero_columns).
    bool fresh_output;
    // check_graph_query_runtime replays every node even where no constant_p query's result changes (#1628): the oracle a
    // test compares the replay's skip with.
    bool replay_queries;
};

_Static_assert(sizeof(CheckValue) == 96 && __builtin_offsetof(CheckValue, category) == 4 && __builtin_offsetof(CheckValue, flags) == 8 && __builtin_offsetof(CheckValue, constant_kind) == 12 &&
               __builtin_offsetof(CheckValue, integer) == 16 && __builtin_offsetof(CheckValue, object) == 64,
               "check_stream_value_to writes a value as its first 64 bytes, its header and three payloads, and its last 32, from its object");

// A node's value in a typed stream (CheckStream.value_types and the columns beside it, #628), written to `destination` by
// each case's own stores. On the vector path a value of columns is one 64-byte store of its header, the rest of those 64
// bytes zero, and one 32-byte store of its object, the rest zero; a whole value is copied by the same two halves, so that
// a load of a field, or of either half, lies within one store. Returned through a temporary instead, as check_stream_value
// returns it, the columns' case was cleared by two overlapping 32-byte stores and copied into the action by a 32-byte load
// across both, which no store can forward: the instruction most sampled in check-expressions of sqlite3.c's -c, at each
// operand of each action (#1050). The scalar path is the same value by assignment.
// Always inlined (#880), as profile-guided builds do; hot caller: check_graph_action, once per operand.
static inline __attribute__((always_inline)) void check_stream_value_to(const CheckStream* stream, uint32_t node, CheckValue* destination)
{
    const CheckValue* whole = NULL;
    if (stream->dense_values != NULL)
    {
        whole = stream->dense_values + node;
    }
    else
    {
        const uint32_t row = stream->payload_rows[node];
        whole = row != 0 && row <= stream->payload_count ? stream->payloads + row - 1 : NULL;
    }
#if BUSTER_HOST_VECTOR
    uint8_t* bytes = (uint8_t*)destination;
    if (whole != NULL)
    {
        check_value_copy(destination, whole);
    }
    else
    {
        const __m128i header = _mm_setr_epi32((int)stream->value_types[node], (int)stream->value_categories[node], (int)stream->value_flags[node],
                                              (int)stream->value_constants[node]);
        _mm512_storeu_si512(bytes, _mm512_zextsi128_si512(header));
        _mm256_storeu_si256((__m256i*)(bytes + 64), _mm256_zextsi128_si256(_mm_cvtsi32_si128((int)stream->value_objects[node])));
    }
#else
    if (whole != NULL)
    {
        *destination = *whole;
    }
    else
    {
        *destination = (CheckValue){
            .type = stream->value_types[node],
            .category = (CheckCategory)stream->value_categories[node],
            .flags = stream->value_flags[node],
            .constant_kind = (CheckConstantKind)stream->value_constants[node],
            .object = stream->value_objects[node],
        };
    }
#endif
}

// A node's value in a typed stream (CheckStream.value_types and the columns beside it, #628), by assignment: its callers,
// the lowering's most of all, take it into registers or a value of their own, which check_stream_value_to's stores would
// leave them to copy whole across its two halves (#1050). The graph's walk reads its stream's columns, and a value with a
// payload row is a seventh of them, which the hints say (#1175).
static CheckValue check_stream_value(const CheckStream* stream, uint32_t node)
{
    CheckValue result;
    if (__builtin_expect(stream->dense_values != NULL, 0))
    {
        result = stream->dense_values[node];
    }
    else
    {
        const uint32_t row = stream->payload_rows[node];
        if (__builtin_expect(row != 0, 0) && row <= stream->payload_count)
        {
            result = stream->payloads[row - 1];
        }
        else
        {
            result = (CheckValue){
                .type = stream->value_types[node],
                .category = (CheckCategory)stream->value_categories[node],
                .flags = stream->value_flags[node],
                .constant_kind = (CheckConstantKind)stream->value_constants[node],
                .object = stream->value_objects[node],
            };
        }
    }
    return result;
}

// Whether the stream holds its nodes' values, in columns or whole.
static bool check_stream_has_values(const CheckStream* stream)
{
    return stream->value_types != NULL || stream->dense_values != NULL;
}

static TypeId check_stream_type(const CheckStream* stream, uint32_t node)
{
    return stream->dense_values != NULL ? stream->dense_values[node].type : stream->value_types[node];
}

static uint32_t check_stream_flags(const CheckStream* stream, uint32_t node)
{
    return stream->dense_values != NULL ? stream->dense_values[node].flags : stream->value_flags[node];
}

static CheckCategory check_stream_category(const CheckStream* stream, uint32_t node)
{
    return stream->dense_values != NULL ? stream->dense_values[node].category : (CheckCategory)stream->value_categories[node];
}

static CheckConstantKind check_stream_constant_kind(const CheckStream* stream, uint32_t node)
{
    return stream->dense_values != NULL ? stream->dense_values[node].constant_kind : (CheckConstantKind)stream->value_constants[node];
}

static uint32_t check_stream_object(const CheckStream* stream, uint32_t node)
{
    return stream->dense_values != NULL ? stream->dense_values[node].object : stream->value_objects[node];
}

// A node's overload set (CheckValue.overload_head), which only a node with a payload row has.
static uint32_t check_stream_overload_head(const CheckStream* stream, uint32_t node)
{
    uint32_t result = 0;
    if (stream->dense_values != NULL)
    {
        result = stream->dense_values[node].overload_head;
    }
    else
    {
        const uint32_t row = stream->payload_rows[node];
        result = row != 0 && row <= stream->payload_count ? stream->payloads[row - 1].overload_head : 0;
    }
    return result;
}

// Whether `value` has more than the stream's columns hold: a field past its type, flags, category, constant kind and
// object, or a category or a constant kind past a byte. A long double component counts by its ten bytes, never by the six
// of padding that a store of the type leaves as they were (c_check_literals.c), which no reader reads.
static bool check_stream_value_rest(const CheckValue* value)
{
    const uint64_t rest = (uint64_t)value->integer | (uint64_t)(value->integer >> 64) | value->floating_storage[0] | (value->floating_storage[1] & 0xffff) |
                          value->imaginary_storage[0] | (value->imaginary_storage[1] & 0xffff) | value->overload_head | (uint64_t)value->offset |
                          value->bit_width | value->fault | value->fault_node | value->object_bit_offset |
                          (((uint32_t)value->category | (uint32_t)value->constant_kind) >> 8);
    return rest != 0;
}

// Writes `value` as node `node`'s: its columns, and its whole value as the node's payload row, where the node has one or
// the value has more than the columns (check_stream_value_rest), which takes the next row. A node keeps the row a write
// gave it (check_graph_run clears `payload_rows` first), so that it has at most one, and `payloads` needs no more rows
// than the stream has nodes, whatever its writes.
// Always inlined (#880), as profile-guided builds do; hot caller: check_graph_leaf, once per leaf.
// check_stream_set_value of `value` but its flags, which are `flags`: check_graph_store writes an action's result so, field
// by field from where the action wrote it, rather than copying the whole result to change its flags, a wide reload of the
// action's narrow stores just made (#1050).
// Always inlined (#880), as profile-guided builds do; hot callers: check_graph_leaf and check_graph_store, once per node.
// As in check_stream_value, the columns are the walk's, and a payload row is rare (#1175).
static inline __attribute__((always_inline)) void check_stream_set_value_flags(CheckStream* stream, uint32_t node, const CheckValue* value, uint32_t flags)
{
    if (__builtin_expect(stream->dense_values != NULL, 0))
    {
        stream->dense_values[node] = *value;
        stream->dense_values[node].flags = flags;
    }
    else
    {
        stream->value_types[node] = value->type;
        stream->value_flags[node] = flags;
        stream->value_objects[node] = value->object;
        stream->value_categories[node] = (uint8_t)value->category;
        stream->value_constants[node] = (uint8_t)value->constant_kind;
        // A first write (CheckStream.zero_columns) has no row yet: it reads none, so that no page of the column is mapped
        // but where a row is written. A row past the payloads is none; row 0 is none already, so one compare decides.
        uint32_t row = stream->zero_columns ? 0 : stream->payload_rows[node];
        row = row <= stream->payload_count ? row : 0;
        if (__builtin_expect(check_stream_value_rest(value), 0) && row == 0)
        {
            row = ++stream->payload_count;
        }
        if (__builtin_expect(row != 0, 0))
        {
            stream->payloads[row - 1] = *value;
            stream->payloads[row - 1].flags = flags;
        }
        if (row != 0 || !stream->zero_columns)
        {
            stream->payload_rows[node] = row;
        }
    }
}

// Always inlined (#880), as profile-guided builds do; hot caller: check_graph_leaf, once per leaf.
static inline __attribute__((always_inline)) void check_stream_set_value(CheckStream* stream, uint32_t node, const CheckValue* value)
{
    check_stream_set_value_flags(stream, node, value, value->flags);
}

// Whether the graph holds its leaves, as records or whole.
static inline bool check_graph_has_leaves(const CheckGraph* graph)
{
    return graph->leaf_records != NULL || graph->dense_leaves != NULL;
}

// Whether `value` has more than a leaf's record holds (CheckGraphLeaf): an integer past 64 bits, a floating component (by
// its ten meaningful bytes, as check_stream_value_rest counts it), an address constant's offset, a bit-field's width or
// place, a fault, or a category or a constant kind past a byte.
static inline __attribute__((always_inline)) bool check_graph_leaf_rest(const CheckValue* value)
{
    const uint64_t rest = (uint64_t)(value->integer >> 64) | value->floating_storage[0] | (value->floating_storage[1] & 0xffff) | value->imaginary_storage[0] |
                          (value->imaginary_storage[1] & 0xffff) | (uint64_t)value->offset | value->bit_width | value->fault | value->fault_node |
                          value->object_bit_offset | (((uint32_t)value->category | (uint32_t)value->constant_kind) >> 8);
    return rest != 0;
}

// The leaf in slot `slot`: its whole value, from the dense leaves, its row, or its record's fields.
// Always inlined (#880), as profile-guided builds do; hot callers: check_graph_leaf and the scalar VALUE action.
static inline __attribute__((always_inline)) CheckValue check_graph_leaf_at(const CheckGraph* graph, uint32_t slot)
{
    CheckValue result;
    if (__builtin_expect(graph->dense_leaves != NULL, 0))
    {
        result = graph->dense_leaves[slot];
    }
    else
    {
        const CheckGraphLeaf* leaf = graph->leaf_records + slot;
        const uint32_t row = leaf->row;
        if (__builtin_expect(row != 0, 0) && row <= graph->leaf_payload_count)
        {
            result = graph->leaf_payloads[row - 1];
        }
        else
        {
            result = (CheckValue){
                .type = leaf->type,
                .category = (CheckCategory)leaf->category,
                .flags = leaf->flags,
                .constant_kind = (CheckConstantKind)leaf->constant_kind,
                .integer = leaf->integer,
                .object = leaf->object,
                .overload_head = leaf->overload_head,
            };
        }
    }
    return result;
}

// A VALUE node's leaf (check_graph_leaf_at).
static inline CheckValue check_graph_leaf_value(const CheckGraph* graph, uint32_t node)
{
    return check_graph_leaf_at(graph, graph->leaf_slots[node]);
}

// A VALUE node's leaf's type, flags, object and overload set, which its record holds whatever its row.
static inline TypeId check_graph_leaf_type(const CheckGraph* graph, uint32_t node)
{
    const uint32_t slot = graph->leaf_slots[node];
    return graph->dense_leaves != NULL ? graph->dense_leaves[slot].type : graph->leaf_records[slot].type;
}

static inline uint32_t check_graph_leaf_flags(const CheckGraph* graph, uint32_t node)
{
    const uint32_t slot = graph->leaf_slots[node];
    return graph->dense_leaves != NULL ? graph->dense_leaves[slot].flags : graph->leaf_records[slot].flags;
}

static inline uint32_t check_graph_leaf_object(const CheckGraph* graph, uint32_t node)
{
    const uint32_t slot = graph->leaf_slots[node];
    return graph->dense_leaves != NULL ? graph->dense_leaves[slot].object : graph->leaf_records[slot].object;
}

static inline uint32_t check_graph_leaf_head_at(const CheckGraph* graph, uint32_t slot)
{
    return graph->dense_leaves != NULL ? graph->dense_leaves[slot].overload_head : graph->leaf_records[slot].overload_head;
}

static inline uint32_t check_graph_leaf_overload_head(const CheckGraph* graph, uint32_t node)
{
    return check_graph_leaf_head_at(graph, graph->leaf_slots[node]);
}

// Whether a leaf's record and its row hold the same value in the fields both hold: the field accessors read the record and
// check_graph_leaf_at the row, which check_graph_write_leaf writes from one value.
static bool check_graph_leaf_row_agrees(const CheckGraphLeaf* leaf, const CheckValue* value)
{
    return leaf->type == value->type && leaf->flags == value->flags && leaf->object == value->object && leaf->overload_head == value->overload_head &&
           leaf->integer == (uint64_t)value->integer && leaf->category == (uint8_t)value->category && leaf->constant_kind == (uint8_t)value->constant_kind;
}

// Whether the leaf in slot `slot` is well formed, as the validators require it: its overload set one of the candidates, and
// a record's row (#628) none or one of the graph's rows, which check_graph_validate_lists bounds by their room and column,
// holding what the record holds. A row past them is refused, not read as none.
static inline bool check_graph_leaf_valid_at(const CheckGraph* graph, uint32_t slot)
{
    bool valid = true;
    if (graph->dense_leaves == NULL)
    {
        const CheckGraphLeaf* leaf = graph->leaf_records + slot;
        valid = leaf->row <= graph->leaf_payload_count &&
                (leaf->row == 0 || check_graph_leaf_row_agrees(leaf, graph->leaf_payloads + leaf->row - 1));
    }
    return valid && check_graph_leaf_head_at(graph, slot) <= graph->overload_candidate_count;
}

// Writes `value` as the leaf in slot `slot`: its record, and where it has more than a record holds, its whole value as a
// row, the row the leaf has (`keep`: a leaf a write has made already) or else the graph's next. A leaf's first write
// (check_graph_set_leaf_at) takes a row only for a value with more, so that a graph's rows are at most its leaves; the
// leaves' writers make each leaf once, and its pages need no clearing first. A row past `leaf_payload_capacity` is none,
// which can't happen while each leaf takes at most one.
// Always inlined (#880), as profile-guided builds do; hot callers: the reader's leaves, once per leaf.
static inline __attribute__((always_inline)) void check_graph_write_leaf(CheckGraph* graph, uint32_t slot, const CheckValue* value, bool keep)
{
    if (__builtin_expect(graph->dense_leaves != NULL, 0))
    {
        graph->dense_leaves[slot] = *value;
    }
    else
    {
        CheckGraphLeaf* leaf = graph->leaf_records + slot;
        uint32_t row = keep ? leaf->row : 0;
        row = row <= graph->leaf_payload_count ? row : 0;
        if (__builtin_expect(check_graph_leaf_rest(value), 0))
        {
            row = row == 0 && graph->leaf_payload_count < graph->leaf_payload_capacity ? ++graph->leaf_payload_count : row;
            if (row != 0)
            {
                graph->leaf_payloads[row - 1] = *value;
            }
        }
        else if (row != 0)
        {
            // A rewrite to a value without more: the row stays the leaf's, and holds the value, as its record does.
            graph->leaf_payloads[row - 1] = *value;
        }
        *leaf = (CheckGraphLeaf){
            .type = value->type,
            .flags = value->flags,
            .object = value->object,
            .overload_head = value->overload_head,
            .integer = (uint64_t)value->integer,
            .row = row,
            .category = (uint8_t)value->category,
            .constant_kind = (uint8_t)value->constant_kind,
        };
    }
}

static inline __attribute__((always_inline)) void check_graph_set_leaf_at(CheckGraph* graph, uint32_t slot, const CheckValue* value)
{
    check_graph_write_leaf(graph, slot, value, false);
}

// A VALUE node's leaf's first write, and a write over it (which keeps its row).
static inline __attribute__((always_inline)) void check_graph_set_leaf(CheckGraph* graph, uint32_t node, const CheckValue* value)
{
    check_graph_write_leaf(graph, graph->leaf_slots[node], value, false);
}

static inline void check_graph_update_leaf(CheckGraph* graph, uint32_t node, const CheckValue* value)
{
    check_graph_write_leaf(graph, graph->leaf_slots[node], value, true);
}

#include "c_check_builtin_graph_metadata.c"

static bool check_graph_initialize(CheckGraph* graph, Arena* arena, uint32_t capacity, uint32_t argument_capacity);
static uint32_t check_graph_add(CheckGraph* graph, CheckOp op, uint32_t token, uint32_t operand0, uint32_t operand1, uint32_t operand2);
static bool check_graph_run(TypeTable* types, const CheckGraph* graph, Arena* output, Arena* scratch, const CheckOptions* options, CheckStream* stream);

// A graph has room for every node its producer could add, and its columns fill from their starts: the C driver's reader
// makes room for a node a token, of which sqlite3.c's graph holds 323,557 of 774,038. Pushed one after another, each
// column's unused tail shared its huge pages with the next column's used start, so that every huge page of the columns
// was touched: 39 MB of sqlite3.c's peak for 17 MB of nodes. A column of a huge page or more starts one instead, where
// its tail takes no page (#628).
static void* check_graph_column(Arena* arena, uint64_t count, uint64_t element_bytes, uint64_t alignment)
{
    const bool large = count * element_bytes >= BUSTER_ARENA_HUGE_PAGE_BYTES;
    return arena_push_array(arena, count, element_bytes, large ? BUSTER_ARENA_HUGE_PAGE_BYTES : alignment);
}

#define BUSTER_CHECK_GRAPH_COLUMN(arena, Type, count) ((Type*)check_graph_column((arena), (count), sizeof(Type), _Alignof(Type)))

static bool check_graph_initialize(CheckGraph* graph, Arena* arena, uint32_t capacity, uint32_t argument_capacity)
{
    const uint64_t start = arena->length;
    *graph = (CheckGraph){ 0 };
    bool result = capacity <= (1u << 30) && argument_capacity <= (1u << 30);
    if (result && capacity != 0)
    {
        // The byte column first, where it shares a huge page with what the arena holds before the graph.
        graph->contexts = BUSTER_CHECK_GRAPH_COLUMN(arena, uint8_t, capacity);
        graph->ops = BUSTER_CHECK_GRAPH_COLUMN(arena, CheckOp, capacity);
        graph->tokens = BUSTER_CHECK_GRAPH_COLUMN(arena, uint32_t, capacity);
        graph->operand0 = BUSTER_CHECK_GRAPH_COLUMN(arena, uint32_t, capacity);
        graph->operand1 = BUSTER_CHECK_GRAPH_COLUMN(arena, uint32_t, capacity);
        graph->operand2 = BUSTER_CHECK_GRAPH_COLUMN(arena, uint32_t, capacity);
        graph->leaf_records = BUSTER_CHECK_GRAPH_COLUMN(arena, CheckGraphLeaf, capacity);
        // Room for a row a leaf, of which the leaves with more than a record take the first few: only those take memory.
        graph->leaf_payloads = BUSTER_CHECK_GRAPH_COLUMN(arena, CheckValue, capacity);
        graph->leaf_payload_capacity = capacity;
        graph->leaf_slots = BUSTER_CHECK_GRAPH_COLUMN(arena, uint32_t, capacity);
        graph->types = BUSTER_CHECK_GRAPH_COLUMN(arena, TypeId, capacity);
        graph->members = BUSTER_CHECK_GRAPH_COLUMN(arena, uint32_t, capacity);
        graph->source_positions = BUSTER_CHECK_GRAPH_COLUMN(arena, uint32_t, capacity);
        graph->argument_offsets = BUSTER_CHECK_GRAPH_COLUMN(arena, uint32_t, capacity);
        graph->argument_counts = BUSTER_CHECK_GRAPH_COLUMN(arena, uint32_t, capacity);
        result = graph->ops != 0 && graph->tokens != 0 && graph->operand0 != 0 && graph->operand1 != 0 && graph->operand2 != 0 && graph->leaf_records != 0 && graph->leaf_payloads != 0 && graph->leaf_slots != 0 && graph->types != 0 && graph->members != 0 && graph->contexts != 0 && graph->source_positions != 0 && graph->argument_offsets != 0 && graph->argument_counts != 0;
    }
    if (result && argument_capacity != 0)
    {
        graph->argument_nodes = BUSTER_CHECK_GRAPH_COLUMN(arena, uint32_t, argument_capacity);
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

// A node of `op` at `token` with operands operand0 to operand2, each NONE or an earlier node, and its columns' defaults: a
// VALUE node takes the next leaf, cleared for its maker to fill. The reader's coded walk numbers its nodes into a batch
// and writes their columns and whole leaves itself (check_input_expression_flush, #1038). Always inlined (#880), as
// profile-guided builds do; hot caller: check_input_add_node, once per node.
static inline __attribute__((always_inline)) uint32_t check_graph_add(CheckGraph* graph, CheckOp op, uint32_t token, uint32_t operand0, uint32_t operand1,
                                                                      uint32_t operand2)
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
        const bool leaf = op == CHECK_OP_VALUE;
        graph->leaf_slots[node] = leaf ? graph->leaf_count : BUSTER_CHECK_NONE;
        if (leaf)
        {
            check_graph_set_leaf_at(graph, graph->leaf_count, &(const CheckValue){ 0 });
        }
        graph->leaf_count += leaf;
        graph->types[node] = 0;
        graph->members[node] = 0;
        graph->contexts[node] = 0;
        // As check_graph_context's: a unit with a floating-point environment of its own is rare (#1175).
        if (__builtin_expect(graph->fp_environments != NULL, 0))
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
        graph->recorded_ready = false;
        result = node;
    }
    return result;
}

// The operators with an argument list: a call and the builtins'. A bit each from CHECK_OP_CALL on, tested without a
// branch: as eight compares, clang laid out a branch per node of the graph's walk that jumped half the time (#1175).
#define BUSTER_CHECK_ARGUMENT_OPERATOR(op) (UINT64_C(1) << ((op) - CHECK_OP_CALL))
static const uint64_t check_graph_argument_operators =
    BUSTER_CHECK_ARGUMENT_OPERATOR(CHECK_OP_CALL) | BUSTER_CHECK_ARGUMENT_OPERATOR(CHECK_OP_BUILTIN_MISC) |
    BUSTER_CHECK_ARGUMENT_OPERATOR(CHECK_OP_BUILTIN_FLOAT) | BUSTER_CHECK_ARGUMENT_OPERATOR(CHECK_OP_BUILTIN_QUERY) |
    BUSTER_CHECK_ARGUMENT_OPERATOR(CHECK_OP_BUILTIN_ATOMIC) | BUSTER_CHECK_ARGUMENT_OPERATOR(CHECK_OP_BUILTIN_VECTOR) |
    BUSTER_CHECK_ARGUMENT_OPERATOR(CHECK_OP_BUILTIN_X86) | BUSTER_CHECK_ARGUMENT_OPERATOR(CHECK_OP_BUILTIN_BIT_CAST);
_Static_assert(CHECK_OP_BUILTIN_MISC > CHECK_OP_CALL && CHECK_OP_BUILTIN_FLOAT > CHECK_OP_CALL && CHECK_OP_BUILTIN_QUERY > CHECK_OP_CALL &&
               CHECK_OP_BUILTIN_ATOMIC > CHECK_OP_CALL && CHECK_OP_BUILTIN_VECTOR > CHECK_OP_CALL && CHECK_OP_BUILTIN_X86 > CHECK_OP_CALL &&
               CHECK_OP_BUILTIN_BIT_CAST > CHECK_OP_CALL && CHECK_OP_BUILTIN_MISC - CHECK_OP_CALL < 64 && CHECK_OP_BUILTIN_FLOAT - CHECK_OP_CALL < 64 &&
               CHECK_OP_BUILTIN_QUERY - CHECK_OP_CALL < 64 && CHECK_OP_BUILTIN_ATOMIC - CHECK_OP_CALL < 64 && CHECK_OP_BUILTIN_VECTOR - CHECK_OP_CALL < 64 &&
               CHECK_OP_BUILTIN_X86 - CHECK_OP_CALL < 64 && CHECK_OP_BUILTIN_BIT_CAST - CHECK_OP_CALL < 64,
               "the operators with arguments fit a 64-bit mask from CHECK_OP_CALL");
#undef BUSTER_CHECK_ARGUMENT_OPERATOR

static bool check_graph_has_arguments(CheckOp op)
{
    const uint32_t offset = (uint32_t)op - (uint32_t)CHECK_OP_CALL;
    return (offset < 64) & (uint32_t)(check_graph_argument_operators >> (offset & 63));
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
#endif
static __attribute__((unused)) CheckOp check_graph_builtin_op(CBuiltinId id)
{
    CheckOp op = CHECK_OP_COUNT;
    if (id == CBUILTIN_NONE)
    {
        // An ordinary call's, which every call of a unit asks for (check_input's call close): none of the families'.
        op = CHECK_OP_COUNT;
    }
    else if (id == CBUILTIN_BIT_CAST)
    {
        op = CHECK_OP_BUILTIN_BIT_CAST;
    }
    else if ((id >= CBUILTIN_EXPECT && id <= CBUILTIN_PREFETCH) || (id >= CBUILTIN_MEMCPY && id <= CBUILTIN_ALLOCA) ||
        id == CBUILTIN_FRAME_ADDRESS || id == CBUILTIN_RETURN_ADDRESS || id == CBUILTIN_EXTRACT_RETURN_ADDR || id == CBUILTIN_FROB_RETURN_ADDR ||
        cbuiltin_bits(id) || cbuiltin_align(id) || id == CBUILTIN_EXPECT_WITH_PROBABILITY || (id >= CBUILTIN_CLASSIFY_TYPE && id <= CBUILTIN_COMPLEX) || cbuiltin_misc_last(id))
    {
        op = CHECK_OP_BUILTIN_MISC;
    }
    else if ((id >= CBUILTIN_HUGE_VAL && id <= CBUILTIN_FLT_ROUNDS) || (id >= CBUILTIN_ISGREATER && id <= CBUILTIN_ISUNORDERED) || cbuiltin_float_constant_extra(id) ||
             check_builtin_math_kind(id) != CHECK_BUILTIN_MATH_NONE || cbuiltin_float_class(id) || cbuiltin_float_signbit(id) ||
             cbuiltin_float_canonicalize(id))
    {
        op = CHECK_OP_BUILTIN_FLOAT;
    }
    else if (id == CBUILTIN_CONSTANT_P || id == CBUILTIN_OBJECT_SIZE || id == CBUILTIN_DYNAMIC_OBJECT_SIZE)
    {
        op = CHECK_OP_BUILTIN_QUERY;
    }
    else if (check_builtin_atomic_checked(id))
    {
        op = CHECK_OP_BUILTIN_ATOMIC;
    }
    else if (check_builtin_typed_overflow_info(id) != NULL)
    {
        op = CHECK_OP_BUILTIN_OVERFLOW;
    }
    else if (cbuiltin_vector(id))
    {
        op = CHECK_OP_BUILTIN_VECTOR;
    }
    return op;
}
#if !defined(BUSTER_UNIT) || BUSTER_SOURCE_CONSUMER
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
// Always inlined (#880), as profile-guided builds do; hot caller: check_graph_record_nodes, once per node.
static inline __attribute__((always_inline)) bool check_graph_pure(const CheckGraph* graph, uint32_t node, const uint8_t* pure)
{
    const CheckOp op = graph->ops[node];
    const TypeId requested = graph->types != 0 ? graph->types[node] : 0;
    bool result = false;
    if (op == CHECK_OP_VALUE)
    {
        result = check_graph_has_leaves(graph) && check_graph_leaf_overload_head(graph, node) == 0 && check_graph_builtin(check_graph_leaf_type(graph, node));
    }
    else if (__builtin_expect(op == CHECK_OP_SIZEOF || op == CHECK_OP_ALIGNOF, 0))
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

// Complete newly written nodes while their columns are hot. The reader calls this before adding the next
// node, after the previous node's leaf, requested type, arguments and witnesses have been filled. The only
// cross-node work is three already-produced purity bytes; required source order is preserved on both paths.
static void check_graph_record_nodes(CheckGraph* graph)
{
    for (uint32_t node = graph->recorded_nodes; node < graph->count; node += 1)
    {
        const CheckOp op = graph->ops[node];
        const uint32_t count = graph->argument_counts != NULL ? graph->argument_counts[node] : 0;
        const bool varargs = op == CHECK_OP_VARARGS;
        CheckGraphValidation* state = &graph->recorded;
        graph->recorded_pure[node] = (uint8_t)check_graph_pure(graph, node, graph->recorded_pure);
        // A node with arguments is about one in twenty-five, and one of the counters below rarer still (#1175).
        if (__builtin_expect(varargs, 0))
        {
            state->varargs += 1;
        }
        else if (__builtin_expect(count != 0, 0))
        {
            state->maximum_arguments = count > state->maximum_arguments ? count : state->maximum_arguments;
            state->arguments += count;
        }
        // Most nodes are leaves or ordinary arithmetic. Leave their cold, exceptional counters untouched:
        // unconditional zero increments otherwise make Clang load/store the complete counter vector per node. The hint
        // expects a VALUE, which takes the default.
        switch (__builtin_expect(op, CHECK_OP_VALUE))
        {
            case CHECK_OP_BUILTIN_OVERFLOW: state->maximum_effects += 1; break;
            case CHECK_OP_BUILTIN_MISC: state->builtins[0] += 1; break;
            case CHECK_OP_BUILTIN_FLOAT: state->builtins[1] += 1; break;
            case CHECK_OP_BUILTIN_QUERY: state->builtins[2] += 1; break;
            case CHECK_OP_BUILTIN_ATOMIC: state->builtins[3] += 1; break;
            case CHECK_OP_BUILTIN_VECTOR: state->builtins[4] += 1; break;
            case CHECK_OP_BUILTIN_BIT_CAST: state->builtins[5] += 1; break;
            default: break;
        }
    }
    graph->recorded_nodes = graph->count;
}

static void check_graph_record_finish(CheckGraph* graph)
{
    check_graph_record_nodes(graph);
    graph->recorded.builtin_calls = graph->builtin_count;
    graph->recorded.vectors = graph->vector_member_count;
    graph->recorded.x86 = graph->x86_count;
    graph->recorded.overloads = graph->overload_count;
    // check_graph_add already counts the compact VALUE leaves as it appends their slots.
    graph->recorded.leaves = graph->leaf_count;
    graph->recorded_ready = true;
}

static bool check_graph_recorded_equal(const CheckGraph* graph, const CheckGraphValidation* validation, const uint8_t* pure, uint32_t* failed)
{
    CheckGraphValidation expected = *validation;
    expected.vector_blocks = 0;
    bool result = graph->recorded_ready && graph->recorded_nodes == graph->count &&
                  memcmp(&expected, &graph->recorded, sizeof(expected)) == 0;
    for (uint32_t node = 0; result && node < graph->count; node += 1)
    {
        result = pure[node] == graph->recorded_pure[node];
        *failed = result ? BUSTER_CHECK_NONE : node;
    }
    return result;
}

// The graph's own counts, its sideband lists and the columns every node needs, before any node.
static bool check_graph_validate_lists(const CheckGraph* graph)
{
    bool result = graph->count <= graph->capacity && graph->count <= (1u << 30) && graph->argument_count <= graph->argument_capacity && graph->argument_count <= (1u << 30);
    // Records' rows (#628): no more than their room, and a column of them where there is any. Each record's row is
    // checked at its node (check_graph_leaf_valid_at), so that every row a reader takes is a row of the column.
    result = result && (graph->dense_leaves != NULL || graph->leaf_records == NULL ||
                        (graph->leaf_payload_count <= graph->leaf_payload_capacity && (graph->leaf_payload_count == 0 || graph->leaf_payloads != NULL)));
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
        const uint32_t flags = candidate->flags;
        const uint32_t library = candidate->library_call;
        result = (candidate->previous == BUSTER_CHECK_NONE || candidate->previous < i) && candidate->declaration != BUSTER_CHECK_NONE &&
                 candidate->canonical != BUSTER_CHECK_NONE &&
                 (flags & ~(BUSTER_CHECK_OVERLOADABLE | BUSTER_CHECK_OVERLOAD_UNAVAILABLE | BUSTER_CHECK_OVERLOAD_DEFINED)) == 0 &&
                 (library == 0 || ((library & BUSTER_CHECK_LIBRARY_CALL) != 0 && (library & ~BUSTER_CHECK_LIBRARY_CALL) < BUSTER_CBUILTIN_LIBRARY_PLAIN_FOLDS &&
                                  (flags & BUSTER_CHECK_OVERLOADABLE) == 0));
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
    result = result && (op == CHECK_OP_VALUE ? check_graph_has_leaves(graph) && slot == state->leaves && slot < graph->leaf_count : slot == BUSTER_CHECK_NONE);
    if (result && op == CHECK_OP_VALUE)
    {
        result = check_graph_leaf_valid_at(graph, slot);
    }
    result = result && (op != CHECK_OP_STATEMENT_VALUE || graph->members == 0 || (graph->members[node] & ~BUSTER_CHECK_SIDE_EFFECT) == 0);
    result = result && ((op != CHECK_OP_VLA_BOUND && op != CHECK_OP_VLA_EXTENT) || (graph->members != NULL && graph->members[node] != 0));
    result = result && (graph->contexts == 0 || (graph->contexts[node] & ~(BUSTER_CHECK_CONTEXT_ICE | BUSTER_CHECK_CONTEXT_ASSERT | BUSTER_CHECK_CONTEXT_STATIC |
                                                                       BUSTER_CHECK_CONTEXT_GNU_CONSTANT | BUSTER_CHECK_CONTEXT_VALUE)) == 0);
    result = result && (graph->fp_environments == NULL || ((graph->fp_environments[node] & ~BUSTER_CHECK_FP_ENV_MASK) == 0 &&
                        (graph->fp_environments[node] & BUSTER_CHECK_FP_EVAL_MASK) != BUSTER_CHECK_FP_EVAL_MASK));
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
                  op == CHECK_OP_BUILTIN_VECTOR || op == CHECK_OP_BUILTIN_BIT_CAST ||
                  (op == CHECK_OP_BUILTIN_MISC && graph->members != NULL && cbuiltin_misc_recorded(graph->members[node])));
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
        result = vector->node == node && (op == CHECK_OP_MEMBER || op == CHECK_OP_ARROW) && vector->member.kind <= CHECK_VECTOR_MEMBER_ODD &&
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
    // live, since each leaf is a load of its record and these are most of the block's time.
    const __mmask16 leaves = _mm512_cmpeq_epi32_mask(ops, _mm512_set1_epi32(CHECK_OP_VALUE));
    const bool readable = check_graph_has_leaves(graph) && graph->leaf_slots != 0;
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
            const uint32_t head = check_graph_leaf_head_at(graph, slot);
            const TypeId type = graph->dense_leaves != NULL ? graph->dense_leaves[slot].type : graph->leaf_records[slot].type;
            heads &= check_graph_leaf_valid_at(graph, slot);
            reorderable_leaves |= (uint32_t)(head == 0 && check_graph_builtin(type)) << lane;
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
        valid &= _mm512_testn_epi32_mask(contexts, _mm512_set1_epi32((int)~(BUSTER_CHECK_CONTEXT_ICE | BUSTER_CHECK_CONTEXT_ASSERT | BUSTER_CHECK_CONTEXT_STATIC |
                                                                       BUSTER_CHECK_CONTEXT_GNU_CONSTANT | BUSTER_CHECK_CONTEXT_VALUE)));
    }
    if (graph->fp_environments != NULL)
    {
        const __m512i environments = _mm512_cvtepu8_epi32(_mm_loadu_si128((const __m128i*)(graph->fp_environments + node)));
        valid &= _mm512_testn_epi32_mask(environments, _mm512_set1_epi32((int)~BUSTER_CHECK_FP_ENV_MASK));
        valid &= _mm512_cmpneq_epi32_mask(_mm512_and_si512(environments, _mm512_set1_epi32(BUSTER_CHECK_FP_EVAL_MASK)),
                                         _mm512_set1_epi32(BUSTER_CHECK_FP_EVAL_MASK));
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

// Construct one semantic view at the graph boundary. Diagnostic node ids and source ends are independent.
// The stream marks required constant evaluation for receipt reads and half arithmetic; branch selection and runtime
// effects stay in their own graph columns.
static CheckContext check_graph_context(const CheckGraph* graph, uint32_t node, bool constant_evaluated)
{
    const uint32_t token = graph->tokens != NULL ? graph->tokens[node] : BUSTER_CHECK_NONE;
    const CheckContext context = {
        .tag_completions = graph->tag_completions,
        .transparent_unions = graph->transparent_unions,
        .tag_completion_count = graph->tag_completion_count,
        .source_position = graph->source_positions != NULL ? graph->source_positions[node] : BUSTER_CHECK_NONE,
        .extensions = graph->extensions | (check_graph_system_token(graph, token) ? BUSTER_CHECK_EXTENSION_SYSTEM_HEADER : 0),
        // A unit that changes its floating-point environment (#pragma STDC FENV_ACCESS and the like) is rare (#1175).
        .fp_environment = __builtin_expect(graph->fp_environments != NULL, 0) ? graph->fp_environments[node] : 0,
        .const_values = graph->const_values,
        .constant_evaluated = constant_evaluated,
    };
    return context;
}

// Always inlined (#880), as profile-guided builds do; hot caller: check_graph_node_action, once per action.
static inline __attribute__((always_inline)) void check_graph_action(const CheckGraph* graph, const CheckStream* stream, uint32_t node, CheckValue* arguments, CheckAction* action)
{
    *action = (CheckAction){
        .op = graph->ops[node],
        .type = graph->types != 0 ? graph->types[node] : 0,
        .member = graph->members != 0 ? graph->members[node] : 0,
        .node = node,
        .context = check_graph_context(graph, node, stream->constant_evaluated != NULL && __builtin_expect(stream->constant_evaluated[node] != 0, 0)),
    };
    if (__builtin_expect(action->op == CHECK_OP_MEMBER || action->op == CHECK_OP_ARROW, 0) && graph->vector_member_count != 0)
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
            check_stream_value_to(stream, operands[i], &action->operands[i]);
        }
    }
    if (action->op == CHECK_OP_VALUE)
    {
        action->operands[0] = check_graph_leaf_value(graph, node);
    }
    if (check_graph_has_arguments(action->op) && graph->argument_counts != 0)
    {
        const uint32_t offset = graph->argument_offsets != 0 ? graph->argument_offsets[node] : 0;
        action->argument_count = graph->argument_counts[node];
        for (uint32_t i = 0; i < action->argument_count; i += 1)
        {
            check_stream_value_to(stream, graph->argument_nodes[offset + i], &arguments[i]);
        }
        action->arguments = arguments;
        action->argument_conversions = action->argument_count != 0 ? stream->argument_conversions + offset : 0;
    }
}

// Context consumers use this converted copy; ordinary stream values remain address-bearing designators.
// In particular a bare case/member/initializer lvalue needs a load even though its node has no consuming action.
static CheckValue check_graph_context_value(const TypeTable* types, const CheckGraph* graph, const CheckStream* stream, uint32_t node)
{
    CheckValue value = node < stream->count && check_stream_has_values(stream) ? check_stream_value(stream, node) : (CheckValue){ 0 };
    const TypeId plain = graph->const_values != NULL ? check_const_plain(types, value.type) : 0;
    if (node < graph->count && value.category == CHECK_LVALUE && graph->const_values != NULL &&
        stream->constant_evaluated != NULL && stream->constant_evaluated[node] != 0 && plain != 0 &&
        !type_kind_is_array(type_kind(types, plain)))
    {
        const uint8_t kind = type_kind(types, type_strip_aligned(types, value.type));
        const bool volatile_read = (type_qualifiers(value.type) & BUSTER_TYPE_VOLATILE) != 0 ||
                                   (value.flags & BUSTER_CHECK_VOLATILE_PART) != 0;
        CheckValue loaded = { 0 };
        const bool known = !volatile_read && value.fault == CHECK_FAULT_NONE &&
                           (value.flags & (BUSTER_CHECK_INACTIVE_UNION_READ | BUSTER_CHECK_CONST_READ_RESTRICTED)) == 0 &&
                           (value.constant_kind == CHECK_CONSTANT_ADDRESS || (value.flags & BUSTER_CHECK_BIT_FIELD) != 0) &&
                           check_const_load(types, graph->const_values, value.object, value.offset, value.object_bit_offset, value.bit_width,
                                            value.type, graph->source_positions != NULL ? graph->source_positions[node] : BUSTER_CHECK_NONE, &loaded);
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
        if (volatile_read || (kind == BUSTER_TYPE_KIND_ATOMIC && !known))
        {
            value.flags |= BUSTER_CHECK_SIDE_EFFECT;
            value.constant_kind = CHECK_CONSTANT_NONE;
        }
        value.type = plain;
        value.category = CHECK_RVALUE;
        value.flags &= ~(BUSTER_CHECK_REGISTER | BUSTER_CHECK_VOLATILE_PART);
    }
    return value;
}

// Always inlined (#880), as profile-guided builds do; hot caller: check_graph_run, once per node.
// A conversion record by its two fields, each one load: an action writes a record's type and flags by separate stores, and
// a wide copy of records just written, as check_graph_store's memcpy of an action's three was, spans several of them, which
// a store can forward only one of (#1050). Relaxed atomic loads, which the compiler doesn't merge into wider ones, keep the
// two loads apart; nothing else accesses the record meanwhile.
// Always inlined (#880), as profile-guided builds do; hot caller: check_graph_store, four times per action.
static inline __attribute__((always_inline)) CheckConversion check_conversion_fields(const CheckConversion* conversion)
{
    const CheckConversion result = {
        .type = __atomic_load_n(&conversion->type, __ATOMIC_RELAXED),
        .flags = __atomic_load_n(&conversion->flags, __ATOMIC_RELAXED),
    };
    return result;
}

static inline __attribute__((always_inline)) void check_graph_store(CheckStream* stream, uint32_t node, const CheckActionResult* result)
{
    check_stream_set_value_flags(stream, node, &result->value, check_value_source_flags((CheckOp)stream->graph->ops[node], result->value.flags));
    CheckNodeConversions* conversions = stream->conversions + node;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(result->conversions); i += 1)
    {
        conversions->operands[i] = check_conversion_fields(&result->conversions[i]);
    }
    conversions->store = check_conversion_fields(&result->store_conversion);
    // Few nodes have a diagnostic or a warning: in a fresh output (CheckStream.zero_columns), the run writes only those
    // that do. A member's offset is written whatever it is, since the member accesses, a node in 13 of sqlite3.c's, touch
    // every line of its column anyway.
    if (result->diagnostic != CHECK_DIAG_NONE || !stream->zero_columns)
    {
        stream->diagnostics[node] = result->diagnostic;
    }
    if (result->diagnostic_operand != 0 || !stream->zero_columns)
    {
        stream->diagnostic_operands[node] = result->diagnostic_operand;
    }
    if (result->warnings != 0 || !stream->zero_columns)
    {
        stream->warnings[node] = result->warnings & ~CHECK_WARNING_ENUM_OPERAND;
        stream->enum_operands |= (result->warnings & CHECK_WARNING_ENUM_OPERAND) != 0;
    }
    stream->member_offsets[node] = result->member_offset;
}

// check_graph_run's marks for the vector scheduler, in its `pure` column: a node a region may reorder (check_graph_pure),
// and a leaf check_graph_region completed before it scheduled the rest.
#define BUSTER_CHECK_MARK_PURE 0x01u
#define BUSTER_CHECK_MARK_DONE 0x02u

// Whether the vector path completes `node` as a leaf (check_graph_leaf): a VALUE without an overload set, about 45% of
// sqlite3.c's body nodes. Without overload candidates no leaf has an overload set (check_graph_validate checks the leaves',
// as check_graph_node_action relies on), and its record isn't read for it.
static bool check_graph_is_leaf(const CheckGraph* graph, uint32_t node)
{
    return graph->ops[node] == CHECK_OP_VALUE && (graph->overload_candidate_count == 0 || check_graph_leaf_overload_head(graph, node) == 0);
}

// The end of the run of marked nodes from `node`, a region of check_graph_run's main loop: the first node from `node`
// without a mark, or `count`. The vector path tests 64 marks at a time, masked to the graph's end, and takes the first
// that is zero; the scalar path, and the vector path after it as a check that ends at once, take each node. Scanned a
// node at a time, its exit, at a data-dependent length, was the main loop's most sampled branch on the 9700X (#1050).
static inline __attribute__((always_inline)) uint32_t check_graph_marked_end(const uint8_t* marks, uint32_t node, uint32_t count, bool scalar)
{
    (void)scalar;

    uint32_t end = node;
#if BUSTER_HOST_VECTOR
    bool open = !BUSTER_SCALAR_PATH(scalar);
    while (open && end < count)
    {
        const uint32_t live = count - end < 64 ? count - end : 64;
        const __mmask64 lanes = (__mmask64)_bzhi_u64(~(uint64_t)0, live);
        const __m512i block = _mm512_maskz_loadu_epi8(lanes, marks + end);
        const uint64_t unmarked = (uint64_t)_mm512_mask_testn_epi8_mask(lanes, block, block);
        open = unmarked == 0;
        end += open ? live : (uint32_t)_tzcnt_u64(unmarked);
    }
#endif
    while (end < count && marks[end] != 0)
    {
        end += 1;
    }
    return end;
}

// Such a leaf's record without its action: what check_graph_scalar_node gives it, which its own leaf decides alone, since a
// VALUE action reads no operand and interns no type. That is the leaf with its type as operand 0's conversion, or for an
// invalid type, that type's diagnostic and a zero value (check_action_compute skips the operator then); its diagnostic
// operand is the run's zero. The vector path writes it in node order, beside its neighbours' records, whose cache lines a
// separate pass of leaves would write a second time (on sqlite3.c, a sixth of the graph's time).
// Always inlined (#880), as profile-guided builds do; hot caller: check_graph_run, once per leaf.
static inline __attribute__((always_inline)) void check_graph_leaf(const TypeTable* types, const CheckGraph* graph, CheckStream* stream, uint32_t node)
{
    const uint32_t slot = graph->leaf_slots[node];
    const CheckGraphLeaf* record = graph->dense_leaves == NULL ? graph->leaf_records + slot : NULL;
    const TypeId type = record != NULL ? record->type : graph->dense_leaves[slot].type;
    const bool valid = type_is_valid(types, type);
    if (record != NULL && valid && record->row == 0 && record->integer == 0 && record->overload_head == 0)
    {
        // A leaf that is its record's columns alone, an identifier's (most leaves): the stream's columns, without a value
        // in memory between them.
        const CheckValue value = {
            .type = record->type,
            .category = (CheckCategory)record->category,
            .flags = record->flags,
            .constant_kind = (CheckConstantKind)record->constant_kind,
            .object = record->object,
        };
        check_stream_set_value(stream, node, &value);
    }
    else
    {
        const CheckValue value = valid ? check_graph_leaf_at(graph, slot) : (CheckValue){ 0 };
        check_stream_set_value(stream, node, &value);
    }
    // In a fresh output (CheckStream.zero_columns) the record's other fields, the warnings, the member's offset and a
    // valid leaf's diagnostic are zero already.
    if (stream->zero_columns)
    {
        stream->conversions[node].operands[0].type = type;
        if (!valid)
        {
            stream->diagnostics[node] = CHECK_DIAG_INVALID_TYPE;
        }
    }
    else
    {
        stream->conversions[node] = (CheckNodeConversions){ .operands = { { .type = type } } };
        stream->diagnostics[node] = valid ? CHECK_DIAG_NONE : CHECK_DIAG_INVALID_TYPE;
        stream->warnings[node] = 0;
        stream->member_offsets[node] = 0;
    }
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
    const char* name = call->info->name;
    const uint32_t length = call->info->name_length;
    // The library builtins (c_builtin_library.c) are calls of their prototypes too, and a listed one folds where clang
    // folds it.
    const bool candidate = cbuiltin_library_candidate(name, length);
    const CBuiltinLibraryId library = candidate ? cbuiltin_library_id(name, length) : CBUILTIN_LIBRARY_COUNT;
    const bool ordinary_call = cbuiltin_fortify_id(name, length) != CBUILTIN_FORTIFY_COUNT || library != CBUILTIN_LIBRARY_COUNT ||
                               (candidate && cbuiltin_library_libgcc(name, length) != BUSTER_CBUILTIN_LIBRARY_LIBGCC) ||
                               (candidate && cbuiltin_library_alias(name, length) != CBUILTIN_LIBRARY_COUNT);
    if (library != CBUILTIN_LIBRARY_COUNT)
    {
        check_builtin_library_fold(types, library, &ordinary, &ordinary_result);
    }
    if (cbuiltin_library_half(library) && result->diagnostic == CHECK_DIAG_NONE)
    {
        // A builtin of __fp16 (#148, c_builtin_library.c): its function type's _Float16 stands for the prototype's __fp16,
        // which the arguments convert to (binary16's rounding is the same) and the call's value has, a folded one too.
        uint32_t count = 0;
        const TypeId* parameters = type_parameters(types, action->type, &count);
        for (uint32_t i = 0; i < count && i < action->argument_count; i += 1)
        {
            if (parameters[i] == BUSTER_TYPE_FLOAT16 && action->argument_conversions[i].type == BUSTER_TYPE_FLOAT16)
            {
                action->argument_conversions[i].type = BUSTER_TYPE_HALF;
            }
        }
        result->value.type = type_unqualified(result->value.type) == BUSTER_TYPE_FLOAT16 ? BUSTER_TYPE_HALF : result->value.type;
    }
    if (ordinary_call)
    {
        // These exact generic signatures have no x86 immediate constraints. Keep the ordinary call's conversions,
        // side effects and diagnostics, without imposing the x86 adapter's 64-argument bound on printf varargs.
        *checked = (CheckBuiltinX86Result){
            .action = ordinary_result,
            .status = ordinary_result.diagnostic == CHECK_DIAG_NONE ? CHECK_BUILTIN_X86_OK : CHECK_BUILTIN_X86_ERROR,
        };
    }
    else
    {
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
}

static void check_graph_x86_action(TypeTable* types, const CheckGraph* graph, CheckStream* stream, const CheckAction* action, CheckActionResult* result)
{
    const uint32_t node = action->node;
    const CheckX86Call* call = graph->x86_calls + stream->x86_count;
    CheckBuiltinX86Result checked;
    check_x86_action(types, call, action, &checked);
    *result = checked.action;
    // A failed hard immediate or target constraint makes the call an invalid expression, as clang's is: its value has no type, so an
    // enclosing call or operator inherits the error (check_graph_inherited_error) rather than reports another. Clang checks
    // no call whose arguments hold an error (#1066).
    const bool hard = checked.constraint == CHECK_BUILTIN_X86_CONSTRAINT_ICE || checked.constraint == CHECK_BUILTIN_X86_CONSTRAINT_RC ||
                      checked.constraint == CHECK_BUILTIN_X86_CONSTRAINT_SAE || checked.constraint == CHECK_BUILTIN_X86_CONSTRAINT_SCALE ||
                      checked.constraint == CHECK_BUILTIN_X86_CONSTRAINT_TARGET32;
    if (checked.status == CHECK_BUILTIN_X86_ERROR && hard)
    {
        result->value = (CheckValue){ 0 };
    }
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
static uint32_t check_graph_atomic_address_alignment(const TypeTable* types, const CheckGraph* graph, const CheckStream* stream, uint32_t node,
                                                    const CheckBuiltinCall* witness, bool lvalue, bool declaration_types)
{
    CheckAlignmentStep steps[BUSTER_CHECK_ALIGNMENT_STEPS];
    uint32_t count = 0;
    uint32_t result = UINT32_MAX;
    bool declaration = false;
    bool done = false;
    while (!done && node < stream->count && count < BUSTER_CHECK_ALIGNMENT_STEPS)
    {
        const CheckOp op = (CheckOp)graph->ops[node];
        const TypeId type = check_stream_type(stream, node);
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
            const TypeId inner = check_stream_type(stream, operand);
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
            const uint8_t left = check_action_kind(types, check_stream_type(stream, operand));
            const bool reverse = op != CHECK_OP_SUBTRACT && left != BUSTER_TYPE_KIND_POINTER && !type_kind_is_array(left);
            const uint32_t base = reverse ? second : operand;
            const CheckValue index_value = check_stream_value(stream, reverse ? operand : second);
            const CheckValue* index = &index_value;
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
            lvalue = type_kind_is_array(check_action_kind(types, check_stream_type(stream, base)));
        }
        else if (!lvalue)
        {
            result = type_alignment_log2(types, type_element(types, type_strip_aligned(types, type)));
            done = true;
        }
        else if (op == CHECK_OP_VALUE)
        {
            const bool witnessed = witness != NULL && witness->base_alignment != 0 && graph->tokens != NULL && graph->tokens[node] == witness->base_token;
            result = witnessed ? (uint32_t)witness->base_alignment - 1 : declaration_types ? type_alignment_log2(types, type) : UINT32_MAX;
            declaration = true;
            done = true;
        }
        else if (op == CHECK_OP_DEREFERENCE && operand < node)
        {
            node = operand;
            lvalue = false;
        }
        else if ((op == CHECK_OP_MEMBER || op == CHECK_OP_ARROW) && operand < node && (check_stream_flags(stream, node) & BUSTER_CHECK_BIT_FIELD) == 0)
        {
            TypeId record = type_strip_aligned(types, check_stream_type(stream, operand));
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

static uint32_t check_graph_atomic_alignment(const TypeTable* types, const CheckGraph* graph, const CheckStream* stream, uint32_t node, const CheckBuiltinCall* witness)
{
    return check_graph_atomic_address_alignment(types, graph, stream, node, witness, false, false);
}

// A member access's counted_by family count (#148), as clang's findCountedByField finds it: `node`'s member, past the
// parentheses, which the graph doesn't keep, of a struct or union through a `.` or an `->`; the member's kind and the
// count's member, looked up by its name in the access's record, its type and offset there, and the member's offset.
typedef struct CheckCountedMember CheckCountedMember;
struct CheckCountedMember
{
    uint32_t base;
    bool arrow;
    bool member;
    uint8_t kind;
    TypeId member_type;
    TypeId count_type;
    bool count_found;
    bool count_bit_field;
    // In bytes, from the access's record's start.
    uint64_t member_offset;
    uint64_t count_offset;
};

// A `.` (or an `->`, `arrow`) access of member `name` of a value of type `base_type`, as check_graph_counted_member reads
// it; the constant evaluator's too (check_constant_call), which has no graph.
static CheckCountedMember check_counted_lookup(const TypeTable* types, TypeId base_type, bool arrow, uint32_t name)
{
    CheckCountedMember result = { .base = BUSTER_CHECK_NONE };
    {
        const TypeId stripped = type_strip_aligned(types, base_type);
        const TypeId record = arrow ? (type_kind(types, stripped) == BUSTER_TYPE_KIND_POINTER ? type_strip_aligned(types, type_element(types, stripped)) : 0) : stripped;
        const uint8_t kind = type_kind(types, record);
        uint64_t offset = 0;
        uint32_t qualifiers = 0;
        const uint32_t member = kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION ? type_lookup_member(types, record, name, &offset, &qualifiers) : 0;
        result.member = member != 0 && member != BUSTER_TYPE_MEMBER_UNKNOWN;
        if (result.member)
        {
            result.arrow = arrow;
            result.member_type = types->members.type[member];
            result.member_offset = offset / 8;
            result.kind = (uint8_t)((types->members.flags[member] & BUSTER_TYPE_MEMBER_COUNT_MASK) >> BUSTER_TYPE_MEMBER_COUNT_SHIFT);
            uint64_t count_offset = 0;
            const uint32_t count = result.kind != BUSTER_TYPE_COUNT_NONE ? type_lookup_member(types, record, types->members.count_name[member], &count_offset, &qualifiers) : 0;
            result.count_found = count != 0 && count != BUSTER_TYPE_MEMBER_UNKNOWN;
            result.count_type = result.count_found ? types->members.type[count] : 0;
            result.count_bit_field = result.count_found && (types->members.flags[count] & BUSTER_TYPE_MEMBER_BIT_FIELD) != 0;
            result.count_offset = count_offset / 8;
        }
    }
    return result;
}

static CheckCountedMember check_graph_counted_member(const TypeTable* types, const CheckGraph* graph, const CheckStream* stream, uint32_t node)
{
    CheckCountedMember result = { .base = BUSTER_CHECK_NONE };
    const uint8_t op = node < graph->count ? graph->ops[node] : CHECK_OP_COUNT;
    const uint32_t base = op == CHECK_OP_MEMBER || op == CHECK_OP_ARROW ? graph->operand0[node] : BUSTER_CHECK_NONE;
    if (base < graph->count)
    {
        result = check_counted_lookup(types, check_stream_type(stream, base), op == CHECK_OP_ARROW, graph->members[node]);
        result.base = result.member ? base : BUSTER_CHECK_NONE;
    }
    return result;
}

// __builtin_dynamic_object_size's argument by clang's counted_by forms (#148, findStructFieldAccess and emitCountedBySize):
// past casts, `&`, `*` and a comma's right operand, at most one subscript (its index and its base's type kept), to a
// member access. A pointer member read as a value (no `&` before it) is CHECK_COUNTED_SIZE_POINTER, its own count's;
// a flexible array member (or a trailing array) CHECK_COUNTED_SIZE_ARRAY; any other member, `&` before an array aside,
// CHECK_COUNTED_SIZE_FIELD, its record's flexible array member's count's, as clang's FindFlexibleArrayMemberField finds
// it (the first trailing array of the record's members, theirs too). `counted` is the counted member's (the pointer's
// or the flexible array's), its count's offset from the access's record.
#define CHECK_COUNTED_SIZE_NONE 0
#define CHECK_COUNTED_SIZE_ARRAY 1
#define CHECK_COUNTED_SIZE_FIELD 2
#define CHECK_COUNTED_SIZE_POINTER 3

typedef struct CheckCountedSize CheckCountedSize;
struct CheckCountedSize
{
    uint8_t shape;
    // The member found is itself of a counted_by family attribute, and no pointer read as a value: clang's evaluator then
    // leaves the size to the code, whatever it knows of the object.
    bool vetoed;
    // The member access, its subscript's index (BUSTER_CHECK_NONE for none) and the subscript's base's type.
    uint32_t member;
    uint32_t index;
    TypeId index_base_type;
    CheckCountedMember counted;
    // The counted member's element's bytes (a pointer's pointee's, 1 for void), and for CHECK_COUNTED_SIZE_FIELD the field's
    // offset and its record's size, and its own element's bytes (an array field's, for its index).
    uint64_t element_bytes;
    uint64_t field_offset;
    uint64_t record_bytes;
    uint64_t field_element_bytes;
};

// Whether member `member` (TypeMembers' index) is its record's last, and so a trailing array's place.
static bool check_counted_last_member(const TypeTable* types, uint32_t member)
{
    bool result = false;
    for (uint32_t tag = 1; !result && tag < types->tags.count; tag += 1)
    {
        const uint32_t begin = types->tags.member_begin[tag];
        result = types->tags.member_count[tag] != 0 && member == begin + types->tags.member_count[tag] - 1;
    }
    return result;
}

// The record's flexible array member as clang's FindFlexibleArrayMemberField finds it: its members in order, a record's
// own members before the next (an explicit stack of them, no recursion), the first array that is its record's last
// member. Its index, its type and its name, or 0.
static uint32_t check_counted_flexible(const TypeTable* types, TypeId record)
{
    uint32_t stack[16];
    uint32_t ends[16];
    uint32_t depth = 0;
    uint32_t result = 0;
    const uint32_t tag = types->types.index[type_index(record)];
    stack[0] = types->tags.member_begin[tag];
    ends[0] = stack[0] + types->tags.member_count[tag];
    depth = 1;
    for (uint32_t steps = 0; result == 0 && depth != 0 && steps < 4096; steps += 1)
    {
        if (stack[depth - 1] >= ends[depth - 1])
        {
            depth -= 1;
        }
        else
        {
            const uint32_t member = stack[depth - 1]++;
            const TypeId type = type_strip_aligned(types, types->members.type[member]);
            const uint8_t kind = type_kind(types, type);
            const bool array = kind == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY || kind == BUSTER_TYPE_KIND_ARRAY;
            if (array && check_counted_last_member(types, member))
            {
                result = member;
            }
            else if ((kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION) && depth < 16 && type_is_complete(types, type))
            {
                const uint32_t inner = types->types.index[type_index(type)];
                stack[depth] = types->tags.member_begin[inner];
                ends[depth] = stack[depth] + types->tags.member_count[inner];
                depth += 1;
            }
        }
    }
    return result;
}

static CheckCountedSize check_graph_counted_size(const TypeTable* types, const CheckGraph* graph, const CheckStream* stream, uint32_t argument)
{
    CheckCountedSize result = { .member = BUSTER_CHECK_NONE, .index = BUSTER_CHECK_NONE };
    uint32_t node = argument;
    bool address = false;
    bool walking = true;
    for (uint32_t depth = 0; walking && depth < 256 && node < graph->count; depth += 1)
    {
        const uint8_t op = graph->ops[node];
        if (op == CHECK_OP_CAST || op == CHECK_OP_CONVERT)
        {
            node = graph->operand0[node];
        }
        else if (op == CHECK_OP_ADDRESS || op == CHECK_OP_DEREFERENCE)
        {
            address = op == CHECK_OP_ADDRESS;
            node = graph->operand0[node];
        }
        else if (op == CHECK_OP_COMMA)
        {
            node = graph->operand1[node];
        }
        else if (op == CHECK_OP_SUBSCRIPT && result.index == BUSTER_CHECK_NONE)
        {
            address = false;
            result.index = graph->operand1[node];
            result.index_base_type = check_stream_type(stream, graph->operand0[node]);
            node = graph->operand0[node];
        }
        else
        {
            result.member = op == CHECK_OP_MEMBER || op == CHECK_OP_ARROW ? node : BUSTER_CHECK_NONE;
            walking = false;
        }
    }
    const CheckCountedMember member = result.member != BUSTER_CHECK_NONE ? check_graph_counted_member(types, graph, stream, result.member) : (CheckCountedMember){ 0 };
    const TypeId member_type = type_strip_aligned(types, member.member_type);
    const uint8_t member_kind = type_kind(types, member_type);
    const bool array = member_kind == BUSTER_TYPE_KIND_ARRAY || member_kind == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY;
    const bool pointer = member_kind == BUSTER_TYPE_KIND_POINTER;
    result.vetoed = member.member && member.kind != BUSTER_TYPE_COUNT_NONE && !(pointer && !address) && !(address && array);
    if (member.member && !(address && array))
    {
        const TypeId base_type = type_strip_aligned(types, check_stream_type(stream, member.base));
        const TypeId record = member.arrow ? type_strip_aligned(types, type_element(types, base_type)) : base_type;
        uint64_t offset = 0;
        uint32_t qualifiers = 0;
        const uint32_t index = type_lookup_member(types, record, graph->members[result.member], &offset, &qualifiers);
        const bool trailing = array && index != 0 && index != BUSTER_TYPE_MEMBER_UNKNOWN && check_counted_last_member(types, index);
        if (pointer && !address)
        {
            result.shape = CHECK_COUNTED_SIZE_POINTER;
            result.counted = member;
            const TypeId pointee = type_strip_aligned(types, type_element(types, member_type));
            const uint64_t bytes = type_kind(types, pointee) == BUSTER_TYPE_KIND_VOID || type_kind(types, pointee) == BUSTER_TYPE_KIND_FUNCTION ? 0 :
                                   type_size(types, pointee);
            result.element_bytes = bytes != 0 ? bytes : 1;
        }
        else if (trailing)
        {
            result.shape = CHECK_COUNTED_SIZE_ARRAY;
            result.counted = member;
            result.element_bytes = type_size(types, type_strip_aligned(types, type_element(types, member_type)));
        }
        else if (type_is_complete(types, record))
        {
            // Another member: its record's flexible array member's count, the field's offset and the record's size.
            const uint32_t flexible = check_counted_flexible(types, record);
            const CheckCountedMember counted = flexible != 0 ? check_counted_lookup(types, record, false, types->members.name[flexible]) : (CheckCountedMember){ 0 };
            result.shape = flexible != 0 ? CHECK_COUNTED_SIZE_FIELD : CHECK_COUNTED_SIZE_NONE;
            result.counted = counted;
            result.counted.base = member.base;
            result.counted.arrow = member.arrow;
            result.element_bytes = flexible != 0 ? type_size(types, type_strip_aligned(types, type_element(types, types->members.type[flexible]))) : 0;
            result.field_offset = member.member_offset;
            result.record_bytes = type_size(types, record);
            result.field_element_bytes = array ? type_size(types, type_strip_aligned(types, type_element(types, member_type))) : 0;
        }
    }
    // The member's count: of a counted_by family attribute, found, no bit-field's. Another member's size clang's evaluator
    // folds where the access's object is named (`s.x`, `(&s)->x`); its code counts only through a pointer.
    const bool counted = result.counted.member && result.counted.kind != BUSTER_TYPE_COUNT_NONE && result.counted.count_found && !result.counted.count_bit_field;
    const uint8_t base_op = member.member && member.base < graph->count ? graph->ops[member.base] : CHECK_OP_COUNT;
    const bool named = base_op == CHECK_OP_ADDRESS || (!member.arrow && base_op == CHECK_OP_VALUE);
    result.shape = counted && !(result.shape == CHECK_COUNTED_SIZE_FIELD && named) ? result.shape : CHECK_COUNTED_SIZE_NONE;
    return result;
}

static void check_graph_special_action(TypeTable* types, const CheckGraph* graph, CheckStream* stream, const CheckAction* action, CheckActionResult* result)
{
    const uint32_t node = action->node;
    const CheckBuiltinCall* witness = check_graph_builtin_call(graph, node);
    if (action->op == CHECK_OP_BUILTIN_MISC)
    {
        const bool counted_ref = action->member == CBUILTIN_COUNTED_BY_REF && graph->argument_counts[node] == 1;
        const CheckCountedMember counted = counted_ref ? check_graph_counted_member(types, graph, stream, graph->argument_nodes[graph->argument_offsets[node]]) :
                                                         (CheckCountedMember){ .base = BUSTER_CHECK_NONE };
        const CheckBuiltinMiscAction input = {
            .id = (CBuiltinId)action->member,
            .arguments = action->arguments,
            .argument_count = action->argument_count,
            .node = node,
            .context = action->context,
            .pointer_object = witness != NULL ? witness->base_object : 0,
            .pointer_alignment = witness != NULL ? witness->base_alignment : 0,
            .alignment_evaluated = witness != NULL && witness->alignment_evaluated,
            .alignment_side_effects = witness != NULL && witness->alignment_side_effects,
            .alignment_value = witness != NULL ? (unsigned __int128)witness->alignment_high << 64 | witness->alignment_low : 0,
            .alignment_limbs = witness != NULL ? witness->alignment_limbs : 0,
            .string_known = witness != NULL && witness->nan_payload_known,
            .string_cast = witness != NULL && witness->nan_payload_cast,
            .string_utf8 = witness != NULL && witness->nan_payload_utf8,
            .alloc_token_known = witness != NULL && witness->alloc_token_known,
            .alloc_token = witness != NULL ? witness->alloc_token : 0,
            .static_chain_call = witness != NULL && witness->static_chain_call,
            .cfstring_object = graph->tokens != NULL && graph->tokens[node] < UINT32_MAX ? graph->tokens[node] + 1 : 0,
            .os_log_items = witness != NULL ? witness->os_log_items : NULL,
            .os_log_count = witness != NULL ? witness->os_log_count : 0,
            .os_log_known = witness != NULL && witness->os_log_known,
            .os_log_invalid_mask = witness != NULL && witness->os_log_invalid_mask,
            .os_log_taken = { witness != NULL ? witness->os_log_taken[0] : 0, witness != NULL ? witness->os_log_taken[1] : 0,
                              witness != NULL ? witness->os_log_taken[2] : 0, witness != NULL ? witness->os_log_taken[3] : 0 },
            .function_objects = witness != NULL ? witness->function_objects : NULL,
            .function_object_count = witness != NULL ? witness->function_object_count : 0,
            .string_bytes = witness != NULL ? witness->nan_payload : NULL,
            .string_length = witness != NULL ? witness->nan_payload_length : 0,
            .ns_constant_string = graph->ns_constant_string,
            .counted_member = !counted.member ? BUSTER_CHECK_COUNTED_NONE :
                              counted.kind == BUSTER_TYPE_COUNTED_BY && counted.count_found ? BUSTER_CHECK_COUNTED_COUNTED : BUSTER_CHECK_COUNTED_MEMBER,
            .counted_type = counted.count_type,
        };
        CheckBuiltinMiscResult checked;
        check_builtin_misc(types, &input, &checked, 1);
        *result = checked.action;
        check_builtin_library_fold_memory(types, input.id, action, checked.argument_conversions, result);
        result->warnings |= (checked.warnings & BUSTER_CHECK_BUILTIN_MISC_WARN_QUALIFIERS) != 0 ? CHECK_WARNING_DISCARDED_QUALIFIERS : 0;
        result->warnings |= (checked.warnings & BUSTER_CHECK_BUILTIN_MISC_WARN_POINTER_SIGN) != 0 ? CHECK_WARNING_POINTER_SIGN : 0;
        for (uint32_t i = 0; i < action->argument_count && i < 4; i += 1)
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
            .cpu_test = checked.cpu_test,
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
                .context = action->context,
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
                fp_conversion |= (action->context.fp_environment & BUSTER_CHECK_FP_ENV_ACCESS) != 0 &&
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
                .context = action->context,
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
                .class_mask = checked.class_mask,
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
            .context = action->context,
            .minimum_alignment = walked < 64 ? UINT64_C(1) << walked : 0,
            .alignment_known = walked < 64,
        };
        // A read/modify/write's own rules (the orders' ICE proofs from the source's analysis); its atomic part is what the
        // effect records.
        CheckBuiltinAtomicResult checked;
        check_builtin_atomic_family(types, &input, &checked);
        *result = checked.action;
        result->warnings |= (checked.warnings & BUSTER_CHECK_BUILTIN_ATOMIC_WARN_QUALIFIERS) != 0 ? CHECK_WARNING_DISCARDED_QUALIFIERS : 0;
        for (uint32_t i = 0; i < action->argument_count && i < BUSTER_ARRAY_LENGTH(checked.argument_conversions); i += 1)
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
            .late_completion = checked.late_completion,
            .answers = { checked.answers[0], checked.answers[1] },
            .query_size = checked.query_size,
            .query_pointer = checked.query_pointer,
            .space_target = checked.space_target,
        };
    }
    else if (action->op == CHECK_OP_BUILTIN_VECTOR)
    {
        const CheckBuiltinVectorAction input = {
            .id = (CBuiltinId)action->member,
            .arguments = action->arguments,
            .argument_count = action->argument_count,
            .destination_type = action->type,
            .fp_evaluation_type = check_action_fp_evaluation_type(action->context.fp_environment),
            .node = node,
            .context = action->context,
            .shuffle_index_ice = witness != NULL ? witness->shuffle_index_ice : NULL,
            .shuffle_index_ice_count = witness != NULL ? witness->shuffle_index_ice_count : 0,
            .allow_gnu_index_ice = (action->context.extensions & BUSTER_CHECK_EXTENSION_GNU) != 0,
        };
        CheckBuiltinVectorResult checked;
        check_builtin_vector(types, &input, &checked, 1);
        *result = checked.action;
        result->warnings |= (checked.warnings & BUSTER_CHECK_BUILTIN_VECTOR_WARN_QUALIFIERS) != 0 ? CHECK_WARNING_DISCARDED_QUALIFIERS : 0;
        for (uint32_t i = 0; i < action->argument_count && i < BUSTER_CHECK_BUILTIN_VECTOR_ARGUMENTS; i += 1)
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
            .context = action->context,
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
            .context = action->context,
            .string_literal_start = witness != NULL && witness->string_literal_start,
            .absolute_address_constant = witness != NULL && witness->absolute_address_constant,
            .complex_constant = witness != NULL && witness->complex_constant,
        };
        input.constant_p_context_sensitive = input.id == CBUILTIN_CONSTANT_P &&
            ((action->context.fp_environment & BUSTER_CHECK_FP_ENV_ACCESS) != 0 ||
             (action->argument_count == 1 && (action->arguments[0].flags & BUSTER_CHECK_FP_ENVIRONMENT) != 0));
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
        // clang's evaluator leaves __builtin_dynamic_object_size of a counted flexible array member (or a pointer
        // member's access through one) to its code (#148): no bound of the object folds it.
        const CheckCountedSize counted = input.id == CBUILTIN_DYNAMIC_OBJECT_SIZE && action->argument_count == 2 ?
                                         check_graph_counted_size(types, graph, stream, graph->argument_nodes[graph->argument_offsets[node]]) :
                                         (CheckCountedSize){ .shape = CHECK_COUNTED_SIZE_NONE };
        if (counted.vetoed)
        {
            memset(input.object_size_known, 0, sizeof(input.object_size_known));
            memset(input.object_size_source_constant, 0, sizeof(input.object_size_source_constant));
        }
        CheckBuiltinQueryResult checked;
        check_builtin_query(types, &input, &checked, 1);
        // Its counted_by code reads the argument's base (check_lower_counted_size): a reference of its operand, which
        // has no side effects there.
        if (counted.shape != CHECK_COUNTED_SIZE_NONE && checked.mode != 3 && checked.action.diagnostic == CHECK_DIAG_NONE && !input.object_size_known[checked.mode] &&
            (action->arguments[0].flags & BUSTER_CHECK_SIDE_EFFECT) == 0)
        {
            checked.evaluated_operands |= 1;
        }
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
                                       (action->context.fp_environment & BUSTER_CHECK_FP_ENV_ACCESS) != 0 && check_builtin_float_strict(action->member);
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
                fp_conversion |= (action->context.fp_environment & BUSTER_CHECK_FP_ENV_ACCESS) != 0 &&
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

// A node's action, its result stored, with `guards` receiving what it read of its source position (CheckAction.guards),
// or null. Always inlined (#880), as profile-guided builds do; hot caller: check_graph_scalar_node, once per impure node.
static inline __attribute__((always_inline)) void check_graph_node_action(TypeTable* types, const CheckGraph* graph, CheckStream* stream, uint32_t node, CheckValue* arguments,
                                                                          Arena* scratch, bool scalar, CheckActionGuards* guards)
{
    (void)scalar;

    CheckAction action;
    CheckActionResult result;
    check_graph_action(graph, stream, node, arguments, &action);
    action.scalar = scalar;
    action.guards = guards;
    // Without overload candidates every value's overload head is zero (check_graph_validate checks the leaves', and the
    // actions only preserve their operands'): no call is contextual, and no operand takes an overload (#530).
    // The cases before the action's computation, the last, are rare at a node, and each begins with a hinted test (#1175):
    // without, clang jumped around each case once a node. A C unit has no overloads.
    const bool overloads = graph->overload_candidate_count != 0;
    const bool overload = __builtin_expect(stream->overload_count < graph->overload_count, 0) && action.op == CHECK_OP_CALL &&
                          graph->overload_calls[stream->overload_count].node == node;
    bool contextual_call = overload || (__builtin_expect(action.operands[0].overload_head != 0, 0) && action.op == CHECK_OP_CALL);
    for (uint32_t i = 0; __builtin_expect(overloads, 0) && action.op == CHECK_OP_CALL && i < action.argument_count; i += 1)
    {
        contextual_call |= action.arguments[i].overload_head != 0;
    }
    const CheckDiagnostic contextual = __builtin_expect(overloads, 0) && !contextual_call ? check_graph_overload_operands(types, graph, stream, &action, scratch) : CHECK_DIAG_NONE;
    if (__builtin_expect(contextual != CHECK_DIAG_NONE, 0))
    {
        result = (CheckActionResult){ .diagnostic = contextual };
    }
    else if (__builtin_expect(contextual_call, 0))
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
    else if (__builtin_expect((action.op == CHECK_OP_BUILTIN_UNARY) | (action.op == CHECK_OP_BUILTIN_OVERFLOW), 0))
    {
        const uint32_t count = action.op == CHECK_OP_BUILTIN_UNARY ? 1 : 3;
        const CheckBuiltinAction builtin = {
            .id = (CBuiltinId)action.member,
            .operands = { action.operands[0], action.operands[1], action.operands[2] },
            .argument_count = count,
            .node = node,
            .context = action.context,
        };
        CheckBuiltinResult checked;
        CheckBuiltinTypedOverflowResult typed = { 0 };
        const bool typed_builtin = check_builtin_typed_overflow_info(builtin.id) != NULL;
        if (typed_builtin)
        {
            check_builtin_typed_overflow(types, &builtin, &typed, 1, true);
            checked = typed.builtin;
        }
        else
        {
            check_builtins(types, &builtin, &checked, 1, true);
        }
        result = checked.action;
        if ((action.context.fp_environment & BUSTER_CHECK_FP_ENV_ACCESS) != 0)
        {
            check_action_fp_environment(types, &action, &result);
        }
        result.warnings |= (typed.warnings & BUSTER_CHECK_BUILTIN_TYPED_OVERFLOW_WARN_QUALIFIERS) != 0 ? CHECK_WARNING_DISCARDED_QUALIFIERS : 0;
        if (checked.store && result.diagnostic == CHECK_DIAG_NONE)
        {
            stream->effects[stream->effect_count++] = (CheckStoreEffect){
                .node = node,
                .value = checked.store_value,
                .destination = checked.store_destination,
                .runtime_builtin = typed_builtin ? typed.runtime_builtin : builtin.id,
                .evaluated_operands = typed_builtin ? typed.evaluated_operands : 7,
                .warnings = typed.warnings,
                .warning_operands = typed.warning_operands,
                .unsupported = typed.unsupported,
            };
        }
    }
    else if (__builtin_expect(action.op == CHECK_OP_VARARGS, 0))
    {
        const CheckVarargsCall* call = &graph->varargs_calls[graph->argument_offsets[node]];
        const CheckVarargsAction builtin = {
            .id = (CBuiltinId)action.member,
            .operands = { action.operands[0], action.operands[1] },
            .argument_count = graph->argument_counts[node],
            .requested_type = action.type,
            .second_argument_declaration = call->second_argument_declaration,
            .node = node,
            .context = action.context,
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
            .microsoft = checked.microsoft,
        };
    }
    else if (__builtin_expect(check_graph_has_arguments(action.op) & (action.op != CHECK_OP_CALL), 0))
    {
        check_graph_special_action(types, graph, stream, &action, &result);
    }
    else
    {
        // check_action_scalar's own call, which its 15 other callers share, would keep the action out of line (#880).
        check_action_compute(types, &action, &result, 0, NULL);
        // A call of a plain library builtin's declaration that clang folds (#148, the reader's mark in its member).
        if (__builtin_expect(action.op == CHECK_OP_CALL, 0) && (action.member & BUSTER_CHECK_LIBRARY_CALL) != 0)
        {
            check_builtin_library_fold_plain(types, action.member, &action, &result);
        }
    }
    if (__builtin_expect(action.op == CHECK_OP_STATEMENT_VALUE, 0))
    {
        check_graph_region_effects(types, graph, stream, node, scratch, &result);
    }
    check_graph_store(stream, node, &result);
    stream->statistics.scalar_nodes += 1;
}

// check_graph_node_action out of line, for the memoized transitions (c_check_transitions.c).
static void check_graph_guarded_node(TypeTable* types, const CheckGraph* graph, CheckStream* stream, uint32_t node, CheckValue* arguments, Arena* scratch, bool scalar,
                                     CheckActionGuards* guards)
{
    check_graph_node_action(types, graph, stream, node, arguments, scratch, scalar, guards);
}

// Whether node `node` is a discard of an rvalue that check_graph_discard takes, with that rvalue in `value`: a cast to void,
// a statement's value as the reader gives it (check_input_expression_terminator), of a valid rvalue without an overload set
// that is no array, function, vector or complex value, outside a constant evaluation and FP_ENV_ACCESS, in a graph without
// overload candidates. Its action then reads nothing else and makes no diagnostic (check_action_compute through
// check_action_cast): the operand's conversion is none, and the cast's checks of its target all pass for void.
// Always inlined (#880), as profile-guided builds do; hot caller: check_graph_scalar_node, once per impure node.
static inline __attribute__((always_inline)) bool check_graph_discard_taken(const TypeTable* types, const CheckGraph* graph, const CheckStream* stream, uint32_t node, CheckValue* value)
{
    bool taken = graph->ops[node] == CHECK_OP_CAST && graph->types != NULL && graph->types[node] == BUSTER_TYPE_VOID && graph->overload_candidate_count == 0 &&
                 (stream->constant_evaluated == NULL || stream->constant_evaluated[node] == 0) &&
                 (graph->fp_environments == NULL || (graph->fp_environments[node] & BUSTER_CHECK_FP_ENV_ACCESS) == 0) && graph->operand0[node] != BUSTER_CHECK_NONE;
    if (taken)
    {
        check_stream_value_to(stream, graph->operand0[node], value);
        taken = type_is_valid(types, value->type) && value->overload_head == 0 && value->category == CHECK_RVALUE;
    }
    if (taken)
    {
        const uint8_t kind = check_action_kind(types, value->type);
        taken = !type_kind_is_array(kind) && kind != BUSTER_TYPE_KIND_FUNCTION && kind != BUSTER_TYPE_KIND_VECTOR && kind != BUSTER_TYPE_KIND_EXT_VECTOR &&
                kind != BUSTER_TYPE_KIND_COMPLEX;
    }
    return taken;
}

// A discard's record (check_graph_discard_taken), without its action: what check_action_compute gives a cast to void of the
// rvalue `value`, by the same steps, check_action_cast's conversion of it to void and its provenance, and the source flags.
// On sqlite3.c's -c, a statement's discard is one node in eight, and its action's machinery (building the action, its
// result's clearing and the dispatch) most of its time (#1050). The scalar path takes the action, the twin whose streams the
// tests compare.
// Always inlined (#880), as profile-guided builds do; hot caller: check_graph_scalar_node, once per discard.
static inline __attribute__((always_inline)) void check_graph_discard(TypeTable* types, CheckStream* stream, uint32_t node, CheckValue* value)
{
    // The cast converts its operand, then takes it as its value: here the value is converted where the record holds it,
    // the same bytes without a copy after the conversion's narrow stores.
    CheckActionResult result;
    memset(&result, 0, sizeof(result));
    result.conversions[0].type = value->type;
    check_value_copy(&result.value, value);
    check_action_conversion(types, &result.value, BUSTER_TYPE_VOID, BUSTER_CHECK_CONVERT_CAST, &result.conversions[0], node);
    check_action_cast_provenance(&result.value, false, false, false, true, true);
    result.value.flags = check_value_source_flags(CHECK_OP_CAST, result.value.flags);
    check_graph_store(stream, node, &result);
    stream->statistics.scalar_nodes += 1;
}

// Always inlined (#880), as profile-guided builds do; hot caller: check_graph_run, once per impure node.
static inline __attribute__((always_inline)) void check_graph_scalar_node(TypeTable* types, const CheckGraph* graph, CheckStream* stream, uint32_t node, CheckValue* arguments, Arena* scratch, bool scalar)
{
    (void)scalar;

    CheckValue discarded;
    if (!BUSTER_SCALAR_PATH(scalar) && check_graph_discard_taken(types, graph, stream, node, &discarded))
    {
        check_graph_discard(types, stream, node, &discarded);
    }
    else
    {
        check_graph_node_action(types, graph, stream, node, arguments, scratch, scalar, NULL);
    }
}

// Count/place/fill a stable level order inside one mutation-free postorder region (CheckOptions.level_order: the
// default is check_graph_region_postorder's batches, which the benchmark measured faster). Dependencies before begin
// are already complete. The region's leaves complete first, in node order (check_graph_leaf), and `marks` keeps them
// out of the schedule. Relative heights keep all histogram clears together linear even for many tiny regions.
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

// A mutation-free region in postorder, the vector path's default, and its level order's alternative (check_graph_region,
// CheckOptions.level_order), which the benchmark measured slower on sqlite3.c's bodies: its leaves complete first, as
// there; then, from the first node not yet done, a batch of up to `batch_size` consecutive nodes that read none of
// each other (an operand in the region not done is the batch's, an earlier member's), checked as one of level order's
// batches is, through the same actions. A batch ends at the first node that reads a member, which in a tree's postorder
// is most often the next one: a parent follows its last child.
static void check_graph_region_postorder(TypeTable* types, const CheckGraph* graph, CheckStream* stream, uint8_t* marks, uint32_t begin, uint32_t end,
                                         uint32_t batch_size)
{
    for (uint32_t node = begin; node < end; node += 1)
    {
        if (graph->ops[node] == CHECK_OP_VALUE)
        {
            check_graph_leaf(types, graph, stream, node);
            marks[node] |= BUSTER_CHECK_MARK_DONE;
        }
    }
    uint32_t node = begin;
    while (node < end)
    {
        uint32_t members[BUSTER_CHECK_BATCH];
        uint32_t count = 0;
        bool open = true;
        while (open && node < end && count < batch_size)
        {
            if ((marks[node] & BUSTER_CHECK_MARK_DONE) == 0)
            {
                const uint32_t operands[] = { graph->operand0[node], graph->operand1[node], graph->operand2[node] };
                for (uint32_t i = 0; i < 3; i += 1)
                {
                    open &= operands[i] == BUSTER_CHECK_NONE || operands[i] < begin || (marks[operands[i]] & BUSTER_CHECK_MARK_DONE) != 0;
                }
                // Only the operators whose integer folds a batch takes 8 lanes at a time (check_action_fold_operator)
                // join it. Another node whose operands are done is checked at once, a discard without its action
                // (check_graph_discard), as the batch's actions read none of each other and a region's nodes intern no
                // type: the same records, and the same table. On sqlite3.c's -c a batch held 1.4 nodes, and its
                // machinery cost more than it saved where no fold went with it (#1050).
                CheckValue discarded;
                if (open && check_graph_discard_taken(types, graph, stream, node, &discarded))
                {
                    check_graph_discard(types, stream, node, &discarded);
                    marks[node] |= BUSTER_CHECK_MARK_DONE;
                    node += 1;
                }
                else if (open && !check_action_fold_operator(graph->ops[node]))
                {
                    check_graph_node_action(types, graph, stream, node, 0, 0, false, NULL);
                    marks[node] |= BUSTER_CHECK_MARK_DONE;
                    node += 1;
                }
                else if (open || count == 0)
                {
                    members[count++] = node;
                    node += 1;
                }
            }
            else
            {
                node += 1;
            }
        }
        if (count == 1)
        {
            check_graph_scalar_node(types, graph, stream, members[0], 0, 0, false);
        }
        else if (count != 0)
        {
            CheckAction actions[BUSTER_CHECK_BATCH];
            CheckActionResult results[BUSTER_CHECK_BATCH];
            for (uint32_t i = 0; i < count; i += 1)
            {
                check_graph_action(graph, stream, members[i], 0, &actions[i]);
                const bool vector = check_action_vector_eligible(types, &actions[i]);
                stream->statistics.vector_nodes += (uint32_t)vector;
                stream->statistics.scalar_nodes += (uint32_t)!vector;
            }
            check_actions(types, actions, results, count);
            for (uint32_t i = 0; i < count; i += 1)
            {
                check_graph_store(stream, members[i], &results[i]);
            }
        }
        for (uint32_t i = 0; i < count; i += 1)
        {
            marks[members[i]] |= BUSTER_CHECK_MARK_DONE;
        }
        stream->statistics.batches += count != 0;
    }
}

#include "c_check_transitions.c"

static void check_graph_report(const CheckGraph* graph, CheckStream* stream, uint32_t node, CheckMessage message, CheckDiagnostic diagnostic, CheckFault fault)
{
    uint32_t token = graph->tokens != 0 ? graph->tokens[node] : node;
    const uint32_t operand = message == CHECK_MESSAGE_ACTION ? stream->diagnostic_operands[node] : 0;
    if (graph->ops[node] == CHECK_OP_CALL && graph->argument_tokens != NULL && operand != 0 && operand <= graph->argument_counts[node])
    {
        token = graph->argument_tokens[graph->argument_offsets[node] + operand - 1];
    }
    else if ((diagnostic == CHECK_DIAG_REGISTER_ADDRESS && graph->ops[node] != CHECK_OP_ADDRESS && graph->ops[node] != CHECK_OP_CALL && operand < 3) ||
             (diagnostic == CHECK_DIAG_OVERFLOW_BIT_INT && operand < 3))
    {
        // A register array's decay, at its name, as clang's DefaultFunctionArrayConversion reports it (#1447), and a
        // mul_overflow's signed _BitInt past 128 bits at its argument, as clang's SemaBuiltinOverflow does.
        const uint32_t operands[] = { graph->operand0[node], graph->operand1[node], graph->operand2[node] };
        token = operands[operand] < graph->count && graph->tokens != 0 ? graph->tokens[operands[operand]] : token;
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
        result |= operand != BUSTER_CHECK_NONE && stream->diagnostics[operand] != CHECK_DIAG_NONE && check_stream_type(stream, operand) == 0;
    }
    if (check_graph_has_arguments(graph->ops[node]) && graph->argument_counts != 0)
    {
        const uint32_t offset = graph->argument_offsets != 0 ? graph->argument_offsets[node] : 0;
        const uint32_t count = graph->argument_counts[node];
        for (uint32_t i = 0; i < count; i += 1)
        {
            const uint32_t operand = graph->argument_nodes[offset + i];
            result |= stream->diagnostics[operand] != CHECK_DIAG_NONE && check_stream_type(stream, operand) == 0;
        }
    }
    return result;
}

// Whether `value`, a static assertion's in its GNU context, is a real floating constant, which clang folds and converts to
// _Bool there (`_Static_assert(0.5, "")` holds), as a GNU constant. Its discarded operand's read the evaluator can't take
// fails it too (BUSTER_CHECK_DISCARDED_UNEVALUABLE: `(s, 0.5)`), as the integer-constant rejection already has it.
static bool check_graph_floating_assertion(uint32_t context, const CheckValue* value)
{
    return (context & BUSTER_CHECK_CONTEXT_ASSERT) != 0 && (context & BUSTER_CHECK_CONTEXT_GNU_CONSTANT) != 0 &&
           value->constant_kind == CHECK_CONSTANT_FLOATING && value->fault == CHECK_FAULT_NONE &&
           (value->flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_DISCARDED_UNEVALUABLE)) == 0;
}

// Whether `value`, in `context`, is a GNU constant where an ICE is required: an integer constant that isn't an ICE, or a
// static assertion's floating constant.
static bool check_graph_gnu_constant(uint32_t context, const CheckValue* value)
{
    return (context & (BUSTER_CHECK_CONTEXT_ICE | BUSTER_CHECK_CONTEXT_ASSERT)) != 0 && (context & BUSTER_CHECK_CONTEXT_GNU_CONSTANT) != 0 &&
           (value->flags & BUSTER_CHECK_ICE) == 0 &&
           (check_action_integer_constant(value, BUSTER_CHECK_EXTENSION_GNU_CONSTANT) || check_graph_floating_assertion(context, value));
}

// Whether `node` has a warning or a GNU constant where an ICE is required, which check_graph_warning_contexts must place.
static bool check_graph_warning_needed(TypeTable* types, const CheckGraph* graph, const CheckStream* stream, uint32_t node)
{
    const uint32_t context = graph->contexts != NULL ? graph->contexts[node] : 0;
    const CheckValue value = context != 0 ? check_graph_context_value(types, graph, stream, node) : check_stream_value(stream, node);
    return stream->warnings[node] != 0 || check_graph_gnu_constant(context, &value);
}

static void check_graph_warning_contexts(TypeTable* types, const CheckGraph* graph, CheckStream* stream, uint8_t* states, bool scalar)
{
    (void)scalar;
    stream->warning_count = 0;
    stream->warnings_prepared = true;
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
            needed = check_graph_warning_needed(types, graph, stream, node + (uint32_t)__builtin_ctz(lanes));
        }
    }
    for (; !needed && node < graph->count; node += 1)
    {
        needed = check_graph_warning_needed(types, graph, stream, node);
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
        const CheckValue converted = context != 0 ? check_graph_context_value(types, graph, stream, node) : check_stream_value(stream, node);
        const CheckValue* value = &converted;
        if (check_graph_gnu_constant(context, value) && (value->flags & BUSTER_CHECK_GNU_OBJECT_READ) == 0)
        {
            stream->warnings[node] |= CHECK_WARNING_GNU_CONSTANT;
            stream->warning_ignored[node] |= CHECK_WARNING_GNU_CONSTANT;
        }
        stream->warning_count += stream->warnings[node] != 0;
        const CheckOp op = graph->ops[node];
        uint32_t mask = UINT32_MAX;
        bool truth = false;
        const bool selection = op == CHECK_OP_CONDITIONAL || op == CHECK_OP_LOGICAL_AND || op == CHECK_OP_LOGICAL_OR;
        const CheckValue condition = selection ? check_graph_context_value(types, graph, stream, graph->operand0[node]) : (CheckValue){ 0 };
        if (op == CHECK_OP_SIZEOF || op == CHECK_OP_ALIGNOF)
        {
            mask = 0;
        }
        else if (selection && check_action_truth(types, &condition, &truth))
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
static bool check_graph_const_warnings(const TypeTable* types, const CheckGraph* graph, CheckStream* stream, Arena* output)
{
    CheckWarningBuffer warnings = { .arena = output };
    bool valid = graph->const_warning_count == 0 ||
                 (graph->const_warnings != NULL && graph->contexts != NULL && graph->const_warning_count <= graph->count);
    CheckStatus status = valid ? CHECK_STATUS_OK : CHECK_STATUS_INVALID_GRAPH;
    for (uint32_t i = 0; valid && i < graph->const_warning_count; i += 1)
    {
        const CheckConstWarning* request = graph->const_warnings + i;
        valid = request->node < graph->count;
        status = valid ? status : CHECK_STATUS_INVALID_GRAPH;
        if (valid && stream->diagnostics[request->node] == CHECK_DIAG_NONE)
        {
            const CheckValue value = check_graph_context_value(types, graph, stream, request->node);
            if ((value.flags & BUSTER_CHECK_GNU_OBJECT_READ) != 0 && check_graph_gnu_constant(graph->contexts[request->node], &value))
            {
                valid = check_warning_append(&warnings, &request->event, NULL, 0);
                status = valid ? status : CHECK_STATUS_RESOURCE;
            }
        }
    }
    if (valid)
    {
        stream->warning_events = check_warning_view(&warnings);
    }
    else
    {
        stream->status = status;
    }
    return valid;
}

static void check_graph_report_node(TypeTable* types, const CheckGraph* graph, CheckStream* stream, uint32_t node)
{
    const uint8_t context = graph->contexts != 0 ? graph->contexts[node] & (uint8_t)~BUSTER_CHECK_CONTEXT_VALUE : 0;
    const CheckValue converted = context != 0 && stream->diagnostics[node] == CHECK_DIAG_NONE ?
                                 check_graph_context_value(types, graph, stream, node) : check_stream_value(stream, node);
    const CheckValue* value = &converted;
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
        else if ((context & (BUSTER_CHECK_CONTEXT_ICE | BUSTER_CHECK_CONTEXT_ASSERT)) != 0 && !check_action_integer_constant(value, extensions) &&
                 !check_graph_floating_assertion(context, value))
        {
            check_graph_report(graph, stream, node, CHECK_MESSAGE_ICE, CHECK_DIAG_NONE, CHECK_FAULT_NONE);
        }
        else if ((context & BUSTER_CHECK_CONTEXT_ASSERT) != 0 &&
                 (value->constant_kind == CHECK_CONSTANT_FLOATING ? value->floating == 0 : value->integer == 0))
        {
            check_graph_report(graph, stream, node, CHECK_MESSAGE_STATIC_ASSERT, CHECK_DIAG_NONE, CHECK_FAULT_NONE);
        }
        else if ((context & BUSTER_CHECK_CONTEXT_STATIC) != 0 && (value->constant_kind == CHECK_CONSTANT_NONE ||
                 (value->flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_DISCARDED_UNEVALUABLE)) != 0))
        {
            check_graph_report(graph, stream, node, CHECK_MESSAGE_STATIC_INITIALIZER, CHECK_DIAG_NONE, CHECK_FAULT_NONE);
        }
    }
}

static void check_graph_reports(TypeTable* types, const CheckGraph* graph, CheckStream* stream, CheckReport* temporary, bool scalar)
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
            check_graph_report_node(types, graph, stream, node + (uint32_t)__builtin_ctz(lanes));
        }
    }
    for (; node < graph->count; node += 1)
    {
        check_graph_report_node(types, graph, stream, node);
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


#define BUSTER_CHECK_CONTEXT_LANES 64u

// Valid graphs are in postorder: one reverse pass marks each required context and all of its dependencies.
// This proof changes no graph column and is retained with the output rather than in the scheduler's scratch.
// Only a context and the nodes it reaches mark anything: the vector path passes over each 64 nodes that hold neither a
// context nor a mark from a later node, as one test of their two columns, since in postorder only a later node marks one
// (#1050). The scalar path takes every node. `zero` says `marks` is zero already (a fresh output, CheckOptions.fresh_output).
static void check_graph_constant_contexts(const CheckGraph* graph, uint8_t* marks, bool zero, bool scalar)
{
    (void)scalar;

    const uint8_t required = BUSTER_CHECK_CONTEXT_ICE | BUSTER_CHECK_CONTEXT_ASSERT | BUSTER_CHECK_CONTEXT_STATIC | BUSTER_CHECK_CONTEXT_VALUE;
    if (!zero)
    {
        memset(marks, 0, graph->count);
    }
    for (uint32_t node = graph->count; node > 0;)
    {
        bool skipped = false;
#if BUSTER_HOST_VECTOR
        if (!BUSTER_SCALAR_PATH(scalar) && graph->contexts != NULL && node >= BUSTER_CHECK_CONTEXT_LANES)
        {
            const uint32_t first = node - BUSTER_CHECK_CONTEXT_LANES;
            const __m512i contexts = _mm512_and_si512(_mm512_loadu_si512(graph->contexts + first), _mm512_set1_epi8((char)required));
            skipped = _mm512_test_epi8_mask(_mm512_or_si512(contexts, _mm512_loadu_si512(marks + first)), _mm512_set1_epi8(-1)) == 0;
        }
#endif
        if (skipped)
        {
            node -= BUSTER_CHECK_CONTEXT_LANES;
        }
        else
        {
            node -= 1;
            if (graph->contexts != NULL && (graph->contexts[node] & required) != 0)
            {
                marks[node] = 1;
            }
            if (marks[node] != 0)
            {
                const uint32_t operands[] = { graph->operand0[node], graph->operand1[node], graph->operand2[node] };
                for (uint32_t i = 0; i < 3; i += 1)
                {
                    if (operands[i] != BUSTER_CHECK_NONE)
                    {
                        marks[operands[i]] = 1;
                    }
                }
                if (check_graph_has_arguments(graph->ops[node]) && graph->argument_counts != NULL)
                {
                    const uint32_t first = graph->argument_offsets != NULL ? graph->argument_offsets[node] : 0;
                    for (uint32_t i = 0; i < graph->argument_counts[node]; i += 1)
                    {
                        marks[graph->argument_nodes[first + i]] = 1;
                    }
                }
            }
        }
    }
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
        const bool recorded = graph->recorded_ready && graph->recorded_nodes == count &&
                              (count == 0 || graph->recorded_pure != NULL);
        if (recorded && !options->verify)
        {
            validation = graph->recorded;
            if (count != 0)
            {
                memcpy(pure, graph->recorded_pure, count);
            }
        }
        else
        {
            result = BUSTER_SCALAR_PATH(options->scalar) ? check_graph_validate(graph, pure, &validation, &stream->failed_node) :
                                       check_graph_validate_vector(graph, pure, &validation, &stream->failed_node);
            if (result && recorded)
            {
                result = check_graph_recorded_equal(graph, &validation, pure, &stream->failed_node);
            }
        }
        stream->status = result ? CHECK_STATUS_OK : CHECK_STATUS_INVALID_GRAPH;
    }
    const uint32_t maximum_arguments = validation.maximum_arguments;
    const uint32_t maximum_effects = validation.maximum_effects;
    const uint32_t* maximum_builtins = validation.builtins;
    CheckValue* arguments = result && maximum_arguments != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckValue, maximum_arguments) : 0;
    bool constant_contexts = false;
    bool zero_output = false;
    for (uint32_t node = 0; result && graph->contexts != NULL && !constant_contexts && node < count; node += 1)
    {
        constant_contexts = (graph->contexts[node] & (BUSTER_CHECK_CONTEXT_ICE | BUSTER_CHECK_CONTEXT_ASSERT | BUSTER_CHECK_CONTEXT_STATIC | BUSTER_CHECK_CONTEXT_VALUE)) != 0;
    }
    if (result && count != 0)
    {
        // Room for a payload row per node, of which the run writes the first few: only those take memory. In a fresh output,
        // every column pushed here is zero until written (CheckOptions.fresh_output), and none is cleared.
        zero_output = options->fresh_output && arena_untouched(output);
        stream->value_types = BUSTER_ARENA_PUSH_ARRAY(output, TypeId, count);
        stream->value_flags = BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, count);
        stream->value_objects = BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, count);
        stream->value_categories = BUSTER_ARENA_PUSH_ARRAY(output, uint8_t, count);
        stream->value_constants = BUSTER_ARENA_PUSH_ARRAY(output, uint8_t, count);
        stream->payload_rows = BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, count);
        stream->payloads = BUSTER_ARENA_PUSH_ARRAY(output, CheckValue, count);
        if (stream->payload_rows != NULL && !zero_output)
        {
            memset(stream->payload_rows, 0, (uint64_t)count * sizeof(uint32_t));
        }
        stream->conversions = BUSTER_ARENA_PUSH_ARRAY(output, CheckNodeConversions, count);
        stream->diagnostics = BUSTER_ARENA_PUSH_ARRAY(output, CheckDiagnostic, count);
        stream->diagnostic_operands = BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, count);
        stream->member_offsets = BUSTER_ARENA_PUSH_ARRAY(output, uint64_t, count);
        stream->warnings = BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, count);
        stream->warning_ignored = BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, count);
        stream->reports = BUSTER_ARENA_PUSH_ARRAY(output, CheckReport, count);
        stream->constant_evaluated = constant_contexts ? BUSTER_ARENA_PUSH_ARRAY(output, uint8_t, count) : NULL;
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
        result = stream->value_types != 0 && stream->value_flags != 0 && stream->value_objects != 0 && stream->value_categories != 0 &&
                 stream->value_constants != 0 && stream->payload_rows != 0 && stream->payloads != 0 && stream->conversions != 0 && stream->diagnostics != 0 && stream->diagnostic_operands != 0 && stream->member_offsets != 0 &&
                 stream->warnings != 0 &&
                 stream->warning_ignored != 0 && stream->reports != 0 &&
                 (!constant_contexts || stream->constant_evaluated != NULL) &&
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
            if (!zero_output)
            {
                memset(stream->warning_ignored, 0, (size_t)count * sizeof(*stream->warning_ignored));
                memset(stream->diagnostic_operands, 0, (size_t)count * sizeof(*stream->diagnostic_operands));
            }
            if (constant_contexts)
            {
                check_graph_constant_contexts(graph, stream->constant_evaluated, zero_output, BUSTER_SCALAR_PATH(options->scalar));
            }
        }
        if (stream->argument_conversions != 0 && !zero_output)
        {
            memset(stream->argument_conversions, 0, (size_t)graph->argument_count * sizeof(*stream->argument_conversions));
        }
        // The vector path's memoized transitions (#1050, c_check_transitions.c) where the options ask for them, unless
        // overload sets make operands contextual, a column its keys read is absent (as check_graph_validate_vector's blocks
        // need theirs), or the scratch arena has no room for the table. Otherwise, in node order: the vector path's leaves
        // without their actions and its other nodes one at a time, or with its regions (CheckOptions.regions, level_order)
        // each run of pure nodes as a region; the scalar path's nodes one at a time.
        const bool columns = graph->types != NULL && graph->members != NULL && graph->argument_offsets != NULL && graph->argument_counts != NULL;
        const bool transitions = !BUSTER_SCALAR_PATH(options->scalar) && !options->level_order && options->transitions &&
                                 graph->overload_candidate_count == 0 && columns &&
                                 check_graph_transitions(types, graph, stream, pure, levels, order, arguments, scratch, batch_size, options->verify_transitions);
        // A verified run's hits each ran their actions too: a mismatch fails the run, as an invalid graph.
        if (transitions && stream->statistics.transition_mismatches != 0)
        {
            result = false;
            stream->status = CHECK_STATUS_INVALID_GRAPH;
        }
        // The main loop writes each node once, first: in a fresh output it writes no zero (CheckStream.zero_columns), which the
        // passes after it, which may write a node again, do.
        stream->zero_columns = zero_output;
        uint32_t node = transitions ? count : 0;
        const bool regions = !BUSTER_SCALAR_PATH(options->scalar) && (options->regions || options->level_order);
        while (node < count)
        {
            if (!BUSTER_SCALAR_PATH(options->scalar) && (!regions || pure[node] == 0) && check_graph_is_leaf(graph, node))
            {
                check_graph_leaf(types, graph, stream, node);
                node += 1;
            }
            else if (!regions || pure[node] == 0)
            {
                check_graph_scalar_node(types, graph, stream, node, arguments, scratch, BUSTER_SCALAR_PATH(options->scalar));
                node += 1;
            }
            else
            {
                const uint32_t begin = node;
                node = check_graph_marked_end(pure, node, count, BUSTER_SCALAR_PATH(options->scalar));
                if (options->level_order)
                {
                    check_graph_region(types, graph, stream, pure, begin, node, batch_size, levels, counts, order);
                }
                else
                {
                    check_graph_region_postorder(types, graph, stream, pure, begin, node, batch_size);
                }
            }
        }
        stream->zero_columns = false;
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
                result = check_graph_query_runtime(types, graph, stream, scratch, pure, levels, arguments, maximum_arguments, BUSTER_SCALAR_PATH(options->scalar),
                                                   options->replay_queries);
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
            result = check_graph_const_warnings(types, graph, stream, output);
            if (result)
            {
                check_graph_reports(types, graph, stream, reports, BUSTER_SCALAR_PATH(options->scalar));
            }
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
#endif
