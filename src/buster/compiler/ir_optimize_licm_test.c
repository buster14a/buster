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

// Folds a result of LICM's per-loop walk into test-ir-licm's digest (IrOptimizeTest.licm_digest): the moved function and
// the counts the motion reports.
static void ir_licm_test_fold(IrOptimizeTest* test, const IrLicmResult* result)
{
    ir_optimize_test_fold_function(&test->licm_digest, &result->function);
    ir_optimize_test_fold(&test->licm_digest, result->moved);
    ir_optimize_test_fold(&test->licm_digest, result->eligible_loops);
    ir_optimize_test_fold(&test->licm_digest, result->skipped_loops);
    ir_optimize_test_fold(&test->licm_digest, result->called_loops);
}

// LICM on a fixture: the per-loop walk moves the expected count and keeps the input's returns, faults and effects; the
// vector-only policy, on these fixtures without a vector or mask value, skips every loop's walk and returns the input
// unwritten. Both results go into the digest.
static void ir_licm_test_fixture(IrOptimizeTest* test, const IrFunction* f, Arena* output, Arena* scratch, uint32_t expected_moved)
{
    const uint64_t om = output->length;
    const uint64_t tm = scratch->length;
    IrLicmResult a = { 0 };
    const IrCheck ac = ir_optimize_licm(f, output, scratch, ir_licm_options(), &a);
    ir_optimize_test_expect(test, ac.error == IR_OK && scratch->length == tm, "LICM's walk accepts the fixture and restores scratch");
    if (ac.error == IR_OK)
    {
        ir_optimize_test_expect(test, a.moved == expected_moved, "LICM's per-loop walk moves the expected count");
        ir_licm_test_fold(test, &a);
        for (uint32_t trial = 0; trial < 18; trial += 1)
        {
            const uint64_t parameter = trial == 17 ? UINT64_MAX : trial;
            const IrOptimizeExecution before = ir_optimize_test_execute(f, scratch, parameter, trial & 1);
            const IrOptimizeExecution after = ir_optimize_test_execute(&a.function, scratch, parameter, trial & 1);
            ir_optimize_test_expect(test, before.status != 4 && before.status == after.status && before.value == after.value && before.effects == after.effects, "LICM independently interpreted returns, faults and effects unchanged");
        }
    }
    // The scalar fixtures have no vector or mask value: the restricted policy skips every loop's instruction walk and
    // returns their input without writing a replacement.
    const uint64_t mark = output->length;
    IrLicmOptions restricted_options = ir_licm_options();
    restricted_options.vectors = true;
    IrLicmResult restricted = { 0 };
    const IrCheck rc = ir_optimize_licm(f, output, scratch, restricted_options, &restricted);
    ir_optimize_test_expect(test, rc.error == IR_OK && restricted.moved == 0 && restricted.visits == 0 && output->length == mark &&
                                ir_optimize_test_equal(f, &restricted.function), "LICM's vector-only policy skips loops without vector values and writes nothing");
    if (rc.error == IR_OK)
    {
        ir_licm_test_fold(test, &restricted);
    }
    arena_pop_to(output, om);
}

// IrLicmOptions.pressure on ir_licm_test_chain's loop (#2719): its header's two phis and the parameter live through it,
// so BUSTER_IR_LICM_REGISTERS less three of the chain's costly values move, the first ones in order, and the rest stay;
// a loop where nothing moves returns its input without writing a replacement. Every result keeps the input's returns,
// faults and effects.
static void ir_licm_test_pressure_chain(IrOptimizeTest* test, const IrFunction* f, Arena* output, Arena* scratch, uint32_t length)
{
    const uint64_t om = output->length;
    const uint64_t tm = scratch->length;
    IrLicmOptions options = ir_licm_options();
    options.pressure = true;
    IrLicmResult result = { 0 };
    const IrCheck check = ir_optimize_licm(f, output, scratch, options, &result);
    const uint32_t budget = BUSTER_IR_LICM_REGISTERS - 3;
    const uint32_t expected = length < budget ? length : budget;
    ir_optimize_test_expect(test, check.error == IR_OK && scratch->length == tm && result.moved == expected && result.called_loops == 0,
                            "LICM's pressure policy moves the chain's first values while its budget lasts");
    ir_optimize_test_expect(test, check.error != IR_OK || expected != 0 || (output->length == om && ir_optimize_test_equal(f, &result.function)),
                            "LICM's pressure policy returns a loop that moves nothing as it was, unwritten");
    if (check.error == IR_OK)
    {
        ir_licm_test_fold(test, &result);
    }
    for (uint32_t trial = 0; check.error == IR_OK && trial < 6; trial += 1)
    {
        const uint64_t parameter = trial == 5 ? UINT64_MAX : trial;
        const IrOptimizeExecution before = ir_optimize_test_execute(f, scratch, parameter, trial & 1);
        const IrOptimizeExecution after = ir_optimize_test_execute(&result.function, scratch, parameter, trial & 1);
        ir_optimize_test_expect(test, before.status == after.status && before.value == after.value && before.effects == after.effects, "LICM's pressure policy keeps returns, faults and effects");
    }
    arena_pop_to(output, om);
}

