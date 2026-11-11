// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Bounded translation-unit inlining for #139. Include after ir_optimize.c. The caller publishes the shared IR bodies
// and explicit linkage/variadic/noinline facts in one symbol-sorted table. Original code; the graph reference is
// Tarjan, Depth-First Search and Linear Graph Algorithms (1972). The DFS uses explicit bounded stacks, not recursion.
//
// Tarjan components are processed in callee-before-caller order. Recursive members are never candidates. A candidate
// is a leaf whose blocks are one straight line (one block, or #1282's chain, each block's br to the next dropped); its
// operations, faulting accesses and volatile flags are copied in their original order. Parameters become the already
// evaluated actual arguments; the final return becomes COPY or disappears for void. A previously rewritten acyclic
// function may become a leaf, allowing nested expansion.
// Caller blocks and predecessor identities do not change. Every variable side-table slice is rebuilt per occurrence,
// because valid source slices can overlap with different roles. This first scope does not splice callee CFGs or phis.
//
// #1282 takes SIMD functions, the x86 intrinsic wrappers': a callee's vectors, masks and machine rows are copied with it
// (its vector constants and rows appended to the caller's, IR_VCONST's index and IrCall.row rebased, a row call staying a
// call), and so are its asm bodies (#1839: appended, IrCall.body rebased, a body call staying a call whose block argument
// is the callee's slot's rebased address), and its stack slots (IR_STACK_ADDR's and IR_LIFETIME's rebased). A call that passes or returns aggregates
// by value (#131's ABI records) is inlined where its records are the callee's own: each aggregate argument's bytes are
// copied into a fresh slot of the caller, whose address the callee's parameter becomes (the callee owns its copy, as the
// convention's callee does), and an aggregate result's bytes from the address the callee returns into the call's
// destination. A copy is one vector load and store where the aggregate is in a vector register whole and the options
// allow its width (IrInlineOptions.vector_copies), else 8-, 4-, 2- and 1-byte integers. SROA and promotion then make the
// copies values (ir_optimize_sroa.c's `flagged`).
//
// Costs are structural units: cloned instructions plus copied constant/address rows, not measured cycles or bytes.
// Per-call cost columns receive one masked 16-lane threshold sweep, with an exact scalar twin. Caller/unit growth
// admission and graph walks are deterministic scalar protocols. No profitable threshold is claimed without target
// measurement. Disabled mode still validates and clones the unit, retaining every original call.
//
// Inputs are immutable, unpadded and borrowed. They may precede the output arena's unused tail; scratch is disjoint.
// The result owns fresh function and audit columns in output. Failure preserves it and both arena lengths; successful
// calls restore scratch. Rebuild CFG/use analyses after this pass. Full corpus/native performance acceptance is pending.

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "host_isa.h"

#define BUSTER_IR_INLINE_INTERNAL 1u
#define BUSTER_IR_INLINE_NOINLINE 2u
#define BUSTER_IR_INLINE_VARIADIC 4u
#define BUSTER_IR_INLINE_FLAGS 7u
#define BUSTER_IR_INLINE_MAX_FUNCTIONS (1u << 16)
#define BUSTER_IR_INLINE_MAX_ITEMS (1u << 20)
#define BUSTER_IR_INLINE_MAX_TOTAL (1u << 22)

typedef struct IrInlineUnit IrInlineUnit;
struct IrInlineUnit
{
    const IrFunction* functions;
    const uint32_t* symbols;
    const uint8_t* flags;
    uint32_t count;
    uint32_t symbol_count;
};

typedef struct IrInlineOptions IrInlineOptions;
struct IrInlineOptions
{
    uint32_t maximum_cost;
    uint32_t maximum_function_growth;
    uint32_t maximum_unit_instructions;
    // The vectors' widths an aggregate's copy may take where a vector register holds it whole (#1282): bit 0 16 bytes,
    // bit 1 32, bit 2 64, which the target's vectors must have (AVX-512F, and VL for 16 and 32). 0 copies by integers.
    uint8_t vector_copies;
    bool enabled;
    bool scalar;
};

typedef enum IrInlineDecision
{
    IR_INLINE_SELECTED,
    IR_INLINE_DISABLED,
    IR_INLINE_INDIRECT,
    IR_INLINE_UNKNOWN_SYMBOL,
    IR_INLINE_EXTERNAL,
    IR_INLINE_NOINLINE,
    IR_INLINE_VARIADIC,
    IR_INLINE_RECURSIVE,
    IR_INLINE_BODY,
    IR_INLINE_COST,
    IR_INLINE_GROWTH,
    // A call that passes or returns an aggregate by value: its ABI records (#131) stay a call's.
    IR_INLINE_ABI,
    IR_INLINE_DECISION_COUNT,
} IrInlineDecision;

typedef struct IrInlineResult IrInlineResult;
struct IrInlineResult
{
    IrFunction* functions;
    uint32_t function_count;
    // CSR of original call sites by input function. Bodies remain in the input function-table order.
    uint32_t* call_offsets;
    uint32_t* call_nodes;
    uint32_t* callees;
    uint32_t* costs;
    uint32_t* expanded_sizes;
    uint8_t* decisions;
    uint32_t call_count;
    uint32_t* components;
    uint8_t* recursive;
    uint32_t component_count;
    uint32_t inlined_calls;
    uint64_t input_instructions;
    uint64_t output_instructions;
    uint64_t retained_bytes;
    uint64_t scratch_bytes;
};

typedef struct IrInlineBody IrInlineBody;
struct IrInlineBody
{
    uint32_t size;
    uint32_t cost;
    bool supported;
};

// What one expansion adds to its caller past the callee's cloned body (#1282): the aggregate copies' instructions,
// addresses and slots. 64-bit, so that a call's aggregates of up to 4 GiB each sum without wrapping and the bounds refuse
// it.
typedef struct IrInlineCopies IrInlineCopies;
struct IrInlineCopies
{
    uint64_t instructions;
    uint64_t addresses;
    uint64_t slots;
};

static __attribute__((unused)) IrInlineOptions ir_inline_options(void)
{
    const IrInlineOptions options = {
        .maximum_cost = 32,
        .maximum_function_growth = 256,
        .maximum_unit_instructions = BUSTER_IR_INLINE_MAX_TOTAL,
        .enabled = true,
    };
    return options;
}

static bool ir_inline_overlap(const void* first, uint64_t first_bytes, const void* second, uint64_t second_bytes)
{
    const uintptr_t a = (uintptr_t)first;
    const uintptr_t b = (uintptr_t)second;
    return first_bytes != 0 && second_bytes != 0 && (a <= b ? b - a < first_bytes : a - b < second_bytes);
}

static bool ir_inline_tail_conflict(const void* pointer, uint64_t bytes, const Arena* output, const Arena* scratch)
{
    return ir_inline_overlap(pointer, bytes, output->base + output->length, output->capacity - output->length) || ir_inline_overlap(pointer, bytes, scratch->base, scratch->capacity);
}

