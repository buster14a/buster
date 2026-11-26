// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Isolated #2610 transformer, with no selection policy. Recolor one already allocated parameter interval into an
// otherwise unused, already preserved GPR. All other fragments and every slot/proof remain frozen. Reconstruct the
// affected edges from their semantic entry states, rather than editing an already ordered parallel move sequence.

typedef struct Tier1EmitRecolorStats Tier1EmitRecolorStats;
struct Tier1EmitRecolorStats
{
    uint32_t value;
    uint32_t interval;
    uint32_t affected_edges;
    uint32_t before_moves;
    uint32_t after_moves;
    uint32_t before_memory_moves;
    uint32_t after_memory_moves;
    uint32_t before_split_edges;
    uint32_t after_split_edges;
    uint32_t added_split_edges;
    uint8_t source;
    uint8_t target;
    uint8_t added_temp_slots;
};

static uint32_t tier1_emit_recolor_memory_moves(const Tier1CfgAllocation* cfg)
{
    uint32_t count = 0;
    const uint32_t moves = cfg->move_count;
    for (uint32_t m = 0; m < moves; m += 16)
    {
        const uint32_t left = moves - m;
        const __mmask16 mask = left >= 16 ? 0xffff : (__mmask16)((1u << left) - 1);
        const __m512i from = _mm512_maskz_loadu_epi32(mask, cfg->move_sources + m);
        const __m512i to = _mm512_maskz_loadu_epi32(mask, cfg->move_destinations + m);
        const __m512i spill = _mm512_set1_epi32(BUSTER_TIER1_CFG_SPILL);
        const __mmask16 stored = _mm512_cmpge_epu32_mask(to, spill);
        const __mmask16 loaded = _mm512_cmpge_epu32_mask(from, spill) & _mm512_cmpneq_epi32_mask(from, _mm512_set1_epi32(BUSTER_TIER1_CFG_REMATERIALIZE));
        count += (uint32_t)__builtin_popcount((stored | loaded) & mask);
    }
    return count;
}

// Eight 64-aligned output columns, including their sentinels. `moves` is the caller's bound for the final resolved
// edge moves, not for the semantic parallel moves. No output column points into scratch or a previous trial.
static uint64_t tier1_emit_cfg_recolor_bytes(const Tier1CfgInput* input, const Tier1CfgAllocation* cfg, uint32_t moves)
{
    return (uint64_t)cfg->allocation.fragment_count + input->value_count + 5 * (uint64_t)input->edge_count + 12 * (uint64_t)moves + 23 + 8 * 63;
}

