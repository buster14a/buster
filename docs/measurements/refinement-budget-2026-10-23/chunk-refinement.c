// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Diagnostic only: retain complete logical lines from a bounded lexer call, then restart its unfinished suffix.

#include <stdio.h>

#ifndef BUSTER_TEST_CHUNK_BYTES
#define BUSTER_TEST_CHUNK_BYTES 65536
#endif
static uint64_t refine_test_chunk_bytes = BUSTER_TEST_CHUNK_BYTES;

static bool refine_test_chunks(InternTable* table, const uint8_t* source, uint64_t length, const uint8_t* expected_kinds, const uint32_t* expected_starts,
                               const uint32_t* expected_ends, uint64_t expected_count, uint32_t* values, uint64_t* space, uint32_t flags)
{
    Scratch scratch = scratch_begin(0);
    const uint64_t capacity = BUSTER_C_TOKENIZER_CAPACITY(length);
    uint8_t* kinds = arena_push(scratch.arena, capacity, 64);
    uint32_t* starts = arena_push_array(scratch.arena, capacity, sizeof(uint32_t), 64);
    uint32_t* ends = arena_push_array(scratch.arena, capacity, sizeof(uint32_t), 64);
    RefineProgress progress = {
        .previous_ends = _mm512_setzero_si512(),
        .backslash_chunks = arena_push_array(scratch.arena, (length + 4095) / 4096 + 1, sizeof(uint64_t), 64),
    };
    bool result = kinds != 0 && starts != 0 && ends != 0 && progress.backslash_chunks != 0;
    uint64_t at = 0;
    uint64_t count = 0;
    uint64_t refined = 0;
    const bool scalar_only = c_tokenize_scalar_only(source, length, flags);
    uint64_t window = scalar_only || (flags & C_TOKENIZER_CPLUSPLUS) != 0 ? length : refine_test_chunk_bytes;
    bool finished = false;
    while (result && !finished)
    {
        const uint64_t bytes = length - at < window ? length - at : window;
        const int64_t lexed = scalar_only ? c_tokenize(source, length, kinds, starts, ends, flags)
                                         : refine_lex_prefix(source, at + bytes, at, kinds + count, starts + count, ends + count, flags);
        result = lexed >= 0;
        uint64_t keep = result ? (uint64_t)lexed : 0;
        finished = at + bytes == length;
        if (result && !finished)
        {
            // The last observed logical line can contain the token truncated by this call. Retokenize that line
            // against the next window; if there is no completed line, grow the window without publishing tokens.
            keep = 0;
            for (uint64_t i = (uint64_t)lexed; keep == 0 && i > 1; i -= 1)
            {
                if ((kinds[count + i - 1] & BUSTER_C_TOKEN_START_OF_LINE) != 0)
                {
                    keep = i - 1;
                }
            }
        }
        if (result && (keep != 0 || finished))
        {
            const uint64_t next = finished ? length : starts[count + keep];
            result = keep <= expected_count - count;
            for (uint64_t i = 0; result && i < keep; i += 1)
            {
                result = kinds[count + i] == expected_kinds[count + i] && starts[count + i] == expected_starts[count + i] && ends[count + i] == expected_ends[count + i];
                if (!result)
                {
                    printf("chunk lexer differs at token %lu, byte window %lu + %lu\n", count + i, at, bytes);
                }
            }
            count += keep;
            const uint64_t complete = finished ? count : count / BUSTER_REFINE_BLOCK_TOKENS * BUSTER_REFINE_BLOCK_TOKENS;
            result = result && refine_blocks(table, source, length, kinds, starts, ends, count, values, space, flags, refined, complete, &progress);
            refined = complete;
            at = next;
            window = refine_test_chunk_bytes;
        }
        else
        {
            window *= 2;
        }
    }
    result = result && count == expected_count;
    scratch_end(scratch);
    return result;
}
