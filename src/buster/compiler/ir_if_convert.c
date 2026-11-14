// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Bounded scalar if-conversion (#1382). Include after ir.c. The caller supplies verified SSA, before publishing use
// lists or a verified-analysis revision, and immediately runs the ordinary optimizer. A successful result borrows the
// input's immutable tables: both the input and output arena must remain alive until that optimizer materializes its
// result. The input is never mutated, including shared/overlapping extra slices.
//
// A 64-byte opcode mask finds conditional terminators, with an exact scalar twin and no padded-tail reads. A region is
// a reachable head ending in a conditional branch whose two sides reach one join: each side is either the join itself
// (a triangle) or an arm, a chain of at most four blocks, each with the previous block (or the head) as its only
// reachable predecessor and an unconditional branch to the next. The join's phis take at most four selects.
//
// The C lowering's arms hold rows no phi needs (the comparisons truth simplification copied, their extensions and
// zeros) and rows the head already computed (a reloaded element and its address). So the arm's rows are numbered
// against the head's and the earlier arm rows first: a row whose operation, type, flags, fields and represented
// operands equal an earlier row's (an address's scale and offset by content) takes that row's value. A load does so
// only from a plain load of the same address and type after the head's last write (a store, an atomic, a fence, a
// call, a stack release, a lifetime marker or an observable load), since no arm row writes. Then only the rows the
// join's phis need, through represented operands, move to the head's end, before its branch. Each needed row must be
// total: constants, copies, wrapping integer arithmetic, bitwise operations, integer comparisons, selects, integer and
// pointer conversions, addresses, and shifts by an in-range constant. A plain load is speculated where the head made
// a plain access (a load or a store) of the same address and at least its size after its last release (a call, a
// stack restore or a lifetime marker), which could unmap or reuse the memory; nothing else proves an address. A row
// no phi needs stays in its arm, which becomes an unreachable stub ending in `unreachable`, for SCCP/DCE/CFG cleanup to
// remove; it must still be one that could have moved (or a reused one), so that dropping it drops no effect or fault.
// Each arm moves at most four non-constant rows (its blocks hold at most 32 in all); their cost and two per select is
// at most eight. These are conservative policy bounds, not measured latencies; IR_CBR has no branch probability.
//
// A join reached only by the region's two edges turns each phi into a select in place. A join with other
// predecessors keeps its phis: each takes a new select row in the head for the region's two entries, which become one
// entry from the head (LLVM SimplifyCFG's FoldTwoEntryPHINode and SpeculativelyExecuteBB, for the C lowering's
// shapes). The head then branches to the join. A region's blocks overlap no other region's in a round, except that a
// join may head another region (sequential clamps). Rows are permuted, and new ones are only the folded selects and
// the sunk accumulations' zeros.
//
// Two cost rules: a head whose condition reads a phi that the join's phis flow into (a running maximum) stays a
// branch, since the select would put the comparison on the loop's carried chain; and an in-place phi of `s op x` and
// `s` becomes `s op select(c, x, 0)`, so that an accumulator waits for the operation alone.
//
// A converted head whose only reachable predecessor chain leads up to a conditional branch may now be an arm of an
// outer region (a nested `?:`, `&&` inside `||`), so the pass runs again, at most four rounds. Between rounds and
// after the last, truth simplification (ir_truth.c's values variant) turns a select of truth values tested against
// zero into a select of i1 values, so that a nested truth value stays within the bounds. Address-taken blocks remain
// untouched.

#ifndef BUSTER_IR_IF_CONVERT_C
#define BUSTER_IR_IF_CONVERT_C

#include "ir_truth.c"

#define BUSTER_IR_IF_CONVERT_BATCH 64u
#define BUSTER_IR_IF_CONVERT_LANES 16u
#define BUSTER_IR_IF_CONVERT_ARM_LIMIT 4u
#define BUSTER_IR_IF_CONVERT_ARM_ROWS 32u
#define BUSTER_IR_IF_CONVERT_CHAIN_LIMIT 4u
#define BUSTER_IR_IF_CONVERT_PHI_LIMIT 4u
#define BUSTER_IR_IF_CONVERT_COST_LIMIT 8u
#define BUSTER_IR_IF_CONVERT_ROUND_LIMIT 4u
#define BUSTER_IR_IF_CONVERT_UNSAFE UINT32_MAX
// A head row's effect on the arms' loads: a write ends the reuse of an earlier load's value, and a release ends the
// proof that an earlier access's address stays dereferenceable.
#define BUSTER_IR_IF_CONVERT_WRITE 1u
#define BUSTER_IR_IF_CONVERT_RELEASE 2u
// A numbering key's fields beyond its operation.
#define BUSTER_IR_IF_CONVERT_KEY_A 1u
#define BUSTER_IR_IF_CONVERT_KEY_B 2u
#define BUSTER_IR_IF_CONVERT_KEY_C 4u
#define BUSTER_IR_IF_CONVERT_KEY_TYPE 8u
#define BUSTER_IR_IF_CONVERT_KEY_FLAGS 16u
// The rows the head reads its condition through, for a recurrence of the join's values (below).
#define BUSTER_IR_IF_CONVERT_RECURRENCE_ROWS 16u
// A round's new rows: a folded join's select, and the zero a sunk accumulation selects on its true or false side.
#define BUSTER_IR_IF_CONVERT_FRESH_SELECT 0u
#define BUSTER_IR_IF_CONVERT_FRESH_ZERO_TRUE 1u
#define BUSTER_IR_IF_CONVERT_FRESH_ZERO_FALSE 2u

typedef struct IrIfConvertResult IrIfConvertResult;
struct IrIfConvertResult
{
    IrFunction function;
    uint32_t regions;
    uint32_t selects;
    // Regions whose join kept other predecessors, their selects new rows in the head.
    uint32_t folded;
    // Arm rows that took an earlier row's value instead of moving, and the arm loads reused or speculated.
    uint32_t reused;
    uint32_t loads;
    // Accumulations whose select sank below their operation, and the candidate regions each round left as branches
    // for a recurrence through their condition.
    uint32_t sunk;
    uint32_t recurrences;
    uint32_t rounds;
};

typedef struct IrIfConvertRegion IrIfConvertRegion;
struct IrIfConvertRegion
{
    uint32_t head;
    uint32_t join;
    // The blocks whose edges into the join carry the true and false values: an arm's last block, or the head.
    uint32_t true_from;
    uint32_t false_from;
    // The region's rows moving to the head, a slice of IrIfConvertWork.moved.
    uint32_t moved;
    uint32_t moved_count;
    uint32_t phis;
    // The region's new rows, a slice of IrIfConvertWork.fresh: its sunk accumulations' zeros, then a folded join's
    // selects, the first of which is `selects` (BUSTER_IR_NONE where the join's phis become the selects).
    uint32_t fresh;
    uint32_t zeros;
    uint32_t selects;
};

typedef struct IrIfConvertFresh IrIfConvertFresh;
struct IrIfConvertFresh
{
    // A select's phi, or a zero's operation row and the phi it accumulates.
    uint32_t row;
    uint32_t phi;
    uint32_t kind;
};

typedef struct IrIfConvertChain IrIfConvertChain;
struct IrIfConvertChain
{
    uint32_t blocks[BUSTER_IR_IF_CONVERT_CHAIN_LIMIT];
    uint32_t count;
    // The block the chain's last unconditional branch reaches, or BUSTER_IR_NONE where the side is no arm.
    uint32_t end;
};

typedef struct IrIfConvertKey IrIfConvertKey;
struct IrIfConvertKey
{
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t fields;
    uint8_t op;
    uint8_t type;
    uint8_t flags;
};

