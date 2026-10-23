// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original inlining fixtures. The interpreter executes original calls with an explicit frame stack and independently
// executes the rewritten unit. It does not use the inliner's operand, remapping, graph or arithmetic helpers. Graph
// tests use transitive closure as an independent SCC oracle. Include after ir_optimize_inline.c.

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

typedef struct IrInlineTest IrInlineTest;
struct IrInlineTest
{
    uint64_t checks;
    uint64_t failures;
    uint64_t random;
    uint64_t executions;
};

typedef unsigned __int128 IrInlineTestValue;

typedef struct IrInlineTestExecution IrInlineTestExecution;
struct IrInlineTestExecution
{
    IrInlineTestValue returned;
    uint8_t memory[64];
    uint64_t effects;
    uint32_t status;
};

typedef struct IrInlineTestFrame IrInlineTestFrame;
struct IrInlineTestFrame
{
    uint32_t function;
    uint32_t node;
    uint32_t block;
    uint32_t predecessor;
    uint32_t destination;
    bool entering;
    IrInlineTestValue arguments[8];
};

static void ir_inline_test_expect(IrInlineTest* test, bool condition, const char* description)
{
    test->checks += 1;
    if (!condition)
    {
        if (test->failures < 12)
        {
            fprintf(stderr, "ir-inline: %s\n", description);
        }
        test->failures += 1;
    }
}

static uint64_t ir_inline_test_random(IrInlineTest* test)
{
    uint64_t value = test->random;
    value ^= value << 13;
    value ^= value >> 7;
    value ^= value << 17;
    test->random = value;
    return value;
}

static uint32_t ir_inline_test_width(uint8_t type)
{
    static const uint8_t widths[] = {
        0,
        1,
        8,
        16,
        32,
        64,
        128,
        32,
        64,
        80,
        64,
    };
    return type < sizeof(widths) ? widths[type] : 0;
}

static IrInlineTestValue ir_inline_test_mask(uint32_t width)
{
    return width == 128 ? ~(IrInlineTestValue)0 : ((IrInlineTestValue)1 << width) - 1;
}

static uint32_t ir_inline_test_symbol(const IrInlineUnit* unit, uint32_t symbol)
{
    uint32_t function = UINT32_MAX;
    for (uint32_t f = 0; f < unit->count; f += 1)
    {
        if (unit->symbols[f] == symbol)
        {
            function = f;
        }
    }
    return function;
}

static IrInlineTestExecution ir_inline_test_execute(const IrInlineUnit* unit, uint32_t entry, const IrInlineTestValue* arguments, const uint8_t* memory, Arena* scratch)
{
    const uint64_t mark = scratch->length;
    IrInlineTestExecution execution = { 0 };
    memcpy(execution.memory, memory, sizeof(execution.memory));
    IrInlineTestFrame frames[32] = { 0 };
    uint32_t stride = 1;
    for (uint32_t f = 0; f < unit->count; f += 1)
    {
        stride = unit->functions[f].instruction_count > stride ? unit->functions[f].instruction_count : stride;
    }
    IrInlineTestValue* all_values = BUSTER_ARENA_PUSH_ARRAY(scratch, IrInlineTestValue, (uint64_t)stride * 32);
    execution.status = all_values && entry < unit->count ? 0 : 4;
    uint32_t depth = execution.status == 0 ? 1 : 0;
    if (depth != 0)
    {
        memset(all_values, 0, (uint64_t)stride * 32 * sizeof(*all_values));
        frames[0].function = entry;
        frames[0].entering = true;
        frames[0].predecessor = UINT32_MAX;
        memcpy(frames[0].arguments, arguments, sizeof(frames[0].arguments));
    }
    uint32_t steps = 0;
    while (execution.status == 0 && depth != 0 && steps++ < 10000)
    {
        IrInlineTestFrame* frame = &frames[depth - 1];
        const IrFunction* function = &unit->functions[frame->function];
        IrInlineTestValue* values = all_values + (uint64_t)(depth - 1) * stride;
        if (frame->entering)
        {
            frame->entering = false;
            IrInlineTestValue pending[64];
            uint32_t phis = 0;
            const uint32_t first = frame->node;
            while (frame->node < function->instruction_count && function->op[frame->node] == IR_PHI && phis < 64)
            {
                const uint32_t phi = frame->node++;
                bool found = false;
                for (uint32_t p = 0; p < function->b[phi]; p += 1)
                {
                    const uint32_t offset = function->a[phi] + p * 2;
                    if (function->extra[offset] == frame->predecessor)
                    {
                        pending[phis] = values[function->extra[offset + 1]];
                        found = true;
                    }
                }
                execution.status = found ? execution.status : 1;
                phis += 1;
            }
            for (uint32_t p = 0; p < phis && execution.status == 0; p += 1)
            {
                values[first + p] = pending[p];
            }
        }
        if (frame->node >= function->instruction_count || execution.status != 0)
        {
            execution.status = 1;
        }
        else
        {
            const uint32_t node = frame->node++;
            const uint32_t op = function->op[node];
            const uint32_t ai = function->a[node];
            const uint32_t bi = function->b[node];
            const uint32_t ci = function->c[node];
            const IrInlineTestValue a = ai < function->instruction_count ? values[ai] : 0;
            const IrInlineTestValue b = bi < function->instruction_count ? values[bi] : 0;
            const IrInlineTestValue c = ci < function->instruction_count ? values[ci] : 0;
            const uint32_t width = ir_inline_test_width(function->type[node]);
            IrInlineTestValue value = 0;
            uint32_t target = UINT32_MAX;
            switch (op)
            {
                case IR_PARAM:
                    value = ai < 8 ? frame->arguments[ai] : 0;
                    execution.status = ai < 8 ? 0 : 1;
                    break;
                case IR_CONST:
                    value = (IrInlineTestValue)function->constants[ai].high << 64 | function->constants[ai].low;
                    break;
                case IR_COPY:
                case IR_TRUNC:
                case IR_ZEXT:
                case IR_BITCAST:
                case IR_INTTOPTR:
                case IR_PTRTOINT:
                    value = a;
                    break;
                case IR_SEXT:
                {
                    const uint32_t from = ir_inline_test_width(function->type[ai]);
                    value = a;
                    if ((a & ((IrInlineTestValue)1 << (from - 1))) != 0)
                    {
                        value |= ~ir_inline_test_mask(from);
                    }
                }
                break;
                case IR_ADD:
                    value = a + b;
                    break;
                case IR_SUB:
                    value = a - b;
                    break;
                case IR_MUL:
                    value = a * b;
                    break;
                case IR_AND:
                    value = a & b;
                    break;
                case IR_OR:
                    value = a | b;
                    break;
                case IR_XOR:
                    value = a ^ b;
                    break;
                case IR_NEG:
                    value = -a;
                    break;
                case IR_NOT:
                    value = ~a;
                    break;
                case IR_SHL:
                case IR_LSHR:
                case IR_ASHR:
                {
                    execution.status = b < width ? 0 : 2;
                    if (execution.status == 0)
                    {
                        value = op == IR_SHL ? a << (uint32_t)b : a >> (uint32_t)b;
                        if (op == IR_ASHR && b != 0 && (a & ((IrInlineTestValue)1 << (width - 1))) != 0)
                        {
                            value |= ir_inline_test_mask(width) ^ ir_inline_test_mask(width - (uint32_t)b);
                        }
                    }
                }
                break;
                case IR_UDIV:
                case IR_UREM:
                {
                    execution.status = b != 0 ? 0 : 2;
                    if (b != 0)
                    {
                        value = op == IR_UDIV ? a / b : a % b;
                    }
                }
                break;
                case IR_ICMP:
                {
                    const IrInlineTestValue sign = (IrInlineTestValue)1 << (ir_inline_test_width(function->type[ai]) - 1);
                    const bool less = (a ^ sign) < (b ^ sign);
                    switch (ci)
                    {
                        case IR_EQ:
                            value = a == b;
                            break;
                        case IR_NE:
                            value = a != b;
                            break;
                        case IR_LT:
                            value = less;
                            break;
                        case IR_LE:
                            value = less || a == b;
                            break;
                        case IR_GT:
                            value = !less && a != b;
                            break;
                        case IR_GE:
                            value = !less;
                            break;
                        case IR_ULT:
                            value = a < b;
                            break;
                        case IR_ULE:
                            value = a <= b;
                            break;
                        case IR_UGT:
                            value = a > b;
                            break;
                        case IR_UGE:
                            value = a >= b;
                            break;
                        default:
                            execution.status = 1;
                            break;
                    }
                }
                break;
                case IR_SELECT:
                    value = a != 0 ? b : c;
                    break;
                case IR_SYMBOL_ADDR:
                    value = 65536 + ai;
                    break;
                case IR_ADDRESS:
                    value = a + (bi == UINT32_MAX ? 0 : b * function->addresses[ci].scale) + (IrInlineTestValue)function->addresses[ci].offset;
                    break;
                case IR_LOAD:
                case IR_STORE:
                {
                    const uint32_t bytes = (ir_inline_test_width(op == IR_LOAD ? function->type[node] : function->type[bi]) + 7) / 8;
                    if (a > 64 || bytes > 64 - (uint64_t)a)
                    {
                        execution.status = 2;
                    }
                    else
                    {
                        if (op == IR_LOAD)
                        {
                            memcpy(&value, execution.memory + (uint32_t)a, bytes);
                        }
                        else
                        {
                            memcpy(execution.memory + (uint32_t)a, &b, bytes);
                        }
                        // Observe access order, address, value and volatile annotation independently of instruction IDs.
                        execution.effects = execution.effects * UINT64_C(1099511628211) ^ op ^ (uint64_t)a * 131 ^ (uint64_t)(op == IR_LOAD ? value : b) ^ ((uint64_t)function->flags[node] << 61);
                    }
                }
                break;
                case IR_CALL:
                case IR_CALL_INDIRECT:
                {
                    const IrCall descriptor = function->calls[bi];
                    const uint32_t symbol = op == IR_CALL ? ai : (uint32_t)(a - 65536);
                    const uint32_t callee = ir_inline_test_symbol(unit, symbol);
                    if (descriptor.argument_count > 8 || depth >= 32)
                    {
                        execution.status = 1;
                    }
                    else if (callee == UINT32_MAX)
                    {
                        // A deterministic external stub makes retained call order and argument mapping observable.
                        value = symbol;
                        for (uint32_t p = 0; p < descriptor.argument_count; p += 1)
                        {
                            value = value * 257 + values[function->extra[descriptor.argument_first + p]];
                        }
                        execution.effects = execution.effects * 17 + (uint64_t)value;
                    }
                    else
                    {
                        IrInlineTestFrame* next = &frames[depth];
                        *next = (IrInlineTestFrame){
                            .function = callee,
                            .destination = node,
                            .predecessor = UINT32_MAX,
                            .entering = true,
                        };
                        for (uint32_t p = 0; p < descriptor.argument_count; p += 1)
                        {
                            next->arguments[p] = values[function->extra[descriptor.argument_first + p]];
                        }
                        memset(all_values + (uint64_t)depth * stride, 0, (uint64_t)stride * sizeof(*all_values));
                        depth += 1;
                    }
                }
                break;
                case IR_BR:
                    target = ai;
                    break;
                case IR_CBR:
                    target = a != 0 ? bi : ci;
                    break;
                case IR_SWITCH:
                {
                    target = function->extra[bi];
                    for (uint32_t p = 0; p < ci; p += 1)
                    {
                        const IrConstant constant = function->constants[function->extra[bi + 1 + p * 2]];
                        const IrInlineTestValue key = (IrInlineTestValue)constant.high << 64 | constant.low;
                        if (a == key)
                        {
                            target = function->extra[bi + 2 + p * 2];
                        }
                    }
                }
                break;
                case IR_RET:
                {
                    value = ai == UINT32_MAX ? 0 : a;
                    depth -= 1;
                    if (depth == 0)
                    {
                        execution.returned = value;
                    }
                    else
                    {
                        all_values[(uint64_t)(depth - 1) * stride + frame->destination] = value;
                    }
                }
                break;
                default:
                    execution.status = 1;
                    break;
            }
            if (target != UINT32_MAX)
            {
                frame->predecessor = frame->block;
                frame->block = target;
                frame->node = function->block_offsets[target];
                frame->entering = true;
            }
            if (op != IR_RET && function->type[node] != IR_VOID)
            {
                values[node] = value & ir_inline_test_mask(width);
            }
        }
    }
    if (execution.status == 0 && depth != 0)
    {
        execution.status = 3;
    }
    arena_pop_to(scratch, mark);
    return execution;
}

