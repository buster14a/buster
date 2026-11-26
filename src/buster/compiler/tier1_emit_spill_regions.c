// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Measurement-only #2610: an immutable incoming parameter's already allocated ordinary slot can hold it across
// call paths that do not read it. Coalesce those paths at unanimous SPILL entries; preserve REGISTER at mixed
// joins and before uses. A real store precedes the first call, never the function entry. Ordinary memory, all
// transport, and total instructions each have an independent all-path check; pure tag migration is refused.

typedef struct Tier1EmitSpillRegions Tier1EmitSpillRegions;
struct Tier1EmitSpillRegions
{
    uint32_t* intervals;
    uint32_t* calls;
    uint8_t* eligible;
    uint8_t* entries;
    uint8_t* exits;
    uint8_t* forced;
    uint32_t blocks;
    uint32_t roots;
};

// The plan's one bit is a real ordinary-slot location, not an assertion that a caller register still contains its
// value. A mixed entry stays in its original register. Critical exits restore before the terminator, retaining the
// separately proved slot home; this avoids adding a branch merely to reload the parameter on one successor.
static Tier1EmitError tier1_emit_spill_regions_plan(const Tier1SelectPlan* plan, const Tier1EmitResult* emitted,
                                                    const Tier1CfgInput* input, const Tier1CfgAllocation* cfg,
                                                    const Tier1EmitRecolorModel* model, uint32_t value, uint8_t source,
                                                    Arena* output, Arena* scratch, uint64_t* work, Tier1EmitSpillRegions* result)
{
    const uint64_t mark = scratch->length;
    const uint32_t blocks = input->block_count;
    const IrFunction* function = plan->function;
    bool ok = true;
    Tier1EmitSpillRegions built = { 0 };
    Tier1EmitError error = tier1_emit_recolor_charge(blocks * UINT64_C(16) + cfg->intervals.count * UINT64_C(32) +
                                                    function->instruction_count * UINT64_C(18) +
                                                    plan->effective_use_offsets[function->instruction_count], work) ? TIER1_EMIT_OK : TIER1_EMIT_LIMIT;
    built.intervals = error == TIER1_EMIT_OK ? ir_allocate(output, (uint64_t)blocks + 1, 4, &ok) : 0;
    built.calls = error == TIER1_EMIT_OK && ok ? ir_allocate(output, (uint64_t)blocks + 1, 4, &ok) : 0;
    built.eligible = error == TIER1_EMIT_OK && ok ? ir_allocate(output, (uint64_t)blocks + 1, 1, &ok) : 0;
    built.entries = error == TIER1_EMIT_OK && ok ? ir_allocate(output, (uint64_t)blocks + 1, 1, &ok) : 0;
    built.exits = error == TIER1_EMIT_OK && ok ? ir_allocate(output, (uint64_t)blocks + 1, 1, &ok) : 0;
    built.forced = error == TIER1_EMIT_OK && ok ? ir_allocate(output, (uint64_t)blocks + 1, 1, &ok) : 0;
    uint8_t* reads = error == TIER1_EMIT_OK && ok ? ir_allocate(scratch, (uint64_t)blocks + 1, 1, &ok) : 0;
    uint8_t* original_entries = error == TIER1_EMIT_OK && ok ? ir_allocate(scratch, (uint64_t)blocks + 1, 1, &ok) : 0;
    uint8_t* original_exits = error == TIER1_EMIT_OK && ok ? ir_allocate(scratch, (uint64_t)blocks + 1, 1, &ok) : 0;
    error = error == TIER1_EMIT_OK && !ok ? TIER1_EMIT_MEMORY : error;
    if (error == TIER1_EMIT_OK)
    {
        memset(built.intervals, 0xff, ((uint64_t)blocks + 1) * 4);
        memset(built.calls, 0xff, ((uint64_t)blocks + 1) * 4);
        for (uint32_t u = input->use_offsets[value]; u < input->use_offsets[value + 1]; u += 1)
        {
            reads[tier1_cfg_block_at(input, cfg, input->use_positions[u])] = 1;
        }
        // Guard selected expansion operands as well as the allocator's uses. This intentionally excludes call
        // arguments too: every ABI source must remain identical in this first ordinary-spill proposal.
        const uint32_t base = 2 * (blocks - function->block_count);
        for (uint32_t node = 0; node < function->instruction_count; node += 1)
        {
            bool read = false;
            for (uint32_t u = plan->effective_use_offsets[node]; u < plan->effective_use_offsets[node + 1]; u += 1)
            {
                read |= emitted->node_values[plan->effective_use_values[u]] == value;
            }
            for (uint32_t p = 0; !plan->covered[node] && p < BUSTER_TIER1_SELECT_VALUES; p += 1)
            {
                const uint32_t operand = plan->patterns[node].values[p];
                read |= operand < function->instruction_count && emitted->node_values[operand] == value;
            }
            if (read)
            {
                reads[tier1_cfg_block_at(input, cfg, base + 2 * node)] = 1;
            }
        }
        for (uint32_t c = 0; c < model->count; c += 1)
        {
            const uint32_t b = model->calls[c].block;
            const uint32_t use = model->calls[c].position - 1;
            const uint32_t seed = use == input->block_starts[b] ? use + 1 : use;
            built.calls[b] = built.calls[b] == UINT32_MAX ? seed : built.calls[b];
        }
    }
    for (uint32_t b = 0; error == TIER1_EMIT_OK && b < blocks; b += 1)
    {
        const uint32_t start = input->block_starts[b];
        const uint32_t end = input->block_starts[b + 1];
        const uint32_t i = tier1_cfg_interval_at(cfg, value, start);
        uint32_t location = 0;
        uint8_t kind = 0;
        const bool entered = tier1_cfg_entry_location(cfg, value, start, &location, &kind);
        original_entries[b] = !entered ? 2 : kind == BUSTER_TIER1_ALLOCATE_SPILL;
        const bool exited = tier1_cfg_location(cfg, value, end - 1, &location, &kind);
        original_exits[b] = exited && kind == BUSTER_TIER1_ALLOCATE_SPILL;
        const uint32_t f = i != BUSTER_TIER1_CFG_NONE ? cfg->allocation.fragment_offsets[i] : 0;
        bool eligible = b != 0 && reads[b] == 0 && i != BUSTER_TIER1_CFG_NONE &&
                        cfg->intervals.starts[i] == start && cfg->intervals.ends[i] == end && start > input->definitions[value] &&
                        cfg->allocation.fragment_offsets[i + 1] == f + 1 && cfg->allocation.kinds[f] == BUSTER_TIER1_ALLOCATE_REGISTER &&
                        cfg->allocation.registers[f] == source;
        // Normally store at the even use phase. A block-first call instead uses its following definition phase,
        // after argument gathering but before preservation and the clobber. The no-operand check above makes this
        // safe, and its one-position REGISTER prefix retains the original incoming-edge convention.
        eligible = eligible && (built.calls[b] == UINT32_MAX || (start < built.calls[b] && built.calls[b] < end - 2));
        if (eligible && built.calls[b] == UINT32_MAX)
        {
            bool dispatch = false;
            error = tier1_emit_region_branch_dispatch(plan, input, b, work, &dispatch);
            eligible = error == TIER1_EMIT_OK && !dispatch;
        }
        built.intervals[b] = i;
        built.eligible[b] = eligible;
        built.exits[b] = eligible ? 1 : original_exits[b];
    }
    bool again = true;
    for (uint32_t outer = 0; error == TIER1_EMIT_OK && again && outer <= blocks; outer += 1)
    {
        bool changed = true;
        for (uint32_t round = 0; error == TIER1_EMIT_OK && changed && round <= blocks; round += 1)
        {
            error = tier1_emit_recolor_charge((uint64_t)blocks * 4 + input->edge_count, work) ? error : TIER1_EMIT_LIMIT;
            changed = false;
            for (uint32_t b = 0; error == TIER1_EMIT_OK && b < blocks; b += 1)
            {
                const uint32_t first = input->predecessor_offsets[b];
                const uint32_t past = input->predecessor_offsets[b + 1];
                bool entry = built.eligible[b] != 0 && first != past;
                for (uint32_t p = first; entry && p < past; p += 1)
                {
                    entry = built.exits[input->predecessors[p]] != 0;
                }
                const uint8_t exit = built.eligible[b] != 0 ? (built.forced[b] == 0 && (entry || built.calls[b] != UINT32_MAX)) : original_exits[b];
                changed |= built.entries[b] != entry || built.exits[b] != exit;
                built.entries[b] = entry;
                built.exits[b] = exit;
            }
            error = error == TIER1_EMIT_OK && changed && round == blocks ? TIER1_EMIT_INVALID : error;
        }
        error = error != TIER1_EMIT_OK || tier1_emit_recolor_charge((uint64_t)blocks + input->edge_count * UINT64_C(4), work) ? error : TIER1_EMIT_LIMIT;
        again = false;
        for (uint32_t b = 0; error == TIER1_EMIT_OK && b < blocks; b += 1)
        {
            const uint32_t first = input->successor_offsets[b];
            const uint32_t past = input->successor_offsets[b + 1];
            for (uint32_t e = first; built.eligible[b] != 0 && built.exits[b] != 0 && e < past; e += 1)
            {
                const uint32_t t = input->successors[e];
                const uint8_t entry = built.eligible[t] != 0 ? built.entries[t] : original_entries[t];
                const bool critical = past - first > 1 && input->predecessor_offsets[t + 1] - input->predecessor_offsets[t] > 1;
                if (entry == 0 && critical && cfg->edge_placements[e] != TIER1_CFG_EDGE_SPLIT)
                {
                    again |= built.forced[b] == 0;
                    built.forced[b] = 1;
                }
            }
        }
        error = error == TIER1_EMIT_OK && again && outer == blocks ? TIER1_EMIT_INVALID : error;
    }
    for (uint32_t b = 0; error == TIER1_EMIT_OK && b < blocks; b += 1)
    {
        if (built.eligible[b] != 0 && (built.entries[b] != 0 || built.calls[b] != UINT32_MAX))
        {
            const uint32_t start = built.entries[b] != 0 ? input->block_starts[b] : built.calls[b];
            const uint32_t end = built.exits[b] != 0 ? input->block_starts[b + 1] : input->block_starts[b + 1] - 2;
            error = start < end ? error : TIER1_EMIT_INVALID;
            built.blocks += start < end;
            built.roots += built.entries[b] == 0 && start < end;
        }
    }
    if (error == TIER1_EMIT_OK)
    {
        *result = built;
    }
    arena_pop_to(scratch, mark);
    return error;
}

