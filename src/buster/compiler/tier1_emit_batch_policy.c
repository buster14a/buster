// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Measurement-only #2610 batch probe. Close a parameter's contiguous register run over complete displaced
// owners and newly critical transport edges, then price and certify the whole exchange. The existing allocation's
// lifetimes, fragment kinds, slots, hard roles, and definition bindings remain frozen. Intermediate closures never
// reach emission. A finite work allowance retains only the last complete, certified accepted allocation.

#define BUSTER_TIER1_BATCH_INTERVALS 128u
#define BUSTER_TIER1_BATCH_ROUNDS 32u

typedef struct Tier1EmitBatchRange Tier1EmitBatchRange;
struct Tier1EmitBatchRange
{
    uint32_t first;
    uint32_t past;
};

// Enlarge only through adjacent complete parameter intervals in the original caller register. A spill gap,
// a differently colored interval, or the incoming definition's binding is a hard boundary for this probe.
static bool tier1_emit_batch_extend(const Tier1CfgInput* input, const Tier1CfgAllocation* cfg, uint32_t value,
                                    uint8_t source, uint8_t target, uint32_t start, uint32_t end, Tier1EmitBatchRange* range)
{
    const uint32_t first = cfg->value_interval_offsets[value];
    const uint32_t past = cfg->value_interval_offsets[value + 1];
    while (range->first > first && range->past - range->first < BUSTER_TIER1_BATCH_INTERVALS && cfg->intervals.starts[range->first] > start)
    {
        range->first -= 1;
    }
    while (range->past < past && range->past - range->first < BUSTER_TIER1_BATCH_INTERVALS && cfg->intervals.ends[range->past - 1] < end)
    {
        range->past += 1;
    }
    bool valid = range->first >= first && range->first < range->past && range->past <= past &&
                 range->past - range->first <= BUSTER_TIER1_BATCH_INTERVALS &&
                 cfg->intervals.starts[range->first] <= start && cfg->intervals.ends[range->past - 1] >= end &&
                 cfg->intervals.starts[range->first] > input->definitions[value];
    for (uint32_t i = range->first; valid && i < range->past; i += 1)
    {
        const uint32_t f = cfg->allocation.fragment_offsets[i];
        const uint32_t boundary = cfg->intervals.starts[i];
        // Internal boundaries are CFG entries, never an instruction's dying-use/result alias point. The final
        // endpoint alone can therefore change ordinary expansion and retains the original endpoint guard.
        const bool entry = (boundary & 1) == 0 && input->block_starts[tier1_cfg_block_at(input, cfg, boundary)] == boundary;
        valid = cfg->allocation.fragment_offsets[i + 1] == f + 1 && cfg->allocation.kinds[f] == BUSTER_TIER1_ALLOCATE_REGISTER &&
                cfg->allocation.registers[f] == source && (cfg->intervals.allowed_registers[i] & (1u << target)) != 0 &&
                (i == range->first || (entry && cfg->intervals.ends[i - 1] == boundary));
    }
    return valid;
}