static bool ir_inline_test_same_function(const IrFunction* a, const IrFunction* b)
{
    bool same = a->instruction_count == b->instruction_count && a->block_count == b->block_count && a->extra_count == b->extra_count && a->constant_count == b->constant_count && a->slot_count == b->slot_count && a->address_count == b->address_count && a->call_count == b->call_count && a->call_type_count == b->call_type_count && a->parameter_count == b->parameter_count && a->result_type == b->result_type && a->symbol_count == b->symbol_count;
    if (same)
    {
        const void* left[] = {
            a->op,
            a->type,
            a->flags,
            a->alignment_log2,
            a->a,
            a->b,
            a->c,
            a->block_offsets,
            a->extra,
            a->constants,
            a->slots,
            a->addresses,
            a->calls,
            a->call_types,
            a->parameter_types,
        };
        const void* right[] = {
            b->op,
            b->type,
            b->flags,
            b->alignment_log2,
            b->a,
            b->b,
            b->c,
            b->block_offsets,
            b->extra,
            b->constants,
            b->slots,
            b->addresses,
            b->calls,
            b->call_types,
            b->parameter_types,
        };
        const uint64_t bytes[] = {
            a->instruction_count,
            a->instruction_count,
            a->instruction_count,
            a->instruction_count,
            (uint64_t)a->instruction_count * 4,
            (uint64_t)a->instruction_count * 4,
            (uint64_t)a->instruction_count * 4,
            ((uint64_t)a->block_count + 1) * 4,
            (uint64_t)a->extra_count * 4,
            (uint64_t)a->constant_count * sizeof(IrConstant),
            (uint64_t)a->slot_count * sizeof(IrSlot),
            (uint64_t)a->address_count * sizeof(IrAddress),
            (uint64_t)a->call_count * sizeof(IrCall),
            a->call_type_count,
            a->parameter_count,
        };
        for (uint32_t column = 0; same && column < sizeof(bytes) / sizeof(bytes[0]); column += 1)
        {
            same = bytes[column] == 0 || memcmp(left[column], right[column], bytes[column]) == 0;
        }
    }
    return same;
}

static IrFunction ir_inline_test_function(IrInlineTest* test, Arena* arena, IrSizes sizes, uint8_t result_type)
{
    IrFunction function = { 0 };
    const IrError error = ir_create(arena, &sizes, &function);
    ir_inline_test_expect(test, error == IR_OK, "fixture allocation");
    if (error == IR_OK)
    {
        function.result_type = result_type;
        function.symbol_count = 128;
        function.block_offsets[function.block_count] = function.instruction_count;
    }
    return function;
}

