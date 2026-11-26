// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Scoring-only regional proposals. The full qualified transformer materializes the chosen proposal exactly.
// Include after tier1_emit_region_admit.c. No proposal view may be passed to emission or a certificate.

typedef struct Tier1EmitRegionEdges Tier1EmitRegionEdges;
struct Tier1EmitRegionEdges
{
    Tier1CfgInput input;
    Tier1CfgAllocation frozen;
    uint32_t* entry_offsets;
    uint32_t* entry_intervals;
    uint32_t* semantic_counts;
    uint32_t* edge_blocks;
    uint32_t* incoming_edges;
    uint32_t* ordinals;
    uint32_t* spill_ends;
    uint64_t entries;
    uint64_t maximum_moves;
    uint64_t retained_bytes;
    uint32_t widest;
};

typedef struct Tier1EmitRegionInterval Tier1EmitRegionInterval;
struct Tier1EmitRegionInterval
{
    const Tier1EmitRegionEdges* cache;
    Tier1EmitRegionAdmission admission;
    Tier1CfgAllocation view;
    Tier1CfgEdge edge;
    uint32_t* edges;
    uint32_t* offsets;
    uint32_t* triples;
    uint8_t* placements;
    uint32_t* before_memory;
    uint32_t* before_recipes;
    uint32_t* after_memory;
    uint32_t* after_recipes;
    uint32_t* sources;
    uint32_t* destinations;
    uint32_t edge_count;
    uint32_t widest;
    uint32_t old_moves;
    uint32_t old_memory;
    uint32_t old_split;
    uint32_t all_memory;
    uint64_t capacity;
    uint64_t retained_bytes;
    uint8_t active_target;
};

typedef struct Tier1EmitRegionProposal Tier1EmitRegionProposal;
struct Tier1EmitRegionProposal
{
    // Only this register column is candidate state. All other CFG arrays still describe the current allocation.
    Tier1CfgAllocation cfg;
    const uint32_t* edges;
    const uint32_t* offsets;
    const uint32_t* triples;
    const uint8_t* placements;
    const uint32_t* before_memory;
    const uint32_t* before_recipes;
    const uint32_t* after_memory;
    const uint32_t* after_recipes;
    Tier1EmitRegionExchangeStats stats;
    uint32_t edge_count;
    uint32_t move_count;
    uint64_t visited;
    uint8_t temp_slots;
    uint8_t target;
};

// Descriptor identity proves the cached CSR/slot facts are still the same facts. Only columns owned by the
// qualified transformer are normalized; arbitrary new ranges, uses, definitions or graph views refuse reuse.
static bool tier1_emit_region_same_shape(const Tier1EmitRegionEdges* cache, const Tier1CfgInput* input, const Tier1CfgAllocation* cfg)
{
    Tier1CfgInput a = *input;
    Tier1CfgAllocation b = *cfg;
    a.allowed_registers = cache->input.allowed_registers;
    b.intervals.allowed_registers = cache->frozen.intervals.allowed_registers;
    b.allocation.registers = cache->frozen.allocation.registers;
    b.allocation.value_registers = cache->frozen.allocation.value_registers;
    b.edge_placements = cache->frozen.edge_placements;
    b.edge_move_offsets = cache->frozen.edge_move_offsets;
    b.split_move_offsets = cache->frozen.split_move_offsets;
    b.move_sources = cache->frozen.move_sources;
    b.move_destinations = cache->frozen.move_destinations;
    b.move_values = cache->frozen.move_values;
    b.move_count = cache->frozen.move_count;
    b.temp_slots = cache->frozen.temp_slots;
    b.split_edges = cache->frozen.split_edges;
    b.retained_bytes = cache->frozen.retained_bytes;
    return memcmp(&a, &cache->input, sizeof(a)) == 0 && memcmp(&b, &cache->frozen, sizeof(b)) == 0;
}

