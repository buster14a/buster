// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Isolated #2610 selection policy. Keep the allocator's complete solution and recolor only a bound parameter's
// post-definition, single-register intervals. Each target is already saved by the function and free throughout
// the interval. No allocator hint or other value is changed. Exact edge, ABI and lazy-epoch costs decide acceptance.

#define BUSTER_TIER1_RECOLOR_WORK (UINT64_C(128) << 20)

#if defined(BUSTER_TIER1_MEASURE_KINDS)
#include <errno.h>

// Temporary measurement control only. Each regional search installs its requested finite allowance and restores
// the prior thread-local value, so ordinary recoloring and other threads retain the original 128 Mi allowance.
static _Thread_local uint64_t tier1_emit_recolor_work_limit = BUSTER_TIER1_RECOLOR_WORK;
#define BUSTER_TIER1_RECOLOR_ACTIVE_WORK tier1_emit_recolor_work_limit

static uint64_t tier1_emit_recolor_measure_limit(void)
{
    const char* text = getenv("BUSTER_TIER1_REGION_WORK");
    uint64_t limit = BUSTER_TIER1_RECOLOR_WORK;
    if (text != 0 && text[0] >= '0' && text[0] <= '9')
    {
        char* end = 0;
        const int saved_errno = errno;
        errno = 0;
        const unsigned long long parsed = strtoull(text, &end, 0);
        const bool valid = errno == 0 && end != text && *end == 0 && parsed != 0 && parsed <= (UINT64_C(1) << 40);
        limit = valid ? (uint64_t)parsed : limit;
        errno = saved_errno;
    }
    return limit;
}
#else
#define BUSTER_TIER1_RECOLOR_ACTIVE_WORK BUSTER_TIER1_RECOLOR_WORK
#endif

static bool tier1_emit_recolor_charge(uint64_t amount, uint64_t* work)
{
    const uint64_t limit = BUSTER_TIER1_RECOLOR_ACTIVE_WORK;
    const bool ok = *work <= limit && amount <= limit - *work;
    *work += ok ? amount : 0;
    return ok;
}

// Count the complete semantic entry state once, including phis. These frozen ranges never change during the
// search. A phi's range contributes exactly the entry that the transform handles in its separate phi loop.
static bool tier1_emit_recolor_entries(const Tier1CfgInput* input, const Tier1CfgAllocation* cfg, Arena* scratch,
                                      uint32_t** entries, uint64_t* total)
{
    const uint64_t mark = scratch->length;
    bool ok = true;
    uint32_t* counts = ir_allocate(scratch, (uint64_t)input->block_count + 1, 4, &ok);
    uint64_t sum = 0;
    for (uint32_t i = 0; ok && i < cfg->intervals.count; i += 1)
    {
        const uint32_t start = cfg->intervals.starts[i];
        uint32_t first = tier1_cfg_block_at(input, cfg, start);
        first += input->block_starts[first] < start;
        const uint32_t past = tier1_cfg_block_at(input, cfg, cfg->intervals.ends[i] - 1) + 1;
        sum += past - first;
        for (uint32_t b = first; b < past; b += 16)
        {
            const uint32_t left = past - b;
            const __mmask16 lanes = left >= 16 ? (__mmask16)0xffff : (__mmask16)((1u << left) - 1);
            const __m512i before = _mm512_maskz_loadu_epi32(lanes, counts + b);
            _mm512_mask_storeu_epi32(counts + b, lanes, _mm512_add_epi32(before, _mm512_set1_epi32(1)));
        }
    }
    if (ok)
    {
        *entries = counts;
        *total = sum;
    }
    else
    {
        arena_pop_to(scratch, mark);
    }
    return ok;
}

// Bound this transform's full scans and staged semantic-edge records before starting it. The pending-copy bound
// is the existing resolver's four records per entry value. Rejected occupancy trials consume this budget too.
static uint64_t tier1_emit_recolor_trial_work(const Tier1CfgInput* input, const Tier1CfgAllocation* cfg,
                                             const uint32_t* entries, uint64_t total_entries, uint32_t start, uint32_t end)
{
    uint64_t states = 0;
    for (uint32_t b = 0; b < input->block_count; b += 1)
    {
        const uint32_t exit = input->block_starts[b + 1] - 1;
        const bool source_changed = start <= exit && exit < end;
        for (uint32_t e = input->successor_offsets[b]; e < input->successor_offsets[b + 1]; e += 1)
        {
            const uint32_t target = input->successors[e];
            const uint32_t entry = input->block_starts[target];
            states += source_changed || (start <= entry && entry < end) ? entries[target] : 0;
        }
    }
    return (uint64_t)cfg->intervals.count * 3 + cfg->allocation.fragment_count * UINT64_C(2) + input->value_count * UINT64_C(2) +
           input->block_count * UINT64_C(6) + input->edge_count * UINT64_C(5) + total_entries * 2 + states * 12 +
           cfg->move_count * UINT64_C(3) + input->fixed.count + input->sequence_count;
}