static void ir_inline_test_node(IrFunction* function, uint32_t node, uint8_t op, uint8_t type, uint32_t a, uint32_t b, uint32_t c)
{
    function->op[node] = op;
    function->type[node] = type;
    function->a[node] = a;
    function->b[node] = b;
    function->c[node] = c;
}

static void ir_inline_test_call(IrFunction* function, uint32_t node, uint32_t descriptor, uint32_t symbol, uint32_t first, uint32_t count, uint8_t type)
{
    ir_inline_test_node(function, node, IR_CALL, type, symbol, descriptor, UINT32_MAX);
    function->calls[descriptor] = (IrCall){
        .argument_first = first,
        .argument_count = count,
        .type_first = first,
        .parameter_count = count,
        .result_type = type,
    };
    for (uint32_t p = 0; p < count; p += 1)
    {
        function->call_types[first + p] = function->type[function->extra[first + p]];
    }
}

static void ir_inline_test_compare(IrInlineTest* test, const IrInlineUnit* input, const IrInlineResult* result, uint32_t entry, Arena* scratch, uint32_t trials)
{
    const IrInlineUnit output = {
        .functions = result->functions,
        .symbols = input->symbols,
        .flags = input->flags,
        .count = input->count,
        .symbol_count = input->symbol_count,
    };
    for (uint32_t trial = 0; trial < trials; trial += 1)
    {
        IrInlineTestValue arguments[8] = { 0 };
        uint8_t memory[64];
        for (uint32_t p = 0; p < 8; p += 1)
        {
            arguments[p] = (IrInlineTestValue)ir_inline_test_random(test) << 64 | ir_inline_test_random(test);
            if (p < input->functions[entry].parameter_count)
            {
                const uint8_t type = input->functions[entry].parameter_types[p];
                arguments[p] &= ir_inline_test_mask(ir_inline_test_width(type));
                if (type == IR_PTR)
                {
                    arguments[p] = trial % 2 == 0 ? 0 : 56;
                }
                if (trial < 4 && type != IR_PTR)
                {
                    arguments[p] = trial;
                }
            }
        }
        for (uint32_t b = 0; b < 64; b += 1)
        {
            memory[b] = (uint8_t)ir_inline_test_random(test);
        }
        const IrInlineTestExecution before = ir_inline_test_execute(input, entry, arguments, memory, scratch);
        const IrInlineTestExecution after = ir_inline_test_execute(&output, entry, arguments, memory, scratch);
        ir_inline_test_expect(test, before.status != 1 && before.status != 3 && before.status != 4, "independent interpreter supports fixture");
        ir_inline_test_expect(test, before.status == after.status && before.returned == after.returned && before.effects == after.effects && memcmp(before.memory, after.memory, 64) == 0, "before/after calls, return, fault and memory semantics");
        test->executions += 2;
    }
}

static bool ir_inline_test_run(IrInlineTest* test, const IrInlineUnit* input, IrInlineOptions options, Arena* output, Arena* scalar_output, Arena* scratch, IrInlineResult* result)
{
    IrInlineResult scalar = { 0 };
    uint32_t failed = 123;
    options.scalar = false;
    const uint64_t mark = scratch->length;
    const IrCheck vector_check = ir_optimize_inline(input, output, scratch, options, result, &failed);
    ir_inline_test_expect(test, vector_check.error == IR_OK && failed == UINT32_MAX && scratch->length == mark, "vector inline and scratch restoration");
    if (vector_check.error != IR_OK)
    {
        fprintf(stderr, "ir-inline diagnostic error=%u function=%u node=%u detail=%u\n", vector_check.error, failed, vector_check.instruction, vector_check.detail);
    }
    options.scalar = true;
    const IrCheck scalar_check = ir_optimize_inline(input, scalar_output, scratch, options, &scalar, &failed);
    ir_inline_test_expect(test, scalar_check.error == IR_OK && failed == UINT32_MAX && scratch->length == mark, "scalar inline and scratch restoration");
    const bool success = vector_check.error == IR_OK && scalar_check.error == IR_OK;
    if (success)
    {
        ir_inline_test_expect(test, result->function_count == scalar.function_count && result->call_count == scalar.call_count && result->inlined_calls == scalar.inlined_calls && result->component_count == scalar.component_count && result->input_instructions == scalar.input_instructions && result->output_instructions == scalar.output_instructions, "scalar/vector result counts");
        for (uint32_t f = 0; f < input->count; f += 1)
        {
            ir_inline_test_expect(test, ir_inline_test_same_function(&result->functions[f], &scalar.functions[f]), "scalar/vector exact function columns");
            ir_inline_test_expect(test, result->components[f] == scalar.components[f] && result->recursive[f] == scalar.recursive[f] && result->call_offsets[f] == scalar.call_offsets[f], "scalar/vector graph columns");
        }
        for (uint32_t c = 0; c < result->call_count; c += 1)
        {
            ir_inline_test_expect(test, result->call_nodes[c] == scalar.call_nodes[c] && result->callees[c] == scalar.callees[c] && result->costs[c] == scalar.costs[c] && result->expanded_sizes[c] == scalar.expanded_sizes[c] && result->decisions[c] == scalar.decisions[c], "scalar/vector exact call decisions");
        }
    }
    return success;
}

static void ir_inline_test_nested(IrInlineTest* test, Arena* input, Arena* output, Arena* twin, Arena* scratch)
{
    const uint8_t types[] = {
        IR_I8,
        IR_I16,
        IR_I32,
        IR_I64,
        IR_I128,
    };
    for (uint32_t t = 0; t < sizeof(types); t += 1)
    {
        const uint8_t type = types[t];
        IrFunction functions[3];
        functions[0] = ir_inline_test_function(test, input, (IrSizes){
                                                                .instruction_count = 6,
                                                                .block_count = 1,
                                                                .extra_count = 4,
                                                                .call_count = 2,
                                                                .call_type_count = 4,
                                                                .parameter_count = 2,
                                                            },
                                               type);
        functions[1] = ir_inline_test_function(test, input, (IrSizes){
                                                                .instruction_count = 6,
                                                                .block_count = 1,
                                                                .extra_count = 2,
                                                                .constant_count = 1,
                                                                .call_count = 1,
                                                                .call_type_count = 2,
                                                                .parameter_count = 2,
                                                            },
                                               type);
        functions[2] = ir_inline_test_function(test, input, (IrSizes){
                                                                .instruction_count = 6,
                                                                .block_count = 1,
                                                                .constant_count = 1,
                                                                .parameter_count = 2,
                                                            },
                                               type);
        for (uint32_t f = 0; f < 3; f += 1)
        {
            for (uint32_t p = 0; p < 2; p += 1)
            {
                functions[f].parameter_types[p] = type;
                ir_inline_test_node(&functions[f], p, IR_PARAM, type, p, UINT32_MAX, UINT32_MAX);
            }
            ir_inline_test_node(&functions[f], 5, IR_RET, IR_VOID, 4, UINT32_MAX, UINT32_MAX);
        }
        functions[0].extra[0] = 0;
        functions[0].extra[1] = 1;
        functions[0].extra[2] = 2;
        functions[0].extra[3] = 0;
        ir_inline_test_call(&functions[0], 2, 0, 2, 0, 2, type);
        ir_inline_test_call(&functions[0], 3, 1, 3, 2, 2, type);
        ir_inline_test_node(&functions[0], 4, IR_ADD, type, 2, 3, UINT32_MAX);
        functions[1].extra[0] = 0;
        functions[1].extra[1] = 1;
        functions[1].constants[0].low = 7;
        ir_inline_test_call(&functions[1], 2, 0, 3, 0, 2, type);
        ir_inline_test_node(&functions[1], 3, IR_CONST, type, 0, UINT32_MAX, UINT32_MAX);
        ir_inline_test_node(&functions[1], 4, IR_XOR, type, 2, 3, UINT32_MAX);
        functions[2].constants[0].low = 5;
        ir_inline_test_node(&functions[2], 2, IR_CONST, type, 0, UINT32_MAX, UINT32_MAX);
        ir_inline_test_node(&functions[2], 3, IR_ADD, type, 0, 1, UINT32_MAX);
        ir_inline_test_node(&functions[2], 4, IR_MUL, type, 3, 2, UINT32_MAX);
        const uint32_t symbols[] = {
            1,
            2,
            3,
        };
        const uint8_t flags[] = {
            0,
            BUSTER_IR_INLINE_INTERNAL,
            BUSTER_IR_INLINE_INTERNAL,
        };
        const IrInlineUnit unit = {
            functions,
            symbols,
            flags,
            3,
            128,
        };
        IrInlineResult result = { 0 };
        if (ir_inline_test_run(test, &unit, ir_inline_options(), output, twin, scratch, &result))
        {
            ir_inline_test_expect(test, result.inlined_calls == 3 && result.functions[0].call_count == 0 && result.functions[1].call_count == 0, "bottom-up nested expansion");
            ir_inline_test_expect(test, result.costs[0] == 9 && result.costs[1] == 5 && result.costs[2] == 5, "costs use rewritten callee and sparse rows");
            ir_inline_test_compare(test, &unit, &result, 0, scratch, 128);
        }
        arena_pop_to(input, 0);
        arena_pop_to(output, 0);
        arena_pop_to(twin, 0);
    }
}

