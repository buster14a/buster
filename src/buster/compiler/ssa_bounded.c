// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Bounded local promotion, included by ssa.c after its shared phi cleanup. Group accesses once, reuse one local's
// predecessor-reading workspace, and retain only its live, simplified phis. Original READs of other locals remain
// symbolic until all locals have been assembled; resolving them earlier makes cross-local cycles order-dependent.
// Storage is O(instructions + blocks + edges + locals + retained phis/operands), including provisional workspace.
// ssa_bounded_rename uses the dense constructor's segmented prefix operation on contiguous groups of actual accesses;
// its scalar twin has exactly the same access order. Neither path enumerates the blocks times locals product.

typedef struct SsaBoundedPart SsaBoundedPart;
struct SsaBoundedPart
{
    uint32_t local;
    uint32_t phi_count;
    uint32_t operand_count;
    uint32_t* blocks;
    uint32_t* offsets;
    uint32_t* operands;
};

typedef struct SsaBounded SsaBounded;
struct SsaBounded
{
    uint32_t* offsets;
    uint32_t* accesses;
    uint32_t* replacements;
    SsaBoundedPart* parts;
    uint32_t part_count;
    uint32_t phi_count;
    uint32_t operand_count;
    uint8_t* live;
    uint32_t* stack;
    uint32_t* mapping;
};

static SsaError ssa_bounded_accesses(const SsaWork* work, SsaBounded* bounded)
{
    const SsaInput* input = work->input;
    const uint32_t locals = input->local_count;
    const uint32_t instructions = input->instruction_count;
    const uint8_t* kinds = input->kinds;
    const uint32_t* instruction_locals = input->locals;
    const uint8_t* promoted = work->result.promoted;
    const uint8_t* reachable = work->result.reachable;
    const uint32_t* blocks = work->instruction_blocks;
    bounded->offsets = ssa_allocate(work->scratch, (uint64_t)locals + 1, sizeof(uint32_t));
    bounded->replacements = ssa_allocate(work->scratch, instructions, sizeof(uint32_t));
    uint32_t* cursors = ssa_allocate(work->scratch, locals, sizeof(uint32_t));
    SsaError error = bounded->offsets != 0 && bounded->replacements != 0 && cursors != 0 ? SSA_OK : SSA_ERROR_MEMORY;
    if (error == SSA_OK)
    {
        uint32_t* offsets = bounded->offsets;
        memset(offsets, 0, ((uint64_t)locals + 1) * sizeof(uint32_t));
        for (uint32_t i = 0; i < instructions; i += 1)
        {
            bounded->replacements[i] = i;
            const uint8_t kind = kinds[i];
            if ((kind == BUSTER_SSA_READ || kind == BUSTER_SSA_WRITE) && promoted[instruction_locals[i]] != 0)
            {
                if (reachable[blocks[i]] != 0)
                {
                    offsets[instruction_locals[i] + 1] += 1;
                }
                else if (kind == BUSTER_SSA_READ)
                {
                    bounded->replacements[i] = instructions + instruction_locals[i];
                }
            }
        }
        for (uint32_t local = 0; local < locals; local += 1)
        {
            offsets[local + 1] += offsets[local];
            cursors[local] = offsets[local];
        }
        const uint32_t accesses = offsets[locals];
        const uint32_t parts = accesses < locals ? accesses : locals;
        bounded->accesses = ssa_allocate(work->scratch, accesses, sizeof(uint32_t));
        bounded->parts = ssa_allocate(work->scratch, parts, sizeof(SsaBoundedPart));
        if (bounded->accesses == 0 || bounded->parts == 0)
        {
            error = SSA_ERROR_MEMORY;
        }
    }
    if (error == SSA_OK)
    {
        for (uint32_t i = 0; i < instructions; i += 1)
        {
            const uint8_t kind = kinds[i];
            if ((kind == BUSTER_SSA_READ || kind == BUSTER_SSA_WRITE) && promoted[instruction_locals[i]] != 0 && reachable[blocks[i]] != 0)
            {
                bounded->accesses[cursors[instruction_locals[i]]++] = i;
            }
        }
    }
    return error;
}

