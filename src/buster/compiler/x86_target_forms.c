// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// ISA eligibility for selected records, included after x86_64_encoder.c. Explicit form rows deliberately fail
// closed: an unclassified form is unavailable even on native. Extend the rows with each selector's new families.
// Do not infer ISA from prefix length: BMI and opmasks use VEX; APX uses EVEX without being AVX-512.
// x86_target_forms builds a byte table once per target; x86_target_validate also checks operand validity in tests
// or at an external-input boundary. Neither the trusted encoder nor branch sizing does these checks in its loop.
#include "buster/compiler/x86_target.c"

// Bit 63 denotes a classified row; absent initializers remain unavailable. The maps and the builder come from the same
// canonical rows. The full-width requirements keep all 64 feature bits (x86_target_requirement's metadata and the
// extended rows' exact pass); the vector table projects the currently used feature bits to 32 bits, halving its read
// working set. No run-time initialization or allocation is needed.
// The rows that need a feature past the projection (BUSTER_X86_TARGET_EXTENDED's, last in x86_target_rows.c) are
// the vector builder's rare case: their compact words carry BUSTER_X86_TARGET_COMPACT_EXTENDED, which no target
// has, and their rest words (x86_target_rest_requirements, 0 for every other form) the features past the projection,
// which the vector loop reads where a block has an extended lane.
#define BUSTER_X86_TARGET_KNOWN (UINT64_C(1) << 63)
#define BUSTER_X86_TARGET_COMPACT_KNOWN UINT32_C(0x80000000)
#define BUSTER_X86_TARGET_COMPACT_EXTENDED UINT32_C(0x40000000)
// The compact word: the features to AES at their own bits, then PCLMUL and the X86 rows' AVX512VBMI,
// AVX512VP2INTERSECT, GFNI, VAES and PREFETCHI (#760) at the next six, an extended row's mark at bit 30, and the
// classification at bit 31.
#define BUSTER_X86_TARGET_COMPACT_LOW ((UINT64_C(1) << (X86_FEATURE_AES + 1)) - 1)
#define BUSTER_X86_TARGET_COMPACT_HIGH                                                                                                                         \
    (BUSTER_X86_FEATURE(PCLMUL) | BUSTER_X86_FEATURE(AVX512VBMI) | BUSTER_X86_FEATURE(AVX512VP2INTERSECT) | BUSTER_X86_FEATURE(GFNI) | BUSTER_X86_FEATURE(VAES) | \
     BUSTER_X86_FEATURE(PREFETCHI))
#define BUSTER_X86_TARGET_COMPACT_FEATURES (BUSTER_X86_TARGET_COMPACT_LOW | BUSTER_X86_TARGET_COMPACT_HIGH | BUSTER_X86_TARGET_KNOWN)
#define BUSTER_X86_TARGET_COMPACT_BIT(value, feature, bit) ((uint32_t)(((value) >> X86_FEATURE_##feature) & 1) << (X86_FEATURE_AES + (bit)))
#define BUSTER_X86_TARGET_COMPACT(value)                                                                                                                       \
    ((uint32_t)((value) & BUSTER_X86_TARGET_COMPACT_LOW) | BUSTER_X86_TARGET_COMPACT_BIT(value, PCLMUL, 1) | BUSTER_X86_TARGET_COMPACT_BIT(value, AVX512VBMI, 2) |  \
     BUSTER_X86_TARGET_COMPACT_BIT(value, AVX512VP2INTERSECT, 3) | BUSTER_X86_TARGET_COMPACT_BIT(value, GFNI, 4) | BUSTER_X86_TARGET_COMPACT_BIT(value, VAES, 5) |   \
     BUSTER_X86_TARGET_COMPACT_BIT(value, PREFETCHI, 6) | ((value) & BUSTER_X86_TARGET_KNOWN ? BUSTER_X86_TARGET_COMPACT_KNOWN : 0))
