// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Isolated #2610 regional role exchange. Include after the frozen recolor helpers. The original allocation is
// complete and valid, with the emitter's uniform plain GPR pool and heuristic preference masks. A parameter's
// single register interval changes r -> t; every displaced t interval must lie wholly inside that interval.
// Its t fragments change to r. No interval, fragment kind, slot or proof changes. Hard bindings remain binding.
//
// Result input/CFG objects publish together only on success. Statistics are diagnostics and are written on
// success or refusal; their failure_reason distinguishes an ineligible proposal from exhausted workspace.

typedef enum Tier1EmitRegionExchangeFailure
{
    TIER1_REGION_EXCHANGE_NONE,
    TIER1_REGION_EXCHANGE_ARGUMENT,
    TIER1_REGION_EXCHANGE_PARAMETER,
    TIER1_REGION_EXCHANGE_TARGET,
    TIER1_REGION_EXCHANGE_HARD_ROLE,
    TIER1_REGION_EXCHANGE_CONTAINMENT,
    TIER1_REGION_EXCHANGE_BINDING,
    TIER1_REGION_EXCHANGE_ENTRY,
    TIER1_REGION_EXCHANGE_MEMORY,
    TIER1_REGION_EXCHANGE_LIMIT,
    TIER1_REGION_EXCHANGE_EDGE,
} Tier1EmitRegionExchangeFailure;

typedef struct Tier1EmitRegionExchangeStats Tier1EmitRegionExchangeStats;
struct Tier1EmitRegionExchangeStats
{
    Tier1EmitRecolorStats edges;
    uint32_t displaced_values;
    uint32_t displaced_intervals;
    uint32_t displaced_fragments;
    Tier1EmitRegionExchangeFailure failure_reason;
};

// The eight recolor output columns, plus owned per-value and per-interval allowed masks. Bindings remain frozen.
static uint64_t tier1_emit_cfg_region_exchange_bytes(const Tier1CfgInput* input, const Tier1CfgAllocation* cfg, uint32_t moves)
{
    return tier1_emit_cfg_recolor_bytes(input, cfg, moves) + 4 * ((uint64_t)input->value_count + cfg->intervals.count + 2) + 2 * 63;
}

// Fresh output never aliases the frozen source or the scratch marks. Retain every old permission and add the
// old caller register only for the selected displaced values/intervals; physical definition bindings stay separate.
static void tier1_emit_region_masks(const uint32_t* source, const uint8_t* marked, uint32_t count, uint32_t bit, uint32_t* output)
{
    for (uint32_t i = 0; i < count; i += 16)
    {
        const uint32_t left = count - i;
        const __mmask16 lanes = left >= 16 ? 0xffff : (__mmask16)((1u << left) - 1);
        const __m512i old = _mm512_maskz_loadu_epi32(lanes, source + i);
        const __mmask16 changed = lanes & _mm_cmpneq_epi8_mask(_mm_maskz_loadu_epi8(lanes, marked + i), _mm_setzero_si128());
        _mm512_mask_storeu_epi32(output + i, lanes, _mm512_mask_or_epi32(old, changed, old, _mm512_set1_epi32((int)bit)));
    }
}

static uint8_t tier1_emit_region_uniform(const Tier1CfgAllocation* cfg, uint32_t value)
{
    const uint32_t begin = cfg->value_interval_offsets[value];
    const uint32_t past = cfg->value_interval_offsets[value + 1];
    const uint8_t reg = begin < past ? cfg->allocation.registers[cfg->allocation.fragment_offsets[begin]] : UINT8_MAX;
    bool uniform = reg < 16;
    for (uint32_t i = begin; uniform && i < past; i += 1)
    {
        const uint32_t limit = cfg->allocation.fragment_offsets[i + 1];
        for (uint32_t f = cfg->allocation.fragment_offsets[i]; uniform && f < limit; f += 64)
        {
            const uint32_t left = limit - f;
            const __mmask64 lanes = left >= 64 ? UINT64_MAX : (UINT64_C(1) << left) - 1;
            const __m512i kinds = _mm512_maskz_loadu_epi8(lanes, cfg->allocation.kinds + f);
            const __m512i registers = _mm512_maskz_loadu_epi8(lanes, cfg->allocation.registers + f);
            uniform = (lanes & (_mm512_cmpneq_epi8_mask(kinds, _mm512_set1_epi8(BUSTER_TIER1_ALLOCATE_REGISTER)) |
                                _mm512_cmpneq_epi8_mask(registers, _mm512_set1_epi8((char)reg)))) == 0;
        }
    }
    return uniform ? reg : UINT8_MAX;
}

