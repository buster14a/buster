// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Tier-neutral source/frame events and their final-layout resolution (#1056). No Tier0Code, frame-size convention,
// prologue/epilogue pattern or allocation is part of this interface. Include after host_isa.h for the vector path.
//
// The producer owns immutable event arrays. The final encoder/layout owns exact record starts and ends: an alignment
// gap belongs to neither adjacent instruction. At boundary b, LEFT means after record b-1 (0 at b=0), RIGHT means
// before record b (the function's size at b=count). A frame change takes effect at LEFT, before any following padding;
// a source statement normally starts at RIGHT. Zero-width records and the end boundary are deliberate, valid cases.
//
// emission_metadata_check checks the bounded protocol, NOT the truth of unwind rules about machine instructions or
// control flow. A backend must independently prove those facts before publication. In particular, balanced CFI state
// operations are not a CFG proof. The current driver and fixed-frame writer are not migrated by this module.
//
// Count/place/fill: emission_metadata_plan validates and publishes required counts; the caller reserves disjoint
// destination slices; emission_metadata_resolve validates again before any store, fills and publishes the result last.
// Inputs must remain unchanged between the check and fill and must not overlap destinations, their descriptors or the
// result. The two destinations are disjoint too. No padding, speculative store, allocation or per-entry source map is
// required. A failed call preserves every destination byte and its result descriptor. Omit this path when metadata
// was not requested. A zero source count means no source metadata, not a synthesized source statement.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(BUSTER_HOST_VECTOR) && BUSTER_HOST_VECTOR
#include <immintrin.h>
#endif

#define BUSTER_EMISSION_NO_ORIGIN UINT32_MAX
#define BUSTER_EMISSION_MAX_REGISTERS 65536u
#define BUSTER_EMISSION_LAYOUT_LANES 16u

typedef enum EmissionError
{
    EMISSION_OK,
    EMISSION_INVALID,
    EMISSION_CAPACITY,
} EmissionError;

typedef enum EmissionPart
{
    EMISSION_NONE,
    EMISSION_DESCRIPTOR,
    EMISSION_LAYOUT,
    EMISSION_SOURCE,
    EMISSION_FRAME,
    EMISSION_DESTINATION,
} EmissionPart;

// On success, part is EMISSION_NONE and index is 0. Descriptor and destination errors report index 0; layout, source
// and frame errors report the first failing record or event, and an unbalanced final state stack reports frame_count.
typedef struct EmissionCheck EmissionCheck;
struct EmissionCheck
{
    EmissionError error;
    EmissionPart part;
    uint32_t index;
};

typedef enum EmissionSide
{
    EMISSION_LEFT,
    EMISSION_RIGHT,
} EmissionSide;

typedef struct EmissionAnchor EmissionAnchor;
struct EmissionAnchor
{
    uint32_t boundary;
    uint32_t side;
};

typedef struct EmissionLayout EmissionLayout;
struct EmissionLayout
{
    const uint32_t* starts;
    const uint32_t* ends;
    uint32_t count;
    uint32_t size;
};

typedef enum EmissionSourceKind
{
    EMISSION_SOURCE_ORIGIN,
    EMISSION_SOURCE_GENERATED,
} EmissionSourceKind;

typedef struct EmissionSourceEvent EmissionSourceEvent;
struct EmissionSourceEvent
{
    EmissionAnchor at;
    uint32_t origin;
    uint32_t kind;
};

// Register numbers are the output ABI's unwind-register numbers, not an encoder's register numbering. RESTORE means
// the CIE's initial rule, SAME_VALUE means the current register, and REGISTER names the holding register in value.
// CFA/CFA_OFFSET and OFFSET carry signed byte offsets. This protocol does not assume eight-byte slots. A target
// writer may still refuse a rule it cannot encode. No expression or target-specific opcode is accepted implicitly.
typedef enum EmissionFrameKind
{
    EMISSION_FRAME_CFA,
    EMISSION_FRAME_CFA_OFFSET,
    EMISSION_FRAME_OFFSET,
    EMISSION_FRAME_REGISTER,
    EMISSION_FRAME_RESTORE,
    EMISSION_FRAME_SAME_VALUE,
    EMISSION_FRAME_UNDEFINED,
    EMISSION_FRAME_REMEMBER_STATE,
    EMISSION_FRAME_RESTORE_STATE,
} EmissionFrameKind;