// Exact retained-size bound after initialization; callers may append cache/workspace to an already committed
// arena. No view points above its successful retained mark. Resolver scratch may use that same arena afterward.
static uint64_t tier1_emit_region_interval_bytes(const Tier1EmitRegionEdges* cache)
{
    const Tier1CfgInput* input = &cache->input;
    const uint64_t fragments = cache->frozen.allocation.fragment_count;
    const uint64_t edges = input->edge_count;
    const uint64_t locations = BUSTER_TIER1_CFG_SPILL + cache->frozen.allocation.spill_count + BUSTER_TIER1_ALLOCATE_CLASSES;
    return 13 * (fragments + 1) + 2 * ((uint64_t)input->value_count + 1) + 26 * (edges + 1) +
           12 * (cache->maximum_moves + 2) + 56 * ((uint64_t)cache->widest + 1) + 8 * locations + 32 * 63;
}

static Tier1EmitError tier1_emit_region_edges_init(const Tier1CfgInput* input, const Tier1CfgAllocation* cfg,
                                                   Arena* output, uint64_t* work, Tier1EmitRegionEdges* result)
{
    const uint64_t mark = output != 0 ? output->length : 0;
    const bool valid = input != 0 && cfg != 0 && output != 0 && work != 0 && result != 0 && cfg->split_count == 0 && input->block_count != 0;
    Tier1EmitError error = valid ? TIER1_EMIT_OK : TIER1_EMIT_INVALID;
    Tier1EmitRegionEdges built = { 0 };
    const uint32_t blocks = valid ? input->block_count : 0;
    const uint32_t edges = valid ? input->edge_count : 0;
    built.entry_offsets = valid ? tier1_emit_region_storage(output, (uint64_t)blocks + 1, 4, work, &error) : 0;
    uint32_t* cursors = error == TIER1_EMIT_OK ? tier1_emit_region_storage(output, (uint64_t)blocks + 1, 4, work, &error) : 0;
    built.semantic_counts = error == TIER1_EMIT_OK ? tier1_emit_region_storage(output, (uint64_t)blocks + 1, 4, work, &error) : 0;
    built.edge_blocks = error == TIER1_EMIT_OK ? tier1_emit_region_storage(output, (uint64_t)edges + 1, 4, work, &error) : 0;
    built.incoming_edges = error == TIER1_EMIT_OK ? tier1_emit_region_storage(output, (uint64_t)edges + 1, 4, work, &error) : 0;
    for (uint32_t pass = 0; error == TIER1_EMIT_OK && pass < 2; pass += 1)
    {
        if (pass == 1 && tier1_emit_region_visit(blocks, work, &error))
        {
            for (uint32_t b = 0; b < blocks; b += 1)
            {
                built.entry_offsets[b + 1] += built.entry_offsets[b];
                cursors[b] = built.entry_offsets[b];
            }
            built.entries = built.entry_offsets[blocks];
            built.entry_intervals = tier1_emit_region_storage(output, built.entries + 1, 4, work, &error);
        }
        for (uint32_t i = 0; error == TIER1_EMIT_OK && i < cfg->intervals.count; i += 1)
        {
            if (tier1_emit_region_visit(1, work, &error))
            {
                const uint32_t begin = cfg->intervals.starts[i];
                uint32_t first = tier1_cfg_block_at(input, cfg, begin);
                first += input->block_starts[first] < begin;
                const uint32_t past = tier1_cfg_block_at(input, cfg, cfg->intervals.ends[i] - 1) + 1;
                if (tier1_emit_region_visit(past - first, work, &error))
                {
                    for (uint32_t b = first; b < past; b += 1)
                    {
                        if (pass == 0)
                        {
                            error = built.entry_offsets[b + 1] != UINT32_MAX ? error : TIER1_EMIT_LIMIT;
                            built.entry_offsets[b + 1] += error == TIER1_EMIT_OK;
                        }
                        else
                        {
                            built.entry_intervals[cursors[b]++] = i;
                        }
                    }
                }
            }
        }
    }
    for (uint32_t b = 0; error == TIER1_EMIT_OK && b < blocks; b += 1)
    {
        const uint32_t count = built.entry_offsets[b + 1] - built.entry_offsets[b];
        if (tier1_emit_region_visit((uint64_t)count + 1, work, &error))
        {
            uint32_t semantic = input->phi_offsets[b + 1] - input->phi_offsets[b];
            for (uint32_t at = built.entry_offsets[b]; at < built.entry_offsets[b + 1]; at += 1)
            {
                const uint32_t value = cfg->intervals.values[built.entry_intervals[at]];
                semantic += input->definitions[value] != input->block_starts[b];
            }
            built.semantic_counts[b] = semantic;
            built.widest = semantic > built.widest ? semantic : built.widest;
        }
    }
    if (error == TIER1_EMIT_OK && tier1_emit_region_visit(5 * (uint64_t)edges + 3 * (uint64_t)blocks, work, &error))
    {
        error = tier1_cfg_edge_ordinals(input, output, &built.ordinals) ? error : TIER1_EMIT_MEMORY;
    }
    for (uint32_t b = 0; error == TIER1_EMIT_OK && b < blocks; b += 1)
    {
        const uint32_t count = input->successor_offsets[b + 1] - input->successor_offsets[b];
        if (tier1_emit_region_visit((uint64_t)count + 1, work, &error))
        {
            for (uint32_t e = input->successor_offsets[b]; e < input->successor_offsets[b + 1]; e += 1)
            {
                const uint32_t target = input->successors[e];
                built.edge_blocks[e] = b;
                built.incoming_edges[input->predecessor_offsets[target] + built.ordinals[e]] = e;
                const uint64_t semantic_bound = 4 * (uint64_t)built.semantic_counts[target];
                const uint32_t old_moves = cfg->edge_move_offsets[e + 1] - cfg->edge_move_offsets[e];
                built.maximum_moves += semantic_bound > old_moves ? semantic_bound : old_moves;
            }
        }
    }
    error = error == TIER1_EMIT_OK && built.maximum_moves > UINT32_MAX - 2 ? TIER1_EMIT_LIMIT : error;
    if (error == TIER1_EMIT_OK && cfg->allocation.spill_count != 0)
    {
        built.spill_ends = tier1_emit_region_storage(output, (uint64_t)cfg->allocation.fragment_count + 1, 4, work, &error);
        if (tier1_emit_region_visit((uint64_t)input->value_count + cfg->allocation.fragment_count, work, &error))
        {
            tier1_cfg_spill_prefix(cfg, built.spill_ends);
        }
    }
    if (error == TIER1_EMIT_OK)
    {
        built.input = *input;
        built.frozen = *cfg;
        built.retained_bytes = output->length - mark;
        *result = built;
    }
    else if (output != 0)
    {
        arena_pop_to(output, mark);
    }
    return error;
}

