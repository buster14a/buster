// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original GVN fixtures. Include after ir_optimize_gvn.c and ir_optimize_test.c. The independent bounded fixture
// interpreter reads opcodes directly; neither it nor the scalar available-expression oracle uses congruence classes.

static void ir_gvn_test_guards(IrOptimizeTest* test)
{
    const long page = sysconf(_SC_PAGESIZE);
    void* mapping = page > 0 ? mmap(0, (uint64_t)page * 10, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    bool ready = mapping != MAP_FAILED;
    for (uint32_t p = 1; ready && p < 10; p += 2)
    {
        ready = mprotect((char*)mapping + p * page, page, PROT_NONE) == 0;
    }
    ir_optimize_test_expect(test, ready, "GVN guarded columns setup");
    uint8_t types[33] = { 0 };
    uint8_t flags[33] = { 0 };
    uint32_t operands[33] = { 0 };
    uint32_t classes[33] = { 0 };
    uint32_t aliases[33] = { 0 };
    uint32_t order[33] = { 0 };
    uint64_t expected[33] = { 0 };
    uint64_t fields[33 * BUSTER_IR_GVN_FIELDS] = { 0 };
    for (uint32_t count = 0; ready && count <= 33; count += 1)
    {
        IrFunction f = {
            .instruction_count = count,
            .op = (uint8_t*)((char*)mapping + page - count),
            .type = types,
            .flags = flags,
            .alignment_log2 = flags,
            .a = operands,
            .b = operands,
            .c = operands,
        };
        IrGvnWork work = {
            .ir = { .function = &f },
            .pure = (uint8_t*)((char*)mapping + 3 * page - count),
            .tainted = (uint8_t*)((char*)mapping + 5 * page - count),
            .hashes = (uint64_t*)((char*)mapping + 7 * page - count * sizeof(uint64_t)),
            .fields = fields,
            .classes = classes,
            .alias = aliases,
            .order = order,
        };
        for (uint32_t i = 0; i < count; i += 1)
        {
            f.op[i] = IR_PARAM;
            f.type[i] = IR_I64;
            operands[i] = i;
            work.pure[i] = 1;
            work.tainted[i] = 0;
            aliases[i] = i;
            classes[i] = i + 1;
        }
        ir_gvn_keys(&work, false, true);
        memcpy(expected, work.hashes, count * sizeof(uint64_t));
        ir_gvn_keys(&work, false, false);
        ir_optimize_test_expect(test, memcmp(expected, work.hashes, count * sizeof(uint64_t)) == 0, "GVN exact scalar/vector hashes at every guarded tail");
        // Equality must compare full keys even if every hash collides.
        for (uint32_t i = 0; i < count; i += 1)
        {
            work.hashes[i] = 0;
        }
        for (uint32_t i = 1; i < count; i += 1)
        {
            ir_optimize_test_expect(test, ir_gvn_compare(&work, i - 1, i, false) != 0, "GVN hash collisions cannot merge distinct parameter keys");
        }
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, (uint64_t)page * 10);
    }
}

static bool ir_gvn_test_cycle(Arena* arena, IrFunction* f)
{
    const IrSizes sizes = { .instruction_count = 15, .block_count = 4, .constant_count = 3, .extra_count = 12 };
    const bool created = ir_create(arena, &sizes, f) == IR_OK;
    if (created)
    {
        f->result_type = IR_I64;
        f->block_offsets[1] = 4;
        f->block_offsets[2] = 9;
        f->block_offsets[3] = 13;
        f->block_offsets[4] = 15;
        f->constants[0].low = 0;
        f->constants[1].low = 1;
        f->constants[2].low = 5;
        ir_optimize_test_instruction(f, 0, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 1, IR_CONST, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 2, IR_CONST, IR_I64, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 3, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 4, IR_PHI, IR_I64, 0, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 5, IR_PHI, IR_I64, 4, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 6, IR_PHI, IR_I64, 8, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 7, IR_ICMP, IR_I1, 4, 2, IR_ULT);
        ir_optimize_test_instruction(f, 8, IR_CBR, IR_VOID, 7, 2, 3);
        ir_optimize_test_instruction(f, 9, IR_ADD, IR_I64, 4, 1, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 10, IR_ADD, IR_I64, 5, 1, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 11, IR_ADD, IR_I64, 6, 1, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 12, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 13, IR_SUB, IR_I64, 5, 6, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 14, IR_RET, IR_VOID, 13, BUSTER_IR_NONE, BUSTER_IR_NONE);
        // The third phi deliberately uses the opposite side-table order.
        const uint32_t extra[] = { 0, 0, 2, 9, 0, 0, 2, 10, 2, 11, 0, 0 };
        memcpy(f->extra, extra, sizeof(extra));
    }
    return created;
}