static Tier1EmitError tier1_emit_cfg_region_exchange(const Tier1CfgInput* input, const Tier1CfgAllocation* original,
                                            uint32_t interval, uint8_t target, uint32_t callee_used,
                                            Arena* output, Arena* scratch, Tier1CfgInput* candidate_input, Tier1CfgAllocation* candidate, Tier1EmitRegionExchangeStats* stats)
{
    const uint64_t output_mark = output != 0 ? output->length : 0;
    const uint64_t scratch_mark = scratch != 0 ? scratch->length : 0;
    bool valid = input != 0 && original != 0 && output != 0 && scratch != 0 && output != scratch && candidate != 0 && candidate != original &&
                 candidate_input != 0 && candidate_input != input;
    valid = valid && interval < original->intervals.count && original->split_count == 0 && target < 16;
    const uint32_t value = valid ? original->intervals.values[interval] : 0;
    const uint32_t first = valid ? original->allocation.fragment_offsets[interval] : 0;
    const uint32_t start = valid ? original->intervals.starts[interval] : 0;
    const uint32_t end = valid ? original->intervals.ends[interval] : 0;
    const uint8_t source = valid ? original->allocation.registers[first] : UINT8_MAX;
    const uint32_t source_bit = source < 16 ? 1u << source : 0;
    const uint32_t target_bit = target < 16 ? 1u << target : 0;
    Tier1EmitRegionExchangeFailure reason = valid ? TIER1_REGION_EXCHANGE_PARAMETER : TIER1_REGION_EXCHANGE_ARGUMENT;
    valid = valid && value < input->value_count && input->classes[value] == BUSTER_TIER1_ALLOCATE_GPR &&
            input->definition_registers != 0 && input->definition_registers[value] == source_bit && start > input->definitions[value] &&
            (input->rematerializable == 0 || input->rematerializable[value] == 0) &&
            original->allocation.fragment_offsets[interval + 1] - first == 1 && original->allocation.kinds[first] == BUSTER_TIER1_ALLOCATE_REGISTER &&
            original->allocation.starts[first] == start && original->allocation.ends[first] == end && source < 16 && source != target &&
            ((BUSTER_TIER1_EMIT_CFG_CALLEE >> source) & 1) == 0;
    if (valid)
    {
        reason = TIER1_REGION_EXCHANGE_TARGET;
        valid = (target_bit & BUSTER_TIER1_EMIT_CFG_CALLEE & callee_used & input->allowed_registers[value] & original->intervals.allowed_registers[interval]) != 0;
    }
    const uint32_t exchanged = source_bit | target_bit;
    if (valid)
    {
        reason = TIER1_REGION_EXCHANGE_HARD_ROLE;
        valid = (exchanged & ~input->registers[BUSTER_TIER1_ALLOCATE_GPR]) == 0 &&
                (exchanged & ~original->intervals.registers[BUSTER_TIER1_ALLOCATE_GPR]) == 0 &&
                source != input->scratch_registers[BUSTER_TIER1_ALLOCATE_GPR] && target != input->scratch_registers[BUSTER_TIER1_ALLOCATE_GPR];
    }
    // Hard roles remain physical. The emitter's caller policy additionally excludes ROW and inline-assembly
    // bodies, whose complete physical contracts are not represented by these allocation columns.
    for (uint32_t f = 0; valid && f < input->fixed.count; f += 16)
    {
        const uint32_t left = input->fixed.count - f;
        const __mmask16 mask = left >= 16 ? 0xffff : (__mmask16)((1u << left) - 1);
        const __m128i classes = _mm_maskz_loadu_epi8(mask, input->fixed.classes + f);
        const __m512i registers = _mm512_maskz_loadu_epi32(mask, input->fixed.registers + f);
        const __mmask16 touched = _mm512_test_epi32_mask(registers, _mm512_set1_epi32((int)exchanged));
        valid = (mask & touched & _mm_cmpeq_epi8_mask(classes, _mm_set1_epi8(BUSTER_TIER1_ALLOCATE_GPR))) == 0;
    }
    for (uint32_t s = 0; valid && s < input->sequence_count; s += 16)
    {
        const uint32_t left = input->sequence_count - s;
        const __mmask16 mask = left >= 16 ? 0xffff : (__mmask16)((1u << left) - 1);
        const __m512i registers = _mm512_maskz_loadu_epi32(mask, input->sequence_registers + s);
        valid = (mask & _mm512_test_epi32_mask(registers, _mm512_set1_epi32((int)exchanged))) == 0;
    }
    Tier1EmitError error = valid ? TIER1_EMIT_OK : TIER1_EMIT_INVALID;
    Tier1CfgAllocation built = valid ? *original : (Tier1CfgAllocation){ 0 };
    Tier1CfgInput built_input = valid ? *input : (Tier1CfgInput){ 0 };
    Tier1EmitRegionExchangeStats measured = {
        .edges = {
            .value = value,
            .interval = interval,
            .source = source,
            .target = target,
        },
    };
    const uint32_t blocks = valid ? input->block_count : 0;
    const uint32_t edges = valid ? input->edge_count : 0;
    const uint32_t values = valid ? input->value_count : 0;
    bool ok = true;
    uint8_t* displaced_values = valid ? ir_allocate(scratch, (uint64_t)values + 1, 1, &ok) : 0;
    uint8_t* displaced_intervals = valid && ok ? ir_allocate(scratch, (uint64_t)original->intervals.count + 1, 1, &ok) : 0;
    error = error == TIER1_EMIT_OK && !ok ? TIER1_EMIT_MEMORY : error;
    bool used = false;
    // All sixteen lifetime/register lanes are checked together. A callee's pieces may be spilled or recomputed,
    // but every interval containing a displaced register fragment must be wholly inside the parameter's region.
    for (uint32_t i = 0; error == TIER1_EMIT_OK && i < original->intervals.count; i += 1)
    {
        if (original->intervals.classes[i] == BUSTER_TIER1_ALLOCATE_GPR)
        {
            const uint32_t limit = original->allocation.fragment_offsets[i + 1];
            uint32_t held_count = 0;
            bool overlaps = false;
            for (uint32_t f = original->allocation.fragment_offsets[i]; f < limit; f += 16)
            {
                const uint32_t left = limit - f;
                const __mmask16 mask = left >= 16 ? 0xffff : (__mmask16)((1u << left) - 1);
                const __m128i kinds = _mm_maskz_loadu_epi8(mask, original->allocation.kinds + f);
                const __m128i registers = _mm_maskz_loadu_epi8(mask, original->allocation.registers + f);
                const __mmask16 held = mask & _mm_cmpeq_epi8_mask(kinds, _mm_set1_epi8(BUSTER_TIER1_ALLOCATE_REGISTER)) & _mm_cmpeq_epi8_mask(registers, _mm_set1_epi8((char)target));
                const __m512i starts = _mm512_maskz_loadu_epi32(mask, original->allocation.starts + f);
                const __m512i ends = _mm512_maskz_loadu_epi32(mask, original->allocation.ends + f);
                const __mmask16 intersects = _mm512_cmplt_epu32_mask(starts, _mm512_set1_epi32((int)end)) & _mm512_cmpgt_epu32_mask(ends, _mm512_set1_epi32((int)start));
                held_count += (uint32_t)__builtin_popcount(held);
                overlaps |= (held & intersects) != 0;
            }
            used |= held_count != 0;
            if (overlaps)
            {
                const uint32_t displaced = original->intervals.values[i];
                const uint32_t begin = original->intervals.starts[i];
                const uint32_t past = original->intervals.ends[i];
                const bool contained = start <= begin && past <= end;
                const bool bound = input->definition_registers[displaced] != 0 && begin <= input->definitions[displaced] && input->definitions[displaced] < past;
                if (!contained || bound)
                {
                    reason = !contained ? TIER1_REGION_EXCHANGE_CONTAINMENT : TIER1_REGION_EXCHANGE_BINDING;
                    error = TIER1_EMIT_INVALID;
                }
                else
                {
                    measured.displaced_values += displaced_values[displaced] == 0;
                    measured.displaced_intervals += 1;
                    measured.displaced_fragments += held_count;
                    displaced_values[displaced] = 1;
                    displaced_intervals[i] = 1;
                }
            }
        }
    }
    if (error == TIER1_EMIT_OK && !used)
    {
        reason = TIER1_REGION_EXCHANGE_TARGET;
        error = TIER1_EMIT_INVALID;
    }
    uint32_t* entry_offsets = error == TIER1_EMIT_OK ? ir_allocate(scratch, (uint64_t)blocks + 1, 4, &ok) : 0;
    uint32_t* entry_cursors = error == TIER1_EMIT_OK && ok ? ir_allocate(scratch, (uint64_t)blocks + 1, 4, &ok) : 0;
    uint8_t* affected = error == TIER1_EMIT_OK && ok ? ir_allocate(scratch, (uint64_t)edges + 1, 1, &ok) : 0;
    uint32_t* entries = 0;
    uint32_t* ordinals = 0;
    error = error == TIER1_EMIT_OK && !ok ? TIER1_EMIT_MEMORY : error;
    if (error == TIER1_EMIT_OK)
    {
        memset(entry_offsets, 0, ((uint64_t)blocks + 1) * 4);
        ok = input->phi_count == 0 || tier1_cfg_edge_ordinals(input, scratch, &ordinals);
        error = ok ? error : TIER1_EMIT_MEMORY;
    }
    // An interval covering an entry is either a live-in or a value defined at that entry. The allocator
    // merges ranges only where every live-in was live out of the layout predecessor; it never bridges a liveness gap.
    for (uint32_t pass = 0; error == TIER1_EMIT_OK && pass < 2; pass += 1)
    {
        if (pass == 1)
        {
            for (uint32_t b = 0; b < blocks; b += 1)
            {
                entry_offsets[b + 1] += entry_offsets[b];
                entry_cursors[b] = entry_offsets[b];
            }
            entries = ir_allocate(scratch, (uint64_t)entry_offsets[blocks] + 1, 4, &ok);
            error = ok ? error : TIER1_EMIT_MEMORY;
        }
        for (uint32_t i = 0; error == TIER1_EMIT_OK && i < original->intervals.count; i += 1)
        {
            const uint32_t v = original->intervals.values[i];
            const uint32_t begin = original->intervals.starts[i];
            uint32_t b = tier1_cfg_block_at(input, original, begin);
            b += input->block_starts[b] < begin;
            for (; b < blocks && input->block_starts[b] < original->intervals.ends[i]; b += 1)
            {
                const bool defined = input->definitions[v] == input->block_starts[b];
                // Prepared whole intervals share each block's merge decision. Check the entry convention as well
                // for other validated producers: a continuing interval can enter in its register at entry - 1
                // before a REGISTER -> SPILL transition at entry, even though ordinary half-open occupancy is free.
                if (pass == 0 && i != interval && displaced_intervals[i] == 0 && input->classes[v] == BUSTER_TIER1_ALLOCATE_GPR && input->block_starts[b] == start)
                {
                    uint32_t location = BUSTER_TIER1_CFG_NONE;
                    uint8_t kind = UINT8_MAX;
                    const bool found = tier1_cfg_interval_entry(original, v, i, begin < start ? start - 1 : start, start, &location, &kind);
                    if (found && kind == BUSTER_TIER1_ALLOCATE_REGISTER && location == target)
                    {
                        reason = TIER1_REGION_EXCHANGE_ENTRY;
                        error = TIER1_EMIT_INVALID;
                    }
                }
                if (!defined && pass == 0)
                {
                    entry_offsets[b + 1] += 1;
                }
                else if (!defined)
                {
                    entries[entry_cursors[b]++] = v;
                }
            }
        }
    }
    uint64_t capacity = 0;
    uint32_t widest = 0;
    for (uint32_t b = 0; error == TIER1_EMIT_OK && b < blocks; b += 1)
    {
        const uint32_t exit = input->block_starts[b + 1] - 1;
        const bool source_changed = start <= exit && exit < end;
        for (uint32_t e = input->successor_offsets[b]; e < input->successor_offsets[b + 1]; e += 1)
        {
            const uint32_t target_block = input->successors[e];
            const uint32_t entry = input->block_starts[target_block];
            const bool target_changed = start <= entry && entry < end;
            const uint32_t count = entry_offsets[target_block + 1] - entry_offsets[target_block] + input->phi_offsets[target_block + 1] - input->phi_offsets[target_block];
            affected[e] = source_changed || target_changed;
            measured.edges.affected_edges += affected[e] != 0;
            capacity += affected[e] != 0 ? 4 * (uint64_t)count : original->edge_move_offsets[e + 1] - original->edge_move_offsets[e];
            widest = affected[e] != 0 && count > widest ? count : widest;
        }
    }
    error = error == TIER1_EMIT_OK && capacity > UINT32_MAX - 2 ? TIER1_EMIT_LIMIT : error;
    if (error == TIER1_EMIT_OK)
    {
        uint32_t* value_masks = ir_allocate(output, (uint64_t)values + 1, 4, &ok);
        uint32_t* interval_masks = ok ? ir_allocate(output, (uint64_t)original->intervals.count + 1, 4, &ok) : 0;
        built.allocation.registers = ok ? ir_allocate(output, (uint64_t)original->allocation.fragment_count + 1, 1, &ok) : 0;
        built.allocation.value_registers = ok ? ir_allocate(output, (uint64_t)values + 1, 1, &ok) : 0;
        built.edge_placements = ok ? ir_allocate(output, (uint64_t)edges + 1, 1, &ok) : 0;
        built.edge_move_offsets = ok ? ir_allocate(output, (uint64_t)edges + 1, 4, &ok) : 0;
        built.split_move_offsets = ok ? ir_allocate(output, 1, 4, &ok) : 0;
        error = ok ? error : TIER1_EMIT_MEMORY;
        if (ok)
        {
            tier1_emit_region_masks(input->allowed_registers, displaced_values, values, source_bit, value_masks);
            tier1_emit_region_masks(original->intervals.allowed_registers, displaced_intervals, original->intervals.count, source_bit, interval_masks);
            value_masks[values] = 0;
            interval_masks[original->intervals.count] = 0;
            built_input.allowed_registers = value_masks;
            built.intervals.allowed_registers = interval_masks;
            memcpy(built.allocation.registers, original->allocation.registers, original->allocation.fragment_count);
            memcpy(built.allocation.value_registers, original->allocation.value_registers, values);
            built.allocation.registers[first] = target;
            for (uint32_t i = 0; i < original->intervals.count; i += 1)
            {
                if (displaced_intervals[i] != 0)
                {
                    const uint32_t limit = original->allocation.fragment_offsets[i + 1];
                    for (uint32_t f = original->allocation.fragment_offsets[i]; f < limit; f += 64)
                    {
                        const uint32_t left = limit - f;
                        const __mmask64 lanes = left >= 64 ? UINT64_MAX : (UINT64_C(1) << left) - 1;
                        const __m512i kinds = _mm512_maskz_loadu_epi8(lanes, original->allocation.kinds + f);
                        const __m512i registers = _mm512_maskz_loadu_epi8(lanes, original->allocation.registers + f);
                        const __mmask64 changed = lanes & _mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(BUSTER_TIER1_ALLOCATE_REGISTER)) &
                                                 _mm512_cmpeq_epi8_mask(registers, _mm512_set1_epi8((char)target));
                        _mm512_mask_storeu_epi8(built.allocation.registers + f, lanes, _mm512_mask_set1_epi8(registers, changed, (char)source));
                    }
                }
            }
            for (uint32_t v = 0; v < values; v += 1)
            {
                if (v == value || displaced_values[v] != 0)
                {
                    built.allocation.value_registers[v] = tier1_emit_region_uniform(&built, v);
                }
            }
            built.allocation.registers[original->allocation.fragment_count] = UINT8_MAX;
            built.allocation.value_registers[values] = UINT8_MAX;
            built.split_edges = 0;
        }
    }
    const uint32_t spills = original != 0 ? original->allocation.spill_count : 0;
    const uint32_t locations = BUSTER_TIER1_CFG_SPILL + spills + BUSTER_TIER1_ALLOCATE_CLASSES;
    uint32_t* staged = error == TIER1_EMIT_OK ? ir_allocate(scratch, (capacity + 2) * 3, 4, &ok) : 0;
    uint32_t* sources = error == TIER1_EMIT_OK && ok ? ir_allocate(scratch, (uint64_t)widest + 1, 4, &ok) : 0;
    uint32_t* destinations = error == TIER1_EMIT_OK && ok ? ir_allocate(scratch, (uint64_t)widest + 1, 4, &ok) : 0;
    uint32_t* spill_ends = error == TIER1_EMIT_OK && ok && spills != 0 ? ir_allocate(scratch, (uint64_t)built.allocation.fragment_count + 1, 4, &ok) : 0;
    Tier1CfgEdge edge = { .spill_ends = spill_ends };
    edge.recompute = error == TIER1_EMIT_OK && ok ? ir_allocate(scratch, ((uint64_t)widest + 1) * 3, 4, &ok) : 0;
    edge.carried_at = error == TIER1_EMIT_OK && ok ? ir_allocate(scratch, locations, 4, &ok) : 0;
    edge.carried_stamps = error == TIER1_EMIT_OK && ok ? ir_allocate(scratch, locations, 4, &ok) : 0;
    for (uint32_t c = 0; error == TIER1_EMIT_OK && ok && c < BUSTER_TIER1_ALLOCATE_CLASSES; c += 1)
    {
        edge.pending[c] = ir_allocate(scratch, ((uint64_t)widest + 1) * 3, 4, &ok);
    }
    error = error == TIER1_EMIT_OK && !ok ? TIER1_EMIT_MEMORY : error;
    if (error == TIER1_EMIT_OK)
    {
        memset(edge.carried_stamps, 0, (uint64_t)locations * 4);
        if (spills != 0)
        {
            tier1_cfg_spill_prefix(&built, spill_ends);
        }
        // edge_stage deliberately folds the resolver's status into bool. Reserve its bounded temporary demand once
        // so an exhausted resolver is reported as MEMORY here, before any edge is staged. The wide resolver uses
        // 40 bytes per pending move, three short bitsets and at most seven 8-byte alignment gaps; the small one uses 16.
        const uint64_t resolver_mark = scratch->length;
        const uint64_t resolver_bytes = 48 * (uint64_t)widest + 128;
        const bool room = widest == 0 || arena_push(scratch, resolver_bytes, 64) != 0;
        arena_pop_to(scratch, resolver_mark);
        error = room ? error : TIER1_EMIT_MEMORY;
    }
    uint32_t count = 0;
    for (uint32_t b = 0; error == TIER1_EMIT_OK && b < blocks; b += 1)
    {
        const uint32_t exit = input->block_starts[b + 1] - 1;
        edge.block_start = input->block_starts[b];
        edge.current = original->current_slot_offsets != 0 ? original->current_slots : 0;
        edge.current_begin = original->current_slot_offsets != 0 ? original->current_slot_offsets[b] : 0;
        edge.current_end = original->current_slot_offsets != 0 ? original->current_slot_offsets[b + 1] : 0;
        for (uint32_t e = input->successor_offsets[b]; error == TIER1_EMIT_OK && e < input->successor_offsets[b + 1]; e += 1)
        {
            built.edge_move_offsets[e] = count;
            if (affected[e] == 0)
            {
                for (uint32_t m = original->edge_move_offsets[e]; m < original->edge_move_offsets[e + 1]; m += 1)
                {
                    staged[(uint64_t)count * 3] = original->move_sources[m];
                    staged[(uint64_t)count * 3 + 1] = original->move_destinations[m];
                    staged[(uint64_t)count * 3 + 2] = original->move_values[m];
                    count += 1;
                }
                built.edge_placements[e] = original->edge_placements[e];
            }
            else
            {
                const uint32_t target_block = input->successors[e];
                const uint32_t entry = input->block_starts[target_block];
                edge.stamp += 1;
                edge.recompute_count = 0;
                for (uint32_t c = 0; c < BUSTER_TIER1_ALLOCATE_CLASSES; c += 1)
                {
                    edge.pending_count[c] = 0;
                    edge.memory_pair[c] = false;
                }
                for (uint32_t x = entry_offsets[target_block]; ok && x < entry_offsets[target_block + 1]; x += 1)
                {
                    const uint32_t v = entries[x];
                    ok = tier1_cfg_edge_value(&built_input, &built, &edge, v, v, exit, entry);
                }
                const uint32_t ordinal = ordinals != 0 ? ordinals[e] : 0;
                for (uint32_t p = input->phi_offsets[target_block]; ok && p < input->phi_offsets[target_block + 1]; p += 1)
                {
                    const uint32_t operand = input->phi_operands[input->phi_operand_offsets[p] + ordinal];
                    ok = operand == BUSTER_TIER1_CFG_NONE || tier1_cfg_edge_value(&built_input, &built, &edge, input->phi_values[p], operand, exit, entry);
                }
                ok = ok && tier1_cfg_edge_stage(input, spills, &edge, scratch, sources, destinations, staged, capacity + 2, &count, &built.temp_slots);
                error = ok ? error : TIER1_EMIT_ALLOCATION;
                const bool moves = count != built.edge_move_offsets[e] || (input->edge_copies != 0 && input->edge_copies[e] != 0);
                const bool one_successor = input->successor_offsets[b + 1] - input->successor_offsets[b] == 1;
                const bool one_predecessor = input->predecessor_offsets[target_block + 1] - input->predecessor_offsets[target_block] == 1;
                built.edge_placements[e] = !moves ? TIER1_CFG_EDGE_EMPTY : one_successor ? TIER1_CFG_EDGE_PREDECESSOR : one_predecessor ? TIER1_CFG_EDGE_SUCCESSOR : TIER1_CFG_EDGE_SPLIT;
            }
            built.split_edges += built.edge_placements[e] == TIER1_CFG_EDGE_SPLIT;
            measured.edges.added_split_edges += built.edge_placements[e] == TIER1_CFG_EDGE_SPLIT && original->edge_placements[e] != TIER1_CFG_EDGE_SPLIT;
        }
    }
    if (error == TIER1_EMIT_OK)
    {
        built.edge_move_offsets[edges] = count;
        built.split_move_offsets[0] = count;
        built.move_count = count;
        built.move_sources = ir_allocate(output, (uint64_t)count + 1, 4, &ok);
        built.move_destinations = ok ? ir_allocate(output, (uint64_t)count + 1, 4, &ok) : 0;
        built.move_values = ok ? ir_allocate(output, (uint64_t)count + 1, 4, &ok) : 0;
        error = ok ? error : TIER1_EMIT_MEMORY;
    }
    for (uint32_t m = 0; error == TIER1_EMIT_OK && m < count; m += 1)
    {
        built.move_sources[m] = staged[(uint64_t)m * 3];
        built.move_destinations[m] = staged[(uint64_t)m * 3 + 1];
        built.move_values[m] = staged[(uint64_t)m * 3 + 2];
    }
    if (error == TIER1_EMIT_OK)
    {
        measured.edges.before_moves = original->move_count;
        measured.edges.after_moves = built.move_count;
        measured.edges.before_memory_moves = tier1_emit_recolor_memory_moves(original);
        measured.edges.after_memory_moves = tier1_emit_recolor_memory_moves(&built);
        measured.edges.before_split_edges = original->split_edges;
        measured.edges.after_split_edges = built.split_edges;
        measured.edges.added_temp_slots = built.temp_slots & ~original->temp_slots;
        built.retained_bytes += output->length - output_mark;
        *candidate_input = built_input;
        *candidate = built;
    }
    else if (output != 0)
    {
        arena_pop_to(output, output_mark);
    }
    if (scratch != 0)
    {
        arena_pop_to(scratch, scratch_mark);
    }
    measured.failure_reason = error == TIER1_EMIT_OK ? TIER1_REGION_EXCHANGE_NONE : error == TIER1_EMIT_MEMORY ? TIER1_REGION_EXCHANGE_MEMORY :
                              error == TIER1_EMIT_LIMIT ? TIER1_REGION_EXCHANGE_LIMIT : error == TIER1_EMIT_ALLOCATION ? TIER1_REGION_EXCHANGE_EDGE : reason;
    if (stats != 0)
    {
        *stats = measured;
    }
    return error;
}

