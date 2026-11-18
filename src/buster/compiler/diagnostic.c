// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Buffered diagnostics for lane jobs. A job has a source-order unit/index before it is scheduled and exactly one
// writer. Each lane owns a DiagnosticBuffer and its text arena. Append copies a complete rendered diagnostic group
// (primary plus notes), so neither argument lifetimes nor sorting can detach expansion notes from their primary.
// No worker prints or cancels other jobs. After the phase joins, diagnostic_merge sorts by (unit, location, job,
// sequence) and retains the prefix through the first fatal group. Earlier jobs must finish even if a later one fails.
// Locations are canonical source positions, never file-cache/intern IDs, presumed #line numbers or lane indices.
//
// The two packed 64-bit keys retain all four u32 fields. A stable byte radix sort counts and places 16 keys at a time;
// conflicts give equal digits their ranks within a block. Scalar stores avoid Zen 5's expensive gather/scatter.
// At most BUSTER_DIAGNOSTIC_SMALL_COUNT records, diagnostic_sort_small counts each record's place 8 keys at a time
// instead (#2959). All buffers are bounded; any failed
// append or merge rejects the whole result before printing. The caller can then report its resource failure deterministically.
//
// Include after lane.c and String8. Layout: records and buffers; append; sorting; merge and write.

#include "host_isa.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#define BUSTER_DIAGNOSTIC_SMALL_COUNT 512
#define BUSTER_DIAGNOSTIC_BATCH 16
#define BUSTER_DIAGNOSTIC_BUCKETS 256
// The small path's keys a compare takes: eight 64-bit lanes.
#define BUSTER_DIAGNOSTIC_KEYS 8

typedef enum DiagnosticSeverity
{
    DIAGNOSTIC_NOTE,
    DIAGNOSTIC_WARNING,
    DIAGNOSTIC_ERROR,
    DIAGNOSTIC_FATAL,
} DiagnosticSeverity;

typedef struct Diagnostic Diagnostic;
struct Diagnostic
{
    uint64_t source;
    uint64_t sequence;
    String8 text;
    DiagnosticSeverity severity;
};

typedef struct DiagnosticOrder DiagnosticOrder;
struct DiagnosticOrder
{
    uint64_t source;
    uint64_t sequence;
    uint32_t record;
};

typedef struct DiagnosticJob DiagnosticJob;
struct DiagnosticJob
{
    uint32_t unit;
    uint32_t index;
    uint32_t next;
    bool stopped;
};

typedef struct DiagnosticBuffer DiagnosticBuffer;
struct DiagnosticBuffer
{
    Diagnostic* records;
    Arena* text;
    uint32_t count;
    uint32_t capacity;
    bool failed;
};

typedef struct Diagnostics Diagnostics;
struct Diagnostics
{
    Diagnostic* records;
    DiagnosticOrder* order;
    uint32_t count;
    bool success;
};

static bool diagnostic_append(DiagnosticBuffer* buffer, DiagnosticJob* job, uint32_t location, DiagnosticSeverity severity, String8 text)
{
    bool result = !buffer->failed && !job->stopped;
    if (result)
    {
        result = buffer->count < buffer->capacity && job->next < UINT32_MAX && (uint32_t)severity <= DIAGNOSTIC_FATAL &&
                 (text.length == 0 || text.pointer != 0);
        char* copy = result && text.length != 0 ? arena_push(buffer->text, text.length, 1) : 0;
        result = result && (text.length == 0 || copy != 0);
        if (result)
        {
            if (text.length != 0)
            {
                memcpy(copy, text.pointer, text.length);
            }
            buffer->records[buffer->count] = (Diagnostic)
            {
                .source = ((uint64_t)job->unit << 32) | location,
                .sequence = ((uint64_t)job->index << 32) | job->next,
                .text =
                {
                    .pointer = copy,
                    .length = text.length,
                },
                .severity = severity,
            };
            buffer->count += 1;
            job->next += 1;
            job->stopped = severity == DIAGNOSTIC_FATAL;
        }
        buffer->failed |= !result;
    }
    return result;
}