static bool ir_inline_function_conflict(const IrFunction* function, const Arena* output, const Arena* scratch)
{
    const void* pointers[] = {
        function,
        function->op,
        function->type,
        function->flags,
        function->alignment_log2,
        function->a,
        function->b,
        function->c,
        function->block_offsets,
        function->extra,
        function->constants,
        function->vector_constants,
        function->rows,
        function->asm_bodies,
        function->slots,
        function->addresses,
        function->calls,
        function->call_types,
        function->parameter_types,
        function->labels,
        function->abi_calls,
        function->passed,
        function->debug,
        function->debug != NULL ? function->debug->origins : NULL,
        function->debug != NULL ? function->debug->locals : NULL,
        function->debug != NULL ? function->debug->scalars : NULL,
        function->debug != NULL ? function->debug->bindings : NULL,
        function->debug != NULL ? function->debug->regions : NULL,
        function->debug != NULL ? function->debug->binding_flags : NULL,
    };
    const uint64_t counts[] = {
        sizeof(*function),
        function->instruction_count,
        function->instruction_count,
        function->instruction_count,
        function->instruction_count,
        (uint64_t)function->instruction_count * 4,
        (uint64_t)function->instruction_count * 4,
        (uint64_t)function->instruction_count * 4,
        ((uint64_t)function->block_count + 1) * 4,
        (uint64_t)function->extra_count * 4,
        (uint64_t)function->constant_count * sizeof(IrConstant),
        (uint64_t)function->vector_constant_count * sizeof(IrSimdValue),
        (uint64_t)function->row_count * sizeof(Tier0Operation),
        (uint64_t)function->asm_body_count * sizeof(IrAsmBody),
        (uint64_t)function->slot_count * sizeof(IrSlot),
        (uint64_t)function->address_count * sizeof(IrAddress),
        (uint64_t)function->call_count * sizeof(IrCall),
        function->call_type_count,
        function->parameter_count,
        (uint64_t)function->label_count * 4,
        (uint64_t)function->abi_call_count * sizeof(IrAbiCall),
        (uint64_t)function->passed_count * sizeof(IrPassed),
        function->debug != NULL ? sizeof(IrDebugInfo) : 0,
        function->debug != NULL ? (uint64_t)function->instruction_count * sizeof(uint32_t) : 0,
        function->debug != NULL ? (uint64_t)function->slot_count * sizeof(uint32_t) : 0,
        function->debug != NULL ? (uint64_t)function->debug->scalar_count * sizeof(IrDebugScalar) : 0,
        function->debug != NULL ? (uint64_t)function->debug->binding_count * sizeof(IrDebugBinding) : 0,
        function->debug != NULL ? (uint64_t)function->debug->region_count * sizeof(IrDebugRegion) : 0,
        function->debug != NULL && function->debug->binding_flags != NULL ? function->debug->local_count : 0,
    };
    bool conflict = false;
    for (uint32_t i = 0; !conflict && i < sizeof(pointers) / sizeof(pointers[0]); i += 1)
    {
        conflict = ir_inline_tail_conflict(pointers[i], counts[i], output, scratch);
    }
    return conflict;
}

static bool ir_inline_sizes_valid(const IrFunction* function)
{
    return function->instruction_count <= BUSTER_IR_INLINE_MAX_ITEMS && function->block_count <= BUSTER_IR_INLINE_MAX_ITEMS && function->extra_count <= BUSTER_IR_INLINE_MAX_ITEMS && function->constant_count <= BUSTER_IR_INLINE_MAX_ITEMS && function->vector_constant_count <= BUSTER_IR_INLINE_MAX_ITEMS && function->slot_count <= BUSTER_IR_INLINE_MAX_ITEMS && function->address_count <= BUSTER_IR_INLINE_MAX_ITEMS && function->call_count <= BUSTER_IR_INLINE_MAX_ITEMS && function->call_type_count <= BUSTER_IR_INLINE_MAX_ITEMS && function->parameter_count <= BUSTER_IR_INLINE_MAX_ITEMS && function->row_count <= BUSTER_IR_INLINE_MAX_ITEMS &&
           function->asm_body_count <= BUSTER_IR_INLINE_MAX_ITEMS && function->passed_count <= BUSTER_IR_INLINE_MAX_ITEMS && function->abi_call_count <= BUSTER_IR_INLINE_MAX_ITEMS;
}

static uint32_t ir_inline_symbol(const IrInlineUnit* unit, uint32_t symbol)
{
    uint32_t first = 0;
    uint32_t end = unit->count;
    while (first < end)
    {
        const uint32_t middle = first + (end - first) / 2;
        if (unit->symbols[middle] < symbol)
        {
            first = middle + 1;
        }
        else
        {
            end = middle;
        }
    }
    return first < unit->count && unit->symbols[first] == symbol ? first : BUSTER_IR_NONE;
}

// Whether `node` is a call site: a call of a callee, direct or indirect, but not a row call (#141) or a body call (#1839),
// which run their machine records in place of a callee and stay as they are.
static bool ir_inline_site(const IrFunction* function, uint32_t node)
{
    const uint32_t op = function->op[node];
    return op == IR_CALL_INDIRECT || (op == IR_CALL && function->calls[function->b[node]].row == 0 && function->calls[function->b[node]].body == 0);
}

// A call with ABI records (#131) has the convention's shape, which only its records give (a hidden pointer its callee
// takes as parameter 0, a void call of a callee whose result is the address of its bytes): its result and parameters'
// IR types aren't its callee's, though whether it is variadic is, and it stays a call (IR_INLINE_ABI).
static bool ir_inline_signature(const IrFunction* caller, uint32_t node, const IrFunction* callee, uint8_t flags)
{
    const IrCall call = caller->calls[caller->b[node]];
    bool match = call.variadic == ((flags & BUSTER_IR_INLINE_VARIADIC) != 0) && (call.abi != 0 || (call.result_type == callee->result_type && call.parameter_count == callee->parameter_count));
    for (uint32_t p = 0; match && call.abi == 0 && p < call.parameter_count; p += 1)
    {
        match = caller->call_types[call.type_first + p] == callee->parameter_types[p];
    }
    return match;
}

static IrCheck ir_inline_inputs(const IrInlineUnit* unit, Arena* output, Arena* scratch, IrInlineOptions options, IrInlineResult* result, uint32_t* failed_function)
{
    IrCheck check = ir_check(IR_OK, BUSTER_IR_NONE, 0);
    if (unit->count > BUSTER_IR_INLINE_MAX_FUNCTIONS || options.maximum_unit_instructions > BUSTER_IR_INLINE_MAX_TOTAL || options.maximum_function_growth > BUSTER_IR_INLINE_MAX_ITEMS)
    {
        check.error = IR_ERROR_LIMIT;
    }
    else if ((unit->count != 0 && (!unit->functions || !unit->symbols || !unit->flags)) || (unit->count != 0 && unit->symbol_count < 2))
    {
        check.error = IR_ERROR_STRUCTURE;
    }
    else if (ir_inline_tail_conflict(unit, sizeof(*unit), output, scratch) || ir_inline_tail_conflict(unit->functions, (uint64_t)unit->count * sizeof(IrFunction), output, scratch) || ir_inline_tail_conflict(unit->symbols, (uint64_t)unit->count * 4, output, scratch) || ir_inline_tail_conflict(unit->flags, unit->count, output, scratch))
    {
        check.error = IR_ERROR_STRUCTURE;
    }
    bool ok = true;
    // Check every borrowed span against the entry tail before making any output allocation. Moving the tail first
    // could conceal an alias with an audit column allocated immediately below the new tail.
    for (uint32_t f = 0; check.error == IR_OK && f < unit->count; f += 1)
    {
        *failed_function = f;
        if (!ir_inline_sizes_valid(&unit->functions[f]))
        {
            check.error = IR_ERROR_LIMIT;
        }
        else if (ir_inline_function_conflict(&unit->functions[f], output, scratch))
        {
            check.error = IR_ERROR_STRUCTURE;
        }
    }
    if (check.error == IR_OK)
    {
        result->call_offsets = ir_allocate(output, (uint64_t)unit->count + 1, 4, &ok);
        if (!ok)
        {
            check.error = IR_ERROR_MEMORY;
        }
    }
    for (uint32_t f = 0; check.error == IR_OK && f < unit->count; f += 1)
    {
        *failed_function = f;
        const IrFunction* function = &unit->functions[f];
        if (!ir_inline_sizes_valid(function))
        {
            check.error = IR_ERROR_LIMIT;
        }
        else if (unit->symbols[f] == 0 || unit->symbols[f] >= unit->symbol_count || (f != 0 && unit->symbols[f - 1] >= unit->symbols[f]) || (unit->flags[f] & ~BUSTER_IR_INLINE_FLAGS) != 0 || function->symbol_count != unit->symbol_count)
        {
            check.error = IR_ERROR_METADATA;
        }
        else
        {
            const uint64_t mark = scratch->length;
            IrOptimizeWork work = { 0 };
            check = ir_optimize_prepare(&work, function, output, scratch, BUSTER_SCALAR_PATH(options.scalar), true);
            const uint64_t bytes = scratch->length - mark;
            result->scratch_bytes = bytes > result->scratch_bytes ? bytes : result->scratch_bytes;
            arena_pop_to(scratch, mark);
        }
        if (check.error == IR_OK)
        {
            result->input_instructions += function->instruction_count;
            if (result->input_instructions > options.maximum_unit_instructions)
            {
                check.error = IR_ERROR_LIMIT;
            }
            result->call_offsets[f] = result->call_count;
            for (uint32_t node = 0; node < function->instruction_count; node += 1)
            {
                result->call_count += ir_inline_site(function, node);
            }
        }
    }
    if (check.error == IR_OK)
    {
        result->call_offsets[unit->count] = result->call_count;
        result->functions = ir_allocate(output, unit->count, sizeof(IrFunction), &ok);
        result->call_nodes = ir_allocate(output, result->call_count, 4, &ok);
        result->callees = ir_allocate(output, result->call_count, 4, &ok);
        result->costs = ir_allocate(output, result->call_count, 4, &ok);
        result->expanded_sizes = ir_allocate(output, result->call_count, 4, &ok);
        result->decisions = ir_allocate(output, result->call_count, 1, &ok);
        result->components = ir_allocate(output, unit->count, 4, &ok);
        result->recursive = ir_allocate(output, unit->count, 1, &ok);
        result->function_count = unit->count;
        if (!ok)
        {
            check.error = IR_ERROR_MEMORY;
        }
    }
    // Every body is verified before a call signature indexes the callee's parameter types.
    for (uint32_t f = 0; check.error == IR_OK && f < unit->count; f += 1)
    {
        *failed_function = f;
        const IrFunction* function = &unit->functions[f];
        uint32_t call = result->call_offsets[f];
        for (uint32_t node = 0; check.error == IR_OK && node < function->instruction_count; node += 1)
        {
            if (ir_inline_site(function, node))
            {
                const uint32_t callee = function->op[node] == IR_CALL ? ir_inline_symbol(unit, function->a[node]) : BUSTER_IR_NONE;
                result->call_nodes[call] = node;
                result->callees[call++] = callee;
                if (callee != BUSTER_IR_NONE && !ir_inline_signature(function, node, &unit->functions[callee], unit->flags[callee]))
                {
                    check = ir_check(IR_ERROR_TYPE, node, callee);
                }
            }
        }
    }
    return check;
}

