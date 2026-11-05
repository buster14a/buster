// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Checked nonnegative induction bounds for #1219. This is an inductive certificate, not optimistic phi iteration:
// every initial value is a nonnegative constant; every other incoming value adds a bounded nonnegative stride to
// this phi and reaches it only through a strict upper-bound guard on that sum. One common bound covers ALL incoming
// edges. Its sum with the largest stride must fit signed I32, including before the guard executes.

#define BUSTER_TIER1_INDUCTION_EDGES 8

// This is the plan's dead positions workspace, before level ordering fills it, not another CFG allocation.
// A scalar edge scatter records one predecessor or ambiguity per block. Indirect control flow leaves the map
// unavailable: enumerating all of its possible label edges here could make an otherwise bounded scan quadratic.
static bool tier1_select_range_predecessors(const IrFunction* function, uint32_t* predecessors)
{
    bool complete = predecessors != 0;
    if (complete)
    {
        memset(predecessors, 0xff, (uint64_t)function->block_count * sizeof(uint32_t));
    }
    for (uint32_t block = 0; complete && block < function->block_count; block += 1)
    {
        const uint32_t end = function->block_offsets[block + 1];
        if (end > function->block_offsets[block])
        {
            const uint32_t terminator = end - 1;
            const uint32_t op = function->op[terminator];
            const uint32_t edges = op == IR_BR ? 1 : op == IR_CBR ? 2 : 0;
            complete = edges != 0 || op == IR_RET || op == IR_UNREACHABLE;
            for (uint32_t edge = 0; edge < edges; edge += 1)
            {
                const uint32_t target = op == IR_BR ? function->a[terminator] : edge == 0 ? function->b[terminator] : function->c[terminator];
                const uint32_t previous = predecessors[target];
                predecessors[target] = previous == BUSTER_IR_NONE || previous == block ? block : BUSTER_IR_NONE - 1;
            }
        }
    }
    return complete;
}

// Follow only an unconditional chain with exactly one incoming predecessor at every link. The final conditional
// edge is then necessary to reach this phi operand; a bridge with another entrance cannot borrow its guard.
static uint32_t tier1_select_range_guard(const IrFunction* function, const uint32_t* predecessors, uint32_t predecessor, uint32_t* target)
{
    uint32_t guard = BUSTER_IR_NONE;
    for (uint32_t depth = 0; depth < BUSTER_TIER1_SELECT_CANONICAL_PASSES && predecessor < function->block_count; depth += 1)
    {
        const uint32_t end = function->block_offsets[predecessor + 1];
        if (end <= function->block_offsets[predecessor])
        {
            break;
        }
        const uint32_t terminator = end - 1;
        if (function->op[terminator] == IR_CBR)
        {
            guard = terminator;
            break;
        }
        if (function->op[terminator] != IR_BR || function->a[terminator] != *target || predecessors == 0)
        {
            break;
        }
        *target = predecessor;
        predecessor = predecessors[predecessor];
    }
    return guard;
}

static uint32_t tier1_select_range_source(const IrFunction* function, uint32_t value)
{
    for (uint32_t depth = 0; value < function->instruction_count && depth < BUSTER_TIER1_SELECT_CANONICAL_PASSES &&
                             function->op[value] == IR_COPY && function->a[value] < function->instruction_count &&
                             function->type[value] == function->type[function->a[value]]; depth += 1)
    {
        value = function->a[value];
    }
    return value;
}

static bool tier1_select_range_zero(const IrFunction* function, uint32_t value)
{
    value = tier1_select_range_source(function, value);
    const bool zero = value < function->instruction_count && function->op[value] == IR_CONST && function->a[value] < function->constant_count &&
                      function->constants[function->a[value]].low == 0 && function->constants[function->a[value]].high == 0;
    return zero;
}

// Integer extension preserves zero/nonzero. Peel only those wrappers and equality/inequality tests of zero;
// truncation and arbitrary arithmetic are not truth-preserving links.
static uint32_t tier1_select_range_condition(const IrFunction* function, uint32_t value, bool* negated)
{
    bool walking = true;
    for (uint32_t depth = 0; walking && value < function->instruction_count && depth < BUSTER_TIER1_SELECT_CANONICAL_PASSES; depth += 1)
    {
        const uint32_t op = function->op[value];
        if (op == IR_COPY || op == IR_ZEXT || op == IR_SEXT)
        {
            value = function->a[value];
        }
        else if (op == IR_ICMP && function->c[value] <= IR_NE &&
                 (tier1_select_range_zero(function, function->b[value]) || tier1_select_range_zero(function, function->a[value])))
        {
            *negated ^= function->c[value] == IR_EQ;
            value = tier1_select_range_zero(function, function->b[value]) ? function->a[value] : function->b[value];
        }
        else
        {
            walking = false;
        }
    }
    return value;
}

static bool tier1_select_range_fact(uint16_t* known, uint32_t value, uint64_t maximum)
{
    const uint32_t bits = maximum == 0 ? 0 : 64 - (uint32_t)__builtin_clzll(maximum);
    const uint32_t u = (known[value] & 255) < bits ? known[value] & 255 : bits;
    const uint32_t s = (known[value] >> 8) < bits + 1 ? known[value] >> 8 : bits + 1;
    const uint16_t next = (uint16_t)(u | (s << 8));
    const bool changed = known[value] != next;
    known[value] = next;
    return changed;
}

