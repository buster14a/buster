// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Measurement-only #2610 call windows. An immutable GPR parameter moves into a free, already paid callee register
// at a call's use phase, then back at the next node's use phase. These are allocation fragments and real MOV edits,
// not call-save records with different tags. Edges, interval ranges, kinds, slots, bindings and the frame stay fixed.
// A no-op fragment template freezes the epoch cache's shape before trials; identity boundaries emit and owe nothing.

#define BUSTER_TIER1_CALL_WINDOWS (1u << 15)

typedef struct Tier1EmitCallWindow Tier1EmitCallWindow;
struct Tier1EmitCallWindow
{
    uint32_t call;
    uint32_t interval;
    uint32_t original_fragment;
    uint32_t fragment;
    uint32_t value;
    uint32_t use;
    uint32_t choices;
    uint8_t source;
};

static bool tier1_emit_call_window_bounds(const Tier1CfgInput* input, const Tier1CfgAllocation* cfg, uint32_t interval, uint32_t use)
{
    const uint32_t f = cfg->allocation.fragment_offsets[interval];
    const uint32_t b = tier1_cfg_block_at(input, cfg, use);
    return (use & 1) == 0 && cfg->allocation.fragment_offsets[interval + 1] == f + 1 &&
           cfg->allocation.kinds[f] == BUSTER_TIER1_ALLOCATE_REGISTER && cfg->intervals.starts[interval] < use &&
           (uint64_t)use + 2 < cfg->intervals.ends[interval] && input->block_starts[b] < use &&
           (uint64_t)use + 2 < input->block_starts[b + 1];
}

// Any occurrence of the parameter at the call must be one the existing exact general-argument resolver models.
// This includes an indirect callee and a pointer result. A parameter used as a stack/aggregate address is refused.
static bool tier1_emit_call_window_operands(const Tier1SelectPlan* plan, const Tier1EmitResult* emitted,
                                           const Tier1EmitRecolorCall* call, uint32_t value)
{
    uint32_t reads = 0;
    uint32_t modeled = 0;
    for (uint32_t u = plan->effective_use_offsets[call->node]; u < plan->effective_use_offsets[call->node + 1]; u += 1)
    {
        reads += emitted->node_values[plan->effective_use_values[u]] == value;
    }
    for (uint32_t a = 0; a < call->count; a += 1)
    {
        modeled += call->values[a] == value;
    }
    return reads == modeled;
}

static uint32_t tier1_emit_call_window_first(const Tier1EmitRecolorModel* model, uint32_t start)
{
    uint32_t first = 0;
    uint32_t past = model->count;
    while (first < past)
    {
        const uint32_t middle = first + (past - first) / 2;
        const bool earlier = model->calls[middle].position <= start + 1;
        first = earlier ? middle + 1 : first;
        past = earlier ? past : middle;
    }
    return first;
}