// All new fragments are owned. The paid slot map, complete value lifetimes, and every nonparameter fragment remain
// identical. finish rebuilds its actual current-slot proofs and all edge programs; its register-only caches are not used.
static Tier1EmitError tier1_emit_spill_regions_materialize(const Tier1CfgInput* input, const Tier1CfgPrepared* prepared,
                                                           const Tier1CfgAllocation* original, const Tier1EmitSpillRegions* regions,
                                                           uint32_t value, uint8_t source, Arena* output, Arena* scratch,
                                                           uint64_t* work, Tier1CfgAllocation* result)
{
    const uint64_t mark = output->length;
    const Tier1Allocation* old = &original->allocation;
    const uint64_t capacity = old->fragment_count + 2 * (uint64_t)regions->blocks;
    bool ok = capacity <= UINT32_MAX;
    Tier1EmitError error = ok && tier1_emit_recolor_charge(capacity * 8 + original->intervals.count + input->value_count, work) ? TIER1_EMIT_OK : TIER1_EMIT_LIMIT;
    Tier1CfgAllocation built = *original;
    built.allocation.fragment_offsets = error == TIER1_EMIT_OK ? ir_allocate(output, (uint64_t)old->interval_count + 1, 4, &ok) : 0;
    built.allocation.starts = error == TIER1_EMIT_OK && ok ? ir_allocate(output, capacity + 1, 4, &ok) : 0;
    built.allocation.ends = error == TIER1_EMIT_OK && ok ? ir_allocate(output, capacity + 1, 4, &ok) : 0;
    built.allocation.kinds = error == TIER1_EMIT_OK && ok ? ir_allocate(output, capacity + 1, 1, &ok) : 0;
    built.allocation.registers = error == TIER1_EMIT_OK && ok ? ir_allocate(output, capacity + 1, 1, &ok) : 0;
    built.allocation.value_registers = error == TIER1_EMIT_OK && ok ? ir_allocate(output, (uint64_t)input->value_count + 1, 1, &ok) : 0;
    error = error == TIER1_EMIT_OK && !ok ? TIER1_EMIT_MEMORY : error;
    uint32_t next = 0;
    for (uint32_t i = 0; error == TIER1_EMIT_OK && i < original->intervals.count; i += 1)
    {
        const uint32_t b = tier1_cfg_block_at(input, original, original->intervals.starts[i]);
        const bool changed = original->intervals.values[i] == value && regions->intervals[b] == i && regions->eligible[b] != 0 &&
                             (regions->entries[b] != 0 || regions->calls[b] != UINT32_MAX);
        built.allocation.fragment_offsets[i] = next;
        if (changed)
        {
            const uint32_t first = original->intervals.starts[i];
            const uint32_t past = original->intervals.ends[i];
            const uint32_t start = regions->entries[b] != 0 ? first : regions->calls[b];
            const uint32_t end = regions->exits[b] != 0 ? past : past - 2;
            const uint32_t cuts[] = { first, start, end, past };
            for (uint32_t k = 0; k < 3; k += 1)
            {
                if (cuts[k] < cuts[k + 1])
                {
                    built.allocation.starts[next] = cuts[k];
                    built.allocation.ends[next] = cuts[k + 1];
                    built.allocation.kinds[next] = k == 1 ? BUSTER_TIER1_ALLOCATE_SPILL : BUSTER_TIER1_ALLOCATE_REGISTER;
                    built.allocation.registers[next++] = k == 1 ? UINT8_MAX : source;
                }
            }
        }
        else
        {
            for (uint32_t f = old->fragment_offsets[i]; f < old->fragment_offsets[i + 1]; f += 1)
            {
                built.allocation.starts[next] = old->starts[f];
                built.allocation.ends[next] = old->ends[f];
                built.allocation.kinds[next] = old->kinds[f];
                built.allocation.registers[next++] = old->registers[f];
            }
        }
    }
    if (error == TIER1_EMIT_OK)
    {
        built.allocation.fragment_offsets[old->interval_count] = next;
        built.allocation.fragment_count = next;
        built.allocation.registers[next] = UINT8_MAX;
        memcpy(built.allocation.value_registers, old->value_registers, input->value_count);
        built.allocation.value_registers[value] = UINT8_MAX;
        error = next <= 2 * ((uint64_t)original->intervals.count + original->intervals.use_count) ? error : TIER1_EMIT_LIMIT;
    }
    if (error == TIER1_EMIT_OK)
    {
        built.split_count = 0;
        built.split_edges = 0;
        built.value_block_base = 0;
        built.value_block_intervals = 0;
        built.unread_entries = 0;
        built.current_slot_offsets = 0;
        built.current_slots = 0;
        Tier1CfgInput fixed_slots = *input;
        fixed_slots.slot_widths = 0;
        Tier1CfgPrepared context = *prepared;
        context.output_saved = mark;
        context.scratch_saved = scratch->length;
        const uint64_t finishes = 64 * ((uint64_t)input->block_count + input->edge_count + 1) * ((uint64_t)input->value_count + 1) +
                                  64 * ((uint64_t)next + original->intervals.count + input->value_count + input->position_count) +
                                  16 * ((uint64_t)prepared->live.in_offsets[input->block_count] + prepared->live.out_offsets[input->block_count]) +
                                  BUSTER_TIER1_CFG_BLOCK_INTERVALS * UINT64_C(8);
        error = tier1_emit_recolor_charge(finishes, work) ? error : TIER1_EMIT_LIMIT;
        const Tier1CfgError finished = error == TIER1_EMIT_OK ? tier1_cfg_finish(&fixed_slots, &context, output, scratch, &built) : TIER1_CFG_INVALID;
        error = error != TIER1_EMIT_OK ? error : finished == TIER1_CFG_OK ? error : finished == TIER1_CFG_CAPACITY ? TIER1_EMIT_MEMORY : TIER1_EMIT_INVALID;
        error = error == TIER1_EMIT_OK && (built.temp_slots != original->temp_slots || built.allocation.spill_count != old->spill_count) ? TIER1_EMIT_INVALID : error;
    }
    // Fresh finish may take optional proof-table fallbacks. Only this parameter's ordinary phase traffic is
    // repriced below, so every unrelated current-slot and unread-entry fact must remain identical.
    for (uint32_t i = 0; error == TIER1_EMIT_OK && i < original->intervals.count; i += 1)
    {
        const uint8_t before = original->unread_entries != 0 ? original->unread_entries[i] : 0;
        const uint8_t after = built.unread_entries != 0 ? built.unread_entries[i] : 0;
        error = original->intervals.values[i] == value || before == after ? error : TIER1_EMIT_UNSUPPORTED;
    }
    for (uint32_t b = 0; error == TIER1_EMIT_OK && b < input->block_count; b += 1)
    {
        uint32_t a = original->current_slot_offsets != 0 ? original->current_slot_offsets[b] : 0;
        uint32_t z = built.current_slot_offsets != 0 ? built.current_slot_offsets[b] : 0;
        const uint32_t a_end = original->current_slot_offsets != 0 ? original->current_slot_offsets[b + 1] : 0;
        const uint32_t z_end = built.current_slot_offsets != 0 ? built.current_slot_offsets[b + 1] : 0;
        while (error == TIER1_EMIT_OK && (a < a_end || z < z_end))
        {
            a += a < a_end && original->current_slots[a] == value;
            z += z < z_end && built.current_slots[z] == value;
            const bool both = a < a_end && z < z_end;
            error = (both && original->current_slots[a] == built.current_slots[z]) || (a == a_end && z == z_end) ? error : TIER1_EMIT_UNSUPPORTED;
            a += both;
            z += both;
        }
    }
    // The scorer below accounts CFG-edge programs. Genuine within-block split programs are constants and must
    // retain their exact positions and triples, even if fresh finish happens to reorder other metadata.
    if (error == TIER1_EMIT_OK)
    {
        error = built.split_count == original->split_count ? error : TIER1_EMIT_INVALID;
        for (uint32_t s = 0; error == TIER1_EMIT_OK && s < built.split_count; s += 1)
        {
            const uint32_t old_first = original->split_move_offsets[s];
            const uint32_t new_first = built.split_move_offsets[s];
            const uint32_t count = original->split_move_offsets[s + 1] - old_first;
            error = built.split_positions[s] == original->split_positions[s] && built.split_move_offsets[s + 1] - new_first == count ? error : TIER1_EMIT_INVALID;
            for (uint32_t m = 0; error == TIER1_EMIT_OK && m < count; m += 1)
            {
                error = original->move_sources[old_first + m] == built.move_sources[new_first + m] &&
                        original->move_destinations[old_first + m] == built.move_destinations[new_first + m] &&
                        original->move_values[old_first + m] == built.move_values[new_first + m] ? error : TIER1_EMIT_INVALID;
            }
        }
    }
    // New SPILL entries are fed only by actual SPILL exits. In particular finish must not invent a store on an
    // unseeded incoming edge: the call-root store or an already-existing ordinary home establishes every region.
    for (uint32_t e = 0; error == TIER1_EMIT_OK && e < input->edge_count; e += 1)
    {
        const uint32_t b = input->successors[e];
        for (uint32_t m = built.edge_move_offsets[e]; regions->eligible[b] != 0 && regions->entries[b] != 0 && m < built.edge_move_offsets[e + 1]; m += 1)
        {
            const bool stores = built.move_values[m] == value && built.move_sources[m] < BUSTER_TIER1_CFG_SPILL &&
                                built.move_destinations[m] == BUSTER_TIER1_CFG_SPILL + old->spill_ids[value];
            error = stores ? TIER1_EMIT_INVALID : error;
        }
    }
    if (error == TIER1_EMIT_OK)
    {
        *result = built;
    }
    else
    {
        arena_pop_to(output, mark);
    }
    return error;
}