static IrFunction ir_inline_test_leaf(IrInlineTest* test, Arena* arena, uint8_t type)
{
    IrFunction function = ir_inline_test_function(test, arena, (IrSizes){
                                                                   .instruction_count = 4,
                                                                   .block_count = 1,
                                                                   .constant_count = 1,
                                                                   .parameter_count = 1,
                                                               },
                                                  type);
    function.parameter_types[0] = type;
    function.constants[0].low = 3;
    ir_inline_test_node(&function, 0, IR_PARAM, type, 0, UINT32_MAX, UINT32_MAX);
    ir_inline_test_node(&function, 1, IR_CONST, type, 0, UINT32_MAX, UINT32_MAX);
    ir_inline_test_node(&function, 2, IR_ADD, type, 0, 1, UINT32_MAX);
    ir_inline_test_node(&function, 3, IR_RET, IR_VOID, 2, UINT32_MAX, UINT32_MAX);
    return function;
}

static void ir_inline_test_cfg(IrInlineTest* test, Arena* input, Arena* output, Arena* twin, Arena* scratch)
{
    IrFunction functions[2];
    functions[0] = ir_inline_test_function(test, input, (IrSizes){
                                                            .instruction_count = 12,
                                                            .block_count = 6,
                                                            .extra_count = 11,
                                                            .constant_count = 3,
                                                            .call_count = 2,
                                                            .call_type_count = 2,
                                                            .parameter_count = 2,
                                                        },
                                           IR_I64);
    functions[1] = ir_inline_test_leaf(test, input, IR_I64);
    IrFunction* f = &functions[0];
    const uint32_t offsets[] = {
        0,
        3,
        5,
        7,
        10,
        11,
        12,
    };
    memcpy(f->block_offsets, offsets, sizeof(offsets));
    f->parameter_types[0] = IR_I64;
    f->parameter_types[1] = IR_I1;
    f->constants[0].low = 0;
    f->constants[1].low = 1;
    f->constants[2].low = 9;
    const uint32_t extra[] = {
        0,
        1,
        3,
        2,
        5,
        7,
        4,
        0,
        4,
        1,
        5,
    };
    memcpy(f->extra, extra, sizeof(extra));
    ir_inline_test_node(f, 0, IR_PARAM, IR_I64, 0, UINT32_MAX, UINT32_MAX);
    ir_inline_test_node(f, 1, IR_PARAM, IR_I1, 1, UINT32_MAX, UINT32_MAX);
    ir_inline_test_node(f, 2, IR_CBR, IR_VOID, 1, 1, 2);
    ir_inline_test_node(f, 3, IR_CALL, IR_I64, 2, 0, UINT32_MAX);
    // The actual-argument slice overlaps the switch's constant-ID slice. Each role needs a fresh output slice.
    f->calls[0] = (IrCall){
        .argument_first = 7,
        .argument_count = 1,
        .type_first = 0,
        .parameter_count = 1,
        .result_type = IR_I64,
    };
    f->call_types[0] = IR_I64;
    ir_inline_test_node(f, 4, IR_BR, IR_VOID, 3, UINT32_MAX, UINT32_MAX);
    ir_inline_test_node(f, 5, IR_CONST, IR_I64, 2, UINT32_MAX, UINT32_MAX);
    ir_inline_test_node(f, 6, IR_BR, IR_VOID, 3, UINT32_MAX, UINT32_MAX);
    ir_inline_test_node(f, 7, IR_PHI, IR_I64, 1, 2, UINT32_MAX);
    ir_inline_test_node(f, 8, IR_CALL, IR_I64, 90, 1, UINT32_MAX);
    f->calls[1] = (IrCall){
        .argument_first = 5,
        .argument_count = 1,
        .type_first = 1,
        .parameter_count = 1,
        .result_type = IR_I64,
    };
    f->call_types[1] = IR_I64;
    ir_inline_test_node(f, 9, IR_SWITCH, IR_VOID, 0, 6, 2);
    ir_inline_test_node(f, 10, IR_RET, IR_VOID, 8, UINT32_MAX, UINT32_MAX);
    ir_inline_test_node(f, 11, IR_RET, IR_VOID, 7, UINT32_MAX, UINT32_MAX);
    const uint32_t symbols[] = {
        1,
        2,
    };
    const uint8_t flags[] = {
        0,
        BUSTER_IR_INLINE_INTERNAL,
    };
    const IrInlineUnit unit = {
        functions,
        symbols,
        flags,
        2,
        128,
    };
    IrInlineResult result = { 0 };
    if (ir_inline_test_run(test, &unit, ir_inline_options(), output, twin, scratch, &result))
    {
        ir_inline_test_expect(test, result.inlined_calls == 1 && result.decisions[1] == IR_INLINE_UNKNOWN_SYMBOL && result.functions[0].block_count == 6 && result.functions[0].call_count == 1, "diamond, phi, switch and retained external call");
        ir_inline_test_compare(test, &unit, &result, 0, scratch, 128);
    }
    arena_pop_to(input, 0);
    arena_pop_to(output, 0);
    arena_pop_to(twin, 0);

    functions[0] = ir_inline_test_function(test, input, (IrSizes){
                                                            .instruction_count = 11,
                                                            .block_count = 4,
                                                            .extra_count = 5,
                                                            .constant_count = 2,
                                                            .call_count = 1,
                                                            .call_type_count = 1,
                                                            .parameter_count = 1,
                                                        },
                                           IR_I8);
    functions[1] = ir_inline_test_leaf(test, input, IR_I8);
    f = &functions[0];
    const uint32_t loop_offsets[] = {
        0,
        4,
        7,
        10,
        11,
    };
    const uint32_t loop_extra[] = {
        0,
        1,
        2,
        8,
        4,
    };
    memcpy(f->block_offsets, loop_offsets, sizeof(loop_offsets));
    memcpy(f->extra, loop_extra, sizeof(loop_extra));
    f->parameter_types[0] = IR_I8;
    f->constants[0].low = 0;
    f->constants[1].low = 1;
    ir_inline_test_node(f, 0, IR_PARAM, IR_I8, 0, UINT32_MAX, UINT32_MAX);
    ir_inline_test_node(f, 1, IR_CONST, IR_I8, 0, UINT32_MAX, UINT32_MAX);
    ir_inline_test_node(f, 2, IR_CONST, IR_I8, 1, UINT32_MAX, UINT32_MAX);
    ir_inline_test_node(f, 3, IR_BR, IR_VOID, 1, UINT32_MAX, UINT32_MAX);
    ir_inline_test_node(f, 4, IR_PHI, IR_I8, 0, 2, UINT32_MAX);
    ir_inline_test_node(f, 5, IR_ICMP, IR_I1, 4, 0, IR_ULT);
    ir_inline_test_node(f, 6, IR_CBR, IR_VOID, 5, 2, 3);
    ir_inline_test_node(f, 7, IR_CALL, IR_I8, 2, 0, UINT32_MAX);
    f->calls[0] = (IrCall){
        .argument_first = 4,
        .argument_count = 1,
        .type_first = 0,
        .parameter_count = 1,
        .result_type = IR_I8,
    };
    f->call_types[0] = IR_I8;
    ir_inline_test_node(f, 8, IR_ADD, IR_I8, 4, 2, UINT32_MAX);
    ir_inline_test_node(f, 9, IR_BR, IR_VOID, 1, UINT32_MAX, UINT32_MAX);
    ir_inline_test_node(f, 10, IR_RET, IR_VOID, 4, UINT32_MAX, UINT32_MAX);
    result = (IrInlineResult){ 0 };
    if (ir_inline_test_run(test, &unit, ir_inline_options(), output, twin, scratch, &result))
    {
        ir_inline_test_expect(test, result.inlined_calls == 1 && result.functions[0].extra[3] == 10, "loop phi backedge references rebased future value");
        ir_inline_test_compare(test, &unit, &result, 0, scratch, 64);
    }
    arena_pop_to(input, 0);
    arena_pop_to(output, 0);
    arena_pop_to(twin, 0);
}

