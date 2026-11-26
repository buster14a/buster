// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

// Cache exactly the entry and phase events of the full walker, then turn every persistent register-cell read into
// an expression over an immutable fragment ID. No physical register value influences this metadata construction.
static Tier1EmitError tier1_emit_epoch_cache_events(Tier1EmitEpochCache* cache, Arena* output, Arena* scratch,
                                                    uint64_t limit, uint64_t* work)
{
    const Tier1SelectPlan* plan = cache->plan;
    const Tier1EmitResult* emitted = cache->emitted;
    const Tier1CfgInput* input = cache->input;
    const Tier1CfgAllocation* cfg = &cache->shape;
    const Tier1Allocation* allocation = &cfg->allocation;
    const IrFunction* function = plan->function;
    const uint32_t nodes = cache->nodes;
    const uint32_t blocks = cache->blocks;
    const uint32_t fragments = allocation->fragment_count;
    const uint64_t mark = scratch->length;
    Tier1EmitError error = TIER1_EMIT_OK;
    cache->transition_offsets = tier1_emit_epoch_cache_allocate(output, (uint64_t)input->position_count + 2, 4, &error);
    cache->transition_fragments = tier1_emit_epoch_cache_allocate(output, (uint64_t)fragments + 1, 4, &error);
    cache->fragment_nodes = tier1_emit_epoch_cache_allocate(output, (uint64_t)fragments + 1, 4, &error);
    cache->fragment_bases = tier1_emit_epoch_cache_allocate(output, (uint64_t)fragments + 1, 1, &error);
    cache->entry_offsets = tier1_emit_epoch_cache_allocate(output, (uint64_t)blocks + 1, 4, &error);
    cache->definitions = tier1_emit_epoch_cache_allocate(output, (uint64_t)nodes + 1, 4, &error);
    cache->flags = tier1_emit_epoch_cache_allocate(output, (uint64_t)nodes + 1, 1, &error);
    cache->dropped = tier1_emit_epoch_cache_allocate(output, (uint64_t)nodes + 1, 1, &error);
    cache->carry_parent = tier1_emit_epoch_cache_allocate(output, (uint64_t)blocks + 1, 4, &error);
    cache->child_offsets = tier1_emit_epoch_cache_allocate(output, (uint64_t)blocks + 1, 4, &error);
    cache->children = tier1_emit_epoch_cache_allocate(output, (uint64_t)blocks + 1, 4, &error);
    uint32_t* cursors = tier1_emit_epoch_cache_allocate(scratch, (uint64_t)input->position_count + 1, 4, &error);
    uint32_t* entries = tier1_emit_epoch_cache_allocate(scratch, (uint64_t)blocks + 1, 4, &error);
    error = error != TIER1_EMIT_OK || tier1_emit_epoch_cache_charge((uint64_t)input->position_count + 2 * (uint64_t)blocks + 3 * (uint64_t)nodes + 7,
                                                                   limit, work) ? error : TIER1_EMIT_LIMIT;
    if (error == TIER1_EMIT_OK)
    {
        tier1_emit_epoch_cache_fill(cache->transition_offsets, (uint64_t)input->position_count + 2, 0);
        tier1_emit_epoch_cache_fill(cache->entry_offsets, (uint64_t)blocks + 1, 0);
        tier1_emit_epoch_cache_fill(cache->child_offsets, (uint64_t)blocks + 1, 0);
        tier1_emit_epoch_cache_fill(cache->definitions, (uint64_t)nodes + 1, BUSTER_TIER1_CFG_NONE);
        memset(cache->flags, 0, (uint64_t)nodes + 1);
        memset(cache->dropped, 0, (uint64_t)nodes + 1);
    }
    for (uint32_t pass = 0; error == TIER1_EMIT_OK && pass < 2; pass += 1)
    {
        if (pass == 1)
        {
            error = tier1_emit_epoch_cache_charge((uint64_t)input->position_count + blocks, limit, work) ? error : TIER1_EMIT_LIMIT;
            for (uint32_t q = 0; error == TIER1_EMIT_OK && q < input->position_count; q += 1)
            {
                cache->transition_offsets[q + 1] += cache->transition_offsets[q];
                cursors[q] = cache->transition_offsets[q];
            }
            uint64_t entry_count = 0;
            for (uint32_t b = 0; error == TIER1_EMIT_OK && b < blocks; b += 1)
            {
                entry_count += cache->entry_offsets[b + 1];
                error = entry_count < UINT32_MAX ? error : TIER1_EMIT_LIMIT;
                cache->entry_offsets[b + 1] = (uint32_t)entry_count;
                entries[b] = cache->entry_offsets[b];
            }
            cache->entry_nodes = tier1_emit_epoch_cache_allocate(output, (uint64_t)cache->entry_offsets[blocks] + 1, 4, &error);
            cache->entry_fragments = tier1_emit_epoch_cache_allocate(output, (uint64_t)cache->entry_offsets[blocks] + 1, 4, &error);
        }
        for (uint32_t i = 0; error == TIER1_EMIT_OK && i < cfg->intervals.count; i += 1)
        {
            const uint32_t value = cfg->intervals.values[i];
            const uint32_t node = emitted->value_nodes[value];
            const uint32_t first = allocation->fragment_offsets[i];
            const uint32_t past = allocation->fragment_offsets[i + 1];
            const uint32_t start = cfg->intervals.starts[i];
            error = node < nodes && first < past && past <= fragments &&
                    cfg->intervals.classes[i] == tier1_emit_class(&emitted->prepared, node) ? error : TIER1_EMIT_UNSUPPORTED;
            error = error != TIER1_EMIT_OK || tier1_emit_epoch_cache_charge(1 + (uint64_t)past - first, limit, work) ? error : TIER1_EMIT_LIMIT;
            for (uint32_t f = first; error == TIER1_EMIT_OK && f < past; f += 1)
            {
                cache->fragment_nodes[f] = node;
                cache->fragment_bases[f] = (uint8_t)(32 * cfg->intervals.classes[i]);
                if (f != first)
                {
                    const uint32_t q = allocation->starts[f];
                    const uint8_t kind = allocation->kinds[f];
                    const uint8_t previous = allocation->kinds[f - 1];
                    error = q < input->position_count && (kind != BUSTER_TIER1_ALLOCATE_REGISTER || (((q - cache->base) & 1) == 0 &&
                            (previous != BUSTER_TIER1_ALLOCATE_REGISTER || cfg->intervals.classes[i] == BUSTER_TIER1_ALLOCATE_GPR))) ? error : TIER1_EMIT_UNSUPPORTED;
                    if (error == TIER1_EMIT_OK && pass == 0)
                    {
                        cache->transition_offsets[q + 1] += 1;
                    }
                    else if (error == TIER1_EMIT_OK)
                    {
                        cache->transition_fragments[cursors[q]] = f;
                        cursors[q] += 1;
                    }
                }
            }
            uint32_t b = tier1_cfg_block_at(input, cfg, start);
            b += input->block_starts[b] < start;
            for (; error == TIER1_EMIT_OK && b < blocks && input->block_starts[b] < cfg->intervals.ends[i]; b += 1)
            {
                error = tier1_emit_epoch_cache_charge(1, limit, work) ? error : TIER1_EMIT_LIMIT;
                if (error == TIER1_EMIT_OK && pass == 0)
                {
                    error = cache->entry_offsets[b + 1] != UINT32_MAX ? error : TIER1_EMIT_LIMIT;
                    cache->entry_offsets[b + 1] += error == TIER1_EMIT_OK;
                }
                else if (error == TIER1_EMIT_OK)
                {
                    const uint32_t entry = input->block_starts[b];
                    const uint32_t interval = tier1_cfg_interval_at(cfg, value, entry);
                    const uint32_t position = interval != BUSTER_TIER1_CFG_NONE && cfg->intervals.starts[interval] < entry ? entry - 1 : entry;
                    const uint32_t fragment = interval != BUSTER_TIER1_CFG_NONE ? tier1_cfg_interval_fragment(cfg, interval, position) : BUSTER_TIER1_CFG_NONE;
                    uint32_t location = 0;
                    uint8_t kind = 0;
                    error = fragment != BUSTER_TIER1_CFG_NONE && tier1_cfg_entry_location(cfg, value, entry, &location, &kind) ? error : TIER1_EMIT_ALLOCATION;
                    if (error == TIER1_EMIT_OK)
                    {
                        const bool held = kind == BUSTER_TIER1_ALLOCATE_REGISTER && tier1_emit_cfg_register_location(location);
                        cache->entry_nodes[entries[b]] = node;
                        cache->entry_fragments[entries[b]] = held ? fragment : BUSTER_TIER1_CFG_NONE;
                        entries[b] += 1;
                    }
                }
            }
        }
    }
    Tier1EmitResult context = *emitted;
    context.cfg_input = *input;
    context.cfg = *cfg;
    for (uint32_t k = nodes; error == TIER1_EMIT_OK && k > 0; k -= 1)
    {
        error = tier1_emit_epoch_cache_charge(1, limit, work) ? error : TIER1_EMIT_LIMIT;
        const uint32_t node = k - 1;
        const uint32_t value = emitted->node_values[node];
        const uint32_t op = function->op[node];
        const bool selected = !plan->covered[node] && op != IR_PHI && !tier1_select_entry_parameter(function, node);
        const bool calls = tier1_select_function_call(function, node);
        const bool leaves = op == IR_RET || op == IR_UNREACHABLE;
        if (error == TIER1_EMIT_OK)
        {
            cache->flags[node] = (selected ? BUSTER_TIER1_EPOCH_SELECTED : 0) | (calls ? BUSTER_TIER1_EPOCH_CALL : 0) |
                                 (leaves ? BUSTER_TIER1_EPOCH_LEAVES : 0);
            cache->dropped[node] = selected && value != BUSTER_TIER1_CFG_NONE &&
                                   tier1_emit_cfg_dead_definition(&context, value, cache->base + 2 * node + 1, cache->dropped, cache->base);
        }
        if (error == TIER1_EMIT_OK && selected && value != BUSTER_TIER1_CFG_NONE)
        {
            const uint32_t position = cache->base + 2 * node + 1;
            const uint32_t interval = tier1_cfg_interval_at(cfg, value, position);
            error = interval != BUSTER_TIER1_CFG_NONE && cfg->intervals.starts[interval] == position ? error : TIER1_EMIT_ALLOCATION;
            if (error == TIER1_EMIT_OK)
            {
                cache->definitions[node] = allocation->fragment_offsets[interval];
            }
        }
    }
    error = error != TIER1_EMIT_OK || tier1_emit_epoch_cache_charge((uint64_t)blocks * 3, limit, work) ? error : TIER1_EMIT_LIMIT;
    for (uint32_t b = 0; error == TIER1_EMIT_OK && b < blocks; b += 1)
    {
        const uint32_t degree = input->predecessor_offsets[b + 1] - input->predecessor_offsets[b];
        const uint32_t only = degree == 1 ? input->predecessors[input->predecessor_offsets[b]] : UINT32_MAX;
        cache->carry_parent[b] = only < b ? only : BUSTER_TIER1_CFG_NONE;
        if (only < b)
        {
            cache->child_offsets[only + 1] += 1;
        }
    }
    for (uint32_t b = 0; error == TIER1_EMIT_OK && b < blocks; b += 1)
    {
        cache->child_offsets[b + 1] += cache->child_offsets[b];
        entries[b] = cache->child_offsets[b];
    }
    for (uint32_t b = 0; error == TIER1_EMIT_OK && b < blocks; b += 1)
    {
        const uint32_t parent = cache->carry_parent[b];
        if (parent != BUSTER_TIER1_CFG_NONE)
        {
            cache->children[entries[parent]] = b;
            entries[parent] += 1;
        }
    }
    arena_pop_to(scratch, mark);
    return error;
}