// Iterative Tarjan. Component numbers are sink-first, so a nonrecursive callee has already been rewritten when a
// caller's component is processed. Duplicate edges are harmless; unknown/indirect calls have no graph target.
static IrError ir_inline_components(const IrInlineUnit* unit, Arena* scratch, IrInlineResult* result, uint32_t** order)
{
    const uint32_t count = unit->count;
    bool ok = true;
    uint32_t* indices = ir_allocate(scratch, count, 4, &ok);
    uint32_t* low = ir_allocate(scratch, count, 4, &ok);
    uint32_t* next = ir_allocate(scratch, count, 4, &ok);
    uint32_t* dfs = ir_allocate(scratch, count, 4, &ok);
    uint32_t* active = ir_allocate(scratch, count, 4, &ok);
    uint8_t* stacked = ir_allocate(scratch, count, 1, &ok);
    uint32_t* sizes = ir_allocate(scratch, count, 4, &ok);
    uint32_t* offsets = ir_allocate(scratch, (uint64_t)count + 1, 4, &ok);
    uint32_t* members = ir_allocate(scratch, count, 4, &ok);
    IrError error = ok ? IR_OK : IR_ERROR_MEMORY;
    if (ok)
    {
        for (uint32_t f = 0; f < count; f += 1)
        {
            indices[f] = BUSTER_IR_NONE;
        }
        uint32_t clock = 0;
        uint32_t active_count = 0;
        for (uint32_t root = 0; root < count; root += 1)
        {
            if (indices[root] == BUSTER_IR_NONE)
            {
                uint32_t depth = 1;
                dfs[0] = root;
                indices[root] = clock;
                low[root] = clock++;
                next[root] = result->call_offsets[root];
                active[active_count++] = root;
                stacked[root] = 1;
                while (depth != 0)
                {
                    const uint32_t function = dfs[depth - 1];
                    if (next[function] < result->call_offsets[function + 1])
                    {
                        const uint32_t callee = result->callees[next[function]++];
                        if (callee != BUSTER_IR_NONE)
                        {
                            result->recursive[function] |= callee == function;
                            if (indices[callee] == BUSTER_IR_NONE)
                            {
                                dfs[depth++] = callee;
                                indices[callee] = clock;
                                low[callee] = clock++;
                                next[callee] = result->call_offsets[callee];
                                active[active_count++] = callee;
                                stacked[callee] = 1;
                            }
                            else if (stacked[callee] && indices[callee] < low[function])
                            {
                                low[function] = indices[callee];
                            }
                        }
                    }
                    else
                    {
                        if (low[function] == indices[function])
                        {
                            uint32_t member;
                            do
                            {
                                member = active[--active_count];
                                stacked[member] = 0;
                                result->components[member] = result->component_count;
                                sizes[result->component_count] += 1;
                            } while (member != function);
                            result->component_count += 1;
                        }
                        depth -= 1;
                        if (depth != 0 && low[function] < low[dfs[depth - 1]])
                        {
                            low[dfs[depth - 1]] = low[function];
                        }
                    }
                }
            }
        }
        for (uint32_t f = 0; f < count; f += 1)
        {
            const uint32_t component = result->components[f];
            result->recursive[f] |= sizes[component] > 1;
            offsets[component + 1] += 1;
        }
        for (uint32_t component = 0; component < result->component_count; component += 1)
        {
            offsets[component + 1] += offsets[component];
            next[component] = offsets[component];
        }
        for (uint32_t f = 0; f < count; f += 1)
        {
            members[next[result->components[f]]++] = f;
        }
        *order = members;
    }
    return error;
}

// Whether `op` may be cloned into a caller in its order (#1282 adds the SIMD operations, slots' addresses and lifetimes,
// fences, intrinsics and atomic read/modify/writes): not a terminator before the return, a phi, a call (but a row call,
// which ir_inline_body checks) or the dynamic stack's operations.
static bool ir_inline_cloned(uint32_t op)
{
    return op != IR_CALL_INDIRECT && op != IR_PHI && op != IR_BR && op != IR_CBR && op != IR_SWITCH && op != IR_RET && op != IR_UNREACHABLE && op != IR_ALLOCA &&
           op != IR_STACK_SAVE && op != IR_STACK_RESTORE && op != IR_BR_INDIRECT && op < IR_OP_COUNT;
}

// Whether the function's blocks are one straight line (#1282): each but the last ends in a br to the next, the last in its
// return, and no other instruction ends a block. The lowering's preheader stays a block of its own past the optimizer's
// CFG cleanup, so a wrapper is two such blocks; its br's are dropped as its body is cloned.
static bool ir_inline_chain(const IrFunction* function)
{
    bool chain = function->block_count != 0 && function->instruction_count != 0 && function->op[function->instruction_count - 1] == IR_RET;
    for (uint32_t block = 0; chain && block + 1 < function->block_count; block += 1)
    {
        const uint32_t end = function->block_offsets[block + 1];
        chain = end > function->block_offsets[block] && function->op[end - 1] == IR_BR && function->a[end - 1] == block + 1;
    }
    return chain;
}