// diagnostic_sort's path for BUSTER_DIAGNOSTIC_SMALL_COUNT records or fewer, which the radix sort's 16 passes cost more
// than (#2959): each record's place is the number of records before it in the order (a smaller (source, sequence), or
// the same keys and a smaller index, which keeps the sort stable), counted 8 records at a time from columns of the
// keys; each record then goes to its place in `scratch`, and the whole back to `items`. O(count^2 / 8) compares,
// branch-free. The blocks before a record's own count equal keys (<= on the sequence), the blocks after it don't (<),
// and in its own block the lanes below it do. The last block's lanes past `count` hold the largest keys and come
// after every record, so they never count. It replaced an insertion sort whose speed moved with where the compiler
// put its loop (#2959).
static void diagnostic_sort_small(DiagnosticOrder* items, uint32_t count, DiagnosticOrder* scratch)
{
    _Alignas(64) uint64_t sources[BUSTER_DIAGNOSTIC_SMALL_COUNT + BUSTER_DIAGNOSTIC_KEYS];
    _Alignas(64) uint64_t sequences[BUSTER_DIAGNOSTIC_SMALL_COUNT + BUSTER_DIAGNOSTIC_KEYS];
    const uint32_t blocks = (count + BUSTER_DIAGNOSTIC_KEYS - 1) / BUSTER_DIAGNOSTIC_KEYS;
    const __m512i one = _mm512_set1_epi64(1);
    const __m512i largest = _mm512_set1_epi64(-1);
    // 8 records are 24 qwords in three registers: their sources are qwords 0, 3, ..., 21 and their sequences 1, 4, ...,
    // 22. Two-register permutes take those below 16, and a third the rest from the third register.
    const __m512i source_low = _mm512_setr_epi64(0, 3, 6, 9, 12, 15, 0, 0);
    const __m512i source_high = _mm512_setr_epi64(0, 0, 0, 0, 0, 0, 2, 5);
    const __m512i sequence_low = _mm512_setr_epi64(1, 4, 7, 10, 13, 0, 0, 0);
    const __m512i sequence_high = _mm512_setr_epi64(0, 0, 0, 0, 0, 0, 3, 6);
    for (uint32_t b = 0; b < blocks; b += 1)
    {
        const uint32_t remaining = count - b * BUSTER_DIAGNOSTIC_KEYS;
        const uint32_t present = remaining < BUSTER_DIAGNOSTIC_KEYS ? remaining : BUSTER_DIAGNOSTIC_KEYS;
        const uint32_t qwords = present * 3;
        const uint8_t* bytes = (const uint8_t*)(items + (uint64_t)b * BUSTER_DIAGNOSTIC_KEYS);
        const __m512i first = _mm512_maskz_loadu_epi64(qwords >= 8 ? (__mmask8)0xff : (__mmask8)((1u << qwords) - 1), bytes);
        const __m512i second = _mm512_maskz_loadu_epi64(qwords >= 16 ? (__mmask8)0xff : qwords > 8 ? (__mmask8)((1u << (qwords - 8)) - 1) : (__mmask8)0, bytes + 64);
        const __m512i third = _mm512_maskz_loadu_epi64(qwords > 16 ? (__mmask8)((1u << (qwords - 16)) - 1) : (__mmask8)0, bytes + 128);
        const __mmask8 absent = (__mmask8)~((1u << present) - 1);
        const __m512i block_sources = _mm512_mask_permutexvar_epi64(_mm512_permutex2var_epi64(first, source_low, second), 0xc0, source_high, third);
        const __m512i block_sequences = _mm512_mask_permutexvar_epi64(_mm512_permutex2var_epi64(first, sequence_low, second), 0xe0, sequence_high, third);
        _mm512_store_si512(sources + (uint64_t)b * BUSTER_DIAGNOSTIC_KEYS, _mm512_mask_mov_epi64(block_sources, absent, largest));
        _mm512_store_si512(sequences + (uint64_t)b * BUSTER_DIAGNOSTIC_KEYS, _mm512_mask_mov_epi64(block_sequences, absent, largest));
    }
    for (uint32_t i = 0; i < count; i += 1)
    {
        const __m512i source = _mm512_set1_epi64((long long)sources[i]);
        const __m512i sequence = _mm512_set1_epi64((long long)sequences[i]);
        const uint32_t own = i / BUSTER_DIAGNOSTIC_KEYS;
        const __mmask8 below = (__mmask8)((1u << (i % BUSTER_DIAGNOSTIC_KEYS)) - 1);
        __m512i places = _mm512_setzero_si512();
        for (uint32_t b = 0; b < own; b += 1)
        {
            const __m512i block_sources = _mm512_load_si512(sources + (uint64_t)b * BUSTER_DIAGNOSTIC_KEYS);
            const __m512i block_sequences = _mm512_load_si512(sequences + (uint64_t)b * BUSTER_DIAGNOSTIC_KEYS);
            const __mmask8 same_source = _mm512_cmpeq_epu64_mask(block_sources, source);
            const __mmask8 before = _mm512_cmplt_epu64_mask(block_sources, source) | _mm512_mask_cmple_epu64_mask(same_source, block_sequences, sequence);
            places = _mm512_mask_add_epi64(places, before, places, one);
        }
        {
            const __m512i block_sources = _mm512_load_si512(sources + (uint64_t)own * BUSTER_DIAGNOSTIC_KEYS);
            const __m512i block_sequences = _mm512_load_si512(sequences + (uint64_t)own * BUSTER_DIAGNOSTIC_KEYS);
            const __mmask8 same_source = _mm512_cmpeq_epu64_mask(block_sources, source);
            const __mmask8 before = _mm512_cmplt_epu64_mask(block_sources, source) | _mm512_mask_cmplt_epu64_mask(same_source, block_sequences, sequence) |
                                    _mm512_mask_cmpeq_epu64_mask(same_source & below, block_sequences, sequence);
            places = _mm512_mask_add_epi64(places, before, places, one);
        }
        for (uint32_t b = own + 1; b < blocks; b += 1)
        {
            const __m512i block_sources = _mm512_load_si512(sources + (uint64_t)b * BUSTER_DIAGNOSTIC_KEYS);
            const __m512i block_sequences = _mm512_load_si512(sequences + (uint64_t)b * BUSTER_DIAGNOSTIC_KEYS);
            const __mmask8 same_source = _mm512_cmpeq_epu64_mask(block_sources, source);
            const __mmask8 before = _mm512_cmplt_epu64_mask(block_sources, source) | _mm512_mask_cmplt_epu64_mask(same_source, block_sequences, sequence);
            places = _mm512_mask_add_epi64(places, before, places, one);
        }
        scratch[_mm512_reduce_add_epi64(places)] = items[i];
    }
    memcpy(items, scratch, (uint64_t)count * sizeof(DiagnosticOrder));
}