// The rest word: every feature the compact word doesn't project, a bit each, so that the two words hold every
// feature (the assertion below).
#define BUSTER_X86_TARGET_REST_BIT(value, feature, bit) ((uint32_t)(((value) >> X86_FEATURE_##feature) & 1) << (bit))
#define BUSTER_X86_TARGET_REST(value)                                                                                                                          \
    (BUSTER_X86_TARGET_REST_BIT(value, AVX512BF16, 0) | BUSTER_X86_TARGET_REST_BIT(value, AVX512BITALG, 1) | BUSTER_X86_TARGET_REST_BIT(value, AVX512IFMA, 2) |     \
     BUSTER_X86_TARGET_REST_BIT(value, AVX512VBMI2, 3) | BUSTER_X86_TARGET_REST_BIT(value, AVX512VNNI, 4) | BUSTER_X86_TARGET_REST_BIT(value, AVX512VPOPCNTDQ, 5) | \
     BUSTER_X86_TARGET_REST_BIT(value, AVXVNNI, 6) | BUSTER_X86_TARGET_REST_BIT(value, CLFLUSHOPT, 7) | BUSTER_X86_TARGET_REST_BIT(value, CLWB, 8) |              \
     BUSTER_X86_TARGET_REST_BIT(value, CLZERO, 9) | BUSTER_X86_TARGET_REST_BIT(value, FSGSBASE, 10) | BUSTER_X86_TARGET_REST_BIT(value, INVPCID, 11) |          \
     BUSTER_X86_TARGET_REST_BIT(value, MOVDIR64B, 12) | BUSTER_X86_TARGET_REST_BIT(value, MOVDIRI, 13) | BUSTER_X86_TARGET_REST_BIT(value, MWAITX, 14) |        \
     BUSTER_X86_TARGET_REST_BIT(value, PKU, 15) | BUSTER_X86_TARGET_REST_BIT(value, PRFCHW, 16) | BUSTER_X86_TARGET_REST_BIT(value, RDPID, 17) |                 \
     BUSTER_X86_TARGET_REST_BIT(value, RDPRU, 18) | BUSTER_X86_TARGET_REST_BIT(value, RDRND, 19) | BUSTER_X86_TARGET_REST_BIT(value, RDSEED, 20) |              \
     BUSTER_X86_TARGET_REST_BIT(value, SHA, 21) | BUSTER_X86_TARGET_REST_BIT(value, SHSTK, 22) | BUSTER_X86_TARGET_REST_BIT(value, SSE4A, 23) |                 \
     BUSTER_X86_TARGET_REST_BIT(value, VPCLMULQDQ, 24) | BUSTER_X86_TARGET_REST_BIT(value, WBNOINVD, 25) | BUSTER_X86_TARGET_REST_BIT(value, XSAVEC, 26) |      \
     BUSTER_X86_TARGET_REST_BIT(value, XSAVEOPT, 27) | BUSTER_X86_TARGET_REST_BIT(value, XSAVES, 28) | BUSTER_X86_TARGET_REST_BIT(value, RDTSCP, 29) |          \
     BUSTER_X86_TARGET_REST_BIT(value, CLFLUSH, 30))