static IrInlineBody ir_inline_body(const IrFunction* function)
{
    // Labels would be defined twice. A variadic definition's incoming frame is its own, and an aggregate result through a
    // hidden pointer (#131's MEMORY) has the convention's parameters rather than the callee's.
    bool slots = true;
    for (uint32_t slot = 0; slot < function->slot_count; slot += 1)
    {
        slots = slots && (function->slots[slot].flags & (BUSTER_IR_SLOT_REGISTER_SAVE_AREA | BUSTER_IR_SLOT_OVERFLOW_AREA)) == 0;
    }
    IrInlineBody body = {
        .cost = UINT32_MAX,
        .supported = ir_inline_chain(function) && function->label_count == 0 && !function->variadic && slots &&
                     (function->parameter_passed == 0 || function->result_abi.kind != BUSTER_ABI_RESULT_MEMORY),
    };
    if (body.supported)
    {
        // A value's result is a COPY; an aggregate's bytes are copied instead (ir_inline_copies).
        body.size = function->result_type != IR_VOID && function->result_size == 0;
        for (uint32_t node = 0; node + 1 < function->instruction_count; node += 1)
        {
            // A br is the chain's (ir_inline_chain), between its blocks, and disappears.
            const uint32_t op = function->op[node];
            body.supported &= op == IR_BR || (ir_inline_cloned(op) && (op != IR_CALL || function->calls[function->b[node]].row != 0 || function->calls[function->b[node]].body != 0));
            body.size += op != IR_PARAM && op != IR_BR;
        }
        const uint64_t cost = (uint64_t)body.size + function->constant_count + function->address_count + function->vector_constant_count + function->row_count + function->asm_body_count + function->slot_count;
        body.cost = cost < UINT32_MAX ? (uint32_t)cost : UINT32_MAX - 1;
    }
    return body;
}

// The vector type that copies an aggregate of `size` bytes whole (#1282): one a vector register holds whole (its first
// register a vector one, no second, neither eightbyte alone), of a width `allowed` has (IrInlineOptions.vector_copies);
// else IR_VOID, for a copy by integers.
static IrType ir_inline_whole(uint8_t first, uint8_t second, bool alone, uint64_t size, uint8_t allowed)
{
    const bool whole = first != BUSTER_ABI_REGISTER_NONE && (first & 0xe0) == BUSTER_ABI_REGISTER_VECTOR && second == BUSTER_ABI_REGISTER_NONE && !alone;
    const uint32_t width = size == 16 ? 0 : size == 32 ? 1 : size == 64 ? 2 : 3;
    const IrType types[4] = { IR_V4I32, IR_V8I32, IR_V16I32, IR_VOID };
    return whole && width < 3 && ((allowed >> width) & 1) != 0 ? types[width] : IR_VOID;
}

static IrType ir_inline_argument_whole(AbiLocation location, uint64_t size, uint8_t allowed)
{
    return location.kind == BUSTER_ABI_LOCATION_REGISTERS ? ir_inline_whole(location.registers[0], location.registers[1], (location.flags & (BUSTER_ABI_FLAG_HIGH | BUSTER_ABI_FLAG_LOW)) != 0, size, allowed) :
                                                            IR_VOID;
}

static IrType ir_inline_result_whole(AbiReturn result, uint64_t size, uint8_t allowed)
{
    return result.kind == BUSTER_ABI_RESULT_REGISTERS ? ir_inline_whole(result.registers[0], result.registers[1], (result.flags & BUSTER_ABI_FLAG_HIGH) != 0, size, allowed) : IR_VOID;
}

// A copy's instructions and addresses (ir_inline_emit_copy's): one vector load and store, or an integer's of 8, 4, 2 or
// 1 bytes at a time, each past the first through an ADDRESS of each side. Counted from the size alone, not by its pieces:
// a call that passes a huge aggregate is refused by the bounds in constant time (#1324's review). Its pieces are size / 8
// of 8 bytes and one per set bit of the remainder's 4, 2 and 1.
static IrInlineCopies ir_inline_copy_size(uint64_t size, IrType whole)
{
    const uint64_t pieces = size / 8 + (uint64_t)__builtin_popcountll(size % 8);
    const IrInlineCopies copies = {
        .instructions = whole != IR_VOID ? 2 : pieces != 0 ? pieces * 4 - 2 : 0,
        .addresses = whole == IR_VOID && pieces != 0 ? (pieces - 1) * 2 : 0,
    };
    return copies;
}

// Whether the call at `node` of `caller` passes and returns what `callee` takes and returns by the same ABI records
// (#131, #1282): a call with records of its own and a callee with its own, neither returning through a hidden pointer, the
// same aggregate result's bytes, and argument by argument the same aggregate's bytes (a ptr to them on both sides) or the
// same type, none an argument the convention ignores. Every other call with records stays a call (IR_INLINE_ABI).
static bool ir_inline_abi_match(const IrFunction* caller, uint32_t node, const IrFunction* callee)
{
    const IrCall call = caller->calls[caller->b[node]];
    const uint32_t count = call.argument_count;
    bool match = call.abi != 0 && call.abi - 1 < caller->abi_call_count && callee->parameter_passed != 0 && !call.variadic && count == callee->parameter_count &&
                 call.parameter_count == count && (uint64_t)callee->parameter_passed - 1 + count <= callee->passed_count;
    const IrAbiCall* records = match ? &caller->abi_calls[call.abi - 1] : 0;
    match = match && (uint64_t)records->passed + count <= caller->passed_count && records->call.result.kind != BUSTER_ABI_RESULT_MEMORY &&
            callee->result_abi.kind != BUSTER_ABI_RESULT_MEMORY && records->result_size == callee->result_size &&
            (callee->result_size != 0 ? call.result_type == IR_VOID && callee->result_type == IR_PTR : call.result_type == callee->result_type);
    for (uint32_t p = 0; match && p < count; p += 1)
    {
        const IrPassed given = caller->passed[records->passed + p];
        const IrPassed taken = callee->passed[callee->parameter_passed - 1 + p];
        const uint8_t type = caller->call_types[call.type_first + p];
        match = given.size == taken.size && given.location.kind != BUSTER_ABI_LOCATION_IGNORE && taken.location.kind != BUSTER_ABI_LOCATION_IGNORE &&
                (given.size != 0 ? type == IR_PTR && callee->parameter_types[p] == IR_PTR : type == callee->parameter_types[p]);
    }
    return match;
}

// What inlining the call at `node` of `caller` adds past `callee`'s cloned body (#1282): each aggregate argument's slot,
// its address and its copy, and the aggregate result's copy into the call's destination where it has one.
static IrInlineCopies ir_inline_copies(const IrFunction* caller, uint32_t node, const IrFunction* callee, uint8_t allowed)
{
    IrInlineCopies copies = { 0 };
    const IrCall call = caller->calls[caller->b[node]];
    if (call.abi != 0)
    {
        const IrAbiCall* records = &caller->abi_calls[call.abi - 1];
        for (uint32_t p = 0; p < call.argument_count; p += 1)
        {
            const IrPassed given = caller->passed[records->passed + p];
            if (given.size != 0)
            {
                const IrInlineCopies copy = ir_inline_copy_size(given.size, ir_inline_argument_whole(given.location, given.size, allowed));
                copies.instructions += copy.instructions + 1;
                copies.addresses += copy.addresses;
                copies.slots += 1;
            }
        }
        if (callee->result_size != 0 && caller->c[node] != BUSTER_IR_NONE)
        {
            const IrInlineCopies copy = ir_inline_copy_size(callee->result_size, ir_inline_result_whole(callee->result_abi, callee->result_size, allowed));
            copies.instructions += copy.instructions;
            copies.addresses += copy.addresses;
        }
    }
    return copies;
}