// `scratch` has `count` records and does not overlap `items`. The result always lands back in `items`.
static void diagnostic_sort(DiagnosticOrder* items, uint32_t count, DiagnosticOrder* scratch)
{
    if (count <= BUSTER_DIAGNOSTIC_SMALL_COUNT)
    {
        diagnostic_sort_small(items, count, scratch);
    }
    else
    {
        DiagnosticOrder* input = items;
        DiagnosticOrder* output = scratch;
        for (uint32_t pass = 0; pass < 16; pass += 1)
        {
            uint32_t histogram[BUSTER_DIAGNOSTIC_BUCKETS] = { 0 };
            const uint32_t shift = (pass & 7) * 8;
            for (uint32_t placing = 0; placing < 2; placing += 1)
            {
                for (uint64_t begin = 0; begin < count; begin += BUSTER_DIAGNOSTIC_BATCH)
                {
                    const uint32_t width = count - begin < BUSTER_DIAGNOSTIC_BATCH ? (uint32_t)(count - begin) : BUSTER_DIAGNOSTIC_BATCH;
                    // Each a 64-byte line: ranks' one 64-byte store, read back four bytes at a time, split across two lines
                    // in some processes and not others, by where the stack fell, and the sort ran at 62 ns/key rather
                    // than 43 on the 9700X (#2959).
                    _Alignas(64) uint32_t digits[BUSTER_DIAGNOSTIC_BATCH] = { 0 };
                    _Alignas(64) uint32_t ranks[BUSTER_DIAGNOSTIC_BATCH];
                    for (uint32_t i = 0; i < width; i += 1)
                    {
                        const DiagnosticOrder item = input[begin + i];
                        digits[i] = (uint32_t)((pass < 8 ? item.sequence : item.source) >> shift) & 255;
                    }
                    const __m512i conflicts = _mm512_conflict_epi32(_mm512_loadu_si512(digits));
                    _mm512_storeu_si512(ranks, _mm512_popcnt_epi32(conflicts));
                    const __mmask16 live = (__mmask16)((1u << width) - 1);
                    // Only live conflicts participate: inactive zero digits must not hide a live bucket's last key.
                    const uint32_t last = (uint32_t)live & ~(uint32_t)_mm512_mask_reduce_or_epi32(live, conflicts);
                    if (placing != 0)
                    {
                        for (uint32_t i = 0; i < width; i += 1)
                        {
                            output[histogram[digits[i]] + ranks[i]] = input[begin + i];
                        }
                    }
                    for (uint32_t remaining = last; remaining != 0; remaining &= remaining - 1)
                    {
                        const uint32_t i = (uint32_t)__builtin_ctz(remaining);
                        histogram[digits[i]] += ranks[i] + 1;
                    }
                }
                if (placing == 0)
                {
                    uint32_t offset = 0;
                    for (uint32_t bucket = 0; bucket < BUSTER_DIAGNOSTIC_BUCKETS; bucket += 1)
                    {
                        const uint32_t size = histogram[bucket];
                        histogram[bucket] = offset;
                        offset += size;
                    }
                }
            }
            DiagnosticOrder* swap = input;
            input = output;
            output = swap;
        }
    }
}

