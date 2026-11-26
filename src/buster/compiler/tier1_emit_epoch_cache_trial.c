// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

static Tier1EmitError tier1_emit_epoch_cache_trial(const Tier1EmitEpochCache* cache, const Tier1CfgAllocation* cfg,
                                                  Arena* output, Arena* scratch, uint64_t limit, uint64_t* work,
                                                  Tier1EmitEpochTrial* result)
{
    const uint64_t output_mark = output != 0 ? output->length : 0;
    const uint64_t scratch_mark = scratch != 0 ? scratch->length : 0;
    bool valid = cache != 0 && cfg != 0 && output != 0 && scratch != 0 && result != 0 && work != 0 &&
                 output != scratch && output->base != scratch->base;
    valid = valid && cache->generation != 0 && tier1_emit_epoch_cache_shape(cache, cfg);
    Tier1EmitError error = valid ? TIER1_EMIT_OK : TIER1_EMIT_INVALID;
    const uint32_t fragments = valid ? cfg->allocation.fragment_count : 0;
    const uint32_t blocks = valid ? cache->blocks : 0;
    const uint64_t words = ((uint64_t)blocks + 63) / 64;
    const uint64_t rows = (uint64_t)blocks * 16;
    uint32_t* changed = tier1_emit_epoch_cache_allocate(scratch, (uint64_t)fragments + 1, 4, &error);
    uint64_t* pending = tier1_emit_epoch_cache_allocate(scratch, words, 8, &error);
    uint32_t* slots = tier1_emit_epoch_cache_allocate(scratch, (uint64_t)blocks + 1, 4, &error);
    uint8_t* registers = tier1_emit_epoch_cache_allocate(scratch, valid ? (uint64_t)cache->nodes + 1 : 1, 1, &error);
    Tier1EmitEpochTrial built = { .cache = cache, .generation = valid ? cache->generation : 0 };
    error = error != TIER1_EMIT_OK || tier1_emit_epoch_cache_charge(words + blocks + 1, limit, work) ? error : TIER1_EMIT_LIMIT;
    if (error == TIER1_EMIT_OK)
    {
        memset(pending, 0, words * 8);
        tier1_emit_epoch_cache_fill(slots, (uint64_t)blocks + 1, BUSTER_TIER1_CFG_NONE);
    }
    // Diff the entire register column rather than trusting a transform-specific value list. This includes displaced
    // intervals in a contained exchange, and the SIMD mask visits only changed fragments for dependency marking.
    for (uint64_t f = 0; error == TIER1_EMIT_OK && f < fragments; f += 64)
    {
        const uint64_t left = fragments - f;
        error = tier1_emit_epoch_cache_charge(left < 64 ? left : 64, limit, work) ? error : TIER1_EMIT_LIMIT;
        uint64_t differences = 0;
        if (error == TIER1_EMIT_OK)
        {
            const __mmask64 lanes = left >= 64 ? (__mmask64)UINT64_MAX : (__mmask64)((UINT64_C(1) << left) - 1);
            const __m512i before = _mm512_maskz_loadu_epi8(lanes, cache->registers + f);
            const __m512i after = _mm512_maskz_loadu_epi8(lanes, cfg->allocation.registers + f);
            differences = lanes & _mm512_cmpneq_epi8_mask(before, after);
        }
        while (error == TIER1_EMIT_OK && differences != 0)
        {
            const uint32_t fragment = (uint32_t)f + (uint32_t)__builtin_ctzll(differences);
            differences &= differences - 1;
            const uint8_t reg = cfg->allocation.registers[fragment];
            error = cfg->allocation.kinds[fragment] == BUSTER_TIER1_ALLOCATE_REGISTER && cache->fragment_bases[fragment] == 0 &&
                    tier1_emit_cfg_register(reg) ? error : TIER1_EMIT_UNSUPPORTED;
            if (error == TIER1_EMIT_OK)
            {
                changed[built.fragment_count] = fragment;
                built.fragment_count += 1;
                const uint32_t first = cache->dependency_offsets[fragment];
                const uint32_t past = cache->dependency_offsets[fragment + 1];
                error = tier1_emit_epoch_cache_charge(1 + (uint64_t)past - first, limit, work) ? error : TIER1_EMIT_LIMIT;
                for (uint32_t d = first; error == TIER1_EMIT_OK && d < past; d += 1)
                {
                    const uint32_t block = cache->dependency_blocks[d];
                    pending[block / 64] |= UINT64_C(1) << (block % 64);
                }
            }
        }
    }
    built.fragments = tier1_emit_epoch_cache_allocate(output, (uint64_t)built.fragment_count + 1, 4, &error);
    built.registers = tier1_emit_epoch_cache_allocate(output, (uint64_t)built.fragment_count + 1, 1, &error);
    built.blocks = tier1_emit_epoch_cache_allocate(output, (uint64_t)blocks + 1, 4, &error);
    built.stores = tier1_emit_epoch_cache_allocate(output, rows, 4, &error);
    built.loads = tier1_emit_epoch_cache_allocate(output, rows, 4, &error);
    built.exit_rows = tier1_emit_epoch_cache_allocate(output, rows, 4, &error);
    error = error != TIER1_EMIT_OK || tier1_emit_epoch_cache_charge(built.fragment_count, limit, work) ? error : TIER1_EMIT_LIMIT;
    for (uint32_t f = 0; error == TIER1_EMIT_OK && f < built.fragment_count; f += 1)
    {
        built.fragments[f] = changed[f];
        built.registers[f] = cfg->allocation.registers[changed[f]];
    }
    // Carry edges always point to a larger layout index. A marked bitset therefore processes every necessary block
    // once in ascending order, including directly changed children even when an earlier parent's rows converge.
    for (uint64_t word = 0; error == TIER1_EMIT_OK && word < words; word += 1)
    {
        error = tier1_emit_epoch_cache_charge(1, limit, work) ? error : TIER1_EMIT_LIMIT;
        while (error == TIER1_EMIT_OK && pending[word] != 0)
        {
            const uint32_t bit = (uint32_t)__builtin_ctzll(pending[word]);
            pending[word] &= pending[word] - 1;
            const uint32_t block = (uint32_t)(word * 64 + bit);
            const uint32_t parent = cache->carry_parent[block];
            const uint32_t parent_slot = parent != BUSTER_TIER1_CFG_NONE ? slots[parent] : BUSTER_TIER1_CFG_NONE;
            const uint32_t* inherited = parent == BUSTER_TIER1_CFG_NONE ? 0 : parent_slot != BUSTER_TIER1_CFG_NONE ?
                                        built.exit_rows + (uint64_t)parent_slot * 16 : cache->exit_rows + (uint64_t)parent * 16;
            const uint64_t row = (uint64_t)built.block_count * 16;
            error = tier1_emit_epoch_cache_block(cache, cfg, block, inherited, registers, built.stores + row,
                                                 built.loads + row, built.exit_rows + row, limit, work);
            if (error == TIER1_EMIT_OK)
            {
                built.blocks[built.block_count] = block;
                slots[block] = built.block_count;
                built.block_count += 1;
                error = tier1_emit_epoch_cache_charge(16, limit, work) ? error : TIER1_EMIT_LIMIT;
                bool changed_rows = false;
                if (error == TIER1_EMIT_OK)
                {
                    const __m512i previous = _mm512_loadu_si512(cache->exit_rows + (uint64_t)block * 16);
                    const __m512i next = _mm512_loadu_si512(built.exit_rows + row);
                    changed_rows = _mm512_cmpneq_epi32_mask(previous, next) != 0;
                }
                const uint32_t first = cache->child_offsets[block];
                const uint32_t past = cache->child_offsets[block + 1];
                error = error != TIER1_EMIT_OK || tier1_emit_epoch_cache_charge(changed_rows ? (uint64_t)past - first : 0, limit, work) ? error : TIER1_EMIT_LIMIT;
                for (uint32_t c = first; error == TIER1_EMIT_OK && changed_rows && c < past; c += 1)
                {
                    const uint32_t child = cache->children[c];
                    pending[child / 64] |= UINT64_C(1) << (child % 64);
                }
            }
        }
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

// Publication cannot allocate. Charge and validate every descriptor before the first accepted byte changes. The
// caller advances its accepted CFG only after this succeeds; a rejected candidate simply discards its trial arena.
static Tier1EmitError tier1_emit_epoch_cache_commit(Tier1EmitEpochCache* cache, const Tier1EmitEpochTrial* trial,
                                                   uint64_t limit, uint64_t* work)
{
    bool valid = cache != 0 && trial != 0 && work != 0 && trial->cache == cache;
    valid = valid && cache->generation != 0 && cache->generation == trial->generation && cache->generation != UINT64_MAX &&
            trial->fragment_count <= cache->shape.allocation.fragment_count && trial->block_count <= cache->blocks &&
            (trial->fragment_count == 0 || (trial->fragments != 0 && trial->registers != 0)) &&
            (trial->block_count == 0 || (trial->blocks != 0 && trial->stores != 0 && trial->loads != 0 && trial->exit_rows != 0));
    Tier1EmitError error = valid ? TIER1_EMIT_OK : TIER1_EMIT_INVALID;
    const uint64_t charge = valid ? 1 + 2 * (uint64_t)trial->fragment_count + 50 * (uint64_t)trial->block_count : 0;
    error = error != TIER1_EMIT_OK || tier1_emit_epoch_cache_charge(charge, limit, work) ? error : TIER1_EMIT_LIMIT;
    for (uint32_t f = 0; error == TIER1_EMIT_OK && f < trial->fragment_count; f += 1)
    {
        const uint32_t fragment = trial->fragments[f];
        error = fragment < cache->shape.allocation.fragment_count && (f == 0 || trial->fragments[f - 1] < fragment) &&
                cache->fragment_bases[fragment] == 0 && cache->shape.allocation.kinds[fragment] == BUSTER_TIER1_ALLOCATE_REGISTER &&
                tier1_emit_cfg_register(trial->registers[f]) ? error : TIER1_EMIT_INVALID;
    }
    for (uint32_t b = 0; error == TIER1_EMIT_OK && b < trial->block_count; b += 1)
    {
        error = trial->blocks[b] < cache->blocks && (b == 0 || trial->blocks[b - 1] < trial->blocks[b]) ? error : TIER1_EMIT_INVALID;
    }
    if (error == TIER1_EMIT_OK)
    {
        for (uint32_t f = 0; f < trial->fragment_count; f += 1)
        {
            cache->registers[trial->fragments[f]] = trial->registers[f];
        }
        for (uint32_t b = 0; b < trial->block_count; b += 1)
        {
            const uint64_t source = (uint64_t)b * 16;
            const uint64_t target = (uint64_t)trial->blocks[b] * 16;
            _mm512_storeu_si512(cache->stores + target, _mm512_loadu_si512(trial->stores + source));
            _mm512_storeu_si512(cache->loads + target, _mm512_loadu_si512(trial->loads + source));
            _mm512_storeu_si512(cache->exit_rows + target, _mm512_loadu_si512(trial->exit_rows + source));
        }
        cache->generation += 1;
    }
    return error;
}