// A segment contains one local in one block, in original instruction order. Its first WRITE makes the entry value
// irrelevant; an exposed READ requests that value once. The prefix maximum selects the last preceding WRITE for
// every lane, and masked tails never load beyond the original columns or the local's access group.
static SsaError ssa_bounded_rename(SsaWork* work, const uint32_t* accesses, uint32_t begin, uint32_t end, SsaReadFrame* stack, bool vector)
{
    (void)vector;
    const SsaInput* input = work->input;
    const uint8_t* kinds = input->kinds;
    const uint32_t* operands = input->operands;
    const uint32_t* blocks = work->instruction_blocks;
    uint32_t* aliases = work->aliases;
    uint32_t first = begin;
    while (first < end && work->construction_error == SSA_OK)
    {
        const uint32_t block = blocks[accesses[first]];
        uint32_t limit = first + 1;
        while (limit < end && blocks[accesses[limit]] == block)
        {
            limit += 1;
        }
        uint32_t current = kinds[accesses[first]] == BUSTER_SSA_READ ? ssa_read_entry(work, block, 0, stack) : BUSTER_SSA_NONE;
#if BUSTER_HOST_VECTOR
        if (vector && work->construction_error == SSA_OK)
        {
            const __m512i positions = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
            _Alignas(64) uint32_t written[16];
            _Alignas(64) uint32_t resolved[16];
            while (first < limit)
            {
                const uint32_t count = limit - first < 16 ? limit - first : 16;
                const __mmask16 active = (__mmask16)((1u << count) - 1);
                for (uint32_t lane = 0; lane < count; lane += 1)
                {
                    const uint32_t i = accesses[first + lane];
                    written[lane] = kinds[i] == BUSTER_SSA_WRITE ? operands[i] : BUSTER_SSA_NONE;
                }
                const __m512i values = _mm512_maskz_loadu_epi32(active, written);
                const __mmask16 writes = _mm512_mask_cmpneq_epi32_mask(active, values, _mm512_set1_epi32(-1));
                __m512i last = _mm512_mask_mov_epi32(_mm512_set1_epi32(-1), writes, positions);
                for (uint32_t step = 1; step <= 8; step *= 2)
                {
                    const __m512i preceding = _mm512_permutexvar_epi32(_mm512_sub_epi32(positions, _mm512_set1_epi32(step)), last);
                    last = _mm512_mask_max_epi32(last, (__mmask16)(0xffffu << step), last, preceding);
                }
                const __mmask16 inside = _mm512_cmpge_epi32_mask(last, _mm512_setzero_si512());
                const __m512i reaching = _mm512_mask_blend_epi32(inside, _mm512_set1_epi32(current), _mm512_permutexvar_epi32(last, values));
                _mm512_store_si512(resolved, reaching);
                uint32_t reads = active & ~writes;
                while (reads != 0)
                {
                    const uint32_t lane = (uint32_t)__builtin_ctz(reads);
                    aliases[accesses[first + lane]] = resolved[lane];
                    reads &= reads - 1;
                }
                current = resolved[count - 1];
                first += count;
            }
        }
        else
#endif
        {
            while (first < limit && work->construction_error == SSA_OK)
            {
                const uint32_t i = accesses[first++];
                if (kinds[i] == BUSTER_SSA_WRITE)
                {
                    current = operands[i];
                }
                else
                {
                    aliases[i] = current;
                }
            }
        }
    }
    return work->construction_error;
}