static void ir_inline_test_memory(IrInlineTest* test, Arena* input, Arena* output, Arena* twin, Arena* scratch)
{
    for (uint32_t returns = 0; returns < 2; returns += 1)
    {
        const uint8_t returned_type = returns != 0 ? IR_I64 : IR_VOID;
        IrFunction functions[2];
        functions[0] = ir_inline_test_function(test, input, (IrSizes){
                                                                .instruction_count = 5,
                                                                .block_count = 1,
                                                                .extra_count = 4,
                                                                .call_count = 2,
                                                                .call_type_count = 4,
                                                                .parameter_count = 2,
                                                            },
                                               returned_type);
        functions[1] = ir_inline_test_function(test, input, (IrSizes){
                                                                .instruction_count = 8,
                                                                .block_count = 1,
                                                                .constant_count = 1,
                                                                .address_count = 1,
                                                                .parameter_count = 2,
                                                            },
                                               returned_type);
        for (uint32_t f = 0; f < 2; f += 1)
        {
            functions[f].parameter_types[0] = IR_PTR;
            functions[f].parameter_types[1] = IR_I64;
            ir_inline_test_node(&functions[f], 0, IR_PARAM, IR_PTR, 0, UINT32_MAX, UINT32_MAX);
            ir_inline_test_node(&functions[f], 1, IR_PARAM, IR_I64, 1, UINT32_MAX, UINT32_MAX);
        }
        IrFunction* main = &functions[0];
        const uint32_t extra[] = {
            0,
            1,
            0,
            1,
        };
        memcpy(main->extra, extra, sizeof(extra));
        ir_inline_test_call(main, 2, 0, 2, 0, 2, returned_type);
        ir_inline_test_call(main, 3, 1, 2, 2, 2, returned_type);
        ir_inline_test_node(main, 4, IR_RET, IR_VOID, returns != 0 ? 3 : UINT32_MAX, UINT32_MAX, UINT32_MAX);
        IrFunction* leaf = &functions[1];
        leaf->constants[0].low = 1;
        leaf->addresses[0] = (IrAddress){
            .offset = 8,
            .scale = 1,
        };
        ir_inline_test_node(leaf, 2, IR_CONST, IR_I64, 0, UINT32_MAX, UINT32_MAX);
        ir_inline_test_node(leaf, 3, IR_ADDRESS, IR_PTR, 0, 2, 0);
        ir_inline_test_node(leaf, 4, IR_LOAD, IR_I64, 3, UINT32_MAX, UINT32_MAX);
        ir_inline_test_node(leaf, 5, IR_ADD, IR_I64, 4, 1, UINT32_MAX);
        ir_inline_test_node(leaf, 6, IR_STORE, IR_VOID, 3, 5, UINT32_MAX);
        ir_inline_test_node(leaf, 7, IR_RET, IR_VOID, returns != 0 ? 5 : UINT32_MAX, UINT32_MAX, UINT32_MAX);
        leaf->flags[4] = BUSTER_IR_VOLATILE;
        leaf->flags[6] = BUSTER_IR_VOLATILE;
        const uint32_t symbols[] = {
            1,
            2,
        };
        const uint8_t flags[] = {
            0,
            BUSTER_IR_INLINE_INTERNAL,
        };
        const IrInlineUnit unit = {
            functions,
            symbols,
            flags,
            2,
            128,
        };
        IrInlineResult result = { 0 };
        if (ir_inline_test_run(test, &unit, ir_inline_options(), output, twin, scratch, &result))
        {
            ir_inline_test_expect(test, result.inlined_calls == 2 && result.functions[0].address_count == 2 && result.functions[0].constant_count == 2, "address and constant tables cloned per call");
            ir_inline_test_compare(test, &unit, &result, 0, scratch, 128);
        }
        arena_pop_to(input, 0);
        arena_pop_to(output, 0);
        arena_pop_to(twin, 0);
    }
}

