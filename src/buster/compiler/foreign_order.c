// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Canonical foreign-name publication (#734). Include from foreign.c after its winner fold. Resolution retains its
// partition-local hash IDs; only this immutable output permutation gives them positions. The global symbol ordinal
// already packs (round, item): foreign_parse checks each round's cumulative end below UINT32_MAX before publishing
// it. Library-only names form a second range, never a bit stolen from that full-width ordinal.

// Sixteen low/high-half digits, including masked tails. Neither load forms a pointer beyond the readable range.
static __m512i foreign_order_digits(const uint64_t* keys, uint32_t count, uint32_t shift)
{
    const uint32_t first = count < 8 ? count : 8;
    const uint32_t second = count > 8 ? count - 8 : 0;
    const __m512i low = _mm512_maskz_loadu_epi64((__mmask8)_bzhi_u32(0xff, first), keys);
    const __m512i high = _mm512_maskz_loadu_epi64((__mmask8)_bzhi_u32(0xff, second), keys + first);
    const __m512i amount = _mm512_set1_epi64(shift);
    const __m256i a = _mm512_cvtepi64_epi32(_mm512_srlv_epi64(low, amount));
    const __m256i b = _mm512_cvtepi64_epi32(_mm512_srlv_epi64(high, amount));
    return _mm512_inserti64x4(_mm512_castsi256_si512(a), b, 1);
}

// Values at opaque u32 name IDs: i32 gather indices would sign-extend IDs with bit 31 set. Keep the zero-extended
// low half of each packed key in an i64 index, including the full legal ID range and masked tails.
static __m512i foreign_order_values(const uint64_t* keys, uint32_t count, const uint32_t* values)
{
    const uint32_t first = count < 8 ? count : 8;
    const uint32_t second = count > 8 ? count - 8 : 0;
    const __mmask8 a_mask = (__mmask8)_bzhi_u32(0xff, first);
    const __mmask8 b_mask = (__mmask8)_bzhi_u32(0xff, second);
    const __m512i ids = _mm512_set1_epi64(UINT32_MAX);
    const __m512i low = _mm512_and_si512(_mm512_maskz_loadu_epi64(a_mask, keys), ids);
    const __m512i high = _mm512_and_si512(_mm512_maskz_loadu_epi64(b_mask, keys + first), ids);
    const __m256i a = _mm512_mask_i64gather_epi32(_mm256_setzero_si256(), a_mask, low, values, 4);
    const __m256i b = _mm512_mask_i64gather_epi32(_mm256_setzero_si256(), b_mask, high, values, 4);
    return _mm512_inserti64x4(_mm512_castsi256_si512(a), b, 1);
}