static Tier1EmitError tier1_emit_epoch_cache_read(Tier1EmitEpochCache* cache, uint32_t node, uint32_t block, uint32_t pass,
                                                  const uint32_t* last, uint32_t* stamps, uint32_t* cursors)
{
    Tier1EmitError error = node < cache->nodes ? TIER1_EMIT_OK : TIER1_EMIT_INVALID;
    if (error == TIER1_EMIT_OK && stamps[node] != block + 1)
    {
        if (pass == 0)
        {
            error = cache->preload_offsets[block + 1] != UINT32_MAX ? error : TIER1_EMIT_LIMIT;
            cache->preload_offsets[block + 1] += error == TIER1_EMIT_OK;
        }
        else
        {
            cache->preload_nodes[cursors[block]] = node;
            cache->preload_fragments[cursors[block]] = last[node];
            cursors[block] += 1;
        }
        stamps[node] = block + 1;
    }
    return error;
}

static Tier1EmitError tier1_emit_epoch_cache_preloads(Tier1EmitEpochCache* cache, Arena* output, Arena* scratch,
                                                     uint64_t limit, uint64_t* work)
{
    const Tier1SelectPlan* plan = cache->plan;
    const Tier1Allocation* allocation = &cache->shape.allocation;
    const uint64_t mark = scratch->length;
    Tier1EmitError error = TIER1_EMIT_OK;
    cache->preload_offsets = tier1_emit_epoch_cache_allocate(output, (uint64_t)cache->blocks + 1, 4, &error);
    uint32_t* last = tier1_emit_epoch_cache_allocate(scratch, (uint64_t)cache->nodes + 1, 4, &error);
    uint32_t* stamps = tier1_emit_epoch_cache_allocate(scratch, (uint64_t)cache->nodes + 1, 4, &error);
    uint32_t* cursors = tier1_emit_epoch_cache_allocate(scratch, (uint64_t)cache->blocks + 1, 4, &error);
    error = error != TIER1_EMIT_OK || tier1_emit_epoch_cache_charge((uint64_t)cache->blocks + 1, limit, work) ? error : TIER1_EMIT_LIMIT;
    if (error == TIER1_EMIT_OK)
    {
        tier1_emit_epoch_cache_fill(cache->preload_offsets, (uint64_t)cache->blocks + 1, 0);
    }
    for (uint32_t pass = 0; error == TIER1_EMIT_OK && pass < 2; pass += 1)
    {
        error = tier1_emit_epoch_cache_charge(2 * ((uint64_t)cache->nodes + 1) + (pass == 1 ? cache->blocks : 0),
                                              limit, work) ? error : TIER1_EMIT_LIMIT;
        if (error == TIER1_EMIT_OK)
        {
            tier1_emit_epoch_cache_fill(last, (uint64_t)cache->nodes + 1, BUSTER_TIER1_CFG_NONE);
            tier1_emit_epoch_cache_fill(stamps, (uint64_t)cache->nodes + 1, 0);
        }
        if (error == TIER1_EMIT_OK && pass == 1)
        {
            uint64_t total = 0;
            for (uint32_t b = 0; error == TIER1_EMIT_OK && b < cache->blocks; b += 1)
            {
                total += cache->preload_offsets[b + 1];
                error = total < UINT32_MAX ? error : TIER1_EMIT_LIMIT;
                cache->preload_offsets[b + 1] = (uint32_t)total;
                cursors[b] = cache->preload_offsets[b];
            }
            cache->preload_nodes = tier1_emit_epoch_cache_allocate(output, total + 1, 4, &error);
            cache->preload_fragments = tier1_emit_epoch_cache_allocate(output, total + 1, 4, &error);
        }
        for (uint32_t b = 0; error == TIER1_EMIT_OK && b < cache->blocks; b += 1)
        {
            error = tier1_emit_epoch_cache_charge(1 + (uint64_t)cache->entry_offsets[b + 1] - cache->entry_offsets[b], limit, work) ? error : TIER1_EMIT_LIMIT;
            for (uint32_t x = cache->entry_offsets[b]; error == TIER1_EMIT_OK && x < cache->entry_offsets[b + 1]; x += 1)
            {
                const uint32_t node = cache->entry_nodes[x];
                last[node] = cache->entry_fragments[x];
                stamps[node] = b + 1;
            }
            const uint32_t first = b < cache->added ? 0 : plan->function->block_offsets[b - cache->added];
            const uint32_t end = b < cache->added ? 0 : plan->function->block_offsets[b - cache->added + 1];
            for (uint32_t node = first; error == TIER1_EMIT_OK && node < end; node += 1)
            {
                const bool selected = (cache->flags[node] & BUSTER_TIER1_EPOCH_SELECTED) != 0;
                const uint32_t use = cache->base + 2 * node;
                const uint32_t def = cache->definitions[node];
                const uint64_t charge = 1 + (uint64_t)plan->effective_use_offsets[node + 1] - plan->effective_use_offsets[node] +
                                         (selected ? BUSTER_TIER1_SELECT_VALUES : 0) +
                                         cache->transition_offsets[use + 2] - cache->transition_offsets[use];
                error = tier1_emit_epoch_cache_charge(charge, limit, work) ? error : TIER1_EMIT_LIMIT;
                // These are potential reads even when the baseline has no owed rows. They precede use transitions.
                for (uint32_t u = plan->effective_use_offsets[node]; error == TIER1_EMIT_OK && u < plan->effective_use_offsets[node + 1]; u += 1)
                {
                    error = tier1_emit_epoch_cache_read(cache, plan->effective_use_values[u], b, pass, last, stamps, cursors);
                }
                for (uint32_t p = 0; error == TIER1_EMIT_OK && selected && p < BUSTER_TIER1_SELECT_VALUES; p += 1)
                {
                    const uint32_t value = plan->patterns[node].values[p];
                    if (value < cache->nodes)
                    {
                        error = tier1_emit_epoch_cache_read(cache, value, b, pass, last, stamps, cursors);
                    }
                }
                for (uint32_t phase = 0; error == TIER1_EMIT_OK && phase < 2; phase += 1)
                {
                    if (phase == 1 && def != BUSTER_TIER1_CFG_NONE)
                    {
                        last[node] = allocation->kinds[def] == BUSTER_TIER1_ALLOCATE_REGISTER ? def : BUSTER_TIER1_CFG_NONE;
                        stamps[node] = b + 1;
                    }
                    for (uint32_t e = cache->transition_offsets[use + phase]; e < cache->transition_offsets[use + phase + 1]; e += 1)
                    {
                        const uint32_t f = cache->transition_fragments[e];
                        const uint32_t value = cache->fragment_nodes[f];
                        last[value] = allocation->kinds[f] == BUSTER_TIER1_ALLOCATE_REGISTER ? f : BUSTER_TIER1_CFG_NONE;
                        stamps[value] = b + 1;
                    }
                }
            }
        }
    }
    arena_pop_to(scratch, mark);
    return error;
}