static void ir_inline_test_decisions(IrInlineTest* test, Arena* input, Arena* output, Arena* twin, Arena* scratch)
{
    for (uint32_t mode = 0; mode < 11; mode += 1)
    {
        for (uint32_t count = 0; count <= 33; count += 1)
        {
            const bool indirect = mode == 10;
            const uint32_t first = indirect ? 2 : 1;
            IrFunction functions[2];
            functions[0] = ir_inline_test_function(test, input, (IrSizes){
                                                                    .instruction_count = first + count + 1,
                                                                    .block_count = 1,
                                                                    .extra_count = count,
                                                                    .call_count = count,
                                                                    .call_type_count = count,
                                                                    .parameter_count = 1,
                                                                },
                                                   IR_I64);
            functions[1] = ir_inline_test_leaf(test, input, IR_I64);
            functions[0].parameter_types[0] = IR_I64;
            ir_inline_test_node(&functions[0], 0, IR_PARAM, IR_I64, 0, UINT32_MAX, UINT32_MAX);
            if (indirect)
            {
                ir_inline_test_node(&functions[0], 1, IR_SYMBOL_ADDR, IR_PTR, 2, UINT32_MAX, UINT32_MAX);
            }
            for (uint32_t c = 0; c < count; c += 1)
            {
                functions[0].extra[c] = 0;
                ir_inline_test_call(&functions[0], first + c, c, mode == 9 ? 90 : 2, c, 1, IR_I64);
                functions[0].calls[c].variadic = mode == 7;
                if (indirect)
                {
                    functions[0].op[first + c] = IR_CALL_INDIRECT;
                    functions[0].a[first + c] = 1;
                }
            }
            ir_inline_test_node(&functions[0], first + count, IR_RET, IR_VOID, count == 0 ? 0 : first + count - 1, UINT32_MAX, UINT32_MAX);
            if (mode == 8)
            {
                functions[1] = ir_inline_test_function(test, input, (IrSizes){
                                                                        .instruction_count = 5,
                                                                        .block_count = 2,
                                                                        .constant_count = 1,
                                                                        .parameter_count = 1,
                                                                    },
                                                       IR_I64);
                functions[1].block_offsets[1] = 2;
                functions[1].parameter_types[0] = IR_I64;
                functions[1].constants[0].low = 3;
                ir_inline_test_node(&functions[1], 0, IR_PARAM, IR_I64, 0, UINT32_MAX, UINT32_MAX);
                ir_inline_test_node(&functions[1], 1, IR_BR, IR_VOID, 1, UINT32_MAX, UINT32_MAX);
                ir_inline_test_node(&functions[1], 2, IR_CONST, IR_I64, 0, UINT32_MAX, UINT32_MAX);
                ir_inline_test_node(&functions[1], 3, IR_ADD, IR_I64, 0, 2, UINT32_MAX);
                ir_inline_test_node(&functions[1], 4, IR_RET, IR_VOID, 3, UINT32_MAX, UINT32_MAX);
            }
            const uint32_t symbols[] = {
                1,
                2,
            };
            const uint8_t flags[] = {
                0,
                mode == 5 ? 0 : BUSTER_IR_INLINE_INTERNAL | (mode == 6 ? BUSTER_IR_INLINE_NOINLINE : 0) | (mode == 7 ? BUSTER_IR_INLINE_VARIADIC : 0),
            };
            const IrInlineUnit unit = {
                functions,
                symbols,
                flags,
                2,
                128,
            };
            IrInlineOptions options = ir_inline_options();
            if (mode == 1)
            {
                options.enabled = false;
            }
            if (mode == 2)
            {
                options.maximum_cost = 3;
            }
            if (mode == 3)
            {
                options.maximum_function_growth = 2;
            }
            if (mode == 4)
            {
                options.maximum_unit_instructions = functions[0].instruction_count + functions[1].instruction_count + 2;
            }
            IrInlineResult result = { 0 };
            if (ir_inline_test_run(test, &unit, options, output, twin, scratch, &result))
            {
                const uint32_t expected_count = mode == 0 ? count : (mode == 3 || mode == 4) && count != 0 ? 1
                                                                                                           : 0;
                ir_inline_test_expect(test, result.inlined_calls == expected_count, "disabled, linkage, shape and exact budget admission");
                for (uint32_t c = 0; c < count; c += 1)
                {
                    const uint8_t expected = mode == 0 ? IR_INLINE_SELECTED : mode == 1            ? IR_INLINE_DISABLED
                                                                          : mode == 2              ? IR_INLINE_COST
                                                                          : mode == 3 || mode == 4 ? c == 0 ? IR_INLINE_SELECTED : IR_INLINE_GROWTH
                                                                          : mode == 5              ? IR_INLINE_EXTERNAL
                                                                          : mode == 6              ? IR_INLINE_NOINLINE
                                                                          : mode == 7              ? IR_INLINE_VARIADIC
                                                                          : mode == 8              ? IR_INLINE_BODY
                                                                          : mode == 9              ? IR_INLINE_UNKNOWN_SYMBOL
                                                                                                   : IR_INLINE_INDIRECT;
                    ir_inline_test_expect(test, result.decisions[c] == expected, "original-order decision reason");
                }
                ir_inline_test_compare(test, &unit, &result, 0, scratch, 4);
            }
            arena_pop_to(input, 0);
            arena_pop_to(output, 0);
            arena_pop_to(twin, 0);
        }
    }
}

static void ir_inline_test_graphs(IrInlineTest* test, Arena* input, Arena* output, Arena* twin, Arena* scratch)
{
    for (uint32_t trial = 0; trial < 64; trial += 1)
    {
        const uint32_t count = trial % 31 + 1;
        uint8_t edges[32][32] = { { 0 } };
        uint8_t reachable[32][32] = { { 0 } };
        IrFunction functions[32];
        uint32_t symbols[32];
        uint8_t flags[32];
        for (uint32_t f = 0; f < count; f += 1)
        {
            uint32_t calls = 0;
            for (uint32_t g = 0; g < count; g += 1)
            {
                const bool edge = trial < 32 ? g == f + 1 || (f == count - 1 && g == 0 && trial % 3 == 0) : ir_inline_test_random(test) % 11 == 0;
                reachable[f][g] = edges[f][g] = edge;
                calls += edge;
            }
            functions[f] = ir_inline_test_function(test, input, (IrSizes){
                                                                    .instruction_count = calls + 2,
                                                                    .block_count = 1,
                                                                    .extra_count = calls,
                                                                    .call_count = calls,
                                                                    .call_type_count = calls,
                                                                    .parameter_count = 1,
                                                                },
                                                   IR_I64);
            functions[f].parameter_types[0] = IR_I64;
            ir_inline_test_node(&functions[f], 0, IR_PARAM, IR_I64, 0, UINT32_MAX, UINT32_MAX);
            uint32_t call = 0;
            for (uint32_t g = 0; g < count; g += 1)
            {
                if (edges[f][g])
                {
                    functions[f].extra[call] = 0;
                    ir_inline_test_call(&functions[f], call + 1, call, g + 1, call, 1, IR_I64);
                    call += 1;
                }
            }
            ir_inline_test_node(&functions[f], calls + 1, IR_RET, IR_VOID, 0, UINT32_MAX, UINT32_MAX);
            symbols[f] = f + 1;
            flags[f] = BUSTER_IR_INLINE_INTERNAL;
        }
        for (uint32_t via = 0; via < count; via += 1)
        {
            for (uint32_t from = 0; from < count; from += 1)
            {
                for (uint32_t to = 0; to < count; to += 1)
                {
                    reachable[from][to] |= reachable[from][via] && reachable[via][to];
                }
            }
        }
        const IrInlineUnit unit = {
            functions,
            symbols,
            flags,
            count,
            128,
        };
        IrInlineResult result = { 0 };
        if (ir_inline_test_run(test, &unit, ir_inline_options(), output, twin, scratch, &result))
        {
            for (uint32_t f = 0; f < count; f += 1)
            {
                ir_inline_test_expect(test, result.recursive[f] == reachable[f][f], "iterative SCC recursion versus transitive closure");
                for (uint32_t g = 0; g < count; g += 1)
                {
                    const bool same = f == g || (reachable[f][g] && reachable[g][f]);
                    ir_inline_test_expect(test, (result.components[f] == result.components[g]) == same, "iterative SCC membership versus transitive closure");
                    if (edges[f][g] && !same)
                    {
                        ir_inline_test_expect(test, result.components[g] < result.components[f], "callee-before-caller component order");
                    }
                }
                for (uint32_t c = result.call_offsets[f]; c < result.call_offsets[f + 1]; c += 1)
                {
                    if (result.recursive[result.callees[c]])
                    {
                        ir_inline_test_expect(test, result.decisions[c] == IR_INLINE_RECURSIVE, "every recursive callee is retained");
                    }
                }
            }
        }
        arena_pop_to(input, 0);
        arena_pop_to(output, 0);
        arena_pop_to(twin, 0);
    }
}