typedef struct IrIfConvertWork IrIfConvertWork;
struct IrIfConvertWork
{
    const IrFunction* function;
    IrCfg cfg;
    IrIfConvertRegion* regions;
    uint32_t* heads;
    uint32_t* joins;
    // A block's reachable predecessors' count, and the only one where there is one.
    uint32_t* reaching;
    uint32_t* single;
    // Every row's representative: itself, or the earlier row whose value an arm row takes.
    uint32_t* alias;
    uint32_t* moved;
    IrIfConvertFresh* fresh;
    // A sunk accumulation's phi: its operation row; the operation row: its zero's index in `fresh`.
    uint32_t* sunk;
    // 1 for an accepted region's arm block, 2 for the candidate's while it is analyzed.
    uint8_t* arms;
    uint8_t* reachable;
    // An arm row the join's phis need, which moves to the head.
    uint8_t* moving;
    uint32_t region_count;
    uint32_t select_count;
    uint32_t moved_count;
    uint32_t fresh_count;
    // A zero in the constant table, or BUSTER_IR_NONE: a sunk accumulation's zero rows name it.
    uint32_t zero;
    uint32_t folded;
    uint32_t reused;
    uint32_t loads;
    uint32_t sunk_count;
    uint32_t recurrences;
    // An accepted head could be an arm of an outer region in the next round.
    bool nested;
    bool scalar;
};

_Static_assert(IR_OP_COUNT == 66, "a new operation needs its class in ir_if_convert_barriers");
// Everything not listed is neither: pure operations, constants, plain loads (an observable one is a write, below),
// phis, allocation and the stack save. A call may free or unmap memory; a stack restore and a lifetime's end release a
// frame's; IR_INVALID never verifies but stays conservative.
static const uint8_t ir_if_convert_barriers[BUSTER_IR_OP_TABLE] = {
    [IR_INVALID] = BUSTER_IR_IF_CONVERT_WRITE | BUSTER_IR_IF_CONVERT_RELEASE,
    [IR_STORE] = BUSTER_IR_IF_CONVERT_WRITE,
    [IR_MASKED_STORE] = BUSTER_IR_IF_CONVERT_WRITE,
    [IR_ATOMIC_RMW] = BUSTER_IR_IF_CONVERT_WRITE,
    [IR_FENCE] = BUSTER_IR_IF_CONVERT_WRITE,
    [IR_CALL] = BUSTER_IR_IF_CONVERT_WRITE | BUSTER_IR_IF_CONVERT_RELEASE,
    [IR_CALL_INDIRECT] = BUSTER_IR_IF_CONVERT_WRITE | BUSTER_IR_IF_CONVERT_RELEASE,
    [IR_STACK_RESTORE] = BUSTER_IR_IF_CONVERT_WRITE | BUSTER_IR_IF_CONVERT_RELEASE,
    [IR_LIFETIME] = BUSTER_IR_IF_CONVERT_WRITE | BUSTER_IR_IF_CONVERT_RELEASE,
};

// The speculation cost of the total operations; zero for a constant. Every admitted operation is total on the admitted
// operand types; integer arithmetic wraps in IR, without signed-overflow flags.
static const uint8_t ir_if_convert_costs[BUSTER_IR_OP_TABLE] = {
    [IR_COPY] = 1,
    [IR_ADD] = 1,
    [IR_SUB] = 1,
    [IR_MUL] = 3,
    [IR_AND] = 1,
    [IR_OR] = 1,
    [IR_XOR] = 1,
    [IR_SHL] = 1,
    [IR_LSHR] = 1,
    [IR_ASHR] = 1,
    [IR_NEG] = 1,
    [IR_NOT] = 1,
    [IR_ICMP] = 1,
    [IR_SELECT] = 2,
    [IR_TRUNC] = 1,
    [IR_ZEXT] = 1,
    [IR_SEXT] = 1,
    [IR_INTTOPTR] = 1,
    [IR_PTRTOINT] = 1,
    [IR_BITCAST] = 1,
    [IR_STACK_ADDR] = 1,
    [IR_SYMBOL_ADDR] = 1,
    [IR_ADDRESS] = 1,
};

// The operations an arm row may take an earlier row's value for: the total ones but constants (free and kept), the
// divisions (an equal division already ran without faulting) and loads (behind the head's writes, below).
static const uint8_t ir_if_convert_numbered[BUSTER_IR_OP_TABLE] = {
    [IR_COPY] = 1,
    [IR_ADD] = 1,
    [IR_SUB] = 1,
    [IR_MUL] = 1,
    [IR_SDIV] = 1,
    [IR_UDIV] = 1,
    [IR_SREM] = 1,
    [IR_UREM] = 1,
    [IR_AND] = 1,
    [IR_OR] = 1,
    [IR_XOR] = 1,
    [IR_SHL] = 1,
    [IR_LSHR] = 1,
    [IR_ASHR] = 1,
    [IR_NEG] = 1,
    [IR_NOT] = 1,
    [IR_ICMP] = 1,
    [IR_SELECT] = 1,
    [IR_TRUNC] = 1,
    [IR_ZEXT] = 1,
    [IR_SEXT] = 1,
    [IR_INTTOPTR] = 1,
    [IR_PTRTOINT] = 1,
    [IR_BITCAST] = 1,
    [IR_STACK_ADDR] = 1,
    [IR_SYMBOL_ADDR] = 1,
    [IR_ADDRESS] = 1,
    [IR_LOAD] = 1,
};

static uint64_t ir_if_convert_candidates(const uint8_t* operations, uint32_t count, bool scalar)
{
    uint64_t candidates = 0;
    if (count != 0 && count <= BUSTER_IR_IF_CONVERT_BATCH)
    {
#if BUSTER_HOST_VECTOR
        if (!scalar)
        {
            const __mmask64 valid = UINT64_MAX >> (BUSTER_IR_IF_CONVERT_BATCH - count);
            const __m512i op = _mm512_maskz_loadu_epi8(valid, operations);
            candidates = valid & _mm512_cmpeq_epi8_mask(op, _mm512_set1_epi8(IR_CBR));
        }
        else
#endif
        {
            for (uint32_t lane = 0; lane < count; lane += 1)
            {
                candidates |= (uint64_t)(operations[lane] == IR_CBR) << lane;
            }
        }
    }
    (void)scalar;
    return candidates;
}

static bool ir_if_convert_scalar_type(uint32_t type)
{
    return (type >= IR_I1 && type <= IR_I64) || type == IR_PTR;
}

// The last row of [first, end) whose class has one of `classes`' bits, or BUSTER_IR_NONE. Its vector twin looks the
// classes up 64 opcodes at a time (vpermi2b over the 128-entry table) and tests the observable loads' flags beside.
static uint32_t ir_if_convert_barrier(const IrFunction* f, uint32_t first, uint32_t end, uint32_t classes, bool scalar)
{
    uint32_t found = BUSTER_IR_NONE;
#if BUSTER_HOST_VECTOR
    if (!scalar)
    {
        const __m512i low = _mm512_loadu_si512(ir_if_convert_barriers);
        const __m512i high = _mm512_loadu_si512(ir_if_convert_barriers + 64);
        uint32_t limit = end;
        while (limit > first && found == BUSTER_IR_NONE)
        {
            const uint32_t start = limit - first > BUSTER_IR_IF_CONVERT_BATCH ? limit - BUSTER_IR_IF_CONVERT_BATCH : first;
            const __mmask64 valid = UINT64_MAX >> (BUSTER_IR_IF_CONVERT_BATCH - (limit - start));
            const __m512i op = _mm512_maskz_loadu_epi8(valid, f->op + start);
            const __m512i flags = _mm512_maskz_loadu_epi8(valid, f->flags + start);
            const __m512i class = _mm512_permutex2var_epi8(low, op, high);
            uint64_t hits = _mm512_mask_test_epi8_mask(valid, class, _mm512_set1_epi8((char)classes));
            if ((classes & BUSTER_IR_IF_CONVERT_WRITE) != 0)
            {
                hits |= _mm512_mask_cmpeq_epi8_mask(valid, op, _mm512_set1_epi8(IR_LOAD)) & _mm512_test_epi8_mask(flags, _mm512_set1_epi8(BUSTER_IR_OBSERVABLE));
            }
            found = hits != 0 ? start + 63 - (uint32_t)__builtin_clzll(hits) : BUSTER_IR_NONE;
            limit = start;
        }
    }
    else
#endif
    {
        for (uint32_t i = end; i > first && found == BUSTER_IR_NONE; i -= 1)
        {
            const uint32_t row = i - 1;
            const uint32_t op = f->op[row];
            const bool observable = op == IR_LOAD && (f->flags[row] & BUSTER_IR_OBSERVABLE) != 0 && (classes & BUSTER_IR_IF_CONVERT_WRITE) != 0;
            found = (ir_if_convert_barriers[op] & classes) != 0 || observable ? row : BUSTER_IR_NONE;
        }
    }
    (void)scalar;
    return found;
}

