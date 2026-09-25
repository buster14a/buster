// Private machine_tests fixtures: exact sparse traffic/region enumeration,
// complete dense-oracle placements, dirty scratch and opt-in allocator timing.
// Included after machine_test_compile_c_with_options, not a separate module.
#pragma once

BUSTER_CT_CHECK(sizeof(MachineQualityRegionTraffic) == 16);

// Independent region-constructor boundary and destructive-enumeration oracles.
BUSTER_GLOBAL_LOCAL UnitTestResult machine_test_quality_region_boundaries(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    enum { candidate_count = 8, region_count = 64, value_count = 40, row_count = 4 * region_count + 4 };
    u64 spans[region_count];
    u32 mapping[value_count];
    u32 weights[row_count];
    MachineEdit edits[512];
    MachineQualityTraffic dense[candidate_count][region_count] = {0};
    u32 edit_count = 0;
    u32 candidate_edits = 0;
    for (u32 value = 0; value < value_count; value += 1)
    {
        mapping[value] = UINT32_MAX;
    }
    for (u32 slot = 0; slot < candidate_count; slot += 1)
    {
        mapping[3 + 4 * slot] = candidate_count - slot - 1;
    }
    for (u32 row = 0; row < row_count; row += 1)
    {
        weights[row] = row % 3 ? 4096 : 1;
    }
    for (u32 region = 0; region < region_count; region += 1)
    {
        u32 first = 4 * region + 1;
        spans[region] = ((u64)first << 32) | (first + 1);
        // Neither an unretained value nor a non-memory edit participates in
        // the monotonicity check. Their points deliberately decrease.
        edits[edit_count++] = (MachineEdit){.point = machine_point_make(row_count - region - 1, MACHINE_POINT_AFTER),
            .kind = MACHINE_EDIT_RELOAD, .subject = 0};
        edits[edit_count++] = (MachineEdit){.point = machine_point_make(0, MACHINE_POINT_BEFORE),
            .kind = MACHINE_EDIT_COPY, .subject = UINT32_MAX};
        if (region == 0 || region == 17 || region == region_count - 1)
        {
            // Both inclusive endpoints, all four phases, and the first row
            // outside the interval. The final outside row tests exhaustion.
            for (u32 row = first; row <= first + 2; row += 1)
            {
                u32 phases = row == first + 2 ? 1 : MACHINE_POINT_PHASE_COUNT;
                for (u32 phase = 0; phase < phases; phase += 1)
                {
                    for (u32 slot = 0; slot < candidate_count; slot += 1)
                    {
                        edits[edit_count++] = (MachineEdit){.point = machine_point_make(row, (MachinePointPhase)phase),
                            .kind = phase & 1u ? MACHINE_EDIT_RELOAD : MACHINE_EDIT_SPILL, .subject = 3 + 4 * slot};
                        candidate_edits += 1;
                    }
                }
            }
        }
    }
    // Deliberately independent of the production forward region cursor:
    // compare each retained edit against every original inclusive interval.
    for (u32 index = 0; index < edit_count; index += 1)
    {
        MachineEdit const* edit = edits + index;
        if (edit->kind == MACHINE_EDIT_SPILL || edit->kind == MACHINE_EDIT_RELOAD)
        {
            u32 slot = mapping[edit->subject];
            if (slot != UINT32_MAX)
            {
                u32 row = machine_point_instruction(edit->point);
                for (u32 region = 0; region < region_count; region += 1)
                {
                    if ((u32)(spans[region] >> 32) <= row && row <= (u32)spans[region])
                    {
                        dense[slot][region] += weights[row];
                    }
                }
            }
        }
    }
    // Test every possible alignment residue, then reuse poisoned allocations.
    for (u32 residue = 0; residue < 16; residue += 1)
    {
        TemporalArena input = arena_begin_temporal(arguments->arena);
        (void)arena_allocate(arguments->arena, u8, residue);
        for (u32 repeat = 0; repeat < 2; repeat += 1)
        {
            TemporalArena table_storage = arena_begin_temporal(arguments->arena);
            MachineQualitySparseRegions table = machine_quality_sparse_regions_build(arguments->arena, spans, region_count,
                candidate_count, mapping, edits, edit_count, weights, candidate_edits);
            if (BUSTER_REQUIRE(arguments, table.offsets && table.entries))
            {
                BUSTER_TEST(arguments, arguments->arena->position - table_storage.position < sizeof(dense));
                BUSTER_TEST(arguments, table.offsets[0] == 0 && table.offsets[candidate_count] == 3 * candidate_count);
                for (u32 slot = 0; slot < candidate_count; slot += 1)
                {
                    u32 first = table.offsets[slot];
                    u32 end = table.offsets[slot + 1];
                    if (!BUSTER_REQUIRE(arguments, first <= end && end <= 3 * candidate_count && end - first == 3))
                    {
                        continue;
                    }
                    u32 remaining = end - first;
                    MachineQualityRegionTraffic* row = table.entries + first;
                    machine_quality_region_heap_build(row, remaining);
                    u32 previous = UINT32_MAX;
                    for (u32 query = 0; query < 4; query += 1)
                    {
                        u32 expected = machine_quality_region_next(dense[slot], region_count, previous);
                        MachineQualityRegionTraffic actual = machine_quality_region_heap_pop(row, &remaining);
                        BUSTER_TEST(arguments, actual.region == expected);
                        if (expected != UINT32_MAX)
                        {
                            BUSTER_TEST(arguments, actual.traffic == dense[slot][expected]);
                        }
                        previous = expected;
                    }
                }
                memset((u8*)arguments->arena + table_storage.position, 0xa5,
                    arguments->arena->position - table_storage.position);
            }
            scratch_end(table_storage);
            // An out-of-order retained edit outside every region must still
            // roll back. It cannot be skipped before checking monotonicity.
            edits[edit_count] = (MachineEdit){.point = machine_point_make(0, MACHINE_POINT_BEFORE),
                .kind = MACHINE_EDIT_SPILL, .subject = 3};
            MachineQualitySparseRegions unordered = machine_quality_sparse_regions_build(arguments->arena, spans, region_count,
                candidate_count, mapping, edits, edit_count + 1, weights, candidate_edits + 1);
            BUSTER_TEST(arguments, !unordered.offsets && !unordered.entries && arguments->arena->position == table_storage.position);
            // A decreasing phase is also out of order, even in one instruction.
            edits[edit_count] = edits[edit_count - 1];
            edits[edit_count].point = machine_point_make(4 * region_count - 1, MACHINE_POINT_AFTER);
            edits[edit_count + 1] = edits[edit_count];
            edits[edit_count + 1].point = machine_point_make(4 * region_count - 1, MACHINE_POINT_EARLY);
            unordered = machine_quality_sparse_regions_build(arguments->arena, spans, region_count,
                candidate_count, mapping, edits, edit_count + 2, weights, candidate_edits + 2);
            BUSTER_TEST(arguments, !unordered.offsets && !unordered.entries && arguments->arena->position == table_storage.position);
        }
        scratch_end(input);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult machine_test_quality_region_all_prefixes(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    MachineQualityTraffic choices[] = {0, 7, UINT64_C(4294967303)};
    u32 population = 1;
    for (u32 width = 0; width <= 8; width += 1)
    {
        for (u32 seed = 0; seed < population; seed += 1)
        {
            MachineQualityTraffic dense[8] = {0};
            MachineQualityRegionTraffic heap[8];
            u32 remaining_seed = seed;
            u32 count = 0;
            // Reverse initial IDs to prevent a stable input order from masking
            // a missing ascending-region-ID tie break in the heap.
            for (u32 index = width; index > 0; index -= 1)
            {
                u32 region = index - 1;
                dense[region] = choices[remaining_seed % BUSTER_ARRAY_LENGTH(choices)];
                remaining_seed /= BUSTER_ARRAY_LENGTH(choices);
                if (dense[region])
                {
                    heap[count++] = (MachineQualityRegionTraffic){.traffic = dense[region], .region = region};
                }
            }
            for (u32 prefix = 0; prefix <= count; prefix += 1)
            {
                machine_quality_region_heap_build(heap, count);
                u32 remaining = count;
                for (u32 index = 0; index < prefix; index += 1)
                {
                    (void)machine_quality_region_heap_pop(heap, &remaining);
                }
                // Popping must retain the entire key/value set in the suffix;
                // rebuilding after every prefix must start the original order.
                machine_quality_region_heap_build(heap, count);
                remaining = count;
                u32 previous = UINT32_MAX;
                for (u32 index = 0; index <= count; index += 1)
                {
                    u32 expected = machine_quality_region_next(dense, width, previous);
                    MachineQualityRegionTraffic actual = machine_quality_region_heap_pop(heap, &remaining);
                    BUSTER_TEST(arguments, actual.region == expected);
                    if (expected != UINT32_MAX)
                    {
                        BUSTER_TEST(arguments, actual.traffic == dense[expected]);
                    }
                    previous = expected;
                }
                BUSTER_TEST(arguments, !remaining);
            }
        }
        population *= 3;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult machine_test_quality_regions(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_TEST_FIXTURE(arguments, machine_test_quality_region_boundaries);
    BUSTER_TEST_FIXTURE(arguments, machine_test_quality_region_all_prefixes);
    u32 counts[] = {64, 1, 16, 4, 0};
    u32 regions[] = {256, 1, 32, 0, 2, 8};
    for (u32 ci = 0; ci < BUSTER_ARRAY_LENGTH(counts); ci += 1)
    {
        for (u32 li = 0; li < BUSTER_ARRAY_LENGTH(regions); li += 1)
        {
            for (u32 shape = 0; shape < 5; shape += 1)
            {
                TemporalArena input = arena_begin_temporal(arguments->arena);
                u32 c = counts[ci];
                u32 l = regions[li];
                u32 value_count = 2 * c + 7;
                u64* spans = arena_allocate(arguments->arena, u64, l);
                u32* mapping = arena_allocate(arguments->arena, u32, value_count);
                u32* weights = arena_allocate(arguments->arena, u32, 4 * l + 4);
                MachineEdit* edits = arena_allocate(arguments->arena, MachineEdit, 2 * (u64)c * l + 2);
                MachineQualityTraffic* dense = arena_allocate_zeroed(arguments->arena, MachineQualityTraffic, (u64)c * l);
                for (u32 value = 0; value < value_count; value += 1) mapping[value] = UINT32_MAX;
                for (u32 slot = 0; slot < c; slot += 1) mapping[2 * (c - slot - 1) + 3] = slot;
                for (u32 row = 0; row < 4 * l + 4; row += 1) weights[row] = row % 3 ? 4096 : 1;
                u32 edit_count = 0;
                // Non-memory edit subjects are not virtual register IDs.
                edits[edit_count++] = (MachineEdit){.point = machine_point_make(0, MACHINE_POINT_BEFORE),
                    .kind = MACHINE_EDIT_COPY, .subject = UINT32_MAX};
                for (u32 region = 0; region < l; region += 1)
                {
                    u32 row = 4 * region + 1;
                    spans[region] = ((u64)row << 32) | (row + 1);
                    for (u32 slot = 0; slot < c; slot += 1)
                    {
                        bool touched = shape == 1 ? region == (17 * slot + 3) % l :
                            shape == 2 ? true : shape == 3 ? slot < 2 :
                            shape == 4 ? (region * 13 + slot * 7) % 32 == 0 : false;
                        if (touched)
                        {
                            for (u32 repeat = 0; repeat < 2; repeat += 1)
                            {
                                edits[edit_count++] = (MachineEdit){.point = machine_point_make(row, MACHINE_POINT_BEFORE),
                                    .kind = repeat ? MACHINE_EDIT_RELOAD : MACHINE_EDIT_SPILL,
                                    .subject = 2 * (c - slot - 1) + 3};
                                dense[(u64)slot * l + region] += weights[row];
                            }
                        }
                    }
                }
                u32 edit_limit = edit_count - 1;
                bool sparse_expected = c && l > 1 && (u64)edit_limit * 16 + (2 * (u64)c + 1) * 4 + 16 < (u64)c * l * 8;
                for (u32 reuse = 0; reuse < 3; reuse += 1)
                {
                    TemporalArena table_storage = arena_begin_temporal(arguments->arena);
                    MachineQualitySparseRegions table = machine_quality_sparse_regions_build(arguments->arena, spans, l, c,
                        mapping, edits, edit_count, weights, edit_limit);
                    BUSTER_TEST(arguments, (table.offsets != 0) == sparse_expected);
                    if (table.offsets)
                    {
                        BUSTER_TEST(arguments, arguments->arena->position - table_storage.position < (u64)c * l * 8);
                        for (u32 slot = 0; slot < c; slot += 1)
                        {
                            u32 row_count = table.offsets[slot + 1] - table.offsets[slot];
                            MachineQualityRegionTraffic* row = table.entries + table.offsets[slot];
                            u32 nonzero = 0;
                            for (u32 region = 0; region < l; region += 1) nonzero += dense[(u64)slot * l + region] != 0;
                            BUSTER_TEST(arguments, row_count == nonzero);
                            for (u32 item = 0; item < row_count; item += 1)
                            {
                                if (BUSTER_REQUIRE(arguments, row[item].region < l))
                                {
                                    BUSTER_TEST(arguments, row[item].traffic == dense[(u64)slot * l + row[item].region]);
                                    BUSTER_TEST(arguments, !item || row[item - 1].region < row[item].region);
                                }
                            }
                            // Exhaustion, early stop and restart from a dirty partial heap.
                            for (u32 restart = 0; restart < 3; restart += 1)
                            {
#if BUSTER_BENCH_ALLOCATIONS
                                MachineQualityCensus before = machine_quality_census_snapshot();
#endif
                                machine_quality_region_heap_build(row, row_count);
                                u32 remaining = row_count;
                                u32 previous = UINT32_MAX;
                                for (u32 query = 0; query <= row_count; query += 1)
                                {
                                    u32 expected = machine_quality_region_next(dense + (u64)slot * l, l, previous);
                                    MachineQualityRegionTraffic actual = machine_quality_region_heap_pop(row, &remaining);
                                    BUSTER_TEST(arguments, actual.region == expected);
                                    if (expected == UINT32_MAX)
                                    {
                                        BUSTER_TEST(arguments, actual.traffic == 0 && remaining == 0);
                                        break;
                                    }
                                    BUSTER_TEST(arguments, actual.traffic == dense[(u64)slot * l + expected]);
                                    previous = expected;
                                    if (restart == 1 && query == row_count / 2) break;
                                }
#if BUSTER_BENCH_ALLOCATIONS
                                MachineQualityCensus after = machine_quality_census_snapshot();
                                u32 levels = 1;
                                for (u32 bound = row_count; bound > 1; bound >>= 1) levels += 1;
                                BUSTER_TEST(arguments, after.sparse_region_heap_steps - before.sparse_region_heap_steps <=
                                    (u64)row_count * (levels + 2));
#endif
                            }
                        }
                        memset((u8*)arguments->arena + table_storage.position, 0xa5,
                            arguments->arena->position - table_storage.position);
                    }
                    else
                    {
                        BUSTER_TEST(arguments, arguments->arena->position == table_storage.position);
                    }
                    scratch_end(table_storage);
                }
                if (sparse_expected && edit_count > 3 && edits[1].point != edits[edit_count - 1].point)
                {
                    MachineEdit exchanged = edits[1];
                    edits[1] = edits[edit_count - 1];
                    edits[edit_count - 1] = exchanged;
                    u64 position = arguments->arena->position;
                    MachineQualitySparseRegions unordered = machine_quality_sparse_regions_build(arguments->arena, spans, l, c,
                        mapping, edits, edit_count, weights, edit_limit);
                    BUSTER_TEST(arguments, !unordered.offsets && !unordered.entries && arguments->arena->position == position);
                }
                scratch_end(input);
            }
        }
    }
    MachineQualityRegionTraffic wide[] = {{UINT64_C(4294967296), 37}, {UINT32_MAX, 2},
        {UINT64_C(4294967296), 3}, {UINT64_C(17592186040320), 90}};
    u32 golden[] = {90, 3, 37, 2};
    u32 remaining = BUSTER_ARRAY_LENGTH(wide);
    machine_quality_region_heap_build(wide, remaining);
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(golden); index += 1)
        BUSTER_TEST(arguments, machine_quality_region_heap_pop(wide, &remaining).region == golden[index]);
    BUSTER_TEST(arguments, machine_quality_region_heap_pop(0, &remaining).region == UINT32_MAX);
    return result;
}

BUSTER_GLOBAL_LOCAL bool machine_test_quality_placements_equal(MachineFunction const* function, MachineStackPlacement const* left,
                                                               MachineStackPlacement const* right)
{
    bool equal = left->valid && right->valid && left->edit_count == right->edit_count && left->frame_size == right->frame_size &&
        left->edge_copy_temporary_offset == right->edge_copy_temporary_offset && left->incoming_base == right->incoming_base &&
        left->reload_count == right->reload_count && left->spill_count == right->spill_count && left->copy_count == right->copy_count &&
        left->rematerialize_count == right->rematerialize_count && left->pinned_register_count == right->pinned_register_count &&
        left->split_register_count == right->split_register_count && left->boundary_spill_count == right->boundary_spill_count &&
        left->boundary_reload_count == right->boundary_reload_count && left->boundary_copy_count == right->boundary_copy_count &&
        left->callee_saved_mask == right->callee_saved_mask;
    if (equal)
    {
        equal = (!left->edit_count || memcmp(left->edits, right->edits, (u64)left->edit_count * sizeof(MachineEdit)) == 0) &&
            (!function->virtual_register_count || memcmp(left->virtual_register_offsets, right->virtual_register_offsets,
                (u64)function->virtual_register_count * sizeof(u32)) == 0) &&
            (!function->stack_slot_count || memcmp(left->stack_slot_offsets, right->stack_slot_offsets,
                (u64)function->stack_slot_count * sizeof(u32)) == 0) &&
            (!function->instruction_count || memcmp(left->operand_registers, right->operand_registers,
                (u64)function->instruction_count * MACHINE_INSTRUCTION_OPERAND_COUNT) == 0);
    }
    return equal;
}

BUSTER_GLOBAL_LOCAL String8 machine_test_quality_region_source(Arena* arena, u32 loops)
{
    String8* parts = arena_allocate(arena, String8, (u64)loops * 53 + 2);
    u32 count = 0;
    parts[count++] = S8("unsigned long long regions(unsigned long long seed, unsigned rounds) { unsigned long long total = seed;\n");
    for (u32 loop = 0; loop < loops; loop += 1)
    {
        parts[count++] = S8("{\n");
        for (u32 value = 0; value < 16; value += 1)
            parts[count++] = string_format(arena, S8("unsigned long long a{u32} = total + {u32};\n"), value, value + loop + 1);
        parts[count++] = S8("for (unsigned i = 0; i < rounds; i += 1) {\n");
        for (u32 value = 0; value < 16; value += 1)
            parts[count++] = string_format(arena, S8("a{u32} += a{u32} ^ a{u32};\n"), value, (value + 1) % 16, (value + 15) % 16);
        parts[count++] = S8("}\ntotal ^= ");
        for (u32 value = 0; value < 16; value += 1)
            parts[count++] = string_format(arena, S8("a{u32}{S8}"), value, value == 15 ? S8(";\n}\n") : S8(" ^ "));
    }
    parts[count++] = S8("return total; }\n");
    return string_join_arena(arena, (SliceString8){.pointer = parts, .length = count}, false);
}

BUSTER_GLOBAL_LOCAL UnitTestResult machine_test_quality_region_placements(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Target targets[] = {{.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_LINUX},
        {.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_LINUX},
        {.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_WINDOWS},
        {.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_WINDOWS},
        {.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_MACOS},
        {.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_MACOS}};
    u32 loop_counts[] = {32, 1, 8};
#if BUSTER_BENCH_ALLOCATIONS
    u64 sparse_tables = 0;
#endif
    bool benchmark = os_get_environment_variable(S8("BUSTER_QUALITY_REGION_BENCH")).length != 0;
    for (u32 ti = 0; ti < BUSTER_ARRAY_LENGTH(targets); ti += 1)
    {
        for (u32 form = 0; form < 2; form += 1)
        {
            for (u32 shape = 0; shape < BUSTER_ARRAY_LENGTH(loop_counts); shape += 1)
            {
                TemporalArena input = arena_begin_temporal(arguments->arena);
                u32 loops = loop_counts[shape];
                String8 source = machine_test_quality_region_source(arguments->arena, loops);
                IrProgram* program = machine_test_compile_c_with_options(arguments->arena, S8("quality-regions.c"), source,
                    targets[ti], (CIRLowerOptions){.disable_direct_ssa = form != 0});
                if (BUSTER_REQUIRE(arguments, program && program->module_count == 1 && program->modules[0].function_count == 1))
                {
                    MachineSelectResult selected = machine_select_canonical_function(arguments->arena, program,
                        program->modules[0].functions, targets[ti]);
                    if (BUSTER_REQUIRE(arguments, selected.supported))
                    {
                        MachineFunction* function = &selected.function;
                        MachineStackPlacement reference = machine_quality_placement_build_regions_test(arguments->arena, function, true);
                        BUSTER_TEST(arguments, reference.valid);
                        MachineEncodeResult reference_code = ti % 2 ? machine_encode_aarch64(arguments->arena, function, &reference) :
                            machine_encode_x86_64(arguments->arena, function, &reference);
                        BUSTER_TEST(arguments, reference_code.valid);
                        for (u32 repeat = 0; repeat < 2; repeat += 1)
                        {
                            TemporalArena trial = arena_begin_temporal(arguments->arena);
#if BUSTER_BENCH_ALLOCATIONS
                            MachineQualityCensus before = machine_quality_census_snapshot();
#endif
                            MachineStackPlacement actual = machine_quality_placement_build_regions_test(arguments->arena, function, false);
#if BUSTER_BENCH_ALLOCATIONS
                            MachineQualityCensus after = machine_quality_census_snapshot();
                            sparse_tables += after.sparse_region_tables - before.sparse_region_tables;
#endif
                            bool equal = machine_test_quality_placements_equal(function, &reference, &actual);
                            BUSTER_TEST(arguments, equal);
                            if (equal)
                            {
                                MachineEncodeResult code = ti % 2 ? machine_encode_aarch64(arguments->arena, function, &actual) :
                                    machine_encode_x86_64(arguments->arena, function, &actual);
                                if (BUSTER_REQUIRE(arguments, reference_code.valid && code.valid && code.byte_count == reference_code.byte_count))
                                    BUSTER_TEST(arguments, memcmp(code.bytes, reference_code.bytes, code.byte_count) == 0);
                            }
                            memset((u8*)arguments->arena + trial.position, 0xa5, arguments->arena->position - trial.position);
                            scratch_end(trial);
                        }
#if !BUSTER_BENCH_ALLOCATIONS
                        // Predeclared two rounds of 20 AB/BA pairs. Inputs and
                        // correctness are outside timing; include allocator
                        // construction, internal scratch cleanup and result rewind.
                        if (benchmark && ti == 0 && form == 0)
                        {
                            for (u32 round = 0; round < 2; round += 1)
                            {
                                for (u32 pair = 0; pair < 22; pair += 1)
                                {
                                    for (u32 order = 0; order < 2; order += 1)
                                    {
                                        bool dense = ((pair + order + round) & 1u) != 0;
                                        TemporalArena trial = arena_begin_temporal(arguments->arena);
                                        TimeDataType start = timestamp_take();
                                        MachineStackPlacement timed = machine_quality_placement_build_regions_test(arguments->arena, function, dense);
                                        bool valid = timed.valid;
                                        scratch_end(trial);
                                        u64 ns = timestamp_ns_between(start, timestamp_take());
                                        BUSTER_TEST(arguments, valid);
                                        if (pair >= 2)
                                            string_print(S8("BENCH_QUALITY_REGION loops={u32} round={u32} pair={u32} dense={u32} ns={u64}\n"),
                                                loops, round, pair - 2, (u32)dense, ns);
                                    }
                                }
                            }
                        }
#else
                        (void)benchmark;
#endif
                    }
                }
                scratch_end(input);
            }
        }
    }
#if BUSTER_BENCH_ALLOCATIONS
    BUSTER_TEST(arguments, sparse_tables > 0);
#endif
    return result;
}