// Keep every original fragment in order. A window adds two boundaries inside one complete register interval;
// adjacent calls are excluded by admission, so every retained prefix and suffix has positive length.
static Tier1EmitError tier1_emit_call_window_template(const Tier1CfgAllocation* original, Tier1EmitCallWindow* windows,
                                                      uint32_t count, Arena* output, Arena* scratch,
                                                      uint64_t* work, Tier1CfgAllocation* result)
{
    const uint64_t mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    const Tier1Allocation* old = &original->allocation;
    const uint64_t fragments = old->fragment_count + 2 * (uint64_t)count;
    const uint64_t bound = 2 * ((uint64_t)original->intervals.count + original->intervals.use_count);
    bool ok = fragments <= UINT32_MAX && fragments <= bound;
    Tier1EmitError error = ok ? TIER1_EMIT_OK : TIER1_EMIT_UNSUPPORTED;
    error = error != TIER1_EMIT_OK || tier1_emit_recolor_charge(fragments * 6 + original->intervals.count + original->intervals.value_count, work) ? error : TIER1_EMIT_LIMIT;
    Tier1CfgAllocation built = *original;
    uint32_t* mapped = error == TIER1_EMIT_OK ? ir_allocate(scratch, (uint64_t)old->fragment_count + 1, 4, &ok) : 0;
    built.allocation.fragment_offsets = error == TIER1_EMIT_OK && ok ? ir_allocate(output, (uint64_t)old->interval_count + 1, 4, &ok) : 0;
    built.allocation.starts = error == TIER1_EMIT_OK && ok ? ir_allocate(output, fragments + 1, 4, &ok) : 0;
    built.allocation.ends = error == TIER1_EMIT_OK && ok ? ir_allocate(output, fragments + 1, 4, &ok) : 0;
    built.allocation.kinds = error == TIER1_EMIT_OK && ok ? ir_allocate(output, fragments + 1, 1, &ok) : 0;
    built.allocation.registers = error == TIER1_EMIT_OK && ok ? ir_allocate(output, fragments + 1, 1, &ok) : 0;
    built.allocation.value_registers = error == TIER1_EMIT_OK && ok ? ir_allocate(output, (uint64_t)original->intervals.value_count + 1, 1, &ok) : 0;
    error = error == TIER1_EMIT_OK && !ok ? TIER1_EMIT_MEMORY : error;
    uint32_t next = 0;
    uint32_t window = 0;
    for (uint32_t f = 0; error == TIER1_EMIT_OK && f < old->fragment_count; f += 1)
    {
        mapped[f] = next;
        uint32_t cursor = old->starts[f];
        while (window < count && windows[window].original_fragment == f)
        {
            const uint32_t use = windows[window].use;
            built.allocation.starts[next] = cursor;
            built.allocation.ends[next] = use;
            built.allocation.kinds[next] = old->kinds[f];
            built.allocation.registers[next++] = old->registers[f];
            windows[window].fragment = next;
            built.allocation.starts[next] = use;
            built.allocation.ends[next] = use + 2;
            built.allocation.kinds[next] = old->kinds[f];
            built.allocation.registers[next++] = old->registers[f];
            cursor = use + 2;
            window += 1;
        }
        built.allocation.starts[next] = cursor;
        built.allocation.ends[next] = old->ends[f];
        built.allocation.kinds[next] = old->kinds[f];
        built.allocation.registers[next++] = old->registers[f];
    }
    if (error == TIER1_EMIT_OK)
    {
        mapped[old->fragment_count] = next;
        error = next == fragments && window == count ? error : TIER1_EMIT_INVALID;
    }
    if (error == TIER1_EMIT_OK)
    {
        for (uint32_t i = 0; i <= old->interval_count; i += 1)
        {
            built.allocation.fragment_offsets[i] = mapped[old->fragment_offsets[i]];
        }
        memcpy(built.allocation.value_registers, old->value_registers, original->intervals.value_count);
        built.allocation.value_registers[original->intervals.value_count] = UINT8_MAX;
        built.allocation.registers[fragments] = UINT8_MAX;
        built.allocation.fragment_count = (uint32_t)fragments;
        built.retained_bytes += output->length - mark;
        *result = built;
    }
    else
    {
        arena_pop_to(output, mark);
    }
    arena_pop_to(scratch, scratch_mark);
    return error;
}

