// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Certify nonnegative instruction savings on every entry-to-terminal CFG path. Block gains contain the exact
// old-minus-new C/R and ABI-copy records; edge gains contain exact old-minus-new resolved copies. The caller must
// preserve every other emitted instruction count, including stub layout and indirect-dispatch fusion.
// A bounded frontier relaxation keeps integer path costs. It refuses a reachable negative cycle, arithmetic
// overflow, optional workspace exhaustion, or any terminal with a negative minimum saving. No profile is assumed.

typedef enum Tier1EmitRegionPathStatus
{
    TIER1_REGION_PATH_OK,
    TIER1_REGION_PATH_LOSS,
    TIER1_REGION_PATH_CYCLE,
    TIER1_REGION_PATH_LIMIT,
    TIER1_REGION_PATH_MEMORY,
    TIER1_REGION_PATH_OVERFLOW,
    TIER1_REGION_PATH_INVALID,
} Tier1EmitRegionPathStatus;

typedef struct Tier1EmitRegionPathStats Tier1EmitRegionPathStats;
struct Tier1EmitRegionPathStats
{
    uint64_t blocks_visited;
    uint64_t edges_visited;
    uint32_t rounds;
    uint32_t terminals;
    int64_t minimum_saving;
};

static bool tier1_emit_region_path_charge(uint64_t amount, uint64_t limit, uint64_t* work)
{
    const bool ok = *work <= limit && amount <= limit - *work;
    *work += ok ? amount : 0;
    return ok;
}