// The rows of [first, first + count), count 1 to 16, that pass a key's filter, as bits; the caller checks each.
static uint32_t ir_if_convert_filter(const IrFunction* f, uint32_t first, uint32_t count, IrIfConvertKey key, bool scalar)
{
    uint32_t matches = 0;
#if BUSTER_HOST_VECTOR
    if (!scalar)
    {
        const __mmask16 valid = (__mmask16)(0xffffu >> (BUSTER_IR_IF_CONVERT_LANES - count));
        __mmask16 m = _mm_mask_cmpeq_epi8_mask(valid, _mm_maskz_loadu_epi8(valid, f->op + first), _mm_set1_epi8((char)key.op));
        if ((key.fields & BUSTER_IR_IF_CONVERT_KEY_TYPE) != 0)
        {
            m &= _mm_cmpeq_epi8_mask(_mm_maskz_loadu_epi8(valid, f->type + first), _mm_set1_epi8((char)key.type));
        }
        if ((key.fields & BUSTER_IR_IF_CONVERT_KEY_FLAGS) != 0)
        {
            m &= _mm_cmpeq_epi8_mask(_mm_maskz_loadu_epi8(valid, f->flags + first), _mm_set1_epi8((char)key.flags));
        }
        if ((key.fields & BUSTER_IR_IF_CONVERT_KEY_A) != 0)
        {
            m &= _mm512_cmpeq_epi32_mask(_mm512_maskz_loadu_epi32(valid, f->a + first), _mm512_set1_epi32((int)key.a));
        }
        if ((key.fields & BUSTER_IR_IF_CONVERT_KEY_B) != 0)
        {
            m &= _mm512_cmpeq_epi32_mask(_mm512_maskz_loadu_epi32(valid, f->b + first), _mm512_set1_epi32((int)key.b));
        }
        if ((key.fields & BUSTER_IR_IF_CONVERT_KEY_C) != 0)
        {
            m &= _mm512_cmpeq_epi32_mask(_mm512_maskz_loadu_epi32(valid, f->c + first), _mm512_set1_epi32((int)key.c));
        }
        matches = m;
    }
    else
#endif
    {
        for (uint32_t lane = 0; lane < count; lane += 1)
        {
            const uint32_t row = first + lane;
            bool match = f->op[row] == key.op;
            match = match && ((key.fields & BUSTER_IR_IF_CONVERT_KEY_TYPE) == 0 || f->type[row] == key.type);
            match = match && ((key.fields & BUSTER_IR_IF_CONVERT_KEY_FLAGS) == 0 || f->flags[row] == key.flags);
            match = match && ((key.fields & BUSTER_IR_IF_CONVERT_KEY_A) == 0 || f->a[row] == key.a);
            match = match && ((key.fields & BUSTER_IR_IF_CONVERT_KEY_B) == 0 || f->b[row] == key.b);
            match = match && ((key.fields & BUSTER_IR_IF_CONVERT_KEY_C) == 0 || f->c[row] == key.c);
            matches |= (uint32_t)match << lane;
        }
    }
    (void)scalar;
    return matches;
}

// Whether row q computes row r's value: the same operation, type, flags and fields, value operands compared by their
// representatives, an address's scale and offset by content. A load's alignment doesn't change its value.
static bool ir_if_convert_same(const IrIfConvertWork* work, uint32_t r, uint32_t q)
{
    const IrFunction* const f = work->function;
    const uint32_t* const alias = work->alias;
    const uint32_t op = f->op[r];
    const uint32_t mask = ir_value_mask(f, r);
    bool same = f->op[q] == op && f->type[q] == f->type[r] && f->flags[q] == f->flags[r] && ir_value_mask(f, q) == mask;
    same = same && (op == IR_LOAD || f->alignment_log2[q] == f->alignment_log2[r]);
    same = same && ((mask & 1) != 0 ? alias[f->a[r]] == alias[f->a[q]] : f->a[r] == f->a[q]);
    same = same && ((mask & 2) != 0 ? alias[f->b[r]] == alias[f->b[q]] : f->b[r] == f->b[q]);
    if (same && op == IR_ADDRESS)
    {
        const IrAddress left = f->addresses[f->c[r]];
        const IrAddress right = f->addresses[f->c[q]];
        same = left.offset == right.offset && left.scale == right.scale;
    }
    else
    {
        same = same && ((mask & 4) != 0 ? alias[f->c[r]] == alias[f->c[q]] : f->c[r] == f->c[q]);
    }
    return same;
}

// The value operand `field` (0 a, 1 b, 2 c) of row r by its representative, or its raw field.
static uint32_t ir_if_convert_field(const IrIfConvertWork* work, uint32_t r, uint32_t field)
{
    const IrFunction* const f = work->function;
    const uint32_t raw = field == 0 ? f->a[r] : field == 1 ? f->b[r] : f->c[r];
    return (ir_value_mask(f, r) & (1u << field)) != 0 ? work->alias[raw] : raw;
}

// The last row of [first, end) that computes row r's value, or BUSTER_IR_NONE; candidates come 16 rows at a time,
// latest first, through the filter, and each is checked whole.
static uint32_t ir_if_convert_equal(const IrIfConvertWork* work, uint32_t first, uint32_t end, uint32_t r)
{
    const IrFunction* const f = work->function;
    const IrIfConvertKey key = {
        .a = ir_if_convert_field(work, r, 0),
        .b = ir_if_convert_field(work, r, 1),
        .c = ir_if_convert_field(work, r, 2),
        .fields = BUSTER_IR_IF_CONVERT_KEY_A | BUSTER_IR_IF_CONVERT_KEY_B | BUSTER_IR_IF_CONVERT_KEY_TYPE | BUSTER_IR_IF_CONVERT_KEY_FLAGS |
                  (f->op[r] == IR_ADDRESS ? 0 : BUSTER_IR_IF_CONVERT_KEY_C),
        .op = f->op[r],
        .type = f->type[r],
        .flags = f->flags[r],
    };
    uint32_t found = BUSTER_IR_NONE;
    uint32_t limit = end;
    while (limit > first && found == BUSTER_IR_NONE)
    {
        const uint32_t start = limit - first > BUSTER_IR_IF_CONVERT_LANES ? limit - BUSTER_IR_IF_CONVERT_LANES : first;
        uint32_t matches = ir_if_convert_filter(f, start, limit - start, key, work->scalar);
        while (matches != 0 && found == BUSTER_IR_NONE)
        {
            const uint32_t lane = 31 - (uint32_t)__builtin_clz(matches);
            found = ir_if_convert_same(work, r, start + lane) ? start + lane : BUSTER_IR_NONE;
            matches &= ~(1u << lane);
        }
        limit = start;
    }
    return found;
}

// Whether [first, end) holds a plain load or store of `address` of at least `bits`.
static bool ir_if_convert_accessed(const IrIfConvertWork* work, uint32_t first, uint32_t end, uint32_t address, uint32_t bits)
{
    const IrFunction* const f = work->function;
    IrIfConvertKey key = {
        .a = address,
        .fields = BUSTER_IR_IF_CONVERT_KEY_A | BUSTER_IR_IF_CONVERT_KEY_FLAGS,
        .op = IR_LOAD,
    };
    bool found = false;
    uint32_t limit = end;
    while (limit > first && !found)
    {
        const uint32_t start = limit - first > BUSTER_IR_IF_CONVERT_LANES ? limit - BUSTER_IR_IF_CONVERT_LANES : first;
        key.op = IR_LOAD;
        uint32_t matches = ir_if_convert_filter(f, start, limit - start, key, work->scalar);
        key.op = IR_STORE;
        matches |= ir_if_convert_filter(f, start, limit - start, key, work->scalar);
        while (matches != 0 && !found)
        {
            const uint32_t q = start + (uint32_t)__builtin_ctz(matches);
            const uint32_t type = f->op[q] == IR_LOAD ? f->type[q] : f->type[f->b[q]];
            found = ir_type_bits[type] >= bits;
            matches &= matches - 1;
        }
        limit = start;
    }
    return found;
}