static Tier1EmitError tier1_emit_call_window_score(const Tier1CfgInput* input, const Tier1CfgAllocation* current,
                                                   const Tier1CfgAllocation* candidate, const Tier1EmitRecolorModel* model,
                                                   const Tier1EmitRegionArguments* arguments, const Tier1EmitEpochCache* cache,
                                                   const Tier1EmitEpochTrial* epochs, uint32_t block,
                                                   Arena* output, Arena* scratch, uint64_t* work, Tier1EmitRegionScore* result)
{
    bool ok = true;
    Tier1EmitRegionScore built = { 0 };
    Tier1EmitError error = tier1_emit_recolor_charge((uint64_t)input->block_count * 2 + input->edge_count + (uint64_t)epochs->block_count * 64, work) ? TIER1_EMIT_OK : TIER1_EMIT_LIMIT;
    built.preservation_gains = error == TIER1_EMIT_OK ? ir_allocate(output, (uint64_t)input->block_count + 1, 8, &ok) : 0;
    built.block_gains = error == TIER1_EMIT_OK && ok ? ir_allocate(output, (uint64_t)input->block_count + 1, 8, &ok) : 0;
    built.edge_gains = error == TIER1_EMIT_OK && ok ? ir_allocate(output, (uint64_t)input->edge_count + 1, 8, &ok) : 0;
    error = error == TIER1_EMIT_OK && !ok ? TIER1_EMIT_MEMORY : error;
    for (uint32_t k = 0; error == TIER1_EMIT_OK && k < epochs->block_count; k += 1)
    {
        const uint32_t b = epochs->blocks[k];
        const uint64_t before = (uint64_t)b * 16;
        const uint64_t after = (uint64_t)k * 16;
        const uint64_t old_records = tier1_emit_region_preservation(cache->stores + before, cache->loads + before, model->callee);
        const uint64_t new_records = tier1_emit_region_preservation(epochs->stores + after, epochs->loads + after, model->callee);
        const int64_t gain = (int64_t)old_records - (int64_t)new_records;
        built.preservation_gains[b] = gain;
        built.block_gains[b] = gain;
        built.cost.preservation -= (__int128)model->weights[b] * gain;
    }
    if (error == TIER1_EMIT_OK)
    {
        // Every accepted window replaces two identity boundaries by two real register MOV records. The first
        // certificate protects memory preservation plus allocation transport; the second also prices ABI moves.
        built.preservation_gains[block] -= 2;
        built.block_gains[block] -= 2;
        built.cost.edges = 2 * (__int128)model->weights[block];
        __int128 argument_gain = 0;
        error = tier1_emit_region_argument_gains(arguments, model, current, candidate, epochs->fragments, epochs->fragment_count,
                                                scratch, work, built.block_gains, &argument_gain);
        built.cost.arguments = -argument_gain;
    }
    if (error == TIER1_EMIT_OK)
    {
        *result = built;
    }
    return error;
}