// Ordinary instruction expansion can gain a copy when a dying parameter no longer shares its result register,
// or when its new register aliases a noncommutative result/address. At the only phase where such equality can
// change, decline either destination alias. Covered and folded operand identities use the emitter's value map.
static bool tier1_emit_recolor_endpoint(const Tier1SelectPlan* plan, const Tier1EmitResult* emitted,
                                       const Tier1CfgInput* input, const Tier1CfgAllocation* cfg,
                                       uint32_t interval, uint8_t source, uint8_t target)
{
    const uint32_t end = cfg->intervals.ends[interval];
    const uint32_t base = 2 * (input->block_count - plan->function->block_count);
    const uint32_t node = end > base && ((end - base) & 1) != 0 ? (end - base - 1) / 2 : UINT32_MAX;
    const uint32_t value = cfg->intervals.values[interval];
    bool safe = true;
    if (node < plan->function->instruction_count && !plan->covered[node])
    {
        const uint32_t defined = emitted->node_values[node];
        uint32_t destination = UINT32_MAX;
        uint8_t kind = UINT8_MAX;
        const bool general = defined != BUSTER_TIER1_CFG_NONE && input->classes[defined] == BUSTER_TIER1_ALLOCATE_GPR;
        const bool placed = general && tier1_cfg_location(cfg, defined, end, &destination, &kind) && kind == BUSTER_TIER1_ALLOCATE_REGISTER;
        if (placed && (destination == source || destination == target))
        {
            const Tier1SelectPattern* pattern = &plan->patterns[node];
            for (uint32_t k = 0; k < BUSTER_TIER1_SELECT_VALUES; k += 1)
            {
                const uint32_t operand = pattern->values[k];
                safe &= operand == BUSTER_IR_NONE || emitted->node_values[operand] != value;
            }
            for (uint32_t u = plan->effective_use_offsets[node]; u < plan->effective_use_offsets[node + 1]; u += 1)
            {
                safe &= emitted->node_values[plan->effective_use_values[u]] != value;
            }
        }
    }
    return safe;
}

static bool tier1_emit_recolor_crosses(const Tier1EmitRecolorModel* model, uint32_t start, uint32_t end)
{
    uint32_t first = 0;
    uint32_t past = model->count;
    while (first < past)
    {
        const uint32_t middle = first + (past - first) / 2;
        first = model->calls[middle].position <= start ? middle + 1 : first;
        past = model->calls[middle].position <= start ? past : middle;
    }
    return first < model->count && model->calls[first].position < end;
}

// One overlap scan finds every available callee before any edge reconstruction. Each transformation still checks
// its own full contract; this mask only avoids repeated work for registers that another frozen fragment occupies.
static uint32_t tier1_emit_recolor_free(const Tier1CfgAllocation* cfg, uint32_t start, uint32_t end, uint32_t choices)
{
    uint32_t occupied = 0;
    for (uint32_t i = 0; (choices & ~occupied) != 0 && i < cfg->intervals.count; i += 1)
    {
        if (cfg->intervals.classes[i] == BUSTER_TIER1_ALLOCATE_GPR)
        {
            const uint32_t past = cfg->allocation.fragment_offsets[i + 1];
            for (uint32_t f = cfg->allocation.fragment_offsets[i]; (choices & ~occupied) != 0 && f < past; f += 16)
            {
                const uint32_t left = past - f;
                const __mmask16 lanes = left >= 16 ? (__mmask16)0xffff : (__mmask16)((1u << left) - 1);
                const __m128i kinds = _mm_maskz_loadu_epi8(lanes, cfg->allocation.kinds + f);
                const __m512i starts = _mm512_maskz_loadu_epi32(lanes, cfg->allocation.starts + f);
                const __m512i ends = _mm512_maskz_loadu_epi32(lanes, cfg->allocation.ends + f);
                const __mmask16 held = lanes & _mm_cmpeq_epi8_mask(kinds, _mm_set1_epi8(BUSTER_TIER1_ALLOCATE_REGISTER));
                const __mmask16 overlap = held & _mm512_cmplt_epu32_mask(starts, _mm512_set1_epi32(end)) &
                                         _mm512_cmpgt_epu32_mask(ends, _mm512_set1_epi32(start));
                const __m512i registers = _mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(lanes, cfg->allocation.registers + f));
                const __m512i bits = _mm512_sllv_epi32(_mm512_set1_epi32(1), registers);
                occupied |= (uint32_t)_mm512_mask_reduce_or_epi32(overlap, bits);
            }
        }
    }
    return choices & ~occupied;
}

