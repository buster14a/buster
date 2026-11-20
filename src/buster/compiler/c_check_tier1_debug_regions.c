// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Source-region fences and fresh machine block entries (#1114). Included by the scalar value proof. Source windows
// retain typed-entry coordinates; only exact current IR block maps and final record permutations bound their PCs.
// Phi entry facts come from the complete emitted-code checker, never from an allocator interval alone.

// The coordinate space of local `local`'s regions (#2541): 0 for the function's own, 1 + the instance whose callee's local
// it is for an expanded callee's.
static uint32_t check_tier1_debug_region_space(const CheckTier1DebugValues* proof, uint32_t local)
{
    return proof->local_instances != NULL && local < proof->local_total && proof->local_instances[local] != BUSTER_IR_NONE ? proof->local_instances[local] + 1 : 0;
}

// The local and source entry of each change that can affect a machine record. Regions are ordered by
// local and disjoint source interval. Adjacent reachable windows share an endpoint in this order.
static uint32_t check_tier1_debug_region_events(const CheckTier1DebugValues* proof, uint64_t* events)
{
    const IrDebugInfo* debug = proof->ir->debug;
    uint32_t count = 0;
    for (uint32_t k = 0; k < debug->region_count; k += 1)
    {
        if (proof->regions[k].begin < proof->regions[k].end)
        {
            const IrDebugRegion region = debug->regions[k];
            const uint32_t local = proof->local_index[region.local];
            const uint64_t start = (uint64_t)region.begin << 32 | local;
            if (count == 0 || events[count - 1] != start)
            {
                events[count++] = start;
            }
            events[count++] = (uint64_t)region.end << 32 | local;
        }
    }
    return count;
}

// The two halves of the spare sort buffer index first occurrences and complete lifetimes.
static void check_tier1_debug_region_links(CheckTier1DebugValues* proof, uint32_t* spare, uint32_t* last_event)
{
    proof->region_previous = spare;
    proof->region_last_if_first = spare + proof->region_event_count;
    memset(last_event, 0, (uint64_t)proof->local_count * sizeof(*last_event));
    for (uint32_t at = 0; at < proof->region_event_count; at += 1)
    {
        const uint32_t local = (uint32_t)proof->region_events[at];
        proof->region_previous[at] = last_event[local];
        last_event[local] = at + 1;
    }
    for (uint32_t at = 0; at < proof->region_event_count; at += 1)
    {
        const uint32_t local = (uint32_t)proof->region_events[at];
        proof->region_last_if_first[at] = proof->region_previous[at] == 0 ? last_event[local] - 1 : UINT32_MAX;
    }
}