static SsaError ssa_bounded_retain(SsaWork* work, SsaBounded* bounded, uint32_t local, uint32_t begin, uint32_t end)
{
    const uint32_t base = work->result.phi_base;
    const uint32_t count = work->phi_count;
    const uint32_t retained_phis = bounded->phi_count;
    const uint32_t retained_operands = bounded->operand_count;
    uint8_t* live = bounded->live;
    uint32_t* stack = bounded->stack;
    uint32_t* mapping = bounded->mapping;
    uint32_t* replacements = bounded->replacements;
    const uint8_t* kinds = work->input->kinds;
    const uint32_t* accesses = bounded->accesses;
    // Alias resolution changes only aliases. The completed local graph and its offsets remain fixed throughout
    // reachability, allocation and copying; the retained part uses separate, newly appended arena columns.
    const uint32_t* phi_blocks = work->phi_blocks;
    const uint32_t* phi_offsets = work->phi_offsets;
    const uint32_t* phi_operands = work->phi_operands;
    memset(live, 0, count);
    uint32_t depth = 0;
    for (uint32_t access = begin; access < end; access += 1)
    {
        const uint32_t i = accesses[access];
        if (kinds[i] == BUSTER_SSA_READ)
        {
            const uint32_t value = ssa_resolve(work, i);
            if (value >= base && live[value - base] == 0)
            {
                live[value - base] = 1;
                stack[depth++] = value - base;
            }
        }
    }
    while (depth != 0)
    {
        const uint32_t phi = stack[--depth];
        const uint32_t operand_end = phi_offsets[phi + 1];
        for (uint32_t operand = phi_offsets[phi]; operand < operand_end; operand += 1)
        {
            const uint32_t value = ssa_resolve(work, phi_operands[operand]);
            if (value >= base && live[value - base] == 0)
            {
                live[value - base] = 1;
                stack[depth++] = value - base;
            }
        }
    }
    uint32_t phis = 0;
    uint64_t operands = 0;
    for (uint32_t phi = 0; phi < count; phi += 1)
    {
        mapping[phi] = BUSTER_SSA_NONE;
        if (live[phi] != 0)
        {
            mapping[phi] = phis++;
            operands += phi_offsets[phi + 1] - phi_offsets[phi];
        }
    }
    SsaError error = (uint64_t)base + retained_phis + phis < BUSTER_SSA_NONE && (uint64_t)retained_operands + operands < BUSTER_SSA_NONE ? SSA_OK : SSA_ERROR_LIMIT;
    SsaBoundedPart part = {
        .local = local,
        .phi_count = phis,
        .operand_count = (uint32_t)operands,
    };
    if (error == SSA_OK && phis != 0)
    {
        part.blocks = ssa_allocate(work->scratch, phis, sizeof(uint32_t));
        part.offsets = ssa_allocate(work->scratch, (uint64_t)phis + 1, sizeof(uint32_t));
        part.operands = ssa_allocate(work->scratch, operands, sizeof(uint32_t));
        if (part.blocks == 0 || part.offsets == 0 || part.operands == 0)
        {
            error = SSA_ERROR_MEMORY;
        }
    }
    if (error == SSA_OK)
    {
        const uint32_t retained_base = base + retained_phis;
        uint32_t next_operand = 0;
        if (phis != 0)
        {
            part.offsets[0] = 0;
        }
        for (uint32_t phi = 0; phi < count; phi += 1)
        {
            if (live[phi] != 0)
            {
                const uint32_t mapped = mapping[phi];
                const uint32_t operand_end = phi_offsets[phi + 1];
                part.blocks[mapped] = phi_blocks[phi];
                for (uint32_t operand = phi_offsets[phi]; operand < operand_end; operand += 1)
                {
                    const uint32_t value = ssa_resolve(work, phi_operands[operand]);
                    part.operands[next_operand++] = value >= base ? retained_base + mapping[value - base] : value;
                }
                part.offsets[mapped + 1] = next_operand;
            }
        }
        for (uint32_t access = begin; access < end; access += 1)
        {
            const uint32_t i = accesses[access];
            if (kinds[i] == BUSTER_SSA_READ)
            {
                const uint32_t value = ssa_resolve(work, i);
                replacements[i] = value >= base ? retained_base + mapping[value - base] : value;
            }
        }
        if (phis != 0)
        {
            bounded->parts[bounded->part_count++] = part;
        }
        bounded->phi_count = retained_phis + phis;
        bounded->operand_count = retained_operands + (uint32_t)operands;
    }
    return error;
}