static bool tier1_select_induction_phi(const IrFunction* function, const uint32_t* blocks, const uint32_t* predecessors, uint16_t* known, uint32_t node)
{
    const uint32_t block = blocks[node];
    const uint32_t incoming = function->b[node];
    bool valid = block != 0 && block < function->block_count && incoming >= 2 && incoming <= BUSTER_TIER1_INDUCTION_EDGES &&
                 (uint64_t)function->a[node] + (uint64_t)incoming * 2 <= function->extra_count;
    bool seeded = false;
    uint64_t maximum = 0;
    uint64_t stride_maximum = 0;
    uint32_t steps[BUSTER_TIER1_INDUCTION_EDGES];
    uint32_t step_count = 0;
    for (uint32_t edge = 0; valid && edge < incoming; edge += 1)
    {
        const uint32_t predecessor = function->extra[function->a[node] + edge * 2];
        const uint32_t value = tier1_select_range_source(function, function->extra[function->a[node] + edge * 2 + 1]);
        valid = value < function->instruction_count && predecessor < function->block_count && function->type[value] == IR_I32;
        if (!valid)
        {
            break;
        }
        if (function->op[value] == IR_CONST && function->a[value] < function->constant_count)
        {
            const uint32_t initial = (uint32_t)function->constants[function->a[value]].low;
            valid = initial <= INT32_MAX;
            maximum = maximum > initial ? maximum : initial;
            seeded = true;
        }
        else
        {
            valid = function->op[value] == IR_ADD;
            if (!valid)
            {
                break;
            }
            const uint32_t left = tier1_select_range_source(function, function->a[value]);
            const uint32_t right = tier1_select_range_source(function, function->b[value]);
            const uint32_t stride = left == node ? right : right == node ? left : BUSTER_IR_NONE;
            uint32_t target = block;
            const uint32_t branch = tier1_select_range_guard(function, predecessors, predecessor, &target);
            valid = stride < function->instruction_count && function->type[stride] == IR_I32 && (known[stride] & 255) < 32 &&
                    branch < function->instruction_count && ((function->b[branch] == target) != (function->c[branch] == target));
            if (!valid)
            {
                break;
            }
            bool negated = false;
            const uint32_t condition = tier1_select_range_condition(function, function->a[branch], &negated);
            valid = condition < function->instruction_count && function->op[condition] == IR_ICMP;
            if (!valid)
            {
                break;
            }
            const uint32_t a = tier1_select_range_source(function, function->a[condition]);
            const uint32_t b = tier1_select_range_source(function, function->b[condition]);
            const uint32_t predicate = function->c[condition];
            const bool forward = a == value && (predicate == IR_LT || predicate == IR_GE || predicate == IR_ULT || predicate == IR_UGE);
            const bool reversed = b == value && (predicate == IR_GT || predicate == IR_LE || predicate == IR_UGT || predicate == IR_ULE);
            const bool less_when_true = predicate == IR_LT || predicate == IR_GT || predicate == IR_ULT || predicate == IR_UGT;
            const uint32_t limit = forward ? b : a;
            valid = (forward || reversed) && limit < function->instruction_count && function->type[limit] == IR_I32 &&
                    (((function->b[branch] == target) != negated) == less_when_true);
            if (!valid)
            {
                break;
            }
            const uint32_t unsigned_bits = known[limit] & 255;
            const uint32_t signed_bits = known[limit] >> 8;
            uint64_t upper = (UINT64_C(1) << unsigned_bits) - 1;
            if (predicate < IR_ULT)
            {
                const uint64_t signed_upper = (UINT64_C(1) << (signed_bits - 1)) - 1;
                upper = upper < signed_upper ? upper : signed_upper;
            }
            valid = upper <= INT32_MAX;
            const uint64_t admitted = upper != 0 ? upper - 1 : 0;
            const uint64_t delta = (UINT64_C(1) << (known[stride] & 255)) - 1;
            maximum = maximum > admitted ? maximum : admitted;
            stride_maximum = stride_maximum > delta ? stride_maximum : delta;
            steps[step_count++] = value;
        }
    }
    bool changed = false;
    if (valid && seeded && step_count != 0 && maximum + stride_maximum <= INT32_MAX)
    {
        changed |= tier1_select_range_fact(known, node, maximum);
        for (uint32_t step = 0; step < step_count; step += 1)
        {
            changed |= tier1_select_range_fact(known, steps[step], maximum + stride_maximum);
        }
    }
    return changed;
}

static bool tier1_select_induction_bounds(const IrFunction* function, const uint32_t* blocks, uint32_t* workspace, uint16_t* known, bool vector)
{
    bool changed = false;
    if (blocks != 0 && function->block_count >= 2)
    {
        const uint32_t* predecessors = tier1_select_range_predecessors(function, workspace) ? workspace : 0;
        if (BUSTER_VECTOR_PATH(vector))
        {
            for (uint32_t first = 0; first < function->instruction_count;)
            {
                const uint32_t left = function->instruction_count - first;
                const __mmask32 active = left >= 32 ? UINT32_MAX : (UINT32_C(1) << left) - 1;
                const __m256i op = _mm256_maskz_loadu_epi8(active, function->op + first);
                const __m256i type = _mm256_maskz_loadu_epi8(active, function->type + first);
                uint32_t candidates = active & _mm256_cmpeq_epi8_mask(op, _mm256_set1_epi8(IR_PHI)) & _mm256_cmpeq_epi8_mask(type, _mm256_set1_epi8(IR_I32));
                while (candidates != 0)
                {
                    changed |= tier1_select_induction_phi(function, blocks, predecessors, known, first + (uint32_t)__builtin_ctz(candidates));
                    candidates &= candidates - 1;
                }
                first += left < 32 ? left : 32;
            }
        }
        else
        {
            for (uint32_t node = 0; node < function->instruction_count; node += 1)
            {
                if (function->op[node] == IR_PHI && function->type[node] == IR_I32)
                {
                    changed |= tier1_select_induction_phi(function, blocks, predecessors, known, node);
                }
            }
        }
    }
    return changed;
}
