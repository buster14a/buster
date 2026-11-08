// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original #1218 block-placement fixtures, included by ir_optimize_test.c. A reversed chain has an independently
// known fall-through order, calls with value-bearing side tables, forward references and a label for every block.
// The existing independent executor checks results and effect order; no layout helper supplies the oracle.

static bool ir_optimize_layout_test_chain(Arena* arena, IrFunction* f, uint32_t blocks)
{
    const uint32_t calls = blocks > 1 ? blocks - 1 : 0;
    const IrSizes sizes = {
        .instruction_count = (uint64_t)blocks * 3,
        .block_count = blocks,
        .constant_count = blocks != 0,
        .extra_count = calls,
        .call_count = calls,
        .call_type_count = calls != 0,
        .parameter_count = blocks != 0,
        .label_count = blocks,
    };
    const bool created = ir_create(arena, &sizes, f) == IR_OK;
    if (created && blocks != 0)
    {
        f->result_type = IR_I64;
        f->symbol_count = 2;
        f->parameter_types[0] = IR_I64;
        f->constants[0].low = 1;
        for (uint32_t b = 0; b <= blocks; b += 1)
        {
            f->block_offsets[b] = b * 3;
        }
        for (uint32_t b = 0; b < blocks; b += 1)
        {
            f->labels[b] = b;
        }
        ir_optimize_test_instruction(f, 0, IR_PARAM, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 1, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 2, blocks == 1 ? IR_RET : IR_BR, IR_VOID, blocks == 1 ? 0 : blocks - 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        if (calls != 0)
        {
            f->call_types[0] = IR_I64;
        }
        for (uint32_t b = 1; b < blocks; b += 1)
        {
            const uint32_t i = b * 3;
            const uint32_t previous = b + 1 == blocks ? 0 : (b + 1) * 3 + 1;
            ir_optimize_test_instruction(f, i, IR_ADD, IR_I64, previous, 1, BUSTER_IR_NONE);
            ir_optimize_test_instruction(f, i + 1, IR_CALL, IR_I64, 1, b - 1, BUSTER_IR_NONE);
            ir_optimize_test_instruction(f, i + 2, b == 1 ? IR_RET : IR_BR, IR_VOID, b == 1 ? i + 1 : b - 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
            f->calls[b - 1] = (IrCall){
                .argument_first = b - 1,
                .argument_count = 1,
                .parameter_count = 1,
                .result_type = IR_I64,
            };
            f->extra[b - 1] = i;
        }
    }
    return created;
}

// Neither arm dominates the other: both are entered from b0 and have edges to one another. The fixed condition
// nevertheless reaches b3 after visiting both arms, so the independent execution has a terminating oracle.
static bool ir_optimize_layout_test_irreducible(Arena* arena, IrFunction* f)
{
    const IrSizes sizes = {
        .instruction_count = 8,
        .block_count = 4,
        .constant_count = 2,
        .extra_count = 4,
        .parameter_count = 1,
    };
    const bool created = ir_create(arena, &sizes, f) == IR_OK;
    if (created)
    {
        f->result_type = IR_I64;
        f->parameter_types[0] = IR_I1;
        f->constants[0].low = 7;
        f->constants[1].low = 9;
        for (uint32_t b = 0; b <= 4; b += 1)
        {
            f->block_offsets[b] = 2 * b;
        }
        ir_optimize_test_instruction(f, 0, IR_PARAM, IR_I1, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 1, IR_CBR, IR_VOID, 0, 1, 2);
        ir_optimize_test_instruction(f, 2, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 3, IR_CBR, IR_VOID, 0, 2, 3);
        ir_optimize_test_instruction(f, 4, IR_CONST, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 5, IR_CBR, IR_VOID, 0, 3, 1);
        ir_optimize_test_instruction(f, 6, IR_PHI, IR_I64, 0, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 7, IR_RET, IR_VOID, 6, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t incoming[] = { 1, 2, 2, 4 };
        memcpy(f->extra, incoming, sizeof(incoming));
    }
    return created;
}

// Reuse the existing aggregate ABI fixture's records, but spell a three-block execution order 0 -> 2 -> 1.
// Only the instruction columns change here; the call still reads a 16-byte by-value argument and writes its result.
static bool ir_optimize_layout_test_aggregate(Arena* arena, IrFunction* f)
{
    IrFunction original = { 0 };
    IrFunction columns = { 0 };
    const IrSizes sizes = {
        .instruction_count = 10,
        .block_count = 3,
    };
    const bool created = ir_optimize_test_aggregate_call(arena, &original) && ir_create(arena, &sizes, &columns) == IR_OK;
    if (created)
    {
        *f = original;
        f->op = columns.op;
        f->type = columns.type;
        f->flags = columns.flags;
        f->alignment_log2 = columns.alignment_log2;
        f->a = columns.a;
        f->b = columns.b;
        f->c = columns.c;
        f->instruction_count = 10;
        f->block_offsets = columns.block_offsets;
        f->block_count = 3;
        f->block_offsets[1] = 2;
        f->block_offsets[2] = 3;
        f->block_offsets[3] = 10;
        f->extra[0] = 5;
        ir_optimize_test_instruction(f, 0, IR_PARAM, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 1, IR_BR, IR_VOID, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 2, IR_RET, IR_VOID, 8, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 3, IR_NEG, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 4, IR_STACK_ADDR, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 5, IR_STACK_ADDR, IR_PTR, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 6, IR_STORE, IR_VOID, 5, 0, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 7, IR_CALL, IR_VOID, 50, 0, 4);
        ir_optimize_test_instruction(f, 8, IR_LOAD, IR_I64, 4, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 9, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
    }
    return created;
}

static bool ir_optimize_layout_test_vector(Arena* arena, IrFunction* f)
{
    const IrSizes sizes = {
        .instruction_count = 6,
        .block_count = 3,
        .vector_constant_count = 2,
    };
    const bool created = ir_create(arena, &sizes, f) == IR_OK;
    if (created)
    {
        f->result_type = IR_M16;
        f->block_offsets[1] = 2;
        f->block_offsets[2] = 3;
        f->block_offsets[3] = 6;
        f->vector_constants[0].words[0] = UINT64_C(0x1122334455667788);
        f->vector_constants[0].words[1] = UINT64_C(0xffeeddccbbaa0099);
        f->vector_constants[1] = f->vector_constants[0];
        f->vector_constants[1].words[0] ^= 1;
        ir_optimize_test_instruction(f, 0, IR_VCONST, IR_V4I32, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 1, IR_BR, IR_VOID, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 2, IR_RET, IR_VOID, 4, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 3, IR_VCONST, IR_V4I32, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 4, IR_VCMP, IR_M16, 0, 3, IR_EQ);
        f->flags[4] = IR_SIMD_LANE_I8;
        ir_optimize_test_instruction(f, 5, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
    }
    return created;
}

// The join is physically before its cases. Layout must rename the switch's default and case targets as well as
// the join's phi predecessors and values; interpreting an unchanged, naturally ordered switch would not test that.
static bool ir_optimize_layout_test_switch(Arena* arena, IrFunction* f)
{
    const IrSizes sizes = {
        .instruction_count = 8,
        .block_count = 4,
        .constant_count = 4,
        .extra_count = 9,
        .parameter_count = 1,
    };
    const bool created = ir_create(arena, &sizes, f) == IR_OK;
    if (created)
    {
        f->result_type = IR_I64;
        f->parameter_types[0] = IR_I64;
        f->constants[0].low = 0;
        f->constants[1].low = 1;
        f->constants[2].low = 13;
        f->constants[3].low = 15;
        for (uint32_t b = 0; b <= 4; b += 1)
        {
            f->block_offsets[b] = 2 * b;
        }
        ir_optimize_test_instruction(f, 0, IR_PARAM, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 1, IR_SWITCH, IR_VOID, 0, 0, 2);
        ir_optimize_test_instruction(f, 2, IR_PHI, IR_I64, 5, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 3, IR_RET, IR_VOID, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 4, IR_CONST, IR_I64, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 5, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 6, IR_CONST, IR_I64, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 7, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t extra[] = { 2, 0, 2, 1, 3, 2, 4, 3, 6 };
        memcpy(f->extra, extra, sizeof(extra));
    }
    return created;
}

static void ir_optimize_layout_test_loop(IrOptimizeTest* test, Arena* input, Arena* output, Arena* scratch)
{
    IrFunction f = { 0 };
    const bool created = ir_optimize_test_loop(input, &f);
    ir_optimize_test_expect(test, created, "layout loop fixture allocation");
    for (uint32_t path = 0; created && path < 2; path += 1)
    {
        IrOptimizeOptions options = ir_optimize_options();
        options.passes = BUSTER_IR_OPTIMIZE_LAYOUT;
        options.path = path != 0 ? BUSTER_IR_OPTIMIZE_SCALAR : BUSTER_IR_OPTIMIZE_VECTOR;
        IrOptimizeResult result = { 0 };
        const IrCheck check = ir_optimize(&f, output, scratch, options, &result);
        bool rotated = check.error == IR_OK && result.function.block_count == 4;
        if (rotated)
        {
            const IrFunction* out = &result.function;
            const uint32_t entry = out->block_offsets[1] - 1;
            const uint32_t latch = out->block_offsets[2] - 1;
            const uint32_t header = out->block_offsets[3] - 1;
            rotated = out->op[entry] == IR_BR && out->a[entry] == 2 && out->op[latch] == IR_BR && out->a[latch] == 2 &&
                      out->op[header] == IR_CBR && out->b[header] == 1 && out->c[header] == 3;
        }
        ir_optimize_test_expect(test, rotated, "loop enters its test once and its latch falls through to that test");
        arena_pop_to(output, 0);
    }
    arena_pop_to(input, 0);
}

static void ir_optimize_test_layout(IrOptimizeTest* test)
{
    Arena input = arena_reserve(16ull << 20);
    Arena vector = arena_reserve(16ull << 20);
    Arena scalar = arena_reserve(16ull << 20);
    Arena scratch = arena_reserve(32ull << 20);
    const bool ready = input.base != 0 && vector.base != 0 && scalar.base != 0 && scratch.base != 0;
    ir_optimize_test_expect(test, ready, "layout fixture arenas");
    for (uint32_t blocks = 0; ready && blocks <= 65; blocks += 1)
    {
        IrFunction f = { 0 };
        const bool created = ir_optimize_layout_test_chain(&input, &f, blocks);
        ir_optimize_test_expect(test, created, "layout reversed-chain allocation");
        if (created)
        {
            ir_optimize_test_compare(test, &f, &vector, &scalar, &scratch, BUSTER_IR_OPTIMIZE_LAYOUT);
            ir_optimize_test_compare(test, &f, &vector, &scalar, &scratch, 15);
            IrOptimizeOptions options = ir_optimize_options();
            options.passes = BUSTER_IR_OPTIMIZE_LAYOUT;
            IrOptimizeResult result = { 0 };
            const IrCheck check = ir_optimize(&f, &vector, &scratch, options, &result);
            ir_optimize_test_expect(test, check.error == IR_OK, "layout reversed-chain accepted");
            if (check.error == IR_OK)
            {
                const IrFunction* out = &result.function;
                bool falls_through = out->block_count == blocks && out->instruction_count == f.instruction_count;
                for (uint32_t b = 0; falls_through && b + 1 < blocks; b += 1)
                {
                    const uint32_t last = out->block_offsets[b + 1] - 1;
                    falls_through = out->op[last] == IR_BR && out->a[last] == b + 1;
                }
                ir_optimize_test_expect(test, falls_through, "every reversed-chain jump targets the next block");
                bool labels = out->label_count == blocks;
                for (uint32_t b = 0; labels && b < blocks; b += 1)
                {
                    labels = out->labels[b] == (b == 0 ? 0 : blocks - b);
                }
                ir_optimize_test_expect(test, labels, "labels follow their original blocks through a nonidentity permutation");
                bool calls = out->call_count == f.call_count;
                for (uint32_t b = 1; calls && b < blocks; b += 1)
                {
                    const uint32_t first = out->block_offsets[b];
                    const uint32_t i = first + 1;
                    calls = out->op[first] == IR_ADD && out->op[i] == IR_CALL && out->b[i] < out->call_count;
                    if (calls)
                    {
                        const IrCall* call = &out->calls[out->b[i]];
                        calls = call->argument_count == 1 && call->argument_first < out->extra_count && out->extra[call->argument_first] == first;
                    }
                }
                ir_optimize_test_expect(test, calls, "moved calls retain their renamed argument lists");
            }
        }
        arena_pop_to(&vector, 0);
        arena_pop_to(&input, 0);
    }
    if (ready)
    {
        ir_optimize_layout_test_loop(test, &input, &vector, &scratch);
        IrFunction f = { 0 };
        const bool created = ir_optimize_layout_test_irreducible(&input, &f);
        ir_optimize_test_expect(test, created, "layout irreducible fixture allocation");
        for (uint32_t passes = 0; created && passes < 16; passes += 1)
        {
            ir_optimize_test_compare(test, &f, &vector, &scalar, &scratch, passes);
        }
        arena_pop_to(&input, 0);
        const bool aggregate = ir_optimize_layout_test_aggregate(&input, &f);
        ir_optimize_test_expect(test, aggregate, "layout aggregate-call fixture allocation");
        for (uint32_t passes = BUSTER_IR_OPTIMIZE_LAYOUT; aggregate && passes < 16; passes += 1)
        {
            ir_optimize_test_compare(test, &f, &vector, &scalar, &scratch, passes);
        }
        arena_pop_to(&input, 0);
        const bool simd = ir_optimize_layout_test_vector(&input, &f);
        ir_optimize_test_expect(test, simd, "layout vector-payload fixture allocation");
        for (uint32_t passes = BUSTER_IR_OPTIMIZE_LAYOUT; simd && passes < 16; passes += 1)
        {
            ir_optimize_test_simd_rewrite(test, &f, &vector, &scalar, &scratch, passes);
        }
        arena_pop_to(&input, 0);
        const bool switched = ir_optimize_layout_test_switch(&input, &f);
        ir_optimize_test_expect(test, switched, "layout relocated-switch fixture allocation");
        for (uint32_t passes = BUSTER_IR_OPTIMIZE_LAYOUT; switched && passes < 16; passes += 1)
        {
            ir_optimize_test_compare(test, &f, &vector, &scalar, &scratch, passes);
        }
        if (switched)
        {
            IrOptimizeOptions options = ir_optimize_options();
            options.passes = BUSTER_IR_OPTIMIZE_LAYOUT;
            IrOptimizeResult result = { 0 };
            const IrCheck check = ir_optimize(&f, &vector, &scratch, options, &result);
            const IrFunction* out = &result.function;
            const bool moved = check.error == IR_OK && out->block_count == 4 && out->op[1] == IR_SWITCH &&
                               out->extra[out->b[1]] == 1 && out->extra[out->b[1] + 2] == 1 && out->extra[out->b[1] + 4] == 3 &&
                               out->op[out->block_offsets[1]] == IR_CONST && out->op[out->block_offsets[2]] == IR_PHI;
            ir_optimize_test_expect(test, moved, "switch default and case targets follow relocated case bodies");
        }
    }
    arena_release(&scratch);
    arena_release(&scalar);
    arena_release(&vector);
    arena_release(&input);
}