// ir_licm_classify's masked loads and stores at every tail (#2924): the operation, type, flag and discriminator columns
// and the taint, safety and free-kind bytes each end where an inaccessible page begins, for every count from 0 to 33.
// Each value's safety and free kind are their definitions': an addition is safe arithmetic, a division isn't safe, a
// constant is safe and free, a splat is safe, and a tainted value isn't safe.
static void ir_licm_test_classify_guards(IrOptimizeTest* test)
{
    const long page = sysconf(_SC_PAGESIZE);
    void* mapping = page > 0 ? mmap(0, (uint64_t)page * 14, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    bool ready = mapping != MAP_FAILED;
    for (uint32_t p = 1; ready && p < 14; p += 2)
    {
        ready = mprotect((char*)mapping + p * page, page, PROT_NONE) == 0;
    }
    ir_optimize_test_expect(test, ready, "LICM guarded classification columns setup");
    uint32_t blocks[33] = { 0 };
    uint32_t destination[33] = { 0 };
    for (uint32_t count = 0; ready && count <= 33; count += 1)
    {
        // The k-th column ends at the k-th inaccessible page.
        char* ends[7];
        for (uint32_t k = 0; k < 7; k += 1)
        {
            ends[k] = (char*)mapping + (2 * k + 1) * page;
        }
        IrFunction f = {
            .instruction_count = count,
            .op = (uint8_t*)(ends[0] - count),
            .type = (uint8_t*)(ends[1] - count),
            .flags = (uint8_t*)(ends[2] - count),
            .c = (uint32_t*)(ends[3] - count * sizeof(uint32_t)),
        };
        IrLicmWork work = {
            .ir = { .function = &f, .cfg = { .instruction_blocks = blocks } },
            .destination = destination,
            .tainted = (uint8_t*)(ends[4] - count),
            .safe = (uint8_t*)(ends[5] - count),
            .free_kinds = (uint8_t*)(ends[6] - count),
        };
        for (uint32_t i = 0; i < count; i += 1)
        {
            static const uint8_t operations[4] = { IR_ADD, IR_UDIV, IR_CONST, IR_INTRINSIC };
            f.op[i] = operations[i % 4];
            f.type[i] = i % 4 == 3 ? IR_V4I32 : IR_I64;
            f.flags[i] = 0;
            f.c[i] = i % 4 == 3 ? BUSTER_IR_INTRINSIC_SPLAT : 0;
            work.tainted[i] = i % 5 == 4;
            work.safe[i] = 7;
            work.free_kinds[i] = 7;
            blocks[i] = i;
        }
        ir_licm_classify(&work);
        bool same = true;
        for (uint32_t i = 0; i < count; i += 1)
        {
            static const uint8_t free_kinds[4] = { BUSTER_IR_LICM_FREE_ARITHMETIC, 0, BUSTER_IR_LICM_FREE, 0 };
            const uint8_t safe = i % 4 != 1 && i % 5 != 4;
            same = same && work.safe[i] == safe && work.free_kinds[i] == free_kinds[i % 4] && destination[i] == i;
        }
        ir_optimize_test_expect(test, same, "LICM's classification at every tail matches its definition");
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, (uint64_t)page * 14);
    }
}

