// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// ISA eligibility for selected records, included after x86_64_encoder.c. Explicit form rows deliberately fail
// closed: an unclassified form is unavailable even on native. Extend the rows with each selector's new families.
// Do not infer ISA from prefix length: BMI and opmasks use VEX; APX uses EVEX without being AVX-512.
// x86_target_forms builds a byte table once per target; x86_target_validate also checks operand validity in tests
// or at an external-input boundary. Neither the trusted encoder nor branch sizing does these checks in its loop.
#include "buster/compiler/x86_target.c"

// Bit 63 denotes a classified row; absent initializers remain unavailable. The maps and both kernels come from the same
// canonical rows. The exact scalar reference keeps all 64 feature bits; the vector table projects the currently
// used feature bits to 32 bits, halving its read working set. No run-time initialization or allocation is needed.
#define BUSTER_X86_TARGET_KNOWN (UINT64_C(1) << 63)
#define BUSTER_X86_TARGET_COMPACT_KNOWN UINT32_C(0x80000000)
#define BUSTER_X86_TARGET_COMPACT_LOW ((UINT64_C(1) << (X86_FEATURE_AES + 1)) - 1)
#define BUSTER_X86_TARGET_COMPACT_FEATURES (BUSTER_X86_TARGET_COMPACT_LOW | BUSTER_X86_FEATURE(PCLMUL) | BUSTER_X86_TARGET_KNOWN)
#define BUSTER_X86_TARGET_COMPACT(value) ((uint32_t)((value) & BUSTER_X86_TARGET_COMPACT_LOW) | (uint32_t)(((value) >> X86_FEATURE_PCLMUL) & 1) << (X86_FEATURE_AES + 1) | ((value) & BUSTER_X86_TARGET_KNOWN ? BUSTER_X86_TARGET_COMPACT_KNOWN : 0))
_Static_assert(X86_FEATURE_AES + 1 < 31 && X86_FEATURE_PCLMUL > X86_FEATURE_AES, "compact target feature positions must be disjoint");

// The scalar reference keeps full-width requirements densely; the inverse map returns zero for unknown forms.
// Slot zero is reserved in the inverse map, so each explicit canonical row stores its ordinal plus one.
#define BUSTER_X86_TARGET_REQUIREMENT(name, required) BUSTER_X86_TARGET_SLOT_##name,
enum
{
#include "buster/compiler/x86_target_rows.c"
    BUSTER_X86_TARGET_CLASSIFIED_COUNT
};
#undef BUSTER_X86_TARGET_REQUIREMENT
_Static_assert(X86_FORM_COUNT <= UINT16_MAX, "classified form index fits its stored width");
_Static_assert(BUSTER_X86_TARGET_CLASSIFIED_COUNT <= UINT16_MAX && BUSTER_X86_TARGET_CLASSIFIED_COUNT <= X86_FORM_COUNT, "classified slot fits the inverse map");

#ifdef BUSTER_X86_TARGET_DATA_ELSEWHERE
extern const uint32_t x86_target_compact_requirements[X86_FORM_COUNT] __attribute__((aligned(64), visibility("hidden")));
extern const uint16_t x86_target_requirement_slots[X86_FORM_COUNT] __attribute__((aligned(64), visibility("hidden")));
extern const uint16_t x86_target_classified_forms[BUSTER_X86_TARGET_CLASSIFIED_COUNT] __attribute__((visibility("hidden")));
extern const uint64_t x86_target_classified_requirements[BUSTER_X86_TARGET_CLASSIFIED_COUNT] __attribute__((visibility("hidden")));
#else
#ifndef BUSTER_X86_TARGET_DATA
#define BUSTER_X86_TARGET_DATA static
#endif
#define BUSTER_X86_TARGET_REQUIREMENT(name, required) [X86_FORM_##name] = BUSTER_X86_TARGET_COMPACT(required),
BUSTER_X86_TARGET_DATA const uint32_t x86_target_compact_requirements[X86_FORM_COUNT] __attribute__((aligned(64))) = {
#include "buster/compiler/x86_target_rows.c"
};
#undef BUSTER_X86_TARGET_REQUIREMENT

#define BUSTER_X86_TARGET_REQUIREMENT(name, required) [X86_FORM_##name] = BUSTER_X86_TARGET_SLOT_##name + 1,
BUSTER_X86_TARGET_DATA const uint16_t x86_target_requirement_slots[X86_FORM_COUNT] __attribute__((aligned(64))) = {
#include "buster/compiler/x86_target_rows.c"
};
#undef BUSTER_X86_TARGET_REQUIREMENT

#define BUSTER_X86_TARGET_REQUIREMENT(name, required) X86_FORM_##name,
BUSTER_X86_TARGET_DATA const uint16_t x86_target_classified_forms[BUSTER_X86_TARGET_CLASSIFIED_COUNT] = {
#include "buster/compiler/x86_target_rows.c"
};
#undef BUSTER_X86_TARGET_REQUIREMENT

