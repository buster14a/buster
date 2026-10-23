// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Bounded translation-unit inlining for #139. Include after ir_optimize.c. The caller publishes the shared IR bodies
// and explicit linkage/variadic/noinline facts in one symbol-sorted table. Original code; the graph reference is
// Tarjan, Depth-First Search and Linear Graph Algorithms (1972). The DFS uses explicit bounded stacks, not recursion.
//
// Tarjan components are processed in callee-before-caller order. Recursive members are never candidates. A candidate
// is a single-block leaf without stack objects; its operations, faulting accesses and volatile flags are copied in
// their original order. Parameters become the already evaluated actual arguments; the final return becomes COPY or
// disappears for void. A previously rewritten acyclic function may become a leaf, allowing nested expansion.
// Caller blocks and predecessor identities do not change. Every variable side-table slice is rebuilt per occurrence,
// because valid source slices can overlap with different roles. This first scope does not splice callee CFGs or phis.
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
#include <immintrin.h>

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
        function->slots,
        function->addresses,
        function->calls,
        function->call_types,
        function->parameter_types,
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
        (uint64_t)function->slot_count * sizeof(IrSlot),
        (uint64_t)function->address_count * sizeof(IrAddress),
        (uint64_t)function->call_count * sizeof(IrCall),
        function->call_type_count,
        function->parameter_count,
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
    return function->instruction_count <= BUSTER_IR_INLINE_MAX_ITEMS && function->block_count <= BUSTER_IR_INLINE_MAX_ITEMS && function->extra_count <= BUSTER_IR_INLINE_MAX_ITEMS && function->constant_count <= BUSTER_IR_INLINE_MAX_ITEMS && function->slot_count <= BUSTER_IR_INLINE_MAX_ITEMS && function->address_count <= BUSTER_IR_INLINE_MAX_ITEMS && function->call_count <= BUSTER_IR_INLINE_MAX_ITEMS && function->call_type_count <= BUSTER_IR_INLINE_MAX_ITEMS && function->parameter_count <= BUSTER_IR_INLINE_MAX_ITEMS;
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

static bool ir_inline_signature(const IrFunction* caller, uint32_t node, const IrFunction* callee, uint8_t flags)
{
    const IrCall call = caller->calls[caller->b[node]];
    bool match = call.result_type == callee->result_type && call.parameter_count == callee->parameter_count && call.variadic == ((flags & BUSTER_IR_INLINE_VARIADIC) != 0);
    for (uint32_t p = 0; match && p < call.parameter_count; p += 1)
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
            check = ir_optimize_prepare(&work, function, output, scratch, options.scalar);
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
                result->call_count += function->op[node] == IR_CALL || function->op[node] == IR_CALL_INDIRECT;
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
            if (function->op[node] == IR_CALL || function->op[node] == IR_CALL_INDIRECT)
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

static IrInlineBody ir_inline_body(const IrFunction* function)
{
    IrInlineBody body = {
        .cost = UINT32_MAX,
        .supported = function->block_count == 1 && function->slot_count == 0 && function->op[function->instruction_count - 1] == IR_RET,
    };
    if (body.supported)
    {
        body.size = function->result_type != IR_VOID;
        for (uint32_t node = 0; node + 1 < function->instruction_count; node += 1)
        {
            const uint32_t op = function->op[node];
            body.supported &= op != IR_CALL && op != IR_CALL_INDIRECT && op != IR_PHI && op != IR_STACK_ADDR && op < IR_BR;
            body.size += op != IR_PARAM;
        }
        body.cost = body.size + function->constant_count + function->address_count;
    }
    return body;
}

static void ir_inline_threshold(const uint32_t* costs, uint32_t count, uint32_t maximum, bool scalar, uint8_t* selected)
{
    if (scalar)
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
        else
        {
            const IrInlineBody body = ir_inline_body(&result->functions[callee]);
            decision = body.supported ? IR_INLINE_SELECTED : IR_INLINE_BODY;
            result->costs[call] = body.cost;
            result->expanded_sizes[call] = body.size;
        }
        result->decisions[call] = decision;
    }
    if (ok)
    {
        if (first != end)
        {
            ir_inline_threshold(result->costs + first, end - first, options.maximum_cost, options.scalar, fits);
        }
        // Appended constant and address rows are bounded like instructions: a call that would carry the caller's
        // side tables past the representation limit stays a call, rather than failing the whole unit.
        uint64_t growth = 0;
        uint64_t constants = unit->functions[function].constant_count;
        uint64_t addresses = unit->functions[function].address_count;
        for (uint32_t call = first; call < end; call += 1)
        {
            if (result->decisions[call] == IR_INLINE_SELECTED)
            {
                const uint32_t size = result->expanded_sizes[call];
                const uint32_t extra = size > 1 ? size - 1 : 0;
                const IrFunction* callee = &result->functions[result->callees[call]];
                if (!fits[call - first])
                {
                    result->decisions[call] = IR_INLINE_COST;
                }
                else if (growth + extra > options.maximum_function_growth || (uint64_t)unit->functions[function].instruction_count + growth + extra > BUSTER_IR_INLINE_MAX_ITEMS || *charged + extra > options.maximum_unit_instructions || constants + callee->constant_count > BUSTER_IR_INLINE_MAX_ITEMS || addresses + callee->address_count > BUSTER_IR_INLINE_MAX_ITEMS)
                {
                    result->decisions[call] = IR_INLINE_GROWTH;
                }
                else
                {
                    growth += extra;
                    *charged += extra;
                    constants += callee->constant_count;
                    addresses += callee->address_count;
                    result->inlined_calls += 1;
                }
            }
        }
    }
    return error;
}

