// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Independent per-body name resolution over CheckInput's retained history (#113). Include after
// c_check_environment.c. c_body_bindings_span prepares a body's event slice and undo-base once; workers then call
// c_body_bindings_resolve with private arenas and (for scalar replay) a private reusable CBindingReplay. All source
// arrays and the file index stay immutable. A missing local binding falls back at the original use's token position.
//
// CheckInput reactivates parameters at OPEN_BODY and restores the enclosing file mark at closing-brace + 1. The
// slice includes declarations at OPEN_BODY (the function itself), parameter reactivations and the final restore.
// Its original undo marks differ from private marks by the file activations preceding that slice. Preserve the
// function's declaration in the private view too: this permits borrowing every other event column unchanged.
// Two bounded SIMD passes count/classify events and translate undo/end columns; there are exact scalar twins.
//
// This consumes already elaborated source events and declaration TypeIds. It does not defer declaration/type
// elaboration, create private types, reconcile linkage effects or check expression/statement semantics. Those
// consumers must preserve this interface's source identities and output slots while completing #113 acceptance.

typedef enum CBodyBindingStatus
{
    C_BODY_BINDINGS_INVALID,
    C_BODY_BINDINGS_OK,
    C_BODY_BINDINGS_RESOURCE,
} CBodyBindingStatus;

typedef struct CBodyBindingSpan CBodyBindingSpan;
struct CBodyBindingSpan
{
    uint32_t begin;
    uint32_t end;
    uint32_t base;
    uint32_t uses;
    uint32_t declarations;
    uint32_t slot;
};

typedef struct CBodyBindings CBodyBindings;
struct CBodyBindings
{
    uint32_t* tokens;
    uint32_t* declarations;
    uint32_t count;
    uint32_t file_queries;
    uint64_t retained_bytes;
    CBindingStatistics statistics;
    CBodyBindingStatus status;
};

static uint32_t c_body_bindings_bound(const CBindings* bindings, uint32_t point, bool after)
{
    uint32_t begin = 0;
    uint32_t end = bindings->count;
    while (begin < end)
    {
        const uint32_t middle = begin + (end - begin) / 2;
        if (bindings->point[middle] < point || (after && bindings->point[middle] == point))
        {
            begin = middle + 1;
        }
        else
        {
            end = middle;
        }
    }
    return begin;
}

// Prepare independent job metadata once before scheduling. No allocation or input padding is required. Scalar
// binary searches find the two ordered position boundaries; event classification is vectorized across the slice.
// A zeroed span is invalid. A successful span belongs to this exact unchanged environment until all readers join.
static bool c_body_bindings_span(CBodyBindingSpan* span, const CheckEnvironment* environment, uint32_t slot, bool scalar)
{
    *span = (CBodyBindingSpan){ 0 };
    const CBindings* source = environment->bindings;
    bool valid = environment->valid && source != 0 && source->valid && slot < environment->body_count;
    const CBodyRange body = valid ? environment->bodies[slot] : (CBodyRange){ 0 };
    valid = valid && body.begin < body.end && body.end <= environment->token_count;
    const uint32_t first = valid ? c_body_bindings_bound(source, body.begin, false) : 0;
    uint32_t end = valid ? c_body_bindings_bound(source, body.end, true) : 0;
    uint32_t close = end;
    bool restored = false;
    while (valid && close > first && source->point[close - 1] == body.end && !restored)
    {
        close -= 1;
        restored = source->operation[close] == C_BINDING_RESTORE;
    }
    valid &= restored;
    end = restored ? close + 1 : 0;
    uint32_t file = 0;
    uint32_t declarations = 0;
    uint32_t uses = 0;
    if (scalar)
    {
        for (uint32_t event = first; valid && event < end; event += 1)
        {
            const bool declaration = source->operation[event] == C_BINDING_DECLARE;
            const bool global = declaration && source->depth[event] == 0;
            declarations += declaration;
            uses += source->operation[event] == C_BINDING_USE;
            file += global;
            valid &= !global || source->point[event] == body.begin;
        }
    }
    else
    {
        for (uint64_t block = first; valid && block < end; block += 16)
        {
            const uint32_t live = end - block < 16 ? (uint32_t)(end - block) : 16;
            const __mmask16 mask = (__mmask16)((1u << live) - 1);
            const __m128i operations = _mm_maskz_loadu_epi8(mask, source->operation + block);
            const __mmask16 declared = _mm_mask_cmpeq_epi8_mask(mask, operations, _mm_set1_epi8(C_BINDING_DECLARE));
            const __mmask16 used = _mm_mask_cmpeq_epi8_mask(mask, operations, _mm_set1_epi8(C_BINDING_USE));
            const __m512i depth = _mm512_maskz_loadu_epi32(declared, source->depth + block);
            const __mmask16 global = _mm512_mask_cmpeq_epi32_mask(declared, depth, _mm512_setzero_si512());
            const __m512i points = _mm512_maskz_loadu_epi32(global, source->point + block);
            valid &= _mm512_mask_cmpneq_epu32_mask(global, points, _mm512_set1_epi32((int)body.begin)) == 0;
            declarations += (uint32_t)__builtin_popcount((uint32_t)declared);
            uses += (uint32_t)__builtin_popcount((uint32_t)used);
            file += (uint32_t)__builtin_popcount((uint32_t)global);
        }
    }
    valid = valid && first < end && source->declaration[close] >= file;
    if (valid)
    {
        *span = (CBodyBindingSpan)
        {
            .begin = first,
            .end = end,
            .base = source->declaration[close] - file,
            .uses = uses,
            .declarations = declarations,
            .slot = slot,
        };
    }
    return valid;
}