// The trial arenas own every mutable column; publish just the final columns once. Unchanged interval, fragment,
// slot and lookup columns still refer to the original allocation. Failure restores output and leaves result alone.
static bool tier1_emit_recolor_publish(const Tier1CfgInput* input, const Tier1CfgAllocation* original,
                                      const Tier1CfgAllocation* source, Arena* output, Tier1CfgAllocation* result)
{
    const uint64_t mark = output->length;
    Tier1CfgAllocation built = *source;
    const uint32_t fragments = source->allocation.fragment_count;
    const uint32_t values = input->value_count;
    const uint32_t edges = input->edge_count;
    const uint32_t moves = source->move_count;
    bool ok = true;
    built.allocation.registers = ir_allocate(output, (uint64_t)fragments + 1, 1, &ok);
    built.allocation.value_registers = ok ? ir_allocate(output, (uint64_t)values + 1, 1, &ok) : 0;
    built.edge_placements = ok ? ir_allocate(output, (uint64_t)edges + 1, 1, &ok) : 0;
    built.edge_move_offsets = ok ? ir_allocate(output, (uint64_t)edges + 1, 4, &ok) : 0;
    built.move_sources = ok ? ir_allocate(output, (uint64_t)moves + 1, 4, &ok) : 0;
    built.move_destinations = ok ? ir_allocate(output, (uint64_t)moves + 1, 4, &ok) : 0;
    built.move_values = ok ? ir_allocate(output, (uint64_t)moves + 1, 4, &ok) : 0;
    built.split_move_offsets = ok ? ir_allocate(output, 1, 4, &ok) : 0;
    if (ok)
    {
        memcpy(built.allocation.registers, source->allocation.registers, fragments);
        memcpy(built.allocation.value_registers, source->allocation.value_registers, values);
        memcpy(built.edge_placements, source->edge_placements, edges);
        memcpy(built.edge_move_offsets, source->edge_move_offsets, ((uint64_t)edges + 1) * 4);
        memcpy(built.move_sources, source->move_sources, (uint64_t)moves * 4);
        memcpy(built.move_destinations, source->move_destinations, (uint64_t)moves * 4);
        memcpy(built.move_values, source->move_values, (uint64_t)moves * 4);
        built.allocation.registers[fragments] = UINT8_MAX;
        built.allocation.value_registers[values] = UINT8_MAX;
        built.edge_placements[edges] = TIER1_CFG_EDGE_EMPTY;
        built.move_sources[moves] = 0;
        built.move_destinations[moves] = 0;
        built.move_values[moves] = 0;
        built.split_move_offsets[0] = moves;
        built.retained_bytes = original->retained_bytes + output->length - mark;
        *result = built;
    }
    else
    {
        arena_pop_to(output, mark);
    }
    return ok;
}