// An arm row's speculation cost, or BUSTER_IR_IF_CONVERT_UNSAFE. A load costs one where the head's access proves its
// address; `proven` is the row after the head's last release (or its first).
static uint32_t ir_if_convert_cost(const IrIfConvertWork* work, uint32_t i, uint32_t proven, uint32_t terminator)
{
    const IrFunction* const f = work->function;
    const uint32_t op = f->op[i];
    const bool load = op == IR_LOAD && f->flags[i] == 0;
    uint32_t cost = op == IR_CONST ? 0 : load ? 1 : ir_if_convert_costs[op] != 0 ? ir_if_convert_costs[op] : BUSTER_IR_IF_CONVERT_UNSAFE;
    bool safe = cost != BUSTER_IR_IF_CONVERT_UNSAFE && ir_if_convert_scalar_type(f->type[i]) && (f->flags[i] == 0 || load);
    const uint32_t mask = ir_value_mask(f, i);
    safe = safe && ((mask & 1) == 0 || ir_if_convert_scalar_type(f->type[f->a[i]]));
    safe = safe && ((mask & 2) == 0 || ir_if_convert_scalar_type(f->type[f->b[i]]));
    safe = safe && ((mask & 4) == 0 || ir_if_convert_scalar_type(f->type[f->c[i]]));
    if (safe && (op == IR_SHL || op == IR_LSHR || op == IR_ASHR))
    {
        const uint32_t count = work->alias[f->b[i]];
        safe = f->op[count] == IR_CONST;
        if (safe)
        {
            const IrConstant bits = f->constants[f->a[count]];
            safe = bits.high == 0 && bits.low < ir_type_bits[f->type[i]];
        }
    }
    if (safe && load)
    {
        safe = ir_if_convert_accessed(work, proven, terminator, work->alias[f->a[i]], ir_type_bits[f->type[i]]);
    }
    return safe ? cost : BUSTER_IR_IF_CONVERT_UNSAFE;
}

// The side of a region from `start`: the chain of blocks to the first block another edge reaches too, its `end`.
static IrIfConvertChain ir_if_convert_chain(const IrIfConvertWork* work, uint32_t head, uint32_t start)
{
    const IrFunction* const f = work->function;
    IrIfConvertChain chain = {
        .end = BUSTER_IR_NONE,
    };
    uint32_t block = start;
    uint32_t from = head;
    uint32_t rows = 0;
    bool going = true;
    while (going)
    {
        const uint32_t first = f->block_offsets[block];
        const uint32_t last = f->block_offsets[(uint64_t)block + 1] - 1;
        rows += last - first;
        const bool member = chain.count < BUSTER_IR_IF_CONVERT_CHAIN_LIMIT && block != 0 && block != head && work->reachable[block] != 0 &&
                            work->single[block] == from && work->arms[block] == 0 && work->heads[block] == 0 && work->joins[block] == 0 &&
                            f->op[last] == IR_BR && rows <= BUSTER_IR_IF_CONVERT_ARM_ROWS;
        going = member;
        if (member)
        {
            chain.blocks[chain.count++] = block;
            const uint32_t next = f->a[last];
            from = block;
            chain.end = work->single[next] != block ? next : BUSTER_IR_NONE;
            going = chain.end == BUSTER_IR_NONE;
            block = next;
        }
        else
        {
            chain.end = BUSTER_IR_NONE;
            chain.count = 0;
        }
    }
    return chain;
}

// The value phi row `phi` takes on the edge from `block`: its first entry from there, the scan stopping at it. Without
// the early exit, clang vectorized the scan of the entries' alternate words into masked gathers, whose unselected
// lanes MemorySanitizer reads as uninitialized (the merge queue's MSan shard on #1382's sparse switches).
static uint32_t ir_if_convert_incoming(const IrFunction* f, uint32_t phi, uint32_t block)
{
    const uint32_t first = f->a[phi];
    const uint32_t count = f->b[phi];
    uint32_t value = BUSTER_IR_NONE;
    for (uint32_t p = 0; p < count && value == BUSTER_IR_NONE; p += 1)
    {
        value = f->extra[first + p * 2] == block ? f->extra[first + p * 2 + 1] : BUSTER_IR_NONE;
    }
    return value;
}

static uint32_t ir_if_convert_occurrences(const IrCfg* cfg, uint32_t block, uint32_t predecessor)
{
    uint32_t count = 0;
    for (uint32_t e = cfg->predecessor_offsets[block]; e < cfg->predecessor_offsets[(uint64_t)block + 1]; e += 1)
    {
        count += cfg->predecessors[e] == predecessor;
    }
    return count;
}

// Whether the head's condition reads, within a few rows, a phi that one of the join's phis flows into: a recurrence
// such as a running maximum's. Its select would put the comparison on the loop's carried chain (cmp, setcc, test and
// cmov per element on the 9700X), where a predicted branch keeps it off; LLVM's x86 cmov conversion turns such a
// cmov back into a branch after selection, and clang -O2 keeps a branch there.
static bool ir_if_convert_recurrence(const IrFunction* f, uint32_t condition, uint32_t join_first, uint32_t phis)
{
    uint32_t stack[BUSTER_IR_IF_CONVERT_RECURRENCE_ROWS];
    uint32_t seen[BUSTER_IR_IF_CONVERT_RECURRENCE_ROWS];
    uint32_t depth = 1;
    uint32_t visited = 0;
    bool found = false;
    stack[0] = condition;
    while (depth != 0 && !found && visited < BUSTER_IR_IF_CONVERT_RECURRENCE_ROWS)
    {
        const uint32_t row = stack[--depth];
        bool known = false;
        for (uint32_t k = 0; k < visited; k += 1)
        {
            known = known || seen[k] == row;
        }
        if (!known)
        {
            seen[visited++] = row;
            if (f->op[row] == IR_PHI)
            {
                for (uint32_t p = 0; p < f->b[row]; p += 1)
                {
                    const uint32_t value = f->extra[f->a[row] + p * 2 + 1];
                    found = found || (value >= join_first && value < join_first + phis);
                }
            }
            else
            {
                const uint32_t mask = ir_value_mask(f, row);
                const uint32_t fields[3] = {
                    f->a[row],
                    f->b[row],
                    f->c[row],
                };
                for (uint32_t field = 0; field < 3; field += 1)
                {
                    if ((mask & (1u << field)) != 0 && depth < BUSTER_IR_IF_CONVERT_RECURRENCE_ROWS)
                    {
                        stack[depth++] = fields[field];
                    }
                }
            }
        }
    }
    return found;
}

// Whether an arm row other than `r`, among the moving ones, or a join phi other than `phi` reads r's value.
static bool ir_if_convert_shared(const IrIfConvertWork* work, const uint32_t* rows, uint32_t row_count, uint32_t join_first, uint32_t phis, uint32_t phi, uint32_t r)
{
    const IrFunction* const f = work->function;
    bool shared = false;
    for (uint32_t k = 0; k < row_count && !shared; k += 1)
    {
        const uint32_t q = rows[k];
        const uint32_t mask = q != r && work->moving[q] != 0 ? ir_value_mask(f, q) : 0;
        for (uint32_t field = 0; field < 3; field += 1)
        {
            shared = shared || ((mask & (1u << field)) != 0 && ir_if_convert_field(work, q, field) == r);
        }
    }
    for (uint32_t other = join_first; other < join_first + phis && !shared; other += 1)
    {
        for (uint32_t p = 0; other != phi && p < f->b[other]; p += 1)
        {
            shared = shared || work->alias[f->extra[f->a[other] + p * 2 + 1]] == r;
        }
    }
    return shared;
}