static void ir_licm_test_ir(IrOptimizeTest* test)
{
    Arena input = arena_reserve(16ull << 20);
    Arena vector = arena_reserve(16ull << 20);
    Arena scratch = arena_reserve(32ull << 20);
    const bool ready = input.base != 0 && vector.base != 0 && scratch.base != 0;
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
                ir_licm_test_fixture(test, &f, &vector, &scratch, kind == 0 ? 2 : 0);
                if (kind == 0)
                {
                    IrLicmResult result = { 0 };
                    const IrCheck check = ir_optimize_licm(&f, &vector, &scratch, ir_licm_options(), &result);
                    ir_optimize_test_expect(test, check.error == IR_OK && result.function.block_offsets[1] == 7 && result.function.op[4] == IR_MUL && result.function.op[5] == IR_ADD, "LICM appends invariant chain before preheader terminator in dependency order");
                    if (check.error == IR_OK)
                    {
                        ir_licm_test_fold(test, &result);
                    }
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
                    ir_licm_test_fixture(test, &f, &vector, &scratch, 0);
                }
            }
            arena_pop_to(&input, 0);
        }
        IrFunction nested = { 0 };
        const bool nested_created = ir_licm_test_nested(&input, &nested);
        ir_optimize_test_expect(test, nested_created, "LICM nested loop allocation");
        if (nested_created)
        {
            ir_licm_test_fixture(test, &nested, &vector, &scratch, 2);
            IrLicmResult result = { 0 };
            const IrCheck check = ir_optimize_licm(&nested, &vector, &scratch, ir_licm_options(), &result);
            ir_optimize_test_expect(test, check.error == IR_OK && result.eligible_loops == 2 && result.function.block_offsets[1] == 7, "LICM inner invariants can move again through outer preheader");
            if (check.error == IR_OK)
            {
                ir_licm_test_fold(test, &result);
            }
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
                ir_licm_test_fixture(test, &f, &vector, &scratch, 0);
            }
            arena_pop_to(&input, 0);
            const bool chain_created = ir_licm_test_chain(&input, &f, test, length);
            ir_optimize_test_expect(test, chain_created, "LICM random loop invariant chain allocation");
            if (chain_created)
            {
                ir_licm_test_fixture(test, &f, &vector, &scratch, length);
                ir_licm_test_pressure_chain(test, &f, &vector, &scratch, length);
            }
            arena_pop_to(&input, 0);
        }
        IrFunction empty = { 0 };
        const IrSizes sizes = { 0 };
        const bool empty_created = ir_create(&input, &sizes, &empty) == IR_OK;
        IrLicmResult result = { 0 };
        const IrCheck check = empty_created ? ir_optimize_licm(&empty, &vector, &scratch, ir_licm_options(), &result) : ir_check(IR_ERROR_MEMORY, 0, 0);
        ir_optimize_test_expect(test, check.error == IR_OK && result.moved == 0 && result.function.instruction_count == 0, "LICM empty function");
        if (check.error == IR_OK)
        {
            ir_licm_test_fold(test, &result);
        }
    }
    arena_release(&scratch);
    arena_release(&vector);
    arena_release(&input);
}