static bool check_tier1_debug_value_regions(CheckTier1DebugValues* proof, const Tier1EmitResult* emitted,
                                          const uint32_t* places, const uint32_t* record_order, uint8_t* cuts, uint32_t* last_event,
                                          Arena* scratch)
{
    const IrFunction* ir = proof->ir;
    const IrDebugInfo* debug = ir->debug;
    const uint32_t nodes = ir->instruction_count;
    const uint32_t blocks = ir->block_count;
    const uint32_t count = emitted->code.count;
    const uint32_t regions = debug->region_count;
    const bool needed = regions != 0 || emitted->debug_phis != NULL;
    bool valid = !needed || (blocks != 0 && ir->block_offsets != NULL && ir->op != NULL &&
                            ir->block_offsets[0] == 0 && ir->block_offsets[blocks] == nodes &&
                            (emitted->general ? emitted->cfg_input.block_count >= blocks && emitted->block_records != NULL : emitted->code.node_first != NULL));
    uint32_t* node_blocks = valid && needed ? ir_allocate(scratch, (uint64_t)nodes + 1, 4, &valid) : NULL;
    uint32_t* old_first = valid && needed ? ir_allocate(scratch, (uint64_t)blocks + 1, 4, &valid) : NULL;
    uint32_t* old_end = valid && needed ? ir_allocate(scratch, (uint64_t)blocks + 1, 4, &valid) : NULL;
    uint32_t* fresh_at = valid && needed ? ir_allocate(scratch, (uint64_t)blocks + 1, 4, &valid) : NULL;
    uint32_t* source_end = valid && regions != 0 ? ir_allocate(scratch, (uint64_t)blocks + 1, 4, &valid) : NULL;
    uint8_t* ambiguous = valid && regions != 0 ? ir_allocate(scratch, (uint64_t)blocks + 1, 1, &valid) : NULL;
    // Per region, whether its block is ambiguous among its coordinate space's regions (`ambiguous` by block, a space at
    // a time: the function's own entries', or one instance's, #2541).
    uint8_t* region_ambiguous = valid && regions != 0 ? ir_allocate(scratch, regions, 1, &valid) : NULL;
    uint32_t* after = valid && needed ? ir_allocate(scratch, (uint64_t)count + 1, 4, &valid) : NULL;
    uint32_t* before = valid && needed ? ir_allocate(scratch, (uint64_t)count + 1, 4, &valid) : NULL;
    uint32_t* fill = valid && needed ? ir_allocate(scratch, (uint64_t)count + 1, 4, &valid) : NULL;
    proof->record_blocks = valid && needed ? ir_allocate(scratch, (uint64_t)count + 1, 4, &valid) : NULL;
    proof->fresh_first = valid && needed ? ir_allocate(scratch, (uint64_t)count + 2, 4, &valid) : NULL;
    proof->fresh_blocks = valid && needed ? ir_allocate(scratch, (uint64_t)blocks + 1, 4, &valid) : NULL;
    proof->phi_first = valid && needed ? ir_allocate(scratch, (uint64_t)count + 2, 4, &valid) : NULL;
    proof->phi_nodes = valid && needed ? ir_allocate(scratch, (uint64_t)nodes + 1, 4, &valid) : NULL;
    proof->phis = emitted->debug_phis;
    uint32_t* phi_at = valid && needed ? ir_allocate(scratch, (uint64_t)nodes + 1, 4, &valid) : NULL;
    uint64_t* phi_keys = valid && needed ? ir_allocate(scratch, (uint64_t)nodes + 1, 8, &valid) : NULL;
    uint64_t* phi_sorting = valid && needed ? ir_allocate(scratch, (uint64_t)nodes + 1, 8, &valid) : NULL;
    uint64_t* events = valid && regions != 0 ? ir_allocate(scratch, (uint64_t)regions * 2, 8, &valid) : NULL;
    uint64_t* sorting = valid && regions != 0 ? ir_allocate(scratch, (uint64_t)regions * 2, 8, &valid) : NULL;
    if (valid && needed)
    {
        memset(proof->record_blocks, 0xff, ((uint64_t)count + 1) * 4);
        memset(proof->fresh_first, 0, ((uint64_t)count + 2) * 4);
        memset(proof->phi_first, 0, ((uint64_t)count + 2) * 4);
        memset(phi_at, 0xff, ((uint64_t)nodes + 1) * 4);
        after[0] = 0;
        before[count] = count;
        for (uint32_t old = 0; old < count; old += 1)
        {
            const uint32_t next = places[old] + 1;
            after[old + 1] = after[old] > next ? after[old] : next;
        }
        for (uint32_t old = count; old != 0; old -= 1)
        {
            const uint32_t previous = places[old - 1];
            before[old - 1] = before[old] < previous ? before[old] : previous;
        }
    }
    if (valid && regions != 0)
    {
        memset(source_end, 0xff, ((uint64_t)blocks + 1) * 4);
        memset(ambiguous, 0, (uint64_t)blocks + 1);
        // The regions are in their locals' order, and a space's locals are together: each space's run of regions marks
        // its blocks, reads them back and clears them.
        for (uint32_t first = 0; first < regions;)
        {
            const uint32_t space = check_tier1_debug_region_space(proof, debug->regions[first].local);
            uint32_t end = first + 1;
            while (end < regions && check_tier1_debug_region_space(proof, debug->regions[end].local) == space)
            {
                end += 1;
            }
            for (uint32_t k = first; k < end; k += 1)
            {
                const IrDebugRegion region = debug->regions[k];
                if (region.block < blocks)
                {
                    ambiguous[region.block] |= source_end[region.block] != BUSTER_IR_NONE && source_end[region.block] != region.block_end;
                    source_end[region.block] = region.block_end;
                }
            }
            for (uint32_t k = first; k < end; k += 1)
            {
                region_ambiguous[k] = debug->regions[k].block < blocks ? ambiguous[debug->regions[k].block] : 1;
            }
            for (uint32_t k = first; k < end; k += 1)
            {
                if (debug->regions[k].block < blocks)
                {
                    source_end[debug->regions[k].block] = BUSTER_IR_NONE;
                    ambiguous[debug->regions[k].block] = 0;
                }
            }
            first = end;
        }
    }
    const uint32_t added = needed && emitted->general && valid ? emitted->cfg_input.block_count - blocks : 0;
    for (uint32_t block = 0; valid && needed && block < blocks; block += 1)
    {
        const uint32_t begin = ir->block_offsets[block];
        const uint32_t end = ir->block_offsets[block + 1];
        valid = begin <= end && end <= nodes;
        if (valid)
        {
            old_first[block] = emitted->general ? emitted->block_records[added + block] : emitted->code.node_first[begin];
            old_end[block] = emitted->general ? emitted->block_records[added + block + 1] : emitted->code.node_first[end];
            valid = old_first[block] <= old_end[block] && old_end[block] <= count;
        }
        bool fresh = false;
        for (uint32_t node = begin; valid && node < end; node += 1)
        {
            node_blocks[node] = block;
            fresh |= proof->needed[node] != 0 && ir->op[node] != IR_PARAM;
        }
        if (valid)
        {
            uint32_t first = after[old_first[block]];
            for (uint32_t old = old_first[block]; old < old_end[block]; old += 1)
            {
                first = places[old] < first ? places[old] : first;
            }
            fresh_at[block] = first;
            if (fresh && first < count)
            {
                proof->fresh_first[first + 1] += 1;
                cuts[first] = 1;
            }
            else
            {
                fresh_at[block] = BUSTER_IR_NONE;
            }
        }
    }
    for (uint32_t r = 0; valid && needed && r < count; r += 1)
    {
        const uint32_t old = record_order != NULL ? record_order[r] : r;
        const uint32_t origin = emitted->origins[old];
        const uint32_t node = origin & BUSTER_TIER1_ORIGIN_PAYLOAD;
        if ((origin >> BUSTER_TIER1_ORIGIN_SHIFT) == TIER1_ORIGIN_NODE && node < nodes)
        {
            proof->record_blocks[r] = node_blocks[node];
        }
    }
    uint32_t phi_count = 0;
    for (uint32_t node = 0; valid && needed && proof->phis != NULL && node < nodes; node += 1)
    {
        const Tier1DebugPhi phi = proof->phis[node];
        if (phi.record != BUSTER_IR_NONE)
        {
            valid = phi.block < blocks && node_blocks[node] == phi.block && ir->op[node] == IR_PHI &&
                    phi.record >= old_first[phi.block] && phi.record <= old_end[phi.block] &&
                    phi.reg < 16 && phi.class <= BUSTER_TIER1_ALLOCATE_VECTOR && phi.bytes != 0 && phi.bytes <= 8 &&
                    proof->widths[node] != 0 && phi.bytes >= proof->widths[node] && proof->floating[node] == (phi.class == BUSTER_TIER1_ALLOCATE_VECTOR);
            if (valid && after[phi.record] < count)
            {
                phi_keys[phi_count++] = (uint64_t)after[phi.record] << 32 | node;
            }
        }
    }
    if (valid && needed)
    {
        phi_keys = c_bindings_sort(phi_keys, phi_sorting, phi_count, true, UINT32_MAX);
        uint32_t writers[32];
        memset(writers, 0xff, sizeof(writers));
        uint32_t cursor = 0;
        for (uint32_t r = 0; r < count; r += 1)
        {
            const uint32_t first = cursor;
            uint32_t seen = 0;
            uint32_t duplicate = 0;
            while (cursor < phi_count && (uint32_t)(phi_keys[cursor] >> 32) == r)
            {
                const uint32_t node = (uint32_t)phi_keys[cursor++];
                const Tier1DebugPhi phi = proof->phis[node];
                const uint32_t carrier = phi.reg + (phi.class == BUSTER_TIER1_ALLOCATE_VECTOR ? 16u : 0u);
                const uint32_t bit = 1u << carrier;
                duplicate |= seen & bit;
                seen |= bit;
            }
            for (uint32_t at = first; at < cursor; at += 1)
            {
                const uint32_t node = (uint32_t)phi_keys[at];
                const Tier1DebugPhi phi = proof->phis[node];
                const uint32_t carrier = phi.reg + (phi.class == BUSTER_TIER1_ALLOCATE_VECTOR ? 16u : 0u);
                const uint32_t written = writers[carrier];
                const uint32_t old = written < count ? record_order != NULL ? record_order[written] : written : BUSTER_IR_NONE;
                // A moved successor instruction may have overwritten the entry carrier before every predecessor
                // record is complete. Such an entry cannot be reopened from the original certificate fact.
                if (((duplicate >> carrier) & 1) == 0 && (written == BUSTER_IR_NONE || old < phi.record))
                {
                    proof->completion[node] = r;
                    if (proof->needed[node] != 0)
                    {
                        phi_at[node] = r;
                        proof->phi_first[r + 1] += 1;
                        cuts[r] = 1;
                    }
                }
            }
            uint32_t killed = proof->transfers[r].killed;
            while (killed != 0)
            {
                writers[(uint32_t)__builtin_ctz(killed)] = r;
                killed &= killed - 1;
            }
        }
        for (uint32_t r = 0; r <= count; r += 1)
        {
            proof->fresh_first[r + 1] += proof->fresh_first[r];
            fill[r] = proof->fresh_first[r];
        }
        for (uint32_t block = 0; block < blocks; block += 1)
        {
            if (fresh_at[block] < count)
            {
                proof->fresh_blocks[fill[fresh_at[block]]++] = block;
            }
        }
        for (uint32_t r = 0; r <= count; r += 1)
        {
            proof->phi_first[r + 1] += proof->phi_first[r];
            fill[r] = proof->phi_first[r];
        }
        for (uint32_t node = 0; node < nodes; node += 1)
        {
            if (phi_at[node] < count)
            {
                proof->phi_nodes[fill[phi_at[node]]++] = node;
            }
        }
    }
    // The active block remaps preserve distinct source-block identities. A future merger must drop or split
    // those source regions; contradictory original block ends also conservatively leave their shared block empty.
    // Within one original block, all later assignments bound an earlier window. Their RHSs may complete out of
    // source order, including across a lifetime gap, so the immediate next assignment alone is insufficient.
    for (uint32_t local = 0; valid && regions != 0 && local < proof->local_total; local += 1)
    {
        uint32_t binding = proof->binding_first[local + 1];
        uint32_t cutoff = count;
        uint32_t original_end = BUSTER_IR_NONE;
        for (uint32_t at = proof->region_first[local + 1]; at > proof->region_first[local];)
        {
            const uint32_t k = --at;
            const IrDebugRegion region = debug->regions[k];
            if (original_end != region.block_end)
            {
                cutoff = count;
                original_end = region.block_end;
                while (binding > proof->binding_first[local])
                {
                    const IrDebugBinding next = debug->bindings[proof->binding_order[binding - 1]];
                    if (next.entry == BUSTER_IR_NONE || next.entry < original_end)
                    {
                        break;
                    }
                    binding -= 1;
                }
            }
            while (binding > proof->binding_first[local])
            {
                const IrDebugBinding next = debug->bindings[proof->binding_order[binding - 1]];
                if (next.entry == BUSTER_IR_NONE || next.entry < region.end)
                {
                    break;
                }
                binding -= 1;
                const uint32_t completion = next.anchor < nodes && node_blocks[next.anchor] == region.block ? proof->completion[next.anchor] : BUSTER_IR_NONE;
                cutoff = completion == BUSTER_IR_NONE ? 0 : completion < cutoff ? completion : cutoff;
            }
            if (region.block < blocks && region_ambiguous[k] == 0 && region.kind != IR_DEBUG_BIND_UNAVAILABLE)
            {
                const uint32_t begin = after[old_first[region.block]];
                const uint32_t end = before[old_end[region.block]] < cutoff ? before[old_end[region.block]] : cutoff;
                if (begin < end)
                {
                    proof->regions[k] = (CheckTier1DebugSpan){ .begin = begin, .end = end };
                }
            }
        }
    }
    if (valid && regions != 0)
    {
        proof->region_event_count = check_tier1_debug_region_events(proof, events);
        proof->region_events = c_bindings_sort(events, sorting, proof->region_event_count, true, UINT32_MAX);
        check_tier1_debug_region_links(proof, (uint32_t*)(proof->region_events == events ? sorting : events), last_event);
    }
    return valid;
}