#define BUSTER_X86_TARGET_REST_FEATURES                                                                                                                        \
    (BUSTER_X86_FEATURE(AVX512BF16) | BUSTER_X86_FEATURE(AVX512BITALG) | BUSTER_X86_FEATURE(AVX512IFMA) | BUSTER_X86_FEATURE(AVX512VBMI2) |                   \
     BUSTER_X86_FEATURE(AVX512VNNI) | BUSTER_X86_FEATURE(AVX512VPOPCNTDQ) | BUSTER_X86_FEATURE(AVXVNNI) | BUSTER_X86_FEATURE(CLFLUSHOPT) |                     \
     BUSTER_X86_FEATURE(CLWB) | BUSTER_X86_FEATURE(CLZERO) | BUSTER_X86_FEATURE(FSGSBASE) | BUSTER_X86_FEATURE(INVPCID) | BUSTER_X86_FEATURE(MOVDIR64B) |      \
     BUSTER_X86_FEATURE(MOVDIRI) | BUSTER_X86_FEATURE(MWAITX) | BUSTER_X86_FEATURE(PKU) | BUSTER_X86_FEATURE(PRFCHW) | BUSTER_X86_FEATURE(RDPID) |             \
     BUSTER_X86_FEATURE(RDPRU) | BUSTER_X86_FEATURE(RDRND) | BUSTER_X86_FEATURE(RDSEED) | BUSTER_X86_FEATURE(SHA) | BUSTER_X86_FEATURE(SHSTK) |                \
     BUSTER_X86_FEATURE(SSE4A) | BUSTER_X86_FEATURE(VPCLMULQDQ) | BUSTER_X86_FEATURE(WBNOINVD) | BUSTER_X86_FEATURE(XSAVEC) | BUSTER_X86_FEATURE(XSAVEOPT) |   \
     BUSTER_X86_FEATURE(XSAVES) | BUSTER_X86_FEATURE(RDTSCP) | BUSTER_X86_FEATURE(CLFLUSH))
_Static_assert((BUSTER_X86_TARGET_REST_FEATURES & BUSTER_X86_TARGET_COMPACT_FEATURES) == 0 &&
                   ((BUSTER_X86_TARGET_REST_FEATURES | BUSTER_X86_TARGET_COMPACT_FEATURES) & ((UINT64_C(1) << X86_FEATURE_COUNT) - 1)) == (UINT64_C(1) << X86_FEATURE_COUNT) - 1,
               "the compact and rest words hold every feature, each in one of them");
_Static_assert(X86_FEATURE_AES + 6 < 30 && X86_FEATURE_PCLMUL > X86_FEATURE_AES && X86_FEATURE_AVX512VBMI > X86_FEATURE_AES && X86_FEATURE_AVX512VP2INTERSECT > X86_FEATURE_AES &&
                   X86_FEATURE_GFNI > X86_FEATURE_AES && X86_FEATURE_VAES > X86_FEATURE_AES && X86_FEATURE_PREFETCHI > X86_FEATURE_AES,
               "compact target feature positions must be disjoint");

// The full-width requirements are kept densely; the inverse map returns zero for unknown forms.
// Slot zero is reserved in the inverse map, so each explicit canonical row stores its ordinal plus one.
// Readers need the two bounds, not every row's slot and policy assertions. The data owner and standalone
// inclusions derive both counts from the canonical rows below and reject a stale interface at compile time.
enum
{
    BUSTER_X86_TARGET_CLASSIFIED_COUNT = 4605,
    BUSTER_X86_TARGET_COMPACT_COUNT = 4384
};
_Static_assert(X86_FORM_COUNT <= UINT16_MAX, "classified form index fits its stored width");
// The extended blocks' bitset's words, 1,024 forms each (x86_target_extended_blocks lists its words' initializers).
#define BUSTER_X86_TARGET_BLOCK_WORDS 12
_Static_assert(X86_FORM_COUNT <= BUSTER_X86_TARGET_BLOCK_WORDS * 1024, "the extended blocks' bitset covers every form: add a word and its initializer");
_Static_assert(BUSTER_X86_TARGET_CLASSIFIED_COUNT <= UINT16_MAX && BUSTER_X86_TARGET_CLASSIFIED_COUNT <= X86_FORM_COUNT, "classified slot fits the inverse map");