static void ir_inline_threshold(const uint32_t* costs, uint32_t count, uint32_t maximum, bool scalar, uint8_t* selected)
{
    (void)scalar;

    if (BUSTER_SCALAR_PATH(scalar))
    {
        for (uint32_t i = 0; i < count; i += 1)
        {
            selected[i] = costs[i] <= maximum;
        }
    }
    else
    {
        for (uint32_t first = 0; first < count; first += 16)
        {
            const uint32_t remaining = count - first;
            const uint32_t n = remaining < 16 ? remaining : 16;
            const __mmask16 mask = n == 16 ? UINT16_MAX : (uint16_t)((1u << n) - 1);
            const __m512i values = _mm512_maskz_loadu_epi32(mask, costs + first);
            const __mmask16 fits = mask & _mm512_cmple_epu32_mask(values, _mm512_set1_epi32((int32_t)maximum));
            _mm_mask_storeu_epi8(selected + first, mask, _mm_maskz_set1_epi8(fits, 1));
        }
    }
}

static IrError ir_inline_choose(const IrInlineUnit* unit, uint32_t function, IrInlineOptions options, Arena* scratch, IrInlineResult* result, uint64_t* charged)
{
    const uint32_t first = result->call_offsets[function];
    const uint32_t end = result->call_offsets[function + 1];
    bool ok = true;
    uint8_t* fits = ir_allocate(scratch, end - first, 1, &ok);
    IrError error = ok ? IR_OK : IR_ERROR_MEMORY;
    for (uint32_t call = first; ok && call < end; call += 1)
    {
        const uint32_t callee = result->callees[call];
        const uint32_t node = result->call_nodes[call];
        uint8_t decision = IR_INLINE_SELECTED;
        result->costs[call] = UINT32_MAX;
        if (!options.enabled)
        {
            decision = IR_INLINE_DISABLED;
        }
        else if (unit->functions[function].op[node] == IR_CALL_INDIRECT)
        {
            decision = IR_INLINE_INDIRECT;
        }
        else if (callee == BUSTER_IR_NONE)
        {
            decision = IR_INLINE_UNKNOWN_SYMBOL;
        }
        else if ((unit->flags[callee] & BUSTER_IR_INLINE_INTERNAL) == 0)
        {
            decision = IR_INLINE_EXTERNAL;
        }
        else if ((unit->flags[callee] & BUSTER_IR_INLINE_NOINLINE) != 0)
        {
            decision = IR_INLINE_NOINLINE;
        }
        else if ((unit->flags[callee] & BUSTER_IR_INLINE_VARIADIC) != 0)
        {
            decision = IR_INLINE_VARIADIC;
        }
        else if (result->recursive[callee])
        {
            decision = IR_INLINE_RECURSIVE;
        }
        else if ((unit->functions[function].calls[unit->functions[function].b[node]].abi != 0 || result->functions[callee].parameter_passed != 0) &&
                 !ir_inline_abi_match(&unit->functions[function], node, &result->functions[callee]))
        {
            decision = IR_INLINE_ABI;
        }
        else
        {
            const IrFunction* body_function = &result->functions[callee];
            const IrInlineBody body = ir_inline_body(body_function);
            const IrInlineCopies copies = body.supported ? ir_inline_copies(&unit->functions[function], node, body_function, options.vector_copies) : (IrInlineCopies){ 0 };
            const uint64_t size = (uint64_t)body.size + copies.instructions;
            const uint64_t cost = (uint64_t)body.cost + copies.instructions + copies.addresses + copies.slots;
            decision = body.supported && size < BUSTER_IR_INLINE_MAX_ITEMS ? IR_INLINE_SELECTED : IR_INLINE_BODY;
            result->costs[call] = decision == IR_INLINE_SELECTED && cost < UINT32_MAX ? (uint32_t)cost : UINT32_MAX;
            result->expanded_sizes[call] = decision == IR_INLINE_SELECTED ? (uint32_t)size : 0;
        }
        result->decisions[call] = decision;
    }
    if (ok)
    {
        if (first != end)
        {
            ir_inline_threshold(result->costs + first, end - first, options.maximum_cost, BUSTER_SCALAR_PATH(options.scalar), fits);
        }
        // Appended constant and address rows are bounded like instructions: a call that would carry the caller's
        // side tables past the representation limit stays a call, rather than failing the whole unit.
        const IrFunction* caller = &unit->functions[function];
        uint64_t growth = 0;
        uint64_t constants = caller->constant_count;
        uint64_t addresses = caller->address_count;
        uint64_t slots = caller->slot_count;
        uint64_t vectors = caller->vector_constant_count;
        uint64_t rows = caller->row_count;
        uint64_t bodies = caller->asm_body_count;
        uint64_t descriptors = caller->call_count;
        uint64_t extras = caller->extra_count;
        uint64_t types = caller->call_type_count;
        for (uint32_t call = first; call < end; call += 1)
        {
            if (result->decisions[call] == IR_INLINE_SELECTED)
            {
                const uint32_t size = result->expanded_sizes[call];
                const uint32_t extra = size > 1 ? size - 1 : 0;
                const IrFunction* callee = &result->functions[result->callees[call]];
                const IrInlineCopies copies = ir_inline_copies(caller, result->call_nodes[call], callee, options.vector_copies);
                if (!fits[call - first])
                {
                    result->decisions[call] = IR_INLINE_COST;
                }
                else if (growth + extra > options.maximum_function_growth || (uint64_t)caller->instruction_count + growth + extra > BUSTER_IR_INLINE_MAX_ITEMS ||
                         *charged + extra > options.maximum_unit_instructions || constants + callee->constant_count > BUSTER_IR_INLINE_MAX_ITEMS ||
                         addresses + callee->address_count + copies.addresses > BUSTER_IR_INLINE_MAX_ITEMS || slots + callee->slot_count + copies.slots > BUSTER_IR_INLINE_MAX_ITEMS ||
                         vectors + callee->vector_constant_count > BUSTER_IR_INLINE_MAX_ITEMS || rows + callee->row_count > BUSTER_IR_INLINE_MAX_ITEMS ||
                         bodies + callee->asm_body_count > BUSTER_IR_INLINE_MAX_ITEMS || descriptors + callee->call_count > BUSTER_IR_INLINE_MAX_ITEMS || extras + callee->extra_count > BUSTER_IR_INLINE_MAX_ITEMS ||
                         types + callee->call_type_count > BUSTER_IR_INLINE_MAX_ITEMS)
                {
                    result->decisions[call] = IR_INLINE_GROWTH;
                }
                else
                {
                    growth += extra;
                    *charged += extra;
                    constants += callee->constant_count;
                    addresses += callee->address_count + copies.addresses;
                    slots += callee->slot_count + copies.slots;
                    vectors += callee->vector_constant_count;
                    rows += callee->row_count;
                    bodies += callee->asm_body_count;
                    descriptors += callee->call_count;
                    extras += callee->extra_count;
                    types += callee->call_type_count;
                    result->inlined_calls += 1;
                }
            }
        }
    }
    return error;
}

// Where an expansion's side-table rows start in its caller: the indices its cloned nodes are rebased by (the caller's own
// nodes' are all 0), and the next descriptor, argument, fixed type and address of the rewrite.
typedef struct IrInlineBases IrInlineBases;
struct IrInlineBases
{
    uint32_t constants;
    uint32_t addresses;
    uint32_t slots;
    uint32_t vectors;
    uint32_t rows;
    uint32_t bodies;
};

typedef struct IrInlineCursor IrInlineCursor;
struct IrInlineCursor
{
    uint32_t node;
    uint32_t extra;
    uint32_t descriptors;
    uint32_t types;
    uint32_t addresses;
    uint32_t slots;
};

