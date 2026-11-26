// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

// The same ordered block state walk as the qualified full helper. Only immutable lookups have been cached. Owners
// and owed reset, rows follow the actual carry parent, and sparse preloads restore persistent register-cell reads.
static Tier1EmitError tier1_emit_epoch_cache_block(const Tier1EmitEpochCache* cache, const Tier1CfgAllocation* cfg,
                                                  uint32_t block, const uint32_t* parent_rows, uint8_t* registers,
                                                  uint32_t* stores, uint32_t* loads, uint32_t* exit_rows,
                                                  uint64_t limit, uint64_t* work)
{
    const Tier1SelectPlan* plan = cache->plan;
    const Tier1EmitResult* emitted = cache->emitted;
    const Tier1Allocation* allocation = &cfg->allocation;
    const IrFunction* function = plan->function;
    Tier1EmitTransitions transitions = {
        .offsets = cache->transition_offsets,
        .fragments = cache->transition_fragments,
        .values = cache->fragment_nodes,
        .registers = registers,
    };
    Tier1EmitSaves saves;
    const uint64_t initialization = 1 + BUSTER_TIER1_EMIT_LOCATIONS + 48 +
                                    (uint64_t)cache->preload_offsets[block + 1] - cache->preload_offsets[block] +
                                    cache->entry_offsets[block + 1] - cache->entry_offsets[block];
    Tier1EmitError error = tier1_emit_epoch_cache_charge(initialization, limit, work) ? TIER1_EMIT_OK : TIER1_EMIT_LIMIT;
    if (error == TIER1_EMIT_OK)
    {
        tier1_emit_saves_reset(&saves);
        tier1_emit_epoch_cache_fill(transitions.owners, BUSTER_TIER1_EMIT_LOCATIONS, BUSTER_IR_NONE);
        _mm512_storeu_si512(stores, _mm512_setzero_si512());
        _mm512_storeu_si512(loads, _mm512_setzero_si512());
        if (cache->carry_parent[block] != BUSTER_TIER1_CFG_NONE)
        {
            error = parent_rows != 0 ? error : TIER1_EMIT_INVALID;
            if (error == TIER1_EMIT_OK)
            {
                _mm512_storeu_si512(saves.rows, _mm512_loadu_si512(parent_rows));
            }
        }
    }
#if defined(BUSTER_TIER1_EPOCH_CACHE_PROOF)
    // Poison every unrelated cell in the differential build. Complete sparse reads must not depend on which block
    // happened to replay before this one; this dense proof-only write is absent from the scorer's normal build.
    if (error == TIER1_EMIT_OK)
    {
        memset(registers, 0xa5, (uint64_t)cache->nodes + 1);
    }
#endif
    for (uint32_t x = cache->preload_offsets[block]; error == TIER1_EMIT_OK && x < cache->preload_offsets[block + 1]; x += 1)
    {
        registers[cache->preload_nodes[x]] = tier1_emit_epoch_cache_expression(cache, allocation, cache->preload_fragments[x]);
    }
    for (uint32_t x = cache->entry_offsets[block]; error == TIER1_EMIT_OK && x < cache->entry_offsets[block + 1]; x += 1)
    {
        const uint32_t node = cache->entry_nodes[x];
        const uint8_t location = tier1_emit_epoch_cache_expression(cache, allocation, cache->entry_fragments[x]);
        registers[node] = location;
        if (location != UINT8_MAX)
        {
            transitions.owners[location] = node;
        }
    }
    const uint32_t first = block < cache->added ? 0 : function->block_offsets[block - cache->added];
    const uint32_t end = block < cache->added ? 0 : function->block_offsets[block - cache->added + 1];
    for (uint32_t node = first; error == TIER1_EMIT_OK && node < end; node += 1)
    {
        const uint32_t use = cache->base + 2 * node;
        const bool selected = (cache->flags[node] & BUSTER_TIER1_EPOCH_SELECTED) != 0;
        const bool calls = (cache->flags[node] & BUSTER_TIER1_EPOCH_CALL) != 0;
        const bool leaves = (cache->flags[node] & BUSTER_TIER1_EPOCH_LEAVES) != 0;
        const uint32_t fragment = cache->definitions[node];
        error = tier1_emit_epoch_cache_charge(1, limit, work) ? error : TIER1_EMIT_LIMIT;
        if (error == TIER1_EMIT_OK && saves.owed != 0)
        {
            const uint64_t reads = (uint64_t)plan->effective_use_offsets[node + 1] - plan->effective_use_offsets[node] +
                                   (selected ? BUSTER_TIER1_SELECT_VALUES : 0) +
                                   cache->transition_offsets[use + 2] - cache->transition_offsets[use];
            error = tier1_emit_epoch_cache_charge(reads, limit, work) ? error : TIER1_EMIT_LIMIT;
            if (error == TIER1_EMIT_OK)
            {
                const bool general = !calls && fragment != BUSTER_TIER1_CFG_NONE &&
                                     tier1_emit_class(&emitted->prepared, node) == BUSTER_TIER1_ALLOCATE_GPR;
                const uint8_t defined = general ? allocation->registers[fragment] : UINT8_MAX;
                const uint32_t repaid = tier1_emit_owed(plan, &emitted->prepared, allocation, &transitions, node, use,
                                                       defined, selected, node + 1 == end && !leaves, saves.owed);
                tier1_emit_exchange_epoch_count(loads, repaid);
                saves.owed &= ~repaid;
            }
        }
        const uint32_t phases = cache->transition_offsets[use + 2] - cache->transition_offsets[use];
        error = error != TIER1_EMIT_OK || tier1_emit_epoch_cache_charge(phases + (fragment != BUSTER_TIER1_CFG_NONE), limit, work) ? error : TIER1_EMIT_LIMIT;
        if (error == TIER1_EMIT_OK)
        {
            tier1_emit_exchange_epoch_phase(&emitted->prepared, allocation, &transitions, use);
            if (fragment != BUSTER_TIER1_CFG_NONE)
            {
                registers[node] = allocation->kinds[fragment] == BUSTER_TIER1_ALLOCATE_REGISTER ?
                                  tier1_emit_epoch_cache_expression(cache, allocation, fragment) : UINT8_MAX;
            }
            tier1_emit_exchange_epoch_phase(&emitted->prepared, allocation, &transitions, use + 1);
        }
        if (error == TIER1_EMIT_OK && calls)
        {
            error = tier1_emit_epoch_cache_charge(16, limit, work) ? error : TIER1_EMIT_LIMIT;
            uint32_t saved = 0;
            for (uint32_t reg = 0; error == TIER1_EMIT_OK && reg < 16; reg += 1)
            {
                const uint32_t owner = transitions.owners[reg];
                const bool live = owner != BUSTER_IR_NONE && owner != node && registers[owner] == reg &&
                                  tier1_cfg_interval_at(cfg, emitted->node_values[owner], use + 1) != BUSTER_TIER1_CFG_NONE;
                saved |= live ? 1u << reg : 0;
            }
            if (error == TIER1_EMIT_OK)
            {
                const uint32_t stored = saved & ~tier1_emit_kept(&saves, &transitions, saved);
                tier1_emit_exchange_epoch_count(stores, stored);
                tier1_emit_saved(&saves, &transitions, saved);
            }
        }
        else if (error == TIER1_EMIT_OK && selected && plan->patterns[node].rule == TIER1_RULE_ASM)
        {
            tier1_emit_fixed_rows(&saves, tier1_select_node_fixed(function, &plan->patterns[node]));
        }
        else if (error == TIER1_EMIT_OK && selected && tier1_select_fixed_registers(plan->patterns[node].rule) != 0)
        {
            tier1_emit_fixed_rows(&saves, tier1_select_fixed_registers(plan->patterns[node].rule));
        }
        if (error == TIER1_EMIT_OK && fragment != BUSTER_TIER1_CFG_NONE && cache->dropped[node] == 0 &&
            tier1_emit_cfg_holds(emitted, node, registers[node]))
        {
            transitions.owners[registers[node]] = node;
        }
        if (error == TIER1_EMIT_OK && leaves)
        {
            saves.owed = 0;
        }
    }
    error = error != TIER1_EMIT_OK || saves.owed == 0 ? error : TIER1_EMIT_INVALID;
    if (error == TIER1_EMIT_OK)
    {
        _mm512_storeu_si512(exit_rows, _mm512_loadu_si512(saves.rows));
    }
    return error;
}

