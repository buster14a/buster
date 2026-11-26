// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Bounded contained-interval exchange. Trials change only a sparse register/edge view. Exact cached epochs and
// ABI resolutions price it; separate path certificates protect transport and total changing instruction counts.
// Only the chosen, independently materialized complete allocation can advance the accepted cache generation.

// Publication owns all ten mutable columns. Nothing in the result may retain a pointer into reusable search
// workspace. A failed publication restores the output mark and leaves both caller descriptors unchanged.
static bool tier1_emit_region_publish(const Tier1CfgAllocation* original, const Tier1CfgInput* source_input,
                                      const Tier1CfgAllocation* source, Arena* output, Tier1CfgInput* result_input,
                                      Tier1CfgAllocation* result)
{
    const uint64_t mark = output->length;
    Tier1CfgInput built_input = *source_input;
    Tier1CfgAllocation staged = *source;
    Tier1CfgAllocation built = { 0 };
    bool ok = true;
    uint32_t* values = ir_allocate(output, (uint64_t)source_input->value_count + 1, 4, &ok);
    uint32_t* intervals = ok ? ir_allocate(output, (uint64_t)source->intervals.count + 1, 4, &ok) : 0;
    if (ok)
    {
        memcpy(values, source_input->allowed_registers, (uint64_t)source_input->value_count * 4);
        memcpy(intervals, source->intervals.allowed_registers, (uint64_t)source->intervals.count * 4);
        values[source_input->value_count] = 0;
        intervals[source->intervals.count] = 0;
        built_input.allowed_registers = values;
        staged.intervals.allowed_registers = intervals;
        ok = tier1_emit_recolor_publish(&built_input, original, &staged, output, &built);
    }
    if (ok)
    {
        built.retained_bytes = original->retained_bytes + output->length - mark;
        *result_input = built_input;
        *result = built;
    }
    else
    {
        arena_pop_to(output, mark);
    }
    return ok;
}

// Reserve without clearing: each component initializes its own logical columns. The outer arena's free tail
// remains disjoint working scratch even though these retained slices come from the same backing reservation.
static bool tier1_emit_region_arena(Arena* scratch, uint64_t bytes, Arena* result)
{
    uint8_t* storage = bytes != 0 ? arena_push_array(scratch, bytes, 1, 64) : 0;
    const bool ok = storage != 0;
    if (ok)
    {
        *result = (Arena){ .base = storage, .capacity = bytes };
    }
    return ok;
}

typedef struct Tier1EmitRegionCandidate Tier1EmitRegionCandidate;
struct Tier1EmitRegionCandidate
{
    Tier1EmitRegionProposal proposal;
    Tier1EmitEpochTrial epochs;
    Tier1EmitRegionScore score;
};

// Cheap sparse traffic/control rejection precedes epoch replay. The score helper checks the same contract before
// publishing gains. The proposal is never emitted; its complete materializer checks all locations and edge copies.
static Tier1EmitError tier1_emit_region_candidate(const Tier1SelectPlan* plan, const Tier1CfgInput* input,
                                                  const Tier1CfgAllocation* current, const Tier1EmitRecolorModel* model,
                                                  const Tier1EmitRegionArguments* arguments, Tier1EmitRegionInterval* interval,
                                                  uint8_t target, const Tier1EmitEpochCache* cache,
                                                  const Tier1EmitRecolorCost* current_cost, Arena* epoch_output,
                                                  Arena* epoch_scratch, Arena* score_output, Arena* scratch, uint64_t* work,
                                                  Tier1EmitRegionCandidate* result)
{
    arena_pop_to(epoch_output, 0);
    arena_pop_to(score_output, 0);
    Tier1EmitRegionCandidate built = { 0 };
    Tier1EmitError error = tier1_emit_region_propose(interval, target, scratch, work, &built.proposal);
    if (error == TIER1_EMIT_OK)
    {
        error = (built.proposal.temp_slots & ~current->temp_slots) == 0 ? error : TIER1_EMIT_INVALID;
    }
    if (error == TIER1_EMIT_OK)
    {
        error = tier1_emit_recolor_charge(built.proposal.edge_count, work) ? error : TIER1_EMIT_LIMIT;
    }
    for (uint32_t k = 0; error == TIER1_EMIT_OK && k < built.proposal.edge_count; k += 1)
    {
        const uint32_t edge = built.proposal.edges[k];
        const bool traffic = built.proposal.after_memory[k] <= built.proposal.before_memory[k] &&
                             built.proposal.after_recipes[k] == built.proposal.before_recipes[k];
        const bool control = tier1_emit_region_score_control(plan, input, edge, current->edge_placements[edge], built.proposal.placements[k]);
        error = traffic && control ? error : TIER1_EMIT_INVALID;
    }
    if (error == TIER1_EMIT_OK)
    {
        error = tier1_emit_epoch_cache_trial(cache, &built.proposal.cfg, epoch_output, epoch_scratch,
                                             BUSTER_TIER1_RECOLOR_WORK, work, &built.epochs);
    }
    if (error == TIER1_EMIT_OK)
    {
        error = tier1_emit_region_score_trial(plan, input, current, &built.proposal, model, arguments, cache, &built.epochs,
                                              current_cost, score_output, scratch, work, &built.score);
    }
    if (error == TIER1_EMIT_OK)
    {
        *result = built;
    }
    return error;
}

