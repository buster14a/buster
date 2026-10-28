// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Ordered warning-range updates. A compact interval index has one leaf per 64 tokens: its low byte summarizes
// clearable constant warnings, its high byte the suppression bits still missing. An update visits only intervals
// with pending work. Warning writes reopen their leaf's path; independently evaluated bounds are excluded. This
// preserves immediate clearing and later warning writes without repeatedly touching nested unevaluated subtrees.
// The index borrows reader scratch (under 1/8 byte per token, rounded to a power of two), is initialized only on
// the first long range, and owns no token/tree copy. Scalar and masked AVX-512 block updates have the same bounds.

#define BUSTER_CHECK_WARNING_BLOCK 64u
#define BUSTER_CHECK_WARNING_STACK 32u

// Count nonzero rows while the existing range/index producers change them. The prepared snapshot's collector can
// skip an empty column without another final scan; speculative restoration uses the same mutation primitive.
static void check_input_constant_warning_set(CheckInput* input, uint32_t token, uint8_t warnings)
{
    input->constant_warning_count = input->constant_warning_count - (input->constant_warnings[token] != 0) + (warnings != 0);
    input->constant_warnings[token] = warnings;
}

static void check_input_warning_changed(CheckInputReader* reader, uint32_t token, uint32_t warnings)
{
    if (reader->warning_index_ready && (uint8_t)warnings != 0)
    {
        for (uint32_t node = reader->warning_index_base + token / BUSTER_CHECK_WARNING_BLOCK; node != 0; node >>= 1)
        {
            reader->warning_index[node] |= (uint8_t)warnings;
        }
    }
}

static void check_input_warning_add(CheckInputReader* reader, uint32_t token, uint32_t warnings)
{
    if ((uint8_t)warnings != 0)
    {
        check_input_constant_warning_set(reader->output, token, reader->output->constant_warnings[token] | (uint8_t)warnings);
        check_input_warning_changed(reader, token, warnings);
    }
}

static uint16_t check_input_warning_block(CheckInputReader* reader, uint32_t block, uint32_t first, uint32_t end,
                                          uint8_t clear, uint8_t suppress)
{
    const uint32_t count = reader->parser->tokens.count;
    const uint32_t block_begin = block * BUSTER_CHECK_WARNING_BLOCK;
    const uint32_t block_end = count - block_begin < BUSTER_CHECK_WARNING_BLOCK ? count : block_begin + BUSTER_CHECK_WARNING_BLOCK;
    const uint32_t begin = first > block_begin ? first : block_begin;
    const uint32_t finish = end < block_end ? end : block_end;
    uint8_t* warnings = reader->output->constant_warnings;
    uint8_t* ignored = warnings != 0 ? reader->output->constant_warning_ignored : 0;
    uint8_t* suppressed = reader->warning_suppressed;
    uint16_t summary = 0;
    if (reader->parser->options.scalar)
    {
        for (uint32_t token = begin; token < finish; token += 1)
        {
            const bool independent = reader->constant_contexts != 0 && (reader->constant_contexts[token] & BUSTER_CHECK_CONSTANT_BOUND_CONTEXT) != 0;
            if (!independent)
            {
                if (token >= first && token < end)
                {
                    if (warnings != 0)
                    {
                        check_input_constant_warning_set(reader->output, token, warnings[token] & (uint8_t)~clear);
                    }
                    if (ignored != 0)
                    {
                        ignored[token] &= (uint8_t)~clear;
                    }
                    if (suppressed != 0)
                    {
                        suppressed[token] |= suppress;
                    }
                }
                summary |= warnings != 0 ? warnings[token] : 0;
                summary |= ignored != 0 ? ignored[token] : 0;
                summary |= suppressed != 0 ? (uint16_t)(uint8_t)~suppressed[token] << 8 : 0;
            }
        }
    }
    else
    {
        const __mmask64 present = finish - begin == BUSTER_CHECK_WARNING_BLOCK ? UINT64_MAX : (UINT64_C(1) << (finish - begin)) - 1;
        const __m512i zero = _mm512_setzero_si512();
        const __m512i contexts = reader->constant_contexts != 0 ? _mm512_maskz_loadu_epi8(present, reader->constant_contexts + begin) : zero;
        const __mmask64 active = present & _mm512_testn_epi8_mask(contexts, _mm512_set1_epi8(BUSTER_CHECK_CONSTANT_BOUND_CONTEXT));
        const uint32_t left = first > begin ? first - begin : 0;
        const uint32_t right = end < finish ? end - begin : finish - begin;
        const __mmask64 through = right == BUSTER_CHECK_WARNING_BLOCK ? UINT64_MAX : (UINT64_C(1) << right) - 1;
        const __mmask64 selected = active & through & (UINT64_MAX << left);
        const __m512i keep = _mm512_set1_epi8((char)~clear);
        __m512i pending = zero;
        if (warnings != 0)
        {
            __m512i bytes = _mm512_maskz_loadu_epi8(present, warnings + begin);
            const __mmask64 before = selected & _mm512_cmpneq_epi8_mask(bytes, zero);
            bytes = _mm512_mask_mov_epi8(bytes, selected, _mm512_and_si512(bytes, keep));
            const __mmask64 after = selected & _mm512_cmpneq_epi8_mask(bytes, zero);
            reader->output->constant_warning_count -= (uint32_t)_mm_popcnt_u64(before) - (uint32_t)_mm_popcnt_u64(after);
            _mm512_mask_storeu_epi8(warnings + begin, selected, bytes);
            pending = _mm512_or_si512(pending, bytes);
        }
        if (ignored != 0)
        {
            __m512i bytes = _mm512_maskz_loadu_epi8(present, ignored + begin);
            bytes = _mm512_mask_mov_epi8(bytes, selected, _mm512_and_si512(bytes, keep));
            _mm512_mask_storeu_epi8(ignored + begin, selected, bytes);
            pending = _mm512_or_si512(pending, bytes);
        }
        uint32_t low = (uint32_t)_mm512_reduce_or_epi32(_mm512_maskz_mov_epi8(active, pending));
        low |= low >> 16;
        low |= low >> 8;
        summary = (uint8_t)low;
        if (suppressed != 0)
        {
            __m512i bytes = _mm512_maskz_loadu_epi8(present, suppressed + begin);
            bytes = _mm512_mask_mov_epi8(bytes, selected, _mm512_or_si512(bytes, _mm512_set1_epi8((char)suppress)));
            _mm512_mask_storeu_epi8(suppressed + begin, selected, bytes);
            uint32_t high = (uint32_t)_mm512_reduce_or_epi32(_mm512_maskz_mov_epi8(active, _mm512_xor_si512(bytes, _mm512_set1_epi8(-1))));
            high |= high >> 16;
            high |= high >> 8;
            summary |= (uint16_t)(uint8_t)high << 8;
        }
    }
    // A boundary block keeps a conservative summary for its untouched bytes. Besides avoiding unrelated loads,
    // this lets deferred lanes update disjoint body ranges sharing a block without reading each other's bytes.
    return begin == block_begin && finish == block_end ? summary : UINT16_MAX;
}