typedef struct EmissionFrameEvent EmissionFrameEvent;
struct EmissionFrameEvent
{
    EmissionAnchor at;
    int32_t value;
    uint16_t reg;
    uint8_t kind;
    uint8_t reserved;
};

typedef struct EmissionMetadata EmissionMetadata;
struct EmissionMetadata
{
    const EmissionSourceEvent* sources;
    const EmissionFrameEvent* frames;
    uint32_t source_count;
    uint32_t frame_count;
    // Origin IDs index the producer's source-origin table; UINT32_MAX is reserved for generated code.
    uint32_t origin_count;
    // The target's unwind-register namespace. Required only when frame events are present.
    uint32_t register_count;
};

typedef struct EmissionMetadataPlan EmissionMetadataPlan;
struct EmissionMetadataPlan
{
    uint32_t source_count;
    uint32_t frame_count;
};

typedef struct EmissionSourceRow EmissionSourceRow;
struct EmissionSourceRow
{
    uint32_t offset;
    uint32_t origin;
    uint32_t kind;
};

typedef struct EmissionFrameRow EmissionFrameRow;
struct EmissionFrameRow
{
    uint32_t offset;
    int32_t value;
    uint16_t reg;
    uint8_t kind;
    uint8_t reserved;
};

typedef struct EmissionStorage EmissionStorage;
struct EmissionStorage
{
    EmissionSourceRow* sources;
    EmissionFrameRow* frames;
    uint32_t source_capacity;
    uint32_t frame_capacity;
};

typedef struct EmissionResolved EmissionResolved;
struct EmissionResolved
{
    const EmissionSourceRow* sources;
    const EmissionFrameRow* frames;
    uint32_t source_count;
    uint32_t frame_count;
    uint32_t size;
};

static EmissionCheck emission_layout_check_scalar(const EmissionLayout* layout)
{
    EmissionCheck check = {
        .error = EMISSION_INVALID,
        .part = EMISSION_LAYOUT,
    };
    if (layout != NULL && (layout->count == 0 || (layout->starts != NULL && layout->ends != NULL)))
    {
        const uint32_t count = layout->count;
        const uint32_t size = layout->size;
        const uint32_t* const starts = layout->starts;
        const uint32_t* const ends = layout->ends;
        uint32_t previous = 0;
        check.error = EMISSION_OK;
        check.part = EMISSION_NONE;
        for (uint32_t i = 0; check.error == EMISSION_OK && i < count; i += 1)
        {
            const uint32_t start = starts[i];
            const uint32_t end = ends[i];
            if (start < previous || end < start || end > size)
            {
                check.error = EMISSION_INVALID;
                check.part = EMISSION_LAYOUT;
                check.index = i;
            }
            previous = end;
        }
    }
    return check;
}

// Contiguous validation only: sparse event-to-record resolution below uses scalar loads rather than AVX-512 gathers.
// The same first invalid record as the exact path, including errors at a batch boundary. Masked tails need no slack.
static EmissionCheck emission_layout_check(const EmissionLayout* layout, bool scalar)
{
    EmissionCheck check;
#if defined(BUSTER_HOST_VECTOR) && BUSTER_HOST_VECTOR
    if (scalar || layout == NULL || (layout->count != 0 && (layout->starts == NULL || layout->ends == NULL)))
    {
        check = emission_layout_check_scalar(layout);
    }
    else
    {
        const uint32_t count = layout->count;
        const uint32_t size = layout->size;
        const uint32_t* const starts = layout->starts;
        const uint32_t* const ends = layout->ends;
        const __m512i limit = _mm512_set1_epi32((int32_t)size);
        uint32_t previous = 0;
        check = (EmissionCheck){
            .error = EMISSION_OK,
            .part = EMISSION_NONE,
        };
        for (uint64_t base = 0; check.error == EMISSION_OK && base < count; base += BUSTER_EMISSION_LAYOUT_LANES)
        {
            const uint32_t take = count - base < BUSTER_EMISSION_LAYOUT_LANES ? (uint32_t)(count - base) : BUSTER_EMISSION_LAYOUT_LANES;
            const __mmask16 valid = (__mmask16)((1u << take) - 1u);
            const __m512i start = _mm512_maskz_loadu_epi32(valid, starts + base);
            const __m512i end = _mm512_maskz_loadu_epi32(valid, ends + base);
            const __m512i before = _mm512_mask_mov_epi32(_mm512_alignr_epi32(end, end, 15), 1, _mm512_set1_epi32((int32_t)previous));
            const __mmask16 bad = _mm512_mask_cmplt_epu32_mask(valid, start, before) |
                                  _mm512_mask_cmplt_epu32_mask(valid, end, start) |
                                  _mm512_mask_cmpgt_epu32_mask(valid, end, limit);
            if (bad != 0)
            {
                check.error = EMISSION_INVALID;
                check.part = EMISSION_LAYOUT;
                check.index = (uint32_t)base + (uint32_t)__builtin_ctz((uint32_t)bad);
            }
            else
            {
                previous = ends[base + take - 1];
            }
        }
    }
#else
    (void)scalar;
    check = emission_layout_check_scalar(layout);
#endif
    return check;
}

