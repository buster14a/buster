// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original field-splitting fixtures. Include after ir_optimize_sroa.c, ir_optimize_test.c and upstream ssa_ir.c.
// Private stores are deliberately excluded from observable traces when comparing a split/promoted function.

static bool ir_sroa_test_fields(Arena* arena, IrFunction* f, uint32_t kind)
{
    const IrSizes sizes = { .instruction_count = 11, .block_count = 1, .constant_count = 1, .parameter_count = 1, .slot_count = 1, .address_count = 2 };
    const bool created = ir_create(arena, &sizes, f) == IR_OK;
    if (created)
    {
        f->result_type = kind == 5 ? IR_PTR : IR_I64;
        f->parameter_types[0] = IR_I64;
        f->constants[0].low = 7;
        f->block_offsets[1] = 11;
        f->slots[0] = (IrSlot){ .size = 16, .alignment = 8, .type = IR_VOID };
        f->addresses[0] = (IrAddress){ .offset = kind == 1 ? 4 : kind == 6 ? 0 : kind == 7 ? 16 : kind == 9 ? -8 : 8, .scale = 1 };
        f->addresses[1] = (IrAddress){ .offset = -8, .scale = 1 };
        ir_optimize_test_instruction(f, 0, IR_PARAM, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 1, IR_CONST, kind == 6 ? IR_I32 : IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 2, IR_STACK_ADDR, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 3, IR_ADDRESS, IR_PTR, 2, kind == 4 ? 0 : BUSTER_IR_NONE, 0);
        ir_optimize_test_instruction(f, 4, kind == 10 ? IR_ADDRESS : IR_COPY, IR_PTR, 3, BUSTER_IR_NONE, kind == 10 ? 1 : BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 5, IR_STORE, IR_VOID, 2, kind == 11 ? 2 : 0, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 6, IR_STORE, IR_VOID, 4, 1, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 7, IR_LOAD, kind == 11 ? IR_PTR : IR_I64, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 8, IR_LOAD, kind == 6 ? IR_I32 : IR_I64, 4, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 9, kind == 6 ? IR_ZEXT : kind == 11 ? IR_PTRTOINT : IR_ADD, IR_I64, kind == 6 ? 8 : 7, kind == 6 || kind == 11 ? BUSTER_IR_NONE : 8, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 10, IR_RET, IR_VOID, kind == 5 ? 2 : 9, BUSTER_IR_NONE, BUSTER_IR_NONE);
        f->flags[6] = kind == 2 ? BUSTER_IR_VOLATILE : 0;
        f->slots[0].flags = kind == 3 ? BUSTER_IR_SLOT_ADDRESS_TAKEN : kind == 8 ? BUSTER_IR_SLOT_VOLATILE : kind == 12 ? BUSTER_IR_SLOT_TEMPORARY : 0;
    }
    return created;
}

