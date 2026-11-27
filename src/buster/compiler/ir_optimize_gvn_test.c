// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original GVN fixtures. Include after ir_optimize_gvn.c and ir_optimize_test.c. The independent bounded fixture
// interpreter reads opcodes directly. Frozen rewrites and full refinement check the class solver.

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
    uint32_t members[33] = { 0 };
    // Hashes of 33 parameter keys captured before retiring the available-expression walk.
    static const uint64_t expected[33] = {
        UINT64_C(0xa0b54f0eae72a95b), UINT64_C(0x429e2ecbee38426f), UINT64_C(0xe4870e892dfddb83),
        UINT64_C(0x866fee466dc37497), UINT64_C(0x43a5ac6c3dfadbe3), UINT64_C(0xca41adc0ed4ea6bf),
        UINT64_C(0x87776be6bd860e0b), UINT64_C(0x0e136d3b6cd9d8e7), UINT64_C(0xe69609c9cd830e6b),
        UINT64_C(0x887ee9870d48a77f), UINT64_C(0xf3ce0c732c2aa423), UINT64_C(0x95b6ec306bf03d37),
        UINT64_C(0x52ecaa563c27a483), UINT64_C(0x1022687c0c5f0bcf), UINT64_C(0xcd5826a1dc96731b),
        UINT64_C(0x1d5a6b256b06a187), UINT64_C(0xbf434ae2aacc3a9b), UINT64_C(0xce5fa4422c590c8f),
        UINT64_C(0x03150a5d2a576cc3), UINT64_C(0x123163bcabe43eb7), UINT64_C(0x6233a8403a546d23),
        UINT64_C(0xe8cfa994e9a837ff), UINT64_C(0x1338e15cfba6d82b), UINT64_C(0x2ca1690f69336a27),
        UINT64_C(0x72577f400ba3d88b), UINT64_C(0xa70ce55b09a238bf), UINT64_C(0x125c084728843563),
        UINT64_C(0x217861a6aa110757), UINT64_C(0x717aa62a388135c3), UINT64_C(0x9be3ddf24a7fd5ef),
        UINT64_C(0xebe62275d8f0045b), UINT64_C(0x3be866f9676032c7), UINT64_C(0xddd146b6a725cbdb),
    };
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
            .members = members,
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
            members[i] = i;
        }
        ir_gvn_keys(&work, count, false);
        ir_optimize_test_expect(test, memcmp(expected, work.hashes, count * sizeof(uint64_t)) == 0, "GVN parameter hashes match the frozen prefix at every guarded tail");
        // Equality must compare full keys even if every hash collides.
        for (uint32_t i = 0; i < count; i += 1)
        {
            work.hashes[i] = 0;
        }
        for (uint32_t i = 1; i < count; i += 1)
        {
            ir_optimize_test_expect(test, ir_gvn_compare(&work, i - 1, i, i - 1, i, false) != 0, "GVN hash collisions cannot merge distinct parameter keys");
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

static void ir_gvn_test_debug_bindings(IrOptimizeTest* test, const IrFunction* source, Arena* input, Arena* output, Arena* reference, Arena* scratch)
{
    const uint64_t im = input->length;
    const uint64_t om = output->length;
    const uint64_t rm = reference->length;
    const uint64_t sm = scratch->length;
    IrFunction f = *source;
    IrDebugBinding binding = { .local = 1, .entry = 3, .value = 3, .anchor = 3, .kind = IR_DEBUG_BIND_VALUE, .bytes = 8, .type = IR_I64 };
    uint8_t flags[2] = { BUSTER_IR_DEBUG_BIND_ENTRY, BUSTER_IR_DEBUG_BIND_REGIONS };
    IrDebugRegion region = { .local = 1, .block = 0, .begin = 4, .end = f.instruction_count, .block_end = f.instruction_count, .value = 3, .next = BUSTER_IR_NONE,
                             .kind = IR_DEBUG_BIND_VALUE, .bytes = 8, .type = IR_I64 };
    const IrDebugInfo seed = { .entry_count = f.instruction_count, .local_count = 2, .bindings = &binding, .binding_count = 1,
                               .regions = &region, .region_count = 1, .binding_flags = flags, .first_block_end = f.instruction_count };
    const bool created = ir_debug_create(input, &f, &seed);
    ir_optimize_test_expect(test, created, "GVN source assignment fixture allocation");
    for (uint32_t i = 0; created && i < f.instruction_count; i += 1)
    {
        f.debug->origins[i] = i;
    }
    for (uint32_t path = 0; created && path < 3; path += 1)
    {
        IrGvnOptions options = ir_gvn_options();
        options.complete = path == 1;
        options.apply_only = path == 2;
        IrGvnResult a = { 0 };
        IrGvnResult b = { 0 };
        const IrCheck ac = ir_optimize_gvn(&f, output, scratch, options, &a);
        const IrCheck bc = ir_optimize_gvn(source, reference, scratch, options, &b);
        ir_optimize_test_expect(test, ac.error == IR_OK && bc.error == IR_OK && scratch->length == sm && ir_optimize_test_equal(&a.function, &b.function), "GVN source assignments do not affect executable IR");
        if (ac.error == IR_OK && a.function.debug != NULL && a.function.debug->binding_count == 1 && a.function.debug->bindings != NULL)
        {
            const IrDebugBinding result = a.function.debug->bindings[0];
            ir_optimize_test_expect(test, a.function.debug->binding_count == 1 && result.kind == IR_DEBUG_BIND_VALUE && result.value < a.function.instruction_count &&
                                   result.anchor == result.value && a.function.op[result.value] == IR_ADD && a.function.debug->origins[result.value] == 2 &&
                                   a.function.debug->region_count == 1 && a.function.debug->regions[0].kind == IR_DEBUG_BIND_VALUE &&
                                   a.function.debug->regions[0].value == result.value && a.function.debug->regions[0].block == 0 &&
                                   a.function.debug->regions[0].begin == 4 && f.debug->regions[0].value == 3 &&
                                   result.entry == 3 && f.debug->bindings[0].value == 3, "GVN removed duplicate source value follows its exact surviving representative");
        }
        else
        {
            ir_optimize_test_expect(test, false, "GVN preserves source assignment sidecar");
        }
        arena_pop_to(reference, rm);
        arena_pop_to(output, om);
    }
    arena_pop_to(input, im);
}

// Cyclic fixtures freeze the exact removal count. A recurrent input has values that reach a cycle.
static void ir_gvn_test_compare(IrOptimizeTest* test, const IrFunction* f, Arena* vector, Arena* reference, Arena* scratch, uint32_t minimum_removed, bool exact_removals, bool recurrent)
{
    const uint64_t vm = vector->length;
    const uint64_t sm = reference->length;
    const uint64_t tm = scratch->length;
    IrGvnOptions options = ir_gvn_options();
    IrGvnResult a = { 0 };
    const IrCheck ac = ir_optimize_gvn(f, vector, scratch, options, &a);
    ir_optimize_test_expect(test, ac.error == IR_OK && scratch->length == tm, "GVN accepts a verified fixture and restores scratch");
    if (ac.error == IR_OK)
    {
        ir_optimize_test_fold_function(&test->gvn_digest, &a.function);
        ir_optimize_test_expect(test, exact_removals ? a.removed == minimum_removed : a.removed >= minimum_removed, "GVN removes the expected congruences");
        ir_optimize_test_expect(test, ir_verify(&a.function, scratch, 0).error == IR_OK, "Rewritten GVN function verifies independently");
        for (uint32_t trial = 0; trial < 18; trial += 1)
        {
            const uint64_t input = trial == 17 ? UINT64_MAX : trial;
            const IrOptimizeExecution before = ir_optimize_test_execute(f, scratch, input, trial & 1);
            const IrOptimizeExecution after = ir_optimize_test_execute(&a.function, scratch, input, trial & 1);
            ir_optimize_test_expect(test, before.status != 4 && before.status == after.status && before.value == after.value && before.effects == after.effects, "GVN vector preserves independently interpreted returns, faults and effects");
        }
        // Values that reach no cycle are numbered without refinement rounds; only the rest are refined.
        ir_optimize_test_expect(test, recurrent ? a.acyclic < f->instruction_count && a.rounds != 0 : a.acyclic == f->instruction_count && a.rounds == 0, "GVN refines exactly the values that reach a cycle");
        // The production entry may prove every phi distinct before its back edge is numbered.
        // Compare that proof with the full census, including cyclic fixtures and permuted inputs.
        arena_pop_to(reference, sm);
        IrGvnOptions apply = ir_gvn_options();
        apply.apply_only = true;
        IrGvnResult applied = { 0 };
        const IrCheck application = ir_optimize_gvn(f, reference, scratch, apply, &applied);
        ir_optimize_test_expect(test, application.error == IR_OK && applied.classes == a.classes &&
                                ir_optimize_test_equal(&applied.function, &a.function), "GVN production phi proof preserves all congruences");
        // Refining every instruction in synchronous rounds is the acyclic numbering's exact oracle: the same
        // partition, so the same class count and rewritten function, from the same scratch.
        arena_pop_to(reference, sm);
        options.complete = true;
        IrGvnResult complete = { 0 };
        const IrCheck cc = ir_optimize_gvn(f, reference, scratch, options, &complete);
        ir_optimize_test_expect(test, cc.error == IR_OK && complete.acyclic == 0 && complete.classes == a.classes && complete.scratch_bytes == a.scratch_bytes && ir_optimize_test_equal(&a.function, &complete.function), "GVN acyclic numbering matches complete refinement");
        arena_pop_to(reference, sm);
        options.sweep_limit = 0;
        IrGvnResult fallback = { 0 };
        const IrCheck fc = ir_optimize_gvn(f, reference, scratch, options, &fallback);
        ir_optimize_test_expect(test, fc.error == IR_OK && fallback.fallback && fallback.classes == complete.classes && fallback.rounds == complete.rounds && fallback.scratch_bytes == complete.scratch_bytes && ir_optimize_test_equal(&complete.function, &fallback.function), "GVN forced sorted refinement completion is exact");
        // With no probe budget the acyclic numbering gives up at its first occupied bucket and every grouping at its
        // first comparison; complete refinement with sorted grouping still reaches the same partition.
        arena_pop_to(reference, sm);
        options = ir_gvn_options();
        options.probes = 0;
        IrGvnResult starved = { 0 };
        const IrCheck sc = ir_optimize_gvn(f, reference, scratch, options, &starved);
        ir_optimize_test_expect(test, sc.error == IR_OK && starved.partition_probes == 0 && starved.classes == a.classes && starved.scratch_bytes == a.scratch_bytes && ir_optimize_test_equal(&a.function, &starved.function), "GVN probe-budget completions are exact");
    }
    arena_pop_to(reference, sm);
    arena_pop_to(vector, vm);
}

static void ir_gvn_test_dead_cycle(IrOptimizeTest* test, Arena* input, Arena* vector, Arena* reference, Arena* scratch)
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
        ir_gvn_test_compare(test, &f, vector, reference, scratch, 4, false, false);
    }
    arena_pop_to(input, 0);
}