static bool emission_anchor_valid(EmissionAnchor at, uint32_t count)
{
    return at.boundary <= count && at.side <= EMISSION_RIGHT;
}

static bool emission_anchor_before(EmissionAnchor a, EmissionAnchor b)
{
    return a.boundary < b.boundary || (a.boundary == b.boundary && a.side < b.side);
}

// Called only after the layout and this anchor have been checked. Never reads a count+1 sentinel or an empty slice.
static uint32_t emission_anchor_offset(const EmissionLayout* layout, EmissionAnchor at)
{
    uint32_t offset;
    if (at.side == EMISSION_LEFT)
    {
        offset = at.boundary == 0 ? 0 : layout->ends[at.boundary - 1];
    }
    else
    {
        offset = at.boundary == layout->count ? layout->size : layout->starts[at.boundary];
    }
    return offset;
}

static EmissionCheck emission_metadata_check(const EmissionMetadata* metadata, const EmissionLayout* layout, bool scalar)
{
    EmissionCheck check = {
        .error = EMISSION_INVALID,
        .part = EMISSION_DESCRIPTOR,
    };
    if (metadata != NULL)
    {
        const uint32_t sources = metadata->source_count;
        const uint32_t frames = metadata->frame_count;
        const uint32_t origins = metadata->origin_count;
        const uint32_t registers = metadata->register_count;
        const EmissionSourceEvent* const source_events = metadata->sources;
        const EmissionFrameEvent* const frame_events = metadata->frames;
        if ((sources == 0 || source_events != NULL) && (frames == 0 || frame_events != NULL) &&
            registers <= BUSTER_EMISSION_MAX_REGISTERS && (frames == 0 || registers != 0))
        {
            check = emission_layout_check(layout, scalar);
            const uint32_t count = check.error == EMISSION_OK ? layout->count : 0;
            EmissionAnchor previous = { 0 };
            for (uint32_t i = 0; check.error == EMISSION_OK && i < sources; i += 1)
            {
                const EmissionSourceEvent event = source_events[i];
                const bool origin = event.kind == EMISSION_SOURCE_ORIGIN && event.origin < origins && event.origin != BUSTER_EMISSION_NO_ORIGIN;
                const bool generated = event.kind == EMISSION_SOURCE_GENERATED && event.origin == BUSTER_EMISSION_NO_ORIGIN;
                // A present source map explicitly describes the start, including a generated prologue or leading padding.
                const bool order = i == 0 ? event.at.boundary == 0 && event.at.side == EMISSION_LEFT : emission_anchor_before(previous, event.at);
                if (!emission_anchor_valid(event.at, count) || !order || (!origin && !generated))
                {
                    check = (EmissionCheck){
                        .error = EMISSION_INVALID,
                        .part = EMISSION_SOURCE,
                        .index = i,
                    };
                }
                previous = event.at;
            }
            previous = (EmissionAnchor){ 0 };
            uint32_t depth = 0;
            for (uint32_t i = 0; check.error == EMISSION_OK && i < frames; i += 1)
            {
                const EmissionFrameEvent event = frame_events[i];
                bool valid = emission_anchor_valid(event.at, count) && !emission_anchor_before(event.at, previous) && event.reserved == 0;
                switch (event.kind)
                {
                    case EMISSION_FRAME_CFA:
                    case EMISSION_FRAME_OFFSET:
                    {
                        valid &= event.reg < registers;
                    } break;
                    case EMISSION_FRAME_CFA_OFFSET:
                    {
                        valid &= event.reg == 0;
                    } break;
                    case EMISSION_FRAME_REGISTER:
                    {
                        valid &= event.reg < registers && event.value >= 0 && (uint32_t)event.value < registers;
                    } break;
                    case EMISSION_FRAME_RESTORE:
                    case EMISSION_FRAME_SAME_VALUE:
                    case EMISSION_FRAME_UNDEFINED:
                    {
                        valid &= event.reg < registers && event.value == 0;
                    } break;
                    case EMISSION_FRAME_REMEMBER_STATE:
                    {
                        valid &= event.reg == 0 && event.value == 0;
                        depth += 1;
                    } break;
                    case EMISSION_FRAME_RESTORE_STATE:
                    {
                        valid &= event.reg == 0 && event.value == 0 && depth != 0;
                        depth -= depth != 0;
                    } break;
                    default:
                    {
                        valid = false;
                    } break;
                }
                if (!valid)
                {
                    check = (EmissionCheck){
                        .error = EMISSION_INVALID,
                        .part = EMISSION_FRAME,
                        .index = i,
                    };
                }
                previous = event.at;
            }
            if (check.error == EMISSION_OK && depth != 0)
            {
                check = (EmissionCheck){
                    .error = EMISSION_INVALID,
                    .part = EMISSION_FRAME,
                    .index = frames,
                };
            }
            if (check.error == EMISSION_OK)
            {
                check.part = EMISSION_NONE;
            }
        }
    }
    return check;
}

