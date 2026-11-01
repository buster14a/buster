// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #938's versioned allocation-problem interchange, not an executable IR.
// Include before tier1_problem_intervals.c. Decode borrows immutable, unpadded bytes;
// row identity is its index. The original MIR remains identified by its SHA-256.
// Native producers use rectangular uint32_t tables. The same validator serves
// native tables and the little-endian wire view; no casts to unaligned structs.
// Decode/encode allocate nothing. Results are published only on success. These
// are ingestion/qualification routines, not measured allocation hot kernels.

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <limits.h>

#define BUSTER_TP_MAGIC UINT32_C(0x50414d42)
#define BUSTER_TP_VERSION 1u
#define BUSTER_TP_HEADER_WORDS 24u
#define BUSTER_TP_META_COUNT 12u
#define BUSTER_TP_TABLE_COUNT 11u
#define BUSTER_TP_MAX_ROWS (1u << 20)
#define BUSTER_TP_MAX_BYTES (1u << 30)
#define BUSTER_TP_NONE UINT32_MAX
#define BUSTER_TP_PRE_PHI 1u
#define BUSTER_TP_GREEDY_FREQUENCIES 2u
#define BUSTER_TP_FROZEN_ORDER 4u
#define BUSTER_TP_SYNTHETIC 8u
#define BUSTER_TP_RESERVED 1u
#define BUSTER_TP_SCRATCH 2u
#define BUSTER_TP_CALLEE_SAVED 4u
#define BUSTER_TP_READ 1u
#define BUSTER_TP_WRITE 2u
#define BUSTER_TP_IMPLICIT 4u
#define BUSTER_TP_UNDEF 8u
#define BUSTER_TP_EARLY_CLOBBER 16u
#define BUSTER_TP_KILL 32u
#define BUSTER_TP_DEAD 64u

typedef enum Tier1ProblemTable
{
    BUSTER_TP_BLOCK,
    BUSTER_TP_EDGE,
    BUSTER_TP_INSTRUCTION,
    BUSTER_TP_VALUE,
    BUSTER_TP_OPERAND,
    BUSTER_TP_REGISTER,
    BUSTER_TP_CLASS,
    BUSTER_TP_MEMBER,
    BUSTER_TP_UNIT,
    BUSTER_TP_PHI,
    BUSTER_TP_FORM,
} Tier1ProblemTable;

typedef enum Tier1ProblemReason
{
    TIER1_PROBLEM_OK,
    TIER1_PROBLEM_ARGUMENT,
    TIER1_PROBLEM_VERSION,
    TIER1_PROBLEM_LENGTH,
    TIER1_PROBLEM_LIMIT,
    TIER1_PROBLEM_METADATA,
    TIER1_PROBLEM_LAYOUT,
    TIER1_PROBLEM_REFERENCE,
    TIER1_PROBLEM_CONSTRAINT,
    TIER1_PROBLEM_CAPACITY,
    TIER1_PROBLEM_TARGET,
    TIER1_PROBLEM_CFG,
    TIER1_PROBLEM_PHI,
    TIER1_PROBLEM_CLASS,
    TIER1_PROBLEM_ALIAS,
    TIER1_PROBLEM_PHASE,
    TIER1_PROBLEM_TIE,
    TIER1_PROBLEM_IMPLICIT,
    TIER1_PROBLEM_SUBREGISTER,
    TIER1_PROBLEM_FORM,
    TIER1_PROBLEM_BOUNDARY,
    TIER1_PROBLEM_SSA,
    TIER1_PROBLEM_MASK_CHANGE,
    TIER1_PROBLEM_CERTIFICATE_UNAVAILABLE,
    TIER1_PROBLEM_REASON_COUNT,
} Tier1ProblemReason;

typedef struct Tier1ProblemText Tier1ProblemText;
struct Tier1ProblemText
{
    const uint8_t* data;
    uint32_t size;
};

typedef struct Tier1ProblemRows Tier1ProblemRows;
struct Tier1ProblemRows
{
    const void* data;
    uint32_t count;
};

