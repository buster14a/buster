// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #2818's independently specified windows, aliases and barriers, included by ir_test.c. Every placement modulo 16
// runs with exact-length columns ending at guard pages, then through the shared interpreter before and after pairing.
// The overlap case distinguishes sequential scalar stores from an incorrectly grouped load: p[1] = p[0]; p[2] = p[1].

enum
{
    IR_COPY_TEST_STACK,
    IR_COPY_TEST_PARAM,
    IR_COPY_TEST_SYMBOL,
    IR_COPY_TEST_IDENTICAL,
    IR_COPY_TEST_DISJOINT,
    IR_COPY_TEST_OVERLAP,
    IR_COPY_TEST_OPAQUE,
    IR_COPY_TEST_VOLATILE,
    IR_COPY_TEST_ATOMIC,
    IR_COPY_TEST_NONADJACENT,
    IR_COPY_TEST_EXTRA_USE,
    IR_COPY_TEST_INTERVENING,
    IR_COPY_TEST_BLOCK,
    IR_COPY_TEST_SOURCE_PARAM,
    IR_COPY_TEST_SLOT_VOLATILE,
    IR_COPY_TEST_UNMARKED,
    IR_COPY_TEST_COUNT = IR_COPY_TEST_UNMARKED + 4,
};

static bool ir_copy_test_function(Arena* arena, IrFunction* f, uint32_t padding, uint32_t kind)
{
    const uint32_t load = 8 + padding;
    const IrSizes sizes = {
        .instruction_count = load + 10,
        .block_count = kind == IR_COPY_TEST_BLOCK ? 2 : 1,
        .constant_count = 2,
        .slot_count = 2,
        .address_count = 4,
        .parameter_count = 1,
    };
    const bool created = ir_create(arena, &sizes, f) == IR_OK;
    if (created)
    {
        f->result_type = IR_I64;
        f->symbol_count = 3;
        f->parameter_types[0] = IR_PTR;
        f->block_offsets[f->block_count] = f->instruction_count;
        if (f->block_count == 2)
        {
            f->block_offsets[1] = load + 3;
        }
        for (uint32_t s = 0; s < 2; s += 1)
        {
            f->slots[s] = (IrSlot){ .size = 64, .alignment = 8, .type = IR_VOID, .flags = BUSTER_IR_SLOT_ADDRESS_TAKEN };
        }
        if (kind == IR_COPY_TEST_SLOT_VOLATILE)
        {
            f->slots[0].flags |= BUSTER_IR_SLOT_VOLATILE;
        }
        f->constants[0].low = UINT64_C(0x0123456789abcdef);
        f->constants[1].low = UINT64_C(0xfedcba9876543210);
        f->addresses[0] = (IrAddress){ .offset = 8, .scale = 1 };
        f->addresses[1] = (IrAddress){ .offset = kind == IR_COPY_TEST_OVERLAP ? 8 : kind == IR_COPY_TEST_DISJOINT ? 16 : 0, .scale = 1 };
        f->addresses[2] = (IrAddress){ .offset = 8, .scale = 1 };
        f->addresses[3] = (IrAddress){ .offset = kind == IR_COPY_TEST_NONADJACENT ? 16 : 8, .scale = 1 };
        ir_test_instruction(f, 0, IR_PARAM, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(f, 1, IR_STACK_ADDR, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(f, 2, IR_STACK_ADDR, IR_PTR, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        if (kind == IR_COPY_TEST_PARAM || kind == IR_COPY_TEST_SOURCE_PARAM)
        {
            ir_test_instruction(f, kind == IR_COPY_TEST_PARAM ? 2 : 1, IR_COPY, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        else if (kind == IR_COPY_TEST_SYMBOL)
        {
            ir_test_instruction(f, 2, IR_SYMBOL_ADDR, IR_PTR, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        else if (kind >= IR_COPY_TEST_IDENTICAL && kind <= IR_COPY_TEST_OVERLAP)
        {
            ir_test_instruction(f, 2, IR_ADDRESS, IR_PTR, 1, BUSTER_IR_NONE, 1);
        }
        else if (kind == IR_COPY_TEST_OPAQUE)
        {
            ir_test_instruction(f, 2, IR_LOAD, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        ir_test_instruction(f, 3, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(f, 4, IR_CONST, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(f, 5, IR_ADDRESS, IR_PTR, 1, BUSTER_IR_NONE, 0);
        ir_test_instruction(f, 6, IR_STORE, IR_VOID, 1, 3, BUSTER_IR_NONE);
        ir_test_instruction(f, 7, IR_STORE, IR_VOID, 5, 4, BUSTER_IR_NONE);
        for (uint32_t k = 8; k < load; k += 1)
        {
            ir_test_instruction(f, k, IR_UNDEF, IR_I64, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        ir_test_instruction(f, load, IR_LOAD, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(f, load + 1, IR_STORE, IR_VOID, 2, load, BUSTER_IR_NONE);
        ir_test_instruction(f, load + 2, IR_ADDRESS, IR_PTR, 1, BUSTER_IR_NONE, 2);
        ir_test_instruction(f, load + 3, IR_ADDRESS, IR_PTR, 2, BUSTER_IR_NONE, 3);
        ir_test_instruction(f, load + 4, IR_LOAD, IR_I64, load + 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(f, load + 5, IR_STORE, IR_VOID, load + 3, load + 4, BUSTER_IR_NONE);
        ir_test_instruction(f, load + 6, IR_LOAD, IR_I64, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(f, load + 7, IR_LOAD, IR_I64, load + 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(f, load + 8, IR_XOR, IR_I64, kind == IR_COPY_TEST_EXTRA_USE ? load : load + 6, load + 7, BUSTER_IR_NONE);
        ir_test_instruction(f, load + 9, IR_RET, IR_VOID, load + 8, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t pieces[4] = { load, load + 1, load + 4, load + 5 };
        for (uint32_t k = 0; k < 4; k += 1)
        {
            f->flags[pieces[k]] = kind == IR_COPY_TEST_UNMARKED + k ? 0 : BUSTER_IR_COPY_PIECE;
        }
        if (kind == IR_COPY_TEST_VOLATILE || kind == IR_COPY_TEST_ATOMIC)
        {
            f->flags[load + 4] |= kind == IR_COPY_TEST_VOLATILE ? BUSTER_IR_VOLATILE : BUSTER_IR_ATOMIC;
            f->alignment_log2[load + 4] = 3;
        }
        if (kind == IR_COPY_TEST_INTERVENING || kind == IR_COPY_TEST_BLOCK)
        {
            ir_test_instruction(f, load + 2, kind == IR_COPY_TEST_INTERVENING ? IR_LOAD : IR_BR,
                                kind == IR_COPY_TEST_INTERVENING ? IR_I64 : IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
            f->a[load + 4] = 5;
        }
    }
    return created;
}

static void ir_copy_pair_tests(IrTestTally* tally, Arena* memory, Arena* scratch)
{
    IrFunction empty = { 0 };
    uint32_t pairs = 123;
    const uint64_t scratch_mark = scratch->length;
    ir_test_expect(tally, ir_copy_pair(&empty, scratch, &pairs) == IR_OK && pairs == 0 && scratch->length == scratch_mark, "empty COPY pairing retains no scratch");
    const long page_value = sysconf(_SC_PAGESIZE);
    const uint64_t page = page_value > 0 ? (uint64_t)page_value : 4096;
    uint8_t* const guard = mmap(NULL, 14 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    bool guarded = guard != MAP_FAILED;
    for (uint32_t column = 0; guarded && column < 7; column += 1)
    {
        guarded = mprotect(guard + (2 * column + 1) * page, page, PROT_NONE) == 0;
    }
    ir_test_expect(tally, guarded, "COPY pairing guarded columns allocated");
    for (uint32_t padding = 0; guarded && padding < 32; padding += 1)
    {
        for (uint32_t kind = 0; kind < IR_COPY_TEST_COUNT; kind += 1)
        {
            const uint64_t mark = memory->length;
            IrFunction f = { 0 };
            const bool created = ir_copy_test_function(memory, &f, padding, kind);
            ir_test_expect(tally, created, "COPY pairing fixture allocated");
            if (created)
            {
                void* columns[7] = { f.op, f.type, f.flags, f.alignment_log2, f.a, f.b, f.c };
                for (uint32_t column = 0; column < 7; column += 1)
                {
                    const uint64_t bytes = f.instruction_count * (column < 4 ? 1u : 4u);
                    void* const tail = guard + (2 * column + 1) * page - bytes;
                    memcpy(tail, columns[column], bytes);
                    columns[column] = tail;
                }
                f.op = columns[0];
                f.type = columns[1];
                f.flags = columns[2];
                f.alignment_log2 = columns[3];
                f.a = columns[4];
                f.b = columns[5];
                f.c = columns[6];
                ir_test_expect(tally, ir_verify(&f, scratch, NULL).error == IR_OK, "COPY pairing input verifies including its metadata");
                uint8_t bytes[64];
                uint8_t before_bytes[64];
                memset(bytes, 0x5a, sizeof(bytes));
                const uint64_t pointer = (uint64_t)(uintptr_t)(bytes + 32);
                if (kind == IR_COPY_TEST_OPAQUE)
                {
                    memcpy(bytes, &pointer, sizeof(pointer));
                }
                const IrInterpreterFunction code = { .symbol = 2, .function = &f };
                const InterpreterBinding binding = { .symbol = 1, .bytes = bytes, .size = sizeof(bytes), .writable = true };
                const IrInterpreterProgram program = { .functions = &code, .function_count = 1, .bindings = &binding, .binding_count = 1 };
                const InterpreterValue argument = { .low = (uint64_t)(uintptr_t)bytes };
                InterpreterOptions options = interpreter_default_options();
                options.max_frames = 2;
                options.max_memories = 8;
                const InterpreterResult before = ir_interpret(&program, 2, &argument, 1, &options, scratch);
                memcpy(before_bytes, bytes, sizeof(bytes));
                if (padding == 0 && kind == IR_COPY_TEST_STACK)
                {
                    Arena exhausted = *scratch;
                    exhausted.capacity = exhausted.length;
                    pairs = 123;
                    ir_test_expect(tally, ir_copy_pair(&f, &exhausted, &pairs) == IR_ERROR_MEMORY && pairs == 0 &&
                                   f.type[8] == IR_I64 && f.op[12] == IR_LOAD && exhausted.length == scratch_mark,
                                   "COPY pairing allocation failure preserves columns and scratch");
                }
                const bool expected = kind <= IR_COPY_TEST_DISJOINT;
                const IrError error = ir_copy_pair(&f, scratch, &pairs);
                ir_test_expect(tally, error == IR_OK && pairs == (uint32_t)expected && scratch->length == scratch_mark,
                               "COPY pairing finds only marked single-use adjacent accesses with proved ranges");
                const uint32_t load = 8 + padding;
                ir_test_expect(tally, f.type[load] == (expected ? IR_V4I32 : IR_I64) && f.op[load + 4] == (expected ? IR_UNDEF : IR_LOAD) &&
                               f.op[load + 5] == (expected ? IR_UNDEF : IR_STORE), "COPY pairing produces the exact load/store shape");
                ir_test_expect(tally, ir_verify(&f, scratch, NULL).error == IR_OK, "COPY pairing output verifies");
                memset(bytes, 0x5a, sizeof(bytes));
                if (kind == IR_COPY_TEST_OPAQUE)
                {
                    memcpy(bytes, &pointer, sizeof(pointer));
                }
                const InterpreterResult after = ir_interpret(&program, 2, &argument, 1, &options, scratch);
                const bool equal = before.status == INTERPRETER_OK && after.status == INTERPRETER_OK && before.value.low == after.value.low &&
                                   before.value.high == after.value.high && memcmp(before_bytes, bytes, sizeof(bytes)) == 0;
                if (!equal && tally->failures < 12)
                {
                    fprintf(stderr, "IR: COPY interpretation kind=%u padding=%u before=%u at %u after=%u at %u\n",
                            kind, padding, (unsigned)before.status, before.entry, (unsigned)after.status, after.entry);
                }
                ir_test_expect(tally, equal, "COPY pairing preserves interpreted values, stores and untouched bytes");
                if (kind == IR_COPY_TEST_OVERLAP)
                {
                    ir_test_expect(tally, after.value.low == 0, "overlapping separately marked pieces retain their sequential read-after-write");
                }
                pairs = 123;
                ir_test_expect(tally, ir_copy_pair(&f, scratch, &pairs) == IR_OK && pairs == 0, "COPY pairing is idempotent");
            }
            arena_pop_to(memory, mark);
        }
    }
    if (guard != MAP_FAILED)
    {
        munmap(guard, 14 * page);
    }
    // Bounds, segment accesses and scalar debug identities are independent reasons to leave a plausible window.
    for (uint32_t kind = 0; kind < 8; kind += 1)
    {
        const uint64_t mark = memory->length;
        IrFunction f = { 0 };
        bool created = ir_copy_test_function(memory, &f, 0, IR_COPY_TEST_STACK);
        if (created && kind < 2)
        {
            f.slots[kind].size = 15;
        }
        else if (created && kind == 2)
        {
            f.flags[8] |= BUSTER_IR_SEGMENT_FS << BUSTER_IR_SEGMENT_SHIFT;
        }
        else if (created && kind == 3)
        {
            f.addresses[2].offset = INT64_MAX;
        }
        else if (created)
        {
            const uint8_t flags[1] = { kind == 6 ? BUSTER_IR_DEBUG_BIND_REGIONS : BUSTER_IR_DEBUG_BIND_ENTRY };
            const IrDebugBinding binding = {
                .local = 0,
                .entry = 8,
                .value = kind == 4 ? 8 : BUSTER_IR_NONE,
                .anchor = kind == 5 ? 8 : kind == 7 ? 13 : BUSTER_IR_NONE,
                .kind = kind == 4 ? IR_DEBUG_BIND_VALUE : kind == 7 ? IR_DEBUG_BIND_UNAVAILABLE : IR_DEBUG_BIND_CONSTANT,
                .bytes = kind == 7 ? 0 : 8,
                .type = kind == 7 ? IR_VOID : IR_I64,
            };
            const IrDebugRegion region = {
                .local = 0,
                .block = 0,
                .begin = 8,
                .end = 9,
                .block_end = f.instruction_count,
                .value = 8,
                .next = BUSTER_IR_NONE,
                .kind = IR_DEBUG_BIND_VALUE,
                .bytes = 8,
                .type = IR_I64,
            };
            const IrDebugInfo seed = {
                .entry_count = f.instruction_count,
                .local_count = 1,
                .bindings = (IrDebugBinding*)&binding,
                .binding_count = kind == 6 ? 0 : 1,
                .regions = (IrDebugRegion*)&region,
                .region_count = kind == 6 ? 1 : 0,
                .binding_flags = (uint8_t*)flags,
                .first_block_end = f.instruction_count,
            };
            created = ir_debug_create(memory, &f, &seed);
        }
        ir_test_expect(tally, created, "COPY pairing conservative-boundary fixture allocated");
        if (created)
        {
            ir_test_expect(tally, ir_verify(&f, scratch, NULL).error == IR_OK, "COPY pairing conservative-boundary fixture verifies");
            ir_test_expect(tally, ir_copy_pair(&f, scratch, &pairs) == IR_OK && pairs == 0 && f.type[8] == IR_I64,
                           "COPY pairing preserves short slots, segments and scalar debug bindings");
            ir_test_expect(tally, ir_verify(&f, scratch, NULL).error == IR_OK, "COPY pairing retains valid debug metadata");
        }
        arena_pop_to(memory, mark);
    }
}