// The high half is the complete checked first-record ordinal, the low half the opaque name ID. Stable byte radix
// count/place/fill, with the same conflict/rank scheme as this project's c_bindings_sort. The scalar twin takes all
// four digits; vector skips only digits proved constant while keys were packed. IDs never break an ordinal tie:
// distinct names cannot have the same first record. Both buffers own count entries and stay alive through copying.
static uint64_t* foreign_order_sort(uint64_t* keys, uint64_t* temporary, uint32_t count, ForeignPath path, uint32_t varying)
{
    uint64_t* input = keys;
    uint64_t* output = temporary;
    for (uint32_t pass = 0; count > 1 && pass < 4; pass += 1)
    {
        if (path == FOREIGN_PATH_EXACT || ((varying >> (pass * 8)) & 255) != 0)
        {
            uint32_t histogram[256] = { 0 };
            const uint32_t shift = 32 + pass * 8;
            for (uint32_t phase = 0; phase < 2; phase += 1)
            {
                if (phase == 1)
                {
                    uint32_t total = 0;
                    for (uint32_t bucket = 0; bucket < 256; bucket += 1)
                    {
                        const uint32_t frequency = histogram[bucket];
                        histogram[bucket] = total;
                        total += frequency;
                    }
                }
                if (path == FOREIGN_PATH_VECTOR)
                {
                    for (uint32_t begin = 0; begin < count;)
                    {
                        const uint32_t lanes = count - begin < 16 ? count - begin : 16;
                        const __mmask16 mask = (__mmask16)_bzhi_u32(0xffff, lanes);
                        const __m512i digits = _mm512_and_si512(foreign_order_digits(input + begin, lanes, shift), _mm512_set1_epi32(255));
                        const __m512i conflicts = _mm512_maskz_conflict_epi32(mask, digits);
                        const __m512i ranks = _mm512_popcnt_epi32(conflicts);
                        const uint32_t earlier = (uint32_t)_mm512_reduce_or_epi32(conflicts);
                        uint32_t buckets[16];
                        uint32_t offsets[16];
                        _mm512_storeu_si512(buckets, digits);
                        _mm512_storeu_si512(offsets, ranks);
                        if (phase == 1)
                        {
                            for (uint32_t lane = 0; lane < lanes; lane += 1)
                            {
                                output[histogram[buckets[lane]] + offsets[lane]] = input[begin + lane];
                            }
                        }
                        for (uint32_t last = (uint32_t)mask & ~earlier; last != 0; last &= last - 1)
                        {
                            const uint32_t lane = (uint32_t)__builtin_ctz(last);
                            histogram[buckets[lane]] += offsets[lane] + 1;
                        }
                        begin += lanes;
                    }
                }
                else
                {
                    for (uint32_t i = 0; i < count; i += 1)
                    {
                        const uint32_t bucket = (uint32_t)(input[i] >> shift) & 255;
                        if (phase == 0)
                        {
                            histogram[bucket] += 1;
                        }
                        else
                        {
                            output[histogram[bucket]++] = input[i];
                        }
                    }
                }
            }
            uint64_t* swap = input;
            input = output;
            output = swap;
        }
    }
    return input;
}

// Lane 0 after all winners join. Replace first-record keys by the ordered (ordinal, ID) pairs in the same persistent
// column. Temporary keys die before the next consumer, while the permutation lives with Foreign. No hash table,
// record, symbol-name map or lookup ID changes. Allocation failure rejects resolution before conversion is allowed.
static bool foreign_name_order(ForeignNames* names, Arena* arena, ForeignPath path)
{
    Scratch scratch = scratch_begin(arena);
    uint64_t* keys = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint64_t, (uint64_t)names->count + 1);
    uint64_t* temporary = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint64_t, (uint64_t)names->count + 1);
    bool result = keys != 0 && temporary != 0;
    uint32_t regular = 0;
    for (uint32_t id = 0; result && id < names->count; id += 1)
    {
        const uint64_t first = names->order[id];
        result = first != 0 && first < (2ull << 32) && (uint32_t)first != 0;
        regular += first >> 32 == 0 ? 1 : 0;
    }
    uint32_t cursor[2] = { 0, regular };
    uint32_t any[2] = { 0, 0 };
    uint32_t every[2] = { UINT32_MAX, UINT32_MAX };
    for (uint32_t id = 0; result && id < names->count; id += 1)
    {
        const uint64_t first = names->order[id];
        const uint32_t class = (uint32_t)(first >> 32);
        const uint32_t ordinal = (uint32_t)first - 1;
        keys[cursor[class]++] = ((uint64_t)ordinal << 32) | id;
        any[class] |= ordinal;
        every[class] &= ordinal;
    }
    for (uint32_t class = 0; result && class < 2; class += 1)
    {
        const uint32_t begin = class == 0 ? 0 : regular;
        const uint32_t count = class == 0 ? regular : names->count - regular;
        const uint64_t* sorted = foreign_order_sort(keys + begin, temporary + begin, count, path, any[class] ^ every[class]);
        memcpy(names->order + begin, sorted, (uint64_t)count * sizeof(uint64_t));
    }
    scratch_end(scratch);
    return result;
}