// Borrow key/point/operation/kind/depth directly. Only restore payloads and declaration-end event indexes need
// translation into the private slice. Never read the unspecified end entries of use/restore events.
static bool c_body_bindings_view(CBindings* view, const CBindings* source, const CBodyBindingSpan* span, Arena* scratch, bool scalar)
{
    const uint32_t count = span->end - span->begin;
    *view = (CBindings)
    {
        .name_capacity = source->name_capacity,
        .capacity = count,
        .count = count,
        .key = source->key + span->begin,
        .point = source->point + span->begin,
        .depth = source->depth + span->begin,
        .operation = source->operation + span->begin,
        .kind = source->kind + span->begin,
    };
    view->declaration = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, count);
    view->end = view->declaration != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, count) : 0;
    bool valid = view->end != 0;
    if (scalar)
    {
        for (uint32_t event = 0; valid && event < count; event += 1)
        {
            const uint32_t original = span->begin + event;
            const uint8_t operation = source->operation[original];
            const uint32_t declaration = source->declaration[original];
            const uint32_t end = operation == C_BINDING_DECLARE ? source->end[original] : BUSTER_C_BINDING_NONE;
            valid &= operation != C_BINDING_RESTORE || declaration >= span->base;
            valid &= end == BUSTER_C_BINDING_NONE || (end >= span->begin && end < span->end);
            view->declaration[event] = operation == C_BINDING_RESTORE ? declaration - span->base : declaration;
            view->end[event] = end == BUSTER_C_BINDING_NONE ? end : end - span->begin;
        }
    }
    else
    {
        for (uint64_t block = 0; valid && block < count; block += 16)
        {
            const uint32_t live = count - block < 16 ? (uint32_t)(count - block) : 16;
            const __mmask16 mask = (__mmask16)((1u << live) - 1);
            const uint64_t original = span->begin + block;
            const __m128i operations = _mm_maskz_loadu_epi8(mask, source->operation + original);
            const __mmask16 restored = _mm_mask_cmpeq_epi8_mask(mask, operations, _mm_set1_epi8(C_BINDING_RESTORE));
            const __mmask16 declared = _mm_mask_cmpeq_epi8_mask(mask, operations, _mm_set1_epi8(C_BINDING_DECLARE));
            const __m512i declaration = _mm512_maskz_loadu_epi32(mask, source->declaration + original);
            const __m512i base = _mm512_set1_epi32((int)span->base);
            valid &= _mm512_mask_cmplt_epu32_mask(restored, declaration, base) == 0;
            _mm512_mask_storeu_epi32(view->declaration + block, mask, _mm512_mask_sub_epi32(declaration, restored, declaration, base));
            const __m512i ends = _mm512_mask_loadu_epi32(_mm512_set1_epi32(-1), declared, source->end + original);
            const __mmask16 known = _mm512_mask_cmpneq_epu32_mask(declared, ends, _mm512_set1_epi32(-1));
            const __m512i first = _mm512_set1_epi32((int)span->begin);
            valid &= _mm512_mask_cmplt_epu32_mask(known, ends, first) == 0 &&
                     _mm512_mask_cmpge_epu32_mask(known, ends, _mm512_set1_epi32((int)span->end)) == 0;
            _mm512_mask_storeu_epi32(view->end + block, mask, _mm512_mask_sub_epi32(ends, known, ends, first));
        }
    }
    view->valid = valid;
    return valid;
}