// The walk of long loops (#2979): independent values, a chain, an independent half before a chain, and a chain whose
// root reads the loop's phi, at 255, 256, 257 and 4,096 values. Each loop's invariant values all move (the variant
// root's chain none), each of its instructions is visited once, and the input's returns, faults and effects stay.
static void ir_licm_test_progress(IrOptimizeTest* test)
{
    Arena input = arena_reserve(16ull << 20);
    Arena output = arena_reserve(16ull << 20);
    Arena scratch = arena_reserve(32ull << 20);
    const bool ready = input.base != 0 && output.base != 0 && scratch.base != 0;
    ir_optimize_test_expect(test, ready, "LICM progress fixture arenas");
    const uint32_t lengths[] = {
        255,
        256,
        257,
        4096,
    };
    for (uint32_t size = 0; ready && size < sizeof(lengths) / sizeof(lengths[0]); size += 1)
    {
        const uint32_t length = lengths[size];
        for (uint32_t shape = 0; shape < 4; shape += 1)
        {
            IrFunction f = { 0 };
            const bool created = ir_licm_test_chain(&input, &f, test, length);
            ir_optimize_test_expect(test, created, "LICM long readiness fixture allocation");
            if (created)
            {
                for (uint32_t i = 0; i < length; i += 1)
                {
                    // Independent values, an independent half followed by a chain, or a loop-variant root.
                    if (shape == 1 || (shape == 2 && i < length / 2))
                    {
                        f.a[9 + i] = 0;
                    }
                }
                if (shape == 3)
                {
                    f.a[9] = 5;
                }
                IrLicmResult walked = { 0 };
                const IrCheck wc = ir_optimize_licm(&f, &output, &scratch, ir_licm_options(), &walked);
                const uint32_t moved = shape == 3 ? 0 : length;
                ir_optimize_test_expect(test, wc.error == IR_OK && scratch.length == 0 && walked.moved == moved && walked.visits <= f.instruction_count,
                                        "LICM's per-loop walk moves a long loop's invariant values and visits each of its instructions once");
                if (wc.error == IR_OK)
                {
                    ir_licm_test_fold(test, &walked);
                    for (uint32_t trial = 0; trial < 3; trial += 1)
                    {
                        const uint64_t parameter = trial == 2 ? UINT64_MAX : trial * 7;
                        const IrOptimizeExecution before = ir_optimize_test_execute(&f, &scratch, parameter, trial & 1);
                        const IrOptimizeExecution after = ir_optimize_test_execute(&walked.function, &scratch, parameter, trial & 1);
                        ir_optimize_test_expect(test, before.status != 4 && before.status == after.status && before.value == after.value && before.effects == after.effects,
                                                "LICM keeps a long loop's returns, faults and effects");
                    }
                }
            }
            arena_pop_to(&scratch, 0);
            arena_pop_to(&output, 0);
            arena_pop_to(&input, 0);
        }
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&input);
}

// IrLicmOptions.vectors' walk (#2781): it moves exactly the expected vector values and their operands, and keeps the
// interpreter's SIMD behavior. Its result goes into the digest.
static void ir_licm_test_vectors(IrOptimizeTest* test, const IrFunction* f, Arena* output, Arena* scratch, uint32_t expected_moved)
{
    IrLicmOptions options = ir_licm_options();
    options.vectors = true;
    IrLicmResult result = { 0 };
    const IrCheck check = ir_optimize_licm(f, output, scratch, options, &result);
    ir_optimize_test_expect(test, check.error == IR_OK && result.moved == expected_moved, "LICM's vector-only walk moves exactly the vector values and their operands");
    if (check.error == IR_OK)
    {
        ir_optimize_test_simd_semantics(test, f, &result.function, scratch);
        ir_licm_test_fold(test, &result);
    }
}