static uint32_t check_tier1_debug_value_region_cursor(const CheckTier1DebugValues* proof, uint32_t entry, bool source)
{
    uint32_t first = 0;
    uint32_t end = source ? proof->region_event_count : 0;
    while (first < end)
    {
        const uint32_t middle = first + (end - first) / 2;
        const bool before = (uint32_t)(proof->region_events[middle] >> 32) <= entry;
        first = before ? middle + 1 : first;
        end = before ? end : middle;
    }
    return first;
}

static uint32_t check_tier1_debug_value_region(const CheckTier1DebugValues* proof, uint32_t local, uint32_t record, uint32_t entry)
{
    const IrDebugInfo* debug = proof->ir->debug;
    const uint32_t begin = proof->region_first[local];
    uint32_t first = begin;
    uint32_t end = proof->region_first[local + 1];
    // Each local's source intervals are ordered and disjoint. Once the last interval has ended, no
    // machine block or carrier can make that local visible at this entry.
    if (begin == end || entry >= debug->regions[end - 1].end)
    {
        return BUSTER_IR_NONE;
    }
    while (first < end)
    {
        const uint32_t middle = first + (end - first) / 2;
        const bool before = debug->regions[middle].begin <= entry;
        first = before ? middle + 1 : first;
        end = before ? end : middle;
    }
    const uint32_t found = first > begin ? first - 1 : BUSTER_IR_NONE;
    return found != BUSTER_IR_NONE && entry < debug->regions[found].end && proof->record_blocks != NULL &&
           proof->record_blocks[record] == debug->regions[found].block && record >= proof->regions[found].begin && record < proof->regions[found].end ? found : BUSTER_IR_NONE;
}