static SsaError ssa_bounded_assemble(SsaWork* work, const SsaBounded* bounded)
{
    const uint32_t phis = bounded->phi_count;
    const uint32_t operands = bounded->operand_count;
    const uint32_t base = work->result.phi_base;
    work->phi_blocks = ssa_allocate(work->scratch, phis, sizeof(uint32_t));
    work->phi_locals = ssa_allocate(work->scratch, phis, sizeof(uint32_t));
    work->phi_offsets = ssa_allocate(work->scratch, (uint64_t)phis + 1, sizeof(uint32_t));
    work->phi_operands = ssa_allocate(work->scratch, operands, sizeof(uint32_t));
    work->aliases = ssa_allocate(work->scratch, (uint64_t)base + phis, sizeof(uint32_t));
    SsaError error = work->phi_blocks != 0 && work->phi_locals != 0 && work->phi_offsets != 0 && work->phi_operands != 0 && work->aliases != 0 ? SSA_OK : SSA_ERROR_MEMORY;
    if (error == SSA_OK)
    {
        // Allocation is complete. These disjoint output columns and the retained-part inventory do not move or
        // grow during assembly; cache their bases after replacing the provisional workspace above.
        const uint32_t part_count = bounded->part_count;
        const SsaBoundedPart* parts = bounded->parts;
        const uint32_t* replacements = bounded->replacements;
        uint32_t* phi_blocks = work->phi_blocks;
        uint32_t* phi_locals = work->phi_locals;
        uint32_t* phi_offsets = work->phi_offsets;
        uint32_t* phi_operands = work->phi_operands;
        uint32_t* aliases = work->aliases;
        uint32_t next_phi = 0;
        uint32_t next_operand = 0;
        phi_offsets[0] = 0;
        for (uint32_t p = 0; p < part_count; p += 1)
        {
            const SsaBoundedPart part = parts[p];
            for (uint32_t phi = 0; phi < part.phi_count; phi += 1)
            {
                phi_blocks[next_phi] = part.blocks[phi];
                phi_locals[next_phi] = part.local;
                phi_offsets[++next_phi] = next_operand + part.offsets[phi + 1];
            }
            memcpy(phi_operands + next_operand, part.operands, (uint64_t)part.operand_count * sizeof(uint32_t));
            next_operand += part.operand_count;
        }
        const uint32_t instructions = work->input->instruction_count;
        for (uint32_t value = 0; value < instructions; value += 1)
        {
            aliases[value] = replacements[value];
        }
        for (uint64_t value = instructions; value < (uint64_t)base + phis; value += 1)
        {
            aliases[value] = (uint32_t)value;
        }
        work->phi_count = phis;
        work->operand_count = operands;
        work->phi_capacity = phis;
        work->operand_capacity = operands;
    }
    return error;
}