#ifdef BUSTER_X86_TARGET_DATA_ELSEWHERE
extern const uint32_t x86_target_compact_requirements[X86_FORM_COUNT] __attribute__((aligned(64), visibility("hidden")));
extern const uint32_t x86_target_rest_requirements[X86_FORM_COUNT] __attribute__((aligned(64), visibility("hidden")));
extern const uint64_t x86_target_extended_blocks[BUSTER_X86_TARGET_BLOCK_WORDS] __attribute__((visibility("hidden")));
extern const uint16_t x86_target_requirement_slots[X86_FORM_COUNT] __attribute__((aligned(64), visibility("hidden")));
extern const uint16_t x86_target_classified_forms[BUSTER_X86_TARGET_CLASSIFIED_COUNT] __attribute__((visibility("hidden")));
extern const uint64_t x86_target_classified_requirements[BUSTER_X86_TARGET_CLASSIFIED_COUNT] __attribute__((visibility("hidden")));
#else
#define BUSTER_X86_TARGET_REQUIREMENT(name, required) BUSTER_X86_TARGET_SLOT_##name,
#define BUSTER_X86_TARGET_EXTENDED(name, required) BUSTER_X86_TARGET_SLOT_##name,
enum
{
#include "buster/compiler/x86_target_rows.c"
    BUSTER_X86_TARGET_CLASSIFIED_ROWS
};
#undef BUSTER_X86_TARGET_EXTENDED
#undef BUSTER_X86_TARGET_REQUIREMENT
// The compact rows' count: the extended rows' slots start there (each one's assertion below keeps them last).
#define BUSTER_X86_TARGET_REQUIREMENT(name, required) BUSTER_X86_TARGET_COMPACT_SLOT_##name,
#define BUSTER_X86_TARGET_EXTENDED(name, required)
enum
{
#include "buster/compiler/x86_target_rows.c"
    BUSTER_X86_TARGET_COMPACT_ROWS
};
#undef BUSTER_X86_TARGET_EXTENDED
#undef BUSTER_X86_TARGET_REQUIREMENT
_Static_assert(BUSTER_X86_TARGET_CLASSIFIED_ROWS == BUSTER_X86_TARGET_CLASSIFIED_COUNT, "classified target row count must match its readers");
_Static_assert(BUSTER_X86_TARGET_COMPACT_ROWS == BUSTER_X86_TARGET_COMPACT_COUNT, "compact target row count must match its readers");

#ifndef BUSTER_X86_TARGET_DATA
#define BUSTER_X86_TARGET_DATA static
#endif
#define BUSTER_X86_TARGET_REQUIREMENT(name, required) [X86_FORM_##name] = BUSTER_X86_TARGET_COMPACT(required),
#define BUSTER_X86_TARGET_EXTENDED(name, required) [X86_FORM_##name] = BUSTER_X86_TARGET_COMPACT(required) | BUSTER_X86_TARGET_COMPACT_EXTENDED,
BUSTER_X86_TARGET_DATA const uint32_t x86_target_compact_requirements[X86_FORM_COUNT] __attribute__((aligned(64))) = {
#include "buster/compiler/x86_target_rows.c"
};
#undef BUSTER_X86_TARGET_EXTENDED
#undef BUSTER_X86_TARGET_REQUIREMENT

// The rest words, the extended rows' alone (a compact row has no rest feature).
#define BUSTER_X86_TARGET_REQUIREMENT(name, required)
#define BUSTER_X86_TARGET_EXTENDED(name, required) [X86_FORM_##name] = BUSTER_X86_TARGET_REST(required),
BUSTER_X86_TARGET_DATA const uint32_t x86_target_rest_requirements[X86_FORM_COUNT] __attribute__((aligned(64))) = {
#include "buster/compiler/x86_target_rows.c"
};
#undef BUSTER_X86_TARGET_EXTENDED
#undef BUSTER_X86_TARGET_REQUIREMENT