typedef struct CheckInputWarningInterval CheckInputWarningInterval;
struct CheckInputWarningInterval
{
    uint32_t node;
    uint32_t first;
    uint32_t end;
};

static void check_input_warning_range(CheckInputReader* reader, uint32_t first, uint32_t end, uint32_t clear, uint32_t suppress)
{
    if (first < end && end <= reader->parser->tokens.count)
    {
        if (!reader->warning_index_ready && end - first >= BUSTER_CHECK_WARNING_BLOCK)
        {
            // Initially conservative: no byte columns are read until a requested interval reaches their block.
            memset(reader->warning_index, 0xff, (uint64_t)reader->warning_index_base * 2 * sizeof(uint16_t));
            reader->warning_index_ready = true;
        }
        if (!reader->warning_index_ready)
        {
            for (uint32_t token = first; token < end; token += 1)
            {
                if (reader->constant_contexts == 0 || (reader->constant_contexts[token] & BUSTER_CHECK_CONSTANT_BOUND_CONTEXT) == 0)
                {
                    if (reader->output->constant_warnings != 0)
                    {
                        check_input_constant_warning_set(reader->output, token, reader->output->constant_warnings[token] & (uint8_t)~clear);
                        if (reader->output->constant_warning_ignored != 0)
                        {
                            reader->output->constant_warning_ignored[token] &= (uint8_t)~clear;
                        }
                    }
                    if (reader->warning_suppressed != 0)
                    {
                        reader->warning_suppressed[token] |= (uint8_t)suppress;
                    }
                }
            }
        }
        else
        {
            // count < 1 << 30 bounds the interval depth below 32, including pending right siblings.
            CheckInputWarningInterval stack[BUSTER_CHECK_WARNING_STACK];
            stack[0] = (CheckInputWarningInterval)
            {
                .node = 1,
                .first = 0,
                .end = reader->warning_index_base * BUSTER_CHECK_WARNING_BLOCK
            };
            uint32_t depth = 1;
            const uint16_t wanted = (uint16_t)((uint8_t)clear | ((uint16_t)(uint8_t)suppress << 8));
            while (depth != 0)
            {
                const CheckInputWarningInterval interval = stack[--depth];
                if (interval.first < end && first < interval.end && (reader->warning_index[interval.node] & wanted) != 0)
                {
                    if (interval.node < reader->warning_index_base)
                    {
                        const uint32_t middle = interval.first + (interval.end - interval.first) / 2;
                        stack[depth++] = (CheckInputWarningInterval)
                        {
                            .node = interval.node * 2 + 1,
                            .first = middle,
                            .end = interval.end
                        };
                        stack[depth++] = (CheckInputWarningInterval)
                        {
                            .node = interval.node * 2,
                            .first = interval.first,
                            .end = middle
                        };
                    }
                    else
                    {
                        reader->warning_index[interval.node] = check_input_warning_block(reader, interval.node - reader->warning_index_base, first, end,
                                                                                       (uint8_t)clear, (uint8_t)suppress);
                        for (uint32_t node = interval.node >> 1; node != 0; node >>= 1)
                        {
                            reader->warning_index[node] = reader->warning_index[node * 2] | reader->warning_index[node * 2 + 1];
                        }
                    }
                }
            }
        }
    }
}