static void ir_inline_node(const IrFunction* source, uint32_t old, IrFunction* destination, uint32_t node, const uint32_t* values, IrInlineBases bases)
{
    const uint32_t op = source->op[old];
    const uint32_t mask = ir_value_mask(source, old);
    destination->op[node] = (uint8_t)op;
    destination->type[node] = source->type[old];
    destination->flags[node] = source->flags[old];
    destination->alignment_log2[node] = source->alignment_log2[old];
    destination->a[node] = (mask & 1) != 0 ? values[source->a[old]] : source->a[old];
    destination->b[node] = (mask & 2) != 0 ? values[source->b[old]] : source->b[old];
    destination->c[node] = (mask & 4) != 0 ? values[source->c[old]] : source->c[old];
    if (op == IR_CONST)
    {
        destination->a[node] += bases.constants;
    }
    else if (op == IR_VCONST)
    {
        destination->a[node] += bases.vectors;
    }
    else if (op == IR_STACK_ADDR || op == IR_LIFETIME)
    {
        destination->a[node] += bases.slots;
    }
    else if (op == IR_ADDRESS)
    {
        destination->c[node] += bases.addresses;
        if (source->b[old] != BUSTER_IR_NONE)
        {
            destination->b[node] = values[source->b[old]];
        }
    }
    else if (op == IR_RET && source->a[old] != BUSTER_IR_NONE)
    {
        destination->a[node] = values[source->a[old]];
    }
    else if ((op == IR_CALL || op == IR_CALL_INDIRECT || op == IR_ATOMIC_RMW) && source->c[old] != BUSTER_IR_NONE)
    {
        // An aggregate result's destination (IrAbiCall.result_size), or a compare-exchange's desired value (#1059).
        destination->c[node] = values[source->c[old]];
    }
}

// A call's fresh descriptor, argument slice and fixed-type slice at the cursor, its machine row's index rebased by `rows`
// (a row call of an expansion, #1282), its ABI records' index the caller's (only the caller's own calls have them).
static void ir_inline_descriptor(const IrFunction* source, uint32_t old, IrFunction* destination, uint32_t node, const uint32_t* values, IrInlineBases bases, IrInlineCursor* cursor)
{
    IrCall descriptor = source->calls[source->b[old]];
    for (uint32_t p = 0; p < descriptor.argument_count; p += 1)
    {
        destination->extra[cursor->extra + p] = values[source->extra[descriptor.argument_first + p]];
    }
    if (descriptor.parameter_count != 0)
    {
        memcpy(destination->call_types + cursor->types, source->call_types + descriptor.type_first, descriptor.parameter_count);
    }
    descriptor.argument_first = cursor->extra;
    descriptor.type_first = cursor->types;
    descriptor.row += descriptor.row != 0 ? bases.rows : 0;
    descriptor.body += descriptor.body != 0 ? bases.bodies : 0;
    cursor->extra += descriptor.argument_count;
    cursor->types += descriptor.parameter_count;
    destination->calls[cursor->descriptors] = descriptor;
    destination->b[node] = cursor->descriptors++;
}

// A ternary intrinsic's third operand (#1840) in a fresh extra slot at the cursor, with its `c` naming that slot.
static void ir_inline_third(const IrFunction* source, uint32_t old, IrFunction* destination, uint32_t node, const uint32_t* values, IrInlineCursor* cursor)
{
    destination->c[node] = ir_intrinsic_ternary_c(source->c[old], cursor->extra);
    destination->extra[cursor->extra++] = values[ir_intrinsic_third(source, old)];
}

static uint32_t ir_inline_emit(IrFunction* f, IrInlineCursor* cursor, uint32_t op, uint32_t type, uint32_t a, uint32_t b, uint32_t c)
{
    const uint32_t node = cursor->node++;
    f->op[node] = (uint8_t)op;
    f->type[node] = (uint8_t)type;
    f->flags[node] = 0;
    f->alignment_log2[node] = 0;
    f->a[node] = a;
    f->b[node] = b;
    f->c[node] = c;
    return node;
}

// `base` past `offset` bytes: base itself at 0, else an ADDRESS of a new address row.
static uint32_t ir_inline_offset(IrFunction* f, IrInlineCursor* cursor, uint32_t base, uint64_t offset)
{
    uint32_t pointer = base;
    if (offset != 0)
    {
        f->addresses[cursor->addresses] = (IrAddress){ .offset = (int64_t)offset, .scale = 1 };
        pointer = ir_inline_emit(f, cursor, IR_ADDRESS, IR_PTR, base, BUSTER_IR_NONE, cursor->addresses++);
    }
    return pointer;
}