// A vector operation of an inner loop can move to its preheader and then the outer loop's. The scalar operations beside
// it stay where they are under IrLicmOptions.vectors; the final rewrite counts the vector's one destination change.
static void ir_licm_test_vector_nested(IrOptimizeTest* test)
{
    static const char text[] = "function (i32) -> m16 {\n"
                               "b0:\n"
                               "  %0 = param.i32 0\n"
                               "  %1 = const.i32 0x0\n"
                               "  %2 = const.i32 0x1\n"
                               "  %3 = const.i32 0x2\n"
                               "  %4 = intrinsic.v4i32 %0 splat\n"
                               "  %5 = br.void b1\n"
                               "b1:\n"
                               "  %6 = phi.i32 [%1, b0], [%18, b5]\n"
                               "  %7 = icmp.i1 %6, %3 lt\n"
                               "  %8 = cbr.void %7, b2, b6\n"
                               "b2:\n"
                               "  %9 = br.void b3\n"
                               "b3:\n"
                               "  %10 = phi.i32 [%1, b2], [%16, b4]\n"
                               "  %11 = icmp.i1 %10, %3 lt\n"
                               "  %12 = cbr.void %11, b4, b5\n"
                               "b4:\n"
                               "  %13 = mul.i32 %0, %3\n"
                               "  %14 = add.v4i32 %4, %4\n"
                               "  %15 = add.i32 %13, %2\n"
                               "  %16 = add.i32 %10, %2\n"
                               "  %17 = br.void b3\n"
                               "b5:\n"
                               "  %18 = add.i32 %6, %2\n"
                               "  %19 = br.void b1\n"
                               "b6:\n"
                               "  %20 = vcmp.m16 %4, %4 eq lanes=i8\n"
                               "  %21 = ret.void %20\n"
                               "}\n";
    Arena input = arena_reserve(UINT64_C(16) << 20);
    Arena vector = arena_reserve(UINT64_C(16) << 20);
    Arena scratch = arena_reserve(UINT64_C(16) << 20);
    IrFunction f = { 0 };
    const bool ready = input.base != NULL && vector.base != NULL && scratch.base != NULL;
    const IrParseResult parsed = ready ? ir_parse((String8){ .pointer = (char*)text, .length = sizeof(text) - 1 }, &input, &scratch, &f) : (IrParseResult){ .error = IR_PARSE_END };
    const bool fixture = parsed.error == IR_PARSE_OK && parsed.check.error == IR_OK;
    ir_optimize_test_expect(test, fixture, "LICM's nested vector-only fixture parses");
    if (fixture)
    {
        ir_licm_test_vectors(test, &f, &vector, &scratch, 1);
    }
    arena_release(&input);
    arena_release(&vector);
    arena_release(&scratch);
}