// Which blocks of 16 forms hold an extended row, a bit a block (block b at word b / 64, bit b % 64), ORed from the
// rows: the blocks x86_target_forms decides again by their rest words.
#define BUSTER_X86_TARGET_REQUIREMENT(name, required)
#define BUSTER_X86_TARGET_EXTENDED(name, required) | (X86_FORM_##name / 1024 == BUSTER_X86_TARGET_WORD ? UINT64_C(1) << (X86_FORM_##name / 16 % 64) : 0)
BUSTER_X86_TARGET_DATA const uint64_t x86_target_extended_blocks[BUSTER_X86_TARGET_BLOCK_WORDS] = {
#define BUSTER_X86_TARGET_WORD 0
    0
#include "buster/compiler/x86_target_rows.c"
    ,
#undef BUSTER_X86_TARGET_WORD
#define BUSTER_X86_TARGET_WORD 1
    0
#include "buster/compiler/x86_target_rows.c"
    ,
#undef BUSTER_X86_TARGET_WORD
#define BUSTER_X86_TARGET_WORD 2
    0
#include "buster/compiler/x86_target_rows.c"
    ,
#undef BUSTER_X86_TARGET_WORD
#define BUSTER_X86_TARGET_WORD 3
    0
#include "buster/compiler/x86_target_rows.c"
    ,
#undef BUSTER_X86_TARGET_WORD
#define BUSTER_X86_TARGET_WORD 4
    0
#include "buster/compiler/x86_target_rows.c"
    ,
#undef BUSTER_X86_TARGET_WORD
#define BUSTER_X86_TARGET_WORD 5
    0
#include "buster/compiler/x86_target_rows.c"
    ,
#undef BUSTER_X86_TARGET_WORD
#define BUSTER_X86_TARGET_WORD 6
    0
#include "buster/compiler/x86_target_rows.c"
    ,
#undef BUSTER_X86_TARGET_WORD
#define BUSTER_X86_TARGET_WORD 7
    0
#include "buster/compiler/x86_target_rows.c"
    ,
#undef BUSTER_X86_TARGET_WORD
#define BUSTER_X86_TARGET_WORD 8
    0
#include "buster/compiler/x86_target_rows.c"
    ,
#undef BUSTER_X86_TARGET_WORD
#define BUSTER_X86_TARGET_WORD 9
    0
#include "buster/compiler/x86_target_rows.c"
    ,
#undef BUSTER_X86_TARGET_WORD
#define BUSTER_X86_TARGET_WORD 10
    0
#include "buster/compiler/x86_target_rows.c"
    ,
#undef BUSTER_X86_TARGET_WORD
#define BUSTER_X86_TARGET_WORD 11
    0
#include "buster/compiler/x86_target_rows.c"
    ,
#undef BUSTER_X86_TARGET_WORD
};
#undef BUSTER_X86_TARGET_EXTENDED
#undef BUSTER_X86_TARGET_REQUIREMENT

#define BUSTER_X86_TARGET_REQUIREMENT(name, required) [X86_FORM_##name] = BUSTER_X86_TARGET_SLOT_##name + 1,
#define BUSTER_X86_TARGET_EXTENDED(name, required) BUSTER_X86_TARGET_REQUIREMENT(name, required)
BUSTER_X86_TARGET_DATA const uint16_t x86_target_requirement_slots[X86_FORM_COUNT] __attribute__((aligned(64))) = {
#include "buster/compiler/x86_target_rows.c"
};
#undef BUSTER_X86_TARGET_EXTENDED
#undef BUSTER_X86_TARGET_REQUIREMENT

#define BUSTER_X86_TARGET_REQUIREMENT(name, required) X86_FORM_##name,
#define BUSTER_X86_TARGET_EXTENDED(name, required) BUSTER_X86_TARGET_REQUIREMENT(name, required)
BUSTER_X86_TARGET_DATA const uint16_t x86_target_classified_forms[BUSTER_X86_TARGET_CLASSIFIED_COUNT] = {
#include "buster/compiler/x86_target_rows.c"
};
#undef BUSTER_X86_TARGET_EXTENDED
#undef BUSTER_X86_TARGET_REQUIREMENT

#define BUSTER_X86_TARGET_REQUIREMENT(name, required) (required),
#define BUSTER_X86_TARGET_EXTENDED(name, required) BUSTER_X86_TARGET_REQUIREMENT(name, required)
BUSTER_X86_TARGET_DATA const uint64_t x86_target_classified_requirements[BUSTER_X86_TARGET_CLASSIFIED_COUNT] = {
#include "buster/compiler/x86_target_rows.c"
};
#undef BUSTER_X86_TARGET_EXTENDED
#undef BUSTER_X86_TARGET_REQUIREMENT