static void tier1_emit_region_move_counts(const uint32_t* triples, uint32_t first, uint32_t past, uint32_t* memory, uint32_t* recipes)
{
    uint32_t stores = 0;
    uint32_t recomputed = 0;
    for (uint32_t m = first; m < past; m += 1)
    {
        const uint32_t from = triples[(uint64_t)m * 3];
        const uint32_t to = triples[(uint64_t)m * 3 + 1];
        recomputed += from == BUSTER_TIER1_CFG_REMATERIALIZE;
        stores += (from >= BUSTER_TIER1_CFG_SPILL && from != BUSTER_TIER1_CFG_REMATERIALIZE) || to >= BUSTER_TIER1_CFG_SPILL;
    }
    *memory = stores;
    *recipes = recomputed;
}

static Tier1EmitError tier1_emit_region_interval_begin(const Tier1EmitRegionEdges* cache, const Tier1CfgInput* input,
                                                        const Tier1CfgAllocation* current, uint32_t interval, uint32_t callee_used,
                                                        Arena* output, uint64_t* work, Tier1EmitRegionInterval* result)
{
    const uint64_t mark = output != 0 ? output->length : 0;
    const bool valid = cache != 0 && input != 0 && current != 0 && output != 0 && work != 0 && result != 0 &&
                       tier1_emit_region_same_shape(cache, input, current);
    Tier1EmitError error = valid ? TIER1_EMIT_OK : TIER1_EMIT_INVALID;
    Tier1EmitRegionInterval built = { .cache = cache, .active_target = UINT8_MAX };
    if (valid)
    {
        error = tier1_emit_region_admit(input, current, cache->entry_offsets, cache->entry_intervals, interval, callee_used,
                                        output, work, &built.admission);
        built.view = *current;
    }
    const bool any = error == TIER1_EMIT_OK && built.admission.targets != 0;
    const uint32_t edges = any ? input->edge_count : 0;
    const uint32_t fragments = any ? current->allocation.fragment_count : 0;
    uint64_t* affected = any ? tier1_emit_region_storage(output, ((uint64_t)edges + 63) / 64 + 1, 8, work, &error) : 0;
    built.edges = any && error == TIER1_EMIT_OK ? tier1_emit_region_storage(output, (uint64_t)edges + 1, 4, work, &error) : 0;
    if (any && error == TIER1_EMIT_OK)
    {
        built.view.allocation.registers = tier1_emit_region_storage(output, (uint64_t)fragments + 1, 1, work, &error);
        if (tier1_emit_region_visit((uint64_t)fragments + 1, work, &error))
        {
            memcpy(built.view.allocation.registers, current->allocation.registers, fragments);
            built.view.allocation.registers[fragments] = UINT8_MAX;
        }
        const uint32_t start = built.admission.start;
        const uint32_t end = built.admission.end;
        const uint32_t first = tier1_cfg_block_at(input, current, start);
        const uint32_t past = tier1_cfg_block_at(input, current, end - 1) + 1;
        for (uint32_t b = first; error == TIER1_EMIT_OK && b < past; b += 1)
        {
            const uint32_t exit = input->block_starts[b + 1] - 1;
            const uint32_t entry = input->block_starts[b];
            const bool source = start <= exit && exit < end;
            const bool target = start <= entry && entry < end;
            const uint32_t successors = source ? input->successor_offsets[b + 1] - input->successor_offsets[b] : 0;
            const uint32_t predecessors = target ? input->predecessor_offsets[b + 1] - input->predecessor_offsets[b] : 0;
            if (tier1_emit_region_visit((uint64_t)successors + predecessors + 1, work, &error))
            {
                for (uint32_t k = 0; k < successors; k += 1)
                {
                    const uint32_t e = input->successor_offsets[b] + k;
                    affected[e / 64] |= UINT64_C(1) << (e % 64);
                }
                for (uint32_t k = 0; k < predecessors; k += 1)
                {
                    const uint32_t e = cache->incoming_edges[input->predecessor_offsets[b] + k];
                    affected[e / 64] |= UINT64_C(1) << (e % 64);
                }
            }
        }
        for (uint32_t word = 0; error == TIER1_EMIT_OK && word < ((uint64_t)edges + 63) / 64; word += 1)
        {
            if (tier1_emit_region_visit(1 + (uint64_t)__builtin_popcountll(affected[word]), work, &error))
            {
                for (uint64_t remaining = affected[word]; remaining != 0; remaining &= remaining - 1)
                {
                    const uint32_t e = word * 64 + (uint32_t)__builtin_ctzll(remaining);
                    const uint32_t semantic = cache->semantic_counts[input->successors[e]];
                    built.edges[built.edge_count++] = e;
                    built.capacity += 4 * (uint64_t)semantic;
                    built.widest = semantic > built.widest ? semantic : built.widest;
                }
            }
        }
    }
    error = error == TIER1_EMIT_OK && built.capacity > UINT32_MAX - 2 ? TIER1_EMIT_LIMIT : error;
    if (any && error == TIER1_EMIT_OK)
    {
        const uint64_t n = (uint64_t)built.edge_count + 1;
        built.offsets = tier1_emit_region_storage(output, n, 4, work, &error);
        built.placements = tier1_emit_region_storage(output, n, 1, work, &error);
        built.before_memory = tier1_emit_region_storage(output, n, 4, work, &error);
        built.before_recipes = tier1_emit_region_storage(output, n, 4, work, &error);
        built.after_memory = tier1_emit_region_storage(output, n, 4, work, &error);
        built.after_recipes = tier1_emit_region_storage(output, n, 4, work, &error);
        built.triples = tier1_emit_region_storage(output, (built.capacity + 2) * 3, 4, work, &error);
        const uint64_t widest = (uint64_t)built.widest + 1;
        const uint64_t locations = BUSTER_TIER1_CFG_SPILL + current->allocation.spill_count + BUSTER_TIER1_ALLOCATE_CLASSES;
        built.sources = tier1_emit_region_storage(output, widest, 4, work, &error);
        built.destinations = tier1_emit_region_storage(output, widest, 4, work, &error);
        built.edge.recompute = tier1_emit_region_storage(output, widest * 3, 4, work, &error);
        built.edge.carried_at = tier1_emit_region_storage(output, locations, 4, work, &error);
        built.edge.carried_stamps = tier1_emit_region_storage(output, locations, 4, work, &error);
        for (uint32_t c = 0; c < BUSTER_TIER1_ALLOCATE_CLASSES; c += 1)
        {
            built.edge.pending[c] = tier1_emit_region_storage(output, widest * 3, 4, work, &error);
        }
        built.edge.spill_ends = cache->spill_ends;
        if (tier1_emit_region_visit(current->move_count, work, &error))
        {
            built.all_memory = tier1_emit_recolor_memory_moves(current);
        }
        for (uint32_t k = 0; error == TIER1_EMIT_OK && k < built.edge_count; k += 1)
        {
            const uint32_t e = built.edges[k];
            const uint32_t first = current->edge_move_offsets[e];
            const uint32_t past = current->edge_move_offsets[e + 1];
            if (tier1_emit_region_visit((uint64_t)past - first + 1, work, &error))
            {
                for (uint32_t m = first; m < past; m += 1)
                {
                    const uint32_t from = current->move_sources[m];
                    const uint32_t to = current->move_destinations[m];
                    built.before_memory[k] += (from >= BUSTER_TIER1_CFG_SPILL && from != BUSTER_TIER1_CFG_REMATERIALIZE) || to >= BUSTER_TIER1_CFG_SPILL;
                    built.before_recipes[k] += from == BUSTER_TIER1_CFG_REMATERIALIZE;
                }
                built.old_moves += past - first;
                built.old_memory += built.before_memory[k];
                built.old_split += current->edge_placements[e] == TIER1_CFG_EDGE_SPLIT;
            }
        }
    }
    if (error == TIER1_EMIT_OK)
    {
        built.retained_bytes = output->length - mark;
        *result = built;
    }
    else if (output != 0)
    {
        arena_pop_to(output, mark);
    }
    return error;
}