static SsaError ssa_construct_bounded(SsaWork* work, bool vector)
{
    const SsaInput* input = work->input;
    const uint32_t blocks = input->block_count;
    const uint32_t locals = input->local_count;
    const uint32_t base = work->result.phi_base;
    const uint8_t* kinds = input->kinds;
    const uint32_t* operands = input->operands;
    const uint32_t* instruction_blocks = work->instruction_blocks;
    SsaBounded bounded = { 0 };
    SsaError error = ssa_bounded_accesses(work, &bounded);
    SsaWork local_work = *work;
    local_work.promoted_count = 1;
    local_work.phi_capacity = blocks;
    local_work.operand_capacity = work->result.reachable_edge_count;
    SsaReadFrame* frames = 0;
    if (error == SSA_OK)
    {
        local_work.last = ssa_allocate(work->scratch, blocks, sizeof(uint32_t));
        local_work.entry = ssa_allocate(work->scratch, blocks, sizeof(uint32_t));
        local_work.phi_blocks = ssa_allocate(work->scratch, blocks, sizeof(uint32_t));
        local_work.phi_locals = ssa_allocate(work->scratch, blocks, sizeof(uint32_t));
        local_work.phi_offsets = ssa_allocate(work->scratch, (uint64_t)blocks + 1, sizeof(uint32_t));
        local_work.phi_operands = ssa_allocate(work->scratch, local_work.operand_capacity, sizeof(uint32_t));
        local_work.aliases = ssa_allocate(work->scratch, (uint64_t)base + blocks, sizeof(uint32_t));
        frames = ssa_allocate(work->scratch, blocks, sizeof(SsaReadFrame));
        bounded.live = ssa_allocate(work->scratch, blocks, sizeof(uint8_t));
        bounded.stack = ssa_allocate(work->scratch, blocks, sizeof(uint32_t));
        bounded.mapping = ssa_allocate(work->scratch, blocks, sizeof(uint32_t));
        if (local_work.last == 0 || local_work.entry == 0 || local_work.phi_blocks == 0 || local_work.phi_locals == 0 || local_work.phi_offsets == 0 || local_work.phi_operands == 0 || local_work.aliases == 0 || frames == 0 || bounded.live == 0 || bounded.stack == 0 || bounded.mapping == 0)
        {
            error = SSA_ERROR_MEMORY;
        }
    }
    // The per-local workspace is reused in place. The reader and cleanup change its contents and counts, but
    // never replace these columns; retained parts are appended beyond them and do not invalidate their bases.
    uint32_t* last = local_work.last;
    uint32_t* entry = local_work.entry;
    uint32_t* aliases = local_work.aliases;
    const uint32_t* phi_blocks = local_work.phi_blocks;
    const uint32_t* offsets = bounded.offsets;
    const uint32_t* accesses = bounded.accesses;
    if (error == SSA_OK)
    {
        memset(last, 0xff, (uint64_t)blocks * sizeof(uint32_t));
        memset(entry, 0xff, (uint64_t)blocks * sizeof(uint32_t));
        for (uint32_t value = 0; value < base; value += 1)
        {
            aliases[value] = value;
        }
    }
    for (uint32_t local = 0; error == SSA_OK && local < locals; local += 1)
    {
        const uint32_t begin = offsets[local];
        const uint32_t end = offsets[local + 1];
        if (begin != end)
        {
            local_work.local_ids = &local;
            local_work.phi_count = 0;
            local_work.operand_count = 0;
            for (uint32_t access = begin; access < end; access += 1)
            {
                const uint32_t i = accesses[access];
                if (kinds[i] == BUSTER_SSA_WRITE)
                {
                    last[instruction_blocks[i]] = operands[i];
                }
            }
            error = ssa_bounded_rename(&local_work, accesses, begin, end, frames, BUSTER_VECTOR_PATH(vector));
            if (error == SSA_OK)
            {
                const uint64_t mark = work->scratch->length;
                error = ssa_simplify(&local_work);
                arena_pop_to(work->scratch, mark);
            }
            if (error == SSA_OK)
            {
                error = ssa_bounded_retain(&local_work, &bounded, local, begin, end);
            }
            // Reset only this local's accesses and requested entries. Other original READs have always stayed
            // identity aliases, even for already processed locals; their saved answers live in replacements.
            for (uint32_t access = begin; access < end; access += 1)
            {
                const uint32_t i = accesses[access];
                last[instruction_blocks[i]] = BUSTER_SSA_NONE;
                aliases[i] = i;
            }
            const uint32_t phi_count = local_work.phi_count;
            for (uint32_t phi = 0; phi < phi_count; phi += 1)
            {
                entry[phi_blocks[phi]] = BUSTER_SSA_NONE;
            }
            if (blocks != 0)
            {
                entry[0] = BUSTER_SSA_NONE;
            }
        }
    }
    if (error == SSA_OK)
    {
        error = ssa_bounded_assemble(work, &bounded);
    }
    return error;
}