static bool ir_gvn_test_straight(Arena* arena, IrFunction* f, uint32_t kind)
{
    const IrSizes sizes = { .instruction_count = 8, .block_count = 1, .constant_count = 1, .parameter_count = 1 };
    const bool created = ir_create(arena, &sizes, f) == IR_OK;
    if (created)
    {
        f->result_type = IR_I64;
        f->parameter_types[0] = IR_I64;
        f->block_offsets[1] = 8;
        f->constants[0].low = kind == 2 ? 0 : 7;
        ir_optimize_test_instruction(f, 0, kind == 1 ? IR_UNDEF : IR_PARAM, IR_I64, kind == 1 ? BUSTER_IR_NONE : 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 1, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 2, kind == 2 ? IR_UDIV : IR_ADD, IR_I64, 0, 1, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 3, kind == 2 ? IR_UDIV : IR_ADD, IR_I64, 0, 1, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 4, IR_COPY, IR_I64, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 5, IR_XOR, IR_I64, 2, 3, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 6, IR_ADD, IR_I64, 5, 4, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 7, IR_RET, IR_VOID, 6, BUSTER_IR_NONE, BUSTER_IR_NONE);
    }
    return created;
}

static void ir_gvn_test_compare(IrOptimizeTest* test, const IrFunction* f, Arena* vector, Arena* scalar, Arena* scratch, uint32_t minimum_removed, bool cyclic)
{
    const uint64_t vm = vector->length;
    const uint64_t sm = scalar->length;
    const uint64_t tm = scratch->length;
    IrGvnOptions options = ir_gvn_options();
    IrGvnResult a = { 0 };
    IrGvnResult b = { 0 };
    const IrCheck ac = ir_optimize_gvn(f, vector, scratch, options, &a);
    options.scalar = true;
    const IrCheck bc = ir_optimize_gvn(f, scalar, scratch, options, &b);
    ir_optimize_test_expect(test, ac.error == IR_OK && bc.error == IR_OK && scratch->length == tm, "GVN paths accept verified fixture and restore scratch");
    if (ac.error == IR_OK && bc.error == IR_OK)
    {
        ir_optimize_test_expect(test, a.removed >= minimum_removed && a.removed >= b.removed, "GVN congruence result removes at least scalar available expressions");
        if (cyclic)
        {
            ir_optimize_test_expect(test, a.removed > b.removed, "GVN refines cyclic congruences missed by scalar available expressions");
        }
        ir_optimize_test_expect(test, ir_verify(&a.function, scratch, 0).error == IR_OK && ir_verify_scalar(&b.function, scratch, 0).error == IR_OK, "GVN rewritten functions independently verified");
        for (uint32_t trial = 0; trial < 18; trial += 1)
        {
            const uint64_t input = trial == 17 ? UINT64_MAX : trial;
            const IrOptimizeExecution before = ir_optimize_test_execute(f, scratch, input, trial & 1);
            const IrOptimizeExecution after = ir_optimize_test_execute(&a.function, scratch, input, trial & 1);
            const IrOptimizeExecution reference = ir_optimize_test_execute(&b.function, scratch, input, trial & 1);
            ir_optimize_test_expect(test, before.status != 4 && before.status == after.status && before.value == after.value && before.effects == after.effects, "GVN vector preserves independently interpreted returns, faults and effects");
            ir_optimize_test_expect(test, before.status == reference.status && before.value == reference.value && before.effects == reference.effects, "GVN scalar preserves independently interpreted returns, faults and effects");
        }
        arena_pop_to(scalar, sm);
        options.scalar = false;
        options.sweep_limit = 0;
        IrGvnResult fallback = { 0 };
        const IrCheck fc = ir_optimize_gvn(f, scalar, scratch, options, &fallback);
        ir_optimize_test_expect(test, fc.error == IR_OK && fallback.fallback && fallback.classes == a.classes && fallback.rounds == a.rounds && fallback.scratch_bytes == a.scratch_bytes && ir_optimize_test_equal(&a.function, &fallback.function), "GVN forced scalar refinement completion is exact");
    }
    arena_pop_to(scalar, sm);
    arena_pop_to(vector, vm);
}