// A successful or failed attempt invalidates prior views of this workspace. The current allocation never changes.
// Restoring/applying only the sparse patch lists makes target iteration independent of total fragment/edge count.
static Tier1EmitError tier1_emit_region_propose(Tier1EmitRegionInterval* interval, uint8_t target, Arena* scratch,
                                                uint64_t* work, Tier1EmitRegionProposal* result)
{
    const uint64_t mark = scratch != 0 ? scratch->length : 0;
    const uint64_t visited = work != 0 ? *work : 0;
    const bool valid = interval != 0 && scratch != 0 && work != 0 && result != 0;
    Tier1EmitError error = valid ? TIER1_EMIT_OK : TIER1_EMIT_INVALID;
    Tier1EmitRegionProposal built = { .target = target };
    if (valid)
    {
        error = tier1_emit_region_admitted(&interval->admission, target, &built.stats);
    }
    if (error == TIER1_EMIT_OK && interval->active_target != target)
    {
        const Tier1EmitRegionAdmission* admission = &interval->admission;
        if (interval->active_target < 16)
        {
            const uint8_t before = interval->active_target;
            const uint32_t first = admission->offsets[before];
            const uint32_t past = admission->offsets[before + 1];
            if (tier1_emit_region_visit((uint64_t)past - first + 1, work, &error))
            {
                for (uint32_t f = first; f < past; f += 1)
                {
                    interval->view.allocation.registers[admission->fragments[f]] = before;
                }
                interval->view.allocation.registers[admission->first] = admission->source;
                interval->active_target = UINT8_MAX;
            }
        }
        const uint32_t first = admission->offsets[target];
        const uint32_t past = admission->offsets[target + 1];
        if (tier1_emit_region_visit((uint64_t)past - first + 1, work, &error))
        {
            for (uint32_t f = first; f < past; f += 1)
            {
                interval->view.allocation.registers[admission->fragments[f]] = admission->source;
            }
            interval->view.allocation.registers[admission->first] = target;
            interval->active_target = target;
        }
    }
    const Tier1CfgInput* input = valid ? &interval->admission.input : 0;
    const Tier1CfgAllocation* current = valid ? &interval->admission.original : 0;
    Tier1CfgEdge* edge = valid ? &interval->edge : 0;
    if (error == TIER1_EMIT_OK)
    {
        const uint64_t locations = BUSTER_TIER1_CFG_SPILL + current->allocation.spill_count + BUSTER_TIER1_ALLOCATE_CLASSES;
        if (tier1_emit_region_visit(locations, work, &error))
        {
            memset(edge->carried_stamps, 0, locations * 4);
            edge->stamp = 0;
            const uint64_t resolver_bytes = 48 * (uint64_t)interval->widest + 128;
            const bool room = interval->widest == 0 || arena_push(scratch, resolver_bytes, 64) != 0;
            arena_pop_to(scratch, mark);
            error = room ? error : TIER1_EMIT_MEMORY;
        }
    }
    uint32_t count = 0;
    uint32_t memory = 0;
    uint32_t split = 0;
    built.temp_slots = error == TIER1_EMIT_OK ? current->temp_slots : 0;
    for (uint32_t k = 0; error == TIER1_EMIT_OK && k < interval->edge_count; k += 1)
    {
        const uint32_t e = interval->edges[k];
        const uint32_t b = interval->cache->edge_blocks[e];
        const uint32_t successor = input->successors[e];
        const uint32_t exit = input->block_starts[b + 1] - 1;
        const uint32_t entry = input->block_starts[successor];
        // Includes each complete semantic assignment and the existing resolver's bounded staged records. Fixed
        // small-class bookkeeping is explicit; this counts structural visits, not the resolver's CPU instructions.
        if (tier1_emit_region_visit(8 + 20 * (uint64_t)interval->cache->semantic_counts[successor], work, &error))
        {
            interval->offsets[k] = count;
            edge->block_start = input->block_starts[b];
            edge->current = current->current_slot_offsets != 0 ? current->current_slots : 0;
            edge->current_begin = current->current_slot_offsets != 0 ? current->current_slot_offsets[b] : 0;
            edge->current_end = current->current_slot_offsets != 0 ? current->current_slot_offsets[b + 1] : 0;
            edge->stamp += 1;
            edge->recompute_count = 0;
            for (uint32_t c = 0; c < BUSTER_TIER1_ALLOCATE_CLASSES; c += 1)
            {
                edge->pending_count[c] = 0;
                edge->memory_pair[c] = false;
            }
            bool ok = true;
            for (uint32_t at = interval->cache->entry_offsets[successor]; ok && at < interval->cache->entry_offsets[successor + 1]; at += 1)
            {
                const uint32_t i = interval->cache->entry_intervals[at];
                const uint32_t value = current->intervals.values[i];
                ok = input->definitions[value] == entry || tier1_cfg_edge_value(input, &interval->view, edge, value, value, exit, entry);
            }
            const uint32_t ordinal = interval->cache->ordinals[e];
            for (uint32_t p = input->phi_offsets[successor]; ok && p < input->phi_offsets[successor + 1]; p += 1)
            {
                const uint32_t operand = input->phi_operands[input->phi_operand_offsets[p] + ordinal];
                ok = operand == BUSTER_TIER1_CFG_NONE || tier1_cfg_edge_value(input, &interval->view, edge, input->phi_values[p], operand, exit, entry);
            }
            ok = ok && tier1_cfg_edge_stage(input, current->allocation.spill_count, edge, scratch, interval->sources, interval->destinations,
                                            interval->triples, interval->capacity + 2, &count, &built.temp_slots);
            error = ok ? error : TIER1_EMIT_ALLOCATION;
            if (ok)
            {
                const bool moves = count != interval->offsets[k] || (input->edge_copies != 0 && input->edge_copies[e] != 0);
                const bool one_successor = input->successor_offsets[b + 1] - input->successor_offsets[b] == 1;
                const bool one_predecessor = input->predecessor_offsets[successor + 1] - input->predecessor_offsets[successor] == 1;
                const uint8_t place = !moves ? TIER1_CFG_EDGE_EMPTY : one_successor ? TIER1_CFG_EDGE_PREDECESSOR : one_predecessor ? TIER1_CFG_EDGE_SUCCESSOR : TIER1_CFG_EDGE_SPLIT;
                interval->placements[k] = place;
                tier1_emit_region_move_counts(interval->triples, interval->offsets[k], count, &interval->after_memory[k], &interval->after_recipes[k]);
                memory += interval->after_memory[k];
                split += place == TIER1_CFG_EDGE_SPLIT;
                built.stats.edges.added_split_edges += place == TIER1_CFG_EDGE_SPLIT && current->edge_placements[e] != TIER1_CFG_EDGE_SPLIT;
            }
        }
    }
    if (error == TIER1_EMIT_OK)
    {
        interval->offsets[interval->edge_count] = count;
        built.cfg = interval->view;
        built.edges = interval->edges;
        built.offsets = interval->offsets;
        built.triples = interval->triples;
        built.placements = interval->placements;
        built.before_memory = interval->before_memory;
        built.before_recipes = interval->before_recipes;
        built.after_memory = interval->after_memory;
        built.after_recipes = interval->after_recipes;
        built.edge_count = interval->edge_count;
        built.move_count = count;
        built.stats.edges.affected_edges = interval->edge_count;
        built.stats.edges.before_moves = current->move_count;
        built.stats.edges.after_moves = current->move_count - interval->old_moves + count;
        built.stats.edges.before_memory_moves = interval->all_memory;
        built.stats.edges.after_memory_moves = interval->all_memory - interval->old_memory + memory;
        built.stats.edges.before_split_edges = current->split_edges;
        built.stats.edges.after_split_edges = current->split_edges - interval->old_split + split;
        built.stats.edges.added_temp_slots = built.temp_slots & ~current->temp_slots;
        built.visited = *work - visited;
        *result = built;
    }
    if (scratch != 0)
    {
        arena_pop_to(scratch, mark);
    }
    return error;
}