static void tier1_emit_cfg_call_windows(const Tier1SelectPlan* plan, const Tier1EmitResult* emitted,
                                        Arena* output, Arena* scratch, const Tier1CfgInput* input,
                                        Tier1CfgAllocation* cfg, uint32_t mode, uint32_t* retained_rows)
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
    bool ok = function->block_count != 0 && cfg->split_count == 0 && input->definition_registers != 0 && output != scratch && emitted->prepared.call_count != 0;
    bool bounded = true;
    const uint32_t callee = emitted->callee_registers & input->registers[BUSTER_TIER1_ALLOCATE_GPR];
    const uint32_t used = ok ? tier1_emit_cfg_callee_used(cfg, callee) : 0;
    ok = ok && used != 0;
    Tier1EmitRecolorModel model = { 0 };
    // Reserve the final original-frame census before trials, so a budget-limited accepted prefix can still publish
    // it. save_positions visits at most 16 fixed roles twice per node; cfg_saves adds four node scans, at most four
    // binary searches per fragment, and a skipped fixed reader per effective use. The reservation covers all of them.
    const uint64_t final_work = mode == 1 ? function->instruction_count * UINT64_C(64) +
                                           original.allocation.fragment_count * UINT64_C(160) + original.intervals.count +
                                           plan->effective_use_offsets[function->instruction_count] + 16 : 0;
    const uint64_t model_work = BUSTER_TIER1_FREQUENCY_WORK + (uint64_t)function->instruction_count * 10 +
                                plan->effective_use_offsets[function->instruction_count] + input->sequence_count + input->fixed.count +
                                input->block_count * UINT64_C(4) + input->edge_count * UINT64_C(2) + input->position_count +
                                original.intervals.count * UINT64_C(34) + original.allocation.fragment_count * UINT64_C(34) + input->value_count * UINT64_C(3) + final_work;
    bounded = !ok || tier1_emit_recolor_charge(model_work, &work);
    ok = !bounded ? ok : ok && tier1_emit_recolor_model(plan, emitted, input, scratch, &model);
    const uint64_t capacity = (uint64_t)model.count * BUSTER_ABI_INTEGER_ARGUMENTS;
    ok = ok && capacity <= BUSTER_TIER1_CALL_WINDOWS;
    Tier1EmitCallWindow* windows = ok && bounded ? ir_allocate(scratch, capacity + 1, sizeof(Tier1EmitCallWindow), &ok) : 0;
    uint8_t* parameters = ok && bounded ? ir_allocate(scratch, (uint64_t)input->value_count + 1, 1, &ok) : 0;
    uint32_t* free_calls = ok && bounded ? ir_allocate(scratch, (uint64_t)model.count + 1, 4, &ok) : 0;
    uint32_t* busy = ok && bounded ? ir_allocate(scratch, (uint64_t)input->position_count + 1, 4, &ok) : 0;
    for (uint32_t node = 0; ok && bounded && node < function->block_offsets[1]; node += 1)
    {
        if (tier1_select_entry_parameter(function, node))
        {
            const uint32_t value = emitted->node_values[node];
            const Tier1SelectPlace place = tier1_select_parameter_place(function, function->a[node]);
            if (value < input->value_count && place.kind == BUSTER_TIER1_PLACE_GENERAL && input->definition_registers[value] != 0 &&
                input->classes[value] == BUSTER_TIER1_ALLOCATE_GPR && (emitted->prepared.sizes[node] == 4 || emitted->prepared.sizes[node] == 8))
            {
                parameters[value] = 1;
            }
        }
    }
    for (uint32_t c = 0; ok && bounded && c < model.count; c += 1)
    {
        const uint32_t use = model.calls[c].position - 1;
        const uint32_t b = model.calls[c].block;
        const bool adjacent = (c != 0 && model.calls[c - 1].position + 2 == model.calls[c].position) ||
                              (c + 1 < model.count && model.calls[c].position + 2 == model.calls[c + 1].position);
        if (!adjacent && input->block_starts[b] < use && (uint64_t)use + 2 < input->block_starts[b + 1])
        {
            bounded = tier1_emit_recolor_charge((uint64_t)original.intervals.count + original.allocation.fragment_count, &work);
            free_calls[c] = bounded ? tier1_emit_recolor_free(&original, use, use + 3, used & ~model.forbidden) : 0;
        }
    }
    uint32_t count = 0;
    for (uint32_t i = 0; ok && bounded && i < original.intervals.count; i += 1)
    {
        const uint32_t value = original.intervals.values[i];
        const uint32_t f = original.allocation.fragment_offsets[i];
        const uint8_t source = original.allocation.registers[f];
        const uint32_t choices = used & ~model.forbidden & original.intervals.allowed_registers[i];
        const bool eligible = parameters[value] != 0 && choices != 0 && original.intervals.starts[i] > input->definitions[value] &&
                              original.allocation.fragment_offsets[i + 1] == f + 1 && original.allocation.kinds[f] == BUSTER_TIER1_ALLOCATE_REGISTER &&
                              source < 16 && source != X86_REGISTER_RAX && ((model.callee | model.forbidden) & (1u << source)) == 0 &&
                              (original.intervals.rematerializable == 0 || original.intervals.rematerializable[i] == 0);
        const uint32_t first = eligible ? tier1_emit_call_window_first(&model, original.intervals.starts[i]) : model.count;
        for (uint32_t c = first; ok && bounded && c < model.count && model.calls[c].position < original.intervals.ends[i]; c += 1)
        {
            const uint32_t use = model.calls[c].position - 1;
            const uint32_t node = model.calls[c].node;
            bounded = tier1_emit_recolor_charge(1 + model.calls[c].count + (uint64_t)plan->effective_use_offsets[node + 1] - plan->effective_use_offsets[node], &work);
            if (bounded && (choices & free_calls[c]) != 0 && tier1_emit_call_window_bounds(input, &original, i, use) &&
                tier1_emit_call_window_operands(plan, emitted, &model.calls[c], value))
            {
                ok = count < capacity;
                if (ok)
                {
                    windows[count++] = (Tier1EmitCallWindow){ .call = c, .interval = i, .original_fragment = f, .value = value,
                                                            .use = use, .choices = choices & free_calls[c], .source = source };
                }
            }
        }
    }
    for (uint32_t i = 0; ok && bounded && i < original.intervals.count; i += 1)
    {
        for (uint32_t f = original.allocation.fragment_offsets[i] + 1; original.intervals.classes[i] == BUSTER_TIER1_ALLOCATE_GPR && f < original.allocation.fragment_offsets[i + 1]; f += 1)
        {
            const uint8_t a = original.allocation.registers[f - 1];
            const uint8_t b = original.allocation.registers[f];
            if (original.allocation.kinds[f - 1] == BUSTER_TIER1_ALLOCATE_REGISTER && original.allocation.kinds[f] == BUSTER_TIER1_ALLOCATE_REGISTER && a != b)
            {
                busy[original.allocation.starts[f]] |= (1u << a) | (1u << b);
            }
        }
    }
    Tier1CfgAllocation current = original;
    Tier1EmitError setup = ok && bounded && count != 0 ? tier1_emit_call_window_template(&original, windows, count, output, scratch, &work, &current) : TIER1_EMIT_OK;
    bounded = bounded && setup != TIER1_EMIT_LIMIT;
    ok = ok && (setup == TIER1_EMIT_OK || !bounded);
    Tier1EmitEpochCache cache = { 0 };
    Tier1EmitRegionArguments arguments = { 0 };
    Arena retained = { 0 }, epoch_scratch = { 0 }, epoch_output = { 0 }, score_output = { 0 };
    uint8_t* trial_registers = 0;
    if (ok && bounded && count != 0 && mode == 1)
    {
        uint64_t entry_count = 0;
        bounded = tier1_emit_recolor_charge(current.intervals.count * UINT64_C(64), &work);
        for (uint32_t i = 0; bounded && i < current.intervals.count; i += 1)
        {
            const uint32_t start = current.intervals.starts[i];
            const uint32_t first = tier1_cfg_block_at(input, &current, start);
            const uint32_t last = tier1_cfg_block_at(input, &current, current.intervals.ends[i] - 1);
            entry_count += (uint64_t)last - first + (input->block_starts[first] == start);
        }
        Arena argument_output = { 0 };
        ok = !bounded || (ok && tier1_emit_region_arena(scratch, tier1_emit_region_arguments_bytes(&current, &model), &argument_output));
        setup = ok && bounded ? tier1_emit_region_arguments_prepare(&current, &model, &argument_output, scratch, &work, &arguments) : TIER1_EMIT_OK;
        bounded = bounded && setup != TIER1_EMIT_LIMIT;
        ok = ok && (setup == TIER1_EMIT_OK || !bounded);
        if (ok && bounded)
        {
            ok = tier1_emit_region_arena(scratch, tier1_emit_epoch_cache_bytes(plan, input, &current, entry_count), &retained) &&
                 tier1_emit_region_arena(scratch, tier1_emit_epoch_cache_scratch_bytes(plan, input, &current), &epoch_scratch);
            setup = ok ? tier1_emit_epoch_cache_prepare(plan, emitted, input, &current, &retained, &epoch_scratch,
                                                       BUSTER_TIER1_RECOLOR_ACTIVE_WORK, &work, &cache) : TIER1_EMIT_MEMORY;
            bounded = setup != TIER1_EMIT_LIMIT;
            ok = setup == TIER1_EMIT_OK || !bounded;
        }
        if (ok && bounded)
        {
            ok = tier1_emit_region_arena(scratch, tier1_emit_epoch_trial_bytes(&cache), &epoch_output) &&
                 tier1_emit_region_arena(scratch, tier1_emit_region_score_bytes(input), &score_output);
            trial_registers = ok ? ir_allocate(scratch, (uint64_t)current.allocation.fragment_count + 1, 1, &ok) : 0;
        }
    }
    uint32_t selected = 0, weighted = 0, paths = 0, conflicts = 0;
    __int128 saved = 0;
    __int128 total = 0;
    for (uint32_t w = 0; ok && bounded && mode == 1 && w < count; w += 1)
    {
        const Tier1EmitCallWindow* window = windows + w;
        const uint32_t c = window->call;
        const uint32_t touching = busy[window->use] | busy[window->use + 2];
        const uint32_t choices = (touching & (1u << window->source)) == 0 ? window->choices & free_calls[c] & ~touching : 0;
        conflicts += choices == 0;
        if (choices == 0)
        {
            continue;
        }
        const uint8_t target = (uint8_t)__builtin_ctz(choices);
        bounded = tier1_emit_recolor_charge(current.allocation.fragment_count, &work);
        Tier1CfgAllocation candidate = current;
        candidate.allocation.registers = trial_registers;
        if (bounded)
        {
            memcpy(trial_registers, current.allocation.registers, current.allocation.fragment_count);
            trial_registers[window->fragment] = target;
        }
        Tier1EmitEpochTrial epochs = { 0 };
        arena_pop_to(&epoch_output, 0);
        arena_pop_to(&score_output, 0);
        Tier1EmitError changed = bounded ? tier1_emit_epoch_cache_trial(&cache, &candidate, &epoch_output, &epoch_scratch,
                                                                       BUSTER_TIER1_RECOLOR_ACTIVE_WORK, &work, &epochs) : TIER1_EMIT_LIMIT;
        Tier1EmitRegionScore score = { 0 };
        changed = changed == TIER1_EMIT_OK ? tier1_emit_call_window_score(input, &current, &candidate, &model, &arguments, &cache,
                                                                        &epochs, model.calls[c].block, &score_output, scratch, &work, &score) : changed;
        bounded = changed != TIER1_EMIT_LIMIT;
        ok = changed == TIER1_EMIT_OK || !bounded;
        const bool better = ok && bounded && score.cost.preservation < 0 && tier1_emit_recolor_total(&score.cost) <= 0;
        weighted += ok && bounded && !better;
        Tier1EmitRegionPathStatus path = TIER1_REGION_PATH_INVALID;
        if (better)
        {
            path = tier1_emit_region_certify(input, &score, scratch, &work);
            bounded = path != TIER1_REGION_PATH_LIMIT;
            ok = path != TIER1_REGION_PATH_MEMORY;
            paths += path != TIER1_REGION_PATH_OK;
        }
        if (ok && bounded && path == TIER1_REGION_PATH_OK)
        {
            changed = tier1_emit_epoch_cache_commit(&cache, &epochs, BUSTER_TIER1_RECOLOR_ACTIVE_WORK, &work);
            bounded = changed != TIER1_EMIT_LIMIT;
            ok = changed == TIER1_EMIT_OK || !bounded;
            if (ok && bounded)
            {
                current.allocation.registers[window->fragment] = target;
                current.allocation.value_registers[window->value] = UINT8_MAX;
                free_calls[c] &= ~(1u << target);
                busy[window->use] |= (1u << window->source) | (1u << target);
                busy[window->use + 2] |= (1u << window->source) | (1u << target);
                selected += 1;
                saved -= score.cost.preservation;
                total -= tier1_emit_recolor_total(&score.cost);
#if defined(BUSTER_TIER1_MEASURE_KINDS)
                if (trace)
                {
                    fprintf(stderr, "CALL_WINDOW selected instructions=%u node=%u value=%u use=%u source=%u target=%u copies=2 saving=%.12Lf preservation=%.12Lf\n",
                            function->instruction_count, model.calls[c].node, window->value, window->use, window->source, target,
                            (long double)-tier1_emit_recolor_total(&score.cost) / (UINT64_C(1) << 32),
                            (long double)-score.cost.preservation / (UINT64_C(1) << 32));
                }
#endif
            }
        }
    }
    if (ok && selected != 0)
    {
        // Keep the original private-row layout even if the last use of a caller's row disappeared. This makes the
        // frame and ordinary frame-address instructions exactly invariant, not an unpriced shrinking-frame effect.
        Tier1EmitResult prior = *emitted;
        prior.cfg_input = *input;
        prior.cfg = original;
        ok = tier1_emit_cfg_saves(plan, scratch, &prior) == TIER1_EMIT_OK;
        *retained_rows |= ok ? prior.prepared.call_save_mask : 0;
    }
    const bool published = ok && count != 0 && (selected != 0 || (mode == 2 && bounded));
    if (published)
    {
        *cfg = current;
    }
    else
    {
        arena_pop_to(output, output_mark);
    }
#if defined(BUSTER_TIER1_MEASURE_KINDS)
    if (trace)
    {
        fprintf(stderr, "CALL_WINDOW summary instructions=%u mode=%u supported=%u bounded=%u candidates=%u selected=%u published=%u conflicts=%u weighted=%u paths=%u work=%llu saving=%.12Lf preservation=%.12Lf\n",
                function->instruction_count, mode, ok, bounded, count, selected, published, conflicts, weighted, paths,
                (unsigned long long)work, (long double)total / (UINT64_C(1) << 32), (long double)saved / (UINT64_C(1) << 32));
    }
    tier1_emit_recolor_work_limit = previous_limit;
#else
    (void)weighted;
    (void)paths;
    (void)conflicts;
    (void)saved;
    (void)total;
#endif
    arena_pop_to(scratch, mark);
}