static Tier1EmitRegionPathStatus tier1_emit_region_certify_result(const Tier1CfgInput* input, const Tier1EmitRegionScore* score,
                                                                 Arena* scratch, uint64_t* work, uint32_t* failed_metric)
{
    Tier1EmitRegionPathStatus status = tier1_emit_region_path_gain(input, score->preservation_gains, score->edge_gains,
                                                                  scratch, BUSTER_TIER1_RECOLOR_WORK, work, 0);
    uint32_t metric = status != TIER1_REGION_PATH_OK ? 1 : 0;
    if (status == TIER1_REGION_PATH_OK)
    {
        status = tier1_emit_region_path_gain(input, score->block_gains, score->edge_gains,
                                             scratch, BUSTER_TIER1_RECOLOR_WORK, work, 0);
        metric = status != TIER1_REGION_PATH_OK ? 2 : 0;
    }
    if (failed_metric != 0)
    {
        *failed_metric = metric;
    }
    return status;
}

static Tier1EmitRegionPathStatus tier1_emit_region_certify(const Tier1CfgInput* input, const Tier1EmitRegionScore* score,
                                                          Arena* scratch, uint64_t* work)
{
    return tier1_emit_region_certify_result(input, score, scratch, work, 0);
}

static Tier1EmitError tier1_emit_cfg_cached_regions(const Tier1SelectPlan* plan, const Tier1EmitResult* emitted,
                                                   Arena* output, Arena* scratch, Tier1CfgInput* input, Tier1CfgAllocation* cfg)
{
    const uint64_t mark = scratch->length;
    const IrFunction* function = plan->function;
    const Tier1CfgAllocation original = *cfg;
    const Tier1CfgInput original_input = *input;
    uint32_t parameters[BUSTER_ABI_INTEGER_ARGUMENTS];
    uint8_t incoming[BUSTER_ABI_INTEGER_ARGUMENTS];
    uint32_t parameter_count = 0;
    uint64_t work = 0;
    bool ok = function->block_count != 0 && cfg->split_count == 0 && input->definition_registers != 0 && output != scratch;
    bool bounded = !ok || tier1_emit_recolor_charge((uint64_t)function->block_offsets[1] + cfg->intervals.count + cfg->allocation.fragment_count, &work);
    for (uint32_t node = 0; ok && bounded && node < function->block_offsets[1]; node += 1)
    {
        if (tier1_select_entry_parameter(function, node))
        {
            const uint32_t value = emitted->node_values[node];
            const Tier1SelectPlace place = tier1_select_parameter_place(function, function->a[node]);
            if (place.kind == BUSTER_TIER1_PLACE_GENERAL && value != BUSTER_TIER1_CFG_NONE && input->definition_registers[value] != 0)
            {
                ok = parameter_count < BUSTER_ABI_INTEGER_ARGUMENTS;
                if (ok)
                {
                    parameters[parameter_count] = value;
                    incoming[parameter_count] = tier1_select_parameter_registers[place.index];
                    parameter_count += 1;
                }
            }
        }
    }
    const uint32_t callee = emitted->callee_registers & input->registers[BUSTER_TIER1_ALLOCATE_GPR];
    const uint32_t used = ok && bounded ? tier1_emit_cfg_callee_used(cfg, callee) : 0;
    ok = ok && parameter_count != 0 && used != 0 && emitted->prepared.call_count != 0;
    Tier1EmitRecolorModel model = { 0 };
    // The unchanged model has its own 8 Mi structural frequency cap. Reserve that complete bound once, plus
    // its other scans, rather than hiding those visits outside this search's shared 128 Mi work allowance.
    const uint64_t model_work = BUSTER_TIER1_FREQUENCY_WORK + (uint64_t)function->instruction_count * 2 +
                                plan->effective_use_offsets[function->instruction_count] + input->sequence_count + input->fixed.count +
                                (uint64_t)input->block_count * 4 + input->edge_count * UINT64_C(2) + emitted->prepared.call_count;
    bounded = !ok || !bounded ? bounded : tier1_emit_recolor_charge(model_work, &work);
    ok = !bounded ? ok : ok && tier1_emit_recolor_model(plan, emitted, &original_input, scratch, &model);
    Tier1EmitRegionEdges edges = { 0 };
    Tier1EmitRegionArguments arguments = { 0 };
    Tier1EmitEpochCache cache = { 0 };
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
    Arena retained = { 0 };
    Arena epoch_scratch = { 0 };
    Arena epoch_output = { 0 };
    Arena score_output = { 0 };
    Arena interval_output = { 0 };
    Arena materialized[2] = { 0 };
    if (ok && bounded)
    {
        const uint64_t bytes = tier1_emit_epoch_cache_bytes(plan, &original_input, &original, edges.entries);
        const uint64_t workspace_bytes = tier1_emit_epoch_cache_scratch_bytes(plan, &original_input, &original);
        ok = tier1_emit_region_arena(scratch, bytes, &retained) && tier1_emit_region_arena(scratch, workspace_bytes, &epoch_scratch);
    }
    if (ok && bounded)
    {
        setup = tier1_emit_epoch_cache_prepare(plan, emitted, &original_input, &original, &retained, &epoch_scratch,
                                               BUSTER_TIER1_RECOLOR_WORK, &work, &cache);
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
        // Reserve publication work now so reaching the search limit can always retain the last complete prefix.
        const uint64_t publish_work = 2 * ((uint64_t)original.allocation.fragment_count + original_input.value_count * UINT64_C(2) +
                                           original.intervals.count + original_input.edge_count * UINT64_C(2) + moves * 3 + 16);
        bounded = tier1_emit_recolor_charge(publish_work, &work);
        ok = !bounded || (tier1_emit_region_arena(scratch, tier1_emit_epoch_trial_bytes(&cache), &epoch_output) &&
             tier1_emit_region_arena(scratch, tier1_emit_region_score_bytes(input), &score_output) &&
             tier1_emit_region_arena(scratch, tier1_emit_region_interval_bytes(&edges), &interval_output) &&
             tier1_emit_region_arena(scratch, bytes, &materialized[0]) && tier1_emit_region_arena(scratch, bytes, &materialized[1]));
    }
    Tier1CfgAllocation current = original;
    Tier1CfgInput current_input = original_input;
    uint32_t held = 2;
    uint32_t selected = 0;
    uint32_t considered = 0;
    uint32_t trials = 0;
    uint32_t endpoint_refusals = 0;
    uint32_t proposal_refusals = 0;
    uint32_t path_refusals = 0;
    uint32_t interval_candidates = 0;
    uint32_t admission_refusals = 0;
    uint32_t weighted_refusals = 0;
    uint32_t transport_refusals = 0;
    uint32_t total_refusals = 0;
    uint64_t replayed_blocks = 0;
#if defined(BUSTER_TIER1_MEASURE_KINDS)
    const char* trace_env = getenv("BUSTER_TIER1_EXCHANGE_TRACE");
    const bool trace = trace_env != 0 && (uint32_t)strtoul(trace_env, 0, 10) == function->instruction_count;
#endif
    for (uint32_t p = 0; ok && bounded && p < parameter_count; p += 1)
    {
        const uint32_t value = parameters[p];
        const uint8_t source = incoming[p];
        const uint32_t targets = used & original_input.allowed_registers[value] & ~model.forbidden;
        for (uint32_t i = original.value_interval_offsets[value]; ok && bounded && (model.forbidden & (1u << source)) == 0 &&
             i < original.value_interval_offsets[value + 1]; i += 1)
        {
            const uint32_t search_visits = 1 + 32 - (uint32_t)__builtin_clz(model.count | 1u);
            bounded = tier1_emit_recolor_charge(search_visits, &work);
            const uint32_t first = current.allocation.fragment_offsets[i];
            const uint32_t start = current.intervals.starts[i];
            const uint32_t end = current.intervals.ends[i];
            const bool eligible = targets != 0 && start > original_input.definitions[value] &&
                                  current.allocation.fragment_offsets[i + 1] - first == 1 &&
                                  current.allocation.kinds[first] == BUSTER_TIER1_ALLOCATE_REGISTER &&
                                  current.allocation.registers[first] == source && tier1_emit_recolor_crosses(&model, start, end);
            Tier1EmitRegionInterval interval = { 0 };
            Tier1EmitError begun = TIER1_EMIT_INVALID;
            if (eligible && bounded)
            {
                interval_candidates += 1;
                arena_pop_to(&interval_output, 0);
                begun = tier1_emit_region_interval_begin(&edges, &current_input, &current, i, used,
                                                         &interval_output, &work, &interval);
                bounded = begun != TIER1_EMIT_LIMIT;
                ok = begun == TIER1_EMIT_OK || begun == TIER1_EMIT_INVALID || !bounded;
            }
            uint32_t chosen = 16;
            Tier1EmitRecolorCost best_cost = current_cost;
            const uint32_t available = begun == TIER1_EMIT_OK ? interval.admission.targets & targets : 0;
            admission_refusals += eligible && bounded && available == 0;
            const uint32_t base = 2 * (original_input.block_count - function->block_count);
            const uint32_t endpoint_node = end > base && ((end - base) & 1) != 0 ? (end - base - 1) / 2 : UINT32_MAX;
            const uint64_t endpoint_work = BUSTER_TIER1_SELECT_VALUES + 2 + (endpoint_node < function->instruction_count ?
                                           (uint64_t)plan->effective_use_offsets[endpoint_node + 1] - plan->effective_use_offsets[endpoint_node] : 0);
            for (uint32_t remaining = available; ok && bounded && remaining != 0; remaining &= remaining - 1)
            {
                const uint8_t target = (uint8_t)__builtin_ctz(remaining);
                bounded = tier1_emit_recolor_charge(endpoint_work, &work);
                const bool endpoint = bounded && tier1_emit_recolor_endpoint(plan, emitted, &current_input, &current, i, source, target);
                endpoint_refusals += bounded && !endpoint;
                considered += endpoint;
                if (endpoint)
                {
                    Tier1EmitRegionCandidate candidate = { 0 };
                    const Tier1EmitError changed = tier1_emit_region_candidate(plan, &current_input, &current, &model, &arguments,
                                                                               &interval, target, &cache, &current_cost,
                                                                               &epoch_output, &epoch_scratch, &score_output, scratch, &work, &candidate);
                    bounded = changed != TIER1_EMIT_LIMIT;
                    ok = changed == TIER1_EMIT_OK || changed == TIER1_EMIT_INVALID || !bounded;
                    proposal_refusals += changed == TIER1_EMIT_INVALID;
                    if (changed == TIER1_EMIT_OK)
                    {
                        trials += 1;
                        replayed_blocks += candidate.epochs.block_count;
                        const Tier1EmitRecolorCost cost = candidate.score.cost;
                        const bool better = tier1_emit_recolor_better(&cost, &current_cost);
                        weighted_refusals += !better;
                        const bool preferred = tier1_emit_recolor_total(&cost) < tier1_emit_recolor_total(&best_cost) ||
                                               (tier1_emit_recolor_total(&cost) == tier1_emit_recolor_total(&best_cost) && cost.preservation < best_cost.preservation);
                        if (better && preferred)
                        {
                            uint32_t metric = 0;
                            const Tier1EmitRegionPathStatus path = tier1_emit_region_certify_result(&current_input, &candidate.score, scratch, &work, &metric);
                            bounded = path != TIER1_REGION_PATH_LIMIT;
                            ok = path != TIER1_REGION_PATH_MEMORY;
                            path_refusals += path != TIER1_REGION_PATH_OK;
                            transport_refusals += metric == 1;
                            total_refusals += metric == 2;
                            if (path == TIER1_REGION_PATH_OK)
                            {
                                chosen = target;
                                best_cost = cost;
                            }
                        }
                    }
                }
            }
            if (ok && bounded && chosen < 16)
            {
                // Rebuild the chosen view; prior proposals are invalidated on each target. Recheck its costs and
                // both path certificates, then independently materialize its complete owned allocation. Cache
                // commit is transactional and precedes advancing either accepted descriptor.
                Tier1EmitRegionCandidate candidate = { 0 };
                Tier1EmitError changed = tier1_emit_region_candidate(plan, &current_input, &current, &model, &arguments,
                                                                      &interval, (uint8_t)chosen, &cache, &current_cost,
                                                                      &epoch_output, &epoch_scratch, &score_output, scratch, &work, &candidate);
                bounded = changed != TIER1_EMIT_LIMIT;
                ok = changed == TIER1_EMIT_OK || !bounded;
                if (ok && bounded)
                {
                    const Tier1EmitRecolorCost* cost = &candidate.score.cost;
                    ok = cost->preservation == best_cost.preservation && cost->arguments == best_cost.arguments && cost->edges == best_cost.edges;
                }
                if (ok && bounded)
                {
                    const Tier1EmitRegionPathStatus path = tier1_emit_region_certify(&current_input, &candidate.score, scratch, &work);
                    bounded = path != TIER1_REGION_PATH_LIMIT;
                    ok = path == TIER1_REGION_PATH_OK || !bounded;
                }
                const uint32_t slot = held == 0 ? 1 : 0;
                Tier1CfgAllocation next = { 0 };
                Tier1CfgInput next_input = { 0 };
                if (ok && bounded)
                {
                    arena_pop_to(&materialized[slot], 0);
                    changed = tier1_emit_region_materialize(&interval, &candidate.proposal, &materialized[slot], scratch, &work, &next_input, &next);
                    bounded = changed != TIER1_EMIT_LIMIT;
                    ok = changed == TIER1_EMIT_OK || !bounded;
                }
                if (ok && bounded)
                {
                    changed = tier1_emit_epoch_cache_commit(&cache, &candidate.epochs, BUSTER_TIER1_RECOLOR_WORK, &work);
                    bounded = changed != TIER1_EMIT_LIMIT;
                    ok = changed == TIER1_EMIT_OK || !bounded;
                }
                if (ok && bounded)
                {
#if defined(BUSTER_TIER1_MEASURE_KINDS)
                    if (trace)
                    {
                        fprintf(stderr, "CACHED_REGION selected instructions=%u value=%u interval=%u start=%u end=%u source=%u target=%u displaced=%u saving=%.12Lf preservation=%.12Lf\n",
                                function->instruction_count, value, i, start, end, source, chosen, candidate.proposal.stats.displaced_fragments,
                                (long double)(tier1_emit_recolor_total(&current_cost) - tier1_emit_recolor_total(&best_cost)) / (UINT64_C(1) << 32),
                                (long double)(current_cost.preservation - best_cost.preservation) / (UINT64_C(1) << 32));
                    }
#endif
                    current = next;
                    current_input = next_input;
                    current_cost = best_cost;
                    held = slot;
                    selected += 1;
                }
            }
        }
#if defined(BUSTER_TIER1_MEASURE_KINDS)
        if (trace)
        {
            fprintf(stderr, "CACHED_REGION parameter instructions=%u value=%u source=%u bounded=%u cumulative_selected=%u cumulative_intervals=%u work=%llu\n",
                    function->instruction_count, value, source, bounded, selected, interval_candidates, (unsigned long long)work);
        }
#endif
    }
    bool published = false;
    if (ok && selected != 0)
    {
        published = tier1_emit_region_publish(&original, &current_input, &current, output, input, cfg);
    }
#if defined(BUSTER_TIER1_MEASURE_KINDS)
    if (trace)
    {
        fprintf(stderr, "CACHED_REGION summary instructions=%u supported=%u bounded=%u selected=%u published=%u intervals=%u admission_refusals=%u considered=%u trials=%u endpoint_refusals=%u proposal_refusals=%u weighted_refusals=%u path_refusals=%u transport_refusals=%u total_refusals=%u replayed_blocks=%llu work=%llu saving=%.12Lf preservation=%.12Lf\n",
                function->instruction_count, ok, bounded, selected, published, interval_candidates, admission_refusals, considered, trials, endpoint_refusals,
                proposal_refusals, weighted_refusals, path_refusals, transport_refusals, total_refusals,
                (unsigned long long)replayed_blocks, (unsigned long long)work,
                (long double)(tier1_emit_recolor_total(&initial_cost) - tier1_emit_recolor_total(&current_cost)) / (UINT64_C(1) << 32),
                (long double)(initial_cost.preservation - current_cost.preservation) / (UINT64_C(1) << 32));
    }
#else
    (void)initial_cost;
    (void)considered;
    (void)trials;
    (void)endpoint_refusals;
    (void)proposal_refusals;
    (void)path_refusals;
    (void)interval_candidates;
    (void)admission_refusals;
    (void)weighted_refusals;
    (void)transport_refusals;
    (void)total_refusals;
    (void)replayed_blocks;
    (void)published;
#endif
    arena_pop_to(scratch, mark);
    return TIER1_EMIT_OK;
}
