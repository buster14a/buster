// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Physical certificates, distinct from semantic range facts. ZERO means all bits beyond the logical width are zero.
// SIGN on an I32 means the complete register contains its signed 64-bit value. SIGN-only values use eight-byte
// storage and copies, including spills, reloads and CFG edges; an ordinary four-byte reload would destroy the proof.

#define BUSTER_TIER1_STORAGE_ZERO 1u
#define BUSTER_TIER1_STORAGE_SIGN 2u

static void tier1_select_sign_request(const IrFunction* function, uint8_t* storage, uint32_t source)
{
    for (uint32_t depth = 0; source < function->instruction_count && depth < BUSTER_TIER1_SELECT_WALK &&
                             function->op[source] == IR_COPY && function->type[source] == IR_I32; depth += 1)
    {
        source = function->a[source];
    }
    if (source < function->instruction_count && function->type[source] == IR_I32)
    {
        const uint32_t op = function->op[source];
        const uint32_t a = function->a[source];
        const bool load = op == IR_LOAD && function->flags[source] == 0;
        const bool extend = op == IR_SEXT && (function->type[a] == IR_I8 || function->type[a] == IR_I16);
        if (load || extend)
        {
            storage[source] = BUSTER_TIER1_STORAGE_SIGN;
        }
    }
}

// Only a real signed-widening use requests this representation. Incoming values, calls, arithmetic and opaque rows
// are never assumed sign-extended, and narrowing a memory access is never part of the request.
static void tier1_select_sign_requests(const IrFunction* function, uint8_t* storage, bool vector)
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
                tier1_select_sign_request(function, storage, function->a[node]);
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
                tier1_select_sign_request(function, storage, function->a[node]);
            }
        }
    }
}

static uint32_t tier1_select_storage_width(const Tier1SelectPlan* plan, uint32_t node)
{
    const uint32_t physical = plan->patterns[node].storage_width;
    return physical != 0 ? physical : tier1_select_width(plan->function->type[node]);
}