// Extending the canonical policy outside the projection must fail at build time, never silently drop a feature.
// Update the projection deliberately before adding such a row; the exhaustive tests also check every round trip.
#define BUSTER_X86_TARGET_REQUIREMENT(name, required) _Static_assert(((required) & ~BUSTER_X86_TARGET_COMPACT_FEATURES) == 0, "target requirement cannot be compacted: " #name); _Static_assert(((required) & BUSTER_X86_TARGET_KNOWN) != 0, "explicit target row must be classified: " #name);
// An extended row needs a feature past the projection (a compactable one belongs to the vector loop) and comes after
// every compact row, where the exact pass reads it.
#define BUSTER_X86_TARGET_EXTENDED(name, required) _Static_assert(((required) & ~BUSTER_X86_TARGET_COMPACT_FEATURES) != 0, "extended target row is compactable: " #name); _Static_assert(((required) & BUSTER_X86_TARGET_KNOWN) != 0, "explicit target row must be classified: " #name); _Static_assert(BUSTER_X86_TARGET_SLOT_##name >= BUSTER_X86_TARGET_COMPACT_COUNT, "extended target rows come after the compact rows: " #name);
#include "buster/compiler/x86_target_rows.c"
#undef BUSTER_X86_TARGET_EXTENDED
#undef BUSTER_X86_TARGET_REQUIREMENT
#endif
_Static_assert(BUSTER_ARRAY_LENGTH(x86_target_classified_forms) == BUSTER_ARRAY_LENGTH(x86_target_classified_requirements), "one full-width requirement per classified form");
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

// Sixteen contiguous 32-bit rows per iteration: no gathers, scratch allocation or speculative tail loads.
// Unknown rows stay zero, so the nonzero test still distinguishes them from classified baseline rows. An extended
// row's mark is in every target's unavailable bits, so the loop denies it; the blocks with one
// (x86_target_extended_blocks, 29 of 523 on 2026-11-24) are then decided again by both words, after the loop, which a
// branch inside it made 35% slower on mispredictions and reading every block's rest word 25% slower.
static void x86_target_forms(const X86Target* target, uint8_t* allowed)
{
    const uint64_t available = target->features | BUSTER_X86_TARGET_KNOWN;
    const __m512i unavailable = _mm512_set1_epi32((int)~x86_target_compact_features(available));
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
    // The blocks with an extended row again, every lane by both words (the mark left out): their 16 rows' rest words
    // are 0 but for the extended rows'.
    const __m512i unavailable_unmarked = _mm512_set1_epi32((int)(~x86_target_compact_features(available) & ~BUSTER_X86_TARGET_COMPACT_EXTENDED));
    const __m512i unavailable_rest = _mm512_set1_epi32((int)~BUSTER_X86_TARGET_REST(available));
    for (uint32_t word = 0; word < BUSTER_X86_TARGET_BLOCK_WORDS; word += 1)
    {
        uint64_t blocks = x86_target_extended_blocks[word];
        while (blocks != 0)
        {
            const uint32_t i = (word * 64 + (uint32_t)__builtin_ctzll(blocks)) * 16;
            const uint32_t count = X86_FORM_COUNT - i < 16 ? X86_FORM_COUNT - i : 16;
            const __mmask16 active = (__mmask16)((1u << count) - 1);
            const __m512i required = _mm512_maskz_loadu_epi32(active, x86_target_compact_requirements + i);
            const __m512i rest = _mm512_maskz_loadu_epi32(active, x86_target_rest_requirements + i);
            const __m512i missing = _mm512_or_si512(_mm512_and_si512(required, unavailable_unmarked), _mm512_and_si512(rest, unavailable_rest));
            const __mmask16 supported = _mm512_mask_testn_epi32_mask(_mm512_cmpneq_epi32_mask(required, zero), missing, missing);
            _mm_mask_storeu_epi8(allowed + i, active, _mm_maskz_set1_epi8(supported, 1));
            blocks &= blocks - 1;
        }
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
