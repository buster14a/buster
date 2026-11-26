// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// A structural guard for equal estimated instruction costs. Every external admission must necessarily reach a
// strict-interior call before leaving the existing interval. The already validated CFG has matching, duplicate-free
// CSR views; model calls belong to their recorded blocks. Input/model storage is disjoint from scratch. No profile
// or branch probability participates. Include after tier1_emit_recolor_cost.c and before its selection policy.

static bool tier1_emit_recolor_call_path(const Tier1CfgInput* input, const Tier1EmitRecolorModel* model,
                                       uint32_t start, uint32_t end, Arena* scratch)
{
    const uint64_t mark = scratch != 0 ? scratch->length : 0;
    bool ok = input != 0 && model != 0 && scratch != 0;
    ok = ok && input->block_count != 0 && start < end && end <= input->position_count &&
         input->block_starts != 0 && input->successor_offsets != 0 && input->predecessor_offsets != 0 &&
         (input->edge_count == 0 || (input->successors != 0 && input->predecessors != 0)) &&
         (model->count == 0 || model->calls != 0);
    const uint32_t first = ok ? tier1_cfg_block_of(input, start) : 0;
    const uint32_t past = ok ? tier1_cfg_block_of(input, end - 1) + 1 : 0;
    ok = ok && input->block_starts[first] == start && past > first;
    const uint32_t blocks = past - first;
    uint32_t* remaining = ok ? ir_allocate(scratch, blocks, sizeof(uint32_t), &ok) : 0;
    uint32_t* queue = ok ? ir_allocate(scratch, blocks, sizeof(uint32_t), &ok) : 0;
    for (uint32_t b = 0; ok && b < blocks; b += 16)
    {
        const uint32_t left = blocks - b;
        const __mmask16 lanes = left >= 16 ? (__mmask16)0xffff : (__mmask16)((1u << left) - 1);
        _mm512_mask_storeu_epi32(remaining + b, lanes, _mm512_set1_epi32(-1));
    }
    for (uint32_t b = first; ok && b < past; b += 1)
    {
        const uint32_t from = input->successor_offsets[b];
        const uint32_t to = input->successor_offsets[b + 1];
        ok = from <= to && to <= input->edge_count &&
             input->predecessor_offsets[b] <= input->predecessor_offsets[b + 1] &&
             input->predecessor_offsets[b + 1] <= input->edge_count;
        // A terminal or partially covered block can leave the interval without traversing a CFG edge. It remains
        // unprovable unless its own interior call seeds it; so does any full block with a successor outside it.
        bool contained = ok && from != to && to - from != UINT32_MAX && input->block_starts[b + 1] <= end;
        for (uint32_t e = from; ok && e < to; e += 1)
        {
            const uint32_t target = input->successors[e];
            ok = target < input->block_count;
            contained &= target >= first && target < past;
        }
        if (ok && contained)
        {
            remaining[b - first] = to - from;
        }
    }
    uint32_t read = 0;
    uint32_t written = 0;
    for (uint32_t c = 0; ok && c < model->count; c += 1)
    {
        const Tier1EmitRecolorCall* call = &model->calls[c];
        ok = call->block < input->block_count && call->position >= input->block_starts[call->block] &&
             call->position < input->block_starts[call->block + 1];
        if (ok && start < call->position && call->position < end)
        {
            const uint32_t index = call->block - first;
            ok = call->block >= first && call->block < past;
            if (ok && remaining[index] != 0)
            {
                remaining[index] = 0;
                queue[written] = call->block;
                written += 1;
            }
        }
    }
    // Zero is both the proof bit and the queue mark. Each predecessor counter loses one per proven successor;
    // escapes retain UINT32_MAX, and a call-free cycle can never bootstrap its own least-fixed-point proof.
    while (ok && read < written)
    {
        const uint32_t block = queue[read];
        read += 1;
        const uint32_t to = input->predecessor_offsets[block + 1];
        for (uint32_t e = input->predecessor_offsets[block]; ok && e < to; e += 1)
        {
            const uint32_t source = input->predecessors[e];
            ok = source < input->block_count;
            if (ok && source >= first && source < past)
            {
                uint32_t* pending = &remaining[source - first];
                if (*pending != 0 && *pending != UINT32_MAX)
                {
                    *pending -= 1;
                    if (*pending == 0)
                    {
                        queue[written] = source;
                        written += 1;
                    }
                }
            }
        }
    }
    bool admitted = false;
    // Function entry is an additional admission when covered, though this optional policy also requires a real
    // external CFG edge. The policy normally excludes the definition-containing parameter interval altogether.
    ok = ok && (first != 0 || remaining[0] == 0);
    for (uint32_t b = first; ok && b < past; b += 1)
    {
        const uint32_t to = input->predecessor_offsets[b + 1];
        for (uint32_t e = input->predecessor_offsets[b]; ok && e < to; e += 1)
        {
            const uint32_t source = input->predecessors[e];
            ok = source < input->block_count;
            // A partially covered last block's outgoing edges occur after the interval has already ended.
            const bool external = ok && (source < first || source >= past || input->block_starts[source + 1] > end);
            admitted |= external;
            ok = ok && (!external || remaining[b - first] == 0);
        }
    }
    if (scratch != 0)
    {
        arena_pop_to(scratch, mark);
    }
    return ok && admitted;
}
