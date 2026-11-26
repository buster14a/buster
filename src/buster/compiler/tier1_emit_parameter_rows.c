// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Measurement-only parameter row retention. Local transfers come from the same exact block replay as regional
// scoring. The meet keeps a row only when every predecessor proves the same immutable entry parameter there.
// It adds no stores, moves or control flow and changes no owed/register/edge convention. The existing emitted-code
// certificate independently proves every published additional home from actual incoming records.

#define BUSTER_TIER1_PARAMETER_ROW_WORK (UINT64_C(128) << 20)

// The entry starts with unknown rows. Unvisited blocks assert nothing: the first real path establishes each
// block's state, and later paths can only widen a known owner to unknown. Thus a transparent loop retains a
// dominating store without seeding a circular fact. Each lane changes at most from unvisited to one owner to
// unknown. One 16-lane vector handles every row; a deduplicated queue visits successors only on a changed exit.
static Tier1EmitError tier1_emit_parameter_row_meet(const Tier1CfgInput* input, const uint32_t* generated,
                                                   const uint16_t* written, Arena* scratch, uint32_t* entries,
                                                   uint64_t limit, uint64_t* work)
{
    const uint64_t mark = scratch->length;
    const uint32_t blocks = input->block_count;
    const bool valid = blocks != 0 && input->predecessor_offsets != 0 && input->successor_offsets != 0 &&
                       (input->edge_count == 0 || (input->predecessors != 0 && input->successors != 0)) &&
                       input->predecessor_offsets[0] == 0 && input->successor_offsets[0] == 0 &&
                       input->predecessor_offsets[blocks] == input->edge_count && input->successor_offsets[blocks] == input->edge_count;
    Tier1EmitError error = valid ? TIER1_EMIT_OK : TIER1_EMIT_INVALID;
    uint32_t* exits = tier1_emit_epoch_cache_allocate(scratch, (uint64_t)blocks * 16, 4, &error);
    uint32_t* queue = tier1_emit_epoch_cache_allocate(scratch, blocks, 4, &error);
    uint8_t* queued = tier1_emit_epoch_cache_allocate(scratch, blocks, 1, &error);
    uint8_t* reached = tier1_emit_epoch_cache_allocate(scratch, blocks, 1, &error);
    error = error != TIER1_EMIT_OK || tier1_emit_epoch_cache_charge((uint64_t)blocks * 34 + 2, limit, work) ? error : TIER1_EMIT_LIMIT;
    if (error == TIER1_EMIT_OK)
    {
        tier1_emit_epoch_cache_fill(entries, (uint64_t)blocks * 16, BUSTER_IR_NONE);
        tier1_emit_epoch_cache_fill(exits, (uint64_t)blocks * 16, BUSTER_IR_NONE);
        memset(queued, 0, blocks);
        memset(reached, 0, blocks);
        queued[0] = 1;
        queue[0] = 0;
    }
    const __m512i unknown = _mm512_set1_epi32((int)BUSTER_IR_NONE);
    uint32_t head = 0;
    uint32_t tail = blocks == 1 ? 0 : 1;
    uint32_t count = 1;
    while (error == TIER1_EMIT_OK && count != 0)
    {
        const uint32_t b = queue[head];
        head = head + 1 == blocks ? 0 : head + 1;
        count -= 1;
        queued[b] = 0;
        const uint32_t first = input->predecessor_offsets[b];
        const uint32_t past = input->predecessor_offsets[b + 1];
        const uint32_t out_first = input->successor_offsets[b];
        const uint32_t out_past = input->successor_offsets[b + 1];
        error = first <= past && past <= input->edge_count && out_first <= out_past && out_past <= input->edge_count ? error : TIER1_EMIT_INVALID;
        error = error != TIER1_EMIT_OK || tier1_emit_epoch_cache_charge(16 * ((uint64_t)past - first + 3) + out_past - out_first + 1, limit, work) ? error : TIER1_EMIT_LIMIT;
        __m512i incoming = unknown;
        bool initialized = b == 0;
        for (uint32_t p = first; error == TIER1_EMIT_OK && b != 0 && p < past; p += 1)
        {
            const uint32_t predecessor = input->predecessors[p];
            error = predecessor < blocks ? error : TIER1_EMIT_INVALID;
            if (error == TIER1_EMIT_OK && reached[predecessor] != 0)
            {
                const __m512i previous = _mm512_loadu_si512(exits + (uint64_t)predecessor * 16);
                const __mmask16 same = _mm512_cmpeq_epi32_mask(incoming, previous);
                incoming = !initialized ? previous : _mm512_mask_mov_epi32(unknown, same, incoming);
                initialized = true;
            }
        }
        error = error == TIER1_EMIT_OK && !initialized ? TIER1_EMIT_INVALID : error;
        if (error == TIER1_EMIT_OK)
        {
            _mm512_storeu_si512(entries + (uint64_t)b * 16, incoming);
            const __m512i replacement = _mm512_loadu_si512(generated + (uint64_t)b * 16);
            const __m512i next = _mm512_mask_mov_epi32(incoming, (__mmask16)written[b], replacement);
            const __m512i previous = _mm512_loadu_si512(exits + (uint64_t)b * 16);
            const __mmask16 changed = _mm512_cmpneq_epi32_mask(previous, next);
            const __mmask16 known = _mm512_cmpneq_epi32_mask(next, unknown);
            // Once reached, a lane can lose precision only. An inconsistent transfer must never change a known
            // owner to another or revive a row already made unknown by a real path.
            error = reached[b] == 0 || (changed & known) == 0 ? error : TIER1_EMIT_INVALID;
            if (error == TIER1_EMIT_OK && (changed != 0 || reached[b] == 0))
            {
                reached[b] = 1;
                _mm512_storeu_si512(exits + (uint64_t)b * 16, next);
                for (uint32_t e = out_first; error == TIER1_EMIT_OK && e < out_past; e += 1)
                {
                    const uint32_t successor = input->successors[e];
                    error = successor < blocks ? error : TIER1_EMIT_INVALID;
                    if (error == TIER1_EMIT_OK && queued[successor] == 0)
                    {
                        error = count < blocks ? error : TIER1_EMIT_LIMIT;
                        if (error == TIER1_EMIT_OK)
                        {
                            queue[tail] = successor;
                            tail = tail + 1 == blocks ? 0 : tail + 1;
                            count += 1;
                            queued[successor] = 1;
                        }
                    }
                }
            }
        }
    }
    arena_pop_to(scratch, mark);
    return error;
}