// The emitter's ordinary slot-stamp rule, for one immutable parameter. Edge moves deliberately do not set stamps:
// only current_slots, an adjacent sole-predecessor run, and actual phase stores/reloads do so in cfg_records.
static Tier1EmitError tier1_emit_spill_regions_memory(const Tier1CfgInput* input, const Tier1CfgAllocation* cfg,
                                                      uint32_t value, Arena* output, uint64_t* work, uint32_t** result)
{
    bool ok = true;
    Tier1EmitError error = tier1_emit_recolor_charge(input->block_count * UINT64_C(40) + cfg->allocation.fragment_count + cfg->intervals.count, work) ? TIER1_EMIT_OK : TIER1_EMIT_LIMIT;
    uint32_t* counts = error == TIER1_EMIT_OK ? ir_allocate(output, (uint64_t)input->block_count + 1, 4, &ok) : 0;
    error = error == TIER1_EMIT_OK && !ok ? TIER1_EMIT_MEMORY : error;
    uint32_t next_block = 0;
    uint32_t stamp = 0, held = 0;
    for (uint32_t i = cfg->value_interval_offsets[value]; error == TIER1_EMIT_OK && i < cfg->value_interval_offsets[value + 1]; i += 1)
    {
        const uint32_t first = cfg->allocation.fragment_offsets[i];
        for (uint32_t f = first + 1; f < cfg->allocation.fragment_offsets[i + 1]; f += 1)
        {
            const uint32_t b = tier1_cfg_block_at(input, cfg, cfg->allocation.starts[f]);
            while (next_block <= b)
            {
                const uint32_t degree = input->predecessor_offsets[next_block + 1] - input->predecessor_offsets[next_block];
                const bool run = next_block != 0 && degree == 1 && input->predecessors[input->predecessor_offsets[next_block]] == next_block - 1;
                stamp = run ? stamp : next_block + 1;
                const uint32_t begin = cfg->current_slot_offsets != 0 ? cfg->current_slot_offsets[next_block] : 0;
                const uint32_t end = cfg->current_slot_offsets != 0 ? cfg->current_slot_offsets[next_block + 1] : 0;
                held = cfg->current_slots != 0 && tier1_cfg_listed(cfg->current_slots, begin, end, value) ? stamp : held;
                next_block += 1;
            }
            const uint8_t before = cfg->allocation.kinds[f - 1];
            const uint8_t after = cfg->allocation.kinds[f];
            if (before == BUSTER_TIER1_ALLOCATE_REGISTER && after == BUSTER_TIER1_ALLOCATE_SPILL)
            {
                counts[b] += held != stamp;
                held = stamp;
            }
            else if (before == BUSTER_TIER1_ALLOCATE_SPILL && after == BUSTER_TIER1_ALLOCATE_REGISTER)
            {
                counts[b] += 1;
                held = stamp;
            }
        }
    }
    if (error == TIER1_EMIT_OK)
    {
        *result = counts;
    }
    return error;
}