#define BUSTER_X86_TARGET_REQUIREMENT(name, required) (required),
BUSTER_X86_TARGET_DATA const uint64_t x86_target_classified_requirements[BUSTER_X86_TARGET_CLASSIFIED_COUNT] = {
#include "buster/compiler/x86_target_rows.c"
};
#undef BUSTER_X86_TARGET_REQUIREMENT
#endif
_Static_assert(BUSTER_ARRAY_LENGTH(x86_target_classified_forms) == BUSTER_ARRAY_LENGTH(x86_target_classified_requirements), "one full-width requirement per classified form");

// Extending the canonical policy outside the projection must fail at build time, never silently drop a feature.
// Update the projection deliberately before adding such a row; the exhaustive tests also check every round trip.
#define BUSTER_X86_TARGET_REQUIREMENT(name, required) _Static_assert(((required) & ~BUSTER_X86_TARGET_COMPACT_FEATURES) == 0, "target requirement cannot be compacted: " #name); _Static_assert(((required) & BUSTER_X86_TARGET_KNOWN) != 0, "explicit target row must be classified: " #name);
#include "buster/compiler/x86_target_rows.c"
#undef BUSTER_X86_TARGET_REQUIREMENT
_Static_assert(sizeof(x86_target_compact_requirements) == X86_FORM_COUNT * sizeof(uint32_t), "one compact requirement per form");
_Static_assert(__alignof__(x86_target_requirement_slots) >= 64 && __alignof__(x86_target_compact_requirements) >= 64, "target requirement map cache-line alignment");

static uint32_t x86_target_compact_features(uint64_t features)
{
    return BUSTER_X86_TARGET_COMPACT(features);
}


// Full-width metadata queries are separate from eligibility construction. Unknown and out-of-range rows return zero.
static uint64_t x86_target_requirement(uint32_t form)
{
    uint64_t result = 0;
    if (form < X86_FORM_COUNT)
    {
        const uint16_t slot = x86_target_requirement_slots[form];
        if (slot != 0)
        {
            result = x86_target_classified_requirements[slot - 1];
        }
    }
    return result;
}

static void x86_target_forms_scalar(const X86Target* target, uint8_t* allowed)
{
    const uint64_t available = target->features | BUSTER_X86_TARGET_KNOWN;
    // Capture features before the clear: target/output overlap is valid. Each classified form writes one byte.
    memset(allowed, 0, X86_FORM_COUNT);
    // The scalar oracle avoids widening indices only to extract them for byte stores. Production uses SIMD.
    #pragma clang loop vectorize(disable)
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(x86_target_classified_forms); i += 1)
    {
        const uint16_t form = x86_target_classified_forms[i];
        const uint64_t required = x86_target_classified_requirements[i];
        allowed[form] = (required & ~available) == 0;
    }
}

// Sixteen contiguous 32-bit rows per iteration: no gathers, scratch allocation or speculative tail loads.
// Unknown rows stay zero, so the nonzero test still distinguishes them from classified baseline rows.
static void x86_target_forms(const X86Target* target, uint8_t* allowed)
{
    const __m512i unavailable = _mm512_set1_epi32((int)~x86_target_compact_features(target->features | BUSTER_X86_TARGET_KNOWN));
    const __m512i zero = _mm512_setzero_si512();
    for (uint32_t i = 0; i < X86_FORM_COUNT; i += 16)
    {
        const uint32_t count = X86_FORM_COUNT - i < 16 ? X86_FORM_COUNT - i : 16;
        const __mmask16 active = (__mmask16)((1u << count) - 1);
        const __m512i required = _mm512_maskz_loadu_epi32(active, x86_target_compact_requirements + i);
        const __mmask16 known = _mm512_cmpneq_epi32_mask(required, zero);
        const __mmask16 supported = _mm512_mask_testn_epi32_mask(known, required, unavailable);
        const __m128i flags = _mm_maskz_set1_epi8(supported, 1);
        _mm_mask_storeu_epi8(allowed + i, active, flags);
    }
}

static bool x86_target_form_allowed(const uint8_t* allowed, uint32_t form)
{
    const uint32_t number = form & BUSTER_X86_FORM_NUMBER;
    const uint32_t row = number % X86_FORM_COUNT;
    return form <= UINT16_MAX && number < BUSTER_X86_FORM_TEMPLATES && allowed[row] != 0;
}

// Returns count on success or the first unsupported/invalid record. Empty input may have a null pointer.
static uint32_t x86_target_validate(const uint8_t* allowed, const X86Instruction* instructions, uint32_t count)
{
    uint32_t i = 0;
    while (i < count && x86_target_form_allowed(allowed, instructions[i].form) && x86_validate(instructions + i, 1) == 1)
    {
        i += 1;
    }
    return i;
}
