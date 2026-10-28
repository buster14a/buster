// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Compact #embed list items share the initializer's subobject cursor. A character-array run is one borrowed byte
// part, independently of resource length; brace elision splits runs only at aggregate boundaries. The preprocessor
// has already applied limit, offset and the optional token sequences. No terminator or implicit separator is added.

static bool check_init_embed(CheckInitWalker* walker, const uint8_t* bytes, uint32_t length, uint32_t token)
{
    uint32_t consumed = 0;
    bool excess = false;
    if (walker->repeat != NULL)
    {
        // A GNU range repeats only the first generated scalar, not the resource's entire comma-separated sequence.
        check_init_fail(walker, CHECK_INIT_UNSUPPORTED, token);
    }
    while (consumed < length && walker->result->status == CHECK_INIT_OK)
    {
        CheckInitFrame* frame = walker->frames + walker->depth - 1;
        CheckInitSelection selected = { 0 };
        if (!check_init_select(walker, frame, &selected))
        {
            if (frame->braced && walker->ignore_excess)
            {
                check_init_warning(walker, token);
                consumed = length;
                excess = true;
            }
            else
            {
                check_init_fail(walker, CHECK_INIT_INVALID, token);
            }
        }
        else if (!check_init_storage(walker, &selected))
        {
            check_init_fail(walker, CHECK_INIT_INVALID, token);
        }
        else if (check_init_aggregate(walker->types, selected.type))
        {
            check_init_used(walker, frame, token, true);
            if (frame->wrapper)
            {
                check_init_fail(walker, CHECK_INIT_INVALID, token);
            }
            else
            {
                check_init_push(walker, &selected, false, token);
            }
        }
        else
        {
            const TypeId element = type_unqualified(type_strip_aligned(walker->types, selected.type));
            const TypeId array = type_strip_aligned(walker->types, frame->type);
            const uint8_t kind = type_kind(walker->types, array);
            const bool contiguous = !frame->wrapper && (kind == BUSTER_TYPE_KIND_ARRAY || kind == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY);
            uint64_t count = contiguous ? length - consumed : 1;
            if (contiguous && kind == BUSTER_TYPE_KIND_ARRAY)
            {
                const uint64_t available = walker->types->types.extent[type_index(array)] - frame->next;
                count = count < available ? count : available;
            }
            // Raw resource bytes represent conversions to char, signed char and unsigned char on this target.
            // Other scalar types need element-wise conversion, and bit fields and volatile stores need their own
            // access semantics. Refuse those forms instead of treating their first byte as the complete resource.
            if (!check_init_character(element) || type_size(walker->types, selected.type) != 1 || selected.width != 0 ||
                (type_qualifiers(selected.type) & BUSTER_TYPE_VOLATILE) != 0)
            {
                check_init_fail(walker, CHECK_INIT_UNSUPPORTED, token);
            }
            else if (count == 0 || count - 1 > UINT64_MAX - frame->next || count > (UINT64_MAX - selected.offset) / 8)
            {
                check_init_fail(walker, CHECK_INIT_RESOURCE, token);
            }
            else
            {
                const CheckInitPart part = {
                    .kind = CHECK_INIT_BYTES,
                    .type = selected.type,
                    .node = BUSTER_CHECK_NONE,
                    .token = token,
                    .bit_offset = selected.offset,
                    .bytes = bytes + consumed,
                    .byte_count = count,
                };
                check_init_used(walker, frame, token, true);
                check_init_append(walker, &part);
                frame->next += count - 1;
                check_init_used(walker, frame, token, false);
                check_init_advance(walker);
                consumed += (uint32_t)count;
            }
        }
    }
    return excess;
}