static Tier1EmitError tier1_emit_region_proposal_matches(const Tier1EmitRegionInterval* interval, const Tier1EmitRegionProposal* proposal,
                                                         const Tier1CfgAllocation* materialized, uint64_t* work, bool* matches)
{
    const bool valid = interval != 0 && proposal != 0 && materialized != 0 && work != 0 && matches != 0 &&
                       proposal->target == interval->active_target && proposal->cfg.allocation.registers == interval->view.allocation.registers;
    Tier1EmitError error = valid ? TIER1_EMIT_OK : TIER1_EMIT_INVALID;
    bool same = valid && materialized->allocation.fragment_count == proposal->cfg.allocation.fragment_count &&
                materialized->move_count == proposal->stats.edges.after_moves && materialized->split_edges == proposal->stats.edges.after_split_edges &&
                materialized->temp_slots == proposal->temp_slots;
    for (uint32_t f = 0; error == TIER1_EMIT_OK && same && f < materialized->allocation.fragment_count; f += 64)
    {
        const uint32_t left = materialized->allocation.fragment_count - f;
        const uint32_t count = left < 64 ? left : 64;
        if (tier1_emit_region_visit(count, work, &error))
        {
            const __mmask64 lanes = count == 64 ? UINT64_MAX : (UINT64_C(1) << count) - 1;
            const __m512i a = _mm512_maskz_loadu_epi8(lanes, materialized->allocation.registers + f);
            const __m512i b = _mm512_maskz_loadu_epi8(lanes, proposal->cfg.allocation.registers + f);
            same = (lanes & _mm512_cmpneq_epi8_mask(a, b)) == 0;
        }
    }
    for (uint32_t k = 0; error == TIER1_EMIT_OK && same && k < proposal->edge_count; k += 1)
    {
        const uint32_t e = proposal->edges[k];
        const uint32_t first = materialized->edge_move_offsets[e];
        const uint32_t count = materialized->edge_move_offsets[e + 1] - first;
        if (tier1_emit_region_visit(1 + 3 * (uint64_t)count, work, &error))
        {
            same = proposal->placements[k] == materialized->edge_placements[e] && count == proposal->offsets[k + 1] - proposal->offsets[k];
            for (uint32_t m = 0; same && m < count; m += 1)
            {
                const uint32_t* triple = proposal->triples + (uint64_t)(proposal->offsets[k] + m) * 3;
                same = materialized->move_sources[first + m] == triple[0] && materialized->move_destinations[first + m] == triple[1] &&
                       materialized->move_values[first + m] == triple[2];
            }
        }
    }
    if (error == TIER1_EMIT_OK)
    {
        *matches = same;
    }
    return error;
}