// Both operands of each dead-block addition name the same value in a later block. Their pending edges exceed
// the in-column wait buffer and two further chunks of 256 records. Each addition must be released exactly once.
static void ir_gvn_test_wait_growth(IrOptimizeTest* test, Arena* input, Arena* vector, Arena* reference, Arena* scratch)
{
    const uint64_t mark = input->length;
    const uint32_t adds = 1200;
    const uint32_t later = adds + 3;
    IrFunction f = { 0 };
    const IrSizes sizes = { .instruction_count = later + 2, .block_count = 3, .constant_count = 1 };
    const bool created = ir_create(input, &sizes, &f) == IR_OK;
    ir_optimize_test_expect(test, created, "GVN pending-edge growth fixture allocation");
    if (created)
    {
        f.result_type = IR_I64;
        f.constants[0].low = 11;
        f.block_offsets[1] = 2;
        f.block_offsets[2] = later;
        f.block_offsets[3] = later + 2;
        ir_optimize_test_instruction(&f, 0, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(&f, 1, IR_RET, IR_VOID, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        for (uint32_t i = 2; i < later - 1; i += 1)
        {
            ir_optimize_test_instruction(&f, i, IR_ADD, IR_I64, later, later, BUSTER_IR_NONE);
        }
        ir_optimize_test_instruction(&f, later - 1, IR_BR, IR_VOID, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(&f, later, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(&f, later + 1, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_gvn_test_compare(test, &f, vector, reference, scratch, f.instruction_count - 2, false, false);
    }
    arena_pop_to(input, mark);
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
    uint32_t members[257] = { 0 };
    const uint32_t tails[] = { 63, 64, 65, 127, 128, 129, 255, 256, 257 };
    for (uint32_t size = 0; ready && size < 34 + BUSTER_ARRAY_LENGTH(tails); size += 1)
    {
        const uint32_t n = size < 34 ? size : tails[size - 34];
        for (uint32_t kind = 0; kind < 4; kind += 1)
        {
            for (uint32_t initial = 0; initial < 2; initial += 1)
            {
                IrFunction f = { .instruction_count = n, .op = operations };
                // Odd sizes partition the members in reverse order, after a nonzero class base.
                const uint32_t base = n & 1 ? 1000 : 0;
                IrGvnWork work = {
                    .ir = { .function = &f },
                    .members = members,
                    .probes = BUSTER_IR_GVN_PROBES_PER_INSTRUCTION,
                    .order = (uint32_t*)((char*)mapping + page - (uint64_t)n * sizeof(uint32_t)),
                    .temporary_order = (uint32_t*)((char*)mapping + 3 * page - (uint64_t)n * sizeof(uint32_t)),
                    .next_classes = (uint32_t*)((char*)mapping + 5 * page - (uint64_t)n * sizeof(uint32_t)),
                    .hashes = (uint64_t*)((char*)mapping + 7 * page - (uint64_t)n * sizeof(uint64_t)),
                    .fields = fields,
                };
                for (uint32_t i = 0; i < n; i += 1)
                {
                    members[i] = base != 0 ? n - 1 - i : i;
                    operations[i] = IR_ADD;
                    const uint32_t key = kind == 1 ? i % 7 : kind == 3 ? 0 : i;
                    for (uint32_t field = 0; field < BUSTER_IR_GVN_FIELDS; field += 1)
                    {
                        // In collision mode each field independently distinguishes some keys.
                        fields[(uint64_t)field * n + i] = kind == 2 ? (field == i % BUSTER_IR_GVN_FIELDS ? (uint64_t)i / BUSTER_IR_GVN_FIELDS + 1 : 0) : ((uint64_t)key << (field * 7)) ^ (UINT64_C(0x9e3779b97f4a7c15) * (field + 1));
                    }
                    work.hashes[i] = kind >= 2 ? 0 : ir_gvn_mix(BUSTER_IR_GVN_HASH_BASIS, key);
                }
                const uint32_t classes = ir_gvn_partition_group(&work, n, initial != 0, false, base);
                if (n != 0)
                {
                    memcpy(grouped, work.next_classes, (uint64_t)n * sizeof(uint32_t));
                }
                ir_optimize_test_expect(test, work.statistics.partition_probes <= (uint64_t)n * BUSTER_IR_GVN_PROBES_PER_INSTRUCTION, "GVN partition probing obeys its linear budget");
                const bool crowded = kind == 2 && n > 2 * BUSTER_IR_GVN_PROBES_PER_INSTRUCTION + 1;
                ir_optimize_test_expect(test, work.statistics.partition_sort_fallbacks == crowded, "GVN distinct full-hash collisions take bounded sort completion; duplicate classes do not");
                const uint32_t sorted = ir_gvn_partition_group(&work, n, initial != 0, true, base);
                ir_optimize_test_expect(test, classes == sorted, "GVN grouped and sorted partition counts agree");
                for (uint32_t i = 0; i < n; i += 1)
                {
                    ir_optimize_test_expect(test, grouped[i] > base && grouped[i] <= base + classes && work.next_classes[i] > base && work.next_classes[i] <= base + classes, "GVN partition publishes every class within its range");
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

static void ir_gvn_test_large_partition(IrOptimizeTest* test, Arena* input, Arena* vector, Arena* reference, Arena* scratch)
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
        ir_gvn_test_compare(test, &f, vector, reference, scratch, 1024, false, false);
        IrGvnResult result = { 0 };
        const IrCheck check = ir_optimize_gvn(&f, vector, scratch, ir_gvn_options(), &result);
        ir_optimize_test_expect(test, check.error == IR_OK && result.partition_sort_fallbacks == 0 && !result.fallback, "GVN ordinary large DAG needs no comparison-sort completion");
        ir_optimize_test_expect(test, check.error == IR_OK && result.acyclic == f.instruction_count && result.rounds == 0 && result.partition_probes <= (uint64_t)f.instruction_count * BUSTER_IR_GVN_PROBES_PER_INSTRUCTION, "GVN numbers a large DAG once, within one linear probe budget");
        IrGvnOptions options = ir_gvn_options();
        options.complete = true;
        IrGvnResult complete = { 0 };
        const IrCheck complete_check = ir_optimize_gvn(&f, vector, scratch, options, &complete);
        ir_optimize_test_expect(test, complete_check.error == IR_OK && complete.rounds > 2 && complete.partition_sort_fallbacks == 0 && complete.partition_probes <= (uint64_t)f.instruction_count * BUSTER_IR_GVN_PROBES_PER_INSTRUCTION * complete.rounds, "GVN complete refinement of a large DAG stays within its per-round budget");
    }
    arena_pop_to(vector, 0);
    arena_pop_to(input, 0);
}

// #866: a header phi whose back-edge input is computed in the latch, through a copy, from loop-invariant values. It
// reaches no cycle, so the acyclic numbering must number it once its input is, from the copy's users, and find it
// congruent to the phi whose matching input is computed before the loop. The counter is a true recurrence.
static bool ir_gvn_test_late_phi(Arena* arena, IrFunction* f)
{
    const IrSizes sizes = { .instruction_count = 17, .block_count = 4, .constant_count = 3, .parameter_count = 1, .extra_count = 12 };
    const bool created = ir_create(arena, &sizes, f) == IR_OK;
    if (created)
    {
        f->result_type = IR_I64;
        f->parameter_types[0] = IR_I64;
        f->block_offsets[1] = 6;
        f->block_offsets[2] = 11;
        f->block_offsets[3] = 15;
        f->block_offsets[4] = 17;
        f->constants[0].low = 0;
        f->constants[1].low = 1;
        f->constants[2].low = 5;
        ir_optimize_test_instruction(f, 0, IR_PARAM, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 1, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 2, IR_CONST, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 3, IR_CONST, IR_I64, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 4, IR_ADD, IR_I64, 0, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 5, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 6, IR_PHI, IR_I64, 0, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 7, IR_PHI, IR_I64, 4, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 8, IR_PHI, IR_I64, 8, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 9, IR_ICMP, IR_I1, 6, 3, IR_ULT);
        ir_optimize_test_instruction(f, 10, IR_CBR, IR_VOID, 9, 2, 3);
        ir_optimize_test_instruction(f, 11, IR_ADD, IR_I64, 6, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 12, IR_ADD, IR_I64, 0, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 13, IR_COPY, IR_I64, 12, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 14, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 15, IR_SUB, IR_I64, 8, 7, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 16, IR_RET, IR_VOID, 15, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t extra[] = { 0, 1, 2, 11, 0, 1, 2, 4, 0, 1, 2, 13 };
        memcpy(f->extra, extra, sizeof(extra));
    }
    return created;
}

// Two identical chains of additions. Synchronous refinement splits one link per round, so it needs a round per link
// over the whole function; the acyclic numbering visits each link once.
static bool ir_gvn_test_chains(Arena* arena, IrFunction* f, uint32_t length)
{
    const IrSizes sizes = { .instruction_count = length * 2 + 4, .block_count = 1, .constant_count = 1, .parameter_count = 1 };
    const bool created = ir_create(arena, &sizes, f) == IR_OK;
    if (created)
    {
        f->result_type = IR_I64;
        f->parameter_types[0] = IR_I64;
        f->block_offsets[1] = f->instruction_count;
        f->constants[0].low = 3;
        ir_optimize_test_instruction(f, 0, IR_PARAM, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 1, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        for (uint32_t link = 0; link < length; link += 1)
        {
            const uint32_t i = 2 + link * 2;
            ir_optimize_test_instruction(f, i, IR_ADD, IR_I64, link == 0 ? 0 : i - 2, 1, BUSTER_IR_NONE);
            ir_optimize_test_instruction(f, i + 1, IR_ADD, IR_I64, link == 0 ? 0 : i - 1, 1, BUSTER_IR_NONE);
        }
        ir_optimize_test_instruction(f, length * 2 + 2, IR_SUB, IR_I64, length * 2, length * 2 + 1, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, length * 2 + 3, IR_RET, IR_VOID, length * 2 + 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
    }
    return created;
}

// Unreachable code may use values defined in later blocks, here in two cycles of additions with no phi across two
// blocks. Only refinement can number them; optimistically all four additions are congruent, as complete refinement
// finds them.
static bool ir_gvn_test_dead_arithmetic(Arena* arena, IrFunction* f)
{
    const IrSizes sizes = { .instruction_count = 8, .block_count = 3, .constant_count = 1 };
    const bool created = ir_create(arena, &sizes, f) == IR_OK;
    if (created)
    {
        f->result_type = IR_I64;
        f->constants[0].low = 1;
        f->block_offsets[1] = 2;
        f->block_offsets[2] = 5;
        f->block_offsets[3] = 8;
        ir_optimize_test_instruction(f, 0, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 1, IR_RET, IR_VOID, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 2, IR_ADD, IR_I64, 5, 0, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 3, IR_ADD, IR_I64, 6, 0, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 4, IR_BR, IR_VOID, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 5, IR_ADD, IR_I64, 2, 0, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 6, IR_ADD, IR_I64, 3, 0, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 7, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
    }
    return created;
}

// A chain of loops, each a header of phis and a body that branches back to it. Phi inputs from the preheader are
// values dominating it; back-edge inputs are any value the body sees, so a phi may be a recurrence, a loop-invariant
// value computed before or inside the loop, or a copy. Bodies and phis repeat earlier ones on purpose.
static bool ir_gvn_test_random_loops(Arena* arena, IrFunction* f, IrOptimizeTest* test, uint32_t loops, uint32_t phis, uint32_t body)
{
    const uint32_t header = phis + 3;
    const uint32_t latch = body + 3;
    const IrSizes sizes = {
        .instruction_count = 7 + loops * (header + latch) + 2,
        .block_count = 2 + loops * 2,
        .constant_count = 3,
        .parameter_count = 1,
        .extra_count = loops * (phis + 1) * 4,
    };
    const bool created = ir_create(arena, &sizes, f) == IR_OK;
    if (created)
    {
        static const uint8_t operations[] = { IR_ADD, IR_SUB, IR_MUL, IR_AND, IR_OR, IR_XOR };
        uint32_t outer[64];
        uint32_t outer_count = 0;
        uint32_t inner[64];
        f->result_type = IR_I64;
        f->parameter_types[0] = IR_I64;
        f->constants[0].low = 0;
        f->constants[1].low = 1;
        f->constants[2].low = 3;
        ir_optimize_test_instruction(f, 0, IR_PARAM, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 1, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 2, IR_CONST, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 3, IR_CONST, IR_I64, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 4, IR_ADD, IR_I64, 0, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 5, IR_XOR, IR_I64, 4, 0, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 6, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t entry_values[] = { 0, 1, 2, 4, 5 };
        for (uint32_t value = 0; value < BUSTER_ARRAY_LENGTH(entry_values); value += 1)
        {
            outer[outer_count++] = entry_values[value];
        }
        f->block_offsets[1] = 7;
        uint32_t extra = 0;
        for (uint32_t loop = 0; loop < loops; loop += 1)
        {
            const uint32_t head_block = 1 + loop * 2;
            const uint32_t body_block = head_block + 1;
            const uint32_t preheader = loop == 0 ? 0 : head_block - 2;
            const uint32_t first = f->block_offsets[head_block];
            const uint32_t start = first + header;
            f->block_offsets[body_block] = start;
            f->block_offsets[body_block + 1] = start + latch;
            uint32_t inner_count = 0;
            for (uint32_t value = 0; value < outer_count; value += 1)
            {
                inner[inner_count++] = outer[value];
            }
            for (uint32_t phi = 0; phi <= phis; phi += 1)
            {
                inner[inner_count++] = first + phi;
            }
            // Body values first, so phi back-edge inputs can be chosen from all of them.
            for (uint32_t k = 0; k < body; k += 1)
            {
                const uint32_t i = start + k;
                if (k != 0 && ir_optimize_test_random(test) % 4 == 0)
                {
                    ir_optimize_test_instruction(f, i, f->op[i - 1], IR_I64, f->a[i - 1], f->b[i - 1], BUSTER_IR_NONE);
                }
                else
                {
                    const uint8_t op = operations[ir_optimize_test_random(test) % 6];
                    ir_optimize_test_instruction(f, i, op, IR_I64, inner[ir_optimize_test_random(test) % inner_count], inner[ir_optimize_test_random(test) % inner_count], BUSTER_IR_NONE);
                }
                inner[inner_count++] = i;
            }
            const uint32_t copy = start + body + 1;
            ir_optimize_test_instruction(f, start + body, IR_ADD, IR_I64, first, 2, BUSTER_IR_NONE);
            ir_optimize_test_instruction(f, copy, IR_COPY, IR_I64, inner[ir_optimize_test_random(test) % inner_count], BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_optimize_test_instruction(f, start + body + 2, IR_BR, IR_VOID, head_block, BUSTER_IR_NONE, BUSTER_IR_NONE);
            inner[inner_count++] = copy;
            // The counter, then phis that may repeat the previous one.
            for (uint32_t phi = 0; phi <= phis; phi += 1)
            {
                const uint32_t i = first + phi;
                ir_optimize_test_instruction(f, i, IR_PHI, IR_I64, extra, 2, BUSTER_IR_NONE);
                const bool repeat = phi > 1 && ir_optimize_test_random(test) % 3 == 0;
                f->extra[extra] = preheader;
                f->extra[extra + 1] = phi == 0 ? 1 : repeat ? f->extra[extra - 3] : outer[ir_optimize_test_random(test) % outer_count];
                f->extra[extra + 2] = body_block;
                f->extra[extra + 3] = phi == 0 ? start + body : repeat ? f->extra[extra - 1] : inner[ir_optimize_test_random(test) % inner_count];
                extra += 4;
            }
            ir_optimize_test_instruction(f, first + phis + 1, IR_ICMP, IR_I1, first, 3, IR_ULT);
            ir_optimize_test_instruction(f, first + phis + 2, IR_CBR, IR_VOID, first + phis + 1, body_block, body_block + 1);
            // Headers dominate everything after them; bodies do not.
            for (uint32_t phi = 0; phi <= phis && outer_count < BUSTER_ARRAY_LENGTH(outer); phi += 1)
            {
                outer[outer_count++] = first + phi;
            }
        }
        const uint32_t exit = f->block_offsets[f->block_count - 1];
        f->block_offsets[f->block_count] = exit + 2;
        ir_optimize_test_instruction(f, exit, IR_XOR, IR_I64, outer[outer_count - 1], outer[outer_count - 2], BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, exit + 1, IR_RET, IR_VOID, exit, BUSTER_IR_NONE, BUSTER_IR_NONE);
    }
    return created;
}

static void ir_gvn_test_acyclic(IrOptimizeTest* test, Arena* input, Arena* vector, Arena* reference, Arena* scratch)
{
    IrFunction late = { 0 };
    const bool late_created = ir_gvn_test_late_phi(input, &late);
    ir_optimize_test_expect(test, late_created, "GVN late phi fixture allocation");
    if (late_created)
    {
        ir_gvn_test_compare(test, &late, vector, reference, scratch, 3, true, true);
        IrGvnResult result = { 0 };
        const IrCheck check = ir_optimize_gvn(&late, vector, scratch, ir_gvn_options(), &result);
        // Only the counter, its increment and its comparison reach the recurrence.
        ir_optimize_test_expect(test, check.error == IR_OK && result.acyclic == late.instruction_count - 3, "GVN wakes a phi through its back-edge input's copy");
        arena_pop_to(vector, 0);
    }
    arena_pop_to(input, 0);
    for (uint32_t length = 1; length <= 256; length *= 4)
    {
        IrFunction chains = { 0 };
        const bool created = ir_gvn_test_chains(input, &chains, length);
        ir_optimize_test_expect(test, created, "GVN chain fixture allocation");
        if (created)
        {
            ir_gvn_test_compare(test, &chains, vector, reference, scratch, length, false, false);
            IrGvnOptions options = ir_gvn_options();
            options.complete = true;
            IrGvnResult complete = { 0 };
            const IrCheck check = ir_optimize_gvn(&chains, vector, scratch, options, &complete);
            ir_optimize_test_expect(test, check.error == IR_OK && complete.rounds > length, "GVN complete refinement splits a chain one link per round");
            arena_pop_to(vector, 0);
        }
        arena_pop_to(input, 0);
    }
    IrFunction dead = { 0 };
    const bool dead_created = ir_gvn_test_dead_arithmetic(input, &dead);
    ir_optimize_test_expect(test, dead_created, "GVN dead arithmetic fixture allocation");
    if (dead_created)
    {
        ir_gvn_test_compare(test, &dead, vector, reference, scratch, 4, false, true);
        IrGvnResult result = { 0 };
        const IrCheck check = ir_optimize_gvn(&dead, vector, scratch, ir_gvn_options(), &result);
        ir_optimize_test_expect(test, check.error == IR_OK && result.acyclic == 4 && result.classes == 5, "GVN refines congruent unreachable cycles together");
        arena_pop_to(vector, 0);
    }
    arena_pop_to(input, 0);
    for (uint32_t trial = 0; trial < 160; trial += 1)
    {
        const uint32_t loops = 1 + trial % 4;
        const uint32_t phis = 1 + (trial / 4) % 4;
        const uint32_t body = (trial / 16) % 10;
        IrFunction f = { 0 };
        const bool created = ir_gvn_test_random_loops(input, &f, test, loops, phis, body);
        ir_optimize_test_expect(test, created, "GVN random loop fixture allocation");
        if (created)
        {
            ir_gvn_test_compare(test, &f, vector, reference, scratch, 0, false, true);
        }
        arena_pop_to(input, 0);
    }
}

// Exercise both rewrites on side tables whose entries can be shared even though each output row
// needs its own operand slice. Include indirect labels, dead blocks and a switch with repeated targets.
static void ir_gvn_test_structured_rewrite(IrOptimizeTest* test, Arena* input, Arena* general, Arena* compact, Arena* scratch)
{
    for (uint32_t kind = 0; kind < 6; kind += 1)
    {
        IrFunction f = { 0 };
        const bool created = kind < 2 ? ir_optimize_test_switch(input, &f, kind != 0) :
                             kind == 2 ? ir_optimize_test_aggregate_call(input, &f) : ir_optimize_test_labels(input, &f, kind - 3);
        ir_optimize_test_expect(test, created, "GVN structured operand fixture allocation");
        if (created)
        {
            if (kind == 2)
            {
                // The second call argument uses a copy that GVN removes before rebuilding the call's operand slice.
                f.op[1] = IR_COPY;
                f.extra[1] = 1;
            }
            IrGvnOptions options = ir_gvn_options();
            IrGvnResult a = { 0 };
            IrGvnResult b = { 0 };
            const IrCheck ac = ir_optimize_gvn(&f, general, scratch, options, &a);
            options.apply_only = true;
            const IrCheck bc = ir_optimize_gvn(&f, compact, scratch, options, &b);
            ir_optimize_test_expect(test, ac.error == IR_OK && bc.error == IR_OK && ir_optimize_test_equal(&a.function, &b.function) &&
                                    ir_verify(&b.function, scratch, NULL).error == IR_OK,
                                    "GVN retains exact operands, labels and ABI metadata through row compaction");
        }
        arena_pop_to(input, 0);
        arena_pop_to(general, 0);
        arena_pop_to(compact, 0);
    }
}

// Pressure keeps equal constants available for rematerialization. Their users still share a class.
static void ir_gvn_test_pressure_constants(IrOptimizeTest* test, Arena* input, Arena* output, Arena* scratch)
{
    static const char text[] = "function (i32) -> i32 {\n"
                               "b0:\n"
                               "  %0 = param.i32 0\n"
                               "  %1 = const.i32 0x00000000000000000000000000000007\n"
                               "  %2 = const.i32 0x00000000000000000000000000000007\n"
                               "  %3 = add.i32 %0, %1\n"
                               "  %4 = add.i32 %0, %2\n"
                               "  %5 = sub.i32 %4, %3\n"
                               "  %6 = ret.void %5\n"
                               "}\n";
    IrFunction f = { 0 };
    const IrParseResult parsed = ir_parse((String8){ .pointer = (char*)text, .length = sizeof(text) - 1 }, input, scratch, &f);
    const bool ready = parsed.error == IR_PARSE_OK && parsed.check.error == IR_OK;
    ir_optimize_test_expect(test, ready, "GVN duplicate-constant pressure fixture parses");
    if (ready)
    {
        IrGvnOptions options = ir_gvn_options();
        options.pressure = true;
        options.apply_only = true;
        IrGvnResult result = { 0 };
        const IrCheck check = ir_optimize_gvn(&f, output, scratch, options, &result);
        ir_optimize_test_expect(test, check.error == IR_OK && result.removed == 1 && result.function.instruction_count == 6 &&
                                result.function.op[1] == IR_CONST && result.function.op[2] == IR_CONST &&
                                result.function.a[4] == 3 && result.function.b[4] == 3,
                                "GVN pressure preserves both constants and forwards the second addition");
    }
    arena_pop_to(input, 0);
    arena_pop_to(output, 0);
}

static void ir_gvn_test_ir(IrOptimizeTest* test)
{
    Arena input = arena_reserve(16ull << 20);
    Arena vector = arena_reserve(16ull << 20);
    Arena reference = arena_reserve(16ull << 20);
    Arena scratch = arena_reserve(32ull << 20);
    const bool ready = input.base != 0 && vector.base != 0 && reference.base != 0 && scratch.base != 0;
    ir_optimize_test_expect(test, ready, "GVN fixture arenas");
    if (ready)
    {
        ir_gvn_test_pressure_constants(test, &input, &vector, &scratch);
        ir_gvn_test_structured_rewrite(test, &input, &vector, &reference, &scratch);
        for (uint32_t kind = 0; kind < 3; kind += 1)
        {
            IrFunction f = { 0 };
            const bool created = ir_gvn_test_straight(&input, &f, kind);
            ir_optimize_test_expect(test, created, "GVN straight fixture allocation");
            if (created)
            {
                ir_gvn_test_compare(test, &f, &vector, &reference, &scratch, kind == 1 ? 0 : 2, false, false);
                if (kind == 0)
                {
                    ir_gvn_test_debug_bindings(test, &f, &input, &vector, &reference, &scratch);
                }
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
            ir_gvn_test_compare(test, &cycle, &vector, &reference, &scratch, 4, true, true);
            // Different initial values force the optimistic class to split through the entire recurrence.
            cycle.extra[11] = 1;
            ir_gvn_test_compare(test, &cycle, &vector, &reference, &scratch, 2, true, true);
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
            ir_gvn_test_compare(test, &diamond, &vector, &reference, &scratch, 0, false, false);
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
                ir_gvn_test_compare(test, &f, &vector, &reference, &scratch, 0, false, false);
            }
            arena_pop_to(&input, 0);
        }
        ir_gvn_test_dead_cycle(test, &input, &vector, &reference, &scratch);
        ir_gvn_test_wait_growth(test, &input, &vector, &reference, &scratch);
        ir_gvn_test_limits(test, &input, &vector, &scratch);
        ir_gvn_test_large_partition(test, &input, &vector, &reference, &scratch);
        ir_gvn_test_acyclic(test, &input, &vector, &reference, &scratch);
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
                ir_gvn_test_compare(test, &f, &vector, &reference, &scratch, 0, false, false);
            }
            arena_pop_to(&input, 0);
        }
    }
    arena_release(&scratch);
    arena_release(&reference);
    arena_release(&vector);
    arena_release(&input);
}

static void ir_gvn_test_simd(IrOptimizeTest* test)
{
    Arena input = arena_reserve(UINT64_C(16) << 20);
    Arena vector = arena_reserve(UINT64_C(16) << 20);
    Arena reference = arena_reserve(UINT64_C(16) << 20);
    Arena scratch = arena_reserve(UINT64_C(16) << 20);
    const IrSizes sizes = {
        .instruction_count = 14,
        .block_count = 1,
        .vector_constant_count = 3,
    };
    IrFunction f = { 0 };
    const bool ready = input.base != NULL && vector.base != NULL && reference.base != NULL && scratch.base != NULL && ir_create(&input, &sizes, &f) == IR_OK;
    ir_optimize_test_expect(test, ready, "GVN SIMD fixture allocation");
    if (ready)
    {
        f.result_type = IR_M64;
        f.block_offsets[1] = 14;
        memset(f.vector_constants[0].words, 0xff, sizeof(IrSimdValue));
        f.vector_constants[1] = f.vector_constants[0];
        f.vector_constants[1].words[7] ^= UINT64_C(0x0101010101010101);
        f.vector_constants[2] = f.vector_constants[0];
        ir_optimize_test_instruction(&f, 0, IR_VCONST, IR_V16I32, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(&f, 1, IR_VCONST, IR_V16I32, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(&f, 2, IR_VCONST, IR_V16I32, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(&f, 3, IR_ADD, IR_V16I32, 0, 0, BUSTER_IR_NONE);
        ir_optimize_test_instruction(&f, 4, IR_ADD, IR_V16I32, 2, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(&f, 5, IR_ADD, IR_V16I32, 0, 0, BUSTER_IR_NONE);
        f.flags[3] = IR_SIMD_LANE_I8;
        f.flags[4] = IR_SIMD_LANE_I8;
        f.flags[5] = IR_SIMD_LANE_I16;
        ir_optimize_test_instruction(&f, 6, IR_VCMP, IR_M64, 3, 4, IR_EQ);
        ir_optimize_test_instruction(&f, 7, IR_VCMP, IR_M64, 0, 1, IR_NE);
        f.flags[6] = IR_SIMD_LANE_I8;
        f.flags[7] = IR_SIMD_LANE_I8;
        ir_optimize_test_instruction(&f, 8, IR_AND, IR_M64, 6, 7, BUSTER_IR_NONE);
        for (uint32_t i = 9; i < 13; i += 1)
        {
            ir_optimize_test_instruction(&f, i, i < 11 ? IR_FADD : IR_VCMP, i < 11 ? IR_V16I32 : IR_M16, 0, 1, i < 11 ? BUSTER_IR_NONE : IR_EQ);
            f.flags[i] = IR_SIMD_LANE_F32;
        }
        ir_optimize_test_instruction(&f, 13, IR_RET, IR_VOID, 8, BUSTER_IR_NONE, BUSTER_IR_NONE);
        for (uint32_t path = 0; path < 2; path += 1)
        {
            IrGvnOptions options = ir_gvn_options();
            options.complete = path != 0;
            IrGvnResult result = { 0 };
            Arena* output = path != 0 ? &reference : &vector;
            const IrCheck check = ir_optimize_gvn(&f, output, &scratch, options, &result);
            ir_optimize_test_expect(test, check.error == IR_OK, "GVN takes vector constants, lane operations and masks");
            if (check.error == IR_OK)
            {
                uint32_t constants = 0;
                uint32_t byte_adds = 0;
                uint32_t short_adds = 0;
                uint32_t floating = 0;
                for (uint32_t i = 0; i < result.function.instruction_count; i += 1)
                {
                    constants += result.function.op[i] == IR_VCONST;
                    byte_adds += result.function.op[i] == IR_ADD && result.function.flags[i] == IR_SIMD_LANE_I8;
                    short_adds += result.function.op[i] == IR_ADD && result.function.flags[i] == IR_SIMD_LANE_I16;
                    floating += ir_optimize_simd_effect(&result.function, i);
                }
                ir_optimize_test_expect(test, constants == 2 && byte_adds == 1 && short_adds == 1 && floating == 4 && result.removed == 2,
                                        "GVN merges identical payloads and lanes, keeps high-word differences, lane widths and floating effects");
                ir_optimize_test_simd_semantics(test, &f, &result.function, &scratch);
            }
        }
        // Full equality is mandatory even if an adversarial hash collides on constants differing only in word 7.
        uint64_t fields[14 * BUSTER_IR_GVN_FIELDS] = { 0 };
        uint64_t hashes[14] = { 0 };
        uint32_t aliases[14] = { 0 };
        uint32_t classes[14] = { 0 };
        uint8_t pure[14] = { 0 };
        IrGvnWork work = {
            .ir.function = &f,
            .fields = fields,
            .hashes = hashes,
            .alias = aliases,
            .classes = classes,
            .pure = pure,
        };
        pure[0] = 1;
        pure[1] = 1;
        ir_gvn_key(&work, 0, 0, true);
        ir_gvn_key(&work, 1, 1, true);
        ir_optimize_test_expect(test, ir_gvn_compare(&work, 0, 1, 0, 1, true) != 0, "a forced GVN hash collision still compares the complete vector payload");
    }
    arena_release(&input);
    arena_release(&vector);
    arena_release(&reference);
    arena_release(&scratch);
}

// IrGvnOptions.vectors (#2781): equal vector operations merge, but the equal scalar operations that feed their splats
// stay distinct, although their congruence can still prove the vector users congruent. Complete refinement checks both policies.
static void ir_gvn_test_vectors(IrOptimizeTest* test)
{
    static const char text[] = "function (i32) -> m16 {\n"
                               "b0:\n"
                               "  %0 = param.i32 0\n"
                               "  %1 = add.i32 %0, %0\n"
                               "  %2 = add.i32 %0, %0\n"
                               "  %3 = copy.i32 %2\n"
                               "  %4 = intrinsic.v4i32 %1 splat\n"
                               "  %5 = intrinsic.v4i32 %3 splat\n"
                               "  %6 = add.v4i32 %4, %5\n"
                               "  %7 = add.v4i32 %4, %5\n"
                               "  %8 = vcmp.m16 %6, %7 eq lanes=i8\n"
                               "  %9 = ret.void %8\n"
                               "}\n";
    Arena input = arena_reserve(UINT64_C(16) << 20);
    Arena vector = arena_reserve(UINT64_C(16) << 20);
    Arena reference = arena_reserve(UINT64_C(16) << 20);
    Arena scratch = arena_reserve(UINT64_C(16) << 20);
    IrFunction f = { 0 };
    const bool ready = input.base != NULL && vector.base != NULL && reference.base != NULL && scratch.base != NULL;
    const IrParseResult parsed = ready ? ir_parse((String8){ .pointer = (char*)text, .length = sizeof(text) - 1 }, &input, &scratch, &f) : (IrParseResult){ .error = IR_PARSE_END };
    const bool fixture = parsed.error == IR_PARSE_OK && parsed.check.error == IR_OK;
    ir_optimize_test_expect(test, fixture, "GVN's vector-only policy fixture parses");
    IrGvnResult results[2][2] = { 0 };
    for (uint32_t policy = 0; fixture && policy < 2; policy += 1)
    {
        for (uint32_t path = 0; path < 2; path += 1)
        {
            IrGvnOptions options = ir_gvn_options();
            options.complete = path != 0;
            options.vectors = policy != 0;
            Arena* output = path != 0 ? &reference : &vector;
            const IrCheck check = ir_optimize_gvn(&f, output, &scratch, options, &results[policy][path]);
            const uint32_t expected = policy != 0 ? 2 : 4;
            ir_optimize_test_expect(test, check.error == IR_OK && results[policy][path].removed == expected,
                                    policy != 0 ? "GVN's vector-only policy merges the equal splat and vector addition" : "GVN unrestricted merges the scalar, splat and vector duplicates");
            if (check.error == IR_OK)
            {
                uint32_t scalar_adds = 0;
                uint32_t vector_adds = 0;
                for (uint32_t i = 0; i < results[policy][path].function.instruction_count; i += 1)
                {
                    scalar_adds += results[policy][path].function.op[i] == IR_ADD && results[policy][path].function.type[i] == IR_I32;
                    vector_adds += results[policy][path].function.op[i] == IR_ADD && results[policy][path].function.type[i] == IR_V4I32;
                }
                ir_optimize_test_expect(test, policy == 0 || (scalar_adds == 2 && vector_adds == 1), "GVN's vector-only policy keeps both scalar additions");
                ir_optimize_test_simd_semantics(test, &f, &results[policy][path].function, &scratch);
            }
        }
        ir_optimize_test_expect(test, results[policy][0].function.instruction_count != 0 && results[policy][1].function.instruction_count != 0 &&
                                    ir_optimize_test_equal(&results[policy][0].function, &results[policy][1].function), "GVN's vector-only policy agrees with complete refinement");
    }
    arena_release(&input);
    arena_release(&vector);
    arena_release(&reference);
    arena_release(&scratch);
}

// The width changes (#1840) on values SCCP can't fold, a parameter's splat: GVN merges an insert, an extract and a widen
// equal to another, keeps the insert and the extract of another part (the intrinsic's flags) apart under both refinement modes, and the
// interpreter's mask is unchanged.
static void ir_gvn_test_widths(IrOptimizeTest* test)
{
    static const char text[] = "function (i32) -> m16 {\n"
                               "b0:\n"
                               "  %0 = param.i32 0\n"
                               "  %1 = intrinsic.v4i32 %0 splat\n"
                               "  %2 = add.v4i32 %1, %1\n"
                               "  %3 = intrinsic.v8i32 %1 widen\n"
                               "  %4 = intrinsic.v8i32 %3, %2 insert 1\n"
                               "  %5 = intrinsic.v8i32 %3, %2 insert 1\n"
                               "  %6 = intrinsic.v8i32 %3, %2 insert 0\n"
                               "  %7 = intrinsic.v4i32 %4 extract 0\n"
                               "  %8 = intrinsic.v4i32 %5 extract 0\n"
                               "  %9 = intrinsic.v4i32 %4 extract 1\n"
                               "  %10 = intrinsic.v8i32 %1 widen\n"
                               "  %11 = intrinsic.v4i32 %6 extract 0\n"
                               "  %12 = intrinsic.v4i32 %10 extract 1\n"
                               "  %13 = add.v4i32 %7, %8\n"
                               "  %14 = add.v4i32 %13, %9\n"
                               "  %15 = add.v4i32 %14, %11\n"
                               "  %16 = add.v4i32 %15, %12\n"
                               "  %17 = vcmp.m16 %16, %2 ult lanes=i8\n"
                               "  %18 = ret.void %17\n"
                               "}\n";
    Arena input = arena_reserve(UINT64_C(16) << 20);
    Arena output = arena_reserve(UINT64_C(16) << 20);
    Arena scratch = arena_reserve(UINT64_C(16) << 20);
    IrFunction f = { 0 };
    const bool ready = input.base != NULL && output.base != NULL && scratch.base != NULL;
    const IrParseResult parsed = ready ? ir_parse((String8){ .pointer = (char*)text, .length = sizeof(text) - 1 }, &input, &scratch, &f) : (IrParseResult){ .error = IR_PARSE_END };
    ir_optimize_test_expect(test, parsed.error == IR_PARSE_OK && parsed.check.error == IR_OK, "GVN's width change fixture parses");
    for (uint32_t path = 0; parsed.error == IR_PARSE_OK && parsed.check.error == IR_OK && path < 2; path += 1)
    {
        IrGvnOptions options = ir_gvn_options();
        options.complete = path != 0;
        IrGvnResult result = { 0 };
        const IrCheck check = ir_optimize_gvn(&f, &output, &scratch, options, &result);
        ir_optimize_test_expect(test, check.error == IR_OK, "GVN takes the width changes");
        if (check.error == IR_OK)
        {
            // Per kind and flags: inserts of part 0 and 1, extracts of part 0 and 1, widens.
            uint32_t counts[5] = { 0 };
            for (uint32_t i = 0; i < result.function.instruction_count; i += 1)
            {
                const bool intrinsic = result.function.op[i] == IR_INTRINSIC;
                const uint32_t kind = intrinsic ? result.function.c[i] : 0;
                const uint32_t part = result.function.flags[i];
                counts[0] += intrinsic && kind == BUSTER_IR_INTRINSIC_INSERT && part == 0;
                counts[1] += intrinsic && kind == BUSTER_IR_INTRINSIC_INSERT && part == 1;
                counts[2] += intrinsic && kind == BUSTER_IR_INTRINSIC_EXTRACT && part == 0;
                counts[3] += intrinsic && kind == BUSTER_IR_INTRINSIC_EXTRACT && part == 1;
                counts[4] += intrinsic && kind == BUSTER_IR_INTRINSIC_WIDEN;
            }
            ir_optimize_test_expect(test, result.removed == 3 && counts[0] == 1 && counts[1] == 1 && counts[2] == 2 && counts[3] == 2 && counts[4] == 1,
                                    "GVN merges equal width changes and keeps those of another part apart");
            ir_optimize_test_simd_semantics(test, &f, &result.function, &scratch);
        }
        arena_pop_to(&output, 0);
    }
    arena_release(&input);
    arena_release(&output);
    arena_release(&scratch);
}

static __attribute__((unused)) int run_ir_gvn_tests(int argc, char** argv)
{
    IrOptimizeTest test = { .random = 139, .gvn_digest = BUSTER_IR_OPTIMIZE_TEST_DIGEST_BASIS };
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
        const bool frozen = test.random == 139;
        ir_gvn_test_guards(&test);
        ir_gvn_test_partitions(&test);
        ir_gvn_test_ir(&test);
        ir_gvn_test_simd(&test);
        ir_gvn_test_vectors(&test);
        ir_gvn_test_widths(&test);
        if (frozen)
        {
            ir_optimize_test_expect(&test, test.gvn_digest == BUSTER_IR_OPTIMIZE_TEST_GVN_DIGEST, "GVN rewrites match the frozen deterministic corpus");
        }
        printf("ir-gvn: %" PRIu64 " checks, %" PRIu64 " failures\n", test.checks, test.failures);
        result = test.failures == 0 ? 0 : 1;
    }
    else
    {
        fprintf(stderr, "usage: test-ir-gvn [--seed N]\n");
    }
    return result;
}
