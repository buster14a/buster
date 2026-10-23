// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original motion fixtures. Include after ir_optimize_licm.c and ir_optimize_test.c.

static bool ir_licm_test_loop(Arena* arena, IrFunction* f, uint32_t kind)
{
    const IrSizes sizes = { .instruction_count = 16, .block_count = 4, .constant_count = 3, .parameter_count = 1, .extra_count = 8 };
    const bool created = ir_create(arena, &sizes, f) == IR_OK;
    if (created)
    {
        f->result_type = IR_I64;
        f->parameter_types[0] = kind == 3 ? IR_PTR : IR_I64;
        f->constants[0].low = 0;
        f->constants[1].low = 1;
        f->constants[2].low = kind == 2 || kind == 3 ? 0 : 5;
        f->block_offsets[1] = 5;
        f->block_offsets[2] = 9;
        f->block_offsets[3] = 15;
        f->block_offsets[4] = 16;
        ir_optimize_test_instruction(f, 0, kind == 1 ? IR_UNDEF : IR_PARAM, f->parameter_types[0], kind == 1 ? BUSTER_IR_NONE : 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 1, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 2, IR_CONST, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 3, IR_CONST, IR_I64, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 4, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 5, IR_PHI, IR_I64, 0, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 6, IR_PHI, IR_I64, 4, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 7, IR_ICMP, IR_I1, 5, 3, IR_ULT);
        ir_optimize_test_instruction(f, 8, IR_CBR, IR_VOID, 7, 2, 3);
        ir_optimize_test_instruction(f, 9, kind == 2 ? IR_UDIV : kind == 3 ? IR_LOAD : IR_MUL, IR_I64, 0, kind == 3 ? BUSTER_IR_NONE : 3, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 10, IR_ADD, IR_I64, 9, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 11, IR_ADD, IR_I64, 6, 10, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 12, IR_ADD, IR_I64, 5, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 13, IR_COPY, IR_I64, 11, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 14, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 15, IR_RET, IR_VOID, 6, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t extra[] = { 0, 1, 2, 12, 0, 1, 2, 13 };
        memcpy(f->extra, extra, sizeof(extra));
    }
    return created;
}

static bool ir_licm_test_nested(Arena* arena, IrFunction* f)
{
    const IrSizes sizes = { .instruction_count = 19, .block_count = 7, .constant_count = 3, .parameter_count = 1, .extra_count = 8 };
    const bool created = ir_create(arena, &sizes, f) == IR_OK;
    if (created)
    {
        f->result_type = IR_I64;
        f->parameter_types[0] = IR_I64;
        f->constants[0].low = 0;
        f->constants[1].low = 1;
        f->constants[2].low = 2;
        const uint32_t offsets[] = { 0, 5, 8, 9, 12, 16, 18, 19 };
        memcpy(f->block_offsets, offsets, sizeof(offsets));
        ir_optimize_test_instruction(f, 0, IR_PARAM, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 1, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 2, IR_CONST, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 3, IR_CONST, IR_I64, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 4, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 5, IR_PHI, IR_I64, 0, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 6, IR_ICMP, IR_I1, 5, 3, IR_ULT);
        ir_optimize_test_instruction(f, 7, IR_CBR, IR_VOID, 6, 2, 6);
        ir_optimize_test_instruction(f, 8, IR_BR, IR_VOID, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 9, IR_PHI, IR_I64, 4, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 10, IR_ICMP, IR_I1, 9, 3, IR_ULT);
        ir_optimize_test_instruction(f, 11, IR_CBR, IR_VOID, 10, 4, 5);
        ir_optimize_test_instruction(f, 12, IR_MUL, IR_I64, 0, 3, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 13, IR_ADD, IR_I64, 12, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 14, IR_ADD, IR_I64, 9, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 15, IR_BR, IR_VOID, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 16, IR_ADD, IR_I64, 5, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 17, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 18, IR_RET, IR_VOID, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t extra[] = { 0, 1, 5, 16, 2, 1, 4, 14 };
        memcpy(f->extra, extra, sizeof(extra));
    }
    return created;
}

static bool ir_licm_test_chain(Arena* arena, IrFunction* f, IrOptimizeTest* test, uint32_t length)
{
    const IrSizes sizes = { .instruction_count = 13 + length, .block_count = 4, .constant_count = 3, .parameter_count = 1, .extra_count = 8 };
    const bool created = ir_create(arena, &sizes, f) == IR_OK;
    if (created)
    {
        f->result_type = IR_I64;
        f->parameter_types[0] = IR_I64;
        f->constants[0].low = 0;
        f->constants[1].low = 1;
        f->constants[2].low = 1 + ir_optimize_test_random(test) % 5;
        f->block_offsets[1] = 5;
        f->block_offsets[2] = 9;
        f->block_offsets[3] = 12 + length;
        f->block_offsets[4] = 13 + length;
        ir_optimize_test_instruction(f, 0, IR_PARAM, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        for (uint32_t i = 1; i < 4; i += 1)
        {
            ir_optimize_test_instruction(f, i, IR_CONST, IR_I64, i - 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        ir_optimize_test_instruction(f, 4, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 5, IR_PHI, IR_I64, 0, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 6, IR_PHI, IR_I64, 4, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 7, IR_ICMP, IR_I1, 5, 3, IR_ULT);
        ir_optimize_test_instruction(f, 8, IR_CBR, IR_VOID, 7, 2, 3);
        const uint8_t operations[] = { IR_ADD, IR_SUB, IR_MUL, IR_AND, IR_OR, IR_XOR };
        for (uint32_t i = 0; i < length; i += 1)
        {
            ir_optimize_test_instruction(f, 9 + i, operations[ir_optimize_test_random(test) % 6], IR_I64, i == 0 ? 0 : 8 + i, 2 + ir_optimize_test_random(test) % 2, BUSTER_IR_NONE);
        }
        ir_optimize_test_instruction(f, 9 + length, IR_ADD, IR_I64, 6, length == 0 ? 0 : 8 + length, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 10 + length, IR_ADD, IR_I64, 5, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 11 + length, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 12 + length, IR_RET, IR_VOID, 6, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t extra[] = { 0, 1, 2, 10 + length, 0, 1, 2, 9 + length };
        memcpy(f->extra, extra, sizeof(extra));
    }
    return created;
}

static void ir_licm_test_compare(IrOptimizeTest* test, const IrFunction* f, Arena* vector, Arena* scalar, Arena* scratch, uint32_t expected_moved)
{
    const uint64_t vm = vector->length;
    const uint64_t sm = scalar->length;
    const uint64_t tm = scratch->length;
    IrLicmOptions options = ir_licm_options();
    IrLicmResult a = { 0 };
    IrLicmResult b = { 0 };
    const IrCheck ac = ir_optimize_licm(f, vector, scratch, options, &a);
    options.scalar = true;
    const IrCheck bc = ir_optimize_licm(f, scalar, scratch, options, &b);
    ir_optimize_test_expect(test, ac.error == IR_OK && bc.error == IR_OK && scratch->length == tm, "LICM vector and scalar accept fixture and restore scratch");
    if (ac.error == IR_OK && bc.error == IR_OK)
    {
        ir_optimize_test_expect(test, a.moved == expected_moved && b.moved == a.moved && ir_optimize_test_equal(&a.function, &b.function), "LICM exact vector/worklist output and expected motion count");
        for (uint32_t trial = 0; trial < 18; trial += 1)
        {
            const uint64_t parameter = trial == 17 ? UINT64_MAX : trial;
            const IrOptimizeExecution before = ir_optimize_test_execute(f, scratch, parameter, trial & 1);
            const IrOptimizeExecution after = ir_optimize_test_execute(&a.function, scratch, parameter, trial & 1);
            ir_optimize_test_expect(test, before.status != 4 && before.status == after.status && before.value == after.value && before.effects == after.effects, "LICM independently interpreted returns, faults and effects unchanged");
        }
        arena_pop_to(scalar, sm);
        options.scalar = false;
        options.sweep_limit = 0;
        IrLicmResult fallback = { 0 };
        const IrCheck fc = ir_optimize_licm(f, scalar, scratch, options, &fallback);
        ir_optimize_test_expect(test, fc.error == IR_OK && ir_optimize_test_equal(&a.function, &fallback.function), "LICM forced worklist completion is exact");
    }
    arena_pop_to(scalar, sm);
    arena_pop_to(vector, vm);
}

static void ir_licm_test_guards(IrOptimizeTest* test)
{
    const long page = sysconf(_SC_PAGESIZE);
    void* mapping = page > 0 ? mmap(0, (uint64_t)page * 6, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    bool ready = mapping != MAP_FAILED;
    for (uint32_t p = 1; ready && p < 6; p += 2)
    {
        ready = mprotect((char*)mapping + p * page, page, PROT_NONE) == 0;
    }
    ir_optimize_test_expect(test, ready, "LICM guarded readiness columns setup");
    uint32_t operands[33] = { 0 };
    uint32_t reference[33] = { 0 };
    uint32_t offsets[34] = { 0 };
    uint32_t queue[33] = { 0 };
    uint8_t queued[33] = { 0 };
    for (uint32_t count = 0; ready && count <= 33; count += 1)
    {
        IrFunction f = {
            .instruction_count = count,
            .op = (uint8_t*)((char*)mapping + page - count),
            .a = operands,
            .b = operands,
            .c = operands,
        };
        IrLicmWork work = {
            .ir = { .function = &f, .uses = { .offsets = offsets }, .queue = queue, .queued = queued },
            .candidate = (uint8_t*)((char*)mapping + 3 * page - count),
            .available = reference,
        };
        for (uint32_t i = 0; i < count; i += 1)
        {
            f.op[i] = i == 0 ? IR_PARAM : IR_ADD;
            operands[i] = i == 0 ? 0 : i - 1;
            work.candidate[i] = 1;
            reference[i] = 0;
        }
        ir_licm_worklist(&work);
        work.available = (uint32_t*)((char*)mapping + 5 * page - count * sizeof(uint32_t));
        memset(work.available, 0, count * sizeof(uint32_t));
        bool changed = true;
        for (uint32_t round = 0; changed && round <= count; round += 1)
        {
            changed = ir_licm_sweep(&work);
        }
        ir_optimize_test_expect(test, !changed && memcmp(reference, work.available, count * sizeof(uint32_t)) == 0, "LICM guarded dependency chain matches worklist at every tail");
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, (uint64_t)page * 6);
    }
}

static void ir_licm_test_ir(IrOptimizeTest* test)
{
    Arena input = arena_reserve(16ull << 20);
    Arena vector = arena_reserve(16ull << 20);
    Arena scalar = arena_reserve(16ull << 20);
    Arena scratch = arena_reserve(32ull << 20);
    const bool ready = input.base != 0 && vector.base != 0 && scalar.base != 0 && scratch.base != 0;
    ir_optimize_test_expect(test, ready, "LICM fixture arenas");
    if (ready)
    {
        for (uint32_t kind = 0; kind < 4; kind += 1)
        {
            IrFunction f = { 0 };
            const bool created = ir_licm_test_loop(&input, &f, kind);
            ir_optimize_test_expect(test, created, "LICM loop fixture allocation");
            if (created)
            {
                ir_licm_test_compare(test, &f, &vector, &scalar, &scratch, kind == 0 ? 2 : 0);
                if (kind == 0)
                {
                    IrLicmResult result = { 0 };
                    const IrCheck check = ir_optimize_licm(&f, &vector, &scratch, ir_licm_options(), &result);
                    ir_optimize_test_expect(test, check.error == IR_OK && result.function.block_offsets[1] == 7 && result.function.op[4] == IR_MUL && result.function.op[5] == IR_ADD, "LICM appends invariant chain before preheader terminator in dependency order");
                    arena_pop_to(&vector, 0);
                    // No dedicated preheader: the former predecessor also exits directly.
                    f.op[0] = IR_PARAM;
                    f.type[0] = IR_I1;
                    f.parameter_types[0] = IR_I1;
                    f.op[9] = IR_MUL;
                    f.a[9] = 2;
                    f.op[4] = IR_CBR;
                    f.a[4] = 0;
                    f.b[4] = 1;
                    f.c[4] = 3;
                    // The exit now has a bypass path; return an entry value instead of the loop phi.
                    f.a[15] = 1;
                    ir_licm_test_compare(test, &f, &vector, &scalar, &scratch, 0);
                }
            }
            arena_pop_to(&input, 0);
        }
        IrFunction nested = { 0 };
        const bool nested_created = ir_licm_test_nested(&input, &nested);
        ir_optimize_test_expect(test, nested_created, "LICM nested loop allocation");
        if (nested_created)
        {
            ir_licm_test_compare(test, &nested, &vector, &scalar, &scratch, 2);
            IrLicmResult result = { 0 };
            const IrCheck check = ir_optimize_licm(&nested, &vector, &scratch, ir_licm_options(), &result);
            ir_optimize_test_expect(test, check.error == IR_OK && result.eligible_loops == 2 && result.function.block_offsets[1] == 7, "LICM inner invariants can move again through outer preheader");
            arena_pop_to(&vector, 0);
            const uint64_t sm = scratch.length;
            for (uint64_t available = 0; available < 2048; available += 31)
            {
                Arena tiny = vector;
                tiny.capacity = available;
                result = (IrLicmResult){ .moved = 139 };
                const IrLicmResult saved = result;
                const IrCheck failure = ir_optimize_licm(&nested, &tiny, &scratch, ir_licm_options(), &result);
                if (failure.error != IR_OK)
                {
                    ir_optimize_test_expect(test, failure.error == IR_ERROR_MEMORY && tiny.length == 0 && scratch.length == sm && memcmp(&result, &saved, sizeof(result)) == 0, "LICM partial output allocation failure preserves arenas and result");
                }
            }
        }
        arena_pop_to(&input, 0);
        for (uint32_t length = 0; length < 66; length += 1)
        {
            IrFunction f = { 0 };
            const bool created = ir_optimize_test_random_dag(&input, &f, test, length);
            ir_optimize_test_expect(test, created, "LICM no-loop random fixture allocation");
            if (created)
            {
                ir_licm_test_compare(test, &f, &vector, &scalar, &scratch, 0);
            }
            arena_pop_to(&input, 0);
            const bool chain_created = ir_licm_test_chain(&input, &f, test, length);
            ir_optimize_test_expect(test, chain_created, "LICM random loop invariant chain allocation");
            if (chain_created)
            {
                ir_licm_test_compare(test, &f, &vector, &scalar, &scratch, length);
            }
            arena_pop_to(&input, 0);
        }
        IrFunction empty = { 0 };
        const IrSizes sizes = { 0 };
        const bool empty_created = ir_create(&input, &sizes, &empty) == IR_OK;
        IrLicmResult result = { 0 };
        const IrCheck check = empty_created ? ir_optimize_licm(&empty, &vector, &scratch, ir_licm_options(), &result) : ir_check(IR_ERROR_MEMORY, 0, 0);
        ir_optimize_test_expect(test, check.error == IR_OK && result.moved == 0 && result.function.instruction_count == 0, "LICM empty function");
    }
    arena_release(&scratch);
    arena_release(&scalar);
    arena_release(&vector);
    arena_release(&input);
}

static __attribute__((unused)) int run_ir_licm_tests(int argc, char** argv)
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
        ir_licm_test_guards(&test);
        ir_licm_test_ir(&test);
        printf("ir-licm: %" PRIu64 " checks, %" PRIu64 " failures\n", test.checks, test.failures);
        result = test.failures == 0 ? 0 : 1;
    }
    else
    {
        fprintf(stderr, "usage: test-ir-licm [--seed N]\n");
    }
    return result;
}