// Admission is shared with proposals, then the unchanged qualified transformer independently checks every
// constraint and builds owned masks/full edges. Nothing publishes until the sparse proposal agrees exactly.
static Tier1EmitError tier1_emit_region_materialize(const Tier1EmitRegionInterval* interval, const Tier1EmitRegionProposal* proposal,
                                                    Arena* output, Arena* scratch, uint64_t* work,
                                                    Tier1CfgInput* result_input, Tier1CfgAllocation* result)
{
    const uint64_t mark = output != 0 ? output->length : 0;
    const bool valid = interval != 0 && proposal != 0 && output != 0 && scratch != 0 && output != scratch && work != 0 && result_input != 0 && result != 0 &&
                       proposal->target == interval->active_target && proposal->cfg.allocation.registers == interval->view.allocation.registers;
    Tier1EmitError error = valid ? tier1_emit_region_admitted(&interval->admission, proposal->target, 0) : TIER1_EMIT_INVALID;
    Tier1CfgInput built_input = { 0 };
    Tier1CfgAllocation built = { 0 };
    if (error == TIER1_EMIT_OK)
    {
        const Tier1CfgInput* input = &interval->admission.input;
        const Tier1CfgAllocation* cfg = &interval->admission.original;
        uint64_t states = 0;
        if (tier1_emit_region_visit(interval->edge_count, work, &error))
        {
            for (uint32_t k = 0; k < interval->edge_count; k += 1)
            {
                const uint32_t target = input->successors[interval->edges[k]];
                const uint32_t all = interval->cache->entry_offsets[target + 1] - interval->cache->entry_offsets[target];
                const uint32_t semantic = interval->cache->semantic_counts[target];
                states += all > semantic ? all : semantic;
            }
        }
        // The contained helper also zeros/copies both permission columns, marks target owners, renames their
        // fragments and recomputes uniform summaries. Reserve those known passes as well as the original scans.
        const uint64_t charge = (uint64_t)cfg->intervals.count * 8 + cfg->allocation.fragment_count * UINT64_C(5) + input->value_count * UINT64_C(6) +
                                input->block_count * UINT64_C(6) + input->edge_count * UINT64_C(5) + interval->cache->entries * 2 + states * 12 +
                                cfg->move_count * UINT64_C(3) + input->fixed.count + input->sequence_count;
        if (tier1_emit_region_visit(charge, work, &error))
        {
            error = tier1_emit_cfg_region_exchange(input, cfg, interval->admission.interval, proposal->target, interval->admission.callee_used,
                                                    output, scratch, &built_input, &built, 0);
        }
    }
    bool matches = false;
    if (error == TIER1_EMIT_OK)
    {
        error = tier1_emit_region_proposal_matches(interval, proposal, &built, work, &matches);
        error = error == TIER1_EMIT_OK && !matches ? TIER1_EMIT_ALLOCATION : error;
    }
    if (error == TIER1_EMIT_OK)
    {
        *result_input = built_input;
        *result = built;
    }
    else if (output != 0)
    {
        arena_pop_to(output, mark);
    }
    return error;
}