static void ir_inline_test_threshold_guards(IrInlineTest* test)
{
    const long page = sysconf(_SC_PAGESIZE);
    void* mapping = page > 0 ? mmap(0, (uint64_t)page * 4, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    const bool ready = mapping != MAP_FAILED && mprotect((uint8_t*)mapping + page, page, PROT_NONE) == 0 && mprotect((uint8_t*)mapping + page * 3, page, PROT_NONE) == 0;
    ir_inline_test_expect(test, ready, "guarded cost/output mappings");
    for (uint32_t count = 0; ready && count <= 65; count += 1)
    {
        uint32_t* costs = (uint32_t*)((uint8_t*)mapping + page - count * 4);
        uint8_t* result = (uint8_t*)mapping + page * 3 - count;
        uint8_t expected[65];
        for (uint32_t i = 0; i < count; i += 1)
        {
            const uint32_t choices[] = {
                0,
                1,
                31,
                32,
                33,
                INT32_MAX,
                (uint32_t)INT32_MAX + 1,
                UINT32_MAX,
            };
            costs[i] = choices[i % (sizeof(choices) / sizeof(choices[0]))];
        }
        const uint32_t limits[] = {
            0,
            32,
            INT32_MAX,
            UINT32_MAX,
        };
        for (uint32_t l = 0; l < sizeof(limits) / sizeof(limits[0]); l += 1)
        {
            ir_inline_threshold(costs, count, limits[l], true, expected);
            ir_inline_threshold(costs, count, limits[l], false, result);
            ir_inline_test_expect(test, count == 0 || memcmp(expected, result, count) == 0, "guarded unsigned cost comparison scalar/vector twin");
            for (uint32_t i = 0; i < count; i += 1)
            {
                ir_inline_test_expect(test, result[i] == (costs[i] <= limits[l]), "cost threshold independent scalar expression");
            }
        }
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, (uint64_t)page * 4);
    }
}

static void ir_inline_test_empty_and_identity(IrInlineTest* test, Arena* input, Arena* output, Arena* twin, Arena* scratch)
{
    const IrInlineUnit empty = { 0 };
    IrInlineResult result = { 0 };
    if (ir_inline_test_run(test, &empty, ir_inline_options(), output, twin, scratch, &result))
    {
        ir_inline_test_expect(test, result.function_count == 0 && result.call_count == 0 && result.component_count == 0 && result.output_instructions == 0, "empty unit");
    }
    arena_pop_to(output, 0);
    arena_pop_to(twin, 0);
    const uint8_t types[] = {
        IR_VOID,
        IR_I1,
        IR_I8,
        IR_I16,
        IR_I32,
        IR_I64,
        IR_I128,
        IR_F32,
        IR_F64,
        IR_F80,
        IR_PTR,
    };
    for (uint32_t t = 0; t < sizeof(types); t += 1)
    {
        const uint8_t type = types[t];
        const uint32_t parameters = type != IR_VOID;
        IrFunction functions[2];
        functions[0] = ir_inline_test_function(test, input, (IrSizes){
                                                                .instruction_count = parameters + 2,
                                                                .block_count = 1,
                                                                .extra_count = parameters,
                                                                .call_count = 1,
                                                                .call_type_count = parameters,
                                                                .parameter_count = parameters,
                                                                .slot_count = 1,
                                                            },
                                               type);
        functions[1] = ir_inline_test_function(test, input, (IrSizes){
                                                                .instruction_count = parameters + 1,
                                                                .block_count = 1,
                                                                .parameter_count = parameters,
                                                            },
                                               type);
        functions[0].slots[0] = (IrSlot){
            .size = 16,
            .alignment = 16,
            .type = IR_I128,
            .flags = BUSTER_IR_SLOT_ADDRESS_TAKEN,
        };
        if (parameters != 0)
        {
            for (uint32_t f = 0; f < 2; f += 1)
            {
                functions[f].parameter_types[0] = type;
                ir_inline_test_node(&functions[f], 0, IR_PARAM, type, 0, UINT32_MAX, UINT32_MAX);
            }
            functions[0].extra[0] = 0;
        }
        ir_inline_test_call(&functions[0], parameters, 0, 2, 0, parameters, type);
        ir_inline_test_node(&functions[0], parameters + 1, IR_RET, IR_VOID, parameters != 0 ? 1 : UINT32_MAX, UINT32_MAX, UINT32_MAX);
        ir_inline_test_node(&functions[1], parameters, IR_RET, IR_VOID, parameters != 0 ? 0 : UINT32_MAX, UINT32_MAX, UINT32_MAX);
        const uint32_t symbols[] = {
            1,
            2,
        };
        const uint8_t flags[] = {
            0,
            BUSTER_IR_INLINE_INTERNAL,
        };
        const IrInlineUnit unit = {
            functions,
            symbols,
            flags,
            2,
            128,
        };
        result = (IrInlineResult){ 0 };
        if (ir_inline_test_run(test, &unit, ir_inline_options(), output, twin, scratch, &result))
        {
            ir_inline_test_expect(test, result.inlined_calls == 1 && result.functions[0].instruction_count == parameters + 1 + (parameters != 0) && result.functions[0].slot_count == 1 && result.functions[0].slots[0].flags == BUSTER_IR_SLOT_ADDRESS_TAKEN, "empty void removal, typed identity and caller stack metadata");
            ir_inline_test_compare(test, &unit, &result, 0, scratch, 32);
        }
        arena_pop_to(input, 0);
        arena_pop_to(output, 0);
        arena_pop_to(twin, 0);
    }
}

static void ir_inline_test_failure(IrInlineTest* test, const IrInlineUnit* unit, Arena* output, Arena* scratch, IrInlineOptions options, IrError expected)
{
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    IrInlineResult result;
    memset(&result, 0xa5, sizeof(result));
    IrInlineResult preserved;
    memcpy(&preserved, &result, sizeof(result));
    uint32_t failed = 0;
    const IrCheck check = ir_optimize_inline(unit, output, scratch, options, &result, &failed);
    if (check.error != expected)
    {
        fprintf(stderr, "ir-inline error diagnostic actual=%u expected=%u function=%u node=%u detail=%u\n", check.error, expected, failed, check.instruction, check.detail);
    }
    ir_inline_test_expect(test, check.error == expected, "expected invalid input or memory error");
    ir_inline_test_expect(test, memcmp(&result, &preserved, sizeof(result)) == 0 && output->length == output_mark && scratch->length == scratch_mark, "transactional result and arena rollback");
}