static void ir_gvn_test_dead_cycle(IrOptimizeTest* test, Arena* input, Arena* vector, Arena* scalar, Arena* scratch)
{
    const IrSizes sizes = { .instruction_count = 6, .block_count = 3, .constant_count = 1 };
    IrFunction f = { 0 };
    const bool created = ir_create(input, &sizes, &f) == IR_OK;
    ir_optimize_test_expect(test, created, "GVN dead copy cycle fixture allocation");
    if (created)
    {
        f.result_type = IR_I64;
        f.constants[0].low = 11;
        f.block_offsets[1] = 2;
        f.block_offsets[2] = 4;
        f.block_offsets[3] = 6;
        ir_optimize_test_instruction(&f, 0, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(&f, 1, IR_RET, IR_VOID, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(&f, 2, IR_COPY, IR_I64, 4, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(&f, 3, IR_BR, IR_VOID, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(&f, 4, IR_COPY, IR_I64, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(&f, 5, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_gvn_test_compare(test, &f, vector, scalar, scratch, 4, false);
    }
    arena_pop_to(input, 0);
}

static void ir_gvn_test_limits(IrOptimizeTest* test, Arena* input, Arena* output, Arena* scratch)
{
    IrFunction f = { 0 };
    const bool created = ir_gvn_test_straight(input, &f, 0);
    ir_optimize_test_expect(test, created, "GVN rollback fixture allocation");
    if (created)
    {
        const uint64_t sm = scratch->length;
        IrGvnResult result = { .removed = 139 };
        const IrGvnResult saved = result;
        // Vary output capacity across individual column allocations, not only an immediately full arena.
        for (uint64_t available = 0; available < 1536; available += 17)
        {
            Arena tiny = *output;
            const uint64_t om = tiny.length;
            tiny.capacity = om + available;
            const IrCheck check = ir_optimize_gvn(&f, &tiny, scratch, ir_gvn_options(), &result);
            if (check.error != IR_OK)
            {
                ir_optimize_test_expect(test, check.error == IR_ERROR_MEMORY && tiny.length == om && scratch->length == sm && memcmp(&result, &saved, sizeof(result)) == 0, "GVN partial allocation failure rolls back arenas and result");
            }
            result = saved;
        }
        f.a[2] = 6;
        const IrCheck invalid = ir_optimize_gvn(&f, output, scratch, ir_gvn_options(), &result);
        ir_optimize_test_expect(test, invalid.error != IR_OK && scratch->length == sm && memcmp(&result, &saved, sizeof(result)) == 0, "GVN invalid SSA is refused transactionally");
    }
    arena_pop_to(input, 0);
}

// Direct producer/consumer checks for partition grouping. Class labels may differ from the sorted oracle, but the
// complete equivalence relation must not. Every mutable grouping column and the hashes end at a protected page.
static void ir_gvn_test_partitions(IrOptimizeTest* test)
{
    const long page = sysconf(_SC_PAGESIZE);
    void* mapping = page >= 257 * (long)sizeof(uint64_t) ? mmap(0, (uint64_t)page * 8, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    bool ready = mapping != MAP_FAILED;
    for (uint32_t p = 1; ready && p < 8; p += 2)
    {
        ready = mprotect((char*)mapping + p * page, page, PROT_NONE) == 0;
    }
    ir_optimize_test_expect(test, ready, "GVN partition guarded columns setup");
    uint8_t operations[257] = { 0 };
    uint64_t fields[257 * BUSTER_IR_GVN_FIELDS] = { 0 };
    uint32_t grouped[257] = { 0 };
    const uint32_t tails[] = { 63, 64, 65, 127, 128, 129, 255, 256, 257 };
    for (uint32_t size = 0; ready && size < 34 + BUSTER_ARRAY_LENGTH(tails); size += 1)
    {
        const uint32_t n = size < 34 ? size : tails[size - 34];
        for (uint32_t kind = 0; kind < 4; kind += 1)
        {
            for (uint32_t initial = 0; initial < 2; initial += 1)
            {
                IrFunction f = { .instruction_count = n, .op = operations };
                IrGvnWork work = {
                    .ir = { .function = &f },
                    .order = (uint32_t*)((char*)mapping + page - (uint64_t)n * sizeof(uint32_t)),
                    .temporary_order = (uint32_t*)((char*)mapping + 3 * page - (uint64_t)n * sizeof(uint32_t)),
                    .next_classes = (uint32_t*)((char*)mapping + 5 * page - (uint64_t)n * sizeof(uint32_t)),
                    .hashes = (uint64_t*)((char*)mapping + 7 * page - (uint64_t)n * sizeof(uint64_t)),
                    .fields = fields,
                };
                for (uint32_t i = 0; i < n; i += 1)
                {
                    operations[i] = IR_ADD;
                    const uint32_t key = kind == 1 ? i % 7 : kind == 3 ? 0 : i;
                    for (uint32_t field = 0; field < BUSTER_IR_GVN_FIELDS; field += 1)
                    {
                        // In collision mode each field independently distinguishes some keys.
                        fields[(uint64_t)field * n + i] = kind == 2 ? (field == i % BUSTER_IR_GVN_FIELDS ? (uint64_t)i / BUSTER_IR_GVN_FIELDS + 1 : 0) : ((uint64_t)key << (field * 7)) ^ (UINT64_C(0x9e3779b97f4a7c15) * (field + 1));
                    }
                    work.hashes[i] = kind >= 2 ? 0 : ir_gvn_hash_one(&work, i, initial != 0);
                }
                const uint32_t classes = ir_gvn_partition_group(&work, initial != 0, false);
                if (n != 0)
                {
                    memcpy(grouped, work.next_classes, (uint64_t)n * sizeof(uint32_t));
                }
                ir_optimize_test_expect(test, work.statistics.partition_probes <= (uint64_t)n * BUSTER_IR_GVN_PROBES_PER_INSTRUCTION, "GVN partition probing obeys its linear budget");
                const bool crowded = kind == 2 && n > 2 * BUSTER_IR_GVN_PROBES_PER_INSTRUCTION + 1;
                ir_optimize_test_expect(test, work.statistics.partition_sort_fallbacks == crowded, "GVN distinct full-hash collisions take bounded sort completion; duplicate classes do not");
                const uint32_t sorted = ir_gvn_partition_group(&work, initial != 0, true);
                ir_optimize_test_expect(test, classes == sorted, "GVN grouped and sorted partition counts agree");
                for (uint32_t i = 0; i < n; i += 1)
                {
                    ir_optimize_test_expect(test, grouped[i] != 0 && grouped[i] <= classes, "GVN partition publishes every class within its range");
                    for (uint32_t j = 0; j < n; j += 1)
                    {
                        bool equal = true;
                        for (uint32_t field = 0; field < BUSTER_IR_GVN_FIELDS; field += 1)
                        {
                            equal = equal && fields[(uint64_t)field * n + i] == fields[(uint64_t)field * n + j];
                        }
                        ir_optimize_test_expect(test, (grouped[i] == grouped[j]) == equal && (work.next_classes[i] == work.next_classes[j]) == equal, "GVN grouped and sorted partitions match the independent full-key relation");
                    }
                }
            }
        }
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, (uint64_t)page * 8);
    }
}

static void ir_gvn_test_large_partition(IrOptimizeTest* test, Arena* input, Arena* vector, Arena* scalar, Arena* scratch)
{
    IrFunction f = { 0 };
    const bool created = ir_optimize_test_random_dag(input, &f, test, 4096);
    ir_optimize_test_expect(test, created, "GVN 4100-instruction DAG allocation");
    if (created)
    {
        for (uint32_t i = 4; i + 1 < f.instruction_count; i += 4)
        {
            ir_optimize_test_instruction(&f, i, f.op[i - 1], f.type[i - 1], f.a[i - 1], f.b[i - 1], f.c[i - 1]);
        }
        ir_gvn_test_compare(test, &f, vector, scalar, scratch, 1024, false);
        IrGvnResult result = { 0 };
        const IrCheck check = ir_optimize_gvn(&f, vector, scratch, ir_gvn_options(), &result);
        ir_optimize_test_expect(test, check.error == IR_OK && result.partition_sort_fallbacks == 0 && !result.fallback, "GVN ordinary large DAG needs no comparison-sort completion");
        ir_optimize_test_expect(test, check.error == IR_OK && result.partition_probes <= (uint64_t)f.instruction_count * BUSTER_IR_GVN_PROBES_PER_INSTRUCTION * result.rounds, "GVN large DAG partition work stays within its per-round budget");
    }
    arena_pop_to(vector, 0);
    arena_pop_to(input, 0);
}

static void ir_gvn_test_ir(IrOptimizeTest* test)
{
    Arena input = arena_reserve(16ull << 20);
    Arena vector = arena_reserve(16ull << 20);
    Arena scalar = arena_reserve(16ull << 20);
    Arena scratch = arena_reserve(32ull << 20);
    const bool ready = input.base != 0 && vector.base != 0 && scalar.base != 0 && scratch.base != 0;
    ir_optimize_test_expect(test, ready, "GVN fixture arenas");
    if (ready)
    {
        for (uint32_t kind = 0; kind < 3; kind += 1)
        {
            IrFunction f = { 0 };
            const bool created = ir_gvn_test_straight(&input, &f, kind);
            ir_optimize_test_expect(test, created, "GVN straight fixture allocation");
            if (created)
            {
                ir_gvn_test_compare(test, &f, &vector, &scalar, &scratch, kind == 1 ? 0 : 2, false);
                if (kind == 1)
                {
                    IrGvnResult result = { 0 };
                    const IrCheck check = ir_optimize_gvn(&f, &vector, &scratch, ir_gvn_options(), &result);
                    ir_optimize_test_expect(test, check.error == IR_OK && result.removed == 0, "GVN excludes undef and transitively tainted values and copies");
                    arena_pop_to(&vector, 0);
                }
            }
            arena_pop_to(&input, 0);
        }
        IrFunction cycle = { 0 };
        const bool cycle_created = ir_gvn_test_cycle(&input, &cycle);
        ir_optimize_test_expect(test, cycle_created, "GVN cyclic fixture allocation");
        if (cycle_created)
        {
            ir_gvn_test_compare(test, &cycle, &vector, &scalar, &scratch, 4, true);
            // Different initial values force the optimistic class to split through the entire recurrence.
            cycle.extra[11] = 1;
            ir_gvn_test_compare(test, &cycle, &vector, &scalar, &scratch, 2, true);
        }
        arena_pop_to(&input, 0);
        IrFunction diamond = { 0 };
        const bool diamond_created = ir_optimize_test_diamond(&input, &diamond, 2);
        ir_optimize_test_expect(test, diamond_created, "GVN sibling fixture allocation");
        if (diamond_created)
        {
            diamond.op[6] = IR_ADD;
            diamond.a[6] = 1;
            diamond.b[6] = 1;
            ir_gvn_test_compare(test, &diamond, &vector, &scalar, &scratch, 0, false);
            IrGvnResult result = { 0 };
            const IrCheck check = ir_optimize_gvn(&diamond, &vector, &scratch, ir_gvn_options(), &result);
            ir_optimize_test_expect(test, check.error == IR_OK && result.removed == 0, "GVN never substitutes an equal expression from a sibling block");
            arena_pop_to(&vector, 0);
        }
        arena_pop_to(&input, 0);
        for (uint32_t kind = 0; kind < 2; kind += 1)
        {
            IrFunction f = { 0 };
            const bool created = ir_optimize_test_effects(&input, &f, kind != 0);
            ir_optimize_test_expect(test, created, "GVN effects fixture allocation");
            if (created)
            {
                ir_gvn_test_compare(test, &f, &vector, &scalar, &scratch, 0, false);
            }
            arena_pop_to(&input, 0);
        }
        ir_gvn_test_dead_cycle(test, &input, &vector, &scalar, &scratch);
        ir_gvn_test_limits(test, &input, &vector, &scratch);
        ir_gvn_test_large_partition(test, &input, &vector, &scalar, &scratch);
        for (uint32_t length = 0; length < 130; length += 1)
        {
            IrFunction f = { 0 };
            const bool created = ir_optimize_test_random_dag(&input, &f, test, length);
            ir_optimize_test_expect(test, created, "GVN random DAG allocation");
            if (created)
            {
                for (uint32_t i = 4; i + 1 < f.instruction_count; i += 4)
                {
                    ir_optimize_test_instruction(&f, i, f.op[i - 1], f.type[i - 1], f.a[i - 1], f.b[i - 1], f.c[i - 1]);
                }
                ir_gvn_test_compare(test, &f, &vector, &scalar, &scratch, 0, false);
            }
            arena_pop_to(&input, 0);
        }
    }
    arena_release(&scratch);
    arena_release(&scalar);
    arena_release(&vector);
    arena_release(&input);
}

static __attribute__((unused)) int run_ir_gvn_tests(int argc, char** argv)
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
        ir_gvn_test_guards(&test);
        ir_gvn_test_partitions(&test);
        ir_gvn_test_ir(&test);
        printf("ir-gvn: %" PRIu64 " checks, %" PRIu64 " failures\n", test.checks, test.failures);
        result = test.failures == 0 ? 0 : 1;
    }
    else
    {
        fprintf(stderr, "usage: test-ir-gvn [--seed N]\n");
    }
    return result;
}