// An in-place phi whose arm value is its other value s combined with x, `s op x` (an addition, a subtraction from s,
// an or or a xor of an integer), and which nothing else reads: the operation's row selects x or zero instead, and the
// phi becomes `s op select`, so that a loop's accumulator waits for one operation, not for the select as well
// (InstCombine's fold of a select of a binary operation; clang -O2's conditional sums add a cmov of zero).
static void ir_if_convert_sink(IrIfConvertWork* work, const uint32_t* rows, uint32_t row_count, uint32_t join_first, uint32_t phis, uint32_t true_from, uint32_t false_from)
{
    const IrFunction* const f = work->function;
    const IrCfg* const cfg = &work->cfg;
    for (uint32_t phi = join_first; work->zero != BUSTER_IR_NONE && phi < join_first + phis; phi += 1)
    {
        const uint32_t values[2] = {
            work->alias[ir_if_convert_incoming(f, phi, true_from)],
            work->alias[ir_if_convert_incoming(f, phi, false_from)],
        };
        uint32_t chosen = BUSTER_IR_NONE;
        uint32_t side = 0;
        for (uint32_t s = 0; s < 2 && chosen == BUSTER_IR_NONE; s += 1)
        {
            const uint32_t r = values[s];
            const uint32_t other = values[1 - s];
            const uint32_t op = f->op[r];
            const uint32_t type = f->type[r];
            bool candidate = work->moving[r] != 0 && work->arms[cfg->instruction_blocks[r]] == 2 && work->arms[cfg->instruction_blocks[other]] != 2 &&
                             work->sunk[r] == BUSTER_IR_NONE && (op == IR_ADD || op == IR_SUB || op == IR_OR || op == IR_XOR) && type == f->type[phi] &&
                             type >= IR_I8 && type <= IR_I64;
            candidate = candidate && (work->alias[f->a[r]] == other || (op != IR_SUB && work->alias[f->b[r]] == other));
            candidate = candidate && !ir_if_convert_shared(work, rows, row_count, join_first, phis, phi, r);
            chosen = candidate ? r : BUSTER_IR_NONE;
            side = s;
        }
        if (chosen != BUSTER_IR_NONE)
        {
            work->fresh[work->fresh_count] = (IrIfConvertFresh){
                .row = chosen,
                .phi = phi,
                .kind = side == 0 ? BUSTER_IR_IF_CONVERT_FRESH_ZERO_TRUE : BUSTER_IR_IF_CONVERT_FRESH_ZERO_FALSE,
            };
            work->sunk[chosen] = work->fresh_count;
            work->sunk[phi] = chosen;
            work->fresh_count += 1;
            work->sunk_count += 1;
        }
    }
}

static void ir_if_convert_region(IrIfConvertWork* work, uint32_t branch)
{
    const IrFunction* const f = work->function;
    const IrCfg* const cfg = &work->cfg;
    const uint32_t head = cfg->instruction_blocks[branch];
    const uint32_t yes = f->b[branch];
    const uint32_t no = f->c[branch];
    bool safe = yes != no && yes != head && no != head && work->reachable[head] != 0 && work->arms[head] == 0 && work->heads[head] == 0;
    IrIfConvertChain sides[2] = {
        {
            .end = BUSTER_IR_NONE,
        },
        {
            .end = BUSTER_IR_NONE,
        },
    };
    uint32_t join = BUSTER_IR_NONE;
    if (safe)
    {
        sides[0] = ir_if_convert_chain(work, head, yes);
        sides[1] = ir_if_convert_chain(work, head, no);
        // A triangle's direct edge takes precedence over a diamond; neither can include a self edge.
        if (sides[0].end == no)
        {
            join = no;
            sides[1] = (IrIfConvertChain){
                .end = no,
            };
        }
        else if (sides[1].end == yes)
        {
            join = yes;
            sides[0] = (IrIfConvertChain){
                .end = yes,
            };
        }
        else if (sides[0].end != BUSTER_IR_NONE && sides[0].end == sides[1].end)
        {
            join = sides[0].end;
        }
    }
    const uint32_t true_from = sides[0].count != 0 ? sides[0].blocks[sides[0].count - 1] : head;
    const uint32_t false_from = sides[1].count != 0 ? sides[1].blocks[sides[1].count - 1] : head;
    safe = safe && join != BUSTER_IR_NONE && join != 0 && join != head && work->reachable[join] != 0 && work->arms[join] == 0 && work->joins[join] == 0;
    // The two edges each come once; a join with more predecessors keeps its phis, with one entry for both.
    const uint32_t predecessors = safe ? cfg->predecessor_offsets[(uint64_t)join + 1] - cfg->predecessor_offsets[join] : 0;
    safe = safe && ir_if_convert_occurrences(cfg, join, true_from) == 1 && ir_if_convert_occurrences(cfg, join, false_from) == 1;
    const bool fold = predecessors > 2;
    const uint32_t join_first = safe ? f->block_offsets[join] : 0;
    uint32_t phis = 0;
    for (uint32_t i = join_first; safe && i < f->block_offsets[(uint64_t)join + 1] && f->op[i] == IR_PHI; i += 1)
    {
        phis += 1;
        safe = phis <= BUSTER_IR_IF_CONVERT_PHI_LIMIT && f->b[i] == predecessors && ir_if_convert_scalar_type(f->type[i]);
    }
    safe = safe && phis != 0;
    const bool recurrence = safe && ir_if_convert_recurrence(f, f->a[branch], join_first, phis);
    work->recurrences += recurrence;
    safe = safe && !recurrence;
    // Mark the candidate's arm blocks, and list their rows, the true side's first: the order they move in.
    uint32_t rows[2 * BUSTER_IR_IF_CONVERT_ARM_ROWS];
    uint8_t side_of[2 * BUSTER_IR_IF_CONVERT_ARM_ROWS];
    uint32_t row_count = 0;
    for (uint32_t s = 0; safe && s < 2; s += 1)
    {
        for (uint32_t k = 0; k < sides[s].count; k += 1)
        {
            const uint32_t block = sides[s].blocks[k];
            work->arms[block] = 2;
            for (uint32_t i = f->block_offsets[block]; i + 1 < f->block_offsets[(uint64_t)block + 1]; i += 1)
            {
                side_of[row_count] = (uint8_t)s;
                rows[row_count++] = i;
            }
        }
    }
    const uint32_t terminator = f->block_offsets[(uint64_t)head + 1] - 1;
    const uint32_t head_first = f->block_offsets[head];
    uint32_t written = BUSTER_IR_NONE;
    uint32_t released = BUSTER_IR_NONE;
    if (safe && row_count != 0)
    {
        written = ir_if_convert_barrier(f, head_first, terminator, BUSTER_IR_IF_CONVERT_WRITE, work->scalar);
        released = ir_if_convert_barrier(f, head_first, terminator, BUSTER_IR_IF_CONVERT_RELEASE, work->scalar);
    }
    const uint32_t readable = written == BUSTER_IR_NONE ? head_first : written + 1;
    const uint32_t proven = released == BUSTER_IR_NONE ? head_first : released + 1;
    // Number the arm rows against the earlier arm rows, then the head's (a load's behind its last write).
    uint32_t reused = 0;
    uint32_t loads = 0;
    for (uint32_t k = 0; safe && k < row_count; k += 1)
    {
        const uint32_t r = rows[k];
        const uint32_t op = f->op[r];
        uint32_t representative = BUSTER_IR_NONE;
        if (ir_if_convert_numbered[op] != 0 && (op != IR_LOAD || f->flags[r] == 0))
        {
            for (uint32_t j = k; j > 0 && representative == BUSTER_IR_NONE; j -= 1)
            {
                const uint32_t q = rows[j - 1];
                representative = work->alias[q] == q && ir_if_convert_same(work, r, q) ? q : BUSTER_IR_NONE;
            }
            if (representative == BUSTER_IR_NONE)
            {
                representative = ir_if_convert_equal(work, op == IR_LOAD ? readable : head_first, terminator, r);
            }
        }
        if (representative != BUSTER_IR_NONE)
        {
            work->alias[r] = representative;
            reused += 1;
            loads += op == IR_LOAD;
        }
    }
    // The rows the phis need, through their represented operands, in reverse: a row's operands come before it.
    for (uint32_t i = join_first; safe && i < join_first + phis; i += 1)
    {
        const uint32_t incoming[2] = {
            ir_if_convert_incoming(f, i, true_from),
            ir_if_convert_incoming(f, i, false_from),
        };
        safe = incoming[0] != BUSTER_IR_NONE && incoming[1] != BUSTER_IR_NONE;
        for (uint32_t s = 0; safe && s < 2; s += 1)
        {
            const uint32_t value = work->alias[incoming[s]];
            if (work->arms[cfg->instruction_blocks[value]] == 2)
            {
                work->moving[value] = 1;
            }
        }
    }
    for (uint32_t k = row_count; safe && k > 0; k -= 1)
    {
        const uint32_t r = rows[k - 1];
        const uint32_t mask = work->moving[r] != 0 ? ir_value_mask(f, r) : 0;
        for (uint32_t field = 0; field < 3; field += 1)
        {
            if ((mask & (1u << field)) != 0)
            {
                const uint32_t value = ir_if_convert_field(work, r, field);
                if (work->arms[cfg->instruction_blocks[value]] == 2)
                {
                    work->moving[value] = 1;
                }
            }
        }
    }
    // Every row that moves must be total (or a proven load); every row that stays must be one that could have moved,
    // or a reused one, so that the stub drops no effect and no fault.
    uint32_t cost = phis * 2;
    uint32_t moving_rows[2] = {
        0,
        0,
    };
    for (uint32_t k = 0; safe && k < row_count; k += 1)
    {
        const uint32_t r = rows[k];
        if (work->alias[r] == r)
        {
            const uint32_t row_cost = ir_if_convert_cost(work, r, proven, terminator);
            safe = row_cost != BUSTER_IR_IF_CONVERT_UNSAFE;
            if (safe && work->moving[r] != 0)
            {
                cost += row_cost;
                moving_rows[side_of[k]] += f->op[r] != IR_CONST;
                loads += f->op[r] == IR_LOAD;
            }
        }
    }
    safe = safe && cost <= BUSTER_IR_IF_CONVERT_COST_LIMIT && moving_rows[0] <= BUSTER_IR_IF_CONVERT_ARM_LIMIT && moving_rows[1] <= BUSTER_IR_IF_CONVERT_ARM_LIMIT;
    if (safe)
    {
        const uint32_t index = work->region_count;
        IrIfConvertRegion region = {
            .head = head,
            .join = join,
            .true_from = true_from,
            .false_from = false_from,
            .moved = work->moved_count,
            .phis = phis,
            .fresh = work->fresh_count,
        };
        for (uint32_t k = 0; k < row_count; k += 1)
        {
            if (work->moving[rows[k]] != 0)
            {
                work->moved[work->moved_count++] = rows[k];
            }
        }
        region.moved_count = work->moved_count - region.moved;
        if (!fold)
        {
            ir_if_convert_sink(work, rows, row_count, join_first, phis, true_from, false_from);
        }
        region.zeros = work->fresh_count - region.fresh;
        region.selects = fold ? work->fresh_count : BUSTER_IR_NONE;
        for (uint32_t p = 0; fold && p < phis; p += 1)
        {
            work->fresh[work->fresh_count++] = (IrIfConvertFresh){
                .row = join_first + p,
                .phi = join_first + p,
                .kind = BUSTER_IR_IF_CONVERT_FRESH_SELECT,
            };
        }
        work->regions[index] = region;
        work->heads[head] = index + 1;
        work->joins[join] = index + 1;
        for (uint32_t s = 0; s < 2; s += 1)
        {
            for (uint32_t k = 0; k < sides[s].count; k += 1)
            {
                work->arms[sides[s].blocks[k]] = 1;
            }
        }
        work->region_count = index + 1;
        work->select_count += phis;
        work->folded += fold;
        work->reused += reused;
        work->loads += loads;
        // The head is an arm candidate of an outer region where its single-predecessor chain reaches a conditional
        // branch: the next round may take it.
        uint32_t block = head;
        bool climbing = true;
        for (uint32_t step = 0; climbing && step < BUSTER_IR_IF_CONVERT_CHAIN_LIMIT; step += 1)
        {
            const uint32_t above = work->single[block];
            const uint32_t op = above != BUSTER_IR_NONE ? f->op[f->block_offsets[(uint64_t)above + 1] - 1] : IR_INVALID;
            work->nested = work->nested || op == IR_CBR;
            climbing = op == IR_BR;
            block = above;
        }
    }
    else
    {
        for (uint32_t k = 0; k < row_count; k += 1)
        {
            work->alias[rows[k]] = rows[k];
            work->moving[rows[k]] = 0;
        }
        for (uint32_t s = 0; s < 2; s += 1)
        {
            for (uint32_t k = 0; k < sides[s].count; k += 1)
            {
                work->arms[sides[s].blocks[k]] = work->arms[sides[s].blocks[k]] == 2 ? 0 : work->arms[sides[s].blocks[k]];
            }
        }
    }
}

