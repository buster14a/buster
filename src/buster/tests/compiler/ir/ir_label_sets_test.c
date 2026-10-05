// Label-set validation: independent pairwise shape/uniqueness oracles,
// arbitrary-order and malformed metadata controls, and geometric work bounds.
// Included by ir_test.c; ir_test_label_sets is the registered fixture.
BUSTER_GLOBAL_LOCAL bool ir_label_sets_reference_unique(IrBlockId* blocks, u32 count)
{
    bool valid = !count || blocks;
    for (u32 left = 0; valid && left < count; left += 1)
    {
        for (u32 right = left + 1; valid && right < count; right += 1)
        {
            valid = blocks[left].value != blocks[right].value;
        }
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool ir_label_sets_reference_contains(IrBlockId* blocks, u32 count, IrBlockId block)
{
    bool found = false;
    for (u32 index = 0; blocks && index < count; index += 1)
    {
        found |= blocks[index].value == block.value;
    }
    return found;
}

BUSTER_GLOBAL_LOCAL bool ir_label_sets_reference_shape(IrValueLabelMetadata* value, u32 block_count, u64 layout_size, u64 pointer_size, bool typed)
{
    bool valid = (value->label_block_count != 0) == (value->label_blocks != 0) &&
                 (value->label_path_count != 0) == (value->label_paths != 0) &&
                 (!value->is_label_value || value->label_block_count != 0) &&
                 (!value->has_label_provenance || value->label_block_count != 0) &&
                 (!value->label_block_count || value->is_label_value || value->has_label_provenance) &&
                 !(value->is_label_value && value->has_label_provenance) &&
                 ir_label_sets_reference_unique(value->label_blocks, value->label_block_count);
    for (u32 index = 0; valid && index < value->label_block_count; index += 1)
    {
        valid = value->label_blocks[index].value < block_count;
    }
    bool path_has_label = false;
    bool path_has_non_label = false;
    for (u32 index = 0; valid && index < value->label_path_count; index += 1)
    {
        IrLabelProvenancePath* path = value->label_paths + index;
        valid = path->size && path->offset <= UINT64_MAX - path->size && (!typed || path->offset + path->size <= layout_size) &&
                (path->label_block_count != 0) == (path->label_blocks != 0) &&
                (!path->is_non_label || !path->label_block_count) && (path->is_non_label || path->label_block_count) &&
                (path->is_non_label || !typed || (pointer_size && path->size == pointer_size)) &&
                ir_label_sets_reference_unique(path->label_blocks, path->label_block_count);
        path_has_label |= !path->is_non_label;
        path_has_non_label |= path->is_non_label;
        for (u32 block_index = 0; valid && block_index < path->label_block_count; block_index += 1)
        {
            valid = path->label_blocks[block_index].value < block_count &&
                    ir_label_sets_reference_contains(value->label_blocks, value->label_block_count, path->label_blocks[block_index]);
        }
        for (u32 previous = 0; valid && previous < index; previous += 1)
        {
            IrLabelProvenancePath* earlier = value->label_paths + previous;
            valid = !(earlier->offset < path->offset + path->size && path->offset < earlier->offset + earlier->size);
        }
    }
    if (valid && value->label_path_count)
    {
        valid = !value->is_label_value && path_has_label == value->has_label_provenance &&
                (!path_has_non_label || value->has_non_label_provenance);
    }
    for (u32 block_index = 0; valid && value->has_label_provenance && block_index < value->label_block_count; block_index += 1)
    {
        bool found = false;
        for (u32 path_index = 0; path_index < value->label_path_count; path_index += 1)
        {
            IrLabelProvenancePath* path = value->label_paths + path_index;
            found |= !path->is_non_label && ir_label_sets_reference_contains(path->label_blocks, path->label_block_count, value->label_blocks[block_index]);
        }
        valid = found;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL UnitTestResult ir_test_label_sets(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = arena_begin_temporal(arguments->arena);
    IrType type = {.id = {.value = 0}, .kind = IR_TYPE_ARRAY, .layout = {.size = 32, .alignment = 8, .resolved = true}};
    IrProgram program = {.types = {.types = &type, .count = 1}, .data_layout = {.pointer = {.size = 8, .alignment = 8}}};
    IrValue slot = {.canonical_type = {.value = 0}, .definition = IR_INSTRUCTION_ID_INVALID};
    IrValueId metadata_id = {.value = 0};
    IrValueLabelMetadata metadata = {0};
    IrFunction function = {.values = &slot, .value_count = 1, .block_count = 3,
                           .label_metadata_values = &metadata_id, .label_metadata = &metadata, .label_metadata_count = 1};
    BUSTER_TEST(arguments, !ir_label_provenance_valid(0));
    BUSTER_TEST(arguments, !ir_label_storage_provenance_valid(0));
    BUSTER_TEST(arguments, ir_block_id_array_unique(0, 0));
    BUSTER_TEST(arguments, !ir_block_id_array_unique(0, 1));
    BUSTER_TEST(arguments, !ir_label_metadata_shape_valid(&program, 0, metadata_id));
    BUSTER_TEST(arguments, !ir_label_metadata_shape_valid(&program, &function, (IrValueId){.value = 1}));
    // More than the tiny-set cutoff, unsorted, and spanning every radix byte.
    // UINT32_MAX itself is a distinct ID but lies outside every block count.
    IrBlockId high_blocks[16];
    IrBlockId high_snapshot[16];
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(high_blocks); index += 1)
    {
        high_blocks[index].value = index * UINT32_C(0x11000001);
    }
    high_blocks[0].value = UINT32_MAX;
    memcpy(high_snapshot, high_blocks, sizeof(high_blocks));
#if BUSTER_BENCH_ALLOCATIONS
    IrConstructionCounters high_before = ir_construction_counters();
#endif
    BUSTER_TEST(arguments, ir_block_id_array_unique(high_blocks, BUSTER_ARRAY_LENGTH(high_blocks)));
#if BUSTER_BENCH_ALLOCATIONS
    IrConstructionCounters high_after = ir_construction_counters();
    BUSTER_TEST(arguments, high_after.values[IR_CONSTRUCTION_VALIDATION_LABEL_SET_WORK] -
                           high_before.values[IR_CONSTRUCTION_VALIDATION_LABEL_SET_WORK] >= 9 * BUSTER_ARRAY_LENGTH(high_blocks));
#endif
    BUSTER_TEST(arguments, !memcmp(high_blocks, high_snapshot, sizeof(high_blocks)));
    function.block_count = UINT32_MAX;
    metadata = (IrValueLabelMetadata){.label_blocks = high_blocks, .label_block_count = BUSTER_ARRAY_LENGTH(high_blocks), .is_label_value = true};
    BUSTER_TEST(arguments, !ir_label_metadata_shape_valid(&program, &function, metadata_id));
    high_blocks[0].value = UINT32_MAX - 1;
    BUSTER_TEST(arguments, ir_label_metadata_shape_valid(&program, &function, metadata_id));
    high_blocks[12] = high_blocks[3];
    BUSTER_TEST(arguments, !ir_block_id_array_unique(high_blocks, BUSTER_ARRAY_LENGTH(high_blocks)));
    BUSTER_TEST(arguments, !ir_label_metadata_shape_valid(&program, &function, metadata_id));
    function.block_count = 3;
    // Every four-symbol tuple through length four includes all permutations,
    // nonadjacent duplicates and IDs just beyond this function's block range.
    for (u32 count = 0; count <= 4; count += 1)
    {
        u32 tuple_count = 1u << (count * 2);
        for (u32 tuple = 0; tuple < tuple_count; tuple += 1)
        {
            IrBlockId blocks[4] = {0};
            for (u32 index = 0; index < count; index += 1)
            {
                blocks[index].value = (tuple >> (index * 2)) & 3;
            }
            IrBlockId snapshot[4];
            memcpy(snapshot, blocks, sizeof(blocks));
            bool unique = ir_label_sets_reference_unique(blocks, count);
            BUSTER_TEST(arguments, ir_block_id_array_unique(blocks, count) == unique);
            for (u32 flags = 0; flags < 8; flags += 1)
            {
                metadata = (IrValueLabelMetadata){.label_blocks = count ? blocks : 0, .label_block_count = count,
                    .is_label_value = (flags & 1) != 0, .has_label_provenance = (flags & 2) != 0, .has_non_label_provenance = (flags & 4) != 0};
                BUSTER_TEST(arguments, ir_label_provenance_valid(&metadata) == (count && unique && flags == 1));
                BUSTER_TEST(arguments, ir_label_storage_provenance_valid(&metadata) == (count && unique && (flags == 2 || flags == 6)));
                IrLabelProvenancePath path = {.label_blocks = count ? blocks : 0, .label_block_count = count, .size = 8};
                for (u32 has_path = 0; has_path < 2; has_path += 1)
                {
                    metadata.label_paths = has_path ? &path : 0;
                    metadata.label_path_count = has_path;
                    bool expected = ir_label_sets_reference_shape(&metadata, function.block_count, type.layout.size, 8, true);
                    BUSTER_TEST(arguments, ir_label_metadata_shape_valid(&program, &function, metadata_id) == expected);
                    expected = ir_label_sets_reference_shape(&metadata, function.block_count, 0, 0, false);
                    BUSTER_TEST(arguments, ir_label_metadata_shape_valid(0, &function, metadata_id) == expected);
                }
            }
            BUSTER_TEST(arguments, !memcmp(blocks, snapshot, sizeof(blocks)));
        }
    }
    IrBlockId aggregate[] = {{.value = 2}, {.value = 0}, {.value = 1}};
    IrBlockId first_blocks[] = {{.value = 1}, {.value = 2}};
    IrBlockId second_blocks[] = {{.value = 0}, {.value = 1}};
    IrLabelProvenancePath paths[] = {
        {.offset = 8, .size = 8, .label_blocks = second_blocks, .label_block_count = 2},
        {.offset = 0, .size = 8, .label_blocks = first_blocks, .label_block_count = 2},
        {.offset = 16, .size = 8, .is_non_label = true},
    };
    for (u32 variant = 0; variant < 15; variant += 1)
    {
        IrBlockId changed_aggregate[3];
        IrBlockId changed_first[2];
        IrBlockId changed_second[2];
        IrLabelProvenancePath changed_paths[3];
        memcpy(changed_aggregate, aggregate, sizeof(aggregate));
        memcpy(changed_first, first_blocks, sizeof(first_blocks));
        memcpy(changed_second, second_blocks, sizeof(second_blocks));
        memcpy(changed_paths, paths, sizeof(paths));
        changed_paths[0].label_blocks = changed_second;
        changed_paths[1].label_blocks = changed_first;
        metadata = (IrValueLabelMetadata){.label_blocks = changed_aggregate, .label_block_count = 3,
            .label_paths = changed_paths, .label_path_count = 3, .has_label_provenance = true, .has_non_label_provenance = true};
        switch (variant)
        {
        case 0: break;
        case 1: changed_aggregate[2] = changed_aggregate[0]; break;
        case 2: changed_aggregate[2].value = 3; break;
        case 3: changed_first[0] = changed_first[1]; break;
        case 4: changed_first[0].value = 3; break;
        case 5: changed_second[0].value = 2; break; // Aggregate block zero has no path coverage.
        case 6: changed_paths[0].offset = 7; break;
        case 7: changed_paths[0].offset = UINT64_MAX - 3; break;
        case 8: changed_paths[1].size = 0; break;
        case 9: metadata.has_non_label_provenance = false; break;
        case 10: metadata.is_label_value = true; break;
        case 11: metadata.label_blocks = 0; break;
        case 12: changed_paths[0].label_blocks = 0; break;
        case 13: changed_paths[2].label_blocks = changed_first; changed_paths[2].label_block_count = 2; break;
        case 14: changed_paths[0].offset = 32; break;
        }
        IrValueLabelMetadata metadata_snapshot = metadata;
        IrLabelProvenancePath paths_snapshot[3];
        memcpy(paths_snapshot, changed_paths, sizeof(changed_paths));
        bool expected = ir_label_sets_reference_shape(&metadata, function.block_count, type.layout.size, 8, true);
        BUSTER_TEST(arguments, expected == (variant == 0));
        // Reused dirty scratch must never turn the coverage-gap control valid.
        TemporalArena dirty = scratch_begin(0, 0);
        u8* dirt = arena_allocate(dirty.arena, u8, 4096);
        memset(dirt, 255, 4096);
        scratch_end(dirty);
        BUSTER_TEST(arguments, ir_label_metadata_shape_valid(&program, &function, metadata_id) == expected);
        BUSTER_TEST(arguments, ir_label_metadata_shape_valid(&program, &function, metadata_id) == expected);
        BUSTER_TEST(arguments, !memcmp(&metadata, &metadata_snapshot, sizeof(metadata)));
        BUSTER_TEST(arguments, !memcmp(changed_paths, paths_snapshot, sizeof(changed_paths)));
    }
    // The universe can be huge while metadata remains small; scratch/work must
    // follow stored IDs and paths rather than function->block_count.
    function.block_count = 1u << 24;
#if BUSTER_BENCH_ALLOCATIONS
    u64 previous_set_work = 0;
    u64 previous_scratch = 0;
#endif
    for (u32 count = 16; count <= 1024; count *= 2)
    {
        TemporalArena scale_temporary = arena_begin_temporal(arguments->arena);
        IrBlockId* blocks = arena_allocate(arguments->arena, IrBlockId, count);
        IrBlockId* path_blocks = arena_allocate(arguments->arena, IrBlockId, count * 4);
        IrBlockId* snapshot = arena_allocate(arguments->arena, IrBlockId, count * 5);
        IrLabelProvenancePath scale_paths[4];
        for (u32 index = 0; index < count; index += 1)
        {
            blocks[index].value = count - index - 1;
            for (u32 path_index = 0; path_index < 4; path_index += 1)
            {
                path_blocks[path_index * count + index].value = (index * 5 + path_index) & (count - 1);
            }
        }
        for (u32 path_index = 0; path_index < 4; path_index += 1)
        {
            scale_paths[path_index] = (IrLabelProvenancePath){.offset = (3 - path_index) * 8, .size = 8,
                .label_blocks = path_blocks + path_index * count, .label_block_count = count};
        }
        memcpy(snapshot, blocks, sizeof(*blocks) * count);
        memcpy(snapshot + count, path_blocks, sizeof(*path_blocks) * count * 4);
        metadata = (IrValueLabelMetadata){.label_blocks = blocks, .label_block_count = count,
            .label_paths = scale_paths, .label_path_count = 4, .has_label_provenance = true};
#if BUSTER_BENCH_ALLOCATIONS
        IrConstructionCounters before = ir_construction_counters();
#endif
        BUSTER_TEST(arguments, ir_label_metadata_shape_valid(&program, &function, metadata_id));
#if BUSTER_BENCH_ALLOCATIONS
        IrConstructionCounters after = ir_construction_counters();
        u64 set_work = after.values[IR_CONSTRUCTION_VALIDATION_LABEL_SET_WORK] - before.values[IR_CONSTRUCTION_VALIDATION_LABEL_SET_WORK];
        u64 scratch = after.values[IR_CONSTRUCTION_VALIDATION_LABEL_SCRATCH_BYTES] - before.values[IR_CONSTRUCTION_VALIDATION_LABEL_SCRATCH_BYTES];
        BUSTER_TEST(arguments, !before.overflowed && !after.overflowed);
        BUSTER_TEST(arguments, set_work > count && set_work <= 128 * (u64)count);
        BUSTER_TEST(arguments, scratch <= 48 * (u64)count);
        BUSTER_TEST(arguments, !previous_set_work || set_work <= 3 * previous_set_work);
        BUSTER_TEST(arguments, !previous_scratch || scratch <= 3 * previous_scratch);
        previous_set_work = set_work;
        previous_scratch = scratch;
#endif
        BUSTER_TEST(arguments, !memcmp(snapshot, blocks, sizeof(*blocks) * count));
        BUSTER_TEST(arguments, !memcmp(snapshot + count, path_blocks, sizeof(*path_blocks) * count * 4));
        BUSTER_TEST(arguments, ir_block_id_array_unique(blocks, count));
        blocks[count / 2] = blocks[0];
        BUSTER_TEST(arguments, !ir_block_id_array_unique(blocks, count));
        BUSTER_TEST(arguments, !ir_label_metadata_shape_valid(&program, &function, metadata_id));
        scratch_end(scale_temporary);
    }
    // All paths share one compact unordered set. Requested bytes may repeat,
    // but live scratch must follow the largest path rather than their sum.
    for (u32 path_count = 16; path_count <= 64; path_count *= 4)
    {
        TemporalArena shared_temporary = arena_begin_temporal(arguments->arena);
        IrBlockId shared_blocks[64];
        IrBlockId shared_snapshot[64];
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(shared_blocks); index += 1)
        {
            shared_blocks[index].value = (u32)BUSTER_ARRAY_LENGTH(shared_blocks) - index - 1;
        }
        memcpy(shared_snapshot, shared_blocks, sizeof(shared_blocks));
        IrLabelProvenancePath* shared_paths = arena_allocate(arguments->arena, IrLabelProvenancePath, path_count);
        IrLabelProvenancePath* paths_snapshot = arena_allocate(arguments->arena, IrLabelProvenancePath, path_count);
        for (u32 index = 0; index < path_count; index += 1)
        {
            shared_paths[index] = (IrLabelProvenancePath){.offset = (u64)(path_count - index - 1) * 8, .size = 8,
                .label_blocks = shared_blocks, .label_block_count = BUSTER_ARRAY_LENGTH(shared_blocks)};
        }
        memcpy(paths_snapshot, shared_paths, sizeof(*shared_paths) * path_count);
        function.block_count = BUSTER_ARRAY_LENGTH(shared_blocks);
        type.layout.size = (u64)path_count * 8;
        metadata = (IrValueLabelMetadata){.label_blocks = shared_blocks, .label_block_count = BUSTER_ARRAY_LENGTH(shared_blocks),
            .label_paths = shared_paths, .label_path_count = path_count, .has_label_provenance = true};
        // Shape uses this same calling-thread scratch selection. Rewinds
        // preserve its observed high water without retaining any copied set.
        TemporalArena measurement = scratch_begin(0, 0);
        u64 previous_high_water = measurement.arena->high_water;
        measurement.arena->high_water = measurement.arena->position;
        bool shared_valid = ir_label_metadata_shape_valid(&program, &function, metadata_id);
        u64 shared_peak = BUSTER_MAX(measurement.arena->high_water, measurement.arena->position);
        measurement.arena->high_water = BUSTER_MAX(previous_high_water, shared_peak);
        BUSTER_TEST(arguments, shared_valid);
        BUSTER_TEST(arguments, measurement.arena->position == measurement.position);
        // Aggregate and one path each need two ID copies, plus coverage and
        // two path-order views. The allowance includes alignment rounding.
        BUSTER_TEST(arguments, shared_peak - measurement.position <= 4 * sizeof(IrBlockId) * BUSTER_ARRAY_LENGTH(shared_blocks) +
                               BUSTER_ARRAY_LENGTH(shared_blocks) + 2 * sizeof(IrLabelProvenancePath*) * (u64)path_count + 64);
        scratch_end(measurement);
        BUSTER_TEST(arguments, !memcmp(shared_blocks, shared_snapshot, sizeof(shared_blocks)));
        BUSTER_TEST(arguments, !memcmp(shared_paths, paths_snapshot, sizeof(*shared_paths) * path_count));
        scratch_end(shared_temporary);
    }
    metadata = (IrValueLabelMetadata){0};
    function.label_metadata_count = 0;
#if BUSTER_BENCH_ALLOCATIONS
    IrConstructionCounters empty_before = ir_construction_counters();
#endif
    BUSTER_TEST(arguments, ir_label_metadata_shape_valid(&program, &function, metadata_id));
    BUSTER_TEST(arguments, ir_label_metadata_shape_valid(0, &function, metadata_id));
    type.layout.resolved = false;
    BUSTER_TEST(arguments, !ir_label_metadata_shape_valid(&program, &function, metadata_id));
#if BUSTER_BENCH_ALLOCATIONS
    IrConstructionCounters empty_after = ir_construction_counters();
    BUSTER_TEST(arguments, empty_before.values[IR_CONSTRUCTION_VALIDATION_LABEL_SET_WORK] == empty_after.values[IR_CONSTRUCTION_VALIDATION_LABEL_SET_WORK]);
    BUSTER_TEST(arguments, empty_before.values[IR_CONSTRUCTION_VALIDATION_LABEL_SCRATCH_BYTES] == empty_after.values[IR_CONSTRUCTION_VALIDATION_LABEL_SCRATCH_BYTES]);
#endif
    scratch_end(temporary);
    return result;
}