static bool ir_sroa_test_cfg(Arena* arena, IrFunction* f, bool loop)
{
    const IrSizes sizes = { .instruction_count = loop ? 20 : 16, .block_count = 4, .constant_count = 3, .parameter_count = 1, .slot_count = 1, .address_count = 1 };
    const bool created = ir_create(arena, &sizes, f) == IR_OK;
    if (created)
    {
        f->result_type = IR_I64;
        f->parameter_types[0] = loop ? IR_I64 : IR_I1;
        f->slots[0] = (IrSlot){ .size = 16, .alignment = 8, .type = IR_VOID };
        f->addresses[0] = (IrAddress){ .offset = 8, .scale = 1 };
        f->constants[0].low = loop ? 0 : 7;
        f->constants[1].low = loop ? 1 : 9;
        f->constants[2].low = 5;
        if (loop)
        {
            const uint32_t offsets[] = { 0, 9, 12, 18, 20 };
            memcpy(f->block_offsets, offsets, sizeof(offsets));
            for (uint32_t i = 0; i < 3; i += 1)
            {
                ir_optimize_test_instruction(f, i, IR_CONST, IR_I64, i, BUSTER_IR_NONE, BUSTER_IR_NONE);
            }
            ir_optimize_test_instruction(f, 3, IR_PARAM, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_optimize_test_instruction(f, 4, IR_STACK_ADDR, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_optimize_test_instruction(f, 5, IR_ADDRESS, IR_PTR, 4, BUSTER_IR_NONE, 0);
            ir_optimize_test_instruction(f, 6, IR_STORE, IR_VOID, 4, 0, BUSTER_IR_NONE);
            ir_optimize_test_instruction(f, 7, IR_STORE, IR_VOID, 5, 0, BUSTER_IR_NONE);
            ir_optimize_test_instruction(f, 8, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_optimize_test_instruction(f, 9, IR_LOAD, IR_I64, 4, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_optimize_test_instruction(f, 10, IR_ICMP, IR_I1, 9, 2, IR_ULT);
            ir_optimize_test_instruction(f, 11, IR_CBR, IR_VOID, 10, 2, 3);
            ir_optimize_test_instruction(f, 12, IR_LOAD, IR_I64, 5, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_optimize_test_instruction(f, 13, IR_ADD, IR_I64, 12, 3, BUSTER_IR_NONE);
            ir_optimize_test_instruction(f, 14, IR_STORE, IR_VOID, 5, 13, BUSTER_IR_NONE);
            ir_optimize_test_instruction(f, 15, IR_ADD, IR_I64, 9, 1, BUSTER_IR_NONE);
            ir_optimize_test_instruction(f, 16, IR_STORE, IR_VOID, 4, 15, BUSTER_IR_NONE);
            ir_optimize_test_instruction(f, 17, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_optimize_test_instruction(f, 18, IR_LOAD, IR_I64, 5, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_optimize_test_instruction(f, 19, IR_RET, IR_VOID, 18, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        else
        {
            const uint32_t offsets[] = { 0, 8, 10, 12, 16 };
            memcpy(f->block_offsets, offsets, sizeof(offsets));
            ir_optimize_test_instruction(f, 0, IR_PARAM, IR_I1, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_optimize_test_instruction(f, 1, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_optimize_test_instruction(f, 2, IR_CONST, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_optimize_test_instruction(f, 3, IR_STACK_ADDR, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_optimize_test_instruction(f, 4, IR_ADDRESS, IR_PTR, 3, BUSTER_IR_NONE, 0);
            ir_optimize_test_instruction(f, 5, IR_STORE, IR_VOID, 3, 1, BUSTER_IR_NONE);
            ir_optimize_test_instruction(f, 6, IR_STORE, IR_VOID, 4, 2, BUSTER_IR_NONE);
            ir_optimize_test_instruction(f, 7, IR_CBR, IR_VOID, 0, 1, 2);
            ir_optimize_test_instruction(f, 8, IR_STORE, IR_VOID, 3, 2, BUSTER_IR_NONE);
            ir_optimize_test_instruction(f, 9, IR_BR, IR_VOID, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_optimize_test_instruction(f, 10, IR_STORE, IR_VOID, 4, 1, BUSTER_IR_NONE);
            ir_optimize_test_instruction(f, 11, IR_BR, IR_VOID, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_optimize_test_instruction(f, 12, IR_LOAD, IR_I64, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_optimize_test_instruction(f, 13, IR_LOAD, IR_I64, 4, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_optimize_test_instruction(f, 14, IR_ADD, IR_I64, 12, 13, BUSTER_IR_NONE);
            ir_optimize_test_instruction(f, 15, IR_RET, IR_VOID, 14, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
    }
    return created;
}

static void ir_sroa_test_compare(IrOptimizeTest* test, const IrFunction* f, Arena* vector, Arena* scalar, Arena* promoted, Arena* scratch, uint32_t expected_fields)
{
    const uint64_t vm = vector->length;
    const uint64_t sm = scalar->length;
    const uint64_t pm = promoted->length;
    const uint64_t tm = scratch->length;
    IrSroaOptions options = ir_sroa_options();
    IrSroaResult a = { 0 };
    IrSroaResult b = { 0 };
    const IrCheck ac = ir_optimize_sroa(f, vector, scratch, options, &a);
    options.scalar = true;
    const IrCheck bc = ir_optimize_sroa(f, scalar, scratch, options, &b);
    ir_optimize_test_expect(test, ac.error == IR_OK && bc.error == IR_OK && scratch->length == tm, "SROA paths accept fixture and restore scratch");
    if (ac.error == IR_OK && bc.error == IR_OK)
    {
        const bool slots_equal = a.function.slot_count == b.function.slot_count && (a.function.slot_count == 0 || memcmp(a.function.slots, b.function.slots, (uint64_t)a.function.slot_count * sizeof(IrSlot)) == 0);
        ir_optimize_test_expect(test, a.fields == expected_fields && b.fields == expected_fields && ir_optimize_test_equal(&a.function, &b.function) && slots_equal, "SROA exact scalar/vector fields, layout and output");
        IrFunction ssa = { 0 };
        const IrCheck sc = ssa_ir_promote(&a.function, promoted, scratch, &ssa);
        ir_optimize_test_expect(test, sc.error == IR_OK, "SROA output accepted by upstream SSA promoter");
        if (expected_fields != 0 && sc.error == IR_OK)
        {
            uint32_t memory = 0;
            for (uint32_t i = 0; i < ssa.instruction_count; i += 1)
            {
                memory += ssa.op[i] == IR_LOAD || ssa.op[i] == IR_STORE || ssa.op[i] == IR_STACK_ADDR;
            }
            ir_optimize_test_expect(test, memory == 0 && ssa.slot_count == 0, "SROA fields promote to SSA with no residual aggregate slots or memory accesses");
        }
        for (uint32_t trial = 0; trial < 18; trial += 1)
        {
            const uint64_t parameter = trial == 17 ? UINT64_MAX : trial;
            const IrOptimizeExecution before = ir_optimize_test_execute(f, scratch, parameter, 0);
            const IrOptimizeExecution split = ir_optimize_test_execute(&a.function, scratch, parameter, 0);
            ir_optimize_test_expect(test, before.status != 4 && before.status == split.status && before.value == split.value, "SROA independent byte-memory interpreter preserves return and fault");
            if (expected_fields == 0)
            {
                ir_optimize_test_expect(test, before.effects == split.effects, "SROA refused slot preserves observable fixture effects");
            }
            if (sc.error == IR_OK)
            {
                const IrOptimizeExecution after = ir_optimize_test_execute(&ssa, scratch, parameter, 0);
                ir_optimize_test_expect(test, before.status == after.status && before.value == after.value, "SROA plus upstream vector SSA preserves independently interpreted value");
            }
        }
        arena_pop_to(promoted, pm);
        const IrCheck scalar_ssa = ssa_ir_promote_scalar(&b.function, promoted, scratch, &ssa);
        ir_optimize_test_expect(test, scalar_ssa.error == IR_OK, "SROA output accepted by independent upstream scalar SSA promoter");
        if (scalar_ssa.error == IR_OK)
        {
            for (uint32_t trial = 0; trial < 9; trial += 1)
            {
                const IrOptimizeExecution before = ir_optimize_test_execute(f, scratch, trial, 0);
                const IrOptimizeExecution after = ir_optimize_test_execute(&ssa, scratch, trial, 0);
                ir_optimize_test_expect(test, before.status == after.status && before.value == after.value, "SROA plus upstream scalar SSA preserves independently interpreted value");
            }
        }
    }
    arena_pop_to(promoted, pm);
    arena_pop_to(scalar, sm);
    arena_pop_to(vector, vm);
}

static void ir_sroa_test_guards(IrOptimizeTest* test)
{
    for (uint32_t count = 0; count <= 16; count += 1)
    {
        for (uint32_t trial = 0; trial < 256; trial += 1)
        {
            IrSroaUseBatch batch = { .count = count };
            uint16_t expected = 0;
            for (uint32_t lane = 0; lane < count; lane += 1)
            {
                batch.op[lane] = ir_optimize_test_random(test) % IR_OP_COUNT;
                batch.position[lane] = ir_optimize_test_random(test) % 4;
                batch.forwarded[lane] = ir_optimize_test_random(test) & 1;
                batch.flags[lane] = ir_optimize_test_random(test) & BUSTER_IR_VOLATILE;
                const bool memory = batch.position[lane] == 0 && (batch.op[lane] == IR_LOAD || batch.op[lane] == IR_STORE);
                const bool forward = batch.position[lane] == 0 && (batch.op[lane] == IR_COPY || batch.op[lane] == IR_BITCAST || batch.op[lane] == IR_ADDRESS) && batch.forwarded[lane] != 0;
                const bool escaped = (!memory && !forward) || (memory && (batch.flags[lane] & BUSTER_IR_VOLATILE) != 0);
                expected |= (uint16_t)escaped << lane;
            }
            ir_optimize_test_expect(test, ir_sroa_escape_mask(&batch) == expected, "SROA exact scalar/vector escape masks for every packet tail");
        }
    }
    const IrSroaUseBatch invalid = { .count = 17 };
    ir_optimize_test_expect(test, ir_sroa_escape_mask(&invalid) == 0, "SROA invalid packet count refuses without column access");
    const long page = sysconf(_SC_PAGESIZE);
    void* mapping = page > 0 ? mmap(0, (uint64_t)page * 8, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    bool ready = mapping != MAP_FAILED;
    for (uint32_t p = 1; ready && p < 8; p += 2)
    {
        ready = mprotect((char*)mapping + p * page, page, PROT_NONE) == 0;
    }
    ir_optimize_test_expect(test, ready, "SROA guarded field column setup");
    for (uint32_t count = 0; ready && count <= 33; count += 1)
    {
        IrSroaWork work = {
            .field_count = count,
            .field_roots = (uint32_t*)((char*)mapping + page - count * 4),
            .field_offsets = (uint64_t*)((char*)mapping + 3 * page - count * 8),
            .field_types = (uint32_t*)((char*)mapping + 5 * page - count * 4),
            .field_sizes = (uint32_t*)((char*)mapping + 7 * page - count * 4),
        };
        for (uint32_t i = 0; i < count; i += 1)
        {
            work.field_roots[i] = ir_optimize_test_random(test) % 3;
            work.field_offsets[i] = ir_optimize_test_random(test) % 128;
            work.field_types[i] = IR_I8 + ir_optimize_test_random(test) % 4;
            work.field_sizes[i] = (ir_type_bits[work.field_types[i]] + 7) / 8;
        }
        for (uint32_t trial = 0; trial < 256; trial += 1)
        {
            const uint32_t root = ir_optimize_test_random(test) % 3;
            const uint64_t offset = ir_optimize_test_random(test) % 128;
            const uint32_t type = IR_I8 + ir_optimize_test_random(test) % 4;
            const uint32_t size = (ir_type_bits[type] + 7) / 8;
            bool scalar_overlap = false;
            bool vector_overlap = false;
            const uint32_t scalar = ir_sroa_field(&work, root, offset, type, size, true, &scalar_overlap);
            const uint32_t vector = ir_sroa_field(&work, root, offset, type, size, false, &vector_overlap);
            ir_optimize_test_expect(test, scalar == vector && scalar_overlap == vector_overlap, "SROA guarded scalar/vector exact field and overlap queries");
        }
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, (uint64_t)page * 8);
    }
}

static void ir_sroa_test_ir(IrOptimizeTest* test)
{
    Arena input = arena_reserve(16ull << 20);
    Arena vector = arena_reserve(16ull << 20);
    Arena scalar = arena_reserve(16ull << 20);
    Arena promoted = arena_reserve(16ull << 20);
    Arena scratch = arena_reserve(32ull << 20);
    const bool ready = input.base != 0 && vector.base != 0 && scalar.base != 0 && promoted.base != 0 && scratch.base != 0;
    ir_optimize_test_expect(test, ready, "SROA fixture arenas");
    if (ready)
    {
        for (uint32_t kind = 0; kind < 13; kind += 1)
        {
            IrFunction f = { 0 };
            const bool created = ir_sroa_test_fields(&input, &f, kind);
            ir_optimize_test_expect(test, created, "SROA field fixture allocation");
            if (created)
            {
                ir_sroa_test_compare(test, &f, &vector, &scalar, &promoted, &scratch, kind == 0 || kind == 12 ? 2 : kind == 10 ? 1 : 0);
                if (kind == 0)
                {
                    const uint64_t sm = scratch.length;
                    for (uint64_t available = 0; available < 2048; available += 23)
                    {
                        Arena tiny = vector;
                        tiny.capacity = available;
                        IrSroaResult result = { .fields = 139 };
                        const IrSroaResult saved = result;
                        const IrCheck check = ir_optimize_sroa(&f, &tiny, &scratch, ir_sroa_options(), &result);
                        if (check.error != IR_OK)
                        {
                            ir_optimize_test_expect(test, check.error == IR_ERROR_MEMORY && tiny.length == 0 && scratch.length == sm && memcmp(&result, &saved, sizeof(result)) == 0, "SROA partial allocation failure rolls back arenas and result");
                        }
                    }
                }
            }
            arena_pop_to(&input, 0);
        }
        for (uint32_t trial = 0; trial < 128; trial += 1)
        {
            IrFunction f = { 0 };
            const bool created = ir_sroa_test_fields(&input, &f, 0);
            ir_optimize_test_expect(test, created, "SROA randomized field allocation");
            if (created)
            {
                f.constants[0].low = ir_optimize_test_random(test);
                f.addresses[0].offset = 8 + ir_optimize_test_random(test) % 41;
                f.slots[0].size = f.addresses[0].offset + 8;
                ir_sroa_test_compare(test, &f, &vector, &scalar, &promoted, &scratch, 2);
            }
            arena_pop_to(&input, 0);
        }
        for (uint32_t loop = 0; loop < 2; loop += 1)
        {
            IrFunction f = { 0 };
            const bool created = ir_sroa_test_cfg(&input, &f, loop != 0);
            ir_optimize_test_expect(test, created, "SROA control-flow field fixture allocation");
            if (created)
            {
                ir_sroa_test_compare(test, &f, &vector, &scalar, &promoted, &scratch, 2);
            }
            arena_pop_to(&input, 0);
        }
    }
    arena_release(&scratch);
    arena_release(&promoted);
    arena_release(&scalar);
    arena_release(&vector);
    arena_release(&input);
}

static __attribute__((unused)) int run_ir_sroa_tests(int argc, char** argv)
{
    IrOptimizeTest test = { .random = 139 };
    bool valid = true;
    for (int i = 1; i < argc; i += 1)
    {
        if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
        {
            char* end = 0;
            errno = 0;
            test.random = strtoull(argv[++i], &end, 0);
            valid = errno == 0 && *end == 0 && test.random != 0;
        }
        else
        {
            valid = false;
        }
    }
    int result = 2;
    if (valid)
    {
        ir_sroa_test_guards(&test);
        ir_sroa_test_ir(&test);
        printf("ir-sroa: %" PRIu64 " checks, %" PRIu64 " failures\n", test.checks, test.failures);
        result = test.failures == 0 ? 0 : 1;
    }
    else
    {
        fprintf(stderr, "usage: test-ir-sroa [--seed N]\n");
    }
    return result;
}