// Row i of the input as output row `out`: its operands by their representatives' new rows, a head's branch to its
// join, an arm's branch as `unreachable`, a join's phi as its select or with one entry for the region's two, and the
// phis' and calls' value slices appended from `extra` on. The next free extra word.
static uint32_t ir_if_convert_row(const IrIfConvertWork* work, IrFunction* view, const uint32_t* values, const uint32_t* fresh, uint32_t i, uint32_t out, uint32_t extra)
{
    const IrFunction* const f = work->function;
    const uint32_t* const alias = work->alias;
    const uint32_t block = work->cfg.instruction_blocks[i];
    if (view->debug != NULL && work->moving[i] != 0)
    {
        // These former arm instructions now execute speculatively in the head. Their lexical scope cannot
        // be advertised unconditionally just because their expression's original token remains known.
        view->debug->origins[out] = BUSTER_IR_NONE;
    }
    const uint32_t mask = ir_value_mask(f, i);
    view->op[out] = f->op[i];
    view->type[out] = f->type[i];
    view->flags[out] = f->flags[i];
    view->alignment_log2[out] = f->alignment_log2[i];
    view->a[out] = (mask & 1) != 0 ? values[alias[f->a[i]]] : f->a[i];
    view->b[out] = (mask & 2) != 0 ? values[alias[f->b[i]]] : f->b[i];
    view->c[out] = (mask & 4) != 0 ? values[alias[f->c[i]]] : f->c[i];
    const uint32_t join = f->op[i] == IR_PHI ? work->joins[block] : 0;
    const uint32_t sunk = work->sunk[i];
    if (sunk != BUSTER_IR_NONE && f->op[i] != IR_PHI)
    {
        // A sunk accumulation's operation selects its other operand x or zero, on the side the operation was.
        const IrIfConvertFresh zero = work->fresh[sunk];
        const IrIfConvertRegion region = work->regions[work->joins[work->cfg.instruction_blocks[zero.phi]] - 1];
        const bool on_true = zero.kind == BUSTER_IR_IF_CONVERT_FRESH_ZERO_TRUE;
        const uint32_t other = alias[ir_if_convert_incoming(f, zero.phi, on_true ? region.false_from : region.true_from)];
        const uint32_t x = alias[f->a[i]] == other ? alias[f->b[i]] : alias[f->a[i]];
        view->op[out] = IR_SELECT;
        view->a[out] = values[alias[f->a[f->block_offsets[(uint64_t)region.head + 1] - 1]]];
        view->b[out] = on_true ? values[x] : fresh[sunk];
        view->c[out] = on_true ? fresh[sunk] : values[x];
    }
    else if (sunk != BUSTER_IR_NONE)
    {
        // Its phi: the operation of the other value and the select.
        const IrIfConvertFresh zero = work->fresh[work->sunk[sunk]];
        const IrIfConvertRegion region = work->regions[join - 1];
        const bool on_true = zero.kind == BUSTER_IR_IF_CONVERT_FRESH_ZERO_TRUE;
        view->op[out] = f->op[sunk];
        view->a[out] = values[alias[ir_if_convert_incoming(f, i, on_true ? region.false_from : region.true_from)]];
        view->b[out] = values[sunk];
        view->c[out] = BUSTER_IR_NONE;
    }
    else if (f->op[i] == IR_CBR && work->heads[block] != 0)
    {
        view->op[out] = IR_BR;
        view->a[out] = work->regions[work->heads[block] - 1].join;
        view->b[out] = BUSTER_IR_NONE;
        view->c[out] = BUSTER_IR_NONE;
    }
    else if (f->op[i] == IR_BR && work->arms[block] != 0)
    {
        view->op[out] = IR_UNREACHABLE;
        view->a[out] = BUSTER_IR_NONE;
    }
    else if (join != 0 && work->regions[join - 1].selects == BUSTER_IR_NONE)
    {
        const IrIfConvertRegion region = work->regions[join - 1];
        view->op[out] = IR_SELECT;
        view->a[out] = values[alias[f->a[f->block_offsets[(uint64_t)region.head + 1] - 1]]];
        view->b[out] = values[alias[ir_if_convert_incoming(f, i, region.true_from)]];
        view->c[out] = values[alias[ir_if_convert_incoming(f, i, region.false_from)]];
    }
    else if (f->op[i] == IR_PHI)
    {
        // A folded join's phi takes one entry from the head, its select, for the region's two.
        const IrIfConvertRegion* region = join != 0 ? &work->regions[join - 1] : NULL;
        bool placed = false;
        view->a[out] = extra;
        view->b[out] = f->b[i] - (region != NULL);
        for (uint32_t p = 0; p < f->b[i]; p += 1)
        {
            const uint32_t from = f->extra[f->a[i] + p * 2];
            if (region != NULL && (from == region->true_from || from == region->false_from))
            {
                if (!placed)
                {
                    view->extra[extra++] = region->head;
                    view->extra[extra++] = fresh[region->selects + (i - f->block_offsets[region->join])];
                    placed = true;
                }
            }
            else
            {
                view->extra[extra++] = from;
                view->extra[extra++] = values[alias[f->extra[f->a[i] + p * 2 + 1]]];
            }
        }
    }
    else if (f->op[i] == IR_CALL || f->op[i] == IR_CALL_INDIRECT)
    {
        const IrCall call = f->calls[f->b[i]];
        view->calls[f->b[i]].argument_first = extra;
        for (uint32_t a = 0; a < call.argument_count; a += 1)
        {
            view->extra[extra++] = values[alias[f->extra[call.argument_first + a]]];
        }
    }
    else if (ir_ternary_instruction(f, i))
    {
        // A ternary intrinsic's third operand (#1840), in its own appended slot.
        view->c[out] = ir_intrinsic_ternary_c(f->c[i], extra);
        view->extra[extra++] = values[alias[ir_intrinsic_third(f, i)]];
    }
    return extra;
}

