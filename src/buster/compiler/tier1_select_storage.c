// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Physical certificates, distinct from semantic range facts. ZERO means all bits beyond the logical width are zero.
// SIGN on an I32 means the complete register contains its signed 64-bit value. SIGN-only values use eight-byte
// storage and copies, including spills, reloads and CFG edges; an ordinary four-byte reload would destroy the proof.

#define BUSTER_TIER1_STORAGE_ZERO 1u
#define BUSTER_TIER1_STORAGE_SIGN 2u
#define BUSTER_TIER1_STORAGE_WIDE 4u

static bool tier1_select_sign_birth(const IrFunction* function, uint32_t node)
{
    const uint32_t op = function->op[node];
    const uint32_t b = function->b[node];
    const bool shifted = op == IR_ASHR && b < function->instruction_count && function->op[b] == IR_CONST &&
                         (function->constants[function->a[b]].low & 31) != 0;
    const bool writes = op == IR_ADD || op == IR_SUB || op == IR_NEG || op == IR_NOT || op == IR_AND || op == IR_OR || op == IR_XOR || shifted;
    return writes;
}

static uint32_t tier1_select_storage_root(uint32_t* parents, uint32_t value)
{
    while (parents[value] != value)
    {
        parents[value] = parents[parents[value]];
        value = parents[value];
    }
    return value;
}

static void tier1_select_storage_phi(const IrFunction* function, uint8_t* storage, uint32_t* parents, uint32_t node)
{
    for (uint32_t edge = 0; edge < function->b[node]; edge += 1)
    {
        uint32_t a = tier1_select_storage_root(parents, node);
        uint32_t b = tier1_select_storage_root(parents, function->extra[function->a[node] + edge * 2 + 1]);
        if (a != b)
        {
            const uint32_t rank_a = storage[a] >> 3;
            const uint32_t rank_b = storage[b] >> 3;
            if (rank_a < rank_b)
            {
                const uint32_t swap = a;
                a = b;
                b = swap;
            }
            parents[b] = a;
            storage[a] += rank_a == rank_b ? 8 : 0;
            storage[a] |= storage[b] & BUSTER_TIER1_STORAGE_WIDE;
        }
    }
}

// Edge moves carry the source value's width. Every I32 phi and its operands must therefore share a spill width,
// including opaque inputs for which eight-byte storage conveys NO upper-bit fact. Union by rank and path halving
// closes arbitrarily long phi chains, with no fixed-point cutoff or extra allocation: positions is dead workspace.
// Five transient high bits hold rank (at most 31 for a u32 instruction count); the final pass removes every rank.
static void tier1_select_storage_components(const IrFunction* function, uint8_t* storage, uint32_t* parents, bool vector)
{
    const uint32_t count = function->instruction_count;
    if (BUSTER_VECTOR_PATH(vector))
    {
        const __m512i lanes = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
        for (uint32_t first = 0; first < count;)
        {
            const uint32_t left = count - first;
            const __mmask16 active = left >= 16 ? UINT16_MAX : (UINT32_C(1) << left) - 1;
            _mm512_mask_storeu_epi32(parents + first, active, _mm512_add_epi32(lanes, _mm512_set1_epi32(first)));
            const __m128i facts = _mm_and_si128(_mm_maskz_loadu_epi8(active, storage + first), _mm_set1_epi8(3));
            const __m128i types = _mm_maskz_loadu_epi8(active, function->type + first);
            const __mmask16 wide = active & _mm_cmpeq_epi8_mask(types, _mm_set1_epi8(IR_I32)) & _mm_cmpeq_epi8_mask(facts, _mm_set1_epi8(BUSTER_TIER1_STORAGE_SIGN));
            _mm_mask_storeu_epi8(storage + first, active, _mm_mask_mov_epi8(facts, wide, _mm_or_si128(facts, _mm_set1_epi8(BUSTER_TIER1_STORAGE_WIDE))));
            first += left < 16 ? left : 16;
        }
        for (uint32_t first = 0; first < count;)
        {
            const uint32_t left = count - first;
            const __mmask32 active = left >= 32 ? UINT32_MAX : (UINT32_C(1) << left) - 1;
            const __m256i op = _mm256_maskz_loadu_epi8(active, function->op + first);
            const __m256i types = _mm256_maskz_loadu_epi8(active, function->type + first);
            uint32_t phis = active & _mm256_cmpeq_epi8_mask(op, _mm256_set1_epi8(IR_PHI)) & _mm256_cmpeq_epi8_mask(types, _mm256_set1_epi8(IR_I32));
            while (phis != 0)
            {
                tier1_select_storage_phi(function, storage, parents, first + (uint32_t)__builtin_ctz(phis));
                phis &= phis - 1;
            }
            first += left < 32 ? left : 32;
        }
    }
    else
    {
        for (uint32_t node = 0; node < count; node += 1)
        {
            parents[node] = node;
            storage[node] &= 3;
            storage[node] |= function->type[node] == IR_I32 && storage[node] == BUSTER_TIER1_STORAGE_SIGN ? BUSTER_TIER1_STORAGE_WIDE : 0;
        }
        for (uint32_t node = 0; node < count; node += 1)
        {
            if (function->op[node] == IR_PHI && function->type[node] == IR_I32)
            {
                tier1_select_storage_phi(function, storage, parents, node);
            }
        }
    }
    for (uint32_t node = 0; node < count; node += 1)
    {
        storage[node] = (storage[node] & 3) | (storage[tier1_select_storage_root(parents, node)] & BUSTER_TIER1_STORAGE_WIDE);
    }
}