static Tier1EmitError tier1_emit_cfg_recolor_parameters(const Tier1SelectPlan* plan, const Tier1EmitResult* emitted,
                                                      Arena* output, Arena* scratch, const Tier1CfgInput* input, Tier1CfgAllocation* cfg)
{
    const uint64_t mark = scratch->length;
    const IrFunction* function = plan->function;
    const Tier1CfgAllocation original = *cfg;
    uint32_t parameters[BUSTER_ABI_INTEGER_ARGUMENTS];
    uint8_t incoming[BUSTER_ABI_INTEGER_ARGUMENTS];
    uint32_t parameter_count = 0;
    bool ok = function->block_count != 0 && cfg->split_count == 0 && input->definition_registers != 0;
    for (uint32_t node = 0; ok && node < function->block_offsets[1]; node += 1)
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
    const uint32_t used = tier1_emit_cfg_callee_used(cfg, callee);
    ok = ok && parameter_count != 0 && used != 0 && emitted->prepared.call_count != 0;
    Tier1EmitRecolorModel model = { 0 };
    ok = ok && tier1_emit_recolor_model(plan, emitted, input, scratch, &model);
    uint32_t* entries = 0;
    uint64_t entry_count = 0;
    ok = ok && tier1_emit_recolor_entries(input, cfg, scratch, &entries, &entry_count);
    Tier1EmitRecolorCost current_cost = { 0 };
    ok = ok && tier1_emit_recolor_cost(plan, emitted, input, cfg, &model, scratch, &current_cost);
    const Tier1EmitRecolorCost initial_cost = current_cost;
    // At most six values change a semantic edge. Four extra resolved records per changed value bound register
    // cycles conservatively. Storage/model refusal keeps the original allocation. A work-limit stop retains only
    // the last completely evaluated improving allocation, never the unfinished trial currently in the other arena.
    const uint64_t maximum_moves = (uint64_t)cfg->move_count + 4 * (uint64_t)parameter_count * input->edge_count;
    ok = ok && maximum_moves <= UINT32_MAX;
    const uint64_t bytes = ok ? tier1_emit_cfg_recolor_bytes(input, cfg, (uint32_t)maximum_moves) : 0;
    Arena trials[2] = { 0 };
    for (uint32_t t = 0; ok && t < 2; t += 1)
    {
        uint8_t* storage = ir_allocate(scratch, bytes, 1, &ok);
        trials[t] = (Arena){ .base = storage, .capacity = bytes };
    }
    Tier1CfgAllocation current = original;
    uint32_t held = 2;
    uint32_t selected = 0;
    uint32_t considered = 0;
    uint32_t valid_trials = 0;
    uint32_t endpoint_refusals = 0;
    uint32_t edge_refusals = 0;
    uint64_t work = 0;
    bool bounded = true;
#if defined(BUSTER_TIER1_MEASURE_KINDS)
    const char* trace_env = getenv("BUSTER_TIER1_EXCHANGE_TRACE");
    const bool trace = trace_env != 0 && (uint32_t)strtoul(trace_env, 0, 10) == function->instruction_count;
    if (trace && ok)
    {
        for (uint32_t k = 0; k < model.count; k += 1)
        {
            fprintf(stderr, "RECOLOR call instructions=%u index=%u node=%u position=%u block=%u\n", function->instruction_count, k, model.calls[k].node, model.calls[k].position, model.calls[k].block);
        }
    }
#endif
    for (uint32_t p = 0; ok && bounded && p < parameter_count; p += 1)
    {
        const uint32_t value = parameters[p];
        const uint8_t source = incoming[p];
        const uint32_t targets = used & input->allowed_registers[value] & ~model.forbidden;
        for (uint32_t i = cfg->value_interval_offsets[value]; ok && bounded && (model.forbidden & (1u << source)) == 0 && i < cfg->value_interval_offsets[value + 1]; i += 1)
        {
            const uint32_t first = current.allocation.fragment_offsets[i];
            const uint32_t start = current.intervals.starts[i];
            const uint32_t end = current.intervals.ends[i];
            const bool candidate_interval = targets != 0 && start > input->definitions[value] &&
                                            current.allocation.fragment_offsets[i + 1] - first == 1 &&
                                            current.allocation.kinds[first] == BUSTER_TIER1_ALLOCATE_REGISTER &&
                                            current.allocation.registers[first] == source && tier1_emit_recolor_crosses(&model, start, end);
            uint32_t chosen = 16;
            Tier1EmitRecolorCost best_cost = current_cost;
            bool path_checked = false;
            bool path = false;
            const uint32_t slot = held == 0 ? 1 : 0;
            bounded = !candidate_interval || tier1_emit_recolor_charge((uint64_t)current.intervals.count + current.allocation.fragment_count + input->block_count + input->edge_count, &work);
            const uint32_t free = candidate_interval && bounded ? tier1_emit_recolor_free(&current, start, end, targets & current.intervals.allowed_registers[i]) : 0;
            const uint64_t trial_work = free != 0 ? tier1_emit_recolor_trial_work(input, &current, entries, entry_count, start, end) : 0;
            for (uint32_t available = free; ok && bounded && available != 0; available &= available - 1)
            {
                const uint8_t target = (uint8_t)__builtin_ctz(available);
                const bool endpoint = tier1_emit_recolor_endpoint(plan, emitted, input, &current, i, source, target);
                endpoint_refusals += !endpoint;
                considered += endpoint;
                bounded = !endpoint || tier1_emit_recolor_charge(trial_work, &work);
                if (endpoint && bounded)
                {
                    arena_pop_to(&trials[slot], 0);
                    Tier1CfgAllocation next = { 0 };
                    Tier1EmitRecolorStats stats = { 0 };
                    const Tier1EmitError changed = tier1_emit_cfg_recolor(input, &current, i, target, used, &trials[slot], scratch, &next, &stats);
                    ok = changed == TIER1_EMIT_OK || changed == TIER1_EMIT_INVALID;
                    if (ok && changed == TIER1_EMIT_OK)
                    {
                        valid_trials += 1;
                        const bool traffic = tier1_emit_recolor_edge_traffic(input, &current, &next);
                        edge_refusals += !traffic;
                        if (traffic)
                        {
                            // Charge the complete epoch traversal, including its two frozen CSR construction
                            // passes and block-entry replay. These are structural visits, not CPU instructions.
                            const uint64_t charge = (uint64_t)current.intervals.count * 2 + current.allocation.fragment_count * UINT64_C(4) +
                                                    entry_count * 3 + input->position_count + (2 + BUSTER_TIER1_SELECT_VALUES) * (uint64_t)function->instruction_count +
                                                    (1 + BUSTER_TIER1_EMIT_LOCATIONS) * (uint64_t)input->block_count + input->use_count +
                                                    plan->effective_use_offsets[function->instruction_count] + model.count * UINT64_C(16) + input->edge_count;
                            bounded = tier1_emit_recolor_charge(charge, &work);
                            Tier1EmitRecolorCost cost = { 0 };
                            ok = !bounded || tier1_emit_recolor_cost(plan, emitted, input, &next, &model, scratch, &cost);
                            bool eligible = ok && bounded && tier1_emit_recolor_better(&cost, &current_cost);
                            const bool preferred = tier1_emit_recolor_total(&cost) < tier1_emit_recolor_total(&best_cost) ||
                                                   (tier1_emit_recolor_total(&cost) == tier1_emit_recolor_total(&best_cost) && cost.preservation < best_cost.preservation);
                            if (eligible && preferred && tier1_emit_recolor_total(&cost) == tier1_emit_recolor_total(&current_cost))
                            {
                                // A half-probability optional call can make an unconditional regional copy look like
                                // a tie. Prefer fewer memory records in a tie only when every admission must reach a
                                // call. This structural guard does not make the separate weighted score a profile.
                                if (!path_checked)
                                {
                                    bounded = tier1_emit_recolor_charge((uint64_t)input->block_count * 3 + input->edge_count * UINT64_C(2) + model.count, &work);
                                    path = bounded && tier1_emit_recolor_call_path(input, &model, start, end, scratch);
                                    path_checked = bounded;
                                }
                                eligible = bounded && path;
                            }
                            if (eligible && preferred)
                            {
                                chosen = target;
                                best_cost = cost;
                            }
                        }
                    }
                }
            }
            bounded = chosen >= 16 || !bounded ? bounded : tier1_emit_recolor_charge(trial_work, &work);
            if (ok && bounded && chosen < 16)
            {
                arena_pop_to(&trials[slot], 0);
                Tier1CfgAllocation next = { 0 };
                ok = tier1_emit_cfg_recolor(input, &current, i, (uint8_t)chosen, used, &trials[slot], scratch, &next, 0) == TIER1_EMIT_OK;
                if (ok)
                {
#if defined(BUSTER_TIER1_MEASURE_KINDS)
                    if (trace)
                    {
                        fprintf(stderr, "RECOLOR selected instructions=%u value=%u interval=%u start=%u end=%u source=%u target=%u saving=%.12Lf preservation=%.12Lf\n",
                                function->instruction_count, value, i, start, end, source, chosen,
                                (long double)(tier1_emit_recolor_total(&current_cost) - tier1_emit_recolor_total(&best_cost)) / (UINT64_C(1) << 32),
                                (long double)(current_cost.preservation - best_cost.preservation) / (UINT64_C(1) << 32));
                    }
#endif
                    current = next;
                    current_cost = best_cost;
                    held = slot;
                    selected += 1;
                }
            }
        }
    }
    bool published = false;
    if (ok && selected != 0)
    {
        published = tier1_emit_recolor_publish(input, &original, &current, output, cfg);
    }
#if defined(BUSTER_TIER1_MEASURE_KINDS)
    if (trace)
    {
        fprintf(stderr, "RECOLOR summary instructions=%u supported=%u bounded=%u selected=%u published=%u considered=%u trials=%u endpoint_refusals=%u edge_refusals=%u work=%llu saving=%.12Lf preservation=%.12Lf\n",
                function->instruction_count, ok, bounded, selected, published, considered, valid_trials, endpoint_refusals, edge_refusals, (unsigned long long)work,
                (long double)(tier1_emit_recolor_total(&initial_cost) - tier1_emit_recolor_total(&current_cost)) / (UINT64_C(1) << 32),
                (long double)(initial_cost.preservation - current_cost.preservation) / (UINT64_C(1) << 32));
    }
#else
    (void)initial_cost;
    (void)considered;
    (void)valid_trials;
    (void)endpoint_refusals;
    (void)edge_refusals;
    (void)published;
#endif
    arena_pop_to(scratch, mark);
    return TIER1_EMIT_OK;
}