// The text remains owned by the lane buffers, which must live until diagnostic_write finishes. `arena` is separate
// from those buffers. No output is usable after a failed merge, including a buffer that ran out of room after a fatal.
static Diagnostics diagnostic_merge(const DiagnosticBuffer* buffers, uint32_t buffer_count, Arena* arena)
{
    Diagnostics result = { 0 };
    const uint64_t before = arena->length;
    uint64_t total = 0;
    bool valid = true;
    for (uint32_t i = 0; i < buffer_count; i += 1)
    {
        total += buffers[i].count;
        valid = valid && !buffers[i].failed && buffers[i].count <= buffers[i].capacity && total <= UINT32_MAX;
    }
    if (valid && total != 0)
    {
        result.records = BUSTER_ARENA_PUSH_ARRAY(arena, Diagnostic, total);
        result.order = BUSTER_ARENA_PUSH_ARRAY(arena, DiagnosticOrder, total);
        DiagnosticOrder* scratch = BUSTER_ARENA_PUSH_ARRAY(arena, DiagnosticOrder, total);
        valid = result.records != 0 && result.order != 0 && scratch != 0;
        for (uint32_t lane = 0; valid && lane < buffer_count; lane += 1)
        {
            for (uint32_t i = 0; i < buffers[lane].count; i += 1)
            {
                const Diagnostic record = buffers[lane].records[i];
                result.records[result.count] = record;
                result.order[result.count] = (DiagnosticOrder)
                {
                    .source = record.source,
                    .sequence = record.sequence,
                    .record = result.count,
                };
                result.count += 1;
            }
        }
        if (valid)
        {
            diagnostic_sort(result.order, result.count, scratch);
            for (uint32_t i = 0; i < result.count; i += 1)
            {
                if (result.records[result.order[i].record].severity == DIAGNOSTIC_FATAL)
                {
                    result.count = i + 1;
                }
            }
        }
    }
    if (!valid)
    {
        arena_pop_to(arena, before);
        result = (Diagnostics){ 0 };
    }
    result.success = valid;
    return result;
}

static bool diagnostic_write(const Diagnostics* diagnostics, FILE* stream)
{
    bool result = diagnostics->success && stream != 0;
    for (uint32_t i = 0; result && i < diagnostics->count; i += 1)
    {
        const String8 text = diagnostics->records[diagnostics->order[i].record].text;
        result = text.length == 0 || fwrite(text.pointer, 1, text.length, stream) == text.length;
    }
    return result;
}