static Tier1EmitError tier1_emit_epoch_cache_dependency(Tier1EmitEpochCache* cache, uint32_t fragment, uint32_t block,
                                                        uint32_t pass, uint32_t* stamps, uint32_t* cursors)
{
    Tier1EmitError error = TIER1_EMIT_OK;
    if (fragment != BUSTER_TIER1_CFG_NONE && stamps[fragment] != block + 1)
    {
        stamps[fragment] = block + 1;
        if (pass == 0)
        {
            error = cache->dependency_offsets[fragment + 1] != UINT32_MAX ? error : TIER1_EMIT_LIMIT;
            cache->dependency_offsets[fragment + 1] += error == TIER1_EMIT_OK;
        }
        else
        {
            cache->dependency_blocks[cursors[fragment]] = block;
            cursors[fragment] += 1;
        }
    }
    return error;
}

static Tier1EmitError tier1_emit_epoch_cache_dependencies(Tier1EmitEpochCache* cache, Arena* output, Arena* scratch,
                                                         uint64_t limit, uint64_t* work)
{
    const uint32_t fragments = cache->shape.allocation.fragment_count;
    const uint64_t mark = scratch->length;
    Tier1EmitError error = TIER1_EMIT_OK;
    cache->dependency_offsets = tier1_emit_epoch_cache_allocate(output, (uint64_t)fragments + 1, 4, &error);
    uint32_t* stamps = tier1_emit_epoch_cache_allocate(scratch, (uint64_t)fragments + 1, 4, &error);
    uint32_t* cursors = tier1_emit_epoch_cache_allocate(scratch, (uint64_t)fragments + 1, 4, &error);
    error = error != TIER1_EMIT_OK || tier1_emit_epoch_cache_charge((uint64_t)fragments + 1, limit, work) ? error : TIER1_EMIT_LIMIT;
    if (error == TIER1_EMIT_OK)
    {
        tier1_emit_epoch_cache_fill(cache->dependency_offsets, (uint64_t)fragments + 1, 0);
    }
    for (uint32_t pass = 0; error == TIER1_EMIT_OK && pass < 2; pass += 1)
    {
        error = tier1_emit_epoch_cache_charge((uint64_t)fragments + 1 + (pass == 1 ? fragments : 0), limit, work) ? error : TIER1_EMIT_LIMIT;
        if (error == TIER1_EMIT_OK)
        {
            tier1_emit_epoch_cache_fill(stamps, (uint64_t)fragments + 1, 0);
        }
        if (error == TIER1_EMIT_OK && pass == 1)
        {
            uint64_t total = 0;
            for (uint32_t f = 0; error == TIER1_EMIT_OK && f < fragments; f += 1)
            {
                total += cache->dependency_offsets[f + 1];
                error = total < UINT32_MAX ? error : TIER1_EMIT_LIMIT;
                cache->dependency_offsets[f + 1] = (uint32_t)total;
                cursors[f] = cache->dependency_offsets[f];
            }
            cache->dependency_blocks = tier1_emit_epoch_cache_allocate(output, total + 1, 4, &error);
        }
        for (uint32_t b = 0; error == TIER1_EMIT_OK && b < cache->blocks; b += 1)
        {
            const uint32_t first = b < cache->added ? 0 : cache->plan->function->block_offsets[b - cache->added];
            const uint32_t end = b < cache->added ? 0 : cache->plan->function->block_offsets[b - cache->added + 1];
            const uint32_t use_first = cache->base + 2 * first;
            const uint32_t use_end = cache->base + 2 * end;
            const uint64_t charge = 1 + (uint64_t)cache->entry_offsets[b + 1] - cache->entry_offsets[b] +
                                    cache->preload_offsets[b + 1] - cache->preload_offsets[b] + end - first +
                                    2 * (uint64_t)(cache->transition_offsets[use_end] - cache->transition_offsets[use_first]);
            error = tier1_emit_epoch_cache_charge(charge, limit, work) ? error : TIER1_EMIT_LIMIT;
            for (uint32_t x = cache->entry_offsets[b]; error == TIER1_EMIT_OK && x < cache->entry_offsets[b + 1]; x += 1)
            {
                error = tier1_emit_epoch_cache_dependency(cache, cache->entry_fragments[x], b, pass, stamps, cursors);
            }
            for (uint32_t x = cache->preload_offsets[b]; error == TIER1_EMIT_OK && x < cache->preload_offsets[b + 1]; x += 1)
            {
                error = tier1_emit_epoch_cache_dependency(cache, cache->preload_fragments[x], b, pass, stamps, cursors);
            }
            for (uint32_t node = first; error == TIER1_EMIT_OK && node < end; node += 1)
            {
                error = tier1_emit_epoch_cache_dependency(cache, cache->definitions[node], b, pass, stamps, cursors);
            }
            for (uint32_t e = cache->transition_offsets[use_first]; error == TIER1_EMIT_OK && e < cache->transition_offsets[use_end]; e += 1)
            {
                const uint32_t f = cache->transition_fragments[e];
                error = tier1_emit_epoch_cache_dependency(cache, f - 1, b, pass, stamps, cursors);
                error = error != TIER1_EMIT_OK ? error : tier1_emit_epoch_cache_dependency(cache, f, b, pass, stamps, cursors);
            }
        }
    }
    arena_pop_to(scratch, mark);
    return error;
}