static const uint32_t* tier1_emit_cfg_parameter_rows_limit(const Tier1SelectPlan* plan, const Tier1EmitResult* emitted,
                                                          Arena* scratch, uint64_t limit, uint64_t* measured)
{
    const uint64_t mark = scratch->length;
    const IrFunction* function = plan->function;
    const Tier1CfgInput* input = &emitted->cfg_input;
    const Tier1CfgAllocation* cfg = &emitted->cfg;
    const uint32_t blocks = input->block_count;
    const uint32_t nodes = function->instruction_count;
    const uint32_t columns = emitted->prepared.call_save_mask & ~emitted->callee_registers & 0xffffu;
    uint32_t parameters[BUSTER_ABI_INTEGER_ARGUMENTS];
    uint32_t parameter_count = 0;
    uint64_t work = 0;
    const bool supported = function->block_count != 0 && blocks >= function->block_count && columns != 0 && emitted->prepared.call_count != 0;
    Tier1EmitError error = supported ? TIER1_EMIT_OK : TIER1_EMIT_UNSUPPORTED;
    error = error != TIER1_EMIT_OK || tier1_emit_epoch_cache_charge(function->block_offsets[1], limit, &work) ? error : TIER1_EMIT_LIMIT;
    for (uint32_t node = 0; error == TIER1_EMIT_OK && node < function->block_offsets[1]; node += 1)
    {
        if (tier1_select_entry_parameter(function, node))
        {
            const Tier1SelectPlace place = tier1_select_parameter_place(function, function->a[node]);
            const uint32_t value = emitted->node_values[node];
            if (place.kind == BUSTER_TIER1_PLACE_GENERAL && value < input->value_count && input->classes[value] == BUSTER_TIER1_ALLOCATE_GPR)
            {
                error = parameter_count < BUSTER_ABI_INTEGER_ARGUMENTS ? error : TIER1_EMIT_UNSUPPORTED;
                if (error == TIER1_EMIT_OK)
                {
                    parameters[parameter_count++] = node;
                }
            }
        }
    }
    error = error == TIER1_EMIT_OK && parameter_count == 0 ? TIER1_EMIT_UNSUPPORTED : error;
    uint64_t entry_count = 0;
    for (uint32_t i = 0; error == TIER1_EMIT_OK && i < cfg->intervals.count; i += 1)
    {
        error = tier1_emit_epoch_cache_charge(1, limit, &work) ? error : TIER1_EMIT_LIMIT;
        const uint32_t start = cfg->intervals.starts[i];
        const uint32_t end = cfg->intervals.ends[i];
        error = error == TIER1_EMIT_OK && start >= end ? TIER1_EMIT_INVALID : error;
        if (error == TIER1_EMIT_OK)
        {
            const uint32_t first = tier1_cfg_block_at(input, cfg, start);
            const uint32_t last = tier1_cfg_block_at(input, cfg, end - 1);
            entry_count += (uint64_t)last - first + (input->block_starts[first] == start);
        }
    }
    uint32_t* entries = tier1_emit_epoch_cache_allocate(scratch, (uint64_t)blocks * 16, 4, &error);
    const uint64_t retained_mark = scratch->length;
    const uint64_t cache_bytes = error == TIER1_EMIT_OK ? tier1_emit_epoch_cache_bytes(plan, input, cfg, entry_count) : 0;
    const uint64_t scratch_bytes = error == TIER1_EMIT_OK ? tier1_emit_epoch_cache_scratch_bytes(plan, input, cfg) : 0;
    Arena metadata = { 0 };
    Arena temporary = { 0 };
    if (error == TIER1_EMIT_OK)
    {
        const bool room = tier1_emit_region_arena(scratch, cache_bytes, &metadata) && tier1_emit_region_arena(scratch, scratch_bytes, &temporary);
        error = room ? error : TIER1_EMIT_MEMORY;
    }
    Tier1EmitEpochCache cache = {
        .plan = plan,
        .emitted = emitted,
        .input = input,
        .shape = *cfg,
        .nodes = nodes,
        .blocks = blocks,
        .added = blocks - function->block_count,
        .base = 2 * (blocks - function->block_count),
    };
    error = error != TIER1_EMIT_OK ? error : tier1_emit_epoch_cache_events(&cache, &metadata, &temporary, limit, &work);
    error = error != TIER1_EMIT_OK ? error : tier1_emit_epoch_cache_preloads(&cache, &metadata, &temporary, limit, &work);
    uint32_t* generated = tier1_emit_epoch_cache_allocate(scratch, (uint64_t)blocks * 16, 4, &error);
    uint16_t* written = tier1_emit_epoch_cache_allocate(scratch, blocks, 2, &error);
    uint8_t* registers = tier1_emit_epoch_cache_allocate(scratch, (uint64_t)nodes + 1, 1, &error);
    error = error != TIER1_EMIT_OK || tier1_emit_epoch_cache_charge((uint64_t)blocks * (18 + parameter_count) + nodes, limit, &work) ? error : TIER1_EMIT_LIMIT;
    if (error == TIER1_EMIT_OK)
    {
        // Each local transfer starts with no carried row. The replay's persistent register-cell preloads remain exact.
        tier1_emit_epoch_cache_fill(cache.carry_parent, blocks, BUSTER_TIER1_CFG_NONE);
    }
    const __m512i unknown = _mm512_set1_epi32((int)BUSTER_IR_NONE);
    for (uint32_t b = 0; error == TIER1_EMIT_OK && b < blocks; b += 1)
    {
        uint32_t stores[16];
        uint32_t loads[16];
        uint32_t local[16];
        uint32_t touched = 0;
        error = tier1_emit_epoch_cache_block_rows(&cache, cfg, b, 0, registers, stores, loads, local, &touched, limit, &work);
        if (error == TIER1_EMIT_OK)
        {
            const __m512i owners = _mm512_loadu_si512(local);
            __mmask16 parameter = 0;
            for (uint32_t p = 0; p < parameter_count; p += 1)
            {
                parameter |= _mm512_cmpeq_epi32_mask(owners, _mm512_set1_epi32((int)parameters[p]));
            }
            written[b] = (uint16_t)(touched & columns);
            _mm512_storeu_si512(generated + (uint64_t)b * 16, _mm512_mask_mov_epi32(unknown, parameter & (__mmask16)columns, owners));
        }
    }
    error = error != TIER1_EMIT_OK ? error : tier1_emit_parameter_row_meet(input, generated, written, scratch, entries, limit, &work);
    const uint32_t* result = error == TIER1_EMIT_OK ? entries : 0;
    if (measured != 0)
    {
        *measured = work;
    }
#if defined(BUSTER_TIER1_MEASURE_KINDS)
    const char* trace_env = getenv("BUSTER_TIER1_EXCHANGE_TRACE");
    if (trace_env != 0 && (uint32_t)strtoul(trace_env, 0, 10) == nodes)
    {
        uint64_t known = 0;
        for (uint64_t r = 0; result != 0 && r < (uint64_t)blocks * 16; r += 16)
        {
            known += (uint32_t)__builtin_popcount(_mm512_cmpneq_epi32_mask(_mm512_loadu_si512(result + r), unknown));
        }
        fprintf(stderr, "PARAMETER_ROWS instructions=%u blocks=%u parameters=%u columns=%x error=%u known=%llu work=%llu\n",
                nodes, blocks, parameter_count, columns, error, (unsigned long long)known, (unsigned long long)work);
    }
#endif
    arena_pop_to(scratch, result != 0 ? retained_mark : mark);
    return result;
}

static const uint32_t* tier1_emit_cfg_parameter_rows(const Tier1SelectPlan* plan, const Tier1EmitResult* emitted, Arena* scratch)
{
    return tier1_emit_cfg_parameter_rows_limit(plan, emitted, scratch, BUSTER_TIER1_PARAMETER_ROW_WORK, 0);
}