// The callee owner's complete interval, not just its overlapping fragment, must move into the freed caller.
// Including adjacent parameter intervals lets a definition after the seed call stay there until its last use.
static Tier1EmitError tier1_emit_batch_owners(const Tier1CfgInput* input, const Tier1CfgAllocation* cfg, uint32_t value,
                                             uint8_t source, uint8_t target, uint64_t* work, Tier1EmitBatchRange* range)
{
    Tier1EmitError error = TIER1_EMIT_OK;
    bool closed = false;
    for (uint32_t round = 0; error == TIER1_EMIT_OK && !closed && round < BUSTER_TIER1_BATCH_ROUNDS; round += 1)
    {
        const uint32_t start = cfg->intervals.starts[range->first];
        const uint32_t end = cfg->intervals.ends[range->past - 1];
        uint32_t low = start;
        uint32_t high = end;
        if (!tier1_emit_recolor_charge((uint64_t)cfg->intervals.count + cfg->allocation.fragment_count + BUSTER_TIER1_BATCH_INTERVALS, work))
        {
            error = TIER1_EMIT_LIMIT;
        }
        for (uint32_t i = 0; error == TIER1_EMIT_OK && i < cfg->intervals.count; i += 1)
        {
            const uint32_t begin = cfg->intervals.starts[i];
            const uint32_t past = cfg->intervals.ends[i];
            if (cfg->intervals.classes[i] == BUSTER_TIER1_ALLOCATE_GPR && begin < end && past > start)
            {
                bool touched = false;
                const uint32_t limit = cfg->allocation.fragment_offsets[i + 1];
                for (uint32_t f = cfg->allocation.fragment_offsets[i]; !touched && f < limit; f += 16)
                {
                    // Include a register just before entry too: entry locations precede an entry-phase spill.
                    const uint32_t left = limit - f;
                    const __mmask16 lanes = left >= 16 ? 0xffff : (__mmask16)((1u << left) - 1);
                    const __m128i kinds = _mm_maskz_loadu_epi8(lanes, cfg->allocation.kinds + f);
                    const __m128i registers = _mm_maskz_loadu_epi8(lanes, cfg->allocation.registers + f);
                    const __m512i starts = _mm512_maskz_loadu_epi32(lanes, cfg->allocation.starts + f);
                    const __m512i ends = _mm512_maskz_loadu_epi32(lanes, cfg->allocation.ends + f);
                    touched = (lanes & _mm_cmpeq_epi8_mask(kinds, _mm_set1_epi8(BUSTER_TIER1_ALLOCATE_REGISTER)) &
                               _mm_cmpeq_epi8_mask(registers, _mm_set1_epi8((char)target)) &
                               _mm512_cmplt_epu32_mask(starts, _mm512_set1_epi32((int)end)) &
                               _mm512_cmpge_epu32_mask(ends, _mm512_set1_epi32((int)start))) != 0;
                }
                low = touched && begin < low ? begin : low;
                high = touched && past > high ? past : high;
            }
        }
        if (error == TIER1_EMIT_OK && !tier1_emit_batch_extend(input, cfg, value, source, target, low, high, range))
        {
            error = TIER1_EMIT_INVALID;
        }
        closed = low == start && high == end;
    }
    return error == TIER1_EMIT_OK && !closed ? TIER1_EMIT_INVALID : error;
}

static void tier1_emit_batch_move_counts(const Tier1CfgAllocation* cfg, uint32_t edge, uint32_t* memory, uint32_t* recipes)
{
    *memory = 0;
    *recipes = 0;
    const uint32_t past = cfg->edge_move_offsets[edge + 1];
    for (uint32_t m = cfg->edge_move_offsets[edge]; m < past; m += 16)
    {
        const uint32_t left = past - m;
        const __mmask16 lanes = left >= 16 ? 0xffff : (__mmask16)((1u << left) - 1);
        const __m512i from = _mm512_maskz_loadu_epi32(lanes, cfg->move_sources + m);
        const __m512i to = _mm512_maskz_loadu_epi32(lanes, cfg->move_destinations + m);
        const __mmask16 remat = _mm512_cmpeq_epi32_mask(from, _mm512_set1_epi32(BUSTER_TIER1_CFG_REMATERIALIZE));
        const __mmask16 stored = _mm512_cmpge_epu32_mask(to, _mm512_set1_epi32(BUSTER_TIER1_CFG_SPILL));
        const __mmask16 loaded = _mm512_cmpge_epu32_mask(from, _mm512_set1_epi32(BUSTER_TIER1_CFG_SPILL)) & ~remat;
        *recipes += (uint32_t)__builtin_popcount(lanes & remat);
        *memory += (uint32_t)__builtin_popcount(lanes & (stored | loaded));
    }
}

typedef struct Tier1EmitBatchCandidate Tier1EmitBatchCandidate;
struct Tier1EmitBatchCandidate
{
    Tier1CfgInput input;
    Tier1CfgAllocation cfg;
    Tier1EmitEpochTrial epochs;
    Tier1EmitRegionScore score;
    Tier1EmitBatchRange range;
    uint32_t displaced;
};