static void ir_inline_node(const IrFunction* source, uint32_t old, IrFunction* destination, uint32_t node, const uint32_t* values, uint32_t constants, uint32_t addresses)
{
    const uint32_t op = source->op[old];
    destination->op[node] = (uint8_t)op;
    destination->type[node] = source->type[old];
    destination->flags[node] = source->flags[old];
    destination->alignment_log2[node] = source->alignment_log2[old];
    destination->a[node] = (ir_operand_table[op] & 1) != 0 ? values[source->a[old]] : source->a[old];
    destination->b[node] = (ir_operand_table[op] & 2) != 0 ? values[source->b[old]] : source->b[old];
    destination->c[node] = (ir_operand_table[op] & 4) != 0 ? values[source->c[old]] : source->c[old];
    if (op == IR_CONST)
    {
        destination->a[node] += constants;
    }
    else if (op == IR_ADDRESS)
    {
        destination->c[node] += addresses;
        if (source->b[old] != BUSTER_IR_NONE)
        {
            destination->b[node] = values[source->b[old]];
        }
    }
    else if (op == IR_RET && source->a[old] != BUSTER_IR_NONE)
    {
        destination->a[node] = values[source->a[old]];
    }
}

static IrError ir_inline_rewrite(const IrInlineUnit* unit, uint32_t function, Arena* output, Arena* scratch, IrInlineResult* result)
{
    const IrFunction* source = &unit->functions[function];
    bool ok = true;
    uint32_t* values = ir_allocate(scratch, source->instruction_count, 4, &ok);
    uint32_t maximum_callee = 0;
    IrSizes sizes = {
        .block_count = source->block_count,
        .constant_count = source->constant_count,
        .slot_count = source->slot_count,
        .address_count = source->address_count,
        .parameter_count = source->parameter_count,
    };
    uint32_t call = result->call_offsets[function];
    for (uint32_t node = 0; ok && node < source->instruction_count; node += 1)
    {
        const uint32_t op = source->op[node];
        const bool calls = op == IR_CALL || op == IR_CALL_INDIRECT;
        const bool selected = calls && result->decisions[call] == IR_INLINE_SELECTED;
        if (selected)
        {
            const IrFunction* callee = &result->functions[result->callees[call]];
            sizes.instruction_count += result->expanded_sizes[call];
            values[node] = source->type[node] == IR_VOID ? BUSTER_IR_NONE : (uint32_t)sizes.instruction_count - 1;
            sizes.constant_count += callee->constant_count;
            sizes.address_count += callee->address_count;
            maximum_callee = callee->instruction_count > maximum_callee ? callee->instruction_count : maximum_callee;
        }
        else
        {
            values[node] = (uint32_t)sizes.instruction_count++;
            if (calls)
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
        }
        call += calls;
    }
    IrError error = ok ? IR_OK : IR_ERROR_MEMORY;
    if (sizes.instruction_count > BUSTER_IR_INLINE_MAX_ITEMS || sizes.constant_count > BUSTER_IR_INLINE_MAX_ITEMS || sizes.address_count > BUSTER_IR_INLINE_MAX_ITEMS || sizes.extra_count > BUSTER_IR_INLINE_MAX_ITEMS || sizes.call_type_count > BUSTER_IR_INLINE_MAX_ITEMS)
    {
        error = IR_ERROR_LIMIT;
    }
    uint32_t* inner_values = error == IR_OK ? ir_allocate(scratch, maximum_callee, 4, &ok) : 0;
    if (!ok)
    {
        error = IR_ERROR_MEMORY;
    }
    IrFunction rewritten = { 0 };
    if (error == IR_OK)
    {
        error = ir_create(output, &sizes, &rewritten);
    }
    if (error == IR_OK)
    {
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
        if (source->parameter_count != 0)
        {
            memcpy(rewritten.parameter_types, source->parameter_types, source->parameter_count);
        }
        uint32_t constants = source->constant_count;
        uint32_t addresses = source->address_count;
        uint32_t extra = 0;
        uint32_t descriptors = 0;
        uint32_t types = 0;
        uint32_t cursor = 0;
        call = result->call_offsets[function];
        for (uint32_t block = 0; block < source->block_count; block += 1)
        {
            rewritten.block_offsets[block] = cursor;
            for (uint32_t node = source->block_offsets[block]; node < source->block_offsets[block + 1]; node += 1)
            {
                const uint32_t op = source->op[node];
                const bool calls = op == IR_CALL || op == IR_CALL_INDIRECT;
                if (calls && result->decisions[call] == IR_INLINE_SELECTED)
                {
                    const IrFunction* callee = &result->functions[result->callees[call]];
                    const IrCall descriptor = source->calls[source->b[node]];
                    if (callee->constant_count != 0)
                    {
                        memcpy(rewritten.constants + constants, callee->constants, (uint64_t)callee->constant_count * sizeof(IrConstant));
                    }
                    if (callee->address_count != 0)
                    {
                        memcpy(rewritten.addresses + addresses, callee->addresses, (uint64_t)callee->address_count * sizeof(IrAddress));
                    }
                    for (uint32_t inner = 0; inner + 1 < callee->instruction_count; inner += 1)
                    {
                        if (callee->op[inner] == IR_PARAM)
                        {
                            inner_values[inner] = values[source->extra[descriptor.argument_first + callee->a[inner]]];
                        }
                        else
                        {
                            inner_values[inner] = cursor;
                            ir_inline_node(callee, inner, &rewritten, cursor++, inner_values, constants, addresses);
                        }
                    }
                    if (callee->result_type != IR_VOID)
                    {
                        rewritten.op[cursor] = IR_COPY;
                        rewritten.type[cursor] = callee->result_type;
                        rewritten.a[cursor++] = inner_values[callee->a[callee->instruction_count - 1]];
                    }
                    constants += callee->constant_count;
                    addresses += callee->address_count;
                }
                else
                {
                    ir_inline_node(source, node, &rewritten, cursor, values, 0, 0);
                    if (calls)
                    {
                        IrCall descriptor = source->calls[source->b[node]];
                        for (uint32_t p = 0; p < descriptor.argument_count; p += 1)
                        {
                            rewritten.extra[extra + p] = values[source->extra[descriptor.argument_first + p]];
                        }
                        if (descriptor.parameter_count != 0)
                        {
                            memcpy(rewritten.call_types + types, source->call_types + descriptor.type_first, descriptor.parameter_count);
                        }
                        descriptor.argument_first = extra;
                        descriptor.type_first = types;
                        extra += descriptor.argument_count;
                        types += descriptor.parameter_count;
                        rewritten.calls[descriptors] = descriptor;
                        rewritten.b[cursor] = descriptors++;
                    }
                    else if (op == IR_PHI)
                    {
                        rewritten.a[cursor] = extra;
                        for (uint32_t p = 0; p < source->b[node]; p += 1)
                        {
                            rewritten.extra[extra++] = source->extra[source->a[node] + p * 2];
                            rewritten.extra[extra++] = values[source->extra[source->a[node] + p * 2 + 1]];
                        }
                    }
                    else if (op == IR_SWITCH)
                    {
                        rewritten.b[cursor] = extra;
                        const uint32_t count = source->c[node] * 2 + 1;
                        memcpy(rewritten.extra + extra, source->extra + source->b[node], (uint64_t)count * 4);
                        extra += count;
                    }
                    cursor += 1;
                }
                call += calls;
            }
        }
        rewritten.block_offsets[rewritten.block_count] = cursor;
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
            check.error = ir_inline_rewrite(unit, failed_function, output, scratch, &inlined);
        }
        if (check.error == IR_OK)
        {
            const uint64_t verification_mark = scratch->length;
            IrOptimizeWork verification = { 0 };
            // The caller CFG is unchanged, including valid unreachable regions; check the same reachable SSA
            // contract as the input, rather than deleting regions or inventing dominance for dead blocks.
            check = ir_optimize_prepare(&verification, &inlined.functions[failed_function], output, scratch, options.scalar);
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