static IrError ir_if_convert_rewrite(const IrIfConvertWork* work, Arena* output, Arena* scratch, IrFunction* result)
{
    const IrFunction* const f = work->function;
    const uint32_t* const alias = work->alias;
    const uint64_t mark = output->length;
    const uint32_t count = f->instruction_count;
    const uint64_t total = (uint64_t)count + work->fresh_count;
    const uint32_t blocks = f->block_count;
    bool ok = total < UINT32_MAX;
    uint32_t* order = ir_allocate(scratch, total, 4, &ok);
    uint32_t* values = ir_allocate(scratch, count, 4, &ok);
    uint32_t* fresh = ir_allocate(scratch, work->fresh_count, 4, &ok);
    IrFunction view = *f;
    view.instruction_count = (uint32_t)total;
    view.op = ir_allocate(output, total, 1, &ok);
    view.type = ir_allocate(output, total, 1, &ok);
    view.flags = ir_allocate(output, total, 1, &ok);
    view.alignment_log2 = ir_allocate(output, total, 1, &ok);
    view.a = ir_allocate(output, total, 4, &ok);
    view.b = ir_allocate(output, total, 4, &ok);
    view.c = ir_allocate(output, total, 4, &ok);
    view.block_offsets = ir_allocate(output, (uint64_t)blocks + 1, 4, &ok);
    uint64_t extras = f->extra_count;
    for (uint32_t i = 0; i < count; i += 1)
    {
        const uint32_t join = work->joins[work->cfg.instruction_blocks[i]];
        if (f->op[i] == IR_PHI && (join == 0 || work->regions[join - 1].selects != BUSTER_IR_NONE))
        {
            extras += (uint64_t)(f->b[i] - (join != 0)) * 2;
        }
        else if (f->op[i] == IR_CALL || f->op[i] == IR_CALL_INDIRECT)
        {
            extras += f->calls[f->b[i]].argument_count;
        }
        else if (ir_ternary_instruction(f, i))
        {
            extras += 1;
        }
    }
    IrError error = !ok ? (total < UINT32_MAX ? IR_ERROR_MEMORY : IR_ERROR_LIMIT) : extras > UINT32_MAX ? IR_ERROR_LIMIT : IR_OK;
    if (error == IR_OK)
    {
        view.extra = ir_allocate(output, extras, 4, &ok);
        view.calls = ir_allocate(output, f->call_count, sizeof(IrCall), &ok);
        view.extra_count = (uint32_t)extras;
        error = ok ? IR_OK : IR_ERROR_MEMORY;
    }
    if (error == IR_OK && !ir_debug_create(output, &view, f->debug))
    {
        error = IR_ERROR_MEMORY;
    }
    if (error == IR_OK)
    {
        // Never remap extra in place: a phi's values, a call's arguments and a switch's block/constant indices may
        // overlap. Keep non-value slices unchanged and append a private value slice for each surviving occurrence.
        if (f->extra_count != 0)
        {
            memcpy(view.extra, f->extra, (uint64_t)f->extra_count * 4);
        }
        if (f->call_count != 0)
        {
            memcpy(view.calls, f->calls, (uint64_t)f->call_count * sizeof(IrCall));
        }
        // A head keeps its rows, then takes its sunk accumulations' zeros, its arms' moving rows and a folded region's
        // selects before its branch. An arm keeps the rows that don't move, as an unreachable stub.
        uint32_t next = 0;
        for (uint32_t block = 0; block < blocks; block += 1)
        {
            view.block_offsets[block] = next;
            const uint32_t first = f->block_offsets[block];
            const uint32_t end = f->block_offsets[(uint64_t)block + 1];
            if (work->heads[block] != 0)
            {
                const IrIfConvertRegion region = work->regions[work->heads[block] - 1];
                for (uint32_t i = first; i + 1 < end; i += 1)
                {
                    order[next++] = i;
                }
                for (uint32_t z = 0; z < region.zeros; z += 1)
                {
                    order[next++] = count + region.fresh + z;
                }
                for (uint32_t m = 0; m < region.moved_count; m += 1)
                {
                    order[next++] = work->moved[region.moved + m];
                }
                for (uint32_t p = 0; region.selects != BUSTER_IR_NONE && p < region.phis; p += 1)
                {
                    order[next++] = count + region.selects + p;
                }
                order[next++] = end - 1;
            }
            else
            {
                for (uint32_t i = first; i < end; i += 1)
                {
                    if (work->moving[i] == 0)
                    {
                        order[next++] = i;
                    }
                }
            }
        }
        view.block_offsets[blocks] = next;
        for (uint32_t out = 0; out < total; out += 1)
        {
            if (order[out] < count)
            {
                values[order[out]] = out;
            }
            else
            {
                fresh[order[out] - count] = out;
            }
        }
        ir_debug_remap(f, &view, values, NULL, NULL);
        // A sunk accumulation's operation row now holds its select, not the value a binding of it described.
        for (uint32_t k = 0; view.debug != NULL && work->sunk_count != 0 && k < view.debug->binding_count; k += 1)
        {
            IrDebugBinding* binding = view.debug->bindings + k;
            const uint32_t row = binding->kind == IR_DEBUG_BIND_VALUE && binding->value < total ? order[binding->value] : BUSTER_IR_NONE;
            if (row < count && work->sunk[row] != BUSTER_IR_NONE && f->op[row] != IR_PHI)
            {
                binding->kind = IR_DEBUG_BIND_UNAVAILABLE;
                binding->value = BUSTER_IR_NONE;
            }
        }
        for (uint32_t k = 0; view.debug != NULL && work->sunk_count != 0 && k < view.debug->region_count; k += 1)
        {
            IrDebugRegion* scope = view.debug->regions + k;
            const uint32_t row = scope->kind == IR_DEBUG_BIND_VALUE && scope->value < total ? order[scope->value] : BUSTER_IR_NONE;
            if (row < count && work->sunk[row] != BUSTER_IR_NONE && f->op[row] != IR_PHI)
            {
                scope->kind = IR_DEBUG_BIND_UNAVAILABLE;
                scope->value = BUSTER_IR_NONE;
            }
        }
        uint32_t extra = f->extra_count;
        for (uint32_t out = 0; out < total; out += 1)
        {
            const uint32_t i = order[out];
            if (i >= count)
            {
                // A folded region's select of a phi's two entries, or a sunk accumulation's zero, in the head.
                const IrIfConvertFresh entry = work->fresh[i - count];
                const IrIfConvertRegion region = work->regions[work->joins[work->cfg.instruction_blocks[entry.phi]] - 1];
                const bool select = entry.kind == BUSTER_IR_IF_CONVERT_FRESH_SELECT;
                view.op[out] = select ? IR_SELECT : IR_CONST;
                view.type[out] = f->type[entry.row];
                view.flags[out] = 0;
                view.alignment_log2[out] = 0;
                view.a[out] = select ? values[alias[f->a[f->block_offsets[(uint64_t)region.head + 1] - 1]]] : work->zero;
                view.b[out] = select ? values[alias[ir_if_convert_incoming(f, entry.phi, region.true_from)]] : BUSTER_IR_NONE;
                view.c[out] = select ? values[alias[ir_if_convert_incoming(f, entry.phi, region.false_from)]] : BUSTER_IR_NONE;
                if (view.debug != NULL)
                {
                    view.debug->origins[out] = BUSTER_IR_NONE;
                }
            }
            else
            {
                extra = ir_if_convert_row(work, &view, values, fresh, i, out, extra);
            }
        }
        view.arena_bytes = output->length - mark;
        *result = view;
    }
    return error;
}