typedef struct Tier1EmitSpillScore Tier1EmitSpillScore;
struct Tier1EmitSpillScore
{
    Tier1EmitRegionScore records;
    int64_t* memory_edges;
    __int128 memory;
    __int128 transport;
    __int128 total;
};

// Every retained number is old minus new. Ordinary S/L is replayed separately from the actual lazy C/R epochs.
// All remaining changing body records are absent by admission: no parameter operand, recipe, or register copy is
// changed. Edge recipes must retain their exact value/destination sequence so their variable expansion cancels.
static Tier1EmitError tier1_emit_spill_regions_score(const Tier1SelectPlan* plan, const Tier1EmitResult* emitted,
                                                     const Tier1CfgInput* input, const Tier1CfgAllocation* before,
                                                     const Tier1CfgAllocation* after, const Tier1EmitRecolorModel* model,
                                                     uint32_t value, Arena* scratch, uint64_t* work, Tier1EmitSpillScore* result)
{
    const uint64_t mark = scratch->length;
    bool ok = true;
    Tier1EmitSpillScore built = { 0 };
    Tier1EmitError error = tier1_emit_recolor_charge((uint64_t)input->block_count * 80 + input->edge_count * UINT64_C(12) +
                                                    before->move_count + after->move_count, work) ? TIER1_EMIT_OK : TIER1_EMIT_LIMIT;
    built.records.block_gains = error == TIER1_EMIT_OK ? ir_allocate(scratch, (uint64_t)input->block_count + 1, 8, &ok) : 0;
    built.records.preservation_gains = built.records.block_gains;
    built.records.edge_gains = error == TIER1_EMIT_OK && ok ? ir_allocate(scratch, (uint64_t)input->edge_count + 1, 8, &ok) : 0;
    built.records.total_edge_gains = error == TIER1_EMIT_OK && ok ? ir_allocate(scratch, (uint64_t)input->edge_count + 1, 8, &ok) : 0;
    built.memory_edges = error == TIER1_EMIT_OK && ok ? ir_allocate(scratch, (uint64_t)input->edge_count + 1, 8, &ok) : 0;
    error = error == TIER1_EMIT_OK && !ok ? TIER1_EMIT_MEMORY : error;
    const uint64_t temporary = scratch->length;
    const uint64_t epoch_work = plan->function->instruction_count * UINT64_C(96) +
                                plan->effective_use_offsets[plan->function->instruction_count] * UINT64_C(4) +
                                ((uint64_t)before->intervals.count + after->intervals.count) * 64 +
                                ((uint64_t)before->allocation.fragment_count + after->allocation.fragment_count) * 32 +
                                (uint64_t)input->block_count * (input->value_count + UINT64_C(64)) + input->position_count * UINT64_C(8);
    error = error != TIER1_EMIT_OK || tier1_emit_recolor_charge(epoch_work, work) ? error : TIER1_EMIT_LIMIT;
    Tier1EmitExchangeEpoch old_epochs = { 0 }, new_epochs = { 0 };
    error = error == TIER1_EMIT_OK ? tier1_emit_exchange_epochs(plan, emitted, input, before, scratch, &old_epochs) : error;
    error = error == TIER1_EMIT_OK ? tier1_emit_exchange_epochs(plan, emitted, input, after, scratch, &new_epochs) : error;
    uint32_t* old_memory = 0;
    uint32_t* new_memory = 0;
    error = error == TIER1_EMIT_OK ? tier1_emit_spill_regions_memory(input, before, value, scratch, work, &old_memory) : error;
    error = error == TIER1_EMIT_OK ? tier1_emit_spill_regions_memory(input, after, value, scratch, work, &new_memory) : error;
    for (uint32_t b = 0; error == TIER1_EMIT_OK && b < input->block_count; b += 1)
    {
        const uint64_t row = (uint64_t)b * 16;
        const uint64_t old_count = tier1_emit_region_preservation(old_epochs.stores + row, old_epochs.loads + row, model->callee) + old_memory[b];
        const uint64_t new_count = tier1_emit_region_preservation(new_epochs.stores + row, new_epochs.loads + row, model->callee) + new_memory[b];
        const int64_t gain = (int64_t)old_count - (int64_t)new_count;
        built.records.block_gains[b] = gain;
        built.memory += (__int128)model->weights[b] * gain;
        built.transport += (__int128)model->weights[b] * gain;
    }
    for (uint32_t c = 0; error == TIER1_EMIT_OK && c < model->count; c += 1)
    {
        const Tier1EmitRecolorCall* call = &model->calls[c];
        error = tier1_emit_recolor_charge(call->count * (uint64_t)call->count * 2 + call->count * UINT64_C(70) + 1, work) ? error : TIER1_EMIT_LIMIT;
        uint32_t old_count = 0, new_count = 0;
        if (error == TIER1_EMIT_OK)
        {
            const bool same = tier1_emit_recolor_arguments(call, before, scratch, &old_count) &&
                              tier1_emit_recolor_arguments(call, after, scratch, &new_count) && old_count == new_count;
            error = same ? error : TIER1_EMIT_UNSUPPORTED;
        }
    }
    for (uint32_t e = 0; error == TIER1_EMIT_OK && e < input->edge_count; e += 1)
    {
        const uint32_t old_first = before->edge_move_offsets[e];
        const uint32_t old_past = before->edge_move_offsets[e + 1];
        const uint32_t new_first = after->edge_move_offsets[e];
        const uint32_t new_past = after->edge_move_offsets[e + 1];
        uint32_t old_count = 0, new_count = 0;
        uint32_t recipe = new_first;
        for (uint32_t m = old_first; error == TIER1_EMIT_OK && m < old_past; m += 1)
        {
            const uint32_t from = before->move_sources[m];
            old_count += (from >= BUSTER_TIER1_CFG_SPILL && from != BUSTER_TIER1_CFG_REMATERIALIZE) || before->move_destinations[m] >= BUSTER_TIER1_CFG_SPILL;
            if (from == BUSTER_TIER1_CFG_REMATERIALIZE)
            {
                while (recipe < new_past && after->move_sources[recipe] != BUSTER_TIER1_CFG_REMATERIALIZE)
                {
                    recipe += 1;
                }
                const bool same = recipe < new_past && before->move_values[m] == after->move_values[recipe] &&
                                  before->move_destinations[m] == after->move_destinations[recipe];
                error = same ? error : TIER1_EMIT_UNSUPPORTED;
                recipe += recipe < new_past;
            }
        }
        for (uint32_t m = new_first; m < new_past; m += 1)
        {
            const uint32_t from = after->move_sources[m];
            new_count += (from >= BUSTER_TIER1_CFG_SPILL && from != BUSTER_TIER1_CFG_REMATERIALIZE) || after->move_destinations[m] >= BUSTER_TIER1_CFG_SPILL;
            error = error == TIER1_EMIT_OK && m >= recipe && from == BUSTER_TIER1_CFG_REMATERIALIZE ? TIER1_EMIT_UNSUPPORTED : error;
        }
        const int64_t memory = (int64_t)old_count - new_count;
        const int64_t transport = (int64_t)(old_past - old_first) - (new_past - new_first);
        built.memory_edges[e] = memory;
        built.records.edge_gains[e] = transport;
        built.memory += (__int128)model->edge_weights[e] * memory;
        built.transport += (__int128)model->edge_weights[e] * transport;
    }
    if (error == TIER1_EMIT_OK)
    {
        error = tier1_emit_region_branch_gains(plan, input, before, after, model, work, &built.records);
        built.total = built.transport - built.records.cost.edges;
    }
    arena_pop_to(scratch, temporary);
    if (error == TIER1_EMIT_OK)
    {
        *result = built;
    }
    else
    {
        arena_pop_to(scratch, mark);
    }
    return error;
}

