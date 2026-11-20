// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

// The scheduler needs no arena space in these cases, including when asked to publish the permutation.
static void tier1_schedule_ir_test_identity(IrOptimizeTest* test, IrFunction* f, const char* description)
{
    uint8_t storage[1024];
    for (uint32_t trial = 0; trial < 3; trial += 1)
    {
        Arena scratch = { .base = storage, .length = 8, .capacity = trial == 0 ? sizeof(storage) : 8, .peak = 8 };
        uint32_t order[10];
        memset(order, 0xff, sizeof(order));
        Tier1ScheduleIrStats stats = { .segments = 1, .moved = 1, .glued = 1 };
        const IrError error = tier1_schedule_ir_function(f, &scratch, trial == 2 ? NULL : order + 1, &stats);
        bool ok = error == IR_OK && stats.segments == 0 && stats.moved == 0 && stats.glued == 0 && scratch.length == 8 && scratch.peak == 8;
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(order); i += 1)
        {
            const uint32_t expected = trial != 2 && i > 0 && i <= f->instruction_count ? i - 1 : UINT32_MAX;
            ok = ok && order[i] == expected;
        }
        ir_optimize_test_expect(test, ok, description);
    }
}

static void tier1_schedule_ir_test_empty_segments(IrOptimizeTest* test)
{
    uint8_t ops[] = { IR_PARAM, IR_FENCE, IR_LOAD, IR_FENCE, IR_FENCE, IR_LOAD, IR_FENCE, IR_RET };
    uint8_t flags[] = { 0, 0, BUSTER_IR_VOLATILE, 0, 0, BUSTER_IR_ATOMIC, 0, 0 };
    uint32_t operands[8] = { 0 };
    uint32_t blocks[] = { 0, 0, 4, 4, 8, 8 };
    IrFunction f = { .op = ops, .flags = flags, .a = operands, .b = operands, .c = operands, .block_offsets = blocks };
    tier1_schedule_ir_test_identity(test, &f, "preschedule: empty function");
    f.block_count = 1;
    tier1_schedule_ir_test_identity(test, &f, "preschedule: empty block");
    f.instruction_count = BUSTER_ARRAY_LENGTH(ops);
    f.block_count = BUSTER_ARRAY_LENGTH(blocks) - 1;
    tier1_schedule_ir_test_identity(test, &f, "preschedule: all barriers");
    // Rows 3 and 4 touch in the columns but belong to different blocks, with an empty block in between.
    ops[1] = IR_UNDEF;
    ops[3] = IR_UNDEF;
    ops[4] = IR_UNDEF;
    ops[6] = IR_UNDEF;
    tier1_schedule_ir_test_identity(test, &f, "preschedule: singletons separated by an observable load or a block end");
}

// The two unary operations tie. The independent multiply comes first, moving three instructions and their debug data.
static void tier1_schedule_ir_test_segment(IrOptimizeTest* test)
{
    const uint32_t none = BUSTER_IR_NONE;
    uint8_t ops[] = { IR_PARAM, IR_NEG, IR_NOT, IR_MUL, IR_ADD, IR_ADD, IR_RET };
    uint8_t types[] = { IR_I64, IR_I64, IR_I64, IR_I64, IR_I64, IR_I64, IR_VOID };
    uint8_t flags[7] = { 0 };
    uint8_t alignments[7] = { 0 };
    uint32_t a[] = { 0, 0, 0, 0, 1, 4, 5 };
    uint32_t b[] = { none, none, none, 0, 2, 3, none };
    uint32_t c[] = { none, none, none, none, none, none, none };
    uint32_t blocks[] = { 0, 7 };
    uint32_t origins[] = { 0, 1, 2, 3, 4, 5, 6 };
    IrDebugBinding binding = { .entry = 3, .value = 3, .anchor = 3, .kind = IR_DEBUG_BIND_VALUE, .bytes = 8, .type = IR_I64 };
    IrDebugRegion region = { .begin = 3, .end = 6, .block_end = 7, .value = 3, .next = none, .kind = IR_DEBUG_BIND_VALUE, .bytes = 8, .type = IR_I64 };
    uint8_t binding_flags[] = { BUSTER_IR_DEBUG_BIND_REGIONS };
    IrDebugInfo debug = {
        .origins = origins,
        .instruction_count = 7,
        .entry_count = 7,
        .local_count = 1,
        .bindings = &binding,
        .binding_count = 1,
        .regions = &region,
        .region_count = 1,
        .binding_flags = binding_flags,
    };
    Tier1ScheduleIrOriginal original = { .function = {
        .op = ops,
        .type = types,
        .flags = flags,
        .alignment_log2 = alignments,
        .a = a,
        .b = b,
        .c = c,
        .block_offsets = blocks,
        .block_count = 1,
        .instruction_count = 7,
        .parameter_count = 1,
        .parameter_types = types,
        .result_type = IR_I64,
        .debug = &debug,
    } };
    const uint32_t expected[] = { 0, 3, 1, 2, 4, 5, 6 };
    const uint32_t identity[] = { 0, 1, 2, 3, 4, 5, 6 };
    uint8_t data[8192];
    _Alignas(8) uint8_t work[1024];
    // After the successful run, deny all space and then only the last byte of the permutation's scratch.
    uint64_t capacities[] = { sizeof(work), 8, 8 };
    for (uint32_t trial = 0; trial < BUSTER_ARRAY_LENGTH(capacities); trial += 1)
    {
        Arena copies = { .base = data, .capacity = sizeof(data) };
        Tier1ScheduleIrOriginal copy = { 0 };
        const bool copied = tier1_schedule_ir_copy(&original.function, &copies, &copy);
        ir_optimize_test_expect(test, copied, "preschedule: copy the original instructions and debug sidecar");
        if (copied)
        {
            Arena scratch = { .base = work, .length = 8, .capacity = capacities[trial], .peak = 8 };
            uint32_t order[7];
            uint32_t place[7];
            memset(order, 0xff, sizeof(order));
            Tier1ScheduleIrStats stats = { .segments = 1, .moved = 1, .glued = 1 };
            const IrError error = tier1_schedule_ir_function(&copy.function, &scratch, order, &stats);
            ir_optimize_test_expect(test, scratch.length == 8, "preschedule: restore the scratch mark");
            if (trial == 0)
            {
                capacities[2] = scratch.peak - 1;
                ir_optimize_test_expect(test, error == IR_OK && stats.segments == 1 && stats.moved == 3 && stats.glued == 0, "preschedule: schedule the real segment");
                ir_optimize_test_expect(test, memcmp(order, expected, sizeof(expected)) == 0 && tier1_schedule_ir_verify(&original, &copy.function, order, place) &&
                                        ir_verify_complete(&copy.function, &copies).error == IR_OK,
                                        "preschedule: exact tie order, operands, and debug origins, bindings and regions");
                const IrOptimizeExecution run = ir_optimize_test_execute(&copy.function, &copies, 5, 0);
                ir_optimize_test_expect(test, run.status == 1 && run.value == 14, "preschedule: 5 * 5 + (-5) + (~5)");
            }
            else
            {
                bool untouched = ir_optimize_test_equal(&original.function, &copy.function) && tier1_schedule_ir_verify(&original, &copy.function, identity, place);
                for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(order); i += 1)
                {
                    untouched = untouched && order[i] == UINT32_MAX;
                }
                ir_optimize_test_expect(test, error == IR_ERROR_MEMORY && stats.segments == 0 && stats.moved == 0 && stats.glued == 0 && untouched,
                                        "preschedule: leave instructions, debug state and caller order untouched on failure");
            }
        }
    }
}