static Tier1EmitError tier1_emit_epoch_cache_prepare(const Tier1SelectPlan* plan, const Tier1EmitResult* emitted,
                                                    const Tier1CfgInput* input, const Tier1CfgAllocation* cfg,
                                                    Arena* output, Arena* scratch, uint64_t limit, uint64_t* work,
                                                    Tier1EmitEpochCache* result)
{
    const uint64_t output_mark = output != 0 ? output->length : 0;
    const uint64_t scratch_mark = scratch != 0 ? scratch->length : 0;
    bool valid = plan != 0 && emitted != 0 && input != 0 && cfg != 0 && output != 0 && scratch != 0 &&
                 output != scratch && output->base != scratch->base && result != 0 && work != 0 && plan->function != 0;
    Tier1EmitEpochCache built = { 0 };
    if (valid)
    {
        built.plan = plan;
        built.emitted = emitted;
        built.input = input;
        built.shape = *cfg;
        built.nodes = plan->function->instruction_count;
        built.blocks = input->block_count;
        built.added = built.blocks - plan->function->block_count;
        built.base = 2 * built.added;
        built.generation = 1;
        valid = built.blocks != 0 && built.blocks >= plan->function->block_count &&
                input->position_count >= 2 * (uint64_t)built.added + 2 * (uint64_t)built.nodes;
    }
    Tier1EmitError error = valid ? TIER1_EMIT_OK : TIER1_EMIT_INVALID;
    const uint32_t fragments = valid ? cfg->allocation.fragment_count : 0;
    const uint64_t rows = (uint64_t)built.blocks * 16;
    built.registers = tier1_emit_epoch_cache_allocate(output, (uint64_t)fragments + 1, 1, &error);
    built.stores = tier1_emit_epoch_cache_allocate(output, rows, 4, &error);
    built.loads = tier1_emit_epoch_cache_allocate(output, rows, 4, &error);
    built.exit_rows = tier1_emit_epoch_cache_allocate(output, rows, 4, &error);
    error = error != TIER1_EMIT_OK || tier1_emit_epoch_cache_charge((uint64_t)fragments + 1, limit, work) ? error : TIER1_EMIT_LIMIT;
    if (error == TIER1_EMIT_OK)
    {
        if (fragments != 0)
        {
            memcpy(built.registers, cfg->allocation.registers, fragments);
        }
        built.registers[fragments] = UINT8_MAX;
    }
    error = error != TIER1_EMIT_OK ? error : tier1_emit_epoch_cache_events(&built, output, scratch, limit, work);
    error = error != TIER1_EMIT_OK ? error : tier1_emit_epoch_cache_preloads(&built, output, scratch, limit, work);
    error = error != TIER1_EMIT_OK ? error : tier1_emit_epoch_cache_dependencies(&built, output, scratch, limit, work);
    uint8_t* registers = tier1_emit_epoch_cache_allocate(scratch, (uint64_t)built.nodes + 1, 1, &error);
    for (uint32_t b = 0; error == TIER1_EMIT_OK && b < built.blocks; b += 1)
    {
        const uint32_t parent = built.carry_parent[b];
        const uint32_t* inherited = parent != BUSTER_TIER1_CFG_NONE ? built.exit_rows + (uint64_t)parent * 16 : 0;
        error = tier1_emit_epoch_cache_block(&built, cfg, b, inherited, registers, built.stores + (uint64_t)b * 16,
                                             built.loads + (uint64_t)b * 16, built.exit_rows + (uint64_t)b * 16, limit, work);
    }
    if (scratch != 0)
    {
        arena_pop_to(scratch, scratch_mark);
    }
    if (error == TIER1_EMIT_OK)
    {
        *result = built;
    }
    else if (output != 0)
    {
        arena_pop_to(output, output_mark);
    }
    return error;
}