// A positive heuristic sum does not prove a beneficial complete path. Construct actual entry-prefix and
// terminal-suffix trees, then concatenate them at every reachable vertex and edge. Each sum names a finite real
// entry-to-terminal walk, including when the two trees overlap around a loop. The all-path proofs remain separate.
static Tier1EmitError tier1_emit_spill_regions_witness(const Tier1CfgInput* input, const int64_t* blocks,
                                                       const int64_t* edges, Arena* scratch, uint64_t* work, bool* result)
{
    const uint64_t mark = scratch->length;
    const uint32_t count = input->block_count;
    bool ok = true;
    Tier1EmitError error = tier1_emit_recolor_charge((uint64_t)count * 16 + input->edge_count * UINT64_C(12), work) ? TIER1_EMIT_OK : TIER1_EMIT_LIMIT;
    int64_t* prefix = error == TIER1_EMIT_OK ? ir_allocate(scratch, (uint64_t)count + 1, 8, &ok) : 0;
    int64_t* suffix = error == TIER1_EMIT_OK && ok ? ir_allocate(scratch, (uint64_t)count + 1, 8, &ok) : 0;
    uint8_t* reached = error == TIER1_EMIT_OK && ok ? ir_allocate(scratch, (uint64_t)count + 1, 1, &ok) : 0;
    uint8_t* returns = error == TIER1_EMIT_OK && ok ? ir_allocate(scratch, (uint64_t)count + 1, 1, &ok) : 0;
    uint32_t* queue = error == TIER1_EMIT_OK && ok ? ir_allocate(scratch, (uint64_t)count + 1, 4, &ok) : 0;
    uint32_t* cursors = error == TIER1_EMIT_OK && ok ? ir_allocate(scratch, (uint64_t)count + 1, 4, &ok) : 0;
    uint32_t* sources = error == TIER1_EMIT_OK && ok ? ir_allocate(scratch, (uint64_t)input->edge_count + 1, 4, &ok) : 0;
    uint32_t* reverse = error == TIER1_EMIT_OK && ok ? ir_allocate(scratch, (uint64_t)input->edge_count + 1, 4, &ok) : 0;
    error = error == TIER1_EMIT_OK && !ok ? TIER1_EMIT_MEMORY : error;
    uint32_t pending = 0;
    if (error == TIER1_EMIT_OK)
    {
        memcpy(cursors, input->predecessor_offsets, (uint64_t)count * 4);
        queue[pending++] = 0;
        reached[0] = 1;
    }
    for (uint32_t b = 0; error == TIER1_EMIT_OK && b < count; b += 1)
    {
        for (uint32_t e = input->successor_offsets[b]; e < input->successor_offsets[b + 1]; e += 1)
        {
            sources[e] = b;
            reverse[cursors[input->successors[e]]++] = e;
        }
    }
    for (uint32_t k = 0; error == TIER1_EMIT_OK && k < pending; k += 1)
    {
        const uint32_t b = queue[k];
        for (uint32_t e = input->successor_offsets[b]; error == TIER1_EMIT_OK && e < input->successor_offsets[b + 1]; e += 1)
        {
            const uint32_t t = input->successors[e];
            if (reached[t] == 0)
            {
                int64_t sum = 0;
                const bool overflow = __builtin_add_overflow(prefix[b], blocks[b], &sum) || __builtin_add_overflow(sum, edges[e], &prefix[t]);
                error = overflow ? TIER1_EMIT_UNSUPPORTED : error;
                reached[t] = 1;
                queue[pending++] = t;
            }
        }
    }
    pending = 0;
    for (uint32_t b = 0; error == TIER1_EMIT_OK && b < count; b += 1)
    {
        if (input->successor_offsets[b] == input->successor_offsets[b + 1])
        {
            suffix[b] = blocks[b];
            returns[b] = 1;
            queue[pending++] = b;
        }
    }
    for (uint32_t k = 0; error == TIER1_EMIT_OK && k < pending; k += 1)
    {
        const uint32_t b = queue[k];
        for (uint32_t p = input->predecessor_offsets[b]; error == TIER1_EMIT_OK && p < input->predecessor_offsets[b + 1]; p += 1)
        {
            const uint32_t e = reverse[p];
            const uint32_t from = sources[e];
            if (returns[from] == 0)
            {
                int64_t sum = 0;
                const bool overflow = __builtin_add_overflow(blocks[from], edges[e], &sum) || __builtin_add_overflow(sum, suffix[b], &suffix[from]);
                error = overflow ? TIER1_EMIT_UNSUPPORTED : error;
                returns[from] = 1;
                queue[pending++] = from;
            }
        }
    }
    bool witness = false;
    for (uint32_t b = 0; error == TIER1_EMIT_OK && !witness && b < count; b += 1)
    {
        int64_t sum = 0;
        if (reached[b] != 0 && returns[b] != 0)
        {
            error = __builtin_add_overflow(prefix[b], suffix[b], &sum) ? TIER1_EMIT_UNSUPPORTED : error;
            witness = error == TIER1_EMIT_OK && sum > 0;
        }
        for (uint32_t e = input->successor_offsets[b]; error == TIER1_EMIT_OK && !witness && reached[b] != 0 && e < input->successor_offsets[b + 1]; e += 1)
        {
            const uint32_t t = input->successors[e];
            if (returns[t] != 0)
            {
                const bool overflow = __builtin_add_overflow(prefix[b], blocks[b], &sum) || __builtin_add_overflow(sum, edges[e], &sum) ||
                                      __builtin_add_overflow(sum, suffix[t], &sum);
                error = overflow ? TIER1_EMIT_UNSUPPORTED : error;
                witness = error == TIER1_EMIT_OK && sum > 0;
            }
        }
    }
    if (error == TIER1_EMIT_OK)
    {
        *result = witness;
    }
    arena_pop_to(scratch, mark);
    return error;
}