static bool c_body_bindings_globals(const CheckEnvironment* environment, const uint32_t* keys, const uint32_t* points,
                                    const uint32_t* destinations, uint32_t count, bool scalar, uint32_t* declarations)
{
    uint32_t handles[16];
    bool valid = true;
    if (scalar)
    {
        for (uint32_t i = 0; i < count; i += 1)
        {
            handles[i] = c_environment_lookup_key(&environment->file, keys[i], points[i]);
        }
    }
    else
    {
        valid = c_environment_lookup_batch(&environment->file, keys, points, count, handles);
    }
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        valid = handles[i] != 0;
        if (valid)
        {
            declarations[destinations[i]] = environment->bindings->declaration[handles[i] - 1];
        }
    }
    return valid;
}

// `span` must come from c_body_bindings_span for this unchanged environment. Scalar workers initialize one replay
// with this name capacity and the maximum span event count before calling; it is empty again after every call.
// Replay storage and all input columns precede the scratch/output entry marks. Arenas are distinct and private to
// the worker. Results own exactly count source tokens/declaration identities in use-event order, without padding.
// Allocation failure rolls back output; all calls release temporary views/results. A missing source binding fails
// closed because the published successful reader had resolved all these uses. No worker changes shared state.
static bool c_body_bindings_resolve(CBodyBindings* output, const CheckEnvironment* environment, const CBodyBindingSpan* span,
                                    bool scalar, CBindingReplay* replay, Arena* arena, Arena* scratch)
{
    *output = (CBodyBindings){ 0 };
    const uint64_t mark = arena->length;
    const uint64_t temporary = scratch->length;
    const CBindings* source = environment->bindings;
    bool valid = arena != scratch && environment->valid && source != 0 && source->valid && span->slot < environment->body_count &&
                 span->begin < span->end && span->end <= source->count;
    const uint32_t count = valid ? span->end - span->begin : 0;
    valid = valid && (!scalar || (replay != 0 && replay->heads != 0 && replay->name_capacity == source->name_capacity && replay->capacity >= count));
    CBodyBindingStatus status = C_BODY_BINDINGS_INVALID;
    CBindings view;
    if (valid)
    {
        status = C_BODY_BINDINGS_RESOURCE;
        valid = c_body_bindings_view(&view, source, span, scratch, scalar);
        if (!valid && view.end != 0)
        {
            status = C_BODY_BINDINGS_INVALID;
        }
    }
    uint32_t* resolved = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, count) : 0;
    valid = valid && resolved != 0;
    if (valid)
    {
        valid = scalar ? c_bindings_replay(&view, replay, resolved) : c_bindings_batch(&view, resolved, scratch, &output->statistics);
        if (scalar)
        {
            output->statistics.identifiers = (uint64_t)span->uses + span->declarations;
            output->statistics.declarations = span->declarations;
        }
    }
    output->tokens = valid && span->uses != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, span->uses) : 0;
    output->declarations = valid && span->uses != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, span->uses) : 0;
    valid = valid && (span->uses == 0 || (output->tokens != 0 && output->declarations != 0));
    uint32_t keys[16];
    uint32_t points[16];
    uint32_t destinations[16];
    uint32_t pending = 0;
    uint32_t written = 0;
    if (valid)
    {
        status = C_BODY_BINDINGS_INVALID;
    }
    for (uint32_t event = 0; valid && event < count; event += 1)
    {
        if (view.operation[event] == C_BINDING_USE)
        {
            valid = written < span->uses;
            if (valid)
            {
                const uint32_t handle = resolved[event];
                output->tokens[written] = view.declaration[event];
                if (handle != 0)
                {
                    output->declarations[written] = view.declaration[handle - 1];
                }
                else
                {
                    keys[pending] = view.key[event];
                    points[pending] = view.point[event];
                    destinations[pending++] = written;
                    output->file_queries += 1;
                }
                written += 1;
            }
        }
        if (valid && pending == 16)
        {
            valid = c_body_bindings_globals(environment, keys, points, destinations, pending, scalar, output->declarations);
            pending = 0;
        }
    }
    if (valid && pending != 0)
    {
        valid = c_body_bindings_globals(environment, keys, points, destinations, pending, scalar, output->declarations);
    }
    valid = valid && written == span->uses;
    if (valid)
    {
        output->count = written;
        output->retained_bytes = arena->length - mark;
        output->status = C_BODY_BINDINGS_OK;
    }
    else
    {
        arena_pop_to(arena, mark);
        *output = (CBodyBindings){ .status = status };
    }
    arena_pop_to(scratch, temporary);
    return valid;
}