static void tier1_select_sign_request(const IrFunction* function, const uint16_t* known, uint8_t* storage, uint32_t source)
{
    uint32_t pending[BUSTER_TIER1_SELECT_WALK] = { source };
    uint32_t count = 1;
    for (uint32_t visited = 0; count != 0 && visited < BUSTER_TIER1_SELECT_WALK; visited += 1)
    {
        source = pending[--count];
        if (source >= function->instruction_count || function->type[source] != IR_I32 ||
            ((known[source] & BUSTER_TIER1_KNOWN_UNSIGNED_MASK) < 32 && (storage[source] & BUSTER_TIER1_STORAGE_ZERO) != 0))
        {
            continue;
        }
        const uint32_t op = function->op[source];
        const uint32_t a = function->a[source];
        const uint32_t b = function->b[source];
        const bool load = op == IR_LOAD && function->flags[source] == 0;
        const bool extend = op == IR_SEXT && (function->type[a] == IR_I8 || function->type[a] == IR_I16);
        if (load || extend)
        {
            storage[source] = BUSTER_TIER1_STORAGE_SIGN;
        }
        else if (op == IR_PHI && function->b[source] <= BUSTER_TIER1_SELECT_WALK)
        {
            for (uint32_t edge = 0; edge < function->b[source] && count < BUSTER_TIER1_SELECT_WALK; edge += 1)
            {
                const uint32_t value = function->extra[function->a[source] + edge * 2 + 1];
                const uint32_t birth = tier1_select_range_source(function, value);
                if ((known[source] & BUSTER_TIER1_KNOWN_BELOW_MAX) != 0 && tier1_select_sign_birth(function, birth))
                {
                    // Preserve I32 wrapping first, then establish its full signed representation once at this
                    // definition. Its loop phi can reuse it, including for a separately proven nonoverflowing +1.
                    storage[birth] = BUSTER_TIER1_STORAGE_SIGN;
                }
                pending[count++] = value;
            }
        }
        else
        {
            uint32_t children[2] = { BUSTER_IR_NONE, BUSTER_IR_NONE };
            const uint32_t sa = a < function->instruction_count ? known[a] >> 8 : 32;
            const uint32_t sb = b < function->instruction_count ? known[b] >> 8 : 32;
            const bool binary = (op == IR_ADD || op == IR_SUB) && sa < 32 && sb < 32;
            const bool multiply = op == IR_MUL && tier1_select_signed_multiply(function, known, source);
            const bool bitwise = op == IR_AND || op == IR_OR || op == IR_XOR;
            const bool constant = b < function->instruction_count && function->op[b] == IR_CONST;
            const bool subtract = op == IR_SUB && constant && sa < 32 && sb < 32;
            const bool shift = op == IR_ASHR && constant && function->constants[function->a[b]].low < 32;
            const int64_t delta = constant ? (int32_t)function->constants[function->a[b]].low : 0;
            const bool plus_one = (op == IR_ADD || op == IR_SUB) && constant && (op == IR_ADD ? delta : -delta) == 1 &&
                                  (known[a] & BUSTER_TIER1_KNOWN_BELOW_MAX) != 0;
            if (op == IR_COPY || op == IR_NOT || (op == IR_NEG && sa < 32) || subtract || shift || plus_one)
            {
                children[0] = a;
            }
            else if (binary || multiply || bitwise)
            {
                children[0] = a;
                children[1] = b;
            }
            else if (op == IR_SELECT)
            {
                children[0] = function->b[source];
                children[1] = function->c[source];
            }
            for (uint32_t child = 0; child < 2 && count < BUSTER_TIER1_SELECT_WALK; child += 1)
            {
                if (children[child] != BUSTER_IR_NONE)
                {
                    pending[count++] = children[child];
                }
            }
        }
    }
}

// Only a real signed-widening use requests this representation. Incoming values, calls, arithmetic and opaque rows
// are never assumed sign-extended, and narrowing a memory access is never part of the request.
static void tier1_select_sign_requests(const IrFunction* function, const uint16_t* known, uint8_t* storage, bool vector)
{
    if (BUSTER_VECTOR_PATH(vector))
    {
        for (uint32_t first = 0; first < function->instruction_count;)
        {
            const uint32_t left = function->instruction_count - first;
            const __mmask32 active = left >= 32 ? UINT32_MAX : (UINT32_C(1) << left) - 1;
            const __m256i op = _mm256_maskz_loadu_epi8(active, function->op + first);
            const __m256i type = _mm256_maskz_loadu_epi8(active, function->type + first);
            uint32_t requests = active & _mm256_cmpeq_epi8_mask(op, _mm256_set1_epi8(IR_SEXT)) & _mm256_cmpeq_epi8_mask(type, _mm256_set1_epi8(IR_I64));
            while (requests != 0)
            {
                const uint32_t node = first + (uint32_t)__builtin_ctz(requests);
                tier1_select_sign_request(function, known, storage, function->a[node]);
                requests &= requests - 1;
            }
            first += left < 32 ? left : 32;
        }
    }
    else
    {
        for (uint32_t node = 0; node < function->instruction_count; node += 1)
        {
            if (function->op[node] == IR_SEXT && function->type[node] == IR_I64)
            {
                tier1_select_sign_request(function, known, storage, function->a[node]);
            }
        }
    }
}

static uint32_t tier1_select_storage_width(const Tier1SelectPlan* plan, uint32_t node)
{
    const uint32_t physical = plan->patterns[node].storage_width;
    return physical != 0 ? physical : tier1_select_width(plan->function->type[node]);
}