static Tier1EmitRegionPathStatus tier1_emit_spill_regions_proof(const Tier1CfgInput* input, const Tier1EmitSpillScore* score,
                                                               Arena* scratch, uint64_t* work, uint32_t* failed)
{
    Tier1EmitRegionPathStatus status = tier1_emit_region_path_gain(input, score->records.block_gains, score->memory_edges,
                                                                  scratch, BUSTER_TIER1_RECOLOR_ACTIVE_WORK, work, 0);
    uint32_t metric = status != TIER1_REGION_PATH_OK ? 1 : 0;
    if (status == TIER1_REGION_PATH_OK)
    {
        status = tier1_emit_region_path_gain(input, score->records.block_gains, score->records.edge_gains,
                                              scratch, BUSTER_TIER1_RECOLOR_ACTIVE_WORK, work, 0);
        metric = status != TIER1_REGION_PATH_OK ? 2 : metric;
    }
    if (status == TIER1_REGION_PATH_OK)
    {
        status = tier1_emit_region_path_gain(input, score->records.block_gains, score->records.total_edge_gains,
                                              scratch, BUSTER_TIER1_RECOLOR_ACTIVE_WORK, work, 0);
        metric = status != TIER1_REGION_PATH_OK ? 3 : metric;
    }
    *failed = metric;
    return status;
}