static EmissionCheck emission_metadata_plan(const EmissionMetadata* metadata, const EmissionLayout* layout, bool scalar, EmissionMetadataPlan* result)
{
    EmissionCheck check = {
        .error = EMISSION_INVALID,
        .part = EMISSION_DESTINATION,
    };
    if (result != NULL)
    {
        check = emission_metadata_check(metadata, layout, scalar);
        if (check.error == EMISSION_OK)
        {
            *result = (EmissionMetadataPlan){
                .source_count = metadata->source_count,
                .frame_count = metadata->frame_count,
            };
        }
    }
    return check;
}

static EmissionCheck emission_metadata_resolve(const EmissionMetadata* metadata, const EmissionLayout* layout, const EmissionStorage* storage,
                                               bool scalar, EmissionResolved* result)
{
    EmissionCheck check = {
        .error = EMISSION_INVALID,
        .part = EMISSION_DESTINATION,
    };
    if (storage != NULL && result != NULL)
    {
        check = emission_metadata_check(metadata, layout, scalar);
        if (check.error == EMISSION_OK)
        {
            const uint32_t source_count = metadata->source_count;
            const uint32_t frame_count = metadata->frame_count;
            const EmissionSourceEvent* const source_events = metadata->sources;
            const EmissionFrameEvent* const frame_events = metadata->frames;
            EmissionSourceRow* const sources = storage->sources;
            EmissionFrameRow* const frames = storage->frames;
            if (source_count > storage->source_capacity || frame_count > storage->frame_capacity)
            {
                check = (EmissionCheck){
                    .error = EMISSION_CAPACITY,
                    .part = EMISSION_DESTINATION,
                };
            }
            else if ((source_count != 0 && sources == NULL) || (frame_count != 0 && frames == NULL))
            {
                check = (EmissionCheck){
                    .error = EMISSION_INVALID,
                    .part = EMISSION_DESTINATION,
                };
            }
            else
            {
                for (uint32_t i = 0; i < source_count; i += 1)
                {
                    const EmissionSourceEvent event = source_events[i];
                    sources[i] = (EmissionSourceRow){
                        .offset = emission_anchor_offset(layout, event.at),
                        .origin = event.origin,
                        .kind = event.kind,
                    };
                }
                for (uint32_t i = 0; i < frame_count; i += 1)
                {
                    const EmissionFrameEvent event = frame_events[i];
                    frames[i] = (EmissionFrameRow){
                        .offset = emission_anchor_offset(layout, event.at),
                        .value = event.value,
                        .reg = event.reg,
                        .kind = event.kind,
                    };
                }
                *result = (EmissionResolved){
                    .sources = source_count != 0 ? sources : NULL,
                    .frames = frame_count != 0 ? frames : NULL,
                    .source_count = source_count,
                    .frame_count = frame_count,
                    .size = layout->size,
                };
            }
        }
    }
    return check;
}