// Input is the emitter's validated, duplicate-free successor CSR; block zero is the function entry, including
// its synthetic entry when present. Unique targets in each row permit masked scatters; the queue stamp deduplicates
// targets reached from different frontier blocks. The temporary frontier is one block per round, and at most B
// rounds are needed without a negative cycle. The frontier order remains serial; each successor slice has eight lanes.
// Registers, allocations, gains and input topology remain untouched; scratch is restored on every result.
static Tier1EmitRegionPathStatus tier1_emit_region_path_gain(const Tier1CfgInput* input, const int64_t* block_gains,
                                                            const int64_t* edge_gains, Arena* scratch,
                                                            uint64_t limit, uint64_t* work, Tier1EmitRegionPathStats* stats)
{
    const uint64_t mark = scratch != 0 ? scratch->length : 0;
    bool valid = input != 0 && block_gains != 0 && scratch != 0 && work != 0 && input->block_count != 0 && input->block_count <= INT32_MAX &&
                 input->successor_offsets != 0 && (input->edge_count == 0 || (input->successors != 0 && edge_gains != 0));
    valid = valid && input->successor_offsets[0] == 0 && input->successor_offsets[input->block_count] == input->edge_count;
    Tier1EmitRegionPathStatus status = valid ? TIER1_REGION_PATH_OK : TIER1_REGION_PATH_INVALID;
    const uint32_t blocks = valid ? input->block_count : 0;
    bool ok = true;
    int64_t* distance = valid ? ir_allocate(scratch, (uint64_t)blocks + 1, sizeof(int64_t), &ok) : 0;
    uint32_t* current = valid && ok ? ir_allocate(scratch, (uint64_t)blocks + 1, 4, &ok) : 0;
    uint32_t* next = valid && ok ? ir_allocate(scratch, (uint64_t)blocks + 1, 4, &ok) : 0;
    uint32_t* queued = valid && ok ? ir_allocate(scratch, (uint64_t)blocks + 1, 4, &ok) : 0;
    status = valid && !ok ? TIER1_REGION_PATH_MEMORY : status;
    Tier1EmitRegionPathStats measured = { .minimum_saving = INT64_MAX };
    if (status == TIER1_REGION_PATH_OK)
    {
        status = tier1_emit_region_path_charge((uint64_t)blocks * 3, limit, work) ? status : TIER1_REGION_PATH_LIMIT;
    }
    for (uint32_t b = 0; status == TIER1_REGION_PATH_OK && b < blocks; b += 8)
    {
        const uint32_t left = blocks - b;
        const __mmask8 lanes = left >= 8 ? (__mmask8)0xff : (__mmask8)((1u << left) - 1);
        _mm512_mask_storeu_epi64(distance + b, lanes, _mm512_set1_epi64(INT64_MAX));
        _mm256_mask_storeu_epi32(queued + b, lanes, _mm256_set1_epi32(-1));
    }
    uint32_t count = 0;
    if (status == TIER1_REGION_PATH_OK)
    {
        distance[0] = 0;
        current[0] = 0;
        queued[0] = 0;
        count = 1;
    }
    // Processing a changed frontier is at least as strong as one Bellman-Ford edge-length round. Values updated
    // earlier in the same round may accelerate propagation. Without a negative cycle every shortest path is simple,
    // so a strict improvement in round B proves that a reachable cycle has negative gain.
    for (uint32_t round = 0; status == TIER1_REGION_PATH_OK && count != 0 && round < blocks; round += 1)
    {
        uint32_t next_count = 0;
        measured.rounds += 1;
        for (uint32_t q = 0; status == TIER1_REGION_PATH_OK && q < count; q += 1)
        {
            const uint32_t block = current[q];
            const uint32_t first = input->successor_offsets[block];
            const uint32_t past = input->successor_offsets[block + 1];
            status = first <= past && past <= input->edge_count ? status : TIER1_REGION_PATH_INVALID;
            if (status == TIER1_REGION_PATH_OK)
            {
                status = tier1_emit_region_path_charge((uint64_t)(past - first) + 1, limit, work) ? status : TIER1_REGION_PATH_LIMIT;
            }
            int64_t leaving = 0;
            if (status == TIER1_REGION_PATH_OK)
            {
                const bool overflow = __builtin_add_overflow(distance[block], block_gains[block], &leaving);
                status = overflow || leaving == INT64_MAX ? TIER1_REGION_PATH_OVERFLOW : status;
                measured.blocks_visited += 1;
            }
            for (uint32_t e = first; status == TIER1_REGION_PATH_OK && e < past; e += 8)
            {
                const uint32_t left = past - e;
                const __mmask8 lanes = left >= 8 ? (__mmask8)0xff : (__mmask8)((1u << left) - 1);
                const __m256i targets = _mm256_maskz_loadu_epi32(lanes, input->successors + e);
                const __m512i gains = _mm512_maskz_loadu_epi64(lanes, edge_gains + e);
                const __m512i from = _mm512_set1_epi64(leaving);
                const __m512i candidate = _mm512_add_epi64(from, gains);
                // A signed sum overflows exactly when its inputs have the same sign and the result changes it.
                const __m512i overflow_bits = _mm512_andnot_si512(_mm512_xor_si512(from, gains), _mm512_xor_si512(from, candidate));
                const __mmask8 overflow = _mm512_movepi64_mask(overflow_bits) |
                                         _mm512_cmpeq_epi64_mask(candidate, _mm512_set1_epi64(INT64_MAX));
                const __mmask8 valid_targets = _mm256_cmplt_epu32_mask(targets, _mm256_set1_epi32((int)blocks));
                status = (lanes & ~valid_targets) == 0 ? status : TIER1_REGION_PATH_INVALID;
                status = status == TIER1_REGION_PATH_OK && (lanes & overflow) != 0 ? TIER1_REGION_PATH_OVERFLOW : status;
                measured.edges_visited += left >= 8 ? 8 : left;
                if (status == TIER1_REGION_PATH_OK)
                {
                    const __m512i previous_distance = _mm512_mask_i32gather_epi64(_mm512_setzero_si512(), lanes, targets, distance, 8);
                    const __mmask8 improved = lanes & _mm512_cmplt_epi64_mask(candidate, previous_distance);
                    if (round + 1 == blocks && improved != 0)
                    {
                        status = TIER1_REGION_PATH_CYCLE;
                    }
                    else if (improved != 0)
                    {
                        const __m256i stamp = _mm256_set1_epi32((int)(round + 1));
                        const __m256i previous_stamp = _mm256_mmask_i32gather_epi32(_mm256_setzero_si256(), improved, targets, queued, 4);
                        const __mmask8 fresh = improved & _mm256_cmpneq_epi32_mask(previous_stamp, stamp);
                        _mm512_mask_i32scatter_epi64(distance, improved, targets, candidate, 8);
                        _mm256_mask_i32scatter_epi32(queued, fresh, targets, stamp, 4);
                        _mm256_mask_compressstoreu_epi32(next + next_count, fresh, targets);
                        next_count += (uint32_t)__builtin_popcount((uint32_t)fresh);
                    }
                }
            }
        }
        uint32_t* previous = current;
        current = next;
        next = previous;
        count = next_count;
    }
    for (uint32_t b = 0; status == TIER1_REGION_PATH_OK && b < blocks; b += 1)
    {
        const bool terminal = input->successor_offsets[b] == input->successor_offsets[b + 1];
        if (terminal && distance[b] != INT64_MAX)
        {
            int64_t saving = 0;
            const bool overflow = __builtin_add_overflow(distance[b], block_gains[b], &saving);
            status = overflow ? TIER1_REGION_PATH_OVERFLOW : saving < 0 ? TIER1_REGION_PATH_LOSS : status;
            measured.minimum_saving = saving < measured.minimum_saving ? saving : measured.minimum_saving;
            measured.terminals += 1;
        }
    }
    status = status == TIER1_REGION_PATH_OK && measured.terminals == 0 ? TIER1_REGION_PATH_INVALID : status;
    if (scratch != 0)
    {
        arena_pop_to(scratch, mark);
    }
    if (stats != 0)
    {
        *stats = measured;
    }
    return status;
}