// Copies `size` bytes from the ptr `from` to the ptr `to` (ir_inline_copy_size's instructions): one vector `whole` load and
// store, or integers of 8, 4, 2 and 1 bytes, all unaligned (neither side's alignment is known past its bytes').
static void ir_inline_emit_copy(IrFunction* f, IrInlineCursor* cursor, uint32_t from, uint32_t to, uint64_t size, IrType whole)
{
    if (whole != IR_VOID)
    {
        const uint32_t loaded = ir_inline_emit(f, cursor, IR_LOAD, whole, from, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_inline_emit(f, cursor, IR_STORE, IR_VOID, to, loaded, BUSTER_IR_NONE);
    }
    for (uint64_t offset = 0; whole == IR_VOID && offset < size;)
    {
        const uint64_t remaining = size - offset;
        const uint32_t piece = remaining >= 8 ? 8 : remaining >= 4 ? 4 : remaining >= 2 ? 2 : 1;
        const IrType type = piece == 8 ? IR_I64 : piece == 4 ? IR_I32 : piece == 2 ? IR_I16 : IR_I8;
        const uint32_t loaded = ir_inline_emit(f, cursor, IR_LOAD, type, ir_inline_offset(f, cursor, from, offset), BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_inline_emit(f, cursor, IR_STORE, IR_VOID, ir_inline_offset(f, cursor, to, offset), loaded, BUSTER_IR_NONE);
        offset += piece;
    }
}

// The expansion of the call at `node` of `source` (the caller) into `rewritten` at the cursor: its aggregate arguments'
// copies into fresh slots (#1282), the callee's nodes but its parameters, which become the arguments (an aggregate's its
// copy's address), and its result: a COPY of its value, or its aggregate bytes copied into the call's destination.
static void ir_inline_expand(const IrFunction* source, uint32_t node, const IrFunction* callee, const uint32_t* values, uint32_t* inner_values, uint32_t* parameters,
                             IrFunction* rewritten, IrInlineBases bases, uint8_t allowed, IrInlineCursor* cursor)
{
    const IrCall descriptor = source->calls[source->b[node]];
    const IrAbiCall* records = descriptor.abi != 0 ? &source->abi_calls[descriptor.abi - 1] : 0;
    if (callee->slot_count != 0)
    {
        memcpy(rewritten->slots + bases.slots, callee->slots, (uint64_t)callee->slot_count * sizeof(IrSlot));
    }
    // Every argument's value, an aggregate's copied whether or not a parameter node reads it (ir_inline_copies counts
    // them all; a copy no one reads is dead once promoted).
    for (uint32_t p = 0; p < descriptor.argument_count; p += 1)
    {
        const uint32_t argument = values[source->extra[descriptor.argument_first + p]];
        const IrPassed given = records != 0 ? source->passed[records->passed + p] : (IrPassed){ 0 };
        parameters[p] = argument;
        if (given.size != 0)
        {
            const IrType whole = ir_inline_argument_whole(given.location, given.size, allowed);
            const uint32_t slot = cursor->slots++;
            rewritten->slots[slot] = (IrSlot){
                .size = given.size,
                .alignment = whole != IR_VOID ? (uint32_t)given.size : 8,
                .type = IR_VOID,
                .flags = BUSTER_IR_SLOT_ADDRESS_TAKEN,
            };
            const uint32_t address = ir_inline_emit(rewritten, cursor, IR_STACK_ADDR, IR_PTR, slot, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_inline_emit_copy(rewritten, cursor, argument, address, given.size, whole);
            parameters[p] = address;
        }
    }
    for (uint32_t inner = 0; inner + 1 < callee->instruction_count; inner += 1)
    {
        if (callee->op[inner] == IR_PARAM)
        {
            inner_values[inner] = parameters[callee->a[inner]];
        }
        else if (callee->op[inner] != IR_BR)
        {
            inner_values[inner] = cursor->node;
            ir_inline_node(callee, inner, rewritten, cursor->node, inner_values, bases);
            if (callee->op[inner] == IR_CALL)
            {
                ir_inline_descriptor(callee, inner, rewritten, cursor->node, inner_values, bases, cursor);
            }
            else if (ir_ternary_instruction(callee, inner))
            {
                ir_inline_third(callee, inner, rewritten, cursor->node, inner_values, cursor);
            }
            cursor->node += 1;
        }
    }
    const uint32_t returned = callee->a[callee->instruction_count - 1];
    if (callee->result_size != 0 && source->c[node] != BUSTER_IR_NONE)
    {
        const IrType whole = ir_inline_result_whole(callee->result_abi, callee->result_size, allowed);
        ir_inline_emit_copy(rewritten, cursor, inner_values[returned], values[source->c[node]], callee->result_size, whole);
    }
    else if (callee->result_type != IR_VOID && callee->result_size == 0)
    {
        ir_inline_emit(rewritten, cursor, IR_COPY, callee->result_type, inner_values[returned], BUSTER_IR_NONE, BUSTER_IR_NONE);
    }
}

static IrError ir_inline_rewrite(const IrInlineUnit* unit, uint32_t function, Arena* output, Arena* scratch, IrInlineOptions options, IrInlineResult* result)
{
    const IrFunction* source = &unit->functions[function];
    bool ok = true;
    uint32_t* values = ir_allocate(scratch, source->instruction_count, 4, &ok);
    uint32_t maximum_callee = 0;
    uint32_t maximum_parameters = 0;
    IrSizes sizes = {
        .block_count = source->block_count,
        .constant_count = source->constant_count,
        .slot_count = source->slot_count,
        .address_count = source->address_count,
        .parameter_count = source->parameter_count,
        .label_count = source->label_count,
        .abi_call_count = source->abi_call_count,
        .passed_count = source->passed_count,
        .vector_constant_count = source->vector_constant_count,
        .row_count = source->row_count,
        .asm_body_count = source->asm_body_count,
    };
    uint32_t call = result->call_offsets[function];
    for (uint32_t node = 0; ok && node < source->instruction_count; node += 1)
    {
        const uint32_t op = source->op[node];
        const bool site = ir_inline_site(source, node);
        const bool selected = site && result->decisions[call] == IR_INLINE_SELECTED;
        if (selected)
        {
            const IrFunction* callee = &result->functions[result->callees[call]];
            const IrInlineCopies copies = ir_inline_copies(source, node, callee, options.vector_copies);
            sizes.instruction_count += result->expanded_sizes[call];
            values[node] = source->type[node] == IR_VOID ? BUSTER_IR_NONE : (uint32_t)sizes.instruction_count - 1;
            sizes.constant_count += callee->constant_count;
            sizes.address_count += callee->address_count + copies.addresses;
            sizes.slot_count += callee->slot_count + copies.slots;
            sizes.vector_constant_count += callee->vector_constant_count;
            sizes.row_count += callee->row_count;
            sizes.asm_body_count += callee->asm_body_count;
            // Its row calls (#1282), the only calls of an expanded body.
            sizes.call_count += callee->call_count;
            sizes.extra_count += callee->extra_count;
            sizes.call_type_count += callee->call_type_count;
            maximum_callee = callee->instruction_count > maximum_callee ? callee->instruction_count : maximum_callee;
            maximum_parameters = callee->parameter_count > maximum_parameters ? callee->parameter_count : maximum_parameters;
        }
        else
        {
            values[node] = (uint32_t)sizes.instruction_count++;
            if (op == IR_CALL || op == IR_CALL_INDIRECT)
            {
                const IrCall descriptor = source->calls[source->b[node]];
                sizes.call_count += 1;
                sizes.extra_count += descriptor.argument_count;
                sizes.call_type_count += descriptor.parameter_count;
            }
            else if (op == IR_PHI)
            {
                sizes.extra_count += (uint64_t)source->b[node] * 2;
            }
            else if (op == IR_SWITCH)
            {
                sizes.extra_count += (uint64_t)source->c[node] * 2 + 1;
            }
            else if (op == IR_BR_INDIRECT && source->b[node] != BUSTER_IR_NONE)
            {
                sizes.extra_count += source->c[node];
            }
            else if (ir_ternary_instruction(source, node))
            {
                sizes.extra_count += 1;
            }
        }
        call += site;
    }
    IrError error = ok ? IR_OK : IR_ERROR_MEMORY;
    if (sizes.instruction_count > BUSTER_IR_INLINE_MAX_ITEMS || sizes.constant_count > BUSTER_IR_INLINE_MAX_ITEMS || sizes.address_count > BUSTER_IR_INLINE_MAX_ITEMS || sizes.extra_count > BUSTER_IR_INLINE_MAX_ITEMS ||
        sizes.call_type_count > BUSTER_IR_INLINE_MAX_ITEMS || sizes.slot_count > BUSTER_IR_INLINE_MAX_ITEMS || sizes.vector_constant_count > BUSTER_IR_INLINE_MAX_ITEMS ||
        sizes.row_count > BUSTER_IR_INLINE_MAX_ITEMS || sizes.asm_body_count > BUSTER_IR_INLINE_MAX_ITEMS || sizes.call_count > BUSTER_IR_INLINE_MAX_ITEMS)
    {
        error = IR_ERROR_LIMIT;
    }
    uint32_t* inner_values = error == IR_OK ? ir_allocate(scratch, maximum_callee, 4, &ok) : 0;
    uint32_t* parameters = error == IR_OK ? ir_allocate(scratch, maximum_parameters, 4, &ok) : 0;
    if (!ok)
    {
        error = IR_ERROR_MEMORY;
    }
    IrFunction rewritten = { 0 };
    if (error == IR_OK)
    {
        error = ir_create(output, &sizes, &rewritten);
    }
    if (error == IR_OK && !ir_debug_create(output, &rewritten, source->debug))
    {
        error = IR_ERROR_MEMORY;
    }
    if (error == IR_OK)
    {
        // Only the caller's identities survive this interface. Cloned callee slots and instructions remain unknown
        // until inline-instance identities are represented; they cannot inherit the caller's token or local number.
        ir_debug_remap(source, &rewritten, values, NULL, NULL);
        rewritten.result_type = source->result_type;
        rewritten.symbol_count = source->symbol_count;
        if (source->constant_count != 0)
        {
            memcpy(rewritten.constants, source->constants, (uint64_t)source->constant_count * sizeof(IrConstant));
        }
        if (source->address_count != 0)
        {
            memcpy(rewritten.addresses, source->addresses, (uint64_t)source->address_count * sizeof(IrAddress));
        }
        if (source->slot_count != 0)
        {
            memcpy(rewritten.slots, source->slots, (uint64_t)source->slot_count * sizeof(IrSlot));
        }
        if (source->vector_constant_count != 0)
        {
            memcpy(rewritten.vector_constants, source->vector_constants, (uint64_t)source->vector_constant_count * sizeof(IrSimdValue));
        }
        if (source->row_count != 0)
        {
            memcpy(rewritten.rows, source->rows, (uint64_t)source->row_count * sizeof(Tier0Operation));
        }
        if (source->asm_body_count != 0)
        {
            memcpy(rewritten.asm_bodies, source->asm_bodies, (uint64_t)source->asm_body_count * sizeof(IrAsmBody));
        }
        if (source->parameter_count != 0)
        {
            memcpy(rewritten.parameter_types, source->parameter_types, source->parameter_count);
        }
        // The caller's blocks don't change, so its labels keep theirs; its ABI records and incoming frame stay as they
        // were, each call it keeps with its index into them.
        if (source->label_count != 0)
        {
            memcpy(rewritten.labels, source->labels, (uint64_t)source->label_count * 4);
        }
        if (source->abi_call_count != 0)
        {
            memcpy(rewritten.abi_calls, source->abi_calls, (uint64_t)source->abi_call_count * sizeof(IrAbiCall));
        }
        if (source->passed_count != 0)
        {
            memcpy(rewritten.passed, source->passed, (uint64_t)source->passed_count * sizeof(IrPassed));
        }
        rewritten.parameter_passed = source->parameter_passed;
        rewritten.result_size = source->result_size;
        rewritten.result_abi = source->result_abi;
        rewritten.variadic = source->variadic;
        rewritten.incoming = source->incoming;
        IrInlineBases bases = {
            .constants = source->constant_count,
            .vectors = source->vector_constant_count,
            .rows = source->row_count,
            .bodies = source->asm_body_count,
        };
        IrInlineCursor cursor = {
            .addresses = source->address_count,
            .slots = source->slot_count,
        };
        call = result->call_offsets[function];
        for (uint32_t block = 0; block < source->block_count; block += 1)
        {
            rewritten.block_offsets[block] = cursor.node;
            for (uint32_t node = source->block_offsets[block]; node < source->block_offsets[block + 1]; node += 1)
            {
                const uint32_t op = source->op[node];
                const bool site = ir_inline_site(source, node);
                if (site && result->decisions[call] == IR_INLINE_SELECTED)
                {
                    const IrFunction* callee = &result->functions[result->callees[call]];
                    if (callee->constant_count != 0)
                    {
                        memcpy(rewritten.constants + bases.constants, callee->constants, (uint64_t)callee->constant_count * sizeof(IrConstant));
                    }
                    if (callee->address_count != 0)
                    {
                        memcpy(rewritten.addresses + cursor.addresses, callee->addresses, (uint64_t)callee->address_count * sizeof(IrAddress));
                    }
                    if (callee->vector_constant_count != 0)
                    {
                        memcpy(rewritten.vector_constants + bases.vectors, callee->vector_constants, (uint64_t)callee->vector_constant_count * sizeof(IrSimdValue));
                    }
                    if (callee->row_count != 0)
                    {
                        memcpy(rewritten.rows + bases.rows, callee->rows, (uint64_t)callee->row_count * sizeof(Tier0Operation));
                    }
                    if (callee->asm_body_count != 0)
                    {
                        memcpy(rewritten.asm_bodies + bases.bodies, callee->asm_bodies, (uint64_t)callee->asm_body_count * sizeof(IrAsmBody));
                    }
                    // The callee's own rows first, then the copies' fresh ones after its slots and addresses.
                    bases.addresses = cursor.addresses;
                    bases.slots = cursor.slots;
                    cursor.addresses += callee->address_count;
                    cursor.slots += callee->slot_count;
                    ir_inline_expand(source, node, callee, values, inner_values, parameters, &rewritten, bases, options.vector_copies, &cursor);
                    bases.constants += callee->constant_count;
                    bases.vectors += callee->vector_constant_count;
                    bases.rows += callee->row_count;
                    bases.bodies += callee->asm_body_count;
                }
                else
                {
                    const IrInlineBases own = { 0 };
                    ir_inline_node(source, node, &rewritten, cursor.node, values, own);
                    if (op == IR_CALL || op == IR_CALL_INDIRECT)
                    {
                        ir_inline_descriptor(source, node, &rewritten, cursor.node, values, own, &cursor);
                    }
                    else if (op == IR_PHI)
                    {
                        rewritten.a[cursor.node] = cursor.extra;
                        for (uint32_t p = 0; p < source->b[node]; p += 1)
                        {
                            rewritten.extra[cursor.extra++] = source->extra[source->a[node] + p * 2];
                            rewritten.extra[cursor.extra++] = values[source->extra[source->a[node] + p * 2 + 1]];
                        }
                    }
                    else if (op == IR_SWITCH)
                    {
                        rewritten.b[cursor.node] = cursor.extra;
                        const uint32_t count = source->c[node] * 2 + 1;
                        memcpy(rewritten.extra + cursor.extra, source->extra + source->b[node], (uint64_t)count * 4);
                        cursor.extra += count;
                    }
                    else if (op == IR_BR_INDIRECT && source->b[node] != BUSTER_IR_NONE)
                    {
                        // Its labels' list (#785), the caller's label indices, as its blocks are.
                        rewritten.b[cursor.node] = cursor.extra;
                        memcpy(rewritten.extra + cursor.extra, source->extra + source->b[node], (uint64_t)source->c[node] * 4);
                        cursor.extra += source->c[node];
                    }
                    else if (ir_ternary_instruction(source, node))
                    {
                        ir_inline_third(source, node, &rewritten, cursor.node, values, &cursor);
                    }
                    cursor.node += 1;
                }
                call += site;
            }
        }
        rewritten.block_offsets[rewritten.block_count] = cursor.node;
        result->functions[function] = rewritten;
        result->output_instructions += rewritten.instruction_count;
    }
    return error;
}

static __attribute__((unused)) IrCheck ir_optimize_inline(const IrInlineUnit* unit, Arena* output, Arena* scratch, IrInlineOptions options, IrInlineResult* result, uint32_t* error_function)
{
    const bool arenas = output && scratch && output->base && scratch->base && output->length <= output->capacity && scratch->length <= scratch->capacity;
    const bool error_storage = error_function == 0 || (arenas && !ir_inline_tail_conflict(error_function, sizeof(*error_function), output, scratch) && !ir_inline_overlap(error_function, sizeof(*error_function), result, sizeof(*result)));
    const bool arguments = unit && result && arenas && error_storage && !ir_inline_overlap(output->base, output->capacity, scratch->base, scratch->capacity) && !ir_inline_tail_conflict(result, sizeof(*result), output, scratch);
    IrCheck check = ir_check(arguments ? IR_OK : IR_ERROR_STRUCTURE, BUSTER_IR_NONE, 0);
    const uint64_t output_mark = arguments ? output->length : 0;
    const uint64_t scratch_mark = arguments ? scratch->length : 0;
    uint32_t failed_function = BUSTER_IR_NONE;
    IrInlineResult inlined = { 0 };
    uint32_t* order = 0;
    if (check.error == IR_OK)
    {
        check = ir_inline_inputs(unit, output, scratch, options, &inlined, &failed_function);
    }
    if (check.error == IR_OK)
    {
        check.error = ir_inline_components(unit, scratch, &inlined, &order);
    }
    uint64_t charged = inlined.input_instructions;
    for (uint32_t position = 0; check.error == IR_OK && position < inlined.function_count; position += 1)
    {
        failed_function = order[position];
        const uint64_t function_mark = scratch->length;
        check.error = ir_inline_choose(unit, failed_function, options, scratch, &inlined, &charged);
        if (check.error == IR_OK)
        {
            check.error = ir_inline_rewrite(unit, failed_function, output, scratch, options, &inlined);
        }
        if (check.error == IR_OK)
        {
            const uint64_t verification_mark = scratch->length;
            IrOptimizeWork verification = { 0 };
            // The caller CFG is unchanged, including valid unreachable regions; check the same reachable SSA
            // contract as the input, rather than deleting regions or inventing dominance for dead blocks.
            check = ir_optimize_prepare(&verification, &inlined.functions[failed_function], output, scratch, BUSTER_SCALAR_PATH(options.scalar), true);
            const uint64_t bytes = scratch->length - scratch_mark;
            inlined.scratch_bytes = bytes > inlined.scratch_bytes ? bytes : inlined.scratch_bytes;
            arena_pop_to(scratch, verification_mark);
        }
        const uint64_t peak = scratch->length - scratch_mark;
        inlined.scratch_bytes = peak > inlined.scratch_bytes ? peak : inlined.scratch_bytes;
        arena_pop_to(scratch, function_mark);
    }
    if (arguments)
    {
        arena_pop_to(scratch, scratch_mark);
        if (check.error == IR_OK)
        {
            failed_function = BUSTER_IR_NONE;
            inlined.retained_bytes = output->length - output_mark;
            *result = inlined;
        }
        else
        {
            arena_pop_to(output, output_mark);
        }
    }
    if (error_function != 0 && error_storage)
    {
        *error_function = failed_function;
    }
    return check;
}