static void tier1_emit_cfg_spill_regions(const Tier1SelectPlan* plan, const Tier1EmitResult* emitted,
                                         Arena* output, Arena* scratch, const Tier1CfgInput* input,
                                         Tier1CfgAllocation* cfg, uint32_t* retained_rows)
{
    const uint64_t mark = scratch->length;
    const uint64_t output_mark = output->length;
    const Tier1CfgAllocation original = *cfg;
    const IrFunction* function = plan->function;
#if defined(BUSTER_TIER1_MEASURE_KINDS)
    const uint64_t previous_limit = tier1_emit_recolor_work_limit;
    tier1_emit_recolor_work_limit = tier1_emit_recolor_measure_limit();
    const char* trace_env = getenv("BUSTER_TIER1_EXCHANGE_TRACE");
    const bool trace = trace_env != 0 && (uint32_t)strtoul(trace_env, 0, 10) == function->instruction_count;
#endif
    uint64_t work = 0;
    bool ok = function->block_count != 0 && original.allocation.spill_count != 0 && original.split_count == 0 &&
              input->definition_registers != 0 && output != scratch && emitted->prepared.call_count != 0;
    bool bounded = true;
    const uint64_t initial_work = BUSTER_TIER1_FREQUENCY_WORK + function->instruction_count * UINT64_C(96) +
                                  plan->effective_use_offsets[function->instruction_count] * UINT64_C(4) +
                                  original.allocation.fragment_count * UINT64_C(192) + original.intervals.count * UINT64_C(64) +
                                  input->sequence_count + input->fixed.count + input->position_count + input->value_count * UINT64_C(32) +
                                  64 * ((uint64_t)input->block_count + input->edge_count + 1) * ((uint64_t)input->value_count + 1) +
                                  BUSTER_TIER1_CFG_BLOCK_INTERVALS * UINT64_C(8);
    bounded = !ok || tier1_emit_recolor_charge(initial_work, &work);
    Tier1EmitRecolorModel model = { 0 };
    ok = !bounded ? ok : ok && tier1_emit_recolor_model(plan, emitted, input, scratch, &model);
    uint8_t* parameters = ok && bounded ? ir_allocate(scratch, (uint64_t)input->value_count + 1, 1, &ok) : 0;
    uint8_t* sources = ok && bounded ? ir_allocate(scratch, (uint64_t)input->value_count + 1, 1, &ok) : 0;
    uint32_t candidates = 0;
    for (uint32_t node = 0; ok && bounded && node < function->block_offsets[1]; node += 1)
    {
        if (tier1_select_entry_parameter(function, node))
        {
            const uint32_t value = emitted->node_values[node];
            const Tier1SelectPlace place = tier1_select_parameter_place(function, function->a[node]);
            const uint32_t binding = value < input->value_count ? input->definition_registers[value] : 0;
            const bool eligible = value < input->value_count && place.kind == BUSTER_TIER1_PLACE_GENERAL && binding != 0 &&
                                  (binding & (binding - 1)) == 0 && (binding & (model.callee | model.forbidden | (1u << X86_REGISTER_RAX))) == 0 &&
                                  input->classes[value] == BUSTER_TIER1_ALLOCATE_GPR && original.allocation.spill_ids[value] < original.allocation.spill_count &&
                                  (emitted->prepared.sizes[node] == 4 || emitted->prepared.sizes[node] == 8);
            if (eligible)
            {
                parameters[value] = 1;
                sources[value] = (uint8_t)__builtin_ctz(binding);
                candidates += 1;
            }
        }
    }
    Tier1CfgAllocation current = original;
    Tier1CfgPrepared prepared = { 0 };
    Tier1EmitResult prior = *emitted;
    if (ok && bounded && candidates != 0)
    {
        prior.cfg_input = *input;
        prior.cfg = original;
        ok = tier1_emit_cfg_saves(plan, scratch, &prior) == TIER1_EMIT_OK;
        ok = ok && tier1_emit_partition_parameters(input, parameters, output, scratch, &current);
        ok = ok && tier1_cfg_prepare(input, output, scratch, &prepared) == TIER1_CFG_OK;
    }
    uint32_t selected = 0, trials = 0, rejected = 0;
    __int128 memory = 0, transport = 0, total = 0;
    for (uint32_t value = 0; ok && bounded && candidates != 0 && value < input->value_count; value += 1)
    {
        if (parameters[value] == 0)
        {
            continue;
        }
        const uint64_t trial_output = output->length;
        const uint64_t trial_scratch = scratch->length;
        Tier1EmitSpillRegions regions = { 0 };
        Tier1CfgAllocation candidate = current;
        Tier1EmitSpillScore score = { 0 };
        Tier1EmitError error = tier1_emit_spill_regions_plan(plan, emitted, input, &current, &model, value, sources[value], output, scratch, &work, &regions);
        error = error == TIER1_EMIT_OK && regions.blocks == 0 ? TIER1_EMIT_UNSUPPORTED : error;
        trials += error == TIER1_EMIT_OK;
        error = error == TIER1_EMIT_OK ? tier1_emit_spill_regions_materialize(input, &prepared, &current, &regions, value, sources[value], output, scratch, &work, &candidate) : error;
        if (error == TIER1_EMIT_OK)
        {
            const uint32_t before = tier1_emit_cfg_callee_used(&current, model.callee);
            const uint32_t after = tier1_emit_cfg_callee_used(&candidate, model.callee);
            error = before == after ? error : TIER1_EMIT_UNSUPPORTED;
        }
        error = error == TIER1_EMIT_OK ? tier1_emit_spill_regions_score(plan, emitted, input, &current, &candidate, &model, value, scratch, &work, &score) : error;
        bool witness = false;
        uint32_t failed = 0;
        Tier1EmitRegionPathStatus path = TIER1_REGION_PATH_INVALID;
        if (error == TIER1_EMIT_OK && score.memory > 0 && score.transport >= 0 && score.total >= 0)
        {
            path = tier1_emit_spill_regions_proof(input, &score, scratch, &work, &failed);
            if (path == TIER1_REGION_PATH_OK)
            {
                error = tier1_emit_spill_regions_witness(input, score.records.block_gains, score.memory_edges, scratch, &work, &witness);
            }
        }
        const bool accepted = error == TIER1_EMIT_OK && path == TIER1_REGION_PATH_OK && witness;
        bounded = error != TIER1_EMIT_LIMIT && path != TIER1_REGION_PATH_LIMIT;
        ok = error != TIER1_EMIT_MEMORY && path != TIER1_REGION_PATH_MEMORY;
        if (accepted)
        {
            current = candidate;
            selected += 1;
            memory += score.memory;
            transport += score.transport;
            total += score.total;
        }
        else
        {
            rejected += 1;
            arena_pop_to(output, trial_output);
        }
#if defined(BUSTER_TIER1_MEASURE_KINDS)
        if (trace)
        {
            fprintf(stderr, "SPILL_REGION trial instructions=%u value=%u source=%u roots=%u blocks=%u error=%u path=%u metric=%u witness=%u selected=%u memory=%.12Lf transport=%.12Lf total=%.12Lf\n",
                    function->instruction_count, value, sources[value], regions.roots, regions.blocks, error, path, failed, witness, accepted,
                    (long double)score.memory / (UINT64_C(1) << 32), (long double)score.transport / (UINT64_C(1) << 32), (long double)score.total / (UINT64_C(1) << 32));
        }
#endif
        arena_pop_to(scratch, trial_scratch);
    }
    const bool published = ok && selected != 0;
    if (published)
    {
        current.retained_bytes = original.retained_bytes + output->length - output_mark;
        *cfg = current;
        *retained_rows |= prior.prepared.call_save_mask;
    }
    else
    {
        arena_pop_to(output, output_mark);
    }
#if defined(BUSTER_TIER1_MEASURE_KINDS)
    if (trace)
    {
        fprintf(stderr, "SPILL_REGION summary instructions=%u supported=%u bounded=%u candidates=%u trials=%u selected=%u rejected=%u published=%u work=%llu memory=%.12Lf transport=%.12Lf total=%.12Lf\n",
                function->instruction_count, ok, bounded, candidates, trials, selected, rejected, published, (unsigned long long)work,
                (long double)memory / (UINT64_C(1) << 32), (long double)transport / (UINT64_C(1) << 32), (long double)total / (UINT64_C(1) << 32));
    }
    tier1_emit_recolor_work_limit = previous_limit;
#else
    (void)trials;
    (void)rejected;
    (void)memory;
    (void)transport;
    (void)total;
#endif
    arena_pop_to(scratch, mark);
}