// A complete materialization is the candidate itself. Its edge columns also adapt directly to the qualified
// sparse score interface. Equal recipe counts, memory traffic, split flags and indirect fusion are still required.
static Tier1EmitError tier1_emit_batch_candidate(const Tier1SelectPlan* plan, const Tier1EmitResult* emitted,
                                                 const Tier1CfgInput* input, const Tier1CfgAllocation* cfg,
                                                 const Tier1EmitRegionEdges* edges, const Tier1EmitRecolorModel* model,
                                                 const Tier1EmitRegionArguments* arguments, const Tier1EmitEpochCache* cache,
                                                 const Tier1EmitRecolorCost* cost, uint32_t seed, uint8_t target, uint32_t used,
                                                 Arena* materialized, Arena* epoch_output, Arena* epoch_scratch, Arena* score_output,
                                                 Arena* scratch, uint64_t* work, Tier1EmitBatchCandidate* result, uint32_t* reason)
{
    const uint32_t value = cfg->intervals.values[seed];
    const uint8_t source = cfg->allocation.registers[cfg->allocation.fragment_offsets[seed]];
    Tier1EmitBatchCandidate built = { .range = { seed, seed + 1 } };
    Tier1EmitError error = TIER1_EMIT_OK;
    bool closed = false;
    *reason = 1;
    for (uint32_t round = 0; error == TIER1_EMIT_OK && !closed && round < BUSTER_TIER1_BATCH_ROUNDS; round += 1)
    {
        error = tier1_emit_batch_owners(input, cfg, value, source, target, work, &built.range);
        const uint32_t start = cfg->intervals.starts[built.range.first];
        const uint32_t end = cfg->intervals.ends[built.range.past - 1];
        *reason = error == TIER1_EMIT_OK ? 2 : *reason;
        const uint32_t base = 2 * (input->block_count - plan->function->block_count);
        const uint32_t endpoint = end > base && ((end - base) & 1) != 0 ? (end - base - 1) / 2 : UINT32_MAX;
        const uint64_t endpoint_work = BUSTER_TIER1_SELECT_VALUES + 2 + (endpoint < plan->function->instruction_count ?
                                       (uint64_t)plan->effective_use_offsets[endpoint + 1] - plan->effective_use_offsets[endpoint] : 0);
        if (error == TIER1_EMIT_OK && !tier1_emit_recolor_charge(endpoint_work, work))
        {
            error = TIER1_EMIT_LIMIT;
        }
        if (error == TIER1_EMIT_OK && !tier1_emit_recolor_endpoint(plan, emitted, input, cfg, built.range.past - 1, source, target))
        {
            error = TIER1_EMIT_INVALID;
        }
        // Reserve the complete transform's scans and semantic edge/resolver work before rebuilding anything.
        uint64_t states = 0;
        if (error == TIER1_EMIT_OK && !tier1_emit_recolor_charge(input->edge_count, work))
        {
            error = TIER1_EMIT_LIMIT;
        }
        for (uint32_t e = 0; error == TIER1_EMIT_OK && e < input->edge_count; e += 1)
        {
            const uint32_t b = edges->edge_blocks[e];
            const uint32_t s = input->successors[e];
            const uint32_t exit = input->block_starts[b + 1] - 1;
            const uint32_t entry = input->block_starts[s];
            const uint32_t count = edges->entry_offsets[s + 1] - edges->entry_offsets[s];
            states += (start <= exit && exit < end) || (start <= entry && entry < end) ?
                      (count > edges->semantic_counts[s] ? count : edges->semantic_counts[s]) : 0;
        }
        const uint64_t charge = (uint64_t)cfg->intervals.count * 8 + cfg->allocation.fragment_count * UINT64_C(5) + input->value_count * UINT64_C(6) +
                                input->block_count * UINT64_C(6) + input->edge_count * UINT64_C(5) + edges->entries * 2 + states * 12 +
                                cfg->move_count * UINT64_C(3) + input->fixed.count + input->sequence_count;
        if (error == TIER1_EMIT_OK && !tier1_emit_recolor_charge(charge, work))
        {
            error = TIER1_EMIT_LIMIT;
        }
        if (error == TIER1_EMIT_OK)
        {
            Tier1EmitRegionExchangeStats stats = { 0 };
            arena_pop_to(materialized, 0);
            *reason = 3;
            error = tier1_emit_cfg_region_exchange_range(input, cfg, built.range.first, built.range.past, target, used,
                                                         materialized, scratch, &built.input, &built.cfg, &stats);
            built.displaced = stats.displaced_fragments;
        }
        if (error == TIER1_EMIT_OK && !tier1_emit_recolor_charge(input->edge_count + BUSTER_TIER1_BATCH_INTERVALS, work))
        {
            error = TIER1_EMIT_LIMIT;
        }
        uint32_t low = start;
        uint32_t high = end;
        *reason = error == TIER1_EMIT_OK ? 4 : *reason;
        for (uint32_t e = 0; error == TIER1_EMIT_OK && e < input->edge_count; e += 1)
        {
            if (built.cfg.edge_placements[e] == TIER1_CFG_EDGE_SPLIT && cfg->edge_placements[e] != TIER1_CFG_EDGE_SPLIT)
            {
                const uint32_t positions[2] = { input->block_starts[edges->edge_blocks[e] + 1] - 1, input->block_starts[input->successors[e]] };
                for (uint32_t k = 0; error == TIER1_EMIT_OK && k < 2; k += 1)
                {
                    const uint32_t i = tier1_cfg_interval_at(cfg, value, positions[k]);
                    error = i != BUSTER_TIER1_CFG_NONE ? error : TIER1_EMIT_INVALID;
                    if (error == TIER1_EMIT_OK)
                    {
                        low = cfg->intervals.starts[i] < low ? cfg->intervals.starts[i] : low;
                        high = cfg->intervals.ends[i] > high ? cfg->intervals.ends[i] : high;
                    }
                }
            }
        }
        closed = error == TIER1_EMIT_OK && low == start && high == end;
        if (error == TIER1_EMIT_OK && !closed && !tier1_emit_batch_extend(input, cfg, value, source, target, low, high, &built.range))
        {
            error = TIER1_EMIT_INVALID;
        }
    }
    error = error == TIER1_EMIT_OK && !closed ? TIER1_EMIT_INVALID : error;
    arena_pop_to(score_output, 0);
    arena_pop_to(epoch_output, 0);
    bool ok = true;
    const uint32_t count = input->edge_count;
    uint32_t* ids = error == TIER1_EMIT_OK ? ir_allocate(score_output, (uint64_t)count + 1, 4, &ok) : 0;
    uint32_t* before_memory = error == TIER1_EMIT_OK && ok ? ir_allocate(score_output, (uint64_t)count + 1, 4, &ok) : 0;
    uint32_t* before_recipes = error == TIER1_EMIT_OK && ok ? ir_allocate(score_output, (uint64_t)count + 1, 4, &ok) : 0;
    uint32_t* after_memory = error == TIER1_EMIT_OK && ok ? ir_allocate(score_output, (uint64_t)count + 1, 4, &ok) : 0;
    uint32_t* after_recipes = error == TIER1_EMIT_OK && ok ? ir_allocate(score_output, (uint64_t)count + 1, 4, &ok) : 0;
    error = error == TIER1_EMIT_OK && !ok ? TIER1_EMIT_MEMORY : error;
    if (error == TIER1_EMIT_OK && !tier1_emit_recolor_charge((uint64_t)count * 5 + cfg->move_count + built.cfg.move_count, work))
    {
        error = TIER1_EMIT_LIMIT;
    }
    *reason = error == TIER1_EMIT_OK ? 5 : *reason;
    for (uint32_t e = 0; error == TIER1_EMIT_OK && e < count; e += 1)
    {
        ids[e] = e;
        tier1_emit_batch_move_counts(cfg, e, before_memory + e, before_recipes + e);
        tier1_emit_batch_move_counts(&built.cfg, e, after_memory + e, after_recipes + e);
        error = after_memory[e] <= before_memory[e] && after_recipes[e] == before_recipes[e] &&
                tier1_emit_region_score_control(plan, input, e, cfg->edge_placements[e], built.cfg.edge_placements[e]) ? error : TIER1_EMIT_INVALID;
    }
    if (error == TIER1_EMIT_OK)
    {
        *reason = 6;
        error = tier1_emit_epoch_cache_trial(cache, &built.cfg, epoch_output, epoch_scratch, BUSTER_TIER1_RECOLOR_ACTIVE_WORK, work, &built.epochs);
    }
    if (error == TIER1_EMIT_OK)
    {
        const Tier1EmitRegionProposal proposal = {
            .cfg = built.cfg, .edges = ids, .offsets = built.cfg.edge_move_offsets, .placements = built.cfg.edge_placements,
            .before_memory = before_memory, .before_recipes = before_recipes, .after_memory = after_memory, .after_recipes = after_recipes,
            .edge_count = count, .move_count = built.cfg.move_count, .temp_slots = built.cfg.temp_slots, .target = target,
        };
        *reason = 7;
        error = tier1_emit_region_score_trial(plan, input, cfg, &proposal, model, arguments, cache, &built.epochs, cost,
                                              score_output, scratch, work, &built.score);
    }
    if (error == TIER1_EMIT_OK)
    {
        *result = built;
    }
    return error;
}

