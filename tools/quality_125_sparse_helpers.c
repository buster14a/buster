#if BUSTER_INCLUDE_TESTS
// A calling-thread scoped oracle selects the retained dense representation in
// the complete allocator, including predicate projection, retries and fallback.
// It is absent from ordinary compilers and cannot change allocator policy.
BUSTER_GLOBAL_LOCAL BUSTER_THREAD_LOCAL_DECL bool machine_quality_test_dense_regions;

MachineStackPlacement machine_quality_placement_build_regions_test(Arena* arena, MachineFunction* function, bool dense)
{
    bool previous = machine_quality_test_dense_regions;
    machine_quality_test_dense_regions = dense;
    MachineStackPlacement result = machine_quality_placement_build(arena, function);
    machine_quality_test_dense_regions = previous;
    return result;
}
#endif

// Sparse rows need the exact same total order as region_next: traffic
// descending, then original region ID ascending. This is deliberately not the
// candidate heap's strict-greater tie policy, which remains unchanged above.
BUSTER_GLOBAL_LOCAL bool machine_quality_region_precedes(MachineQualityRegionTraffic left, MachineQualityRegionTraffic right)
{
    return left.traffic > right.traffic || (left.traffic == right.traffic && left.region < right.region);
}

void machine_quality_region_heap_sift(MachineQualityRegionTraffic* heap, u32 count, u32 root)
{
    // The bound prevents 2 * root + 1 overflowing even at UINT32_MAX entries.
    while (root < count / 2)
    {
        BUSTER_QUALITY_COUNT(sparse_region_heap_steps, 1);
        u32 child = 2 * root + 1;
        if (child + 1 < count && machine_quality_region_precedes(heap[child + 1], heap[child]))
        {
            child += 1;
        }
        if (!machine_quality_region_precedes(heap[child], heap[root]))
        {
            break;
        }
        MachineQualityRegionTraffic swapped = heap[root];
        heap[root] = heap[child];
        heap[child] = swapped;
        root = child;
    }
}

void machine_quality_region_heap_build(MachineQualityRegionTraffic* heap, u32 count)
{
    BUSTER_QUALITY_COUNT(sparse_region_heap_entries, count);
    for (u32 root = count / 2; root > 0; root -= 1)
    {
        machine_quality_region_heap_sift(heap, count, root - 1);
    }
}

MachineQualityRegionTraffic machine_quality_region_heap_pop(MachineQualityRegionTraffic* heap, u32* count)
{
    MachineQualityRegionTraffic result = {.traffic = 0, .region = UINT32_MAX};
    BUSTER_QUALITY_COUNT(sparse_region_queries, 1);
    if (*count)
    {
        result = heap[0];
        *count -= 1;
        heap[0] = heap[*count];
        heap[*count] = result;
        machine_quality_region_heap_sift(heap, *count, 0);
    }
    return result;
}