typedef struct Tier1Problem Tier1Problem;
struct Tier1Problem
{
    // LLVM revision, exporter revision, source SHA-256, MIR SHA-256, target,
    // CPU, features, exact pass options, ABI, frequency provenance, workload, function.
    Tier1ProblemText metadata[BUSTER_TP_META_COUNT];
    Tier1ProblemRows tables[BUSTER_TP_TABLE_COUNT];
    uint32_t flags;
    uint32_t original_operands;
    uint32_t unit_count;
    bool wire;
};

typedef struct Tier1ProblemTally Tier1ProblemTally;
struct Tier1ProblemTally
{
    uint64_t functions;
    uint64_t accepted;
    uint64_t rejected;
    // One terminal reason per attempted function, including malformed inputs.
    uint64_t reasons[TIER1_PROBLEM_REASON_COUNT];
};

static const uint32_t tier1_problem_widths[BUSTER_TP_TABLE_COUNT] = { 8, 8, 8, 4, 10, 8, 5, 1, 1, 3, 4 };

static uint32_t tier1_problem_load(const uint8_t* p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void tier1_problem_store(uint8_t* p, uint32_t value)
{
    for (uint32_t b = 0; b < 4; b += 1)
    {
        p[b] = (uint8_t)(value >> (8 * b));
    }
}

// Internal accessor: callers have established the table's storage and row bounds.
static uint32_t tier1_problem_word(const Tier1Problem* problem, uint32_t table, uint32_t row, uint32_t column)
{
    const uint8_t* p = (const uint8_t*)problem->tables[table].data + ((uint64_t)row * tier1_problem_widths[table] + column) * 4;
    uint32_t result;
    if (problem->wire)
    {
        result = tier1_problem_load(p);
    }
    else
    {
        memcpy(&result, p, sizeof(result));
    }
    return result;
}

static bool tier1_problem_text_equal(Tier1ProblemText text, const char* expected)
{
    const size_t size = strlen(expected);
    return text.size == size && (size == 0 || memcmp(text.data, expected, size) == 0);
}

static bool tier1_problem_digest(Tier1ProblemText text, uint32_t length)
{
    bool valid = text.size == length && text.data != 0;
    for (uint32_t i = 0; valid && i < length; i += 1)
    {
        const uint8_t c = text.data[i];
        valid = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    }
    return valid;
}

static bool tier1_problem_range(uint32_t begin, uint32_t count, uint32_t limit)
{
    return begin <= limit && count <= limit - begin;
}

static bool tier1_problem_overlap(const void* a, uint64_t an, const void* b, uint64_t bn)
{
    const uintptr_t x = (uintptr_t)a;
    const uintptr_t y = (uintptr_t)b;
    return an != 0 && bn != 0 && (x <= y ? y - x < an : x - y < bn);
}

static Tier1ProblemReason tier1_problem_validate(const Tier1Problem* p)
{
    Tier1ProblemReason result = p == 0 ? TIER1_PROBLEM_ARGUMENT : TIER1_PROBLEM_OK;
    if (result == TIER1_PROBLEM_OK && ((p->flags & ~15u) != 0 || (p->flags & BUSTER_TP_PRE_PHI) == 0))
    {
        result = TIER1_PROBLEM_VERSION;
    }
    for (uint32_t t = 0; result == TIER1_PROBLEM_OK && t < BUSTER_TP_TABLE_COUNT; t += 1)
    {
        if (p->tables[t].count > BUSTER_TP_MAX_ROWS)
        {
            result = TIER1_PROBLEM_LIMIT;
        }
        else if (p->tables[t].count != 0 && p->tables[t].data == 0)
        {
            result = TIER1_PROBLEM_ARGUMENT;
        }
    }
    for (uint32_t m = 0; result == TIER1_PROBLEM_OK && m < BUSTER_TP_META_COUNT; m += 1)
    {
        const Tier1ProblemText text = p->metadata[m];
        if (text.size > BUSTER_TP_MAX_ROWS || (text.size != 0 && text.data == 0) || (text.size == 0 && m != 6))
        {
            result = TIER1_PROBLEM_METADATA;
        }
        for (uint32_t i = 0; result == TIER1_PROBLEM_OK && i < text.size; i += 1)
        {
            if (text.data[i] < 32 || text.data[i] == 127)
            {
                result = TIER1_PROBLEM_METADATA;
            }
        }
        if (result == TIER1_PROBLEM_OK && m < 4 && !tier1_problem_digest(text, m < 2 ? 40 : 64))
        {
            result = TIER1_PROBLEM_METADATA;
        }
    }
    if (result == TIER1_PROBLEM_OK)
    {
        if (p->unit_count > BUSTER_TP_MAX_ROWS || p->original_operands > p->tables[BUSTER_TP_OPERAND].count || p->tables[BUSTER_TP_BLOCK].count == 0)
        {
            result = TIER1_PROBLEM_LAYOUT;
        }
    }
    // Canonical CSR ownership forbids overlaps, holes, duplicate ownership and
    // orphan records. Every instruction, edge, operand and alternative is visited.
    uint32_t next_instruction = 0;
    uint32_t next_edge = 0;
    for (uint32_t b = 0; result == TIER1_PROBLEM_OK && b < p->tables[BUSTER_TP_BLOCK].count; b += 1)
    {
        const uint32_t ib = tier1_problem_word(p, BUSTER_TP_BLOCK, b, 1);
        const uint32_t ic = tier1_problem_word(p, BUSTER_TP_BLOCK, b, 2);
        const uint32_t eb = tier1_problem_word(p, BUSTER_TP_BLOCK, b, 3);
        const uint32_t ec = tier1_problem_word(p, BUSTER_TP_BLOCK, b, 4);
        if (ib != next_instruction || eb != next_edge || !tier1_problem_range(ib, ic, p->tables[BUSTER_TP_INSTRUCTION].count) || !tier1_problem_range(eb, ec, p->tables[BUSTER_TP_EDGE].count) || tier1_problem_word(p, BUSTER_TP_BLOCK, b, 7) != 0)
        {
            result = TIER1_PROBLEM_LAYOUT;
        }
        for (uint32_t i = ib; result == TIER1_PROBLEM_OK && i < ib + ic; i += 1)
        {
            if (tier1_problem_word(p, BUSTER_TP_INSTRUCTION, i, 1) != b)
            {
                result = TIER1_PROBLEM_REFERENCE;
            }
        }
        for (uint32_t e = eb; result == TIER1_PROBLEM_OK && e < eb + ec; e += 1)
        {
            if (tier1_problem_word(p, BUSTER_TP_EDGE, e, 0) != b)
            {
                result = TIER1_PROBLEM_REFERENCE;
            }
        }
        next_instruction = ib + ic;
        next_edge = eb + ec;
    }
    if (result == TIER1_PROBLEM_OK && (next_instruction != p->tables[BUSTER_TP_INSTRUCTION].count || next_edge != p->tables[BUSTER_TP_EDGE].count))
    {
        result = TIER1_PROBLEM_LAYOUT;
    }
    uint32_t next_phi = 0;
    for (uint32_t e = 0; result == TIER1_PROBLEM_OK && e < p->tables[BUSTER_TP_EDGE].count; e += 1)
    {
        const uint32_t begin = tier1_problem_word(p, BUSTER_TP_EDGE, e, 2);
        const uint32_t count = tier1_problem_word(p, BUSTER_TP_EDGE, e, 3);
        const uint32_t numerator = tier1_problem_word(p, BUSTER_TP_EDGE, e, 6);
        const uint32_t denominator = tier1_problem_word(p, BUSTER_TP_EDGE, e, 7);
        if (tier1_problem_word(p, BUSTER_TP_EDGE, e, 1) >= p->tables[BUSTER_TP_BLOCK].count || begin != next_phi || !tier1_problem_range(begin, count, p->tables[BUSTER_TP_PHI].count) || denominator == 0 || numerator > denominator)
        {
            result = TIER1_PROBLEM_REFERENCE;
        }
        for (uint32_t f = begin; result == TIER1_PROBLEM_OK && f < begin + count; f += 1)
        {
            if (tier1_problem_word(p, BUSTER_TP_PHI, f, 2) != e)
            {
                result = TIER1_PROBLEM_REFERENCE;
            }
        }
        next_phi = begin + count;
    }
    if (result == TIER1_PROBLEM_OK && next_phi != p->tables[BUSTER_TP_PHI].count)
    {
        result = TIER1_PROBLEM_LAYOUT;
    }
    uint32_t next_operand = 0;
    uint32_t next_form = 0;
    for (uint32_t i = 0; result == TIER1_PROBLEM_OK && i < p->tables[BUSTER_TP_INSTRUCTION].count; i += 1)
    {
        const uint32_t begin = tier1_problem_word(p, BUSTER_TP_INSTRUCTION, i, 3);
        const uint32_t count = tier1_problem_word(p, BUSTER_TP_INSTRUCTION, i, 4);
        const uint32_t fb = tier1_problem_word(p, BUSTER_TP_INSTRUCTION, i, 6);
        const uint32_t fc = tier1_problem_word(p, BUSTER_TP_INSTRUCTION, i, 7);
        if (begin != next_operand || fb != next_form || !tier1_problem_range(begin, count, p->original_operands) || !tier1_problem_range(fb, fc, p->tables[BUSTER_TP_FORM].count) || (tier1_problem_word(p, BUSTER_TP_INSTRUCTION, i, 5) & ~15u) != 0)
        {
            result = TIER1_PROBLEM_LAYOUT;
        }
        for (uint32_t f = fb; result == TIER1_PROBLEM_OK && f < fb + fc; f += 1)
        {
            if (tier1_problem_word(p, BUSTER_TP_FORM, f, 0) != i)
            {
                result = TIER1_PROBLEM_REFERENCE;
            }
        }
        next_operand = begin + count;
        next_form = fb + fc;
    }
    if (result == TIER1_PROBLEM_OK && (next_operand != p->original_operands || next_form != p->tables[BUSTER_TP_FORM].count))
    {
        result = TIER1_PROBLEM_LAYOUT;
    }
    for (uint32_t f = 0; result == TIER1_PROBLEM_OK && f < p->tables[BUSTER_TP_FORM].count; f += 1)
    {
        const uint32_t begin = tier1_problem_word(p, BUSTER_TP_FORM, f, 2);
        const uint32_t count = tier1_problem_word(p, BUSTER_TP_FORM, f, 3);
        if (begin != next_operand || !tier1_problem_range(begin, count, p->tables[BUSTER_TP_OPERAND].count))
        {
            result = TIER1_PROBLEM_LAYOUT;
        }
        next_operand = begin + count;
    }
    if (result == TIER1_PROBLEM_OK && next_operand != p->tables[BUSTER_TP_OPERAND].count)
    {
        result = TIER1_PROBLEM_LAYOUT;
    }
    uint32_t next_unit = 0;
    for (uint32_t r = 0; result == TIER1_PROBLEM_OK && r < p->tables[BUSTER_TP_REGISTER].count; r += 1)
    {
        const uint32_t begin = tier1_problem_word(p, BUSTER_TP_REGISTER, r, 4);
        const uint32_t count = tier1_problem_word(p, BUSTER_TP_REGISTER, r, 5);
        const uint32_t flags = tier1_problem_word(p, BUSTER_TP_REGISTER, r, 6);
        if (begin != next_unit || count == 0 || !tier1_problem_range(begin, count, p->tables[BUSTER_TP_UNIT].count) || tier1_problem_word(p, BUSTER_TP_REGISTER, r, 1) > 3 || tier1_problem_word(p, BUSTER_TP_REGISTER, r, 3) == 0 || (flags & ~7u) != 0 || ((flags & BUSTER_TP_SCRATCH) != 0 && (flags & BUSTER_TP_RESERVED) == 0) || tier1_problem_word(p, BUSTER_TP_REGISTER, r, 7) != 0)
        {
            result = TIER1_PROBLEM_CONSTRAINT;
        }
        uint32_t previous = 0;
        for (uint32_t u = begin; result == TIER1_PROBLEM_OK && u < begin + count; u += 1)
        {
            const uint32_t unit = tier1_problem_word(p, BUSTER_TP_UNIT, u, 0);
            if (unit >= p->unit_count || (u != begin && unit <= previous))
            {
                result = TIER1_PROBLEM_REFERENCE;
            }
            previous = unit;
        }
        next_unit = begin + count;
    }
    if (result == TIER1_PROBLEM_OK && next_unit != p->tables[BUSTER_TP_UNIT].count)
    {
        result = TIER1_PROBLEM_LAYOUT;
    }
    uint32_t next_member = 0;
    for (uint32_t c = 0; result == TIER1_PROBLEM_OK && c < p->tables[BUSTER_TP_CLASS].count; c += 1)
    {
        const uint32_t begin = tier1_problem_word(p, BUSTER_TP_CLASS, c, 3);
        const uint32_t count = tier1_problem_word(p, BUSTER_TP_CLASS, c, 4);
        if (begin != next_member || !tier1_problem_range(begin, count, p->tables[BUSTER_TP_MEMBER].count) || tier1_problem_word(p, BUSTER_TP_CLASS, c, 1) > 3)
        {
            result = TIER1_PROBLEM_LAYOUT;
        }
        else if (tier1_problem_word(p, BUSTER_TP_CLASS, c, 2) == 0)
        {
            result = TIER1_PROBLEM_CONSTRAINT;
        }
        uint32_t previous = 0;
        for (uint32_t m = begin; result == TIER1_PROBLEM_OK && m < begin + count; m += 1)
        {
            const uint32_t reg = tier1_problem_word(p, BUSTER_TP_MEMBER, m, 0);
            if (reg >= p->tables[BUSTER_TP_REGISTER].count || (m != begin && reg <= previous))
            {
                result = TIER1_PROBLEM_REFERENCE;
            }
            previous = reg;
        }
        next_member = begin + count;
    }
    if (result == TIER1_PROBLEM_OK && next_member != p->tables[BUSTER_TP_MEMBER].count)
    {
        result = TIER1_PROBLEM_LAYOUT;
    }
    for (uint32_t v = 0; result == TIER1_PROBLEM_OK && v < p->tables[BUSTER_TP_VALUE].count; v += 1)
    {
        if (tier1_problem_word(p, BUSTER_TP_VALUE, v, 1) >= p->tables[BUSTER_TP_CLASS].count || tier1_problem_word(p, BUSTER_TP_VALUE, v, 2) == 0 || (tier1_problem_word(p, BUSTER_TP_VALUE, v, 3) & ~3u) != 0)
        {
            result = TIER1_PROBLEM_REFERENCE;
        }
    }
    for (uint32_t o = 0; result == TIER1_PROBLEM_OK && o < p->tables[BUSTER_TP_OPERAND].count; o += 1)
    {
        const uint32_t kind = tier1_problem_word(p, BUSTER_TP_OPERAND, o, 0);
        const uint32_t id = tier1_problem_word(p, BUSTER_TP_OPERAND, o, 1);
        const uint32_t flags = tier1_problem_word(p, BUSTER_TP_OPERAND, o, 3);
        const uint32_t cls = tier1_problem_word(p, BUSTER_TP_OPERAND, o, 4);
        const uint32_t tie = tier1_problem_word(p, BUSTER_TP_OPERAND, o, 5);
        const uint32_t width = tier1_problem_word(p, BUSTER_TP_OPERAND, o, 6);
        if (kind > 3 || tier1_problem_word(p, BUSTER_TP_OPERAND, o, 2) > 3 || (flags & ~127u) != 0 || (tie != BUSTER_TP_NONE && tie >= p->tables[BUSTER_TP_OPERAND].count))
        {
            result = TIER1_PROBLEM_CONSTRAINT;
        }
        else if ((kind == 0 && (id >= p->tables[BUSTER_TP_VALUE].count || cls >= p->tables[BUSTER_TP_CLASS].count || width == 0)) || (kind == 1 && (id >= p->tables[BUSTER_TP_REGISTER].count || width == 0)) || (kind == 2 && id >= p->tables[BUSTER_TP_CLASS].count))
        {
            result = TIER1_PROBLEM_REFERENCE;
        }
        else if (kind != 0 && cls != BUSTER_TP_NONE)
        {
            result = TIER1_PROBLEM_CONSTRAINT;
        }
        else if (tie != BUSTER_TP_NONE && (tie == o || tier1_problem_word(p, BUSTER_TP_OPERAND, tie, 5) != o))
        {
            result = TIER1_PROBLEM_CONSTRAINT;
        }
    }
    // Ties may not escape their original instruction or alternative form.
    for (uint32_t group = 0; result == TIER1_PROBLEM_OK && group < 2; group += 1)
    {
        const uint32_t table = group == 0 ? BUSTER_TP_INSTRUCTION : BUSTER_TP_FORM;
        const uint32_t column = group == 0 ? 3 : 2;
        const uint32_t count = p->tables[table].count;
        for (uint32_t i = 0; result == TIER1_PROBLEM_OK && i < count; i += 1)
        {
            const uint32_t begin = tier1_problem_word(p, table, i, column);
            const uint32_t end = begin + tier1_problem_word(p, table, i, column + 1);
            for (uint32_t o = begin; result == TIER1_PROBLEM_OK && o < end; o += 1)
            {
                const uint32_t tie = tier1_problem_word(p, BUSTER_TP_OPERAND, o, 5);
                if (tie != BUSTER_TP_NONE && (tie < begin || tie >= end))
                {
                    result = TIER1_PROBLEM_CONSTRAINT;
                }
            }
        }
    }
    for (uint32_t f = 0; result == TIER1_PROBLEM_OK && f < p->tables[BUSTER_TP_PHI].count; f += 1)
    {
        const uint32_t from = tier1_problem_word(p, BUSTER_TP_PHI, f, 0);
        const uint32_t to = tier1_problem_word(p, BUSTER_TP_PHI, f, 1);
        if (from >= p->tables[BUSTER_TP_VALUE].count || to >= p->tables[BUSTER_TP_VALUE].count)
        {
            result = TIER1_PROBLEM_REFERENCE;
        }
        else if (tier1_problem_word(p, BUSTER_TP_VALUE, from, 2) != tier1_problem_word(p, BUSTER_TP_VALUE, to, 2))
        {
            result = TIER1_PROBLEM_CONSTRAINT;
        }
    }
    return result;
}

static Tier1ProblemReason tier1_problem_decode(const uint8_t* bytes, size_t size, Tier1Problem* output)
{
    Tier1ProblemReason result = bytes == 0 || output == 0 ? TIER1_PROBLEM_ARGUMENT : TIER1_PROBLEM_OK;
    Tier1Problem p = { 0 };
    const uint32_t header_bytes = BUSTER_TP_HEADER_WORDS * 4;
    if (result == TIER1_PROBLEM_OK && (size < header_bytes || size > BUSTER_TP_MAX_BYTES))
    {
        result = TIER1_PROBLEM_LENGTH;
    }
    if (result == TIER1_PROBLEM_OK && tier1_problem_overlap(bytes, size, output, sizeof(*output)))
    {
        result = TIER1_PROBLEM_ARGUMENT;
    }
    if (result == TIER1_PROBLEM_OK && (tier1_problem_load(bytes) != BUSTER_TP_MAGIC || tier1_problem_load(bytes + 4) != BUSTER_TP_VERSION))
    {
        result = TIER1_PROBLEM_VERSION;
    }
    uint64_t offset = header_bytes;
    uint32_t metadata_end = 0;
    if (result == TIER1_PROBLEM_OK)
    {
        p.wire = true;
        p.flags = tier1_problem_load(bytes + 12);
        p.original_operands = tier1_problem_load(bytes + 16);
        const uint32_t metadata_size = tier1_problem_load(bytes + 20);
        p.unit_count = tier1_problem_load(bytes + 68);
        if (tier1_problem_load(bytes + 8) != size || metadata_size > size - header_bytes || (metadata_size & 3u) != 0)
        {
            result = TIER1_PROBLEM_LENGTH;
        }
        metadata_end = header_bytes + metadata_size;
        for (uint32_t h = 18; result == TIER1_PROBLEM_OK && h < BUSTER_TP_HEADER_WORDS; h += 1)
        {
            if (tier1_problem_load(bytes + 4 * h) != 0)
            {
                result = TIER1_PROBLEM_VERSION;
            }
        }
    }
    for (uint32_t m = 0; result == TIER1_PROBLEM_OK && m < BUSTER_TP_META_COUNT; m += 1)
    {
        if (offset + 4 > metadata_end)
        {
            result = TIER1_PROBLEM_LENGTH;
        }
        else
        {
            const uint32_t length = tier1_problem_load(bytes + offset);
            offset += 4;
            if (length > metadata_end - offset)
            {
                result = TIER1_PROBLEM_LENGTH;
            }
            else
            {
                p.metadata[m].data = bytes + offset;
                p.metadata[m].size = length;
                offset += length;
            }
        }
    }
    if (result == TIER1_PROBLEM_OK && metadata_end - offset > 3)
    {
        result = TIER1_PROBLEM_LAYOUT;
    }
    while (result == TIER1_PROBLEM_OK && offset < metadata_end)
    {
        if (bytes[offset] != 0)
        {
            result = TIER1_PROBLEM_LAYOUT;
        }
        offset += 1;
    }
    for (uint32_t t = 0; result == TIER1_PROBLEM_OK && t < BUSTER_TP_TABLE_COUNT; t += 1)
    {
        const uint32_t count = tier1_problem_load(bytes + 24 + 4 * t);
        const uint64_t length = (uint64_t)count * tier1_problem_widths[t] * 4;
        if (count > BUSTER_TP_MAX_ROWS)
        {
            result = TIER1_PROBLEM_LIMIT;
        }
        else if (offset > size || length > size - offset)
        {
            result = TIER1_PROBLEM_LENGTH;
        }
        else
        {
            p.tables[t].data = bytes + offset;
            p.tables[t].count = count;
            offset += length;
        }
    }
    if (result == TIER1_PROBLEM_OK)
    {
        result = offset == size ? tier1_problem_validate(&p) : TIER1_PROBLEM_LENGTH;
    }
    if (result == TIER1_PROBLEM_OK)
    {
        *output = p;
    }
    return result;
}

// Output must not overlap the descriptor, metadata, or any borrowed table.
// A failed encode leaves both destination and written untouched.
static Tier1ProblemReason tier1_problem_encode(const Tier1Problem* p, uint8_t* bytes, size_t capacity, size_t* written)
{
    Tier1ProblemReason result = tier1_problem_validate(p);
    uint64_t metadata_size = 0;
    uint64_t total = BUSTER_TP_HEADER_WORDS * 4;
    if (result == TIER1_PROBLEM_OK && (bytes == 0 || written == 0))
    {
        result = TIER1_PROBLEM_ARGUMENT;
    }
    for (uint32_t m = 0; result == TIER1_PROBLEM_OK && m < BUSTER_TP_META_COUNT; m += 1)
    {
        metadata_size += 4 + p->metadata[m].size;
    }
    metadata_size = (metadata_size + 3) & ~UINT64_C(3);
    total += metadata_size;
    for (uint32_t t = 0; result == TIER1_PROBLEM_OK && t < BUSTER_TP_TABLE_COUNT; t += 1)
    {
        total += (uint64_t)p->tables[t].count * tier1_problem_widths[t] * 4;
    }
    if (result == TIER1_PROBLEM_OK && (total > BUSTER_TP_MAX_BYTES || total > capacity))
    {
        result = total > BUSTER_TP_MAX_BYTES ? TIER1_PROBLEM_LIMIT : TIER1_PROBLEM_CAPACITY;
    }
    if (result == TIER1_PROBLEM_OK && (tier1_problem_overlap(bytes, total, p, sizeof(*p)) || tier1_problem_overlap(bytes, total, written, sizeof(*written)) || tier1_problem_overlap(written, sizeof(*written), p, sizeof(*p))))
    {
        result = TIER1_PROBLEM_ARGUMENT;
    }
    for (uint32_t m = 0; result == TIER1_PROBLEM_OK && m < BUSTER_TP_META_COUNT; m += 1)
    {
        if (tier1_problem_overlap(bytes, total, p->metadata[m].data, p->metadata[m].size) || tier1_problem_overlap(written, sizeof(*written), p->metadata[m].data, p->metadata[m].size))
        {
            result = TIER1_PROBLEM_ARGUMENT;
        }
    }
    for (uint32_t t = 0; result == TIER1_PROBLEM_OK && t < BUSTER_TP_TABLE_COUNT; t += 1)
    {
        if (tier1_problem_overlap(bytes, total, p->tables[t].data, (uint64_t)p->tables[t].count * tier1_problem_widths[t] * 4) || tier1_problem_overlap(written, sizeof(*written), p->tables[t].data, (uint64_t)p->tables[t].count * tier1_problem_widths[t] * 4))
        {
            result = TIER1_PROBLEM_ARGUMENT;
        }
    }
    if (result == TIER1_PROBLEM_OK)
    {
        memset(bytes, 0, BUSTER_TP_HEADER_WORDS * 4);
        tier1_problem_store(bytes, BUSTER_TP_MAGIC);
        tier1_problem_store(bytes + 4, BUSTER_TP_VERSION);
        tier1_problem_store(bytes + 8, (uint32_t)total);
        tier1_problem_store(bytes + 12, p->flags);
        tier1_problem_store(bytes + 16, p->original_operands);
        tier1_problem_store(bytes + 20, (uint32_t)metadata_size);
        tier1_problem_store(bytes + 68, p->unit_count);
        uint64_t offset = BUSTER_TP_HEADER_WORDS * 4;
        for (uint32_t m = 0; m < BUSTER_TP_META_COUNT; m += 1)
        {
            const Tier1ProblemText text = p->metadata[m];
            tier1_problem_store(bytes + offset, text.size);
            offset += 4;
            if (text.size != 0)
            {
                memcpy(bytes + offset, text.data, text.size);
                offset += text.size;
            }
        }
        while (offset < BUSTER_TP_HEADER_WORDS * 4 + metadata_size)
        {
            bytes[offset++] = 0;
        }
        for (uint32_t t = 0; t < BUSTER_TP_TABLE_COUNT; t += 1)
        {
            const uint32_t count = p->tables[t].count;
            const uint32_t width = tier1_problem_widths[t];
            tier1_problem_store(bytes + 24 + 4 * t, count);
            for (uint32_t row = 0; row < count; row += 1)
            {
                for (uint32_t column = 0; column < width; column += 1)
                {
                    tier1_problem_store(bytes + offset, tier1_problem_word(p, t, row, column));
                    offset += 4;
                }
            }
        }
        *written = (size_t)total;
    }
    return result;
}

static const char* tier1_problem_reason_name(Tier1ProblemReason reason)
{
    static const char* const names[TIER1_PROBLEM_REASON_COUNT] =
    {
        "ok",
        "argument",
        "version",
        "length",
        "limit",
        "metadata",
        "layout",
        "reference",
        "constraint",
        "capacity",
        "target",
        "cfg",
        "phi",
        "class",
        "alias",
        "phase",
        "tie",
        "implicit",
        "subregister",
        "form",
        "boundary",
        "ssa",
        "mask-change",
        "certificate-unavailable",
    };
    return (unsigned)reason < TIER1_PROBLEM_REASON_COUNT ? names[reason] : "invalid-reason";
}

static void tier1_problem_tally(Tier1ProblemTally* tally, Tier1ProblemReason reason)
{
    if (tally != 0)
    {
        if ((unsigned)reason >= TIER1_PROBLEM_REASON_COUNT)
        {
            reason = TIER1_PROBLEM_ARGUMENT;
        }
        tally->functions += 1;
        tally->accepted += reason == TIER1_PROBLEM_OK;
        tally->rejected += reason != TIER1_PROBLEM_OK;
        tally->reasons[reason] += 1;
    }
}