static Tier1EmitError tier1_emit_cfg_batch_regions(const Tier1SelectPlan* plan, const Tier1EmitResult* emitted,
                                                  Arena* output, Arena* scratch, Tier1CfgInput* input, Tier1CfgAllocation* cfg)
{
    const uint64_t mark = scratch->length;
    const IrFunction* function = plan->function;
#if defined(BUSTER_TIER1_MEASURE_KINDS)
    const uint64_t previous_limit = tier1_emit_recolor_work_limit;
    tier1_emit_recolor_work_limit = tier1_emit_recolor_measure_limit();
    const char* trace_env = getenv("BUSTER_TIER1_EXCHANGE_TRACE");
    const bool trace = trace_env != 0 && (uint32_t)strtoul(trace_env, 0, 10) == function->instruction_count;
#endif
    const Tier1CfgAllocation original = *cfg;
    const Tier1CfgInput original_input = *input;
    uint64_t work = 0;
    bool ok = function->block_count != 0 && cfg->split_count == 0 && input->definition_registers != 0 && output != scratch && emitted->prepared.call_count != 0;
    bool bounded = true;
    const uint32_t callee = emitted->callee_registers & input->registers[BUSTER_TIER1_ALLOCATE_GPR];
    const uint32_t used = ok ? tier1_emit_cfg_callee_used(cfg, callee) : 0;
    ok = ok && used != 0;
    Tier1EmitRecolorModel model = { 0 };
    Tier1EmitRegionEdges edges = { 0 };
    Tier1EmitRegionArguments arguments = { 0 };
    Tier1EmitEpochCache cache = { 0 };
    Arena retained = { 0 }, epoch_scratch = { 0 }, epoch_output = { 0 }, score_output = { 0 }, materialized[2] = { 0 };
    const uint64_t model_work = BUSTER_TIER1_FREQUENCY_WORK + (uint64_t)function->instruction_count * 2 +
                                plan->effective_use_offsets[function->instruction_count] + input->sequence_count + input->fixed.count +
                                (uint64_t)input->block_count * 4 + input->edge_count * UINT64_C(2) + emitted->prepared.call_count;
    bounded = !ok || tier1_emit_recolor_charge(model_work, &work);
    ok = !bounded ? ok : ok && tier1_emit_recolor_model(plan, emitted, &original_input, scratch, &model);
    Tier1EmitError setup = TIER1_EMIT_OK;
    if (ok && bounded)
    {
        setup = tier1_emit_region_edges_init(&original_input, &original, scratch, &work, &edges);
        bounded = setup != TIER1_EMIT_LIMIT;
        ok = setup == TIER1_EMIT_OK || !bounded;
    }
    if (ok && bounded)
    {
        Arena argument_output = { 0 };
        ok = tier1_emit_region_arena(scratch, tier1_emit_region_arguments_bytes(&original, &model), &argument_output);
        setup = ok ? tier1_emit_region_arguments_prepare(&original, &model, &argument_output, scratch, &work, &arguments) : TIER1_EMIT_MEMORY;
        bounded = setup != TIER1_EMIT_LIMIT;
        ok = setup == TIER1_EMIT_OK || !bounded;
    }
    if (ok && bounded)
    {
        ok = tier1_emit_region_arena(scratch, tier1_emit_epoch_cache_bytes(plan, &original_input, &original, edges.entries), &retained) &&
             tier1_emit_region_arena(scratch, tier1_emit_epoch_cache_scratch_bytes(plan, &original_input, &original), &epoch_scratch);
        setup = ok ? tier1_emit_epoch_cache_prepare(plan, emitted, &original_input, &original, &retained, &epoch_scratch,
                                                    BUSTER_TIER1_RECOLOR_ACTIVE_WORK, &work, &cache) : TIER1_EMIT_MEMORY;
        bounded = setup != TIER1_EMIT_LIMIT;
        ok = setup == TIER1_EMIT_OK || !bounded;
    }
    Tier1EmitRecolorCost current_cost = { 0 };
    if (ok && bounded)
    {
        setup = tier1_emit_region_score_initial(&original_input, &original, &model, &arguments, &cache, scratch, &work, &current_cost);
        bounded = setup != TIER1_EMIT_LIMIT;
        ok = setup == TIER1_EMIT_OK || !bounded;
    }
    const Tier1EmitRecolorCost initial_cost = current_cost;
    if (ok && bounded)
    {
        const uint64_t moves = edges.maximum_moves > original.move_count ? edges.maximum_moves : original.move_count;
        const uint64_t bytes = moves <= UINT32_MAX ? tier1_emit_cfg_region_exchange_bytes(input, cfg, (uint32_t)moves) : 0;
        const uint64_t publish_work = 2 * ((uint64_t)original.allocation.fragment_count + original_input.value_count * UINT64_C(2) +
                                           original.intervals.count + original_input.edge_count * UINT64_C(2) + moves * 3 + 16);
        bounded = tier1_emit_recolor_charge(publish_work, &work);
        ok = !bounded || (tier1_emit_region_arena(scratch, tier1_emit_epoch_trial_bytes(&cache), &epoch_output) &&
             tier1_emit_region_arena(scratch, tier1_emit_region_score_bytes(input) + 20 * ((uint64_t)input->edge_count + 1) + 5 * 63, &score_output) &&
             tier1_emit_region_arena(scratch, bytes, &materialized[0]) && tier1_emit_region_arena(scratch, bytes, &materialized[1]));
    }
    Tier1CfgAllocation current = original;
    Tier1CfgInput current_input = original_input;
    uint32_t held = 2;
    uint32_t selected = 0, trials = 0, weighted = 0, paths = 0, refused[8] = { 0 };
    for (uint32_t node = 0; ok && bounded && node < function->block_offsets[1]; node += 1)
    {
        if (!tier1_select_entry_parameter(function, node))
        {
            continue;
        }
        const uint32_t value = emitted->node_values[node];
        const Tier1SelectPlace place = tier1_select_parameter_place(function, function->a[node]);
        if (place.kind != BUSTER_TIER1_PLACE_GENERAL || value == BUSTER_TIER1_CFG_NONE || original_input.definition_registers[value] == 0)
        {
            continue;
        }
        const uint8_t source = tier1_select_parameter_registers[place.index];
        const uint32_t targets = used & original_input.allowed_registers[value] & ~model.forbidden;
        for (uint32_t i = original.value_interval_offsets[value]; ok && bounded && (model.forbidden & (1u << source)) == 0 && i < original.value_interval_offsets[value + 1]; i += 1)
        {
            const uint32_t f = current.allocation.fragment_offsets[i];
            const uint32_t start = current.intervals.starts[i];
            const uint32_t end = current.intervals.ends[i];
            bounded = tier1_emit_recolor_charge(1, &work);
            if (!bounded || start <= original_input.definitions[value] || current.allocation.fragment_offsets[i + 1] != f + 1 ||
                current.allocation.kinds[f] != BUSTER_TIER1_ALLOCATE_REGISTER || current.allocation.registers[f] != source ||
                !tier1_emit_recolor_crosses(&model, start, end))
            {
                continue;
            }
            uint32_t chosen = UINT32_MAX;
            Tier1EmitRecolorCost best_cost = current_cost;
            const uint32_t slot = held == 0 ? 1 : 0;
            for (uint32_t target = 0; ok && bounded && target < 16; target += 1)
            {
                if ((targets & (1u << target)) == 0)
                {
                    continue;
                }
                Tier1EmitBatchCandidate candidate = { 0 };
                uint32_t reason = 0;
                const Tier1EmitError changed = tier1_emit_batch_candidate(plan, emitted, &current_input, &current, &edges, &model, &arguments,
                    &cache, &current_cost, i, (uint8_t)target, used, &materialized[slot], &epoch_output, &epoch_scratch, &score_output,
                    scratch, &work, &candidate, &reason);
                trials += 1;
                bounded = changed != TIER1_EMIT_LIMIT;
                ok = changed != TIER1_EMIT_MEMORY && changed != TIER1_EMIT_ALLOCATION;
                refused[reason] += changed != TIER1_EMIT_OK;
                if (changed == TIER1_EMIT_OK)
                {
                    const Tier1EmitRecolorCost* cost = &candidate.score.cost;
                    const bool better = tier1_emit_recolor_better(cost, &current_cost);
                    weighted += !better;
                    const bool preferred = tier1_emit_recolor_total(cost) < tier1_emit_recolor_total(&best_cost) ||
                                           (tier1_emit_recolor_total(cost) == tier1_emit_recolor_total(&best_cost) && cost->preservation < best_cost.preservation);
                    if (better && preferred)
                    {
                        const Tier1EmitRegionPathStatus path = tier1_emit_region_certify(&current_input, &candidate.score, scratch, &work);
                        bounded = path != TIER1_REGION_PATH_LIMIT;
                        ok = path != TIER1_REGION_PATH_MEMORY;
                        paths += path != TIER1_REGION_PATH_OK;
                        if (path == TIER1_REGION_PATH_OK)
                        {
                            chosen = target;
                            best_cost = *cost;
                        }
                    }
                }
            }
            if (ok && bounded && chosen < 16)
            {
                Tier1EmitBatchCandidate candidate = { 0 };
                uint32_t reason = 0;
                Tier1EmitError changed = tier1_emit_batch_candidate(plan, emitted, &current_input, &current, &edges, &model, &arguments,
                    &cache, &current_cost, i, (uint8_t)chosen, used, &materialized[slot], &epoch_output, &epoch_scratch, &score_output,
                    scratch, &work, &candidate, &reason);
                bounded = changed != TIER1_EMIT_LIMIT;
                ok = changed == TIER1_EMIT_OK || !bounded;
                if (ok && bounded)
                {
                    ok = candidate.score.cost.preservation == best_cost.preservation && candidate.score.cost.arguments == best_cost.arguments &&
                         candidate.score.cost.edges == best_cost.edges;
                    const Tier1EmitRegionPathStatus path = ok ? tier1_emit_region_certify(&current_input, &candidate.score, scratch, &work) : TIER1_REGION_PATH_INVALID;
                    bounded = path != TIER1_REGION_PATH_LIMIT;
                    ok = path == TIER1_REGION_PATH_OK || !bounded;
                }
                if (ok && bounded)
                {
                    changed = tier1_emit_epoch_cache_commit(&cache, &candidate.epochs, BUSTER_TIER1_RECOLOR_ACTIVE_WORK, &work);
                    bounded = changed != TIER1_EMIT_LIMIT;
                    ok = changed == TIER1_EMIT_OK || !bounded;
                }
                if (ok && bounded)
                {
#if defined(BUSTER_TIER1_MEASURE_KINDS)
                    if (trace)
                    {
                        fprintf(stderr, "BATCH_REGION selected instructions=%u value=%u seed=%u first=%u past=%u start=%u end=%u source=%u target=%u displaced=%u saving=%.12Lf preservation=%.12Lf\n",
                            function->instruction_count, value, i, candidate.range.first, candidate.range.past, current.intervals.starts[candidate.range.first],
                            current.intervals.ends[candidate.range.past - 1], source, chosen, candidate.displaced,
                            (long double)(tier1_emit_recolor_total(&current_cost) - tier1_emit_recolor_total(&best_cost)) / (UINT64_C(1) << 32),
                            (long double)(current_cost.preservation - best_cost.preservation) / (UINT64_C(1) << 32));
                    }
#endif
                    current = candidate.cfg;
                    current_input = candidate.input;
                    current_cost = best_cost;
                    held = slot;
                    selected += 1;
                }
            }
        }
    }
    const bool published = ok && selected != 0 && tier1_emit_region_publish(&original, &current_input, &current, output, input, cfg);
#if defined(BUSTER_TIER1_MEASURE_KINDS)
    if (trace)
    {
        fprintf(stderr, "BATCH_REGION summary instructions=%u supported=%u bounded=%u selected=%u published=%u trials=%u owners=%u endpoint=%u materialize=%u closure=%u traffic=%u epochs=%u score=%u weighted=%u paths=%u work=%llu saving=%.12Lf preservation=%.12Lf\n",
            function->instruction_count, ok, bounded, selected, published, trials, refused[1], refused[2], refused[3], refused[4], refused[5], refused[6], refused[7],
            weighted, paths, (unsigned long long)work, (long double)(tier1_emit_recolor_total(&initial_cost) - tier1_emit_recolor_total(&current_cost)) / (UINT64_C(1) << 32),
            (long double)(initial_cost.preservation - current_cost.preservation) / (UINT64_C(1) << 32));
    }
    tier1_emit_recolor_work_limit = previous_limit;
#else
    (void)published;
    (void)initial_cost;
    (void)trials;
    (void)weighted;
    (void)paths;
    (void)refused;
#endif
    arena_pop_to(scratch, mark);
    return TIER1_EMIT_OK;
}
