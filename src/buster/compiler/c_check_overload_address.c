// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Contextual addresses of C overload sets. Include from c_check_overload.c after declaration validation. The immutable chain and source-time
// context are the same as ordinary overload calls. A singleton is already an ordinary function designator: select
// it before conversions, including its availability constraint. Several distinct declarations need an expected
// function-pointer type and exactly one canonical whole-function match. C compatible enum/integer, old-style
// prototype and transparent-union relations do not select an address from several overloads.
//
// Installed Clang 23 diagnoses an unavailable singleton even in typeof or an unselected generic association, but
// accepts a contextual address selected from several overloads even when that selected declaration is unavailable.
// Preserve this observed distinction explicitly; the result still retains the selected attribute flags. Ordinary
// direct calls use check_overload_call and their normal availability rule. Never choose a latest declaration just
// because it supplies a convertible placeholder type. The caller commits the returned declaration/object only
// after the surrounding expression has obtained its actual context.

#ifndef BUSTER_CHECK_OVERLOAD_ADDRESS_IMPLEMENTATION
#define BUSTER_CHECK_OVERLOAD_ADDRESS_IMPLEMENTATION 1

static CheckOverloadResult check_overload_address(const TypeTable* table, const CheckOverloadCandidate* candidates, uint32_t count, uint32_t head,
                                                  TypeId target, const CheckOverloadContext* context, Arena* scratch)
{
    CheckOverloadResult result = check_overload_result(CHECK_OVERLOAD_INVALID);
    const uint64_t mark = scratch != 0 ? scratch->length : 0;
    bool valid = context != 0 && scratch != 0 && (count == 0 || candidates != 0) && (head == UINT32_MAX || head < count) &&
                 (target == 0 || type_is_valid(table, target));
    const uint32_t scope = valid && head != UINT32_MAX ? candidates[head].scope : 0;
    const uint32_t identity = valid && head != UINT32_MAX ? candidates[head].canonical : UINT32_MAX;
    uint32_t length = 0;
    bool multiple = false;
    for (uint32_t current = valid ? head : UINT32_MAX; valid && current != UINT32_MAX; current = candidates[current].previous)
    {
        const CheckOverloadCandidate* candidate = candidates + current;
        valid = check_overload_candidate_valid(table, candidate) && candidate->scope == scope &&
                (candidate->previous == UINT32_MAX || candidate->previous < current) &&
                (context->source_position == 0 || candidate->position <= context->source_position);
        multiple |= candidate->canonical != identity;
        length += 1;
    }
    uint32_t selected = valid && length != 0 && !multiple ? head : UINT32_MAX;
    TypeId function = 0;
    if (valid && multiple && target != 0 && check_action_kind(table, target) == BUSTER_TYPE_KIND_POINTER)
    {
        const TypeId pointee = table->types.base[type_index(type_strip_aligned(table, target))];
        if (check_action_kind(table, pointee) == BUSTER_TYPE_KIND_FUNCTION)
        {
            function = pointee;
        }
    }
    bool full = false;
    uint32_t matched = 0;
    if (valid && multiple && function != 0)
    {
        uint64_t capacity = 1;
        while (capacity < (uint64_t)length * 2)
        {
            capacity *= 2;
        }
        uint32_t* seen = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, capacity);
        full = seen == 0;
        if (!full)
        {
            memset(seen, 0xff, capacity * sizeof(*seen));
        }
        for (uint32_t current = head; !full && matched < 2 && current != UINT32_MAX; current = candidates[current].previous)
        {
            const CheckOverloadCandidate* candidate = candidates + current;
            uint64_t bucket = ((uint64_t)candidate->canonical * UINT64_C(11400714819323198485)) & (capacity - 1);
            while (seen[bucket] != UINT32_MAX && seen[bucket] != candidate->canonical)
            {
                bucket = (bucket + 1) & (capacity - 1);
            }
            if (seen[bucket] == UINT32_MAX)
            {
                seen[bucket] = candidate->canonical;
                const TypeAnswer same = type_same_canonical(table, candidate->type, function);
                full = same == TYPE_ANSWER_FULL;
                if (same == TYPE_ANSWER_YES)
                {
                    selected = current;
                    matched += 1;
                }
            }
        }
    }
    if (valid)
    {
        result.status = full ? CHECK_OVERLOAD_FULL : length == 0 ? CHECK_OVERLOAD_NO_MATCH :
                        multiple && (function == 0 || matched > 1) ? CHECK_OVERLOAD_AMBIGUOUS :
                        selected == UINT32_MAX ? CHECK_OVERLOAD_NO_MATCH : CHECK_OVERLOAD_OK;
        if (result.status == CHECK_OVERLOAD_OK)
        {
            const CheckOverloadCandidate* candidate = candidates + selected;
            result.candidate = selected;
            result.declaration = candidate->declaration;
            result.canonical = candidate->canonical;
            result.object = candidate->object;
            result.type = candidate->type;
            result.flags = candidate->flags;
            result.status = !multiple && (candidate->flags & BUSTER_CHECK_OVERLOAD_UNAVAILABLE) != 0 ? CHECK_OVERLOAD_UNAVAILABLE : CHECK_OVERLOAD_OK;
        }
    }
    if (scratch != 0)
    {
        if (context != 0 && context->scratch_peak != 0 && scratch->length > *context->scratch_peak)
        {
            *context->scratch_peak = scratch->length;
        }
        arena_pop_to(scratch, mark);
    }
    return result;
}

#endif