static void ir_licm_test_simd(IrOptimizeTest* test)
{
    Arena input = arena_reserve(UINT64_C(16) << 20);
    Arena vector = arena_reserve(UINT64_C(16) << 20);
    Arena scratch = arena_reserve(UINT64_C(16) << 20);
    const IrSizes sizes = {
        .instruction_count = 21,
        .block_count = 4,
        .constant_count = 4,
        .vector_constant_count = 2,
        .extra_count = 8,
    };
    IrFunction f = { 0 };
    const bool ready = input.base != NULL && vector.base != NULL && scratch.base != NULL && ir_create(&input, &sizes, &f) == IR_OK;
    ir_optimize_test_expect(test, ready, "LICM SIMD loop fixture allocation");
    if (ready)
    {
        f.result_type = IR_M64;
        const uint32_t offsets[] = { 0, 7, 11, 20, 21 };
        const uint32_t incoming[] = { 0, 2, 2, 18, 0, 5, 2, 15 };
        memcpy(f.block_offsets, offsets, sizeof(offsets));
        memcpy(f.extra, incoming, sizeof(incoming));
        f.constants[1].low = 1;
        f.constants[2].low = 3;
        memset(f.vector_constants[0].words, 2, sizeof(IrSimdValue));
        memset(f.vector_constants[1].words, 1, sizeof(IrSimdValue));
        ir_optimize_test_instruction(&f, 0, IR_VCONST, IR_V16I32, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(&f, 1, IR_VCONST, IR_V16I32, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        for (uint32_t i = 2; i < 5; i += 1)
        {
            ir_optimize_test_instruction(&f, i, IR_CONST, IR_I64, i - 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        ir_optimize_test_instruction(&f, 5, IR_CONST, IR_M64, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(&f, 6, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(&f, 7, IR_PHI, IR_I64, 0, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(&f, 8, IR_PHI, IR_M64, 4, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(&f, 9, IR_ICMP, IR_I1, 7, 4, IR_LT);
        ir_optimize_test_instruction(&f, 10, IR_CBR, IR_VOID, 9, 2, 3);
        ir_optimize_test_instruction(&f, 11, IR_ADD, IR_V16I32, 0, 1, BUSTER_IR_NONE);
        ir_optimize_test_instruction(&f, 12, IR_SHL, IR_V16I32, 11, 1, BUSTER_IR_NONE);
        ir_optimize_test_instruction(&f, 13, IR_VCMP, IR_M64, 12, 0, IR_EQ);
        ir_optimize_test_instruction(&f, 14, IR_VSELECT, IR_V16I32, 13, 12, 1);
        ir_optimize_test_instruction(&f, 15, IR_VCMP, IR_M64, 14, 0, IR_NE);
        for (uint32_t i = 11; i < 16; i += 1)
        {
            f.flags[i] = IR_SIMD_LANE_I8;
        }
        ir_optimize_test_instruction(&f, 16, IR_FADD, IR_V16I32, 0, 1, BUSTER_IR_NONE);
        ir_optimize_test_instruction(&f, 17, IR_VCMP, IR_M16, 0, 1, IR_EQ);
        f.flags[16] = IR_SIMD_LANE_F32;
        f.flags[17] = IR_SIMD_LANE_F32;
        ir_optimize_test_instruction(&f, 18, IR_ADD, IR_I64, 7, 3, BUSTER_IR_NONE);
        ir_optimize_test_instruction(&f, 19, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(&f, 20, IR_RET, IR_VOID, 8, BUSTER_IR_NONE, BUSTER_IR_NONE);
        IrLicmResult moved = { 0 };
        const IrCheck check = ir_optimize_licm(&f, &vector, &scratch, ir_licm_options(), &moved);
        ir_optimize_test_expect(test, check.error == IR_OK && moved.moved == 5, "LICM hoists vector arithmetic, total shifts, integer compare and select");
        if (check.error == IR_OK)
        {
            const IrFunction* out = &moved.function;
            uint32_t floating_inside = 0;
            for (uint32_t i = out->block_offsets[2]; i < out->block_offsets[3]; i += 1)
            {
                floating_inside += ir_optimize_simd_effect(out, i);
            }
            ir_optimize_test_expect(test, floating_inside == 2, "LICM keeps both floating environment effects inside the loop");
            ir_optimize_test_simd_semantics(test, &f, out, &scratch);
            ir_licm_test_fold(test, &moved);
        }
        // The restricted policy moves the same five vector values; the scalar addition depends on the loop's phi and stays.
        ir_licm_test_vectors(test, &f, &vector, &scratch, 5);
        f.op[0] = IR_UNDEF;
        f.a[0] = BUSTER_IR_NONE;
        arena_pop_to(&vector, 0);
        IrLicmResult result = { 0 };
        const IrCheck tainted = ir_optimize_licm(&f, &vector, &scratch, ir_licm_options(), &result);
        ir_optimize_test_expect(test, tainted.error == IR_OK && result.moved == 0, "LICM keeps undef-tainted vector operations in their original blocks");
        if (tainted.error == IR_OK)
        {
            ir_licm_test_fold(test, &result);
        }
    }
    arena_release(&input);
    arena_release(&vector);
    arena_release(&scratch);
}

// The width changes (#1840) in a loop, on values SCCP can't fold, a parameter's splat: LICM hoists the loop-invariant widen
// and the extract of its zeros, keeps the insert of the loop's phi and its extract inside, and the interpreter's mask is
// unchanged.
static void ir_licm_test_widths(IrOptimizeTest* test)
{
    static const char text[] = "function (i32) -> m16 {\n"
                               "b0:\n"
                               "  %0 = param.i32 0\n"
                               "  %1 = intrinsic.v4i32 %0 splat\n"
                               "  %2 = const.i32 0x00000000000000000000000000000000\n"
                               "  %3 = const.i32 0x00000000000000000000000000000001\n"
                               "  %4 = const.i32 0x00000000000000000000000000000004\n"
                               "  %5 = br.void b1\n"
                               "b1:\n"
                               "  %6 = br.void b2\n"
                               "b2:\n"
                               "  %7 = phi.i32 [%2, b1], [%12, b3]\n"
                               "  %8 = phi.v4i32 [%1, b1], [%14, b3]\n"
                               "  %9 = intrinsic.v8i32 %1 widen\n"
                               "  %10 = intrinsic.v4i32 %9 extract 1\n"
                               "  %11 = intrinsic.v8i32 %9, %8 insert 1\n"
                               "  %12 = add.i32 %7, %3\n"
                               "  %13 = intrinsic.v4i32 %11 extract 1\n"
                               "  %14 = add.v4i32 %13, %1\n"
                               "  %15 = icmp.i1 %12, %4 lt\n"
                               "  %16 = cbr.void %15, b3, b4\n"
                               "b3:\n"
                               "  %17 = br.void b2\n"
                               "b4:\n"
                               "  %18 = add.v4i32 %14, %10\n"
                               "  %19 = vcmp.m16 %18, %1 ugt lanes=i8\n"
                               "  %20 = ret.void %19\n"
                               "}\n";
    Arena input = arena_reserve(UINT64_C(16) << 20);
    Arena vector = arena_reserve(UINT64_C(16) << 20);
    Arena scratch = arena_reserve(UINT64_C(16) << 20);
    IrFunction f = { 0 };
    const bool ready = input.base != NULL && vector.base != NULL && scratch.base != NULL;
    const IrParseResult parsed = ready ? ir_parse((String8){ .pointer = (char*)text, .length = sizeof(text) - 1 }, &input, &scratch, &f) : (IrParseResult){ .error = IR_PARSE_END };
    const bool fixture = parsed.error == IR_PARSE_OK && parsed.check.error == IR_OK;
    ir_optimize_test_expect(test, fixture, "LICM's width change fixture parses");
    IrLicmResult moved = { 0 };
    const IrCheck check = fixture ? ir_optimize_licm(&f, &vector, &scratch, ir_licm_options(), &moved) : ir_check(IR_ERROR_METADATA, BUSTER_IR_NONE, 0);
    ir_optimize_test_expect(test, check.error == IR_OK && moved.moved == 2, "LICM hoists the invariant widen and its extract");
    if (check.error == IR_OK)
    {
        // The loop's header (b2) keeps the insert and the extract of the phi's part alone.
        const IrFunction* out = &moved.function;
        uint32_t inside = 0;
        for (uint32_t i = out->block_offsets[2]; i < out->block_offsets[3]; i += 1)
        {
            const bool intrinsic = out->op[i] == IR_INTRINSIC;
            inside += intrinsic && (out->c[i] == BUSTER_IR_INTRINSIC_INSERT || out->c[i] == BUSTER_IR_INTRINSIC_EXTRACT || out->c[i] == BUSTER_IR_INTRINSIC_WIDEN);
        }
        ir_optimize_test_expect(test, inside == 2, "LICM keeps the loop-variant insert and extract inside the loop");
        ir_optimize_test_simd_semantics(test, &f, out, &scratch);
        ir_licm_test_fold(test, &moved);
    }
    if (fixture)
    {
        ir_licm_test_vectors(test, &f, &vector, &scratch, 2);
    }
    arena_release(&input);
    arena_release(&vector);
    arena_release(&scratch);
}

// A permute of two tables (#1840), whose third operand is in the extra array: an invariant one twice in a loop, which GVN
// gives one value, and LICM then hoists with the sum of the two, beside a loop-carried one that stays; each result the
// interpreter's, after each of GVN's two paths.
static void ir_licm_test_permute2(IrOptimizeTest* test)
{
    static const char text[] = "function (i32) -> m16 {\n"
                               "b0:\n"
                               "  %0 = param.i32 0\n"
                               "  %1 = intrinsic.v4i32 %0 splat\n"
                               "  %2 = const.i32 0x00000000000000000000000000000000\n"
                               "  %3 = const.i32 0x00000000000000000000000000000001\n"
                               "  %4 = const.i32 0x00000000000000000000000000000004\n"
                               "  %5 = add.v4i32 %1, %1\n"
                               "  %6 = br.void b1\n"
                               "b1:\n"
                               "  %7 = phi.i32 [%2, b0], [%12, b1]\n"
                               "  %8 = phi.v4i32 [%1, b0], [%13, b1]\n"
                               "  %9 = intrinsic.v4i32 %1, %5, %1 permute2 2\n"
                               "  %10 = intrinsic.v4i32 %1, %5, %1 permute2 2\n"
                               "  %11 = add.v4i32 %9, %10\n"
                               "  %12 = add.i32 %7, %3\n"
                               "  %13 = intrinsic.v4i32 %8, %11, %5 permute2 2\n"
                               "  %14 = icmp.i1 %12, %4 lt\n"
                               "  %15 = cbr.void %14, b1, b2\n"
                               "b2:\n"
                               "  %16 = vcmp.m16 %13, %1 ugt lanes=i8\n"
                               "  %17 = ret.void %16\n"
                               "}\n";
    Arena input = arena_reserve(UINT64_C(16) << 20);
    Arena vector = arena_reserve(UINT64_C(16) << 20);
    Arena scalar = arena_reserve(UINT64_C(16) << 20);
    Arena scratch = arena_reserve(UINT64_C(16) << 20);
    IrFunction f = { 0 };
    const bool ready = input.base != NULL && vector.base != NULL && scalar.base != NULL && scratch.base != NULL;
    const IrParseResult parsed = ready ? ir_parse((String8){ .pointer = (char*)text, .length = sizeof(text) - 1 }, &input, &scratch, &f) : (IrParseResult){ .error = IR_PARSE_END };
    const bool fixture = parsed.error == IR_PARSE_OK && parsed.check.error == IR_OK;
    ir_optimize_test_expect(test, fixture, "the permute2 loop fixture parses");
    IrLicmResult results[2] = { 0 };
    for (uint32_t path = 0; fixture && path < 2; path += 1)
    {
        Arena* output = path != 0 ? &scalar : &vector;
        IrGvnOptions numbering = ir_gvn_options();
        numbering.scalar = path != 0;
        IrGvnResult numbered = { 0 };
        const IrCheck gvn = ir_optimize_gvn(&f, output, &scratch, numbering, &numbered);
        ir_optimize_test_expect(test, gvn.error == IR_OK && numbered.removed == 1, "GVN gives the two equal permute2s one value");
        if (gvn.error == IR_OK)
        {
            ir_optimize_test_simd_semantics(test, &f, &numbered.function, &scratch);
            const IrCheck check = ir_optimize_licm(&numbered.function, output, &scratch, ir_licm_options(), results + path);
            ir_optimize_test_expect(test, check.error == IR_OK && results[path].moved == 2, "LICM hoists the invariant permute2 and its sum");
            if (check.error == IR_OK)
            {
                ir_optimize_test_simd_semantics(test, &f, &results[path].function, &scratch);
            }
            if (check.error == IR_OK && path == 0)
            {
                ir_licm_test_fold(test, &results[path]);
            }
        }
    }
    ir_optimize_test_expect(test, results[0].function.instruction_count != 0 && results[1].function.instruction_count != 0 &&
                            ir_optimize_test_equal(&results[0].function, &results[1].function), "GVN's two paths, each followed by LICM, treat permute2 alike");
    // Without GVN the two equal permute2s remain separate values, so the vector policy moves both and their sum.
    if (fixture)
    {
        ir_licm_test_vectors(test, &f, &vector, &scratch, 3);
    }
    arena_release(&input);
    arena_release(&vector);
    arena_release(&scalar);
    arena_release(&scratch);
}

static __attribute__((unused)) int run_ir_licm_tests(int argc, char** argv)
{
    IrOptimizeTest test = {
        .random = BUSTER_IR_OPTIMIZE_TEST_SEED,
        .licm_digest = BUSTER_IR_OPTIMIZE_TEST_DIGEST_BASIS,
    };
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
        const uint64_t seed = test.random;
        ir_licm_test_classify_guards(&test);
        ir_licm_test_ir(&test);
        ir_licm_test_progress(&test);
        ir_licm_test_simd(&test);
        ir_licm_test_vector_nested(&test);
        ir_licm_test_widths(&test);
        ir_licm_test_permute2(&test);
        // The digest of the walk's results, with the default seed, which generates the random fixtures.
        if (seed == BUSTER_IR_OPTIMIZE_TEST_SEED)
        {
            ir_optimize_test_expect(&test, test.licm_digest == BUSTER_IR_OPTIMIZE_TEST_LICM_DIGEST, "LICM's results have their frozen digest");
            if (test.licm_digest != BUSTER_IR_OPTIMIZE_TEST_LICM_DIGEST)
            {
                fprintf(stderr, "ir-licm: digest %#018" PRIx64 "\n", test.licm_digest);
            }
        }
        printf("ir-licm: %" PRIu64 " checks, %" PRIu64 " failures\n", test.checks, test.failures);
        result = test.failures == 0 ? 0 : 1;
    }
    else
    {
        fprintf(stderr, "usage: test-ir-licm [--seed N]\n");
    }
    return result;
}