static void ir_inline_test_failures(IrInlineTest* test, Arena* input, Arena* output, Arena* twin, Arena* scratch)
{
    IrFunction functions[2];
    functions[0] = ir_inline_test_function(test, input, (IrSizes){
                                                            .instruction_count = 3,
                                                            .block_count = 1,
                                                            .extra_count = 1,
                                                            .call_count = 1,
                                                            .call_type_count = 1,
                                                            .parameter_count = 1,
                                                        },
                                           IR_I64);
    functions[1] = ir_inline_test_leaf(test, input, IR_I64);
    functions[0].parameter_types[0] = IR_I64;
    functions[0].extra[0] = 0;
    ir_inline_test_node(&functions[0], 0, IR_PARAM, IR_I64, 0, UINT32_MAX, UINT32_MAX);
    ir_inline_test_call(&functions[0], 1, 0, 2, 0, 1, IR_I64);
    ir_inline_test_node(&functions[0], 2, IR_RET, IR_VOID, 1, UINT32_MAX, UINT32_MAX);
    uint32_t symbols[] = {
        1,
        2,
    };
    uint8_t flags[] = {
        0,
        BUSTER_IR_INLINE_INTERNAL,
    };
    IrInlineUnit unit = {
        functions,
        symbols,
        flags,
        2,
        128,
    };
    const IrInlineOptions defaults = ir_inline_options();
    IrInlineResult result = { 0 };
    const bool ready = ir_inline_test_run(test, &unit, defaults, output, twin, scratch, &result);
    const uint64_t needed = output->length;
    arena_pop_to(output, 0);
    arena_pop_to(twin, 0);
    if (ready)
    {
        const uint64_t capacity = output->capacity;
        for (uint64_t bytes = 0; bytes < needed; bytes += 13)
        {
            output->capacity = bytes;
            ir_inline_test_failure(test, &unit, output, scratch, defaults, IR_ERROR_MEMORY);
        }
        output->capacity = needed - 1;
        ir_inline_test_failure(test, &unit, output, scratch, defaults, IR_ERROR_MEMORY);
        output->capacity = capacity;
        const uint64_t scratch_capacity = scratch->capacity;
        for (uint64_t bytes = 0; bytes <= 4096; bytes += 31)
        {
            scratch->capacity = bytes;
            const uint64_t mark = output->length;
            IrInlineResult tentative = { 0 };
            const IrCheck check = ir_optimize_inline(&unit, output, scratch, defaults, &tentative, 0);
            ir_inline_test_expect(test, (check.error == IR_ERROR_MEMORY && output->length == mark) || check.error == IR_OK, "scratch shortage either rolls back or succeeds completely");
            ir_inline_test_expect(test, scratch->length == 0, "scratch shortage restores entry mark");
            arena_pop_to(output, mark);
        }
        scratch->capacity = scratch_capacity;
    }
    symbols[1] = 1;
    ir_inline_test_failure(test, &unit, output, scratch, defaults, IR_ERROR_METADATA);
    symbols[1] = 2;
    flags[1] = 0x80;
    ir_inline_test_failure(test, &unit, output, scratch, defaults, IR_ERROR_METADATA);
    flags[1] = BUSTER_IR_INLINE_INTERNAL;
    functions[1].symbol_count = 129;
    ir_inline_test_failure(test, &unit, output, scratch, defaults, IR_ERROR_METADATA);
    functions[1].symbol_count = 128;
    functions[0].calls[0].variadic = true;
    ir_inline_test_failure(test, &unit, output, scratch, defaults, IR_ERROR_TYPE);
    functions[0].calls[0].variadic = false;
    functions[1].result_type = IR_VOID;
    functions[1].a[3] = UINT32_MAX;
    ir_inline_test_failure(test, &unit, output, scratch, defaults, IR_ERROR_TYPE);
    functions[1].result_type = IR_I64;
    functions[1].a[3] = 2;
    functions[0].extra[0] = 2;
    ir_inline_test_failure(test, &unit, output, scratch, defaults, IR_ERROR_TYPE);
    functions[0].extra[0] = 0;
    functions[0].instruction_count = BUSTER_IR_INLINE_MAX_ITEMS + 1;
    ir_inline_test_failure(test, &unit, output, scratch, defaults, IR_ERROR_LIMIT);
    functions[0].instruction_count = 3;
    IrInlineOptions bounded = defaults;
    bounded.maximum_unit_instructions = 6;
    ir_inline_test_failure(test, &unit, output, scratch, bounded, IR_ERROR_LIMIT);
    bounded = defaults;
    bounded.maximum_function_growth = BUSTER_IR_INLINE_MAX_ITEMS + 1;
    ir_inline_test_failure(test, &unit, output, scratch, bounded, IR_ERROR_LIMIT);
    unit.count = BUSTER_IR_INLINE_MAX_FUNCTIONS + 1;
    ir_inline_test_failure(test, &unit, output, scratch, defaults, IR_ERROR_LIMIT);
    unit.count = 2;
    const uint8_t* original_op = functions[0].op;
    functions[0].op = output->base;
    ir_inline_test_failure(test, &unit, output, scratch, defaults, IR_ERROR_STRUCTURE);
    functions[0].op = (uint8_t*)original_op;
    IrInlineResult sentinel = { .function_count = 123 };
    const IrCheck overlap = ir_optimize_inline(&unit, output, output, defaults, &sentinel, 0);
    ir_inline_test_expect(test, overlap.error == IR_ERROR_STRUCTURE && sentinel.function_count == 123 && output->length == 0, "overlapping arenas rejected before mutation");
    uint32_t* tail_error = (uint32_t*)output->base;
    *tail_error = 17;
    const IrCheck tail_alias = ir_optimize_inline(&unit, output, scratch, defaults, &sentinel, tail_error);
    ir_inline_test_expect(test, tail_alias.error == IR_ERROR_STRUCTURE && *tail_error == 17 && sentinel.function_count == 123 && output->length == 0 && scratch->length == 0, "error-index output tail alias rejected without corrupting audit columns");
    uint32_t* scratch_error = (uint32_t*)scratch->base;
    *scratch_error = 19;
    const IrCheck scratch_alias = ir_optimize_inline(&unit, output, scratch, defaults, &sentinel, scratch_error);
    ir_inline_test_expect(test, scratch_alias.error == IR_ERROR_STRUCTURE && *scratch_error == 19 && sentinel.function_count == 123 && output->length == 0 && scratch->length == 0, "error-index scratch alias rejected without mutation");
    const IrCheck result_alias = ir_optimize_inline(&unit, output, scratch, defaults, &sentinel, &sentinel.function_count);
    ir_inline_test_expect(test, result_alias.error == IR_ERROR_STRUCTURE && sentinel.function_count == 123 && output->length == 0 && scratch->length == 0, "error-index and result alias rejected without mutation");
    // Inputs retained before the output tail are a supported composition, unlike any borrowed span in the tail.
    result = (IrInlineResult){ 0 };
    const uint64_t input_mark = input->length;
    const IrCheck append = ir_optimize_inline(&unit, input, scratch, defaults, &result, 0);
    ir_inline_test_expect(test, append.error == IR_OK && input->length > input_mark, "append after immutable source prefix");
    if (append.error == IR_OK)
    {
        ir_inline_test_compare(test, &unit, &result, 0, scratch, 16);
    }
    arena_pop_to(input, 0);
    arena_pop_to(output, 0);
    arena_pop_to(twin, 0);
}

static __attribute__((unused)) int run_ir_optimize_inline_tests(int argc, char** argv)
{
    (void)argv;
    IrInlineTest test = { .random = UINT64_C(0x823a91c57e684dbf) };
    Arena input = arena_reserve(64u << 20);
    Arena output = arena_reserve(64u << 20);
    Arena twin = arena_reserve(64u << 20);
    Arena scratch = arena_reserve(64u << 20);
    ir_inline_test_expect(&test, argc == 0 && input.base && output.base && twin.base && scratch.base, "arguments and arena setup");
    if (test.failures == 0)
    {
        ir_inline_test_nested(&test, &input, &output, &twin, &scratch);
        ir_inline_test_cfg(&test, &input, &output, &twin, &scratch);
        ir_inline_test_memory(&test, &input, &output, &twin, &scratch);
        ir_inline_test_decisions(&test, &input, &output, &twin, &scratch);
        ir_inline_test_graphs(&test, &input, &output, &twin, &scratch);
        ir_inline_test_threshold_guards(&test);
        ir_inline_test_empty_and_identity(&test, &input, &output, &twin, &scratch);
        ir_inline_test_failures(&test, &input, &output, &twin, &scratch);
    }
    arena_release(&scratch);
    arena_release(&twin);
    arena_release(&output);
    arena_release(&input);
    fprintf(stderr, "ir-inline checks=%" PRIu64 " failures=%" PRIu64 " independent-executions=%" PRIu64 "\n", test.checks, test.failures, test.executions);
    return test.failures == 0 ? 0 : 1;
}