MachineQualitySparseRegions machine_quality_sparse_regions_build(Arena* arena, u64 const* spans, u32 region_count,
    u32 candidate_count, u32 const* candidate_indices, MachineEdit const* edits, u32 edit_count,
    u32 const* instruction_weights, u32 candidate_edit_limit)
{
    MachineQualitySparseRegions result = {0};
    u64 dense_bytes = (u64)candidate_count * region_count * sizeof(MachineQualityTraffic);
    // Every occupied cell needs at least one retained-candidate edit. This
    // conservative pre-gate prices both construction arrays and alignment;
    // tiny/dense populations keep the existing constructor without a prepass.
    u64 sparse_bound = (u64)candidate_edit_limit * sizeof(MachineQualityRegionTraffic) +
                       (2 * (u64)candidate_count + 1) * sizeof(u32) + 16;
    bool use_sparse = candidate_count && region_count > 1 && sparse_bound < dense_bytes;
#if BUSTER_INCLUDE_TESTS
    use_sparse = use_sparse && !machine_quality_test_dense_regions;
#endif
    if (use_sparse)
    {
        TemporalArena storage = arena_begin_temporal(arena);
        u32* offsets = arena_allocate(arena, u32, (u64)candidate_count + 1);
        u32* cursors = arena_allocate(arena, u32, candidate_count);
        for (u32 slot = 0; slot < candidate_count; slot += 1)
        {
            offsets[slot] = 0;
            cursors[slot] = UINT32_MAX;
        }
        offsets[candidate_count] = 0;
        bool ordered = true;
        MachinePoint previous_point = 0;
        u32 region = 0;
        // FAST merges its main and retroactive streams in point order. Check
        // rather than assuming that contract for every admitted MIR producer:
        // an unordered relevant stream rolls back and uses the old dense path.
        for (u32 index = 0; index < edit_count && ordered; index += 1)
        {
            BUSTER_QUALITY_COUNT(sparse_region_construction_edits, 1);
            MachineEdit const* edit = edits + index;
            if (edit->kind != MACHINE_EDIT_SPILL && edit->kind != MACHINE_EDIT_RELOAD)
            {
                continue;
            }
            u32 slot = candidate_indices[edit->subject];
            if (slot == UINT32_MAX)
            {
                continue;
            }
            ordered = edit->point >= previous_point;
            previous_point = edit->point;
            u32 instruction = machine_point_instruction(edit->point);
            while (region < region_count && (u32)spans[region] < instruction)
            {
                region += 1;
            }
            if (ordered && region < region_count && (u32)(spans[region] >> 32) <= instruction && cursors[slot] != region)
            {
                offsets[slot + 1] += 1;
                cursors[slot] = region;
            }
        }
        if (ordered)
        {
            for (u32 slot = 0; slot < candidate_count; slot += 1)
            {
                offsets[slot + 1] += offsets[slot];
                cursors[slot] = offsets[slot];
            }
            u32 entry_count = offsets[candidate_count];
            BUSTER_VALIDATE(entry_count <= candidate_edit_limit);
            MachineQualityRegionTraffic* entries = arena_allocate(arena, MachineQualityRegionTraffic, entry_count);
            region = 0;
            for (u32 index = 0; index < edit_count; index += 1)
            {
                BUSTER_QUALITY_COUNT(sparse_region_construction_edits, 1);
                MachineEdit const* edit = edits + index;
                if (edit->kind != MACHINE_EDIT_SPILL && edit->kind != MACHINE_EDIT_RELOAD)
                {
                    continue;
                }
                u32 slot = candidate_indices[edit->subject];
                if (slot == UINT32_MAX)
                {
                    continue;
                }
                u32 instruction = machine_point_instruction(edit->point);
                while (region < region_count && (u32)spans[region] < instruction)
                {
                    region += 1;
                }
                if (region < region_count && (u32)(spans[region] >> 32) <= instruction)
                {
                    BUSTER_QUALITY_COUNT(candidate_region_updates, 1);
                    if (cursors[slot] == offsets[slot] || entries[cursors[slot] - 1].region != region)
                    {
                        entries[cursors[slot]] = (MachineQualityRegionTraffic){.traffic = 0, .region = region};
                        cursors[slot] += 1;
                    }
                    machine_quality_traffic_add(&entries[cursors[slot] - 1].traffic, instruction_weights[instruction]);
                }
            }
            result = (MachineQualitySparseRegions){.entries = entries, .offsets = offsets};
            BUSTER_QUALITY_COUNT(sparse_region_tables, 1);
            BUSTER_QUALITY_COUNT(sparse_region_entries, entry_count);
            BUSTER_QUALITY_COUNT(sparse_region_storage_bytes, arena->position - storage.position);
        }
        else
        {
            BUSTER_QUALITY_COUNT(sparse_region_order_fallbacks, 1);
            scratch_end(storage);
        }
    }
    return result;
}
