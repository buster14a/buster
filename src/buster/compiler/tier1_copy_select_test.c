// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #2818: the backend unit's paired-copy selection checks. Guarded native copies check the two narrow loads' bytes
// independently of their records; the IR fixtures check that provenance preserves scalar folding and selects halves.

static void tier1_copy_select_records(Tier1SelectTest* test)
{
    const long page = sysconf(_SC_PAGESIZE);
    const size_t bytes = page > 0 ? (size_t)page : 4096;
    uint8_t* source = mmap(0, 3 * bytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint8_t* destination = mmap(0, 3 * bytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint8_t* expected = malloc(bytes);
    uint8_t* native = malloc(X86_FORM_COUNT);
    const bool ready = source != MAP_FAILED && destination != MAP_FAILED && expected != 0 && native != 0 &&
                       mprotect(source + bytes, bytes, PROT_READ | PROT_WRITE) == 0 && mprotect(destination + bytes, bytes, PROT_READ | PROT_WRITE) == 0;
    tier1_select_test_expect(test, ready, "allocate guarded paired-copy source and destination");
    if (ready)
    {
        const X86Target host = x86_target_native();
        x86_target_forms(&host, native);
        test->native_allowed = native;
        uint64_t random = UINT64_C(0x2818b3ef79a15cd1);
        for (size_t b = 0; b < bytes; b += 1)
        {
            source[bytes + b] = (uint8_t)tier1_select_test_random(&random);
        }
        static const char* const targets[3] = {
            "x86-64",
            "x86-64-v3",
            "x86-64-v4",
        };
        static const int32_t displacements[3] = {
            -129,
            0,
            127,
        };
        for (uint32_t target_index = 0; target_index < BUSTER_ARRAY_LENGTH(targets); target_index += 1)
        {
            // measure-tier1-model's manifest contains baseline forms only. Keep the AVX targets' native and coverage
            // checks, but not their rows or accounting, as tier1_select_test_intrinsics does for its V3 fixtures.
            const bool manifest = test->manifest;
            uint64_t emitted_records[3];
            uint64_t unknown_costs[3];
            uint64_t missing_columns[3][X86_COST_COLUMN_COUNT];
            memcpy(emitted_records, test->emitted_records, sizeof(emitted_records));
            memcpy(unknown_costs, test->unknown_costs, sizeof(unknown_costs));
            memcpy(missing_columns, test->missing_columns, sizeof(missing_columns));
            test->manifest = manifest && target_index == 0;
            X86Target target;
            const bool parsed = x86_target_parse(targets[target_index], strlen(targets[target_index]), &target);
            tier1_select_test_expect(test, parsed, "paired-copy target parses");
            if (parsed)
            {
                x86_target_forms(&target, test->allowed);
            }
            for (uint32_t reg = 0; parsed && reg < 16; reg += 1)
            {
                for (uint32_t shape = 0; shape < 6; shape += 1)
                {
                    const bool indexed = (shape & 1u) != 0;
                    const uint32_t scale = shape & 3u;
                    const int32_t displacement = displacements[shape / 2];
                    const Tier1SelectHoles holes = {
                        .rule = indexed ? TIER1_RULE_FLOAT_LOAD_ADDRESS : TIER1_RULE_FLOAT_LOAD,
                        .memory = {
                            .displacement = displacement,
                            .rm = X86_REGISTER_RDI,
                            .index = indexed ? X86_REGISTER_RDX : 0,
                            .mode = indexed ? (uint8_t)BUSTER_X86_MEMORY_BASE_INDEX(scale) : BUSTER_X86_MEMORY_BASE,
                        },
                        .width = 16,
                        .source_width = 8,
                        .destination = (uint8_t)(32 + reg),
                        .vex = target_index == 1,
                        .sse = target_index == 0,
                    };
                    const Tier1SelectRecords selected = tier1_select_test_expand(test, &holes);
                    const uint16_t low = target_index == 0 ? X86_FORM_MOVSD_X_XM64 : X86_FORM_VMOVSD_X_M64;
                    const uint16_t high = target_index == 0 ? X86_FORM_MOVHPS_X_M64 : X86_FORM_VMOVHPS_X_X_M64;
                    const bool exact = selected.count == 2 && selected.records[0].form == low && selected.records[1].form == high &&
                                       selected.records[0].reg == reg && selected.records[1].reg == reg && selected.records[1].vvvv == (target_index == 0 ? 0 : reg) &&
                                       selected.records[0].displacement == displacement && selected.records[1].displacement == displacement + 8 &&
                                       selected.records[0].rm == X86_REGISTER_RDI && selected.records[1].rm == X86_REGISTER_RDI &&
                                       selected.records[0].mode == holes.memory.mode && selected.records[1].mode == holes.memory.mode &&
                                       selected.records[0].index == holes.memory.index && selected.records[1].index == holes.memory.index;
                    tier1_select_test_expect(test, exact, "paired-copy selection preserves both narrow addresses and the high-half merge");
                    if (exact)
                    {
                        X86Instruction records[4] = {
                            selected.records[0],
                            selected.records[1],
                            {
                                .form = target_index == 0 ? X86_FORM_MOVUPS_XM_X : target_index == 1 ? X86_FORM_VMOVDQU_XM128_X : X86_FORM_VMOVDQU32_XM128_X,
                                .reg = (uint8_t)reg,
                                .rm = X86_REGISTER_RSI,
                                .mode = BUSTER_X86_MEMORY_BASE,
                            },
                            { .form = X86_FORM_RET },
                        };
                        if (tier1_select_test_load_records(test, records, BUSTER_ARRAY_LENGTH(records), 0, 3, false))
                        {
                            const size_t positions[4] = {
                                0,
                                1,
                                bytes - 17,
                                bytes - 16,
                            };
                            for (uint32_t position = 0; position < BUSTER_ARRAY_LENGTH(positions); position += 1)
                            {
                                const size_t source_offset = positions[position];
                                const size_t destination_offset = positions[3 - position];
                                memset(destination + bytes, 0x5a, bytes);
                                memset(expected, 0x5a, bytes);
                                memcpy(expected + destination_offset, source + bytes + source_offset, 16);
                                const uint64_t index = indexed ? 3 : 0;
                                const uint64_t base = (uint64_t)(uintptr_t)(source + bytes + source_offset) - (uint64_t)(int64_t)displacement - (index << scale);
                                (void)tier1_select_test_call(test, base, (uintptr_t)(destination + bytes + destination_offset), index);
                                tier1_select_test_expect(test, memcmp(destination + bytes, expected, bytes) == 0,
                                                         "paired native copy reads exactly 16 guarded bytes and stores exactly those bytes");
                            }
                        }
                    }
                }
            }
            test->manifest = manifest;
            if (target_index != 0)
            {
                memcpy(test->emitted_records, emitted_records, sizeof(emitted_records));
                memcpy(test->unknown_costs, unknown_costs, sizeof(unknown_costs));
                memcpy(test->missing_columns, missing_columns, sizeof(missing_columns));
            }
        }
        test->native_allowed = 0;
        const X86Target baseline = { .architecture = X86_ARCH_BASELINE };
        x86_target_forms(&baseline, test->allowed);
    }
    Tier1SelectHoles edge = {
        .rule = TIER1_RULE_FLOAT_LOAD_ADDRESS,
        .memory = {
            .displacement = INT32_MAX - 8,
            .rm = X86_REGISTER_RDI,
            .mode = BUSTER_X86_MEMORY_BASE,
        },
        .width = 16,
        .source_width = 8,
        .destination = 32,
        .sse = true,
    };
    const Tier1SelectRecords fits = tier1_select_expand(&edge);
    tier1_select_test_expect(test, fits.count == 2 && !fits.overflowed && fits.records[1].displacement == INT32_MAX,
                             "paired-copy high half fits the last signed displacement");
    edge.memory.displacement += 1;
    const Tier1SelectRecords overflowed = tier1_select_expand(&edge);
    tier1_select_test_expect(test, overflowed.overflowed && overflowed.count == 0, "paired-copy displacement overflow is refused before either load");
    edge.memory.displacement = 0;
    edge.destination = BUSTER_TIER1_SELECT_NO_REGISTER;
    const Tier1SelectRecords unused = tier1_select_expand(&edge);
    tier1_select_test_expect(test, !unused.overflowed && unused.count == 2 && unused.records[0].reg == BUSTER_TIER1_SELECT_FLOAT_SCRATCH &&
                             unused.records[1].reg == BUSTER_TIER1_SELECT_FLOAT_SCRATCH,
                             "an unused paired-copy load keeps both accesses in the reserved vector scratch");
    edge.destination = 0;
    const Tier1SelectRecords physical = tier1_select_expand(&edge);
    tier1_select_test_expect(test, !physical.overflowed && physical.count == 2 && physical.records[0].reg == 0 && physical.records[1].reg == 0,
                             "paired-copy expansion also accepts a physical low vector register");
    static const uint8_t invalid_locations[] = { 16, 31, 48, 63, 64, 254 };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(invalid_locations); i += 1)
    {
        edge.destination = invalid_locations[i];
        const Tier1SelectRecords invalid = tier1_select_expand(&edge);
        tier1_select_test_expect(test, invalid.overflowed && invalid.count == 0, "paired-copy rejects a location outside the low vector class");
    }
    if (source != MAP_FAILED)
    {
        munmap(source, 3 * bytes);
    }
    if (destination != MAP_FAILED)
    {
        munmap(destination, 3 * bytes);
    }
    free(expected);
    free(native);
}

static void tier1_copy_select_plans(Tier1SelectTest* test)
{
    Arena input = arena_reserve(1u << 20);
    Arena output = arena_reserve(4u << 20);
    Arena scratch = arena_reserve(4u << 20);
    for (uint32_t vector = 0; vector < 2; vector += 1)
    {
        for (uint32_t marked = 0; marked < 3; marked += 1)
        {
            IrFunction function;
            const IrSizes sizes = {
                .instruction_count = 5,
                .block_count = 1,
                .parameter_count = 2,
            };
            const bool created = ir_create(&input, &sizes, &function) == IR_OK;
            tier1_select_test_expect(test, created, "create copy-provenance selection fixture");
            if (created)
            {
                function.result_type = vector != 0 ? IR_VOID : IR_I64;
                function.block_offsets[1] = 5;
                function.parameter_types[0] = IR_PTR;
                function.parameter_types[1] = vector != 0 ? IR_PTR : IR_I64;
                tier1_select_test_node(&function, 0, IR_PARAM, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 1, IR_PARAM, function.parameter_types[1], 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 2, IR_LOAD, vector != 0 ? IR_V4I32 : IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 3, vector != 0 ? IR_STORE : IR_ADD, vector != 0 ? IR_VOID : IR_I64, 1, 2, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 4, IR_RET, IR_VOID, vector != 0 ? BUSTER_IR_NONE : 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
                function.flags[2] = marked == 0 ? 0 : BUSTER_IR_COPY_PIECE | (marked == 2 ? BUSTER_IR_VOLATILE : 0);
                function.flags[3] = vector != 0 ? function.flags[2] : 0;
                Tier1SelectPlan plan;
                uint32_t failed = BUSTER_IR_NONE;
                const bool planned = tier1_select_plan(&function, 0, &output, &scratch, &plan, &failed) == TIER1_SELECT_OK;
                tier1_select_test_expect(test, planned, "copy-provenance fixture selects");
                if (planned)
                {
                    if (vector != 0)
                    {
                        tier1_select_test_expect(test, !plan.covered[2] && plan.patterns[2].rule == TIER1_RULE_FLOAT_LOAD &&
                                                 plan.patterns[2].width == 16 && plan.patterns[2].source_width == (marked == 1 ? 8 : 0) &&
                                                 plan.patterns[2].intrinsic_cost == (marked == 1 ? 2 : 1),
                                                 "only the marked vector load selects two separately costed narrow halves");
                    }
                    else
                    {
                        // An unfused ADD may use LEA. The volatile contract is that the load stays at its own node.
                        const bool folded = (plan.covered[2] != 0) == (marked != 2) &&
                                            (marked != 2 ? plan.patterns[3].rule == TIER1_RULE_LOAD_OPERAND : plan.patterns[2].rule == TIER1_RULE_LOAD);
                        if (!folded)
                        {
                            fprintf(stderr, "tier1-select: copy provenance marked=%u covered=%u load-rule=%u add-rule=%u\n", marked,
                                    plan.covered[2], plan.patterns[2].rule, plan.patterns[3].rule);
                        }
                        tier1_select_test_expect(test, folded,
                                                 "scalar copy provenance preserves the load fold and volatile refusal");
                    }
                }
            }
            arena_pop_to(&output, 0);
            arena_pop_to(&input, 0);
        }
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&input);
}

static void tier1_copy_select_tests(Tier1SelectTest* test)
{
    tier1_copy_select_records(test);
    tier1_copy_select_plans(test);
}