// One round over `function`: its regions found, then rewritten into output. A round without regions borrows it.
static IrCheck ir_if_convert_round(const IrFunction* function, uint64_t first_candidate, Arena* output, Arena* scratch, bool scalar, IrIfConvertResult* converted, bool* again)
{
    IrIfConvertWork work = {
        .function = function,
        .scalar = scalar,
    };
    const uint32_t count = function->instruction_count;
    const uint8_t* const operations = function->op;
    IrCheck check = ir_check(ir_cfg_validated(function, scratch, scratch, &work.cfg), BUSTER_IR_NONE, 0);
    bool ok = check.error == IR_OK;
    const uint32_t blocks = function->block_count;
    work.regions = ir_allocate(scratch, blocks, sizeof(IrIfConvertRegion), &ok);
    work.heads = ir_allocate(scratch, blocks, 4, &ok);
    work.joins = ir_allocate(scratch, blocks, 4, &ok);
    work.reaching = ir_allocate(scratch, blocks, 4, &ok);
    work.single = ir_allocate(scratch, blocks, 4, &ok);
    work.arms = ir_allocate(scratch, blocks, 1, &ok);
    work.reachable = ir_allocate(scratch, blocks, 1, &ok);
    work.alias = ir_allocate(scratch, count, 4, &ok);
    work.moved = ir_allocate(scratch, count, 4, &ok);
    work.fresh = ir_allocate(scratch, count, sizeof(IrIfConvertFresh), &ok);
    work.sunk = ir_allocate(scratch, count, 4, &ok);
    work.moving = ir_allocate(scratch, count, 1, &ok);
    uint32_t* queue = ir_allocate(scratch, blocks, 4, &ok);
    if (check.error == IR_OK && !ok)
    {
        check.error = IR_ERROR_MEMORY;
    }
    *again = false;
    if (check.error == IR_OK)
    {
        uint32_t queued = 1;
        queue[0] = 0;
        work.reachable[0] = 1;
        for (uint32_t at = 0; at < queued; at += 1)
        {
            const uint32_t block = queue[at];
            for (uint32_t e = work.cfg.successor_offsets[block]; e < work.cfg.successor_offsets[(uint64_t)block + 1]; e += 1)
            {
                const uint32_t target = work.cfg.successors[e];
                if (work.reachable[target] == 0)
                {
                    work.reachable[target] = 1;
                    queue[queued++] = target;
                }
            }
        }
        for (uint32_t block = 0; block < blocks; block += 1)
        {
            uint32_t reaching = 0;
            uint32_t single = BUSTER_IR_NONE;
            for (uint32_t e = work.cfg.predecessor_offsets[block]; e < work.cfg.predecessor_offsets[(uint64_t)block + 1]; e += 1)
            {
                const uint32_t from = work.cfg.predecessors[e];
                reaching += work.reachable[from];
                single = work.reachable[from] != 0 ? from : single;
            }
            work.reaching[block] = reaching;
            work.single[block] = reaching == 1 ? single : BUSTER_IR_NONE;
        }
        for (uint32_t i = 0; i < count; i += 1)
        {
            work.alias[i] = i;
            work.sunk[i] = BUSTER_IR_NONE;
        }
        work.zero = BUSTER_IR_NONE;
        for (uint32_t k = function->constant_count; k > 0; k -= 1)
        {
            work.zero = function->constants[k - 1].low == 0 && function->constants[k - 1].high == 0 ? k - 1 : work.zero;
        }
        for (uint64_t first = first_candidate; first < count; first += BUSTER_IR_IF_CONVERT_BATCH)
        {
            const uint32_t lanes = count - first < BUSTER_IR_IF_CONVERT_BATCH ? count - (uint32_t)first : BUSTER_IR_IF_CONVERT_BATCH;
            uint64_t candidates = ir_if_convert_candidates(operations + first, lanes, scalar);
            while (candidates != 0)
            {
                ir_if_convert_region(&work, (uint32_t)first + (uint32_t)__builtin_ctzll(candidates));
                candidates &= candidates - 1;
            }
        }
    }
    if (check.error == IR_OK && work.region_count != 0)
    {
        IrFunction rewritten = { 0 };
        check.error = ir_if_convert_rewrite(&work, output, scratch, &rewritten);
        if (check.error == IR_OK)
        {
            // The new selects' truth values tested against zero, as i1 selects (ir_truth.c), on the owned columns.
            ir_truth_simplify_values(&rewritten, scalar);
            converted->function = rewritten;
            converted->regions += work.region_count;
            converted->selects += work.select_count;
            converted->folded += work.folded;
            converted->reused += work.reused;
            converted->loads += work.loads;
            converted->sunk += work.sunk_count;
            converted->rounds += 1;
            *again = work.nested;
        }
    }
    converted->recurrences += check.error == IR_OK ? work.recurrences : 0;
    return check;
}

static IrCheck ir_if_convert(const IrFunction* function, Arena* output, Arena* scratch, bool scalar, IrIfConvertResult* result)
{
    const uint64_t om = output->length;
    const uint64_t sm = scratch->length;
    IrCheck check = ir_check(output != scratch ? IR_OK : IR_ERROR_STRUCTURE, BUSTER_IR_NONE, 0);
    const uint32_t count = function->instruction_count;
    const uint8_t* const operations = function->op;
    uint64_t first_candidate = 0;
    bool candidate = false;
    if (check.error == IR_OK && function->label_count == 0)
    {
        // The usual straight-line function pays only a bounded byte scan, with no CFG, queue or output allocation.
        while (first_candidate < count && !candidate)
        {
            const uint32_t lanes = count - first_candidate < BUSTER_IR_IF_CONVERT_BATCH ? count - (uint32_t)first_candidate : BUSTER_IR_IF_CONVERT_BATCH;
            candidate = ir_if_convert_candidates(operations + first_candidate, lanes, scalar) != 0;
            first_candidate += candidate ? 0 : BUSTER_IR_IF_CONVERT_BATCH;
        }
    }
    // Indirect branch labels name semantic entries, including otherwise unreachable blocks. Until the pass has a
    // label-aware region proof, leave the whole such function intact. Empty functions also need no preparation.
    IrIfConvertResult converted = {
        .function = *function,
    };
    bool again = check.error == IR_OK && candidate;
    for (uint32_t round = 0; again && round < BUSTER_IR_IF_CONVERT_ROUND_LIMIT; round += 1)
    {
        const uint64_t mark = scratch->length;
        const IrFunction input = converted.function;
        // A later round's input has no conditional branch before the first one's.
        check = ir_if_convert_round(&input, round == 0 ? first_candidate : 0, output, scratch, scalar, &converted, &again);
        again = again && check.error == IR_OK;
        arena_pop_to(scratch, mark);
    }
    if (check.error == IR_OK)
    {
        *result = converted;
    }
    else
    {
        arena_pop_to(output, om);
    }
    arena_pop_to(scratch, sm);
    return check;
}

#endif