// `original` and `input` are a validated complete emitter allocation, with no intrablock split moves. `callee_used`
// is the emitter's existing callee-save mask, never permission to use a register outside the actual pool or masks.
// Success publishes only fresh changed columns; failure restores both arenas and leaves candidate/stats unchanged.
// The caller separately prices C/R, ABI shuffles, edge stubs and temporary rows and decides whether to keep it.
static Tier1EmitError tier1_emit_cfg_recolor(const Tier1CfgInput* input, const Tier1CfgAllocation* original,
                                            uint32_t interval, uint8_t target, uint32_t callee_used,
                                            Arena* output, Arena* scratch, Tier1CfgAllocation* candidate, Tier1EmitRecolorStats* stats)
{
    const uint64_t output_mark = output != 0 ? output->length : 0;
    const uint64_t scratch_mark = scratch != 0 ? scratch->length : 0;
    bool valid = input != 0 && original != 0 && output != 0 && scratch != 0 && output != scratch && candidate != 0 && candidate != original;
    valid = valid && interval < original->intervals.count && original->split_count == 0 && target < 16;
    const uint32_t value = valid ? original->intervals.values[interval] : 0;
    const uint32_t first = valid ? original->allocation.fragment_offsets[interval] : 0;
    const uint32_t start = valid ? original->intervals.starts[interval] : 0;
    const uint32_t end = valid ? original->intervals.ends[interval] : 0;
    const uint8_t source = valid ? original->allocation.registers[first] : UINT8_MAX;
    const uint32_t target_bit = target < 16 ? 1u << target : 0;
    valid = valid && value < input->value_count && input->classes[value] == BUSTER_TIER1_ALLOCATE_GPR &&
            input->definition_registers != 0 && input->definition_registers[value] != 0 && start > input->definitions[value] &&
            original->allocation.fragment_offsets[interval + 1] - first == 1 && original->allocation.kinds[first] == BUSTER_TIER1_ALLOCATE_REGISTER &&
            original->allocation.starts[first] == start && original->allocation.ends[first] == end && source < 16 && source != target &&
            ((BUSTER_TIER1_EMIT_CFG_CALLEE >> source) & 1) == 0 &&
            (target_bit & BUSTER_TIER1_EMIT_CFG_CALLEE & callee_used & input->registers[BUSTER_TIER1_ALLOCATE_GPR] &
             original->intervals.registers[BUSTER_TIER1_ALLOCATE_GPR] & input->allowed_registers[value] & original->intervals.allowed_registers[interval]) != 0;
    bool used = false;
    // Fragments of each interval share a class. Check sixteen lifetimes/registers together; only the variable-length
    // interval boundary is serial. Masked byte and dword loads keep short ranges and tails inside the same path.
    for (uint32_t i = 0; valid && i < original->intervals.count; i += 1)
    {
        if (original->intervals.classes[i] == BUSTER_TIER1_ALLOCATE_GPR)
        {
            const uint32_t limit = original->allocation.fragment_offsets[i + 1];
            for (uint32_t f = original->allocation.fragment_offsets[i]; valid && f < limit; f += 16)
            {
                const uint32_t left = limit - f;
                const __mmask16 mask = left >= 16 ? 0xffff : (__mmask16)((1u << left) - 1);
                const __m128i kinds = _mm_maskz_loadu_epi8(mask, original->allocation.kinds + f);
                const __m128i registers = _mm_maskz_loadu_epi8(mask, original->allocation.registers + f);
                const __mmask16 held = mask & _mm_cmpeq_epi8_mask(kinds, _mm_set1_epi8(BUSTER_TIER1_ALLOCATE_REGISTER)) & _mm_cmpeq_epi8_mask(registers, _mm_set1_epi8((char)target));
                const __m512i starts = _mm512_maskz_loadu_epi32(mask, original->allocation.starts + f);
                const __m512i ends = _mm512_maskz_loadu_epi32(mask, original->allocation.ends + f);
                const __mmask16 overlaps = _mm512_cmplt_epu32_mask(starts, _mm512_set1_epi32((int)end)) & _mm512_cmpgt_epu32_mask(ends, _mm512_set1_epi32((int)start));
                used |= held != 0;
                valid = (held & overlaps) == 0;
            }
        }
    }
    valid = valid && used;
    for (uint32_t f = 0; valid && f < input->fixed.count; f += 16)
    {
        const uint32_t left = input->fixed.count - f;
        const __mmask16 mask = left >= 16 ? 0xffff : (__mmask16)((1u << left) - 1);
        const __m128i classes = _mm_maskz_loadu_epi8(mask, input->fixed.classes + f);
        const __m512i starts = _mm512_maskz_loadu_epi32(mask, input->fixed.starts + f);
        const __m512i ends = _mm512_maskz_loadu_epi32(mask, input->fixed.ends + f);
        const __m512i registers = _mm512_maskz_loadu_epi32(mask, input->fixed.registers + f);
        const __mmask16 overlaps = _mm512_cmplt_epu32_mask(starts, _mm512_set1_epi32((int)end)) & _mm512_cmpgt_epu32_mask(ends, _mm512_set1_epi32((int)start));
        const __mmask16 touched = _mm512_test_epi32_mask(registers, _mm512_set1_epi32((int)target_bit));
        valid = (mask & overlaps & touched & _mm_cmpeq_epi8_mask(classes, _mm_set1_epi8(BUSTER_TIER1_ALLOCATE_GPR))) == 0;
    }
    for (uint32_t s = 0; valid && s < input->sequence_count; s += 16)
    {
        const uint32_t left = input->sequence_count - s;
        const __mmask16 mask = left >= 16 ? 0xffff : (__mmask16)((1u << left) - 1);
        const __m512i positions = _mm512_maskz_loadu_epi32(mask, input->sequences + s);
        const __m512i registers = _mm512_maskz_loadu_epi32(mask, input->sequence_registers + s);
        const __mmask16 inside = _mm512_cmpge_epu32_mask(positions, _mm512_set1_epi32((int)start)) & _mm512_cmplt_epu32_mask(positions, _mm512_set1_epi32((int)end));
        valid = (mask & inside & _mm512_test_epi32_mask(registers, _mm512_set1_epi32((int)target_bit))) == 0;
    }
    Tier1EmitError error = valid ? TIER1_EMIT_OK : TIER1_EMIT_INVALID;
    Tier1CfgAllocation built = valid ? *original : (Tier1CfgAllocation){ 0 };
    Tier1EmitRecolorStats measured = {
        .value = value,
        .interval = interval,
        .source = source,
        .target = target,
    };
    const uint32_t blocks = valid ? input->block_count : 0;
    const uint32_t edges = valid ? input->edge_count : 0;
    const uint32_t values = valid ? input->value_count : 0;
    bool ok = true;
    uint32_t* entry_offsets = valid ? ir_allocate(scratch, (uint64_t)blocks + 1, 4, &ok) : 0;
    uint32_t* entry_cursors = valid && ok ? ir_allocate(scratch, (uint64_t)blocks + 1, 4, &ok) : 0;
    uint8_t* affected = valid && ok ? ir_allocate(scratch, (uint64_t)edges + 1, 1, &ok) : 0;
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
                if (pass == 0 && i != interval && input->classes[v] == BUSTER_TIER1_ALLOCATE_GPR && input->block_starts[b] == start)
                {
                    uint32_t location = BUSTER_TIER1_CFG_NONE;
                    uint8_t kind = UINT8_MAX;
                    const bool found = tier1_cfg_interval_entry(original, v, i, begin < start ? start - 1 : start, start, &location, &kind);
                    error = found && kind == BUSTER_TIER1_ALLOCATE_REGISTER && location == target ? TIER1_EMIT_INVALID : error;
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
            measured.affected_edges += affected[e] != 0;
            capacity += affected[e] != 0 ? 4 * (uint64_t)count : original->edge_move_offsets[e + 1] - original->edge_move_offsets[e];
            widest = affected[e] != 0 && count > widest ? count : widest;
        }
    }
    error = error == TIER1_EMIT_OK && capacity > UINT32_MAX - 2 ? TIER1_EMIT_LIMIT : error;
    if (error == TIER1_EMIT_OK)
    {
        built.allocation.registers = ir_allocate(output, (uint64_t)original->allocation.fragment_count + 1, 1, &ok);
        built.allocation.value_registers = ok ? ir_allocate(output, (uint64_t)values + 1, 1, &ok) : 0;
        built.edge_placements = ok ? ir_allocate(output, (uint64_t)edges + 1, 1, &ok) : 0;
        built.edge_move_offsets = ok ? ir_allocate(output, (uint64_t)edges + 1, 4, &ok) : 0;
        built.split_move_offsets = ok ? ir_allocate(output, 1, 4, &ok) : 0;
        error = ok ? error : TIER1_EMIT_MEMORY;
        if (ok)
        {
            memcpy(built.allocation.registers, original->allocation.registers, original->allocation.fragment_count);
            memcpy(built.allocation.value_registers, original->allocation.value_registers, values);
            built.allocation.registers[first] = target;
            // The untouched incoming definition and this post-definition interval necessarily differ now.
            built.allocation.value_registers[value] = UINT8_MAX;
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
                    ok = tier1_cfg_edge_value(input, &built, &edge, v, v, exit, entry);
                }
                const uint32_t ordinal = ordinals != 0 ? ordinals[e] : 0;
                for (uint32_t p = input->phi_offsets[target_block]; ok && p < input->phi_offsets[target_block + 1]; p += 1)
                {
                    const uint32_t operand = input->phi_operands[input->phi_operand_offsets[p] + ordinal];
                    ok = operand == BUSTER_TIER1_CFG_NONE || tier1_cfg_edge_value(input, &built, &edge, input->phi_values[p], operand, exit, entry);
                }
                ok = ok && tier1_cfg_edge_stage(input, spills, &edge, scratch, sources, destinations, staged, capacity + 2, &count, &built.temp_slots);
                error = ok ? error : TIER1_EMIT_ALLOCATION;
                const bool moves = count != built.edge_move_offsets[e] || (input->edge_copies != 0 && input->edge_copies[e] != 0);
                const bool one_successor = input->successor_offsets[b + 1] - input->successor_offsets[b] == 1;
                const bool one_predecessor = input->predecessor_offsets[target_block + 1] - input->predecessor_offsets[target_block] == 1;
                built.edge_placements[e] = !moves ? TIER1_CFG_EDGE_EMPTY : one_successor ? TIER1_CFG_EDGE_PREDECESSOR : one_predecessor ? TIER1_CFG_EDGE_SUCCESSOR : TIER1_CFG_EDGE_SPLIT;
            }
            built.split_edges += built.edge_placements[e] == TIER1_CFG_EDGE_SPLIT;
            measured.added_split_edges += built.edge_placements[e] == TIER1_CFG_EDGE_SPLIT && original->edge_placements[e] != TIER1_CFG_EDGE_SPLIT;
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
        measured.before_moves = original->move_count;
        measured.after_moves = built.move_count;
        measured.before_memory_moves = tier1_emit_recolor_memory_moves(original);
        measured.after_memory_moves = tier1_emit_recolor_memory_moves(&built);
        measured.before_split_edges = original->split_edges;
        measured.after_split_edges = built.split_edges;
        measured.added_temp_slots = built.temp_slots & ~original->temp_slots;
        built.retained_bytes += output->length - output_mark;
        *candidate = built;
        if (stats != 0)
        {
            *stats = measured;
        }
    }
    else if (output != 0)
    {
        arena_pop_to(output, output_mark);
    }
    if (scratch != 0)
    {
        arena_pop_to(scratch, scratch_mark);
    }
    return error;
}
